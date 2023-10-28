#ifndef LIBASYIK_ASYIK_ASIO_INTERNAL_HPP
#define LIBASYIK_ASYIK_ASIO_INTERNAL_HPP
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/fiber/all.hpp>
#include <string>

#include "use_fiber_future.hpp"

namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace ip = boost::asio::ip;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = ip::tcp;

namespace asyik {
namespace internal {
namespace socket {
template <typename... Args>
auto async_read(Args&&... args) -> boost::fibers::future<size_t>
{
  return boost::asio::async_read(std::forward<Args>(args)..., use_fiber_future);
};

template <typename... Args>
auto async_write(Args&&... args) -> boost::fibers::future<size_t>
{
  return boost::asio::async_write(std::forward<Args>(args)...,
                                  use_fiber_future);
};

template <typename... Args>
auto async_read_until(Args&&... args) -> boost::fibers::future<size_t>
{
  return boost::asio::async_read_until(std::forward<Args>(args)...,
                                       use_fiber_future);
};

template <typename T>
auto async_timer_wait(T&& p) -> boost::fibers::future<void>
{
  return p.async_wait(use_fiber_future);
};

template <typename Resolver, typename... Args>
auto async_resolve(Resolver& res, Args&&... args)
    -> boost::fibers::future<tcp::resolver::results_type>
{
  return res.async_resolve(std::forward<Args>(args)..., use_fiber_future);
};

template <typename Conn, typename... Args>
auto async_connect(Conn& con, Args&&... args)
    -> boost::fibers::future<tcp::resolver::results_type::endpoint_type>
{
  return con.async_connect(std::forward<Args>(args)..., use_fiber_future);
}
}  // namespace socket

namespace http {
template <typename... Args>
auto async_read_header(Args&&... args) -> boost::fibers::future<size_t>
{
  return boost::beast::http::async_read_header(std::forward<Args>(args)...,
                                               use_fiber_future);
};

template <typename... Args>
auto async_read(Args&&... args) -> boost::fibers::future<size_t>
{
  return boost::beast::http::async_read(std::forward<Args>(args)...,
                                        use_fiber_future);
};

template <typename... Args>
auto async_write(Args&&... args) -> boost::fibers::future<size_t>
{
  return boost::beast::http::async_write(std::forward<Args>(args)...,
                                         use_fiber_future);
}
}  // namespace http

namespace ssl {
template <typename Conn, typename... Args>
auto async_handshake(Conn& con, Args&&... args) -> boost::fibers::future<void>
{
  return con.async_handshake(std::forward<Args>(args)..., use_fiber_future);
}

template <typename Conn, typename... Args>
auto async_shutdown(Conn& con, Args&&... args) -> boost::fibers::future<void>
{
  return con.async_shutdown(std::forward<Args>(args)..., use_fiber_future);
}
}  // namespace ssl

namespace websocket {
template <typename Conn, typename... Args>
auto async_read(Conn& con, Args&&... args) -> boost::fibers::future<size_t>
{
  return con.async_read(std::forward<Args>(args)..., use_fiber_future);
}

template <typename Conn, typename... Args>
auto async_read_some(Conn& con, Args&&... args) -> boost::fibers::future<size_t>
{
  return con.async_read_some(std::forward<Args>(args)..., use_fiber_future);
}

template <typename Conn, typename... Args>
auto async_write(Conn& con, Args&&... args) -> boost::fibers::future<size_t>
{
  return con.async_write(std::forward<Args>(args)..., use_fiber_future);
}

template <typename Conn, typename... Args>
auto async_accept(Conn& con, Args&&... args) -> boost::fibers::future<void>
{
  return con.async_accept(std::forward<Args>(args)..., use_fiber_future);
}

template <typename Conn, typename... Args>
auto async_handshake(Conn& con, Args&&... args) -> boost::fibers::future<void>
{
  return con.async_handshake(std::forward<Args>(args)..., use_fiber_future);
}

template <typename Conn, typename... Args>
auto async_close(Conn& con, Args&&... args) -> boost::fibers::future<void>
{
  return con.async_close(std::forward<Args>(args)..., use_fiber_future);
}
}  // namespace websocket
}  // namespace internal
}  // namespace asyik

#endif  // NFXCHNG_API_H
