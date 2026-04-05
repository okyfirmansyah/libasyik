
#include <boost/asio/ip/udp.hpp>

#include "catch2/catch.hpp"
#include "libasyik/asyik_round_robin.hpp"
#include "libasyik/error.hpp"
#include "libasyik/http.hpp"
#include "libasyik/internal/use_fiber_future.hpp"
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

// ---------------------------------------------------------------------------
// Scheduler-level termination tests
// ---------------------------------------------------------------------------

TEST_CASE("scheduler stop terminates fiber sleeping (stop before sleep)",
          "[service][scheduler_stop]")
{
  // Scenario: scheduler is already stopped when fiber reaches sleep_for.
  // The fiber should throw immediately at the check_interrupt() call
  // inside asyik::sleep_for(), without actually sleeping.
  auto as = asyik::make_service();
  std::atomic<bool> caught{false};
  std::atomic<bool> slept{false};

  as->execute([&]() {
    // Trigger scheduler stop directly
    auto* sched = asyik::asyik_round_robin::current();
    REQUIRE(sched != nullptr);
    sched->request_stop();

    // Now spawn a new fiber — it should throw when trying to sleep
    as->execute([&]() {
      try {
        asyik::sleep_for(std::chrono::seconds(60));
        slept = true;  // should never reach here
      } catch (asyik::service_terminated_error& e) {
        REQUIRE(e.code() == boost::asio::error::operation_aborted);
        caught = true;
      }
    });

    // Let the spawned fiber run
    boost::this_fiber::yield();
    boost::this_fiber::yield();

    as->stop();
  });

  as->run();
  REQUIRE(caught);
  REQUIRE_FALSE(slept);
}

TEST_CASE("scheduler stop terminates fiber sleeping (stop after sleep starts)",
          "[service][scheduler_stop]")
{
  // Scenario: fiber begins a long sleep, then scheduler stop is triggered.
  // The fiber should throw when it wakes up and reaches check_interrupt()
  // after the sleep returns.
  auto as = asyik::make_service();
  std::atomic<bool> caught{false};
  std::atomic<bool> continued_after_sleep{false};

  as->execute([&]() {
    // Spawn a fiber that sleeps briefly
    as->execute([&]() {
      try {
        // Sleep for a short time — when it returns, check_interrupt() fires
        asyik::sleep_for(std::chrono::milliseconds(50));
        continued_after_sleep = true;  // should not reach here
      } catch (asyik::service_terminated_error& e) {
        REQUIRE(e.code() == boost::asio::error::operation_aborted);
        caught = true;
      }
    });

    // Give the sleeping fiber a chance to start
    boost::this_fiber::sleep_for(std::chrono::milliseconds(10));

    // Now trigger scheduler stop while the fiber is still sleeping
    auto* sched = asyik::asyik_round_robin::current();
    REQUIRE(sched != nullptr);
    sched->request_stop();

    // Wait for the sleeping fiber to wake up and hit check_interrupt
    boost::this_fiber::sleep_for(std::chrono::milliseconds(100));

    as->stop();
  });

  as->run();
  REQUIRE(caught);
  REQUIRE_FALSE(continued_after_sleep);
}

TEST_CASE("scheduler stop terminates fiber waiting on fibers::promise",
          "[service][scheduler_stop]")
{
  // Scenario: a fiber is blocked on fibers::future::get() for a promise
  // that will never be fulfilled normally. The orchestrator breaks the
  // promise (destroys it) to unblock the waiting fiber, simulating what
  // happens during shutdown when promise owners are torn down.
  auto as = asyik::make_service();
  std::atomic<bool> caught{false};
  std::atomic<bool> got_value{false};
  auto promise = std::make_shared<boost::fibers::promise<int>>();
  // Get the future BEFORE spawning, so the fiber doesn't hold the promise
  auto future =
      std::make_shared<boost::fibers::future<int>>(promise->get_future());

  as->execute([&]() {
    // Spawn a fiber that waits on the future
    as->execute([&, future]() {
      try {
        int val = future->get();
        got_value = true;  // should not reach here
        (void)val;
      } catch (boost::fibers::future_error&) {
        // broken_promise when the promise is destroyed without setting value
        caught = true;
      } catch (boost::system::system_error& e) {
        if (e.code() == boost::asio::error::operation_aborted) {
          caught = true;
        }
      }
    });

    // Give the waiting fiber a chance to block on the future
    asyik::sleep_for(std::chrono::milliseconds(50));

    // Break the promise by destroying it — this unblocks the fiber with
    // a broken_promise exception, simulating resource teardown at shutdown
    promise.reset();

    // Let the woken fiber run
    asyik::sleep_for(std::chrono::milliseconds(50));

    as->stop();
  });

  as->run();
  REQUIRE(caught);
  REQUIRE_FALSE(got_value);
}

TEST_CASE("scheduler stop terminates fiber waiting on ASIO UDP read",
          "[service][scheduler_stop]")
{
  // Scenario: a fiber is blocked on an async UDP receive via
  // asyik::use_fiber_future. Closing the socket cancels the pending
  // operation and unblocks the fiber with operation_aborted.
  auto as = asyik::make_service();
  std::atomic<bool> caught{false};
  std::atomic<bool> received{false};

  as->execute([&]() {
    auto& io = as->get_io_service();

    // Open a UDP socket bound to a random port — nobody will send to it
    auto socket = std::make_shared<boost::asio::ip::udp::socket>(
        io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));

    as->execute([&, socket]() {
      try {
        std::array<char, 1024> buf;
        boost::asio::ip::udp::endpoint sender_ep;

        // This will suspend the fiber waiting for a UDP packet
        // that will never arrive (until the socket is closed)
        auto sz = socket->async_receive_from(
            boost::asio::buffer(buf), sender_ep, asyik::use_fiber_future);
        sz.get();
        received = true;  // should not reach here
      } catch (boost::system::system_error& e) {
        // operation_aborted from socket close
        REQUIRE(e.code() == boost::asio::error::operation_aborted);
        caught = true;
      }
    });

    // Give the UDP fiber a chance to start the async read
    asyik::sleep_for(std::chrono::milliseconds(50));

    // Close the socket — this cancels the pending async_receive_from,
    // which unblocks the fiber with operation_aborted
    socket->close();

    // Let the fiber process the cancellation
    asyik::sleep_for(std::chrono::milliseconds(50));

    as->stop();
  });

  as->run();
  REQUIRE(caught);
  REQUIRE_FALSE(received);
}

TEST_CASE("scheduler stop terminates multiple fibers at suspension points",
          "[service][scheduler_stop]")
{
  // Scenario: multiple fibers are suspended (sleeping, waiting on promise,
  // waiting on ASIO). Different mechanisms unblock each:
  //   - sleeping fiber: scheduler check_interrupt() in sleep_for
  //   - promise fiber: promise destroyed → broken_promise
  //   - UDP fiber: socket closed → operation_aborted
  auto as = asyik::make_service();
  std::atomic<int> caught_count{0};
  auto promise = std::make_shared<boost::fibers::promise<int>>();
  auto future =
      std::make_shared<boost::fibers::future<int>>(promise->get_future());

  as->execute([&]() {
    auto& io = as->get_io_service();

    // Fiber 1: sleeping — uses a short sleep so it wakes up and hits
    // check_interrupt() after the scheduler stop flag is set.
    // (Waking sleeping fibers early is not supported; we rely on the
    // post-sleep check_interrupt.)
    as->execute([&]() {
      try {
        asyik::sleep_for(std::chrono::milliseconds(80));
      } catch (asyik::service_terminated_error&) {
        caught_count++;
      }
    });

    // Fiber 2: waiting on unfulfilled promise (captures only the future)
    as->execute([&, future]() {
      try {
        future->get();
      } catch (boost::fibers::future_error&) {
        caught_count++;
      } catch (boost::system::system_error& e) {
        if (e.code() == boost::asio::error::operation_aborted) {
          caught_count++;
        }
      }
    });

    // Fiber 3: waiting on UDP read
    auto socket = std::make_shared<boost::asio::ip::udp::socket>(
        io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
    as->execute([&, socket]() {
      try {
        std::array<char, 1024> buf;
        boost::asio::ip::udp::endpoint sender_ep;
        socket
            ->async_receive_from(boost::asio::buffer(buf), sender_ep,
                                 asyik::use_fiber_future)
            .get();
      } catch (boost::system::system_error& e) {
        if (e.code() == boost::asio::error::operation_aborted) {
          caught_count++;
        }
      }
    });

    // Let all fibers reach their suspension points
    asyik::sleep_for(std::chrono::milliseconds(50));

    // Trigger scheduler stop — this will interrupt the sleeping fiber
    auto* sched = asyik::asyik_round_robin::current();
    REQUIRE(sched != nullptr);
    sched->request_stop();

    // Break the promise to unblock fiber 2
    promise.reset();

    // Close the socket to unblock fiber 3
    socket->close();

    // Let fibers process the interruptions
    boost::this_fiber::sleep_for(std::chrono::milliseconds(100));

    as->stop();
  });

  as->run();
  REQUIRE(caught_count == 3);
}

}  // namespace asyik