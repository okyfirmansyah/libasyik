#ifndef LIBASYIK_ASYIK_SERVICE_HPP
#define LIBASYIK_ASYIK_SERVICE_HPP

#include <string>
#include <type_traits>

#include "aixlog.hpp"
#include "asyik_fwd.hpp"
#include "boost/asio.hpp"
#include "boost/fiber/all.hpp"
#include "common.hpp"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;
namespace asio = boost::asio;

namespace asyik {
using log_severity = AixLog::Severity;

void _TEST_invoke_service();

namespace service_internal {
template <typename R>
struct helper {
  template <typename P, typename F, typename... Args>
  static void set(P& p, F& f, Args&&... args)
  {
    p->set_value(f(std::forward<Args>(args)...));
  }
};

template <>
struct helper<void> {
  template <typename P, typename F, typename... Args>
  static void set(P& p, F& f, Args&&... args)
  {
    f(std::forward<Args>(args)...);
    p->set_value();
  }
};
};  // namespace service_internal

template <typename T>
void sleep_for(T&& t)
{
  boost::this_fiber::sleep_for(t);
}

struct async_stats {
  uint32_t task_started;
  uint32_t task_terminated;
  uint32_t task_error;

  uint32_t queue_size;
};

class service : public std::enable_shared_from_this<service> {
 private:
  struct private_ {};

  static std::atomic<uint32_t> async_task_started;
  static std::atomic<uint32_t> async_task_terminated;
  static std::atomic<uint32_t> async_task_error;
  static std::atomic<uint32_t> async_queue_size;

 public:
  ~service(){};
  service& operator=(const service&) = delete;
  service() = delete;
  service(const service&) = delete;
  service(service&&) = default;
  service& operator=(service&&) = default;

  service(struct private_&&);

  static async_stats get_async_stats();

  template <typename F, typename... Args>
  fibers::future<typename std::result_of<F(Args...)>::type> execute(
      F&& fun, Args&&... args)
  {
    auto p = std::make_shared<
        fibers::promise<typename std::result_of<F(Args...)>::type>>();
    auto future = p->get_future();
    auto t = std::atomic_load(&execute_tasks);
    t->push([f = std::forward<F>(fun), &args..., p]() mutable {
      try {
        service_internal::helper<
            typename std::result_of<F(Args...)>::type>::set(p, f,
                                                            std::forward<Args>(
                                                                args)...);
      } catch (...) {
        p->set_exception(std::current_exception());
      };
    });

    return future;
  }

  void set_default_log_severity(log_severity s)
  {
    BOOST_ASSERT(default_log_sink);
    default_log_sink->filter = AixLog::Filter(s);
  }

  template <typename F, typename... Args>
  fibers::future<typename std::result_of<F(Args...)>::type> async(
      F&& fun, Args&&... args)
  {
    if (!is_workers_initiated()) init_workers();

    auto p = std::make_shared<
        fibers::promise<typename std::result_of<F(Args...)>::type>>();
    auto future = p->get_future();
    auto t = std::atomic_load(&tasks);
    async_queue_size++;
    t->push([f = std::forward<F>(fun), &args..., p]() mutable {
      try {
        service_internal::helper<
            typename std::result_of<F(Args...)>::type>::set(p, f,
                                                            std::forward<Args>(
                                                                args)...);
      } catch (...) {
        async_task_error++;
        p->set_exception(std::current_exception());
      };
    });

    return future;
  };

  void run();
  void stop()
  {
    execute([i = &io_service, s = &stopped]() {
      *s = true;
      std::move(i);
    });
  };
  bool is_stopped() { return stopped; }

  boost::asio::io_context& get_io_service() { return io_service; };
  static void terminate();

  static std::chrono::time_point<std::chrono::high_resolution_clock>
      start;  //!!!

 private:
  static bool is_workers_initiated(bool initiated = false)
  {
    static std::atomic<bool> workers_initiated(false);
    if (initiated) workers_initiated = true;
    return workers_initiated;
  }

  std::atomic<bool> stopped;
  boost::asio::io_context io_service;
  boost::asio::io_context::strand strand;
  std::shared_ptr<fibers::buffered_channel<std::function<void()>>>
      execute_tasks;
  static void init_workers();

  static std::shared_ptr<fibers::buffered_channel<std::function<void()>>> tasks;
  static std::shared_ptr<AixLog::Sink> default_log_sink;

 public:
  friend service_ptr make_service();
};

service_ptr make_service();

}  // namespace asyik

#endif
