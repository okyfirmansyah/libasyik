/**
 * libasyik benchmark server — multi-service / SO_REUSEPORT edition
 *
 * Architecture (share-nothing, per libasyik design philosophy):
 *   Each OS thread owns one asyik::service and one http_server bound to
 *   the same address:port via SO_REUSEPORT.  The Linux kernel acts as the
 *   load-balancer, distributing accepted connections across all listeners.
 *   Threads are fully independent: no shared state, no mutexes.
 *
 * Endpoints (mirrors the GIN benchmark server exactly):
 *   GET  /plaintext      → "Hello, World!"  (text/plain)
 *   GET  /json           → {"message":"Hello, World!"}  (application/json)
 *   POST /echo           → echoes request body back  (application/json)
 *   GET  /delay/<int>    → sleeps N ms (fiber-aware), returns {"ok":true}
 *
 * Usage:
 *   ./bench_server [port]           default port = 8080
 *
 * Tuning env vars:
 *   ASYIK_THREAD_MULTIPLIER=N      number of service threads (default: nCPU)
 *                                  Each thread runs its own asyik::service
 *                                  and a separate SO_REUSEPORT acceptor.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aixlog.hpp"
#include "libasyik/http.hpp"
#include "libasyik/profiling.hpp"
#include "libasyik/service.hpp"

// Pre-built constant responses (no per-request allocation)
static const std::string PLAINTEXT_BODY = "Hello, World!";
static const std::string JSON_BODY = R"({"message":"Hello, World!"})";
static const std::string DELAY_BODY = R"({"ok":true})";

// Register all benchmark routes on a server instance.
// Called identically by every service thread — pure read-only captures.
static void register_routes(
    asyik::http_server_ptr<asyik::http_stream_type> server)
{
  // ── Scenario A: Plaintext ──────────────────────────────────────────────
  server->on_http_request("/plaintext", "GET", [](auto req, auto /*args*/) {
    req->response.headers.set("content-type", "text/plain");
    req->response.body = PLAINTEXT_BODY;
    req->response.result(200);
  });

  // ── Scenario B: JSON ──────────────────────────────────────────────────
  server->on_http_request("/json", "GET", [](auto req, auto /*args*/) {
    req->response.headers.set("content-type", "application/json");
    req->response.body = JSON_BODY;
    req->response.result(200);
  });

  // ── Scenario C: POST echo ─────────────────────────────────────────────
  server->on_http_request("/echo", "POST", [](auto req, auto /*args*/) {
    req->response.headers.set("content-type", "application/json");
    req->response.body = req->body;
    req->response.result(200);
  });

  // ── Scenario D: Simulated I/O delay ───────────────────────────────────
  // asyik::sleep_for yields to other fibers; the thread is not blocked.
  server->on_http_request("/delay/<int>", "GET", [](auto req, auto args) {
    int ms = std::stoi(args[1]);
    if (ms < 0) ms = 0;
    if (ms > 10000) ms = 10000;
    asyik::sleep_for(std::chrono::milliseconds(ms));
    req->response.headers.set("content-type", "application/json");
    req->response.body = DELAY_BODY;
    req->response.result(200);
  });
}

int main(int argc, char* argv[])
{
  // ── Suppress INFO logs — log I/O would skew throughput numbers ─────────
  // Initialise once before any service is created (each service would
  // otherwise install its own sink).
  AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::warning);

  // ── Port ────────────────────────────────────────────────────────────────
  uint16_t port = 8080;
  if (argc > 1) {
    int p = std::atoi(argv[1]);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }

  // ── Thread count ────────────────────────────────────────────────────────
  // Default: one service thread per logical CPU (optimal for CPU-bound work).
  // Override with ASYIK_THREAD_MULTIPLIER (exact count, not a multiplier).
  int num_threads = static_cast<int>(std::thread::hardware_concurrency());
  if (num_threads < 1) num_threads = 1;

  const char* env_tm = std::getenv("ASYIK_THREAD_MULTIPLIER");
  if (env_tm) {
    int v = std::atoi(env_tm);
    if (v > 0) num_threads = v;
  }

  std::cout << "[bench] libasyik bench server starting on 0.0.0.0:" << port
            << " with " << num_threads << " service thread(s) (SO_REUSEPORT)\n";

  // ── Shared ready counter ─────────────────────────────────────────────────
  // Each thread increments it after its server is listening; the main thread
  // prints "Ready" once all threads have checked in.
  std::atomic<int> ready_count{0};

  // ── Spawn service threads ────────────────────────────────────────────────
  // Each thread is fully independent: own io_context, own fiber scheduler,
  // own acceptor on the same port via SO_REUSEPORT.  No shared variables.
  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([port, &ready_count]() {
      auto as = asyik::make_service();
      // reuse_port=true → SO_REUSEPORT; kernel distributes connections
      auto server = asyik::make_http_server(as, "0.0.0.0", port,
                                            /*reuse_port=*/true);
      register_routes(server);

      ready_count.fetch_add(1, std::memory_order_release);
      as->run();
    });
  }

  // ── Wait until every thread is accepting ────────────────────────────────
  while (ready_count.load(std::memory_order_acquire) < num_threads) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cout << "[bench] Ready. Endpoints:\n"
            << "  GET  /plaintext\n"
            << "  GET  /json\n"
            << "  POST /echo\n"
            << "  GET  /delay/<ms>\n";

#ifdef LIBASYIK_HTTP_PROFILING
  // ── Profiling reporter thread ──────────────────────────────────────────────
  // Wakes every ASYIK_PROF_INTERVAL seconds (default 5), prints a per-stage
  // breakdown, then resets counters.  Runs until a stop flag is set.
  int prof_interval_s = 5;
  if (const char* e = std::getenv("ASYIK_PROF_INTERVAL")) {
    int v = std::atoi(e);
    if (v > 0) prof_interval_s = v;
  }
  std::atomic<bool> prof_stop{false};
  std::thread prof_thread([&prof_stop, prof_interval_s]() {
    std::cout
        << "[profiling] Reporter active — interval " << prof_interval_s
        << "s  (disable with LIBASYIK_HTTP_PROFILING=OFF at cmake time)\n";
    while (!prof_stop.load(std::memory_order_relaxed)) {
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::seconds(prof_interval_s);
      // Sleep in small steps so the stop flag is checked promptly.
      while (!prof_stop.load(std::memory_order_relaxed) &&
             std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (prof_stop.load(std::memory_order_relaxed)) break;
      asyik::profiling::report(static_cast<double>(prof_interval_s));
      asyik::profiling::reset();
    }
  });
#endif

  for (auto& t : threads) t.join();

#ifdef LIBASYIK_HTTP_PROFILING
  prof_stop.store(true, std::memory_order_relaxed);
  prof_thread.join();
#endif

  return 0;
}
