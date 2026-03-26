#ifndef LIBASYIK_ASYIK_HTTP_SERVER_HPP
#define LIBASYIK_ASYIK_HTTP_SERVER_HPP

#include <boost/algorithm/string/predicate.hpp>
#include <boost/any.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <regex>
#include <string>
#include <type_traits>

#include "asyik_fwd.hpp"
#include "boost/asio.hpp"
#include "boost/fiber/all.hpp"
#include "common.hpp"
#include "error.hpp"
#include "http_static.hpp"
#include "http_types.hpp"
#include "service.hpp"

namespace fibers = boost::fibers;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace ip = boost::asio::ip;
namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

namespace asyik {

// Factory functions
http_server_ptr<http_stream_type> make_http_server(service_ptr as,
                                                   string_view addr,
                                                   uint16_t port = 80,
                                                   bool reuse_port = false);
#ifdef LIBASYIK_ENABLE_SSL_SERVER
http_server_ptr<https_stream_type> make_https_server(service_ptr,
                                                     ssl::context&& ssl,
                                                     string_view,
                                                     uint16_t port = 443,
                                                     bool reuse_port = false);
#endif

namespace internal {
std::string route_spec_to_regex(string_view route_spc);
}

template <typename StreamType>
class http_server
    : public std::enable_shared_from_this<http_server<StreamType>> {
 private:
  struct private_ {};

 public:
  using stream_type = StreamType;
  ~http_server(){};
  http_server& operator=(const http_server&) = delete;
  http_server() = delete;
  http_server(const http_server&) = delete;
  http_server(http_server&&) = default;
  http_server& operator=(http_server&&) = default;

  http_server(struct private_&&, service_ptr as, string_view addr,
              uint16_t port);

  template <typename T,
            std::enable_if_t<!std::is_convertible_v<std::decay_t<T>, string_view>,
                             int> = 0>
  void on_http_request(string_view route_spec, T&& cb,
                       bool insert_front = false)
  {
    std::regex re(internal::route_spec_to_regex(route_spec));
    on_http_request_regex(re, "", std::forward<T>(cb), insert_front);
  }

  template <typename T>
  void on_http_request(string_view route_spec, string_view method, T&& cb,
                       bool insert_front = false)
  {
    std::regex re(internal::route_spec_to_regex(route_spec));
    on_http_request_regex(re, method, std::forward<T>(cb), insert_front);
  }

  template <typename R, typename M, typename T>
  void on_http_request_regex(R&& r, M&& m, T&& cb,
                             bool insert_front = false)
  {
    auto route = http_route_tuple{std::string{std::forward<M>(m)},
                                  std::forward<R>(r), std::forward<T>(cb)};
    if (insert_front)
      http_routes.insert(http_routes.begin(), std::move(route));
    else
      http_routes.push_back(std::move(route));
  }

  template <typename T,
            std::enable_if_t<!std::is_convertible_v<std::decay_t<T>, string_view>,
                             int> = 0>
  void on_websocket(string_view route_spec, T&& cb,
                    bool insert_front = false)
  {
    std::regex re(internal::route_spec_to_regex(route_spec));
    on_websocket_regex(re, std::forward<T>(cb), insert_front);
  }

  template <typename R, typename T>
  void on_websocket_regex(R&& r, T&& cb, bool insert_front = false)
  {
    auto route = websocket_route_tuple{"", std::forward<R>(r),
                                       std::forward<T>(cb)};
    if (insert_front)
      ws_routes.insert(ws_routes.begin(), std::move(route));
    else
      ws_routes.push_back(std::move(route));
  }
  /// Serve static files from @p root_dir under the URL prefix @p url_prefix.
  ///
  /// All GET requests whose target starts with @p url_prefix are handled by
  /// reading files from @p root_dir. The remaining path after stripping the
  /// prefix is appended to @p root_dir to locate the file.
  ///
  /// Features enabled by default (all controllable via @p cfg):
  ///  - MIME type detection from file extension
  ///  - ETag + If-None-Match → 304 Not Modified
  ///  - Last-Modified + If-Modified-Since → 304 Not Modified
  ///  - Range: bytes=A-B → 206 Partial Content
  ///  - Path-traversal guard (realpath() + canonical-root prefix check)
  ///  - Directory URL → serves cfg.index_file (default "index.html")
  ///
  /// Example:
  ///   server->serve_static("/static", "/var/www/html");
  void serve_static(string_view url_prefix, string_view root_dir,
                    const static_file_config& cfg = {})
  {
    std::string prefix{url_prefix};
    if (!prefix.empty() && prefix.back() == '/') prefix.pop_back();

    // Escape regex-special characters found in the prefix.
    static const std::regex special_chars{R"([-[\]{}/()*+?.,\^$|#])"};
    std::string escaped =
        std::regex_replace(prefix, special_chars, R"(\$&)");

    // Pattern: <escaped-prefix>  then optionally  /<path>  then query string.
    std::regex re("^" + escaped + R"((\/[^?#\s]*)?(|\?[^\?\s]*)$)");

    on_http_request_regex(
        re, "GET",
        internal::make_static_file_handler(prefix, std::string{root_dir}, cfg));
  }
  template <typename Req>
  http_connection_ptr<stream_type> get_request_connection(const Req& req)
  {
    auto p =
        boost::any_cast<http_connection_wptr<stream_type>>(req.connection_wptr);
    if (auto connection = p.lock()) {
      return connection;
    }
    return nullptr;
  }

  size_t get_request_header_limit() const { return request_header_limit; }

  size_t get_request_body_limit() const { return request_body_limit; }

  void set_request_header_limit(size_t l) { request_header_limit = l; }

  void set_request_body_limit(size_t l) { request_body_limit = l; }
  void close() { acceptor->close(); }

 private:
  void start_accept(asio::io_context& io_service);

  template <typename RouteType, typename ReqType>
  const RouteType& find_route(const std::vector<RouteType>& routeList,
                              const ReqType& req, http_route_args& a) const
  {
    auto it = find_if(routeList.begin(), routeList.end(),
                      [&req, &a](const RouteType& tuple) -> bool {
                        std::smatch m;
                        auto method = std::get<0>(tuple);
                        if (!method.compare("") ||
                            boost::iequals(method, req.method_string())) {
                          std::string s{req.target()};
                          if (std::regex_search(s, m, std::get<1>(tuple))) {
                            a.clear();
                            for (const auto& item : m) a.push_back(item.str());
                            return true;
                          } else
                            return false;
                        } else
                          return false;
                      });
    if (it != routeList.end())
      return *it;
    else
      throw not_found_error("route not found");
  }

  template <typename ReqType>
  const websocket_route_tuple& find_websocket_route(const ReqType& req,
                                                    http_route_args& a) const
  {
    return find_route(ws_routes, req, a);
  }

  template <typename ReqType>
  const http_route_tuple& find_http_route(const ReqType& req,
                                          http_route_args& a) const
  {
    return find_route(http_routes, req, a);
  }

  std::shared_ptr<ip::tcp::acceptor> acceptor;
  service_wptr service;

  std::vector<http_route_tuple> http_routes;
  std::vector<websocket_route_tuple> ws_routes;

  std::shared_ptr<ssl::context> ssl_context;

  size_t request_body_limit;
  size_t request_header_limit;

  template <typename S>
  friend class http_connection;
  friend http_server_ptr<http_stream_type> make_http_server(service_ptr,
                                                            string_view,
                                                            uint16_t, bool);
#ifdef LIBASYIK_ENABLE_SSL_SERVER
  friend http_server_ptr<https_stream_type> make_https_server(
      service_ptr, ssl::context&& ssl, string_view, uint16_t, bool);
#endif
};

template <typename StreamType>
class http_connection
    : public std::enable_shared_from_this<http_connection<StreamType>> {
 private:
  struct private_ {};

 public:
  ~http_connection(){};
  http_connection& operator=(const http_connection&) = delete;
  http_connection() = delete;
  http_connection(const http_connection&) = delete;
  http_connection(http_connection&&) = default;
  http_connection& operator=(http_connection&&) = default;

  // constructor for server connection
  http_connection(struct private_&&, tcp::socket&& sock,
                  http_server_ptr<http_stream_type> server)
      : http_server(http_server_wptr<StreamType>(server)),
        stream(std::move(sock)),
        is_websocket(false),
        is_server_connection(true),
        remote_endpoint(
            beast::get_lowest_layer(stream).socket().remote_endpoint()){};

  http_connection(struct private_&&, tcp::socket&& sock,
                  http_server_ptr<https_stream_type> server)
      : http_server(http_server_wptr<StreamType>(server)),
        ssl_context(server->ssl_context),
        stream(std::move(sock), *ssl_context),
        is_websocket(false),
        is_server_connection(true),
        remote_endpoint(
            beast::get_lowest_layer(stream).socket().remote_endpoint()){};

  StreamType& get_stream() { return stream; };
  tcp::endpoint get_remote_endpoint() const { return remote_endpoint; };

 private:
  void start();
  inline void handshake_if_ssl();
  inline void shutdown_ssl();

  http_server_wptr<StreamType> http_server;
  std::shared_ptr<ssl::context> ssl_context;
  StreamType stream;
  bool is_websocket;
  bool is_server_connection;
  tcp::endpoint remote_endpoint;

  template <typename S>
  friend class http_server;
};

}  // namespace asyik

#endif
