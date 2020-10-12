#ifndef LIBASYIK_ASYIK_SERVICE_HPP
#define LIBASYIK_ASYIK_SERVICE_HPP
#include <string>
#include <type_traits>
#include "boost/fiber/all.hpp"
#include "libasyik/common.hpp"
#include "aixlog.hpp"
#include "boost/asio.hpp"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;
namespace asio = boost::asio;

namespace asyik
{
  void _TEST_invoke_service();

  namespace service_internal
  {
    template <typename R>
    struct helper
    {
      template <typename P, typename F, typename... Args>
      static void set(P &&p, F &&f, Args &&... args)
      {
        p->set_value(f(std::forward<Args>(args)...));
      }
    };

    template <>
    struct helper<void>
    {
      template <typename P, typename F, typename... Args>
      static void set(P &&p, F &&f, Args &&... args)
      {
        f(std::forward<Args>(args)...);
        p->set_value();
      }
    };
  }; // namespace service_internal

  template <typename T>
  void sleep_for(T &&t)
  {
    boost::this_fiber::sleep_for(t);
  }

  class service;
  using service_ptr = std::shared_ptr<service>;
  using service_wptr = std::weak_ptr<service>;
  class service : public std::enable_shared_from_this<service>
  {
  private:
    struct private_
    {
    };

  public:
    ~service(){};
    service &operator=(const service &) = delete;
    service() = delete;
    service(const service &) = delete;
    service(service &&) = default;
    service &operator=(service &&) = default;

    service(struct private_ &&);

    template <typename F, typename... Args>
    fibers::future<typename std::result_of<F(Args...)>::type> execute(F &&fun, Args &&... args)
    {
      auto p = std::make_shared<fibers::promise<typename std::result_of<F(Args...)>::type>>();
      auto future = p->get_future();
      strand.post([fun2 = std::forward<F>(fun), &args..., p]()mutable {
        fiber fb([fun3 = std::forward<F>(fun2), p](Args &&... args) {
          try
          {
            service_internal::helper<typename std::result_of<F(Args...)>::type>::set(p, fun3, std::forward<Args>(args)...);
          }
          catch (...)
          {
            p->set_exception(std::current_exception());
          }
        });
        fb.detach();
      });

      return std::move(future);
    }

    template <typename F, typename... Args>
    fibers::future<typename std::result_of<F(Args...)>::type> async(F &&fun, Args &&... args)
    {
      if (!workers_initiated)
        init_workers();

      auto p = std::make_shared<fibers::promise<typename std::result_of<F(Args...)>::type>>();
      auto future = p->get_future();
      auto t = std::atomic_load(&tasks);
      t->push([f = std::forward<F>(fun),
                   &args...,
                   p]() mutable {
        try
        {
          service_internal::helper<typename std::result_of<F(Args...)>::type>::set(p, f, std::forward<Args>(args)...);
        }
        catch (...)
        {
          p->set_exception(std::current_exception());
        };
      });

      return std::move(future);
    };

    void run();
    void stop()
    {
      if (workers_initiated)
      {
        //!! this pass by reference solution could be better
        execute([t=&tasks, w=&workers]() {
          (*t)->close();
          t->reset();
          std::for_each(w->begin(), w->end(), [](auto &t) {
            t.join();
          });
        });
      };
      execute([i=&io_service, w=&workers_initiated, s=&stopped]() {
        i->stop();
        *w = false;
        *s = true;
      });
    };

    boost::asio::io_context &get_io_service() { return io_service; };
    static void terminate();

    static std::chrono::time_point<std::chrono::high_resolution_clock> start;//!!!

  private:
    bool stopped;
    boost::asio::io_context io_service;
    boost::asio::io_context::strand strand;
    void init_workers();
    std::atomic<bool> workers_initiated;
    std::vector<std::thread> workers;
    std::shared_ptr<fibers::buffered_channel<std::function<void()>>> tasks;

  public:
    friend service_ptr make_service();
  };

  service_ptr make_service();

} // namespace asyik

#endif
