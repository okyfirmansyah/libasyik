// Shared infrastructure of the libasyik soak executable: scenario
// registration, per-scenario context (seeded RNG, deadline, failure
// recording, progress for the watchdog) and helpers to run services on
// their own threads.

#ifndef LIBASYIK_SOAK_HPP
#define LIBASYIK_SOAK_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "libasyik/service.hpp"

namespace soak {

using clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// Thrown by context::fail(); scenarios let it propagate.
struct failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class context {
 public:
  context(std::string name, uint64_t seed, clock::duration duration)
      : name(std::move(name)),
        seed(seed),
        deadline_(clock::now() + duration),
        duration_(duration)
  {}

  const std::string name;
  const uint64_t seed;

  bool time_left() const { return clock::now() < deadline_ && !failed(); }
  clock::duration duration() const { return duration_; }

  // Independent, reproducible random stream: same seed + stream id gives the
  // same sequence, whichever thread or fiber uses it.
  std::mt19937_64 rng(uint64_t stream) const
  {
    std::seed_seq seq{seed, stream, std::hash<std::string>{}(name)};
    return std::mt19937_64(seq);
  }

  // Records the first failure and throws soak::failure.
  [[noreturn]] void fail(const std::string& what)
  {
    {
      std::lock_guard<std::mutex> l(mtx_);
      if (failure_.empty()) failure_ = what;
    }
    failed_ = true;
    throw failure(what);
  }
  bool failed() const { return failed_; }

  // The scenario cannot run here (e.g. no database); not a failure.
  void skip(const std::string& why)
  {
    std::lock_guard<std::mutex> l(mtx_);
    skipped_ = why;
  }
  std::string skipped() const
  {
    std::lock_guard<std::mutex> l(mtx_);
    return skipped_;
  }
  std::string failure_message() const
  {
    std::lock_guard<std::mutex> l(mtx_);
    return failure_;
  }

  // Watchdog support: scenarios report progress and what they are doing.
  void progress(uint64_t n = 1) { progress_ += n; }
  uint64_t progress_count() const { return progress_; }
  void phase(const char* p) { phase_ = p; }
  const char* current_phase() const { return phase_; }

  // Named counters, printed in the scenario summary.
  void count(const std::string& key, uint64_t n = 1)
  {
    std::lock_guard<std::mutex> l(mtx_);
    counters_[key] += n;
  }
  std::map<std::string, uint64_t> counters() const
  {
    std::lock_guard<std::mutex> l(mtx_);
    return counters_;
  }

 private:
  clock::time_point deadline_;
  clock::duration duration_;
  mutable std::mutex mtx_;
  std::string failure_, skipped_;
  std::atomic<bool> failed_{false};
  std::atomic<uint64_t> progress_{0};
  std::atomic<const char*> phase_{"starting"};
  std::map<std::string, uint64_t> counters_;
};

#define SOAK_CHECK(ctx, cond, msg)                                        \
  do {                                                                    \
    if (!(cond))                                                          \
      (ctx).fail(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                 ": check failed: " #cond " -- " + (msg));                \
  } while (0)

// ── scenario registry ───────────────────────────────────────────────────────

struct scenario {
  std::string name;
  std::string description;
  void (*run)(context&);
};

std::vector<scenario>& registry();

struct registrar {
  registrar(const char* name, const char* description, void (*run)(context&))
  {
    registry().push_back({name, description, run});
  }
};

#define SOAK_SCENARIO(id, name, description)                    \
  static void id(soak::context&);                               \
  static soak::registrar id##_registrar(name, description, id); \
  static void id(soak::context& ctx)

// ── services on their own threads ───────────────────────────────────────────

// An asyik::service running on a dedicated thread. A service installs its
// fiber scheduler on the thread that creates it, so it is created there.
class service_thread {
 public:
  service_thread()
  {
    auto ready = std::make_shared<std::promise<asyik::service_ptr>>();
    auto f = ready->get_future();
    thread_ = std::thread([ready]() {
      auto as = asyik::make_service();
      as->set_default_log_severity(asyik::log_severity::warning);
      ready->set_value(as);
      as->run();
    });
    as_ = f.get();
  }
  service_thread(const service_thread&) = delete;
  service_thread& operator=(const service_thread&) = delete;
  ~service_thread()
  {
    // stop_and_join() reports hangs; this is only the exception path
    if (thread_.joinable()) {
      as_->stop();
      thread_.join();
    }
  }

  const asyik::service_ptr& get() const { return as_; }

  // Stops the service and waits for its thread to finish, including thread
  // exit (fibers left behind make the thread hang there). Fails ctx on
  // timeout; the stuck thread is then detached.
  void stop_and_join(context& ctx, clock::duration limit = 10s)
  {
    if (!thread_.joinable()) return;
    as_->stop();
    auto joined = std::make_shared<std::promise<void>>();
    auto f = joined->get_future();
    std::thread joiner([t = std::move(thread_), joined]() mutable {
      t.join();
      joined->set_value();
    });
    if (f.wait_for(limit) != std::future_status::ready) {
      joiner.detach();
      ctx.fail(
          "service thread did not finish within " +
          std::to_string(
              std::chrono::duration_cast<std::chrono::seconds>(limit).count()) +
          "s of stop() (shutdown hang)");
    }
    joiner.join();
  }

 private:
  asyik::service_ptr as_;
  std::thread thread_;
};

// Runs f in a fiber on as and waits for it from a non-fiber thread,
// rethrowing its exception.
template <typename F>
auto run_on(const asyik::service_ptr& as, F&& f)
{
  return as->execute(std::forward<F>(f)).get();
}

// ── process resources ───────────────────────────────────────────────────────

struct resources {
  long fds = 0;
  long threads = 0;
  long rss_kb = 0;
};
resources sample_resources();

// Deterministic payload of n bytes for a given key, and its checksum.
std::string make_payload(uint64_t key, size_t n);
uint64_t checksum(const std::string& s);

// First free port of a per-scenario range (servers are rebound every round;
// rotating ports avoids TIME_WAIT surprises).
uint16_t next_port();

}  // namespace soak

#endif
