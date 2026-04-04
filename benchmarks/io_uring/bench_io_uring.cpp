/**
 * bench_io_uring.cpp — Raw io_uring TCP server with hand-rolled HTTP parsing
 *
 * Purpose:
 *   Establishes the absolute lowest-latency baseline by using Linux io_uring
 *   directly via liburing — bypassing both Boost.Asio and Boost.Beast entirely.
 *   io_uring avoids per-syscall overhead by batching submissions and
 * completions through shared kernel/userspace ring buffers, making it the
 * closest thing to kernel-bypass networking without DPDK.
 *
 *   Comparing this against bench_asio isolates the cost of epoll vs io_uring.
 *
 * Architecture:
 *   N threads (default = nCPU), each owning one io_uring instance and one
 *   listening socket bound to the same address:port via SO_REUSEPORT.
 *   Threads are fully independent — no shared state, no mutexes.
 *
 * HTTP support (intentionally minimal, same as bench_asio):
 *   - HTTP/1.1 keep-alive (reads until \r\n\r\n, then dispatches)
 *   - Recognises GET and POST, extracts path from the request line
 *   - POST body: reads exactly Content-Length bytes
 *   - Does NOT support: chunked encoding, pipelining, Expect: 100-continue
 *   - Responses are pre-formatted with fixed headers
 *
 * Endpoints (identical to all other benchmarks):
 *   GET  /plaintext      → "Hello, World!"              (text/plain)
 *   GET  /json           → {"message":"Hello, World!"}  (application/json)
 *   POST /echo           → echoes request body          (application/json)
 *   GET  /delay/<int>    → busy-wait N ms, {"ok":true}  (no async timer in raw
 * io_uring)
 *
 * Usage:
 *   ./bench_io_uring [port]           default port = 8087
 *
 * Env vars:
 *   IOURING_THREAD_MULTIPLIER=N       thread count (default: nCPU)
 *
 * Note on /delay: Since raw io_uring doesn't have a built-in async timer
 * abstraction like Boost.Asio's steady_timer, we use IORING_OP_TIMEOUT
 * (kernel-level timeout) to implement the delay without busy-waiting.
 */

#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// ── Configuration ────────────────────────────────────────────────────────────
static constexpr int QUEUE_DEPTH = 2048;
static constexpr int BUF_SIZE = 8192;
static constexpr int MAX_CONNECTIONS = 16384;

// ── Event types for io_uring completion identification ───────────────────────
enum EventType : uint8_t {
  EVENT_ACCEPT = 0,
  EVENT_READ = 1,
  EVENT_WRITE = 2,
  EVENT_TIMEOUT = 3,
};

// ── Per-connection state ─────────────────────────────────────────────────────
struct conn_info {
  int fd = -1;
  char buf[BUF_SIZE];
  size_t buf_used = 0;
  // For POST body accumulation
  std::string body_buf;
  std::string pending_path;  // stored path for POST body reads
  size_t content_length = 0;
  size_t header_len = 0;
  bool reading_body = false;
  // For write
  std::string write_buf;
  // For delay
  bool delay_pending = false;
};

// Pack event type + connection index into io_uring user_data (64-bit)
static inline uint64_t make_user_data(EventType type, uint32_t conn_idx)
{
  return (static_cast<uint64_t>(type) << 32) | conn_idx;
}
static inline EventType get_event_type(uint64_t user_data)
{
  return static_cast<EventType>(user_data >> 32);
}
static inline uint32_t get_conn_idx(uint64_t user_data)
{
  return static_cast<uint32_t>(user_data & 0xFFFFFFFF);
}

// ── Pre-built complete HTTP responses ────────────────────────────────────────
static const std::string RESP_PLAINTEXT =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "Connection: keep-alive\r\n"
    "Server: io_uring/bench\r\n"
    "\r\n"
    "Hello, World!";

static const std::string RESP_JSON =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 27\r\n"
    "Connection: keep-alive\r\n"
    "Server: io_uring/bench\r\n"
    "\r\n"
    R"({"message":"Hello, World!"})";

static const std::string RESP_DELAY =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 11\r\n"
    "Connection: keep-alive\r\n"
    "Server: io_uring/bench\r\n"
    "\r\n"
    R"({"ok":true})";

static const std::string RESP_NOT_FOUND =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: keep-alive\r\n"
    "Server: io_uring/bench\r\n"
    "\r\n"
    "Not Found";

static const char ECHO_HEADER_PREFIX[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: keep-alive\r\n"
    "Server: io_uring/bench\r\n"
    "Content-Length: ";

// ── Connection pool (per-thread, no locking) ─────────────────────────────────
class conn_pool {
  std::vector<conn_info> conns_;
  std::vector<uint32_t> free_list_;

 public:
  conn_pool()
  {
    conns_.resize(MAX_CONNECTIONS);
    free_list_.reserve(MAX_CONNECTIONS);
    for (uint32_t i = MAX_CONNECTIONS; i > 0; --i) free_list_.push_back(i - 1);
  }

  uint32_t alloc(int fd)
  {
    if (free_list_.empty()) return UINT32_MAX;
    uint32_t idx = free_list_.back();
    free_list_.pop_back();
    auto& c = conns_[idx];
    c.fd = fd;
    c.buf_used = 0;
    c.body_buf.clear();
    c.pending_path.clear();
    c.content_length = 0;
    c.header_len = 0;
    c.reading_body = false;
    c.write_buf.clear();
    c.delay_pending = false;
    return idx;
  }

  void free(uint32_t idx)
  {
    if (idx < conns_.size()) {
      if (conns_[idx].fd >= 0) {
        ::close(conns_[idx].fd);
        conns_[idx].fd = -1;
      }
      free_list_.push_back(idx);
    }
  }

  conn_info& operator[](uint32_t idx) { return conns_[idx]; }
};

// ── Helper: build echo response ──────────────────────────────────────────────
static std::string build_echo_response(std::string_view body)
{
  std::string resp;
  resp.reserve(sizeof(ECHO_HEADER_PREFIX) + 10 + 4 + body.size());
  resp.append(ECHO_HEADER_PREFIX, sizeof(ECHO_HEADER_PREFIX) - 1);
  resp.append(std::to_string(body.size()));
  resp.append("\r\n\r\n");
  resp.append(body.data(), body.size());
  return resp;
}

// ── Helper: create listening socket ──────────────────────────────────────────
static int create_listen_socket(uint16_t port)
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    std::exit(1);
  }

  int opt = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("bind");
    std::exit(1);
  }

  if (::listen(fd, SOMAXCONN) < 0) {
    perror("listen");
    std::exit(1);
  }

  return fd;
}

// ── Submit: accept ───────────────────────────────────────────────────────────
static void submit_accept(struct io_uring* ring, int listen_fd,
                          struct sockaddr_in* client_addr, socklen_t* addr_len)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  io_uring_prep_accept(sqe, listen_fd,
                       reinterpret_cast<struct sockaddr*>(client_addr),
                       addr_len, 0);
  io_uring_sqe_set_data64(sqe, make_user_data(EVENT_ACCEPT, 0));
}

// ── Submit: read ─────────────────────────────────────────────────────────────
static void submit_read(struct io_uring* ring, conn_pool& pool, uint32_t idx)
{
  auto& c = pool[idx];
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  io_uring_prep_recv(sqe, c.fd, c.buf + c.buf_used, BUF_SIZE - c.buf_used, 0);
  io_uring_sqe_set_data64(sqe, make_user_data(EVENT_READ, idx));
}

// ── Submit: write ────────────────────────────────────────────────────────────
static void submit_write(struct io_uring* ring, conn_pool& pool, uint32_t idx,
                         const std::string& data)
{
  auto& c = pool[idx];
  c.write_buf = data;
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  io_uring_prep_send(sqe, c.fd, c.write_buf.data(), c.write_buf.size(), 0);
  io_uring_sqe_set_data64(sqe, make_user_data(EVENT_WRITE, idx));
}

// ── Submit: timeout (for /delay endpoint) ────────────────────────────────────
// We use a __kernel_timespec stored in the conn's body_buf (repurposed).
// Actually, let's use a small struct stored on the conn.
static void submit_timeout(struct io_uring* ring, conn_pool& pool, uint32_t idx,
                           int delay_ms)
{
  auto& c = pool[idx];
  c.delay_pending = true;

  // Store the timespec in the body_buf memory (we need it to live until
  // completion)
  c.body_buf.resize(sizeof(struct __kernel_timespec));
  auto* ts = reinterpret_cast<struct __kernel_timespec*>(c.body_buf.data());
  ts->tv_sec = delay_ms / 1000;
  ts->tv_nsec = (delay_ms % 1000) * 1000000LL;

  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  io_uring_prep_timeout(sqe, ts, 0, 0);
  io_uring_sqe_set_data64(sqe, make_user_data(EVENT_TIMEOUT, idx));
}

// ── Parse and dispatch HTTP request ──────────────────────────────────────────
// Returns true if the request was fully handled (response submitted).
// Returns false if we need more data.
static bool try_parse_and_dispatch(struct io_uring* ring, conn_pool& pool,
                                   uint32_t idx)
{
  auto& c = pool[idx];

  if (c.reading_body) {
    // We're in the middle of reading a POST body
    size_t body_in_buf = c.buf_used;
    size_t still_need = c.content_length - c.body_buf.size();

    if (body_in_buf >= still_need) {
      c.body_buf.append(c.buf, still_need);
      // Dispatch POST
      std::string_view body(c.body_buf.data(), c.content_length);
      if (c.pending_path == "/echo") {
        submit_write(ring, pool, idx, build_echo_response(body));
      } else {
        submit_write(ring, pool, idx, RESP_NOT_FOUND);
      }
      c.reading_body = false;
      return true;
    } else {
      c.body_buf.append(c.buf, body_in_buf);
      c.buf_used = 0;
      return false;
    }
  }

  // Look for end of headers
  std::string_view data(c.buf, c.buf_used);
  auto hdr_end = data.find("\r\n\r\n");
  if (hdr_end == std::string_view::npos) {
    return false;  // need more data
  }

  size_t header_len = hdr_end + 4;
  std::string_view headers(c.buf, header_len);

  // Parse request line
  auto first_line_end = headers.find("\r\n");
  std::string_view request_line = headers.substr(0, first_line_end);

  auto sp1 = request_line.find(' ');
  if (sp1 == std::string_view::npos) {
    submit_write(ring, pool, idx, RESP_NOT_FOUND);
    return true;
  }
  std::string_view method = request_line.substr(0, sp1);

  auto rest = request_line.substr(sp1 + 1);
  auto sp2 = rest.find(' ');
  std::string_view full_path = rest.substr(0, sp2);
  auto qpos = full_path.find('?');
  if (qpos != std::string_view::npos) full_path = full_path.substr(0, qpos);

  if (method == "POST") {
    // Parse Content-Length
    size_t content_length = 0;
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
          std::from_chars(val.data(), val.data() + val.size(), content_length);
        }
      }
      hdr_block.remove_prefix(nl + 2);
    }

    size_t body_in_buf = c.buf_used - header_len;
    if (body_in_buf >= content_length) {
      // Full body already received
      std::string_view body(c.buf + header_len, content_length);
      if (full_path == "/echo") {
        submit_write(ring, pool, idx, build_echo_response(body));
      } else {
        submit_write(ring, pool, idx, RESP_NOT_FOUND);
      }
      return true;
    } else {
      // Need more body bytes — switch to body-reading mode
      c.reading_body = true;
      c.content_length = content_length;
      c.pending_path.assign(full_path.data(), full_path.size());
      c.header_len = header_len;
      c.body_buf.clear();
      c.body_buf.append(c.buf + header_len, body_in_buf);
      c.buf_used = 0;
      return false;
    }
  }

  // GET requests
  if (full_path == "/plaintext") {
    submit_write(ring, pool, idx, RESP_PLAINTEXT);
  } else if (full_path == "/json") {
    submit_write(ring, pool, idx, RESP_JSON);
  } else if (full_path.size() > 7 && full_path.substr(0, 7) == "/delay/") {
    int ms = 0;
    auto digits = full_path.substr(7);
    std::from_chars(digits.data(), digits.data() + digits.size(), ms);
    if (ms < 0) ms = 0;
    if (ms > 10000) ms = 10000;
    submit_timeout(ring, pool, idx, ms);
  } else {
    submit_write(ring, pool, idx, RESP_NOT_FOUND);
  }
  return true;
}

// ── Per-thread event loop ────────────────────────────────────────────────────
static void worker_thread(uint16_t port, std::atomic<int>& ready_count)
{
  // Create io_uring instance
  struct io_uring ring;
  struct io_uring_params params {};
  // SQPOLL can reduce syscalls further, but requires root/CAP_SYS_ADMIN
  // and dedicated kernel threads. For fairness with other benchmarks, we
  // use standard submission mode.
  int ret = io_uring_queue_init_params(QUEUE_DEPTH, &ring, &params);
  if (ret < 0) {
    std::cerr << "[bench] io_uring_queue_init failed: " << strerror(-ret)
              << "\n";
    std::exit(1);
  }

  // Create listening socket
  int listen_fd = create_listen_socket(port);

  // Connection pool
  conn_pool pool;

  // Start accepting
  struct sockaddr_in client_addr {};
  socklen_t addr_len = sizeof(client_addr);
  submit_accept(&ring, listen_fd, &client_addr, &addr_len);
  io_uring_submit(&ring);

  ready_count.fetch_add(1, std::memory_order_release);

  // Event loop
  struct io_uring_cqe* cqe;
  for (;;) {
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
      if (ret == -EINTR) continue;
      break;
    }

    uint64_t ud = io_uring_cqe_get_data64(cqe);
    EventType ev = get_event_type(ud);
    uint32_t conn_idx = get_conn_idx(ud);
    int res = cqe->res;
    io_uring_cqe_seen(&ring, cqe);

    switch (ev) {
      case EVENT_ACCEPT: {
        // Always re-arm accept
        submit_accept(&ring, listen_fd, &client_addr, &addr_len);

        if (res >= 0) {
          // New connection
          int client_fd = res;
          // Disable Nagle for lower latency
          int opt = 1;
          ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

          uint32_t idx = pool.alloc(client_fd);
          if (idx == UINT32_MAX) {
            ::close(client_fd);  // pool full
          } else {
            submit_read(&ring, pool, idx);
          }
        }
        break;
      }

      case EVENT_READ: {
        if (res <= 0) {
          // EOF or error — close connection
          pool.free(conn_idx);
          break;
        }
        auto& c = pool[conn_idx];
        c.buf_used += static_cast<size_t>(res);

        if (!try_parse_and_dispatch(&ring, pool, conn_idx)) {
          // Need more data
          if (c.buf_used >= BUF_SIZE) {
            pool.free(conn_idx);  // headers too large
          } else {
            submit_read(&ring, pool, conn_idx);
          }
        }
        break;
      }

      case EVENT_WRITE: {
        if (res <= 0) {
          pool.free(conn_idx);
          break;
        }
        // Write complete — reset for next request (keep-alive)
        auto& c = pool[conn_idx];
        c.buf_used = 0;
        c.body_buf.clear();
        c.write_buf.clear();
        c.reading_body = false;
        submit_read(&ring, pool, conn_idx);
        break;
      }

      case EVENT_TIMEOUT: {
        auto& c = pool[conn_idx];
        c.delay_pending = false;
        // Timeout fired — send delay response
        submit_write(&ring, pool, conn_idx, RESP_DELAY);
        break;
      }
    }

    // Submit all queued SQEs at once (batched submission)
    io_uring_submit(&ring);
  }

  ::close(listen_fd);
  io_uring_queue_exit(&ring);
}

// ══════════════════════════════════════════════════════════════════════════════
// main
// ══════════════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
  // ── Port ────────────────────────────────────────────────────────────────
  uint16_t port = 8087;
  if (argc > 1) {
    int p = std::atoi(argv[1]);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }

  // ── Thread count ────────────────────────────────────────────────────────
  int num_threads = static_cast<int>(std::thread::hardware_concurrency());
  if (num_threads < 1) num_threads = 1;
  if (const char* e = std::getenv("IOURING_THREAD_MULTIPLIER")) {
    int v = std::atoi(e);
    if (v > 0) num_threads = v;
  }

  std::cout << "[bench] Raw io_uring server starting on 0.0.0.0:" << port
            << " with " << num_threads << " thread(s) (SO_REUSEPORT)\n";

  std::atomic<int> ready_count{0};
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(num_threads));

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back(worker_thread, port, std::ref(ready_count));
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
