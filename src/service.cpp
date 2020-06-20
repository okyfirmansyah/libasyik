#include <iostream>
#include <regex>
#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"
#include <chrono>

namespace ip = boost::asio::ip;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik
{
  std::chrono::time_point<std::chrono::high_resolution_clock> service::start; //!!!

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
          asyik::sleep_for(std::chrono::microseconds(10));
        }
        else
        {
          asyik::sleep_for(std::chrono::microseconds(4000));
        }
      }
      else
        idle_threshold = 10000;
    }
    for (int i = 0; i < 10000; i++)
    {
      io_service.poll();
      asyik::sleep_for(std::chrono::microseconds(10));
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
} // namespace asyik
