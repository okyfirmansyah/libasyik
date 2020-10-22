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

  std::shared_ptr<fibers::buffered_channel<std::function<void()>>> service::tasks;

  service::service(struct service::private_ &&) : stopped(false), io_service(), strand(io_service)
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
          asyik::sleep_for(std::chrono::microseconds(10));
        }
        else
        {
          asyik::sleep_for(std::chrono::microseconds(4000));
        }
      }
      else
        idle_threshold = 10000;
      if (!stopped)
        io_service.restart();
    }
    for (int i = 0; i < 10000; i++)
    {
      io_service.poll();
      asyik::sleep_for(std::chrono::microseconds(10));
    }
  }

  void service::init_workers()
  {
    int pool_size = std::thread::hardware_concurrency() * 4;
    std::atomic_store(&tasks, std::make_shared<fibers::buffered_channel<std::function<void()>>>(1024));
    is_workers_initiated(true);

    for (std::size_t i = 0; i < (size_t)pool_size; ++i)
    {
      std::thread th([]() {
        auto as = asyik::make_service();

        as->execute([as]() {
          std::function<void()> tsk;
          auto safe_tasks = std::atomic_load(&tasks);
          while (boost::fibers::channel_op_status::closed != safe_tasks->pop(tsk))
          {
            as->execute([tsk_in = std::move(tsk)]() {
              tsk_in();
            });
          };

          as->stop();
        });

        as->run();
      });
      th.detach();
    };
  }
} // namespace asyik
