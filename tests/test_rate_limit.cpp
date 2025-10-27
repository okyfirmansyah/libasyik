#include "catch2/catch.hpp"
#include "libasyik/error.hpp"
#include "libasyik/rate_limit.hpp"
#include "libasyik/service.hpp"

namespace asyik {

void _TEST_invoke_rate_limit(){};

TEST_CASE("Test rate limit basic")
{
  auto as = asyik::make_service();
  as->execute([as]() {
    auto limiter = asyik::make_rate_limit_memory(as, 100, 50);

    for (int i = 0; i < 50; i++) limiter->checkpoint("get_status");
    REQUIRE(limiter->get_remaining("get_status") == 50);
    REQUIRE(limiter->get_remaining("get_status2") == 100);

    for (int i = 0; i < 50; i++) REQUIRE(limiter->checkpoint("get_status"));
    REQUIRE(limiter->checkpoint("get_status") == 0);
    REQUIRE(limiter->checkpoint("get_status") == 0);

    asyik::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(limiter->checkpoint("get_status", 2) == 1);
    asyik::sleep_for(std::chrono::milliseconds(40));
    REQUIRE(limiter->get_remaining("get_status") == 2);
    REQUIRE(limiter->checkpoint("get_status", 2) == 2);
    asyik::sleep_for(std::chrono::milliseconds(2200));
    REQUIRE(limiter->get_remaining("get_status") == 100);
    LOG(INFO) << "start checkpoint from async..\n";
    for (int i = 0; i < 50; i++)
      as->async(
          [limiter, i]() { REQUIRE(limiter->checkpoint("get_status") == 1); });
    asyik::sleep_for(std::chrono::milliseconds(5));
    LOG(INFO) << "done\n";
    REQUIRE(limiter->get_remaining("get_status") == 50);

    limiter->reset();
    REQUIRE(limiter->get_remaining("get_status") == 100);
    REQUIRE(limiter->get_remaining("get_status2") == 100);

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
    const int desired_qps = 90;
    const int num_worker = 8;
    const int target_quota = 200;
    const int quota_burst = num_worker + 2;
    auto limiter = asyik::make_rate_limit_memory(as, quota_burst, desired_qps);

    std::atomic<int> total_granted(0);

    LOG(INFO) << "start emulating desired QPS=" << desired_qps << "...\n";
    uint32_t start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    for (int j = 0; j < num_worker; j++)
      as->async([as, limiter, &total_granted, j]() {
        while (total_granted < target_quota) {
          total_granted += limiter->checkpoint("check_status");
          asyik::sleep_for(
              std::chrono::microseconds((1000000 * num_worker) / desired_qps));
        }
      });

    while (total_granted < target_quota)
      asyik::sleep_for(std::chrono::microseconds(100));

    uint32_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

    total_granted -= num_worker;
    unsigned int current_qps = int(total_granted) * 1000 / (stop_ms - start_ms);
    LOG(INFO) << "total granted=" << int(total_granted) << "\n";
    LOG(INFO) << "total ms=" << stop_ms - start_ms << "\n";
    LOG(INFO) << "total qps=" << current_qps << "\n";

    REQUIRE(current_qps >= (desired_qps - 1));
    REQUIRE(current_qps <= (desired_qps + 1));

    as->stop();
  });

  as->run();
}

TEST_CASE("Test complex async rate limit(contention)")
{
  auto as = asyik::make_service();
  as->execute([as]() {
    const int desired_qps = 45;
    const int num_worker = 16;
    const int target_quota = 100;
    const int quota_burst = num_worker + 2;
    auto limiter = asyik::make_rate_limit_memory(as, quota_burst, desired_qps);

    std::atomic<int> total_granted(0);

    LOG(INFO) << "start emulating high qps requests...\n";
    uint32_t start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    for (int j = 0; j < num_worker; j++)
      as->async([as, limiter, &total_granted, j]() {
        while (total_granted < target_quota) {
          total_granted +=
              limiter->checkpoint("check_status", 1 + (rand() % 4));
          asyik::sleep_for(std::chrono::microseconds(50));
        }
      });

    while (total_granted < target_quota)
      asyik::sleep_for(std::chrono::microseconds(100));

    uint32_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

    total_granted -= num_worker;
    unsigned int current_qps = int(total_granted) * 1000 / (stop_ms - start_ms);
    LOG(INFO) << "total granted=" << int(total_granted) << "\n";
    LOG(INFO) << "total ms=" << stop_ms - start_ms << "\n";
    LOG(INFO) << "total qps=" << current_qps << "\n";

    REQUIRE(current_qps >= (desired_qps - 5));
    REQUIRE(current_qps <= (desired_qps + 1));

    as->stop();
  });

  as->run();
}

}  // namespace asyik