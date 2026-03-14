#include "libasyik/service.hpp"

#include <chrono>
#include <cstdlib>
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
  execute_task_count = 0;
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

thread_local service_wptr service::active_service;
void service::run(bool stop_on_complete)
{
  BOOST_ASSERT_MSG(!stopped,
                   "Re-run already stopped service is not supported, please "
                   "use different thread and create new service!");

  service::active_service = shared_from_this();
  fiber fb([as = shared_from_this()]() {
    std::function<void()> tsk;
    while (!as->stopped && boost::fibers::channel_op_status::closed !=
                               as->execute_tasks->pop(tsk)) {
      as->active_fiber_count.fetch_add(1, std::memory_order_relaxed);
      fiber fb([tsk_in = std::move(tsk), as]() mutable {
        // RAII guard: decrement the counter when this fiber finishes,
        // regardless of exceptions. The guard destructor runs inside the
        // still-active fiber (before Boost.Fiber GC takes over), so the
        // decrement is always visible to the post-drain wait loop.
        struct FiberGuard {
          std::atomic<int>& ctr;
          ~FiberGuard() noexcept
          {
            ctr.fetch_sub(1, std::memory_order_release);
          }
        } guard{as->active_fiber_count};

        tsk_in();

        // Eagerly destroy captured objects (beast streams, websockets, any
        // Asio-registered handles) HERE, while this fiber is still executing
        // and the io_context is provably alive.  When Boost.Fiber later
        // reclaims this fiber's context, tsk_in is already empty so its
        // destructor is a no-op and the use-after-free race is eliminated.
        tsk_in = {};
      });
      fb.detach();
    }
  });

  // in-thread io_service loop
  // Use adaptive sleep to avoid busy-polling and excessive clock_gettime
  // syscalls, which are especially expensive on VPS/cloud environments where
  // clock_gettime may not be handled by vDSO and becomes a real syscall.
  int idle_count = 0;
  while (!stopped && (!stop_on_complete || execute_task_count > 0)) {
    if (io_service.poll()) {
      idle_count = 0;
    } else {
      idle_count++;
      if (idle_count < 100) {
        // brief spin phase: yield to other fibers quickly
        boost::this_fiber::yield();
      } else if (idle_count < 200) {
        // short sleep phase
        asyik::sleep_for(std::chrono::microseconds(100));
      } else {
        // deep idle: sleep longer to minimize CPU/syscall overhead
        asyik::sleep_for(std::chrono::milliseconds(5));
      }
    }
    if (!stopped && (!stop_on_complete || execute_task_count > 0))
      io_service.restart();
  }

  execute_tasks->close();

  // Phase 1: drain the task channel – give all dispatched-but-not-yet-started
  // fibers a chance to pick up their task and begin executing.
  for (int i = 0; i < 200; i++) {
    io_service.poll();
    boost::this_fiber::yield();
  }

  fb.join();

  // Phase 2: wait until every active fiber has both finished executing its task
  // AND destroyed its captured objects (tsk_in = {} above). This guarantees
  // that Asio-registered objects (beast websocket streams, etc.) are destroyed
  // while the io_context is still alive, preventing the use-after-free crash
  // that occurs when Boost.Fiber defers context-reclaim past io_context
  // teardown.
  {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (active_fiber_count.load(std::memory_order_acquire) > 0) {
      if (std::chrono::steady_clock::now() > deadline) {
        LOG(WARNING) << "service::run(): "
                     << active_fiber_count.load(std::memory_order_relaxed)
                     << " fiber(s) still active after 1s drain, forcing exit\n";
        break;
      }
      io_service.poll();
      boost::this_fiber::yield();
    }
    // Final flush: any deregistrations queued by the last batch of fiber
    // completions are processed here, before the io_context is destroyed.
    for (int i = 0; i < 20; i++) {
      io_service.poll();
      boost::this_fiber::yield();
    }
  }

  service::active_service.reset();
}

void service::init_workers()
{
  // Get thread multiplier from environment variable, default to 5
  int multiplier = 5;
  const char* env_multiplier = std::getenv("ASYIK_THREAD_MULTIPLIER");
  if (env_multiplier != nullptr) {
    multiplier = std::atoi(env_multiplier);
    if (multiplier <= 0) {
      multiplier = 5;  // fallback to default if invalid value
    }
  }

  int pool_size = std::thread::hardware_concurrency() * multiplier;
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
