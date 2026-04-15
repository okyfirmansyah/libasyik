/**
 * bench_libasyik_raw.cpp — libasyik fibers + raw Asio TCP (no Beast HTTP)
 *
 * Purpose:
 *   Measures the overhead of libasyik's fiber scheduler by using its
 *   asyik::service for concurrency (fibers, sleep_for, etc.) but bypassing
 *   the built-in HTTP server entirely.  TCP accept/read/write are done
 *   with raw Boost.Asio async APIs, made fiber-friendly via
 *   boost::asio::use_fiber_future.  HTTP parsing and serialization are
 *   hand-rolled (identical to bench_asio.cpp).
 *
 *   Comparing this against bench_asio isolates the cost of libasyik's
 *   fiber scheduler, and comparing against bench_server isolates the cost
 *   of Beast's HTTP layer.
 *
 * Architecture:
 *   N threads (default = nCPU), each owning one asyik::service.  Each
 *   service creates a TCP acceptor bound to the same address:port via
 *   SO_REUSEPORT (share-nothing, per libasyik design philosophy).
 *   Accept and per-connection I/O run as fibers — no callbacks, no
 *   shared_ptr prevent-destruction tricks.
 *
 * Memory strategy:
 *   - Per-connection read buffer is a fixed 8 KB char array (stack/object)
 *   - Pre-built response strings are static const (no per-request alloc)
 *   - Echo response assembled into a per-connection scratch buffer that
 *     is reused across keep-alive requests (no heap alloc on hot path)
 *   - Each connection object is placement-new'd from a lock-free pool
 *
 * HTTP support (intentionally minimal, matches bench_asio):
 *   - HTTP/1.1 keep-alive
 *   - GET and POST, extracts path from request line
 *   - POST body: reads exactly Content-Length bytes
 *   - No chunked encoding, pipelining, or Expect: 100-continue
 *
 * Endpoints:
 *   GET  /plaintext      → "Hello, World!"              (text/plain)
 *   GET  /json           → {"message":"Hello, World!"}  (application/json)
 *   POST /echo           → echoes request body          (application/json)
 *   GET  /delay/<int>    → fiber sleep N ms, {"ok":true}
 *
 * Usage:
 *   ./bench_libasyik_raw [port]         default port = 4004
 *
 * Env vars:
 *   ASYIK_THREAD_MULTIPLIER=N           thread count (default: nCPU)
 */

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aixlog.hpp"
#include "libasyik/error.hpp"
#include "libasyik/service.hpp"
// Must come after service.hpp (provides fibers:: namespace alias)
#include "libasyik/internal/asio_internal.hpp"

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using asyik::internal::socket::async_read;
using asyik::internal::socket::async_timer_wait;
using asyik::internal::socket::async_write;

// ── Pre-built complete HTTP responses (zero allocation) ─────────────────────

static const std::string RESP_PLAINTEXT =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "Connection: keep-alive\r\n"
    "Server: libasyik-raw/bench\r\n"
    "\r\n"
    "Hello, World!";

static const std::string RESP_JSON =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 27\r\n"
    "Connection: keep-alive\r\n"
    "Server: libasyik-raw/bench\r\n"
    "\r\n"
    R"({"message":"Hello, World!"})";

static const std::string RESP_DELAY =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 11\r\n"
    "Connection: keep-alive\r\n"
    "Server: libasyik-raw/bench\r\n"
    "\r\n"
    R"({"ok":true})";

static const std::string RESP_NOT_FOUND =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: keep-alive\r\n"
    "Server: libasyik-raw/bench\r\n"
    "\r\n"
    "Not Found";

static constexpr const char ECHO_HEADER_PREFIX[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: keep-alive\r\n"
    "Server: libasyik-raw/bench\r\n"
    "Content-Length: ";

// ── Simple lock-free object pool ────────────────────────────────────────────
// Avoids heap allocation for connection objects on the hot path.
// Each thread gets its own pool (no sharing), so no atomics needed.

template <typename T, std::size_t PoolSize = 512>
class object_pool {
  struct slot {
    alignas(T) char storage[sizeof(T)];
    slot* next;
  };

  std::unique_ptr<slot[]> slots_;
  slot* free_head_ = nullptr;

 public:
  object_pool() : slots_(std::make_unique<slot[]>(PoolSize))
  {
    for (std::size_t i = 0; i < PoolSize; ++i) {
      slots_[i].next = free_head_;
      free_head_ = &slots_[i];
    }
  }

  // Returns nullptr if pool exhausted (caller falls back to new)
  void* allocate()
  {
    if (!free_head_) return nullptr;
    auto* s = free_head_;
    free_head_ = s->next;
    return s->storage;
  }

  void deallocate(void* p)
  {
    auto* s = reinterpret_cast<slot*>(reinterpret_cast<char*>(p) -
                                      offsetof(slot, storage));
    s->next = free_head_;
    free_head_ = s;
  }
};

// ── Per-connection state ────────────────────────────────────────────────────
// Runs entirely as a fiber — linear code, no callbacks.

struct connection_ctx;
static thread_local object_pool<connection_ctx>* tl_pool = nullptr;

struct connection_ctx {
  tcp::socket socket;

  // Fixed read buffer — no heap allocation
  static constexpr std::size_t BUF_SIZE = 8192;
  char buf[BUF_SIZE];
  std::size_t buf_used = 0;

  // Scratch buffer for echo responses — reused across keep-alive requests
  static constexpr std::size_t SCRATCH_SIZE = 8192;
  char scratch[SCRATCH_SIZE];

  // Body accumulation buffer for large POST bodies
  static constexpr std::size_t BODY_BUF_SIZE = 8192;
  char body_buf[BODY_BUF_SIZE];
  std::size_t body_buf_used = 0;

  explicit connection_ctx(tcp::socket s) : socket(std::move(s)) {}

  static void* operator new(std::size_t)
  {
    if (tl_pool) {
      void* p = tl_pool->allocate();
      if (p) return p;
    }
    return ::operator new(sizeof(connection_ctx));
  }

  static void operator delete(void* p)
  {
    if (tl_pool) {
      tl_pool->deallocate(p);
      return;
    }
    ::operator delete(p);
  }
};

// ── Fiber-based connection handler ──────────────────────────────────────────
// Each accepted connection runs this as a fiber. All I/O uses
// asyik::internal wrappers (use_fiber_future) so fibers yield while waiting.

static void handle_connection(connection_ctx* ctx)
{
  auto& sock = ctx->socket;
  auto& buf = ctx->buf;
  auto& buf_used = ctx->buf_used;

  try {
    for (;;) {
      // ── 1. Read until we have complete headers (\r\n\r\n) ─────────
      for (;;) {
        std::string_view data(buf, buf_used);
        if (data.find("\r\n\r\n") != std::string_view::npos) break;
        if (buf_used >= connection_ctx::BUF_SIZE) return;  // headers too large

        auto n = asyik::internal::websocket::async_read_some(
                     sock, asio::buffer(buf + buf_used,
                                        connection_ctx::BUF_SIZE - buf_used))
                     .get();
        buf_used += n;
      }

      // ── 2. Parse request line ─────────────────────────────────────
      std::string_view data(buf, buf_used);
      auto hdr_end = data.find("\r\n\r\n");
      std::size_t header_len = hdr_end + 4;
      std::string_view headers(buf, header_len);

      auto first_line_end = headers.find("\r\n");
      std::string_view request_line = headers.substr(0, first_line_end);

      auto sp1 = request_line.find(' ');
      if (sp1 == std::string_view::npos) return;
      std::string_view method = request_line.substr(0, sp1);

      auto rest = request_line.substr(sp1 + 1);
      auto sp2 = rest.find(' ');
      std::string_view full_path = rest.substr(0, sp2);
      auto qpos = full_path.find('?');
      if (qpos != std::string_view::npos) full_path = full_path.substr(0, qpos);

      // ── 3. Handle POST — read body if needed ─────────────────────
      const std::string* response_ptr = nullptr;
      bool use_scratch = false;
      std::size_t scratch_len = 0;

      if (method == "POST") {
        std::size_t content_length = 0;

        // Extract Content-Length from headers
        std::string_view hdr_block = headers.substr(first_line_end + 2);
        for (;;) {
          auto nl = hdr_block.find("\r\n");
          if (nl == std::string_view::npos || nl == 0) break;
          std::string_view line = hdr_block.substr(0, nl);
          if (line.size() > 16 && (line[0] == 'C' || line[0] == 'c') &&
              (line[8] == 'L' || line[8] == 'l')) {
            auto colon = line.find(':');
            if (colon != std::string_view::npos) {
              auto val = line.substr(colon + 1);
              while (!val.empty() && val[0] == ' ') val.remove_prefix(1);
              std::from_chars(val.data(), val.data() + val.size(),
                              content_length);
            }
          }
          hdr_block.remove_prefix(nl + 2);
        }

        // Gather body
        std::size_t body_in_buf = buf_used - header_len;
        const char* body_data = nullptr;
        std::size_t body_len = content_length;

        if (body_in_buf >= content_length) {
          // Full body already in read buffer
          body_data = buf + header_len;
        } else {
          // Need to read more body bytes
          std::size_t copied = 0;
          if (body_in_buf > 0) {
            std::memcpy(ctx->body_buf, buf + header_len, body_in_buf);
            copied = body_in_buf;
          }
          while (copied < content_length) {
            std::size_t want = content_length - copied;
            if (want > connection_ctx::BUF_SIZE)
              want = connection_ctx::BUF_SIZE;
            auto n = asyik::internal::websocket::async_read_some(
                         sock, asio::buffer(buf, want))
                         .get();
            std::size_t to_copy = n;
            if (copied + to_copy > connection_ctx::BODY_BUF_SIZE)
              to_copy = connection_ctx::BODY_BUF_SIZE - copied;
            std::memcpy(ctx->body_buf + copied, buf, to_copy);
            copied += n;
          }
          body_data = ctx->body_buf;
        }

        // Dispatch POST
        if (full_path == "/echo") {
          // Build echo response into scratch buffer (no heap alloc)
          constexpr std::size_t PREFIX_LEN = sizeof(ECHO_HEADER_PREFIX) - 1;

          // Format: prefix + content_length_digits + \r\n\r\n + body
          char cl_buf[16];
          auto [ptr, ec] =
              std::to_chars(cl_buf, cl_buf + sizeof(cl_buf), body_len);
          std::size_t cl_len = static_cast<std::size_t>(ptr - cl_buf);

          scratch_len = PREFIX_LEN + cl_len + 4 + body_len;
          if (scratch_len <= connection_ctx::SCRATCH_SIZE) {
            char* out = ctx->scratch;
            std::memcpy(out, ECHO_HEADER_PREFIX, PREFIX_LEN);
            out += PREFIX_LEN;
            std::memcpy(out, cl_buf, cl_len);
            out += cl_len;
            std::memcpy(out, "\r\n\r\n", 4);
            out += 4;
            std::memcpy(out, body_data, body_len);
            use_scratch = true;
          } else {
            // Body too large for scratch — fall back to not found
            response_ptr = &RESP_NOT_FOUND;
          }
        } else {
          response_ptr = &RESP_NOT_FOUND;
        }
      } else {
        // ── 4. Handle GET ───────────────────────────────────────────
        if (full_path == "/plaintext") {
          response_ptr = &RESP_PLAINTEXT;
        } else if (full_path == "/json") {
          response_ptr = &RESP_JSON;
        } else if (full_path.size() > 7 &&
                   full_path.substr(0, 7) == "/delay/") {
          int ms = 0;
          auto digits = full_path.substr(7);
          std::from_chars(digits.data(), digits.data() + digits.size(), ms);
          if (ms < 0) ms = 0;
          if (ms > 10000) ms = 10000;

          // Fiber-friendly sleep — yields to other fibers
          asyik::sleep_for(std::chrono::milliseconds(ms));
          response_ptr = &RESP_DELAY;
        } else {
          response_ptr = &RESP_NOT_FOUND;
        }
      }

      // ── 5. Send response ──────────────────────────────────────────
      if (use_scratch) {
        async_write(sock, asio::buffer(ctx->scratch, scratch_len)).get();
      } else {
        async_write(sock, asio::buffer(*response_ptr)).get();
      }

      // ── 6. Shift leftover data for keep-alive pipelining ─────────
      std::size_t consumed = header_len;
      if (method == "POST") {
        // We consumed header + body bytes from the original buffer
        // But body may have been read separately; only shift if body
        // was in the original buffer read
        std::string_view orig_data(buf, buf_used);
        auto orig_hdr_end = orig_data.find("\r\n\r\n");
        std::size_t orig_header_len = orig_hdr_end + 4;
        std::size_t body_in_orig = buf_used - orig_header_len;

        // Find content_length again for consumed calculation
        std::size_t cl = 0;
        std::string_view hdr_block2 =
            orig_data.substr(orig_data.find("\r\n") + 2, orig_hdr_end);
        for (;;) {
          auto nl = hdr_block2.find("\r\n");
          if (nl == std::string_view::npos || nl == 0) break;
          std::string_view line = hdr_block2.substr(0, nl);
          if (line.size() > 16 && (line[0] == 'C' || line[0] == 'c') &&
              (line[8] == 'L' || line[8] == 'l')) {
            auto colon = line.find(':');
            if (colon != std::string_view::npos) {
              auto val = line.substr(colon + 1);
              while (!val.empty() && val[0] == ' ') val.remove_prefix(1);
              std::from_chars(val.data(), val.data() + val.size(), cl);
            }
          }
          hdr_block2.remove_prefix(nl + 2);
        }

        if (body_in_orig >= cl) {
          consumed = orig_header_len + cl;
        } else {
          // Body was read separately; original buf is fully consumed
          consumed = buf_used;
        }
      }

      if (consumed < buf_used) {
        std::memmove(buf, buf + consumed, buf_used - consumed);
        buf_used -= consumed;
      } else {
        buf_used = 0;
      }
      ctx->body_buf_used = 0;
    }
  } catch (...) {
    // Connection closed or error — fiber exits, socket destroyed
  }
}

// ── Callback-based accept loop ──────────────────────────────────────────────
// Modelled after libasyik's own http_server::start_accept().
// The acceptor lives in a shared_ptr so the lambda captures keep it alive.
// Each accepted connection is dispatched as a fiber via as->execute().

struct accept_state {
  std::shared_ptr<tcp::acceptor> acceptor;
  asyik::service_ptr as;

  void start_accept()
  {
    acceptor->async_accept(
        as->get_io_service(),
        [this](const boost::system::error_code& ec, tcp::socket socket) {
          if (!ec) {
            auto* ctx = new connection_ctx(std::move(socket));
            as->execute([ctx]() {
              handle_connection(ctx);
              delete ctx;
            });
            start_accept();  // post next accept
          } else {
            std::cerr << "[bench] accept error: " << ec.message() << "\n";
          }
        });
  }
};

static accept_state* setup_acceptor(asyik::service_ptr as, uint16_t port)
{
  auto& ioc = as->get_io_service();
  auto acceptor = std::make_shared<tcp::acceptor>(ioc);

  boost::system::error_code ec;
  acceptor->open(tcp::v4(), ec);
  if (ec) {
    std::cerr << "[bench] open error: " << ec.message() << "\n";
    return nullptr;
  }

  acceptor->set_option(asio::socket_base::reuse_address(true), ec);

#ifdef SO_REUSEPORT
  {
    int opt = 1;
    ::setsockopt(acceptor->native_handle(), SOL_SOCKET, SO_REUSEPORT, &opt,
                 sizeof(opt));
  }
#endif

  tcp::endpoint ep(asio::ip::address_v4::any(), port);
  acceptor->bind(ep, ec);
  if (ec) {
    std::cerr << "[bench] bind error: " << ec.message() << "\n";
    return nullptr;
  }
  acceptor->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    std::cerr << "[bench] listen error: " << ec.message() << "\n";
    return nullptr;
  }

  // Leaked intentionally — lives for the lifetime of the process
  auto* state = new accept_state{std::move(acceptor), as};
  state->start_accept();
  return state;
}

// ══════════════════════════════════════════════════════════════════════════════
// main
// ══════════════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
  // Suppress INFO logs — would skew throughput
  AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::warning);

  // ── Port ──────────────────────────────────────────────────────────────
  uint16_t port = 4004;
  if (argc > 1) {
    int p = std::atoi(argv[1]);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }

  // ── Thread count ──────────────────────────────────────────────────────
  int num_threads = static_cast<int>(std::thread::hardware_concurrency());
  if (num_threads < 1) num_threads = 1;
  if (const char* e = std::getenv("ASYIK_THREAD_MULTIPLIER")) {
    int v = std::atoi(e);
    if (v > 0) num_threads = v;
  }

  std::cout << "[bench] libasyik-raw server starting on 0.0.0.0:" << port
            << " with " << num_threads << " service thread(s) (SO_REUSEPORT)\n";

  std::atomic<int> ready_count{0};
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(num_threads));

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([port, &ready_count]() {
      // Thread-local connection pool
      object_pool<connection_ctx> pool;
      tl_pool = &pool;

      auto as = asyik::make_service();

      // Set up acceptor with callback-based accept (like libasyik http_server)
      setup_acceptor(as, port);

      ready_count.fetch_add(1, std::memory_order_release);
      as->run();
    });
  }

  // Wait until all threads are accepting
  while (ready_count.load(std::memory_order_acquire) < num_threads) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cout << "[bench] Ready. Endpoints:\n"
            << "  GET  /plaintext\n"
            << "  GET  /json\n"
            << "  POST /echo\n"
            << "  GET  /delay/<ms>\n";

  for (auto& t : threads) t.join();
  return 0;
}
