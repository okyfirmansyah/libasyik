#include <iostream>
#include <regex>
#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/service.hpp"
#include "catch2/catch.hpp"
#include "libasyik/internal_api.hpp"
#include "libasyik/http.hpp"

namespace ip = boost::asio::ip;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik
{
  service::service(struct service::private_ &&) : stopped(false), io_service()
  {
    AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::trace,
                                        AixLog::Type::normal);
    fibers::use_scheduling_algorithm<fibers::algo::round_robin>();
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
          usleep(10);
        }
        else
        {
          boost::this_fiber::yield();
          usleep(3000);
          io_service.poll();
        }
      }
      else
        idle_threshold = 300;

      boost::this_fiber::yield();
    }
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
} // namespace asyik
