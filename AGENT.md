# AGENT.md — AI Assistant Guide for libasyik

## What is libasyik?

A C++17 microframework combining **Boost.Asio** (async I/O), **Boost.Beast** (HTTP/WebSocket), and **Boost.Fiber** (cooperative user-space threads) into a synchronous-looking API for building HTTP/REST/WebSocket services. Each connection runs in its own lightweight fiber — enabling simple sequential code that handles thousands of concurrent connections on a single thread.

## Architecture at a Glance

```
┌──────────────────────────────────────────┐
│  asyik::service  (1 OS thread)           │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│  │ fiber #1 │ │ fiber #2 │ │ fiber #N │ │  ← cooperative scheduling
│  │ (conn A) │ │ (conn B) │ │ (conn N) │ │  ← 1 connection = 1 fiber
│  └──────────┘ └──────────┘ └──────────┘ │
│         boost::asio::io_context          │
│         boost::fiber scheduler           │
├──────────────────────────────────────────┤
│  Worker thread pool  (async())           │  ← CPU/blocking I/O offload
└──────────────────────────────────────────┘
```

**Key principle**: Fibers within the same `asyik::service` share one OS thread — no locks needed for shared state. Fibers yield cooperatively at libasyik API calls.

## Critical: Fiber Programming Rules

### RULE 1 — Never block the fiber scheduler

Fibers share one OS thread per service. Any blocking call freezes **all** fibers on that service.

```cpp
// ✗ WRONG — blocks entire scheduler
as->execute([]() {
    std::this_thread::sleep_for(std::chrono::seconds(1)); // BLOCKS all fibers!
    usleep(1000);                                          // BLOCKS all fibers!
    sleep(1);                                              // BLOCKS all fibers!
    auto data = fread(fp, ...);                            // BLOCKS all fibers!
    boost::asio::connect(socket, endpoint);                // BLOCKS (sync Asio)!
});

// ✓ CORRECT — use fiber-friendly sleep
as->execute([]() {
    asyik::sleep_for(std::chrono::seconds(1));  // yields to scheduler
});
```

### RULE 2 — Use fiber-aware synchronization primitives

`std::mutex` suspends the OS thread. Use `boost::fibers::mutex` instead — it yields the current fiber while waiting.

```cpp
// ✗ WRONG — std::mutex blocks the thread (all fibers starve)
std::mutex mtx;
as->execute([&mtx]() {
    std::lock_guard<std::mutex> lock(mtx);  // thread-level block!
    // ...
});

// ✓ CORRECT — fibers::mutex yields to other fibers while waiting
boost::fibers::mutex mtx;
as->execute([&mtx]() {
    std::lock_guard<boost::fibers::mutex> lock(mtx);  // fiber yields
    // ...
});
```

Similarly, use `boost::fibers::promise` / `boost::fibers::future` / `boost::fibers::condition_variable` / `boost::fibers::buffered_channel` instead of their `std::` equivalents.

### RULE 3 — Offload CPU-intensive or blocking work with async()

`service::async()` dispatches work to a background thread pool and returns a `fibers::future`. The calling fiber yields until the result is ready.

```cpp
as->execute([as]() {
    // Offload heavy computation to thread pool
    auto result = as->async([]() -> std::string {
        // OK to use blocking APIs here — runs on worker thread
        sleep(3);
        return do_heavy_computation();
    }).get();  // fiber yields until worker thread completes

    LOG(INFO) << "Result: " << result << "\n";
});
```

The thread pool size is `hardware_concurrency × ASYIK_THREAD_MULTIPLIER` (env var, default 5).

### RULE 4 — Wrapping external async operations with use_fiber_future

To integrate any Boost.Asio-compatible async operation into fiber code, use `asyik::use_fiber_future` as the completion token:

```cpp
#include "libasyik/internal/use_fiber_future.hpp"

// Converts an Asio async call into a fiber-blocking call
// The fiber yields until the async operation completes
size_t n = stream.async_write_some(
    boost::asio::buffer(data),
    asyik::use_fiber_future   // completion token
).get();                      // .get() blocks *only* this fiber
```

For non-Asio async libraries, use `fibers::promise`/`fibers::future` manually:

```cpp
boost::fibers::promise<std::string> p;
auto f = p.get_future();

external_lib::async_fetch(url, [p = std::move(p)](auto result) mutable {
    p.set_value(result);  // fulfills promise from callback
});

std::string data = f.get();  // fiber yields until callback fires
```

## Service Lifecycle

```cpp
#include "libasyik/service.hpp"

// 1. Create service (creates io_context + fiber scheduler)
auto as = asyik::make_service();

// 2. Spawn fibers and register handlers
as->execute([as]() {
    // ... setup work ...
});

// 3. Run (blocks current thread — runs event loop + fiber scheduler)
as->run();

// To stop from inside a fiber:
as->stop();
```

`get_current_service()` retrieves the active service from within any fiber or `async()` worker:
```cpp
auto as = asyik::get_current_service();
```

## HTTP Server

```cpp
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

auto as = asyik::make_service();
auto server = asyik::make_http_server(as, "127.0.0.1", 8080);

// GET with path parameters — <string>, <int>, <path> tags
server->on_http_request("/users/<int>/name/<string>", "GET",
    [](auto req, auto args) {
        // args[0] = full match, args[1] = first capture, args[2] = second, ...
        req->response.body = "User " + args[1] + " name: " + args[2];
        req->response.result(200);
    });

// POST — read body and headers
server->on_http_request("/echo", "POST", [](auto req, auto args) {
    std::string custom_header{req->headers["x-custom"]};
    req->response.headers.set("content-type", "application/json");
    req->response.body = req->body;  // echo back
    req->response.result(200);
});

// Any method
server->on_http_request("/any", [](auto req, auto args) {
    std::string method{req->method()};
    req->response.body = "Method was: " + method;
    req->response.result(200);
});

// Regex route
server->on_http_request_regex(std::regex("^/api/(\\d+)$"), "GET",
    [](auto req, auto args) {
        req->response.body = "ID: " + args[1];
        req->response.result(200);
    });

// Set limits
server->set_request_body_limit(10 * 1024 * 1024);   // 10 MB
server->set_request_header_limit(8 * 1024);          // 8 KB

as->run();
```

### Route tag reference

| Tag | Regex | Example |
|-----|-------|---------|
| `<int>` | `([0-9]+)` | `/users/<int>` matches `/users/42` |
| `<string>` | `([^\/\?\s]+)` | `/name/<string>` matches `/name/alice` |
| `<path>` | catch-all | `/files/<path>` matches `/files/a/b/c.txt` |

### Multi-thread server (SO_REUSEPORT)

```cpp
for (int i = 0; i < std::thread::hardware_concurrency(); i++) {
    std::thread([]() {
        auto as = asyik::make_service();
        auto server = asyik::make_http_server(as, "0.0.0.0", 8080, true);
        server->on_http_request("/hello", "GET", [](auto req, auto args) {
            req->response.body = "Hello!";
            req->response.result(200);
        });
        as->run();
    }).detach();
}
// Each thread has independent service + acceptor; kernel load-balances
```

### Static file serving

```cpp
#include "libasyik/http.hpp"

server->serve_static("/static", "/var/www/html");
// Supports: ETag/304, Last-Modified/304, Range/206, MIME detection, index files
```

## HTTP Client

```cpp
#include "libasyik/http.hpp"

// Simple GET
auto req = asyik::http_easy_request(as, "GET", "http://example.com/api");
if (req->response.result() == 200) {
    std::string body = req->response.body;
}

// POST with body and headers
auto req = asyik::http_easy_request(as, "POST",
    "http://example.com/api", "payload",
    {{"content-type", "application/json"}, {"authorization", "Bearer xyz"}});

// With timeout in milliseconds
auto req = asyik::http_easy_request(as, 5000, "GET", "http://example.com/slow");

// HTTPS works transparently
auto req = asyik::http_easy_request(as, "GET", "https://example.com/secure");
```

## WebSocket

### Server

```cpp
server->on_websocket("/ws/<string>", [](auto ws, auto args) {
    ws->set_idle_timeout(30);        // seconds
    ws->set_keepalive_pings(true);
    while (1) {
        auto msg = ws->get_string();         // fiber yields until message arrives
        ws->send_string("echo: " + msg);     // fiber yields until sent
    }
    // Loop exits via exception when client disconnects
});
```

### Client

```cpp
#include "libasyik/http.hpp"

auto ws = asyik::make_websocket_connection(as, "ws://127.0.0.1:8080/ws/room1");
ws->send_string("hello");
auto reply = ws->get_string();
ws->close(asyik::websocket_close_code::normal, "bye");
```

## SQL Database (SOCI)

```cpp
#include "libasyik/sql.hpp"

// Create connection pool (PostgreSQL or SQLite)
auto pool = asyik::make_sql_pool(
    asyik::sql_backend_postgresql,
    "host=localhost dbname=mydb user=app password=secret",
    4);  // pool size

auto ses = pool->get_session(as);  // fiber yields if pool exhausted

// DDL
ses->query("CREATE TABLE IF NOT EXISTS users (id int, name varchar(255))");

// Insert with bind params
int id = 1; std::string name = "Alice";
ses->query("INSERT INTO users(id, name) VALUES(:id, :name)",
           soci::use(id), soci::use(name));

// Select into variables
int out_id; std::string out_name;
ses->query("SELECT id, name FROM users WHERE id=:id",
           soci::use(id), soci::into(out_id), soci::into(out_name));

// Multi-row iteration
auto rows = ses->query_rows("SELECT id, name FROM users ORDER BY id");
for (const auto& r : rows) {
    int rid = r.get<int>(0);
    std::string rname = r.get<std::string>(1);
}

// Transactions
ses->begin();
ses->query("UPDATE users SET name='Bob' WHERE id=1");
ses->commit();  // or ses->rollback()
```

## Rate Limiting

```cpp
#include "libasyik/http.hpp"

auto limiter = asyik::make_rate_limit_memory(as, 100, 10);
// max_bucket=100 tokens, refill rate=10 tokens/sec (leaky bucket)

server->on_http_request("/api/<path>", "GET", [limiter](auto req, auto args) {
    std::string client_ip{req->headers["x-forwarded-for"]};
    if (limiter->checkpoint(client_ip) == 0) {
        req->response.result(429);
        req->response.body = "Rate limited";
        return;
    }
    req->response.body = "OK";
    req->response.result(200);
});
```

## In-Memory Cache

```cpp
#include "libasyik/memcache.hpp"

// TTL = 60 seconds, 4 segments
auto cache = asyik::make_memcache<std::string, std::string, 60, 4>(as);

cache->put("key", "value");
auto val = cache->get("key");  // extends TTL
cache->erase("key");

// Thread-safe variant (uses fibers::mutex internally)
auto mt_cache = asyik::make_memcache_mt<std::string, std::string, 60, 4>(as);
```

## Inter-Fiber Communication

Use `boost::fibers::buffered_channel` for typed message passing between fibers:

```cpp
boost::fibers::buffered_channel<std::string> ch(16);  // capacity 16

as->execute([&ch]() {
    asyik::sleep_for(std::chrono::seconds(1));
    ch.push("hello from fiber A");
});

as->execute([&ch, as]() {
    std::string msg;
    ch.pop(msg);  // fiber yields until message available
    LOG(INFO) << msg << "\n";
    as->stop();
});
```

## Error Handling

Exceptions propagate through `fibers::future`:

```cpp
try {
    int result = as->execute([]() -> int {
        throw asyik::not_found_error("missing item");
    }).get();
} catch (asyik::not_found_error& e) {
    // handle error
}
```

### Error hierarchy

```
io_error
├── network_error
│   ├── network_timeout_error
│   ├── network_unreachable_error
│   └── network_expired_error
├── file_error
└── resource_error

input_error
├── invalid_input_error
├── not_found_error
├── unexpected_input_error
├── out_of_range_error
└── overflow_error

unexpected_error
already_expired_error
already_closed_error
already_exists_error
timeout_error
service_terminated_error   ← thrown when service stops while fiber is active
```

## Logging

```cpp
#include "libasyik/service.hpp"

as->set_default_log_severity(4);  // 0=trace ... 6=fatal
LOG(INFO) << "message\n";
LOG(WARNING) << "something happened\n";
LOG(ERROR) << "bad thing\n";
```

## Build Integration

```cmake
cmake_minimum_required(VERSION 3.14)
project(my_service)

find_package(libasyik REQUIRED)
find_package(Boost COMPONENTS context fiber date_time url REQUIRED)
find_package(Threads REQUIRED)

add_executable(my_service main.cpp)
target_link_libraries(my_service libasyik Boost::fiber Boost::context Threads::Threads)

# Optional: enable SOCI database support
# SET(LIBASYIK_ENABLE_SOCI ON)

# Optional: enable SSL server
# SET(LIBASYIK_ENABLE_SSL_SERVER ON)
```

## Required includes by feature

| Feature | Include |
|---------|---------|
| Service, execute, async, sleep_for | `"libasyik/service.hpp"` |
| HTTP server, client, websocket | `"libasyik/http.hpp"` |
| SQL (PostgreSQL, SQLite) | `"libasyik/sql.hpp"` |
| Rate limiting | `"libasyik/http.hpp"` (or `"libasyik/rate_limit.hpp"`) |
| In-memory cache | `"libasyik/memcache.hpp"` |
| Pub/Sub | `"libasyik/pubsub.hpp"` |
| use_fiber_future | `"libasyik/internal/use_fiber_future.hpp"` |

## Quick Reference: Fiber-Safe Alternatives

| Blocked API (DO NOT use in fibers) | Fiber-safe alternative |
|---|---|
| `std::this_thread::sleep_for()` | `asyik::sleep_for()` |
| `sleep()`, `usleep()` | `asyik::sleep_for()` |
| `std::mutex` | `boost::fibers::mutex` |
| `std::condition_variable` | `boost::fibers::condition_variable` |
| `std::promise` / `std::future` | `boost::fibers::promise` / `boost::fibers::future` |
| Sync file I/O, `fread()` | Wrap in `as->async([]{ ... }).get()` |
| CPU-heavy computation | Wrap in `as->async([]{ ... }).get()` |
| Sync `boost::asio::connect()` | Use async version with `asyik::use_fiber_future` |
