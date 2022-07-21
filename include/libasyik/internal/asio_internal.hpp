#ifndef LIBASYIK_ASYIK_ASIO_INTERNAL_HPP
#define LIBASYIK_ASYIK_ASIO_INTERNAL_HPP
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/fiber/all.hpp>
#include <string>

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
auto async_read(Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();
  boost::asio::async_read(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const boost::system::error_code& ec,
                                  size_t sz) mutable {
        if (!ec)
          prom.set_value(sz);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during socket::async_read")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("read_error")));
      });
  return future;
};

template <typename... Args>
auto async_write(Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();
  boost::asio::async_write(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const boost::system::error_code& ec,
                                  size_t sz) mutable {
        if (!ec)
          prom.set_value(sz);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during socket::async_write")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("write_error")));
      });
  return future;
};

template <typename... Args>
auto async_read_until(Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();
  boost::asio::async_read_until(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const boost::system::error_code& ec,
                                  size_t sz) mutable {
        if (!ec)
          prom.set_value(sz);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during socket::async_read_until")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("read_error")));
      });
  return future;
};

template <typename T>
auto async_timer_wait(T&& p)
{
  boost::fibers::promise<void> promise;
  auto future = promise.get_future();

  p.async_wait(
      [prom = std::move(promise)](const boost::system::error_code&) mutable {
        prom.set_value();
      });

  return future;
};

template <typename Resolver, typename... Args>
auto async_resolve(Resolver& res, Args&&... args)
{
  boost::fibers::promise<tcp::resolver::results_type> promise;
  auto future = promise.get_future();

  res.async_resolve(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec,
                                  tcp::resolver::results_type res) mutable {
        if (!ec)
          prom.set_value(res);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during socket::async_resolve")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("resolve error")));
      });
  return future;
};

template <typename Conn, typename... Args>
auto async_connect(Conn& con, Args&&... args)
{
  boost::fibers::promise<tcp::resolver::results_type::endpoint_type> promise;
  auto future = promise.get_future();

  con.async_connect(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](
          const beast::error_code& ec,
          tcp::resolver::results_type::endpoint_type res) mutable {
        if (!ec)
          prom.set_value(res);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during socket::async_connect")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("connect error")));
      });
  return future;
}
}  // namespace socket

namespace http {
template <typename... Args>
auto async_read(Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();
  boost::beast::http::async_read(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const boost::system::error_code& ec,
                                  size_t sz) mutable {
        if (!ec)
          prom.set_value(sz);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(
              network_timeout_error("timeout error during http::async_read")));
        else if (ec == beast::http::error::header_limit) {
          prom.set_exception(std::make_exception_ptr(
              overflow_error("incoming request header size is too large")));
        } else if (ec == beast::http::error::body_limit) {
          prom.set_exception(std::make_exception_ptr(
              overflow_error("incoming request body size is too large")));
        } else
          prom.set_exception(
              std::make_exception_ptr(network_error("read error")));
      });
  return future;
};

template <typename... Args>
auto async_write(Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();
  boost::beast::http::async_write(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const boost::system::error_code& ec,
                                  size_t sz) mutable {
        if (!ec)
          prom.set_value(sz);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(
              network_timeout_error("timeout error during http::async_write")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("write_error")));
      });
  return future;
};
}  // namespace http

namespace ssl {
template <typename Conn, typename... Args>
auto async_handshake(Conn& con, Args&&... args)
{
  boost::fibers::promise<void> promise;
  auto future = promise.get_future();

  con.async_handshake(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec) mutable {
        if (!ec)
          prom.set_value();
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during socket::async_handshake")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("handshake error")));
      });

  return future;
}

template <typename Conn, typename... Args>
auto async_shutdown(Conn& con, Args&&... args)
{
  boost::fibers::promise<void> promise;
  auto future = promise.get_future();

  con.async_shutdown(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec) mutable {
        if (!ec)
          prom.set_value();
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during socket::async_shutdown")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("shutdown error")));
      });

  return future;
}
}  // namespace ssl

namespace websocket {
template <typename Conn, typename... Args>
auto async_read(Conn& con, Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();

  con.async_read(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec,
                                  size_t bytes_transferred) mutable {
        if (!ec)
          prom.set_value(bytes_transferred);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during websocket::async_read")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("read_error")));
      });
  return future;
}

template <typename Conn, typename... Args>
auto async_read_some(Conn& con, Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();

  con.async_read_some(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec,
                                  size_t bytes_transferred) mutable {
        if (!ec)
          prom.set_value(bytes_transferred);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during websocket::async_read_some")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("read_error")));
      });
  return future;
}

template <typename Conn, typename... Args>
auto async_write(Conn& con, Args&&... args)
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();

  con.async_write(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec,
                                  size_t bytes_transferred) mutable {
        if (!ec)
          prom.set_value(bytes_transferred);
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during websocket::async_write")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("write error")));
      });
  return future;
}

template <typename Conn, typename... Args>
auto async_accept(Conn& con, Args&&... args)
{
  boost::fibers::promise<void> promise;
  auto future = promise.get_future();

  con.async_accept(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec) mutable {
        if (!ec)
          prom.set_value();
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during websocket::async_accept")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("accept error")));
      });

  return future;
}

template <typename Conn, typename... Args>
auto async_handshake(Conn& con, Args&&... args)
{
  boost::fibers::promise<void> promise;
  auto future = promise.get_future();

  con.async_handshake(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec) mutable {
        if (!ec)
          prom.set_value();
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during websocket::async_handshake")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("handshake error")));
      });

  return future;
}

template <typename Conn, typename... Args>
auto async_close(Conn& con, Args&&... args)
{
  boost::fibers::promise<void> promise;
  auto future = promise.get_future();

  con.async_close(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const beast::error_code& ec) mutable {
        if (!ec)
          prom.set_value();
        else if ((ec == asio::error::timed_out) ||
                 (ec == beast::error::timeout))
          prom.set_exception(std::make_exception_ptr(network_timeout_error(
              "timeout error during websocket::async_close")));
        else
          prom.set_exception(
              std::make_exception_ptr(network_error("close error")));
      });

  return future;
}
}  // namespace websocket
}  // namespace internal
}  // namespace asyik

#endif  // NFXCHNG_API_H
