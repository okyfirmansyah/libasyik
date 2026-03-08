#ifndef LIBASYIK_PROFILING_HPP
#define LIBASYIK_PROFILING_HPP

/**
 * include/libasyik/profiling.hpp
 *
 * Lightweight HTTP pipeline profiler for libasyik.
 * Enabled only when compiled with -DLIBASYIK_HTTP_PROFILING.
 *
 * Stages measured per HTTP request (server-side):
 *   read_request   – async_read() (TCP recv + HTTP header + body in one pass)
 *   route_match    – find_http_route()    (regex scan over route table)
 *   handler        – user route callback
 *   write_response – prepare_payload() + async_write()
 *   TOTAL          – read_request start → write_response end
 *
 * All counters/sums are std::atomic so every service thread can write
 * without locks.  Stats are aggregated across all threads — call report()
 * and reset() from any one thread.
 *
 * Typical usage (bench_server.cpp):
 *   // In a background thread or fiber that fires every N seconds:
 *   asyik::profiling::report(elapsed_sec);
 *   asyik::profiling::reset();
 */

#ifdef LIBASYIK_HTTP_PROFILING

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace asyik {
namespace profiling {

// ── Per-stage statistics ─────────────────────────────────────────────────────
struct StageStat {
  std::atomic<uint64_t> count{0};
  std::atomic<uint64_t> total_ns{0};

  void record(uint64_t ns) noexcept
  {
    count.fetch_add(1, std::memory_order_relaxed);
    total_ns.fetch_add(ns, std::memory_order_relaxed);
  }

  struct Snapshot {
    uint64_t count, total_ns;
  };
  Snapshot snapshot() const noexcept
  {
    return {count.load(std::memory_order_relaxed),
            total_ns.load(std::memory_order_relaxed)};
  }

  void reset() noexcept
  {
    count.store(0, std::memory_order_relaxed);
    total_ns.store(0, std::memory_order_relaxed);
  }
};

// ── Global pipeline stats (inline = one definition across all TUs) ───────────
struct HttpPipelineStats {
  StageStat read_request;  // single async_read (header + body combined)
  StageStat route_match;
  StageStat handler;
  StageStat write_response;
  StageStat total;
};

inline HttpPipelineStats g_http_prof;

// ── Report: print a formatted breakdown table
// ─────────────────────────────────
inline void report(double elapsed_sec = 0.0)
{
  struct Row {
    const char* name;
    StageStat::Snapshot s;
  };
  Row rows[] = {
      {"read_request", g_http_prof.read_request.snapshot()},
      {"route_match", g_http_prof.route_match.snapshot()},
      {"handler", g_http_prof.handler.snapshot()},
      {"write_response", g_http_prof.write_response.snapshot()},
      {"TOTAL", g_http_prof.total.snapshot()},
  };

  uint64_t nreq = rows[4].s.count;
  double total_avg_us = (nreq > 0) ? rows[4].s.total_ns / 1000.0 / nreq : 0.0;

  std::printf("\n[profiling] HTTP Pipeline Breakdown");
  if (elapsed_sec > 0.0 && nreq > 0)
    std::printf("  |  %llu req  %.0f req/s", (unsigned long long)nreq,
                nreq / elapsed_sec);
  std::printf("\n");
  std::printf("  %-16s | %8s | %10s | %7s\n", "Stage", "Count", "Avg (us)",
              "Share");
  std::printf("  -----------------+----------+------------+---------\n");

  for (int i = 0; i < 5; ++i) {
    auto& r = rows[i];
    double avg_us = (r.s.count > 0) ? r.s.total_ns / 1000.0 / r.s.count : 0.0;
    double share =
        (total_avg_us > 0.0 && i < 4) ? avg_us / total_avg_us * 100.0 : 0.0;
    if (i == 4)
      std::printf("  -----------------+----------+------------+---------\n");
    if (i < 4)
      std::printf("  %-16s | %8llu | %10.2f | %6.1f%%\n", r.name,
                  (unsigned long long)r.s.count, avg_us, share);
    else
      std::printf("  %-16s | %8llu | %10.2f | 100.0%%\n", r.name,
                  (unsigned long long)r.s.count, avg_us);
  }
  std::fflush(stdout);
}

// ── Reset all counters
// ────────────────────────────────────────────────────────
inline void reset() noexcept
{
  g_http_prof.read_request.reset();
  g_http_prof.route_match.reset();
  g_http_prof.handler.reset();
  g_http_prof.write_response.reset();
  g_http_prof.total.reset();
}

}  // namespace profiling
}  // namespace asyik

// ── Convenience macro: nanoseconds elapsed since a steady_clock time_point
// ──── Usage:  uint64_t ns = ASYIK_PROF_NS(t_start);
#define ASYIK_PROF_NS(t_start)                                                \
  static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>( \
                            std::chrono::steady_clock::now() - (t_start))     \
                            .count())

#endif  // LIBASYIK_HTTP_PROFILING
#endif  // LIBASYIK_PROFILING_HPP
