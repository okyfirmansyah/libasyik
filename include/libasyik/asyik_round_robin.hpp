#ifndef LIBASYIK_ASYIK_ROUND_ROBIN_HPP
#define LIBASYIK_ASYIK_ROUND_ROBIN_HPP

#include <atomic>
#include <boost/asio/error.hpp>
#include <boost/assert.hpp>
#include <boost/fiber/algo/algorithm.hpp>
#include <boost/fiber/context.hpp>
#include <boost/fiber/scheduler.hpp>
#include <boost/fiber/type.hpp>
#include <boost/system/system_error.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "error.hpp"

namespace asyik {

// Custom round-robin fiber scheduler with cooperative stop/interrupt support.
//
// When request_stop() is called, a thread-local "stopped" flag is set.
// Any worker fiber reaching a suspension point instrumented with
// check_interrupt() will throw boost::system::system_error(operation_aborted).
//
// The mechanism:
//  1. If request_stop() was already called when a fiber hits check_interrupt(),
//     the exception is thrown immediately (before suspending).
//  2. If request_stop() is called while fibers are suspended, they will throw
//     the next time they resume and reach a check_interrupt() call.
//
// Only worker fibers are interrupted; main_context and dispatcher_context are
// left alone so the service run-loop can drain gracefully.
class asyik_round_robin : public boost::fibers::algo::algorithm {
 private:
  using rqueue_type = boost::fibers::scheduler::ready_queue_type;

  rqueue_type rqueue_{};
  std::mutex mtx_{};
  std::condition_variable cnd_{};
  bool flag_{false};
  std::atomic<bool> stopped_{false};

  // Thread-local pointer to the scheduler instance for this thread.
  // Uses static-local trick to avoid requiring a .cpp definition file.
  static asyik_round_robin*& instance_ref_() noexcept
  {
    static thread_local asyik_round_robin* ptr = nullptr;
    return ptr;
  }

 public:
  asyik_round_robin() { instance_ref_() = this; }

  asyik_round_robin(const asyik_round_robin&) = delete;
  asyik_round_robin& operator=(const asyik_round_robin&) = delete;

  void awakened(boost::fibers::context* ctx) noexcept override
  {
    BOOST_ASSERT(nullptr != ctx);
    BOOST_ASSERT(!ctx->ready_is_linked());
    BOOST_ASSERT(ctx->is_resumable());
    ctx->ready_link(rqueue_);
  }

  boost::fibers::context* pick_next() noexcept override
  {
    boost::fibers::context* victim = nullptr;
    if (!rqueue_.empty()) {
      victim = &rqueue_.front();
      rqueue_.pop_front();
      BOOST_ASSERT(nullptr != victim);
      BOOST_ASSERT(!victim->ready_is_linked());
      BOOST_ASSERT(victim->is_resumable());
    }
    return victim;
  }

  bool has_ready_fibers() const noexcept override { return !rqueue_.empty(); }

  void suspend_until(
      std::chrono::steady_clock::time_point const& time_point) noexcept override
  {
    if ((std::chrono::steady_clock::time_point::max)() == time_point) {
      std::unique_lock<std::mutex> lk{mtx_};
      cnd_.wait(lk, [&]() { return flag_; });
      flag_ = false;
    } else {
      std::unique_lock<std::mutex> lk{mtx_};
      cnd_.wait_until(lk, time_point, [&]() { return flag_; });
      flag_ = false;
    }
  }

  void notify() noexcept override
  {
    std::unique_lock<std::mutex> lk{mtx_};
    flag_ = true;
    lk.unlock();
    cnd_.notify_all();
  }

  // ---- Stop / interrupt mechanism ----

  // Signal all fibers on this thread's scheduler to terminate.
  // Sets the stopped flag and wakes the scheduler from suspend_until().
  void request_stop() noexcept
  {
    stopped_.store(true, std::memory_order_release);
    notify();
  }

  bool is_stopped() const noexcept
  {
    return stopped_.load(std::memory_order_acquire);
  }

  // Get this thread's scheduler instance (nullptr if not using this scheduler).
  static asyik_round_robin* current() noexcept { return instance_ref_(); }

  // If the current thread's scheduler has been stopped AND the calling fiber
  // is a worker fiber, throw service_terminated_error.  Main and dispatcher
  // contexts are never interrupted so the service run-loop can drain
  // gracefully.
  static void check_interrupt()
  {
    auto* sched = instance_ref_();
    if (sched && sched->stopped_.load(std::memory_order_acquire)) {
      auto* ctx = boost::fibers::context::active();
      if (ctx && ctx->is_context(boost::fibers::type::worker_context)) {
        throw service_terminated_error("service stopped");
      }
    }
  }
};

}  // namespace asyik

#endif  // LIBASYIK_ASYIK_ROUND_ROBIN_HPP
