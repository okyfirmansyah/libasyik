#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>

#include "catch2/catch.hpp"
#include "libasyik/error.hpp"
#include "libasyik/rate_limit.hpp"
#include "libasyik/service.hpp"

namespace asyik {

void _TEST_invoke_rate_limit(){};

namespace {
using test_clock = std::chrono::steady_clock;

// Most tokens a limiter refilling `rate` tokens/s can have added to a key
// whose first checkpoint happened after `since`. The limiter timestamps in
// whole milliseconds, hence the extra 1ms.
//
// Timing checks use this instead of assuming a fixed delay: on a quiet machine
// the bound is 0 and checks are exact, while a stalled CI machine only gets
// the refill that the measured time actually allows.
unsigned max_refill(unsigned rate, test_clock::time_point since)
{
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                test_clock::now() - since)
                .count();
  return static_cast<unsigned>((ms + 1) * rate / 1000);
}

int64_t steady_now_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             test_clock::now().time_since_epoch())
      .count();
}
}  // namespace

TEST_CASE("Test rate limit basic")
{
  auto as = asyik::make_service();
  as->execute([as]() {
    // 100-token buckets refilling 50 tokens/s (one token per 20ms). Each
    // phase uses a fresh key so its refill clock starts inside the phase.
    auto limiter = asyik::make_rate_limit_memory(as, 100, 50);

    // consuming: 50 of 100 tokens
    auto since = test_clock::now();
    for (int i = 0; i < 50; i++) REQUIRE(limiter->checkpoint("consume") == 1);
    auto remaining = limiter->get_remaining("consume");
    REQUIRE(remaining >= 50);
    REQUIRE(remaining <= 50 + max_refill(50, since));
    REQUIRE(limiter->get_remaining("untouched") == 100);

    // exhausting: a drained bucket grants only what has refilled since
    since = test_clock::now();
    unsigned granted = 0;
    for (int i = 0; i < 100; i++) granted += limiter->checkpoint("drain");
    REQUIRE(granted == 100);
    REQUIRE(limiter->checkpoint("drain", 100) <= max_refill(50, since));
    REQUIRE(limiter->checkpoint("drain", 100) <= max_refill(50, since));

    // refilling: sleeping 100ms refills at least 4 tokens (99ms after
    // millisecond truncation) and at most what the elapsed time allows
    asyik::sleep_for(std::chrono::milliseconds(100));
    remaining = limiter->get_remaining("drain");
    REQUIRE(remaining >= 4);
    REQUIRE(remaining <= max_refill(50, since));
    REQUIRE(limiter->checkpoint("drain", 2) == 2);

    // capping: refill never exceeds the bucket size
    asyik::sleep_for(std::chrono::milliseconds(2200));
    REQUIRE(limiter->get_remaining("drain") == 100);

    // concurrency: checkpoints from 50 async() calls must each consume exactly
    // one token. Wait for all of them; async() dispatch alone can take over a
    // second on a loaded CI machine, so allow the refill that elapsed. A 1
    // token/s limiter keeps that allowance small enough to still catch a lost
    // update.
    LOG(INFO) << "start checkpoint from async..\n";
    auto slow_limiter = asyik::make_rate_limit_memory(as, 100, 1);
    since = test_clock::now();
    auto done = std::make_shared<std::atomic<int>>(0);
    for (int i = 0; i < 50; i++)
      as->async([slow_limiter, done]() {
        auto granted = slow_limiter->checkpoint("concurrent");
        (*done)++;
        REQUIRE(granted == 1);
      });
    while (*done < 50) asyik::sleep_for(std::chrono::milliseconds(1));
    LOG(INFO) << "done\n";
    remaining = slow_limiter->get_remaining("concurrent");
    REQUIRE(remaining >= 50);
    REQUIRE(remaining <= 50 + max_refill(1, since));

    limiter->reset();
    REQUIRE(limiter->get_remaining("consume") == 100);
    REQUIRE(limiter->get_remaining("drain") == 100);

    as->stop();
  });

  REQUIRE_THROWS_AS(asyik::make_rate_limit_memory(as, 5001, 50),
                    std::out_of_range);
  as->run();
}

TEST_CASE("Test complex async rate limit(achieve qps)")
{
  auto as = asyik::make_service();
  as->execute([as]() {
    static constexpr int desired_qps = 90;
    static constexpr int num_worker = 8;
    static constexpr int target_quota = 200;
    static constexpr int quota_burst = num_worker + 2;
    auto limiter = asyik::make_rate_limit_memory(as, quota_burst, desired_qps);

    std::atomic<int> total_granted(0);
    std::atomic<int> total_requested(0);
    // Timing starts when the first worker runs: async() dispatch latency is
    // not part of the rate being measured.
    std::atomic<int64_t> start_ms(0);

    LOG(INFO) << "start emulating desired QPS=" << desired_qps << "...\n";

    for (int j = 0; j < num_worker; j++)
      as->async([as, limiter, &total_granted, &total_requested, &start_ms]() {
        int64_t unset = 0;
        start_ms.compare_exchange_strong(unset, steady_now_ms());
        while (total_granted < target_quota) {
          total_requested++;
          total_granted += limiter->checkpoint("check_status");
          asyik::sleep_for(
              std::chrono::microseconds((1000000 * num_worker) / desired_qps));
        }
      });

    while (total_granted < target_quota)
      asyik::sleep_for(std::chrono::microseconds(100));

    int64_t elapsed_ms = steady_now_ms() - start_ms;
    // read requests before grants, so a request still in flight can only
    // understate the offered rate
    int requested = int(total_requested) - num_worker;
    int granted = int(total_granted) - num_worker;
    int current_qps = int(granted * 1000 / elapsed_ms);
    int offered_qps = int(requested * 1000 / elapsed_ms);
    LOG(INFO) << "total granted=" << granted << "\n";
    LOG(INFO) << "total ms=" << elapsed_ms << "\n";
    LOG(INFO) << "total qps=" << current_qps << " (offered " << offered_qps
              << ")\n";

    // Workers pace themselves to exactly desired_qps, but a loaded machine
    // (or Windows' coarser sleeps) can make them offer less. The limiter must
    // grant whatever was offered up to the desired rate, and never more.
    REQUIRE(current_qps >= std::min(offered_qps, desired_qps) - 1);
    REQUIRE(current_qps <= (desired_qps + 1));

    as->stop();
  });

  as->run();

  // this is required because of the static variables in rate limit
  // implementation, otherwise
  asyik::sleep_for(std::chrono::milliseconds(1500));
}

TEST_CASE("Test complex async rate limit(contention)")
{
  auto as = asyik::make_service();
  as->execute([as]() {
    static constexpr int desired_qps = 45;
    static constexpr int num_worker = 16;
    static constexpr int target_quota = 100;
    static constexpr int quota_burst = num_worker + 2;
    auto limiter = asyik::make_rate_limit_memory(as, quota_burst, desired_qps);

    std::atomic<int> total_granted(0);
    // Timing starts when the first worker runs (see above)
    std::atomic<int64_t> start_ms(0);

    LOG(INFO) << "start emulating high qps requests...\n";

    for (int j = 0; j < num_worker; j++)
      as->async([as, limiter, &total_granted, &start_ms]() {
        int64_t unset = 0;
        start_ms.compare_exchange_strong(unset, steady_now_ms());
        while (total_granted < target_quota) {
          total_granted +=
              limiter->checkpoint("check_status", 1 + (rand() % 4));
          asyik::sleep_for(std::chrono::microseconds(50));
        }
      });

    while (total_granted < target_quota)
      asyik::sleep_for(std::chrono::microseconds(100));

    int64_t elapsed_ms = steady_now_ms() - start_ms;
    int granted = int(total_granted) - num_worker;
    int current_qps = int(granted * 1000 / elapsed_ms);
    LOG(INFO) << "total granted=" << granted << "\n";
    LOG(INFO) << "total ms=" << elapsed_ms << "\n";
    LOG(INFO) << "total qps=" << current_qps << "\n";

    REQUIRE(current_qps >= (desired_qps - 5));
    REQUIRE(current_qps <= (desired_qps + 1));

    as->stop();
  });

  as->run();
}

}  // namespace asyik