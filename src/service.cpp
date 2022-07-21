#include "libasyik/service.hpp"

#include <chrono>
#include <iostream>
#include <regex>

#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/http.hpp"

namespace ip = boost::asio::ip;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik {
std::chrono::time_point<std::chrono::high_resolution_clock>
    service::start;  //!!!

std::shared_ptr<fibers::buffered_channel<std::function<void()>>> service::tasks;
std::shared_ptr<AixLog::Sink> service::default_log_sink;

service::service(struct service::private_&&)
    : stopped(false),
      io_service(),
      strand(io_service),
      execute_tasks(
          std::make_shared<fibers::buffered_channel<std::function<void()>>>(
              1024))
{
  if (!default_log_sink)
    default_log_sink =
        AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::info);
  fibers::use_scheduling_algorithm<fibers::algo::round_robin>();
}

service_ptr make_service()
{
  return std::make_shared<service>(service::private_{});
}

std::atomic<uint32_t> service::async_task_started;
std::atomic<uint32_t> service::async_task_terminated;
std::atomic<uint32_t> service::async_task_error;
std::atomic<uint32_t> service::async_queue_size;

async_stats service::get_async_stats()
{
  async_stats stats;

  stats.task_started = async_task_started;
  stats.task_terminated = async_task_terminated;
  stats.task_error = async_task_error;
  stats.queue_size = async_queue_size;

  return stats;
}

void service::run()
{
  fiber fb([as = shared_from_this()]() {
    std::function<void()> tsk;
    while (!as->stopped && boost::fibers::channel_op_status::closed !=
                               as->execute_tasks->pop(tsk)) {
      fiber fb([tsk_in = std::move(tsk)]() { tsk_in(); });
      fb.detach();
    }
  });

  int idle_threshold = 30000;
  while (!stopped) {
    if (!io_service.poll()) {
      if (idle_threshold) {
        idle_threshold--;
        asyik::sleep_for(std::chrono::microseconds(10));
      } else {
        asyik::sleep_for(std::chrono::microseconds(1000));
      }
    } else
      idle_threshold = 30000;
    if (!stopped) io_service.restart();
  }

  execute_tasks->close();
  for (int i = 0; i < 10000; i++) {
    io_service.poll();
    asyik::sleep_for(std::chrono::microseconds(10));
  }

  fb.join();
}

void service::init_workers()
{
  int pool_size = std::thread::hardware_concurrency() * 2;
  std::atomic_store(
      &tasks,
      std::make_shared<fibers::buffered_channel<std::function<void()>>>(1024));
  is_workers_initiated(true);

  for (std::size_t i = 0; i < (size_t)pool_size; ++i) {
    std::thread th([]() {
      std::function<void()> tsk;
      auto safe_tasks = std::atomic_load(&tasks);
      while (boost::fibers::channel_op_status::closed != safe_tasks->pop(tsk)) {
        async_queue_size--;
        fiber fb([tsk_in = std::move(tsk)]() {
          async_task_started++;
          tsk_in();
          async_task_terminated++;
        });

        fb.detach();
      };
    });
    th.detach();
  };
}
}  // namespace asyik
