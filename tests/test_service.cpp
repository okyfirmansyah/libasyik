
#include "catch2/catch.hpp"
#include "libasyik/error.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"

namespace asyik {
void _TEST_invoke_service(){};

TEST_CASE("test log severity change")
{
  auto as = asyik::make_service();
  LOG(INFO) << "this certainly should be shown\n";
  as->set_default_log_severity(log_severity::debug);
  LOG(DEBUG) << "this also should be shown\n";
  as->set_default_log_severity(log_severity::info);
  LOG(DEBUG) << "this should not be shown!!!\n";
}

TEST_CASE("basic execute and async latency checking", "service")
{
  auto as = asyik::make_service();

  as->execute([]() {
    auto as = asyik::get_current_service();
    REQUIRE(as);

    as->async([]() {}).get();  // just to trigger threadpool creation

    asyik::sleep_for(std::chrono::milliseconds(500));
    auto ts = std::chrono::high_resolution_clock::now();
    as->execute([ts]() {
      auto as = asyik::get_current_service();
      REQUIRE(as);

      auto diff = std::chrono::high_resolution_clock::now() - ts;
      REQUIRE(
          std::chrono::duration_cast<std::chrono::microseconds>(diff).count() <
          20000);
      LOG(INFO) << "duration execute(us)="
                << std::to_string(
                       std::chrono::duration_cast<std::chrono::microseconds>(
                           diff)
                           .count())
                << "\n";
    });
    ts = std::chrono::high_resolution_clock::now();
    as->async([as, ts]() {
      REQUIRE(as == asyik::get_current_service());

      auto diff = std::chrono::high_resolution_clock::now() - ts;
      REQUIRE(
          std::chrono::duration_cast<std::chrono::microseconds>(diff).count() <
          20000);
      LOG(INFO) << "duration async(us)="
                << std::to_string(
                       std::chrono::duration_cast<std::chrono::microseconds>(
                           diff)
                           .count())
                << "\n";
      asyik::sleep_for(std::chrono::milliseconds(500));
      as->stop();
    });
  });

  as->run();
}

TEST_CASE("very basic fiber execution", "[service]")
{
  auto as = asyik::make_service();

  as->execute([] {
    auto as = asyik::get_current_service();
    REQUIRE(as);

    LOG(INFO) << "brief fiber\n";
    asyik::sleep_for(std::chrono::milliseconds(1));
    as->stop();
  });
  as->run();
  REQUIRE(true);
}

TEST_CASE("Check execute()", "[service]")
{
  auto as = asyik::make_service();
  std::string sequence;
  as->execute([&] {
    sequence += "A";
    asyik::sleep_for(std::chrono::milliseconds(10));
    sequence += "A";
    asyik::sleep_for(std::chrono::milliseconds(20));
    sequence += "A";
  });
  as->execute([as, &sequence] {
    asyik::sleep_for(std::chrono::milliseconds(20));
    sequence += "B";
    asyik::sleep_for(std::chrono::milliseconds(20));
    sequence += "B";
    as->stop();
  });
  as->run();
  REQUIRE(!sequence.compare("AABAB"));
}

TEST_CASE("Check return value from execute()", "[service]")
{
  auto as = asyik::make_service();
  as->execute([&]() {
    std::string sequence =
        as->execute([&]() -> std::string { return "hehehe"; }).get();
    REQUIRE(!sequence.compare("hehehe"));

    // return exception
    try {
      int never = as->execute([&]() -> int {
                      throw asyik::already_expired_error("expected");
                    }).get();
      REQUIRE(0);
    } catch (asyik::already_expired_error& e) {
      LOG(INFO) << "exception correctly received\n";
    };
    as->stop();
  });

  as->run();
}

TEST_CASE("Check return value from async()", "[service]")
{
  auto as = asyik::make_service();
  as->execute([&]() {
    std::string sequence =
        as->async([]() -> std::string { return "hehehe"; }).get();
    REQUIRE(!sequence.compare("hehehe"));

    // return exception
    try {
      int never = as->async([]() -> int {
                      throw asyik::already_expired_error("expected");
                    }).get();
      REQUIRE(0);
    } catch (asyik::already_expired_error& e) {
      LOG(INFO) << "exception correctly received\n";
    };
    as->stop();
  });

  as->run();
}

TEST_CASE("Test return value execute from async", "[service]")
{
  auto as = asyik::make_service();

  as->async([as]() {
    REQUIRE(as == asyik::get_current_service());

    std::string test =
        as->execute([]() -> std::string { return "hehe"; }).get();
    REQUIRE(!test.compare("hehe"));
    as->stop();
  });

  as->run();
}

TEST_CASE("execute async", "[service]")
{
  auto as = asyik::make_service();
  std::atomic<int> i(0);

  as->async(
        [&i](int inp) {
          usleep(100);
          i = inp;
        },
        10)
      .get();
#if __cplusplus >= 201402L
  as->async(
      [](auto& i) -> void {
        usleep(0);
        i++;
      },
      i);
#else
  as->async(
      [](std::atomic<int>& i) -> void {
        usleep(0);
        i++;
      },
      i);
#endif
  as->async(
        [&i](string_view s, const std::string s2) -> void {
          usleep(130);
          int k = i + s.length() + s2.length();
          i = k;
        },
        "lol", "ok")
      .get();

  REQUIRE(i == 16);

  as->stop();
  as->run();
}

TEST_CASE("execute async with return value", "[service]")
{
  auto as = asyik::make_service();

  std::string s = as->async(
                        [](int inp) {
                          usleep(100);
                          return std::to_string(inp);
                        },
                        10)
                      .get();

  REQUIRE(!s.compare("10"));

  as->stop();
  as->run();
}

TEST_CASE("stop service from another thread/async", "[service]")
{
  auto as = asyik::make_service();

  as->async([as]() {
    asyik::sleep_for(std::chrono::milliseconds(300));
    as->stop();
  });

  as->run();
}

TEST_CASE("test proper cleanup of function object in execute()", "[service]")
{
  auto as = asyik::make_service();
  auto i_ptr = std::make_shared<int>(1);
  std::weak_ptr<int> i_wptr = i_ptr;
  REQUIRE(!i_wptr.expired());

  as->execute([i_ptr]() { asyik::sleep_for(std::chrono::milliseconds(10)); });

  as->execute([i_wptr, as]() {
    asyik::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(i_wptr.expired());
    as->stop();
  });

  i_ptr.reset();
  as->run();
}

TEST_CASE("test proper cleanup of function object in async()", "[service]")
{
  auto as = asyik::make_service();
  auto i_ptr = std::make_shared<int>(1);
  std::weak_ptr<int> i_wptr = i_ptr;
  REQUIRE(!i_wptr.expired());

  as->async([i_ptr]() { asyik::sleep_for(std::chrono::milliseconds(10)); });

  as->execute([i_wptr, as]() {
    asyik::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(i_wptr.expired());
    as->stop();
  });

  i_ptr.reset();
  as->run();
}

TEST_CASE("testing complex async and execute interaction", "[service]")
{
  auto as = asyik::make_service();
  int count = 0;

  std::thread th([&count, as]() {
    for (int i = 0; i < 500; i++) {
      as->execute([&count, as]() {
        asyik::sleep_for(std::chrono::microseconds(10));
        for (int j = 0; j < 10; j++)
          as->async([&count, as]() {
              for (int l = 0; l < 4; l++)
                as->execute([&count, as]() {
                  asyik::sleep_for(std::chrono::microseconds(1));
                  count++;
                });
            }).get();
        for (int k = 0; k < 100; k++)
          as->execute([&count, as]() {
            for (int m = 0; m < 3; m++)
              as->async([&count, as]() {
                asyik::sleep_for(std::chrono::microseconds(1));
                as->execute([&count]() { count++; });
              });
          });
      });
    }
  });
  th.detach();

  LOG(INFO) << "running complex async and execute..\n";
  as->execute([&count, as]() {
    while (count < 170000) asyik::sleep_for(std::chrono::milliseconds(50));
    LOG(INFO) << "complex async and execute loop finished!\n";
    as->stop();
  });

  as->run();
}

TEST_CASE("testing highly parallel, fiber-i/o blocking async()", "[service]")
{
  auto as = asyik::make_service();
  int count = 0;

  for (int i = 0; i < 1000; i++) {
    as->async([&count, as]() {
      asyik::sleep_for(std::chrono::milliseconds(1500));
      as->execute([&count, as]() { count++; });
    });
  }

  LOG(INFO) << "waiting all async() to be finished..\n";
  as->execute([&count, as]() {
    while (count < 1000) asyik::sleep_for(std::chrono::milliseconds(50));
    LOG(INFO) << "all async() completed!\n";
    as->stop();
  });

  as->run();
}

TEST_CASE(
    "testing highly parallel, fiber-i/o blocking async() with auto stopping "
    "run()",
    "[service]")
{
  auto as = asyik::make_service();
  int count = 0;

  for (int i = 0; i < 1000; i++) {
    as->async([&count, as]() {
      asyik::sleep_for(std::chrono::milliseconds(rand() % 150));
      as->execute([&count, as]() { count++; });
    });
  }

  LOG(INFO) << "waiting all execute() to be finished..\n";

  as->run(true);
}
}  // namespace asyik