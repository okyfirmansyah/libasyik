#ifndef LIBASYIK_ASYIK_SERVICE_HPP
#define LIBASYIK_ASYIK_SERVICE_HPP

#include <memory>
#include <string>
#include <tuple>
#include <type_traits>

#include "aixlog.hpp"
#include "asyik_fwd.hpp"
#include "asyik_round_robin.hpp"
#include "boost/asio.hpp"
#include "boost/fiber/all.hpp"
#include "common.hpp"
#include "pooled_guarded_stack.hpp"

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

// Nullary callable invoking f with args, for tasks that run after execute()
// or async() returned. Temporaries (rvalue args) are moved into it, so they
// outlive the call; lvalue args are passed by reference, so the caller must
// keep them alive until the task has run. The args sit behind a shared_ptr
// because tasks are stored in std::function, which needs copyable callables.
template <typename F, typename... Args>
auto bind_args(F&& f, Args&&... args)
{
  if constexpr (sizeof...(Args) == 0) {
    return std::forward<F>(f);
  } else {
    return [f = std::forward<F>(f),
            a = std::make_shared<std::tuple<Args...>>(
                std::forward<Args>(args)...)]() mutable -> decltype(auto) {
      return std::apply(
          [&f](auto&... x) -> decltype(auto) {
            return f(static_cast<Args&&>(x)...);
          },
          *a);
    };
  }
}
};  // namespace service_internal

template <typename T>
void sleep_for(T&& t)
{
  asyik_round_robin::check_interrupt();
  boost::this_fiber::sleep_for(t);
  asyik_round_robin::check_interrupt();
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
  static thread_local service_wptr active_service;
  std::atomic<uint32_t> execute_task_count;

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
    execute_task_count++;
    t->push([f = service_internal::bind_args(std::forward<F>(fun),
                                             std::forward<Args>(args)...),
             p, this]() mutable {
      try {
        service_internal::helper<
            typename std::result_of<F(Args...)>::type>::set(p, f);
      } catch (...) {
        p->set_exception(std::current_exception());
      };
      execute_task_count--;
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
    t->push([f = service_internal::bind_args(std::forward<F>(fun),
                                             std::forward<Args>(args)...),
             as = weak_from_this(), p]() mutable {
      auto prev_as = service::active_service;
      service::active_service = as;
      try {
        service_internal::helper<
            typename std::result_of<F(Args...)>::type>::set(p, f);
      } catch (...) {
        async_task_error++;
        p->set_exception(std::current_exception());
      };
      service::active_service = prev_as;
    });

    return future;
  };

  void run(bool stop_on_complete = false);
  void stop()
  {
    execute([s = &stopped, cv = &terminate_req_cond, i = &io_service]() {
      *s = true;
      std::move(i);
    });
  };
  bool is_stopped() { return stopped; }

  static service_ptr get_current_service() { return active_service.lock(); }

  boost::asio::io_context& get_io_service() { return io_service; };

  // TLS client context used by https:// and wss:// requests made through this
  // service when the call does not pass one. nullptr (the default) means
  // tls::default_client_context().
  void set_tls_client_context(tls::client_context_ptr ctx)
  {
    std::atomic_store(&tls_client_context, std::move(ctx));
  }
  tls::client_context_ptr get_tls_client_context() const;
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
  // Counts dispatched fibers that have not yet fully exited (their function
  // returned AND their captured objects destroyed). service::run() waits for
  // this to reach zero before returning so that Asio-registered objects
  // (e.g. beast::websocket::stream) are always destroyed while the
  // io_context is still alive.
  std::atomic<int> active_fiber_count{0};
  boost::asio::io_context io_service;
  boost::asio::io_context::strand strand;
  std::shared_ptr<fibers::buffered_channel<std::function<void()>>>
      execute_tasks;
  static void init_workers();

  static std::shared_ptr<fibers::buffered_channel<std::function<void()>>> tasks;
  static std::shared_ptr<AixLog::Sink> default_log_sink;

  boost::fibers::condition_variable terminate_req_cond;
  boost::fibers::mutex terminate_req_mtx;
  pooled_guarded_stack fiber_stack_pool_;
  tls::client_context_ptr tls_client_context;

 public:
  friend service_ptr make_service();
};

inline static service_ptr get_current_service()
{
  return service::get_current_service();
}

service_ptr make_service();

}  // namespace asyik

#endif
