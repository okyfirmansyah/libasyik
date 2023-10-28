#include "libasyik/service.hpp"

#include <chrono>
#include <iostream>
#include <regex>

#include "aixlog.hpp"
#include "boost/fiber/all.hpp"

namespace ip = boost::asio::ip;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik {
std::chrono::time_point<std::chrono::high_resolution_clock>
    service::start;  //!!!

std::shared_ptr<fibers::buffered_channel<std::function<void()>>> service::tasks;
std::shared_ptr<AixLog::Sink> service::default_log_sink;

service::service(struct service::private_&&, int thread_num)
    : stopped(false),
      io_service(),
      strand(io_service),
      execute_tasks(
          std::make_shared<fibers::buffered_channel<std::function<void()>>>(
              1024)),
      io_service_thread_num(thread_num)
{
  BOOST_ASSERT_MSG(
      io_service_thread_num >= 0,
      "service's thread number should be greater or equal than 0!");

  if (!default_log_sink)
    default_log_sink =
        AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::info);
  fibers::use_scheduling_algorithm<fibers::algo::round_robin>();
}

service_ptr make_service(int thread_num)
{
  return std::make_shared<service>(service::private_{}, thread_num);
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

thread_local service_wptr service::active_service;
void service::run()
{
  BOOST_ASSERT_MSG(!stopped,
                   "Re-run already stopped service is not supported, please "
                   "use different thread and create new service!");

  service::active_service = shared_from_this();
  fiber fb([as = shared_from_this()]() {
    std::function<void()> tsk;
    while (!as->stopped && boost::fibers::channel_op_status::closed !=
                               as->execute_tasks->pop(tsk)) {
      fiber fb([tsk_in = std::move(tsk)]() { tsk_in(); });
      fb.detach();
    }
  });

  std::shared_ptr<boost::asio::io_service::work> work;
  std::vector<std::thread> t;

  if (!io_service_thread_num) {
    // in-thread io_service loop
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
  } else {
    // separate thread io_service loop
    work = std::make_shared<boost::asio::io_service::work>(io_service);
    sched_param sch_params;
    for (int i = 0; i < io_service_thread_num; i++) {
      t.emplace_back([i = &io_service, s = &stopped, &sch_params]() {
        while (*s == false) {
          i->run();
        }
      });
      sch_params.sched_priority = 2;
      pthread_setschedparam(t.back().native_handle(), SCHED_FIFO, &sch_params);
    }

    // wait/yield until stop is signaled
    std::unique_lock<fibers::mutex> l(terminate_req_mtx);
    while (!stopped) terminate_req_cond.wait(l);
    l.unlock();

    io_service.stop();

    for (int i = 0; i < io_service_thread_num; i++) t.at(i).join();
    std::move(io_service);
  }

  execute_tasks->close();
  for (int i = 0; i < 10000; i++) {
    if (!io_service_thread_num) io_service.poll();  // build-in thread only
    asyik::sleep_for(std::chrono::microseconds(10));
  }

  fb.join();
  service::active_service.reset();
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
