#ifndef LIBASYIK_ASYIK_SERVICE_HPP
#define LIBASYIK_ASYIK_SERVICE_HPP
#include <string>
#include "boost/fiber/all.hpp"
#include "libasyik/common.hpp"
#include "aixlog.hpp"
#include "boost/asio.hpp"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;
namespace asio = boost::asio;

namespace asyik
{
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

    template <typename T, typename... V>
    void execute(T &&t, V &&... v)
    {
      boost::fibers::fiber fb([t2 = std::forward<T>(t)](V &&... v) { t2(v...); });
      fb.detach();
    }

    template <typename T, typename... V>
    fiber spawn(T &&t, V &&... v)
    {
      boost::fibers::fiber fb([t2 = std::forward<T>(t)](V &&... v) { t2(v...); });
      return std::move(fb);
    }

    void run();
    void stop() { stopped = true; };

    boost::asio::io_context &get_io_service() { return io_service; };

  private:
    bool stopped;
    boost::asio::io_context io_service;

    friend service_ptr make_service();
  };

  service_ptr make_service();

  template <typename T>
  void sleep_for(T &&t)
  {
    boost::this_fiber::sleep_for(t);
  }

} // namespace asyik

#endif
