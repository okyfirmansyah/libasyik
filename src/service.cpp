#include <iostream>
#include <regex>
#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/service.hpp"
#include "catch2/catch.hpp"
#include "libasyik/http.hpp"

namespace ip = boost::asio::ip;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik
{
  void _TEST_invoke_service(){};

  service::service(struct service::private_ &&) : stopped(false), io_service()
  {
    AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::trace,
                                        AixLog::Type::normal);
    fibers::use_scheduling_algorithm<fibers::algo::round_robin>();

    workers_initiated = false;
  }

  service_ptr make_service() { return std::make_shared<service>(service::private_{}); }

  void service::run()
  {
    int idle_threshold;
    while (!stopped)
    {
      if (!io_service.poll())
      {
        if (idle_threshold)
        {
          idle_threshold--;
          asyik::sleep_for(std::chrono::microseconds(50));
        }
        else
        {
          asyik::sleep_for(std::chrono::microseconds(2000));
          io_service.poll();
        }
      }
      else
        idle_threshold = 300;
    }
  }

  void service::init_workers()
  {
    int pool_size = std::thread::hardware_concurrency();
    tasks = std::make_shared<fibers::buffered_channel<std::function<void()>>>(1024);
    workers_initiated = true;

    for (std::size_t i = 0; i < (size_t)pool_size; ++i)
    {
      workers.emplace_back([p = shared_from_this()]() {
        std::function<void()> tsk;
        while (boost::fibers::channel_op_status::closed != p->tasks->pop(tsk))
        {
          tsk();
        };
      });
    };
  }

  TEST_CASE("very basic fiber execution", "[service]")
  {
    auto as = asyik::make_service();

    as->execute([=] {
      LOG(INFO) << "brief fiber";
      asyik::sleep_for(std::chrono::milliseconds(1));
      as->stop();
    });
    as->run();
    REQUIRE(true);
  }

  TEST_CASE("Check spawn() and execute()", "[service]")
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
    auto fb = as->spawn([&] {
      asyik::sleep_for(std::chrono::milliseconds(20));
      sequence += "B";
      asyik::sleep_for(std::chrono::milliseconds(20));
      sequence += "B";
    });
    fb.join();
    REQUIRE(!sequence.compare("AABAB"));
  }

  TEST_CASE("execute async", "[service]")
  {
    auto as = asyik::make_service();
    std::atomic<int> i(0);

    as->async([&i](int inp) {
        usleep(100);
        i = inp;
      },
              10)
        .get();
    as->async([](auto &i) -> void {
      usleep(0);
      i++;
    },
              i);
    as->async([&i](string_view s, const std::string s2) -> void {
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

    std::string s =
        as->async([](int inp) {
            usleep(100);
            return std::to_string(inp);
          },
                  10)
            .get();

    REQUIRE(!s.compare("10"));

    as->stop();
    as->run();
  }

} // namespace asyik
