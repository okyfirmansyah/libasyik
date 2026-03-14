#ifndef LIBASYIK_ASYIK_HTTP_WEBSOCKET_HPP
#define LIBASYIK_ASYIK_HTTP_WEBSOCKET_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/fiber/all.hpp>
#include <string>
#include <vector>

#include "asyik_fwd.hpp"
#include "boost/asio.hpp"
#include "common.hpp"
#include "error.hpp"
#include "http_types.hpp"
#include "internal/asio_internal.hpp"

namespace fibers = boost::fibers;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

namespace asyik {

// WebSocket connection factory
websocket_ptr make_websocket_connection(service_ptr as, string_view url,
                                        int timeout = 10);

// WebSocket base class
class websocket : public std::enable_shared_from_this<websocket> {
 public:
  virtual ~websocket(){};
  websocket& operator=(const websocket&) = delete;
  websocket() = delete;
  websocket(const websocket&) = delete;
  websocket(websocket&&) = default;
  websocket& operator=(websocket&&) = default;

 protected:
  websocket(string_view host_, string_view port_, string_view path_)
      : host(host_), port(port_), path(path_), is_server_connection(false){};

  template <typename req_type>
  websocket(req_type&& req)
      : request(std::forward<req_type>(req)), is_server_connection(true){};

  // API
 public:
  virtual std::string get_string() = 0;
  virtual size_t read_basic_buffer(std::vector<uint8_t>& b) = 0;
  virtual void send_string(string_view s) = 0;
  virtual void write_basic_buffer(const std::vector<uint8_t>& b) = 0;
  virtual void set_keepalive_pings(bool b) = 0;
  virtual void set_idle_timeout(int t) = 0;

  void close(websocket_close_code code) { close(code, "NORMAL"); }
  virtual void close(websocket_close_code code, string_view reason){};
  http_request_ptr request;

 protected:
  std::string host;
  std::string port;
  std::string path;

  bool is_server_connection;
};

// WebSocket implementation template
template <typename StreamType>
class websocket_impl : public websocket {
 private:
  struct private_ {};

 public:
  virtual ~websocket_impl(){};
  websocket_impl& operator=(const websocket_impl&) = delete;
  websocket_impl() = delete;
  websocket_impl(const websocket_impl&) = delete;
  websocket_impl(websocket_impl&&) = default;
  websocket_impl& operator=(websocket_impl&&) = default;

  websocket_impl(struct private_&&, string_view host_, string_view port_,
                 string_view path_)
      : websocket(host_, port_, path_){};

  template <typename ws_type, typename req_type>
  websocket_impl(struct private_&&, ws_type&& ws, req_type&& req)
      : websocket(std::forward<req_type>(req)), ws(std::forward<ws_type>(ws)){};

  // API
  virtual std::string get_string()
  {
    std::string message;
    auto buffer = asio::dynamic_buffer(message);

    // we need to call async ops thru strand for this op
    // to be MT-safe
    asio::dispatch(
        ws->get_executor(),
        use_fiber_future([ws_ = ws, &buffer]() -> fibers::future<size_t> {
          return internal::websocket::async_read(*ws_, buffer);
        }))
        .get()
        .get();

    if (ws->got_binary())
      throw asyik::unexpected_error(
          "unexpected binary message when get_string()");

    return message;
  }

  virtual void send_string(string_view s)
  {
    ws->binary(false);
    internal::websocket::async_write(*ws, asio::buffer(s.data(), s.length()))
        .get();
  }

  // basic buffer API
  virtual size_t read_basic_buffer(std::vector<uint8_t>& b)
  {
    // auto buffer = asio::dynamic_buffer(b);
    asio::mutable_buffer mb(b.data(), b.size());
    auto buffer = beast::buffers_adaptor<asio::mutable_buffer>(mb);

    // we need to call async ops thru strand for this op
    // to be MT-safe
    return asio::dispatch(ws->get_executor(),
                          use_fiber_future(
                              [ws_ = ws, &buffer]() -> fibers::future<size_t> {
                                return internal::websocket::async_read(*ws_,
                                                                       buffer);
                              }))
        .get()
        .get();
  }

  virtual void write_basic_buffer(const std::vector<uint8_t>& b)
  {
    ws->binary(true);
    internal::websocket::async_write(*ws, asio::buffer(b.data(), b.size()))
        .get();
  }

  virtual void set_keepalive_pings(bool b)
  {
    beast::websocket::stream_base::timeout opt;

    ws->get_option(opt);
    opt.keep_alive_pings = b;
    ws->set_option(opt);
  };

  virtual void set_idle_timeout(int t)
  {
    beast::websocket::stream_base::timeout opt;

    ws->get_option(opt);
    opt.idle_timeout = std::chrono::seconds(t);
    ws->set_option(opt);
  };

  virtual void close(websocket_close_code code, string_view reason)
  {
    const boost::beast::websocket::close_reason cr(code, reason);

    try {
      auto f = asio::dispatch(
                   ws->get_executor(),
                   use_fiber_future([ws_ = ws, cr]() -> fibers::future<void> {
                     beast::get_lowest_layer(*ws_).cancel();
                     return internal::websocket::async_close(*ws_, cr);
                   }))
                   .get();
      if (f.wait_for(std::chrono::seconds(2)) ==
          fibers::future_status::timeout) {
        return;
      }

      f.get();
    } catch (...) {
    }
  }

 private:
  template <typename S>
  friend class http_connection;
  friend websocket_ptr make_websocket_connection(service_ptr as,
                                                 string_view url, int timeout);
  friend websocket_ptr make_websocket_connection_plain(
      service_ptr as, const http_url_scheme& scheme, int timeout);
  friend websocket_ptr make_websocket_connection_ssl(
      service_ptr as, const http_url_scheme& scheme, int timeout);

  std::shared_ptr<StreamType> ws;
};

}  // namespace asyik

#endif
