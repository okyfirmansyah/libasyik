/**
 * bench_beast.cpp — Direct Boost.Beast async HTTP server
 *
 * Purpose:
 *   This benchmark establishes the theoretical performance ceiling for the
 *   Boost.Beast/Asio stack — the same C++ async foundation that powers
 *   libasyik — but driven directly, without any libasyik wrapper layers:
 *     · No Boost.Fiber cooperative scheduler
 *     · No route-matching engine
 *     · No request/response abstraction objects
 *     · No service lifetime management
 *
 *   Comparing GIN < libasyik < Beast shows exactly how much overhead each
 *   abstraction layer adds, and how much headroom libasyik still has.
 *
 * Architecture (mirrors bench_server exactly):
 *   N threads (default = nCPU), each owning one io_context and one acceptor
 *   bound to the same address:port via SO_REUSEPORT.  The kernel distributes
 *   accepted connections.  Threads are fully independent — no shared state,
 *   no mutexes, no strands needed (single-threaded io_context per thread).
 *
 * Endpoints (identical to libasyik bench_server and GIN bench):
 *   GET  /plaintext      → "Hello, World!"          (text/plain)
 *   GET  /json           → {"message":"Hello, World!"}  (application/json)
 *   POST /echo           → echoes request body       (application/json)
 *   GET  /delay/<int>    → async timer N ms, {"ok":true}
 *
 * Usage:
 *   ./bench_beast [port]              default port = 8084
 *
 * Env vars:
 *   BEAST_THREAD_MULTIPLIER=N         thread count (default: nCPU)
 */

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

// ── Pre-built constant response bodies (no per-request heap allocation)
// ────────
static const std::string PLAINTEXT_BODY = "Hello, World!";
static const std::string JSON_BODY = R"({"message":"Hello, World!"})";
static const std::string DELAY_BODY = R"({"ok":true})";

// ══════════════════════════════════════════════════════════════════════════════
// session — owns one TCP connection, runs a keep-alive read/write loop.
//
// Lifetime is managed by shared_ptr: each async operation holds a copy via
// shared_from_this(), so the session stays alive until all pending handlers
// have fired and released their copies.
// ══════════════════════════════════════════════════════════════════════════════
class session : public std::enable_shared_from_this<session> {
  beast::tcp_stream stream_;
  beast::flat_buffer
      buffer_;  // reused across requests; retains pipelined leftovers
  http::request<http::string_body> req_;

 public:
  explicit session(tcp::socket sock) : stream_(std::move(sock)) {}

  void start() { do_read(); }

 private:
  // ── 1. Read the next HTTP request ─────────────────────────────────────────
  void do_read()
  {
    req_ = {};  // reset state; buffer_ is intentionally kept (pipelined data)
    stream_.expires_after(std::chrono::seconds(30));
    http::async_read(
        stream_, buffer_, req_,
        beast::bind_front_handler(&session::on_read, shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t /*bytes_transferred*/)
  {
    // Client sent FIN → clean shutdown
    if (ec == http::error::end_of_stream) {
      do_close();
      return;
    }
    // Any other error: drop the connection silently
    if (ec) return;
    dispatch();
  }

  // ── 2. Route to the correct handler ───────────────────────────────────────
  void dispatch()
  {
    // Strip query string so "/plaintext?foo=bar" still matches
    std::string target(req_.target());
    auto qpos = target.find('?');
    if (qpos != std::string::npos) target.resize(qpos);

    const auto verb = req_.method();

    // Scenario A: GET /plaintext
    if (verb == http::verb::get && target == "/plaintext") {
      respond(http::status::ok, "text/plain", PLAINTEXT_BODY);

      // Scenario B: GET /json
    } else if (verb == http::verb::get && target == "/json") {
      respond(http::status::ok, "application/json", JSON_BODY);

      // Scenario C: POST /echo
    } else if (verb == http::verb::post && target == "/echo") {
      // req_.body() is a std::string; copy into response body
      respond(http::status::ok, "application/json", req_.body());

      // Scenario D: GET /delay/<ms>
    } else if (verb == http::verb::get && target.size() > 7 &&
               target.compare(0, 7, "/delay/") == 0) {
      int ms = 0;
      auto digits = target.substr(7);
      std::from_chars(digits.data(), digits.data() + digits.size(), ms);
      if (ms < 0) ms = 0;
      if (ms > 10000) ms = 10000;
      do_delay(ms);

    } else {
      respond(http::status::not_found, "text/plain", "Not Found");
    }
  }

  // ── 3a. /delay handler: async timer, then respond ─────────────────────────
  void do_delay(int ms)
  {
    // Allocate the timer on the heap so it outlives this stack frame.
    // It shares the session's io_context executor via stream_.get_executor().
    auto timer = std::make_shared<asio::steady_timer>(
        stream_.get_executor(), std::chrono::milliseconds(ms));

    timer->async_wait(
        [self = shared_from_this(), timer](beast::error_code /*ec*/) {
          self->respond(http::status::ok, "application/json", DELAY_BODY);
        });
  }

  // ── 3b. Build and send a response, then loop or close ─────────────────────
  void respond(http::status status, std::string_view content_type,
               const std::string& body)
  {
    // Heap-allocate so the response outlives the async_write call.
    // shared_ptr is captured in the completion handler below.
    auto res = std::make_shared<http::response<http::string_body>>(
        status, req_.version());
    res->set(http::field::server, "Beast/bench");
    res->set(http::field::content_type, content_type);
    res->keep_alive(
        req_.keep_alive());  // honour Connection: keep-alive / close
    res->body() = body;
    res->prepare_payload();  // fills Content-Length

    const bool keep_alive = res->keep_alive();

    http::async_write(
        stream_, *res,
        [self = shared_from_this(), res, keep_alive](
            beast::error_code ec, std::size_t /*bytes_transferred*/) {
          if (ec) return;
          if (keep_alive)
            self->do_read();  // next request on the same connection
          else
            self->do_close();
        });
  }

  // ── 4. TCP half-close ─────────────────────────────────────────────────────
  void do_close()
  {
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    // stream_ destructor closes the socket
  }
};

// ══════════════════════════════════════════════════════════════════════════════
// listener — async accept loop for one io_context (one per thread).
// ══════════════════════════════════════════════════════════════════════════════
class listener {
  asio::io_context& ioc_;
  tcp::acceptor acceptor_;

 public:
  listener(asio::io_context& ioc, const tcp::endpoint& ep)
      : ioc_(ioc), acceptor_(ioc)
  {
    beast::error_code ec;

    acceptor_.open(ep.protocol(), ec);
    if (ec) throw std::runtime_error("open: " + ec.message());

    // Allow multiple acceptors to bind the same port (SO_REUSEADDR)
    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);

    // SO_REUSEPORT: kernel distributes incoming SYNs round-robin across
    // all per-thread acceptors → perfect load-balancing, zero mutex contention.
    // Same technique as libasyik's make_http_server(reuse_port=true).
#ifdef SO_REUSEPORT
    {
      int opt = 1;
      ::setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT, &opt,
                   sizeof(opt));
    }
#endif

    acceptor_.bind(ep, ec);
    if (ec) throw std::runtime_error("bind: " + ec.message());

    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) throw std::runtime_error("listen: " + ec.message());
  }

  void run() { do_accept(); }

 private:
  void do_accept()
  {
    // io_context is single-threaded per thread → passing ioc_ as executor
    // for the new socket is sufficient; no strand overhead needed.
    acceptor_.async_accept(
        ioc_, [this](beast::error_code ec, tcp::socket sock) {
          if (!ec) std::make_shared<session>(std::move(sock))->start();
          // Always re-arm, even on error (e.g. EMFILE after ulimit hit)
          do_accept();
        });
  }
};

// ══════════════════════════════════════════════════════════════════════════════
// main
// ══════════════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
  // ── Port ──────────────────────────────────────────────────────────────────
  uint16_t port = 8086;
  if (argc > 1) {
    int p = std::atoi(argv[1]);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }

  // ── Thread count ──────────────────────────────────────────────────────────
  int num_threads = static_cast<int>(std::thread::hardware_concurrency());
  if (num_threads < 1) num_threads = 1;
  if (const char* e = std::getenv("BEAST_THREAD_MULTIPLIER")) {
    int v = std::atoi(e);
    if (v > 0) num_threads = v;
  }

  std::cout << "[bench] Boost.Beast direct server starting on 0.0.0.0:" << port
            << " with " << num_threads << " thread(s) (SO_REUSEPORT)\n";

  const tcp::endpoint ep{asio::ip::address_v4::any(), port};
  std::atomic<int> ready_count{0};

  // ── Spawn one thread per io_context (share-nothing) ───────────────────────
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(num_threads));

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([ep, &ready_count]() {
      try {
        // concurrency_hint=1: tells Asio this io_context will only be
        // driven by a single thread → enables internal lock-free paths.
        asio::io_context ioc{1};

        listener lst(ioc, ep);
        lst.run();  // posts the first async_accept

        ready_count.fetch_add(1, std::memory_order_release);
        ioc.run();  // blocks until io_context is stopped
      } catch (const std::exception& e) {
        std::cerr << "[bench] Thread fatal error: " << e.what() << "\n";
        std::exit(1);
      }
    });
  }

  // ── Wait until every thread is accepting, then print "Ready" ──────────────
  while (ready_count.load(std::memory_order_acquire) < num_threads)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::cout << "[bench] Ready. Endpoints:\n"
            << "  GET  /plaintext\n"
            << "  GET  /json\n"
            << "  POST /echo\n"
            << "  GET  /delay/<ms>\n";

  for (auto& t : threads) t.join();
  return 0;
}
