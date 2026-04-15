---
name: libasyik-development
description: 'Write C++ code using the libasyik microframework. USE WHEN: creating HTTP/REST/WebSocket servers or clients with libasyik; writing fiber-based concurrent code; using asyik::service, execute(), async(); integrating SQL databases via SOCI; implementing rate limiting, caching, or pub/sub; debugging fiber scheduling issues; wrapping blocking operations for fiber safety. DO NOT USE: for general C++ without libasyik; for Boost.Asio/Beast code that does not use libasyik.'
argument-hint: 'Describe the libasyik feature or endpoint you want to build'
---

# libasyik Development Skill

## What is libasyik?

C++17 microframework combining **Boost.Asio**, **Boost.Beast**, and **Boost.Fiber** into a synchronous-looking API for HTTP/REST/WebSocket services. Each connection gets its own lightweight fiber — simple sequential code handles thousands of concurrent connections on one OS thread.

## Architecture

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

## CRITICAL — Fiber Programming Rules

These rules MUST be followed in ALL generated code. Violating them causes deadlocks or scheduler starvation.

### RULE 1 — Never block the fiber scheduler

Any blocking call freezes **all** fibers on that service.

```cpp
// ✗ WRONG — blocks entire scheduler
as->execute([]() {
    std::this_thread::sleep_for(std::chrono::seconds(1)); // BLOCKS all fibers!
    usleep(1000);                                          // BLOCKS all fibers!
    sleep(1);                                              // BLOCKS all fibers!
    auto data = fread(fp, ...);                            // BLOCKS all fibers!
    boost::asio::connect(socket, endpoint);                // BLOCKS (sync Asio)!
});

// ✓ CORRECT — fiber-friendly sleep
as->execute([]() {
    asyik::sleep_for(std::chrono::seconds(1));  // yields to scheduler
});
```

### RULE 2 — Use fiber-aware synchronization

`std::mutex` suspends the OS thread. Use `boost::fibers::mutex` — it yields the fiber.

```cpp
// ✗ WRONG — std::mutex blocks thread
std::mutex mtx;
as->execute([&mtx]() {
    std::lock_guard<std::mutex> lock(mtx);  // thread-level block!
});

// ✓ CORRECT — fibers::mutex yields fiber
boost::fibers::mutex mtx;
as->execute([&mtx]() {
    std::lock_guard<boost::fibers::mutex> lock(mtx);  // fiber yields
});
```

Also use `service::async()` instead of `std::async()`. `std::async` returns a `std::future` whose `.get()` blocks the OS thread (and all fibers). `service::async()` returns a fiber-aware future that yields.

```cpp
// ✗ WRONG — std::async blocks the fiber scheduler on .get()
auto result = std::async([&]() {
    return heavy_query();
}).get();  // blocks OS thread!

// ✓ CORRECT — service::async yields the fiber
auto result = as->async([&]() {
    return heavy_query();
}).get();  // fiber yields until worker completes
```

### RULE 3 — Offload blocking/CPU work with async()

`service::async()` dispatches to a background thread pool. The calling fiber yields until done.

```cpp
as->execute([as]() {
    auto result = as->async([]() -> std::string {
        sleep(3);                       // OK — runs on worker thread
        return heavy_computation();
    }).get();  // fiber yields until worker completes
});
```

### RULE 4 — Wrapping async operations with use_fiber_future

For Boost.Asio async ops:
```cpp
#include "libasyik/internal/use_fiber_future.hpp"

size_t n = stream.async_write_some(
    boost::asio::buffer(data),
    asyik::use_fiber_future
).get();  // blocks only this fiber
```

For non-Asio callbacks, use `fibers::promise`:
```cpp
boost::fibers::promise<std::string> p;
auto f = p.get_future();
external_lib::async_fetch(url, [p = std::move(p)](auto result) mutable {
    p.set_value(result);
});
std::string data = f.get();  // fiber yields until callback fires
```

### Fiber-Safe Alternatives — Quick Reference

| Blocked API (NEVER in fibers) | Fiber-safe replacement |
|---|---|
| `std::this_thread::sleep_for()`, `sleep()`, `usleep()` | `asyik::sleep_for()` |
| `std::mutex` | `boost::fibers::mutex` |
| `std::condition_variable` | `boost::fibers::condition_variable` |
| `std::promise` / `std::future` | `boost::fibers::promise` / `boost::fibers::future` |
| `std::async()` | `as->async([]{...}).get()` |
| Sync file I/O (`fread`, etc.) | `as->async([]{...}).get()` |
| CPU-heavy computation | `as->async([]{...}).get()` |
| Sync `boost::asio::connect()` | Async version + `asyik::use_fiber_future` |

## API Reference

### Includes by feature

| Feature | Include |
|---------|---------|
| Service, execute, async, sleep_for | `"libasyik/service.hpp"` |
| HTTP server, client, websocket | `"libasyik/http.hpp"` |
| SQL (PostgreSQL, SQLite) | `"libasyik/sql.hpp"` |
| Rate limiting | `"libasyik/http.hpp"` |
| In-memory cache | `"libasyik/memcache.hpp"` |
| Pub/Sub | `"libasyik/pubsub.hpp"` |

### Service lifecycle

```cpp
#include "libasyik/service.hpp"

auto as = asyik::make_service();         // create
as->execute([as]() { /* fiber */ });     // spawn fibers
as->run();                               // blocks — runs event loop
// Inside a fiber:
as->stop();                              // graceful shutdown
auto as = asyik::get_current_service();  // get service from any fiber/async
```

### HTTP server

```cpp
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

auto as = asyik::make_service();
auto server = asyik::make_http_server(as, "127.0.0.1", 8080);

// Route tags: <int> → ([0-9]+), <string> → ([^\/\?\s]+), <path> → catch-all
// args[0] = full match, args[1]+ = captures

// GET with params
server->on_http_request("/users/<int>/name/<string>", "GET",
    [](auto req, auto args) {
        req->response.body = "User " + args[1] + ": " + args[2];
        req->response.result(200);
    });

// POST — read body + headers
server->on_http_request("/echo", "POST", [](auto req, auto args) {
    std::string hdr{req->headers["x-custom"]};
    req->response.headers.set("content-type", "application/json");
    req->response.body = req->body;
    req->response.result(200);
});

// Any method
server->on_http_request("/any", [](auto req, auto args) {
    std::string method{req->method()};
    req->response.body = method;
    req->response.result(200);
});

// Regex route
server->on_http_request_regex(std::regex("^/api/(\\d+)$"), "GET",
    [](auto req, auto args) {
        req->response.body = "ID: " + args[1];
        req->response.result(200);
    });

// Limits
server->set_request_body_limit(10 * 1024 * 1024);
server->set_request_header_limit(8 * 1024);

as->run();
```

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
```

### Static file serving

```cpp
server->serve_static("/static", "/var/www/html");
// Supports: ETag/304, Last-Modified/304, Range/206, MIME detection, index files
```

### HTTP client

```cpp
auto req = asyik::http_easy_request(as, "GET", "http://example.com/api");
std::string body = req->response.body;
int status = req->response.result();

// POST with body + headers
auto req = asyik::http_easy_request(as, "POST",
    "http://example.com/api", "payload",
    {{"content-type", "application/json"}});

// With timeout (ms)
auto req = asyik::http_easy_request(as, 5000, "GET", "http://example.com/slow");

// HTTPS — works transparently
auto req = asyik::http_easy_request(as, "GET", "https://example.com/secure");
```

### WebSocket server + client

```cpp
// Server
server->on_websocket("/ws/<string>", [](auto ws, auto args) {
    ws->set_idle_timeout(30);
    ws->set_keepalive_pings(true);
    while (1) {
        auto msg = ws->get_string();
        ws->send_string("echo: " + msg);
    }
});

// Client
auto ws = asyik::make_websocket_connection(as, "ws://127.0.0.1:8080/ws/room1");
ws->send_string("hello");
auto reply = ws->get_string();
ws->close(asyik::websocket_close_code::normal, "bye");
```

### SQL database (SOCI)

```cpp
#include "libasyik/sql.hpp"

auto pool = asyik::make_sql_pool(
    asyik::sql_backend_postgresql,
    "host=localhost dbname=mydb user=app password=secret", 4);
auto ses = pool->get_session(as);

ses->query("CREATE TABLE IF NOT EXISTS users (id int, name varchar(255))");

int id = 1; std::string name = "Alice";
ses->query("INSERT INTO users(id, name) VALUES(:id, :name)",
           soci::use(id), soci::use(name));

int out_id; std::string out_name;
ses->query("SELECT id, name FROM users WHERE id=:id",
           soci::use(id), soci::into(out_id), soci::into(out_name));

auto rows = ses->query_rows("SELECT id, name FROM users ORDER BY id");
for (const auto& r : rows) {
    int rid = r.get<int>(0);
    std::string rname = r.get<std::string>(1);
}

ses->begin(); ses->query("UPDATE ..."); ses->commit();
```

### Rate limiting

```cpp
auto limiter = asyik::make_rate_limit_memory(as, 100, 10);
// max_bucket=100, rate=10/sec (leaky bucket)

server->on_http_request("/api/<path>", "GET", [limiter](auto req, auto args) {
    std::string key{req->headers["x-forwarded-for"]};
    if (limiter->checkpoint(key) == 0) {
        req->response.result(429);
        req->response.body = "Rate limited";
        return;
    }
    req->response.body = "OK";
    req->response.result(200);
});
```

### In-memory cache

```cpp
#include "libasyik/memcache.hpp"

auto cache = asyik::make_memcache<std::string, std::string, 60, 4>(as);
cache->put("key", "value");
auto val = cache->get("key");    // extends TTL
cache->erase("key");

// Thread-safe variant (fiber mutex)
auto mt_cache = asyik::make_memcache_mt<std::string, std::string, 60, 4>(as);
```

### Inter-fiber channels

```cpp
boost::fibers::buffered_channel<std::string> ch(16);

as->execute([&ch]() {
    ch.push("hello from fiber A");
});
as->execute([&ch, as]() {
    std::string msg;
    ch.pop(msg);  // fiber yields until available
    as->stop();
});
```

## Error handling

Exceptions propagate through `fibers::future`:
```cpp
try {
    int r = as->execute([]() -> int {
        throw asyik::not_found_error("missing");
    }).get();
} catch (asyik::not_found_error& e) { /* handle */ }
```

Error hierarchy: `io_error` → `network_error` / `file_error` / `resource_error`; `input_error` → `invalid_input_error` / `not_found_error` / `unexpected_input_error`; `service_terminated_error` (thrown on stop).

## Build integration

```cmake
cmake_minimum_required(VERSION 3.14)
project(my_service)

find_package(libasyik REQUIRED)
find_package(Boost COMPONENTS context fiber date_time url REQUIRED)
find_package(Threads REQUIRED)

add_executable(my_service main.cpp)
target_link_libraries(my_service libasyik Boost::fiber Boost::context Threads::Threads)
```

## Logging

```cpp
as->set_default_log_severity(4);  // 0=trace ... 6=fatal
LOG(INFO) << "message\n";
LOG(WARNING) << "warning\n";
```

## SOCI SQLite Pitfalls & Best Practices

The SOCI SQLite3 backend has strict type-mapping rules that differ from PostgreSQL. Code that works with PostgreSQL will often throw `std::bad_cast` on SQLite. Follow these rules when writing SOCI queries for SQLite (or dual-backend code).

### RULE 1 — SQLite INTEGER maps to `int`, never `int64_t`

SOCI's SQLite3 backend reports INTEGER columns as `dt_integer` which maps to C++ `int`. Using `int64_t` (or `long long`) throws `std::bad_cast`.

```cpp
// ✗ WRONG — throws std::bad_cast on SQLite
int64_t ts = r.get<int64_t>(0);
int64_t ts = r.get<long long>(0);

// ✓ CORRECT
int ts = r.get<int>(0);
```

For function signatures and variables that store timestamps or IDs, use `int` (not `int64_t`) when targeting SQLite.

### RULE 2 — Avoid `rowset<row>` for aggregated/computed columns

`rowset<row>` uses runtime type detection via `r.get<T>(col)`. SQLite's type affinity for computed expressions (`AVG()`, arithmetic like `(ts / 30) * 30`, `CAST(...)`) is unpredictable through SOCI — the backend may report `dt_integer` or `dt_double` inconsistently, and `CAST(... AS REAL)` may not be honored by SOCI's type detection layer.

```cpp
// ✗ WRONG — r.get<double>(0) or r.get<int>(0) can both throw std::bad_cast
//           depending on what SOCI infers for the computed column
soci::rowset<row> rs = (sql->prepare
    << "SELECT (ts / :bucket) * :bucket AS bucket_ts, AVG(value) ...",
    soci::use(bucket, "bucket"));
for (auto& r : rs) {
    int ts = r.get<int>(0);        // may throw!
    double avg = r.get<double>(1);  // may throw!
}

// ✓ CORRECT — use statement + into() with explicit C++ types
double bucket_ts = 0, avg_val = 0;
soci::statement st = (sql->prepare
    << "SELECT (ts / " + std::to_string(bucket) + ") * " +
       std::to_string(bucket) + " + 0.0, "
       "COALESCE(AVG(value), 0) "
       "FROM metrics WHERE stream_id = :id",
    soci::use(stream_id, "id"),
    soci::into(bucket_ts), soci::into(avg_val));
st.execute();
while (st.fetch()) {
    int ts = static_cast<int>(bucket_ts);
    // use avg_val...
}
```

### RULE 3 — Inline integer constants into SQL for arithmetic

SOCI's SQLite backend struggles with named bind parameters used in arithmetic expressions. The parameter binding may fail or produce wrong types.

```cpp
// ✗ WRONG — named params in arithmetic cause issues on SQLite
soci::use(bucket_seconds, "bucket")
// ... "SELECT (ts / :bucket) * :bucket ..."

// ✓ CORRECT — inline integer constants (safe when value is server-controlled)
std::string q = "SELECT (ts / " + std::to_string(bucket_seconds) + ") * " +
                std::to_string(bucket_seconds) + " + 0.0 AS bucket_ts ...";
```

The `+ 0.0` trick forces SQLite to return REAL, ensuring SOCI maps it to `dt_double` which matches `soci::into(double&)`.

> **Security note**: Only inline values derived from server-controlled constants (e.g., hardcoded bucket sizes). Never inline user-supplied strings — always use `soci::use()` for string parameters to prevent SQL injection.
