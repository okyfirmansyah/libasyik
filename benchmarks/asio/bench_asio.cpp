/**
 * bench_asio.cpp — Raw Boost.Asio TCP server with hand-rolled HTTP parsing
 *
 * Purpose:
 *   Establishes the absolute performance floor for async C++ networking by
 *   using ONLY Boost.Asio for TCP — bypassing Boost.Beast entirely. HTTP
 *   request parsing and response serialization are done by hand with minimal
 *   string operations.
 *
 *   Comparing this against bench_beast isolates the cost of Beast's HTTP
 *   parser, serializer, flat_buffer, and tcp_stream layers.
 *
 * Architecture (mirrors bench_beast exactly):
 *   N threads (default = nCPU), each owning one io_context and one acceptor
 *   bound to the same address:port via SO_REUSEPORT.  Threads are fully
 *   independent — no shared state, no mutexes.
 *
 * HTTP support (intentionally minimal):
 *   - HTTP/1.1 keep-alive (reads until an empty line, then dispatches)
 *   - Recognises GET and POST, extracts path from the request line
 *   - POST body: reads exactly Content-Length bytes
 *   - Does NOT support: chunked encoding, pipelining, Expect: 100-continue
 *   - Responses are pre-formatted with fixed headers (no heap allocation
 *     on the hot path for /plaintext and /json)
 *
 * Endpoints (identical to bench_beast):
 *   GET  /plaintext      → "Hello, World!"              (text/plain)
 *   GET  /json           → {"message":"Hello, World!"}  (application/json)
 *   POST /echo           → echoes request body          (application/json)
 *   GET  /delay/<int>    → async timer N ms, {"ok":true}
 *
 * Usage:
 *   ./bench_asio [port]              default port = 8085
 *
 * Env vars:
 *   ASIO_THREAD_MULTIPLIER=N         thread count (default: nCPU)
 */

#include <atomic>
#include <boost/asio.hpp>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// ── Pre-built complete HTTP responses (zero allocation for hot paths) ────────
// These include headers + body, ready to send in a single async_write.

static const std::string RESP_PLAINTEXT =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "Connection: keep-alive\r\n"
    "Server: Asio/bench\r\n"
    "\r\n"
    "Hello, World!";

static const std::string RESP_JSON =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 27\r\n"
    "Connection: keep-alive\r\n"
    "Server: Asio/bench\r\n"
    "\r\n"
    R"({"message":"Hello, World!"})";

static const std::string RESP_DELAY =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 11\r\n"
    "Connection: keep-alive\r\n"
    "Server: Asio/bench\r\n"
    "\r\n"
    R"({"ok":true})";

static const std::string RESP_NOT_FOUND =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: keep-alive\r\n"
    "Server: Asio/bench\r\n"
    "\r\n"
    "Not Found";

// ── Header prefix for echo responses (body appended dynamically) ────────────
static const char ECHO_HEADER_PREFIX[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: keep-alive\r\n"
    "Server: Asio/bench\r\n"
    "Content-Length: ";

// ══════════════════════════════════════════════════════════════════════════════
// session — owns one TCP connection, runs a keep-alive read/write loop.
// ══════════════════════════════════════════════════════════════════════════════
class session : public std::enable_shared_from_this<session> {
  tcp::socket socket_;
  // Read buffer — 8 KB is enough for headers + small bodies; large POST
  // bodies are read in a second pass.
  static constexpr std::size_t BUF_SIZE = 8192;
  char buf_[BUF_SIZE];
  std::size_t buf_used_ = 0;   // bytes currently in buf_
  std::string body_buf_;        // for POST body accumulation

 public:
  explicit session(tcp::socket sock) : socket_(std::move(sock)) {}

  void start() { do_read(); }

 private:
  // ── 1. Read until we have a complete header block (\r\n\r\n) ──────────────
  void do_read()
  {
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(buf_ + buf_used_, BUF_SIZE - buf_used_),
        [this, self](boost::system::error_code ec, std::size_t n) {
          if (ec) return;  // EOF or error → let shared_ptr destroy us
          buf_used_ += n;
          try_parse();
        });
  }

  // ── 2. Check if we have a full header; if so, dispatch ────────────────────
  void try_parse()
  {
    // Look for end of headers
    std::string_view data(buf_, buf_used_);
    auto hdr_end = data.find("\r\n\r\n");
    if (hdr_end == std::string_view::npos) {
      // Need more data
      if (buf_used_ >= BUF_SIZE) {
        // Headers too large — drop connection
        return;
      }
      do_read();
      return;
    }

    std::size_t header_len = hdr_end + 4;  // includes the \r\n\r\n
    std::string_view headers(buf_, header_len);

    // Parse request line: "GET /path HTTP/1.1\r\n"
    auto first_line_end = headers.find("\r\n");
    std::string_view request_line = headers.substr(0, first_line_end);

    // Extract method
    auto sp1 = request_line.find(' ');
    if (sp1 == std::string_view::npos) return;
    std::string_view method = request_line.substr(0, sp1);

    // Extract path (strip query string)
    auto rest = request_line.substr(sp1 + 1);
    auto sp2 = rest.find(' ');
    std::string_view full_path = rest.substr(0, sp2);
    auto qpos = full_path.find('?');
    if (qpos != std::string_view::npos) full_path = full_path.substr(0, qpos);

    // Check if POST — need Content-Length
    if (method == "POST") {
      std::size_t content_length = 0;

      // Scan headers for Content-Length (case-insensitive enough for benchmarks)
      std::string_view hdr_block = headers.substr(first_line_end + 2);
      for (;;) {
        auto nl = hdr_block.find("\r\n");
        if (nl == std::string_view::npos || nl == 0) break;
        std::string_view line = hdr_block.substr(0, nl);
        // Quick case-insensitive check for "Content-Length:" or "content-length:"
        if (line.size() > 16 &&
            (line[0] == 'C' || line[0] == 'c') &&
            (line[8] == 'L' || line[8] == 'l')) {
          auto colon = line.find(':');
          if (colon != std::string_view::npos) {
            auto val = line.substr(colon + 1);
            // Skip whitespace
            while (!val.empty() && val[0] == ' ') val.remove_prefix(1);
            std::from_chars(val.data(), val.data() + val.size(), content_length);
          }
        }
        hdr_block.remove_prefix(nl + 2);
      }

      // How much body do we already have in the buffer?
      std::size_t body_in_buf = buf_used_ - header_len;
      if (body_in_buf >= content_length) {
        // Full body already received
        std::string_view body(buf_ + header_len, content_length);
        dispatch_post(full_path, body);
      } else {
        // Need to read more body bytes
        body_buf_.assign(buf_ + header_len, body_in_buf);
        buf_used_ = 0;
        read_body(full_path, content_length, content_length - body_in_buf);
      }
    } else {
      // GET — dispatch immediately; shift leftover bytes for pipelining
      dispatch_get(full_path);
    }
  }

  // ── Read remaining POST body bytes ────────────────────────────────────────
  void read_body(std::string_view path, std::size_t total, std::size_t remaining)
  {
    // Store path for use in callback (path may point into buf_ which gets overwritten)
    // We'll use body_buf_ size to know when done
    auto self = shared_from_this();
    std::string path_copy(path);
    socket_.async_read_some(
        asio::buffer(buf_, std::min(remaining, BUF_SIZE)),
        [this, self, path_copy, total, remaining](boost::system::error_code ec, std::size_t n) {
          if (ec) return;
          body_buf_.append(buf_, n);
          if (n >= remaining) {
            std::string_view body(body_buf_.data(), total);
            dispatch_post(path_copy, body);
          } else {
            read_body(path_copy, total, remaining - n);
          }
        });
  }

  // ── 3a. Dispatch GET requests ─────────────────────────────────────────────
  void dispatch_get(std::string_view path)
  {
    if (path == "/plaintext") {
      send_fixed(RESP_PLAINTEXT);
    } else if (path == "/json") {
      send_fixed(RESP_JSON);
    } else if (path.size() > 7 && path.substr(0, 7) == "/delay/") {
      int ms = 0;
      auto digits = path.substr(7);
      std::from_chars(digits.data(), digits.data() + digits.size(), ms);
      if (ms < 0) ms = 0;
      if (ms > 10000) ms = 10000;
      do_delay(ms);
    } else {
      send_fixed(RESP_NOT_FOUND);
    }
  }

  // ── 3b. Dispatch POST requests ────────────────────────────────────────────
  void dispatch_post(std::string_view path, std::string_view body)
  {
    if (path == "/echo") {
      send_echo(body);
    } else {
      send_fixed(RESP_NOT_FOUND);
    }
  }

  // ── 4. Send a pre-built response (zero-copy for fixed responses) ──────────
  void send_fixed(const std::string& resp)
  {
    auto self = shared_from_this();
    asio::async_write(
        socket_, asio::buffer(resp),
        [this, self](boost::system::error_code ec, std::size_t /*n*/) {
          if (ec) return;
          // Reset buffer and read next request (keep-alive)
          buf_used_ = 0;
          do_read();
        });
  }

  // ── 5. Send echo response (dynamic Content-Length) ────────────────────────
  void send_echo(std::string_view body)
  {
    // Build response: header prefix + content-length + \r\n\r\n + body
    auto resp = std::make_shared<std::string>();
    resp->reserve(sizeof(ECHO_HEADER_PREFIX) + 10 + 4 + body.size());
    resp->append(ECHO_HEADER_PREFIX, sizeof(ECHO_HEADER_PREFIX) - 1);
    resp->append(std::to_string(body.size()));
    resp->append("\r\n\r\n");
    resp->append(body.data(), body.size());

    auto self = shared_from_this();
    asio::async_write(
        socket_, asio::buffer(*resp),
        [this, self, resp](boost::system::error_code ec, std::size_t /*n*/) {
          if (ec) return;
          buf_used_ = 0;
          body_buf_.clear();
          do_read();
        });
  }

  // ── 6. Async delay handler ────────────────────────────────────────────────
  void do_delay(int ms)
  {
    auto timer = std::make_shared<asio::steady_timer>(
        socket_.get_executor(), std::chrono::milliseconds(ms));
    auto self = shared_from_this();

    timer->async_wait(
        [this, self, timer](boost::system::error_code /*ec*/) {
          send_fixed(RESP_DELAY);
        });
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
    boost::system::error_code ec;

    acceptor_.open(ep.protocol(), ec);
    if (ec) throw std::runtime_error("open: " + ec.message());

    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);

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
    acceptor_.async_accept(
        ioc_, [this](boost::system::error_code ec, tcp::socket sock) {
          if (!ec) std::make_shared<session>(std::move(sock))->start();
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
  uint16_t port = 8085;
  if (argc > 1) {
    int p = std::atoi(argv[1]);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }

  // ── Thread count ──────────────────────────────────────────────────────────
  int num_threads = static_cast<int>(std::thread::hardware_concurrency());
  if (num_threads < 1) num_threads = 1;
  if (const char* e = std::getenv("ASIO_THREAD_MULTIPLIER")) {
    int v = std::atoi(e);
    if (v > 0) num_threads = v;
  }

  std::cout << "[bench] Raw Boost.Asio server starting on 0.0.0.0:" << port
            << " with " << num_threads << " thread(s) (SO_REUSEPORT)\n";

  const tcp::endpoint ep{asio::ip::address_v4::any(), port};
  std::atomic<int> ready_count{0};

  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(num_threads));

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([ep, &ready_count]() {
      try {
        asio::io_context ioc{1};  // concurrency_hint=1: single-thread fast path

        listener lst(ioc, ep);
        lst.run();

        ready_count.fetch_add(1, std::memory_order_release);
        ioc.run();
      } catch (const std::exception& e) {
        std::cerr << "[bench] Thread fatal error: " << e.what() << "\n";
        std::exit(1);
      }
    });
  }

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
