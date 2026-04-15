/**
 * bench_drogon.cpp — Drogon HTTP server benchmark
 *
 * Purpose:
 *   Benchmark the Drogon C++ HTTP framework for comparison against libasyik,
 *   Beast, Asio, and other server implementations.
 *
 * Endpoints (identical to other benchmarks):
 *   GET  /plaintext      → "Hello, World!"              (text/plain)
 *   GET  /json           → {"message":"Hello, World!"}  (application/json)
 *   POST /echo           → echoes request body          (application/json)
 *   GET  /delay/{ms}     → async timer N ms, {"ok":true}
 *
 * Usage:
 *   ./bench_drogon [port]              default port = 8089
 *
 * Env vars:
 *   DROGON_THREAD_MULTIPLIER=N         IO thread count (default: nCPU)
 */

#include <drogon/drogon.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace drogon;

int main(int argc, char* argv[])
{
  uint16_t port = 8093;
  if (argc > 1) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }

  unsigned int n_threads = std::thread::hardware_concurrency();
  if (const char* env = std::getenv("DROGON_THREAD_MULTIPLIER")) {
    n_threads = static_cast<unsigned int>(std::atoi(env));
  }
  if (n_threads == 0) n_threads = 4;

  std::cerr << "[bench] Drogon server starting on 0.0.0.0:" << port << " with "
            << n_threads << " IO thread(s) (SO_REUSEPORT)" << std::endl;

  // ── GET /plaintext ──────────────────────────────────────────────────────
  app().registerHandler(
      "/plaintext",
      [](const HttpRequestPtr&,
         std::function<void(const HttpResponsePtr&)>&& callback) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody("Hello, World!");
        callback(resp);
      },
      {Get});

  // ── GET /json ───────────────────────────────────────────────────────────
  app().registerHandler(
      "/json",
      [](const HttpRequestPtr&,
         std::function<void(const HttpResponsePtr&)>&& callback) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(R"({"message":"Hello, World!"})");
        callback(resp);
      },
      {Get});

  // ── POST /echo ──────────────────────────────────────────────────────────
  app().registerHandler(
      "/echo",
      [](const HttpRequestPtr& req,
         std::function<void(const HttpResponsePtr&)>&& callback) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(std::string(req->body()));
        callback(resp);
      },
      {Post});

  // ── GET /delay/{ms} ─────────────────────────────────────────────────────
  app().registerHandler(
      "/delay/{1}",
      [](const HttpRequestPtr&,
         std::function<void(const HttpResponsePtr&)>&& callback,
         const std::string& ms_str) {
        int ms = std::atoi(ms_str.c_str());
        if (ms <= 0) {
          auto resp = HttpResponse::newHttpResponse();
          resp->setContentTypeCode(CT_APPLICATION_JSON);
          resp->setBody(R"({"ok":true})");
          callback(resp);
          return;
        }
        // Use Drogon's event loop timer for async delay
        auto loop = app().getLoop();
        loop->runAfter(static_cast<double>(ms) / 1000.0,
                       [callback = std::move(callback)]() {
                         auto resp = HttpResponse::newHttpResponse();
                         resp->setContentTypeCode(CT_APPLICATION_JSON);
                         resp->setBody(R"({"ok":true})");
                         callback(resp);
                       });
      },
      {Get});

  // ── Server configuration ────────────────────────────────────────────────
  app().addListener("0.0.0.0", port);
  app().setThreadNum(n_threads);
  app().enableReusePort(true);
  app().disableSigtermHandling();
  app().setLogLevel(trantor::Logger::kWarn);

  app().run();
  return 0;
}
