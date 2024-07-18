#ifndef LIBASYIK_ASYIK_HTTP_HPP
#define LIBASYIK_ASYIK_HTTP_HPP

#include <boost/algorithm/string/predicate.hpp>
#include <boost/any.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/url.hpp>
#include <regex>
#include <string>

#include "aixlog.hpp"
#include "asyik_fwd.hpp"
#include "boost/asio.hpp"
#include "boost/fiber/all.hpp"
#include "common.hpp"
#include "error.hpp"
#include "internal/asio_internal.hpp"
#include "internal/digestauth.hpp"
#include "service.hpp"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace ip = boost::asio::ip;
namespace websocket = beast::websocket;

namespace asyik {
void _TEST_invoke_http();

static const size_t default_request_body_limit = 1 * 1024 * 1024;
static const size_t default_request_header_limit = 1 * 1024 * 1024;
static const size_t default_response_body_limit = 64 * 1024 * 1024;
static const size_t default_response_header_limit = 1 * 1024 * 1024;

using http_route_args = std::vector<std::string>;

using http_beast_request =
    boost::beast::http::request<boost::beast::http::string_body>;
using http_beast_response =
    boost::beast::http::response<boost::beast::http::string_body>;
using http_request_headers = http_beast_request::header_type;
using http_request_body = http_beast_request::body_type::value_type;
using http_response_headers = http_beast_response::header_type;
using http_response_body = http_beast_response::body_type::value_type;

using http_route_callback =
    std::function<void(http_request_ptr, const http_route_args&)>;
using http_route_tuple =
    std::tuple<std::string, std::regex, http_route_callback>;

using websocket_route_callback =
    std::function<void(websocket_ptr, const http_route_args&)>;
using websocket_route_tuple =
    std::tuple<std::string, std::regex, websocket_route_callback>;

using websocket_close_code = boost::beast::websocket::close_code;

http_server_ptr<http_stream_type> make_http_server(service_ptr as,
                                                   string_view addr,
                                                   uint16_t port = 80,
                                                   bool reuse_port = false);
http_server_ptr<https_stream_type> make_https_server(service_ptr,
                                                     ssl::context&& ssl,
                                                     string_view,
                                                     uint16_t port = 443,
                                                     bool reuse_port = false);
// http_connection_ptr make_http_connection(service_ptr as, string_view addr,
// string_view port);
websocket_ptr make_websocket_connection(service_ptr as, string_view url,
                                        int timeout = 10);

http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url);
template <typename D>
http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url, D&& data);

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, string_view method, string_view url, D&& data,
    const std::map<string_view, string_view>& headers);

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, int timeout_ms, string_view method, string_view url,
    D&& data, const std::map<string_view, string_view>& headers);

struct http_url_scheme {
  boost::urls::url_view get_url_view() const { return uv; }

  bool is_ssl() const { return is_ssl_; }
  string_view host() const
  {
    return string_view(uv.encoded_host().data(), uv.encoded_host().length());
  }
  string_view authority() const
  {
    return string_view(uv.authority().data(), uv.authority().size());
  }
  string_view username() const { return username_; }
  string_view password() const { return password_; }
  uint16_t port() const { return port_; }
  string_view target() const
  {
    return string_view(uv.encoded_resource().data(),
                       uv.encoded_resource().size());
  }

 private:
  boost::urls::url_view uv;
  std::string username_;
  std::string password_;
  bool is_ssl_;
  uint16_t port_;

  friend bool http_analyze_url(string_view u, http_url_scheme& scheme);
};

bool http_analyze_url(string_view url, http_url_scheme& scheme);

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

  template <typename T>
  void on_http_request(string_view route_spec, T&& cb)
  {
    std::regex re(internal::route_spec_to_regex(route_spec));
    on_http_request_regex(re, "", std::forward<T>(cb));
  }

  template <typename T>
  void on_http_request(string_view route_spec, string_view method, T&& cb)
  {
    std::regex re(internal::route_spec_to_regex(route_spec));
    on_http_request_regex(re, method, std::forward<T>(cb));
  }

  template <typename R, typename M, typename T>
  void on_http_request_regex(R&& r, M&& m, T&& cb)
  {
    http_routes.push_back({std::string{std::forward<M>(m)}, std::forward<R>(r),
                           std::forward<T>(cb)});
  }

  template <typename T>
  void on_websocket(string_view route_spec, T&& cb)
  {
    std::regex re(internal::route_spec_to_regex(route_spec));
    on_websocket_regex(re, std::forward<T>(cb));
  }

  template <typename R, typename T>
  void on_websocket_regex(R&& r, T&& cb)
  {
    ws_routes.push_back({"", std::forward<R>(r), std::forward<T>(cb)});
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
  friend http_server_ptr<https_stream_type> make_https_server(
      service_ptr, ssl::context&& ssl, string_view, uint16_t, bool);
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
  template <typename executor_type>
  http_connection(struct private_&&, const executor_type& io_service,
                  http_server_ptr<http_stream_type> server)
      : http_server(http_server_wptr<StreamType>(server)),
        socket(io_service),
        stream(std::move(socket)),
        is_websocket(false),
        is_server_connection(true){};

  template <typename executor_type>
  http_connection(struct private_&&, const executor_type& io_service,
                  http_server_ptr<https_stream_type> server)
      : http_server(http_server_wptr<StreamType>(server)),
        socket(io_service),
        ssl_context(server->ssl_context),
        stream(socket, *ssl_context),
        is_websocket(false),
        is_server_connection(true){};

  // constructor for client connection
  // template <typename executor_type>
  // http_connection(struct private_ &&, const executor_type &io_service)
  //     : http_server(nullptr),
  //       socket(io_service),
  //       stream(socket),
  //       is_websocket(false),
  //       is_server_connection(false){};

  StreamType& get_stream() { return stream; };

 private:
  void start();
  inline void handshake_if_ssl();
  inline void shutdown_ssl();

  http_server_wptr<StreamType> http_server;
  tcp::socket socket;
  std::shared_ptr<ssl::context> ssl_context;
  StreamType stream;
  bool is_websocket;
  bool is_server_connection;

  template <typename S>
  friend class http_server;
  // friend http_connection_ptr make_http_connection(service_ptr as, string_view
  // addr, string_view port);
};

class http_request : public std::enable_shared_from_this<http_request> {
 private:
  struct private_ {};

 public:
  ~http_request(){};
  http_request& operator=(const http_request&) = delete;
  http_request(const http_request&) = delete;
  http_request(http_request&&) = default;
  http_request& operator=(http_request&&) = default;

  http_request()
      : beast_request(),
        headers(beast_request.base()),
        body(beast_request.body()),
        response(),
        multipart_response(),
        manual_response(false){};

  http_beast_request beast_request;
  http_request_headers& headers;
  http_request_body& body;
  boost::urls::url_view uv;

  template <typename S>
  inline auto get_connection_handle(S server)
      -> decltype(server->get_request_connection(*this))
  {
    return server->get_request_connection(*this);
  }

  string_view target() const { return beast_request.target(); };

  void target(string_view t) { beast_request.target(t); };

  string_view method() const { return beast_request.method_string(); };

  void method(string_view verb)
  {
    beast_request.method(beast::http::string_to_verb(verb));
  };

  void set_url_view() { uv = boost::urls::url_view(target()); }

  boost::urls::url_view& get_url_view() { return uv; }

  struct response {
    response()
        : beast_response(),
          headers(beast_response.base()),
          body(beast_response.body()){};
    http_beast_response beast_response;
    http_response_headers& headers;
    http_response_body& body;
    http_result result() const { return beast_response.result_int(); };
    void result(http_result res) { beast_response.result(res); };
  } response, multipart_response;

  void activate_direct_response_handling() { manual_response = true; }

 private:
  boost::beast::flat_buffer buffer;
  bool manual_response;
  boost::any connection_wptr;

  template <typename StreamType>
  friend class http_server;

  template <typename StreamType>
  friend class http_connection;

  template <typename D, typename F>
  friend http_request_ptr http_easy_request_multipart(
      service_ptr as, int timeout_ms, string_view method, string_view url,
      D&& data, const std::map<string_view, string_view>& headers, F&& f);

  template <typename S, typename R, typename F>
  friend boost::fibers::future<size_t> handle_client_request_response(
      S& stream, int timeout_ms, R& req, http_url_scheme& scheme, F& f);
};

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

  template <typename executor_type>
  websocket_impl(struct private_&&, const executor_type& io_service,
                 string_view host_, string_view port_, string_view path_)
      : websocket(host_, port_, path_){};

  template <typename executor_type, typename ws_type, typename req_type>
  websocket_impl(struct private_&&, const executor_type& io_service,
                 ws_type&& ws, req_type&& req)
      : websocket(std::forward<req_type>(req)), ws(std::forward<ws_type>(ws)){};

  // API
  virtual std::string get_string()
  {
    std::string message;
    auto buffer = asio::dynamic_buffer(message);

    internal::websocket::async_read(*ws, buffer).get();
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
    asio::mutable_buffers_1 mb(b.data(), b.size());
    auto buffer = beast::buffers_adaptor<asio::mutable_buffers_1>(mb);

    return internal::websocket::async_read(*ws, buffer).get();
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
    internal::websocket::async_close(*ws, cr);
  }

 private:
  template <typename S>
  friend class http_connection;
  friend websocket_ptr make_websocket_connection(service_ptr as,
                                                 string_view url, int timeout);

  std::shared_ptr<StreamType> ws;
};

inline boost::optional<std::string> deduce_boundary(
    beast::string_view content_type)
{
  boost::optional<std::string> result;

  static std::regex r1{
      R"regex(^multipart/mixed-replace\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r2{
      R"regex(^multipart/mixed-replace\s*;\s*boundary=([^"]+)$)regex",
      std::regex_constants::icase};
  static std::regex r3{
      R"regex(^multipart/mixed\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r4{R"regex(^multipart/mixed\s*;\s*boundary=([^"]+)$)regex",
                       std::regex_constants::icase};
  static std::regex r5{
      R"regex(^multipart/x-mixed-replace\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r6{
      R"regex(^multipart/x-mixed-replace\s*;\s*boundary=([^"]+)$)regex",
      std::regex_constants::icase};
  static std::regex r7{
      R"regex(^multipart/form-data\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r8{
      R"regex(^multipart/form-data\s*;\s*boundary=([^"]+)$)regex",
      std::regex_constants::icase};

  auto match = std::cmatch();

  if (std::regex_match(content_type.begin(), content_type.end(), match, r1) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r2) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r3) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r4) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r5) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r6) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r7) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r8)) {
    result = match[1].str();
  }

  return result;
};

template <typename S, typename B>
bool find_multipart_boundary(S& stream, B& buffer, const std::string& boundary)
{
  std::string boundary_line = boundary + "\r\n";
  std::string terminate_line = boundary + "--\r\n";

  while (1) {
    string_view s(asio::buffer_cast<const char*>(buffer.data()), buffer.size());

    auto pos = s.find(boundary_line);
    if (pos > s.size()) {
      if (s.find(terminate_line) < s.size()) {
        // reach end of stream
        return false;
      }
      auto b = buffer.prepare(65536);

      auto sz = stream.async_read_some(b, asyik::use_fiber_future).get();
      buffer.commit(sz);
    } else {
      buffer.consume(pos + boundary_line.length());
      return true;
    }
  }
}

template <typename S, typename Buf, typename BR, typename P>
void handle_client_auth(S& stream, http_url_scheme& scheme, Buf& buffer,
                        BR& beast_request, P& empty_parser)
{
  http::response_parser<http::string_body> resp_parser_unauth{
      std::move(empty_parser)};

  asyik::internal::http::async_read(stream, buffer, resp_parser_unauth).get();

  digest_authenticator auth(
      resp_parser_unauth.get()[http::field::www_authenticate],
      scheme.username(), scheme.password(), scheme.target(),
      beast_request.method_string(), resp_parser_unauth.get().body());

  auth.generateAuthorization();
  beast_request.insert("Authorization", auth.authorization());
  beast_request.prepare_payload();

  auto w = internal::http::async_write(stream, beast_request);

  // reset the parser
  using T = http::response_parser<http::empty_body>;
  empty_parser.~T();
  new (&empty_parser) T();

  asyik::internal::http::async_read_header(stream, buffer, empty_parser).get();
  w.get();
}

template <typename S, typename R, typename F>
boost::fibers::future<size_t> handle_client_request_response(
    S& stream, int timeout_ms, R& req, http_url_scheme& scheme, F& f)
{
  auto w = internal::http::async_write(stream, req->beast_request);

  http::response_parser<http::empty_body> empty_parser;
  empty_parser.eager(false);
  empty_parser.header_limit(default_response_header_limit);
  empty_parser.body_limit(default_response_body_limit);

  asyik::internal::http::async_read_header(stream, req->buffer, empty_parser)
      .get();

  if ((empty_parser.get().result() ==
       boost::beast::http::status::unauthorized) &&
      scheme.username().length() && scheme.password().length()) {
    w.get();
    handle_client_auth(stream, scheme, req->buffer, req->beast_request,
                       empty_parser);
  }

  if (empty_parser.get().count("content-type") &&
      (empty_parser.get().at("content-type").contains("multipart"))) {
    // Multipart handling

    auto boundary =
        deduce_boundary(empty_parser.get()[http::field::content_type]);

    if (!boundary)
      throw asyik::unexpected_error(
          "HTTP response error, cannot get boundary token");

    http::response_parser<http::string_body> resp_parser{
        std::move(empty_parser)};
    req->response.beast_response = resp_parser.release();

    while (find_multipart_boundary(stream, req->buffer,
                                   boundary.get_value_or(""))) {
      beast::get_lowest_layer(stream).expires_after(
          std::chrono::milliseconds(timeout_ms));

      http::response_parser<http::empty_body> empty_mpart_parser;
      empty_mpart_parser.eager(false);
      empty_mpart_parser.header_limit(default_response_header_limit);
      empty_mpart_parser.body_limit(default_response_body_limit);

      std::string status =
          "HTTP/1.1 " + std::to_string((int)resp_parser.get().result());
      status += " " + std::string(resp_parser.get().reason()) + "\r\n";

      boost::beast::error_code ec;
      empty_mpart_parser.put(asio::buffer(status), ec);

      asyik::internal::http::async_read_header(stream, req->buffer,
                                               empty_mpart_parser)
          .get();

      if (!empty_mpart_parser.content_length())
        throw asyik::unexpected_error(
            "HTTP multipart without part content-length is not "
            "supported!");

      http::response_parser<http::string_body> resp_mpart_parser{
          std::move(empty_mpart_parser)};
      asyik::internal::http::async_read(stream, req->buffer, resp_mpart_parser)
          .get();

      // proses resp_mpart_parser;
      req->multipart_response.beast_response = resp_mpart_parser.release();

      f(req);
    }
  } else {
    // non-multipart handling
    http::response_parser<http::string_body> resp_parser{
        std::move(empty_parser)};

    asyik::internal::http::async_read(stream, req->buffer, resp_parser).get();

    req->response.beast_response = resp_parser.release();
  }
  return w;
}

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(
    service_ptr as, int timeout_ms, string_view method, string_view url,
    D&& data, const std::map<string_view, string_view>& headers, F&& f)
{
  http_url_scheme scheme;

  BOOST_ASSERT(timeout_ms > 0);

  if (http_analyze_url(url, scheme)) {
    auto req = std::make_shared<http_request>();

    if (scheme.port() == 80 || scheme.port() == 443)
      req->headers.set("Host", scheme.host());
    else
      req->headers.set("Host", std::string(scheme.host()) + ":" +
                                   std::to_string(scheme.port()));
    req->headers.set("User-Agent", LIBASYIK_VERSION_STRING);
    req->headers.set("Accept", "*/*");

    // user-overidden headers
    for (const auto& item : headers) req->headers.set(item.first, item.second);

    req->body = std::forward<D>(data);
    if (req->body.length()) {
      if (!req->headers.count(http::field::content_type))
        req->headers.set("Content-Type", "text/html");
    }
    req->beast_request.version(11);
    req->method(method);
    req->target(scheme.target().length() ? scheme.target() : "/");
    req->beast_request.keep_alive(true);
    req->headers.set("Connection", "Keep-Alive");

    req->beast_request.prepare_payload();

    tcp::resolver resolver(as->get_io_service().get_executor());
    tcp::resolver::results_type results =
        internal::socket::async_resolve(resolver, std::string(scheme.host()),
                                        std::to_string(scheme.port()))
            .get();

    if (scheme.is_ssl()) {
      // The SSL context is required, and holds certificates
      ssl::context ctx(ssl::context::tlsv12_client);

      // This holds the root certificate used for verification
      // load_root_certificates(ctx);

      ctx.set_default_verify_paths();
      ctx.set_verify_mode(ssl::verify_none);  //!!!

      beast::ssl_stream<beast::tcp_stream> stream(
          as->get_io_service().get_executor(), ctx);

      stream.next_layer().expires_after(std::chrono::milliseconds(timeout_ms));

      internal::socket::async_connect(beast::get_lowest_layer(stream), results)
          .get();
      internal::ssl::async_handshake(stream, ssl::stream_base::client).get();

      auto is_write_done =
          handle_client_request_response(stream, timeout_ms, req, scheme, f);

      try {
        is_write_done.get();
        internal::ssl::async_shutdown(stream).get();
      } catch (...) {
        // it's okay, we need both parties to close SSL gracefully,
        // but it's not always the case
        // https://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
      }

      return req;
    } else {
      beast::tcp_stream stream(as->get_io_service().get_executor());

      stream.expires_after(std::chrono::milliseconds(timeout_ms));

      internal::socket::async_connect(stream, results).get();

      auto is_write_done =
          handle_client_request_response(stream, timeout_ms, req, scheme, f);

      beast::error_code ec;
      try {
        is_write_done.get();
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
      } catch (...) {
        // it's okay, we need both parties to close SSL gracefully,
        // but it's not always the case
        // https://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
      }

      return req;
    }
  } else
    return nullptr;
};

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(
    service_ptr as, string_view method, string_view url, D&& data,
    const std::map<string_view, string_view>& headers, F&& f)
{
  return http_easy_request_multipart(as, 30000, method, url,
                                     std::forward<D>(data), headers,
                                     std::forward<F>(f));
}

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(service_ptr as, string_view method,
                                             string_view url, D&& data, F&& f)
{
  return http_easy_request_multipart(as, method, url, std::forward<D>(data), {},
                                     std::forward<F>(f));
};

template <typename F>
http_request_ptr http_easy_request_multipart(service_ptr as, string_view method,
                                             string_view url, F&& f)

{
  return http_easy_request_multipart(as, method, url, "", std::forward<F>(f));
}

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, int timeout_ms, string_view method, string_view url,
    D&& data, const std::map<string_view, string_view>& headers)
{
  return http_easy_request_multipart(
      as, timeout_ms, method, url, std::forward<D>(data), headers,
      [](http_request_ptr req) {
        throw asyik::unexpected_error(
            "could not handle multipart response using http_easy_request(). "
            "Use http_easy_request_multipart() instead");
      });
};

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, string_view method, string_view url, D&& data,
    const std::map<string_view, string_view>& headers)
{
  return http_easy_request(as, 30000, method, url, std::forward<D>(data),
                           headers);
}

template <typename D>
http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url, D&& data)
{
  return http_easy_request(as, method, url, std::forward<D>(data), {});
};

template <>
inline void http_connection<http_stream_type>::handshake_if_ssl()
{}

template <>
inline void http_connection<https_stream_type>::handshake_if_ssl()
{
  internal::ssl::async_handshake(stream, ssl::stream_base::server).get();
}

template <>
inline void http_connection<http_stream_type>::shutdown_ssl()
{}

template <>
inline void http_connection<https_stream_type>::shutdown_ssl()
{
  internal::ssl::async_shutdown(stream).get();
}

template <typename StreamType>
void http_connection<StreamType>::start()
{
  if (auto server = http_server.lock()) {
    if (auto service = server->service.lock()) {
      service->execute([p = this->shared_from_this(),
                        body_limit = server->get_request_body_limit(),
                        header_limit =
                            server->get_request_header_limit()](void) {
        // flag to ignore eos error since work has been
        // done anyway
        bool safe_to_close = false;

        try {
          ip::tcp::no_delay option(true);
          beast::get_lowest_layer(p->get_stream()).set_option(option);

          p->handshake_if_ssl();

          auto asyik_req = std::make_shared<http_request>();
          auto& req = asyik_req->beast_request;
          asyik_req->connection_wptr = http_connection_wptr<StreamType>(p);
          while (1) {
            http::request_parser<http::empty_body> empty_parser;
            empty_parser.header_limit(header_limit);
            empty_parser.body_limit(body_limit);

            asyik_req->buffer.clear();
            asyik::internal::http::async_read_header(
                p->get_stream(), asyik_req->buffer, empty_parser)
                .get();
            safe_to_close = false;

            http::request_parser<http::string_body> req_parser{
                std::move(empty_parser)};

            asyik::internal::http::async_read(p->get_stream(),
                                              asyik_req->buffer, req_parser)
                .get();
            req = req_parser.release();

            asyik_req->set_url_view();
            // See if its a WebSocket upgrade request
            if (beast::websocket::is_upgrade(req)) {
              // Clients SHOULD NOT begin sending WebSocket
              // frames until the server has provided a response.
              if (asyik_req->buffer.size() != 0)
                throw unexpected_input_error(
                    "unexpected data before websocket handshake!");

              try {
                if (p->http_server.expired())
                  throw network_expired_error("server expired");

                auto server = p->http_server.lock();

                http_route_args args;
                const websocket_route_tuple& route =
                    server->find_websocket_route(req, args);

                // Construct the str ;o,\eam, transferring ownership of the
                // socket
                if (server->service.expired())
                  throw network_expired_error("service expired");

                auto service = server->service.lock();

                auto new_ws = std::make_shared<
                    websocket_impl<beast::websocket::stream<StreamType>>>(
                    typename websocket_impl<
                        beast::websocket::stream<StreamType>>::private_{},
                    service->get_io_service().get_executor(),
                    std::make_shared<beast::websocket::stream<StreamType>>(
                        std::move(p->get_stream())),
                    asyik_req);

                asyik::internal::websocket::async_accept(*new_ws->ws, req)
                    .get();
                p->is_websocket = true;

                try {
                  std::get<2>(route)(new_ws, args);
                } catch (...) {
                  LOG(ERROR)
                      << "uncaught error in routing handler: " << req.target()
                      << "\n";
                }
              } catch (...) {
              }
              return;
            } else {
              // Its not a WebSocket upgrade, so
              // handle it like a normal HTTP request.
              auto& res = asyik_req->response.beast_response;

              asyik_req->response.headers.set(http::field::server,
                                              LIBASYIK_VERSION_STRING);
              asyik_req->response.headers.set(http::field::content_type,
                                              "text/html");
              asyik_req->response.beast_response.keep_alive(
                  asyik_req->beast_request.keep_alive());

              // pretty much should be improved
              try {
                if (p->http_server.expired())
                  throw network_expired_error("server expired");
                auto server = p->http_server.lock();

                try {
                  http_route_args args;
                  const http_route_tuple& route =
                      server->find_http_route(req, args);

                  std::get<2>(route)(asyik_req, args);
                } catch (not_found_error& e) {
                  asyik_req->response.body = "";
                  asyik_req->response.result(404);
                  asyik_req->response.beast_response.keep_alive(false);
                }
              } catch (...) {
                asyik_req->response.body = "";
                asyik_req->response.result(500);
                asyik_req->response.beast_response.keep_alive(false);
              };

              if (asyik_req->manual_response) {
                safe_to_close = true;
                break;
              }

              asyik_req->response.beast_response.prepare_payload();
              http::serializer<false, http::string_body> sr{res};
              asyik::internal::http::async_write(p->get_stream(), res).get();

              if (!req.need_eof()) break;

              safe_to_close = true;
            }
          }
          p->shutdown_ssl();
        }
        // TODO: all HTTP 5xx and 4xx handling should be put here instead
        catch (overflow_error& e) {
          http_beast_response beast_response;
          beast_response.body() = e.what();
          beast_response.keep_alive(false);
          beast_response.result(413);

          beast_response.prepare_payload();
          http::serializer<false, http::string_body> sr{beast_response};
          asyik::internal::http::async_write(p->get_stream(), beast_response)
              .get();

          p->shutdown_ssl();
        } catch (asyik::already_closed_error& e) {
          if (!safe_to_close) {
            LOG(WARNING) << "End of stream exception is catched during client "
                            "connection, reason: "
                         << e.what() << "\n";
          }
        } catch (std::exception& e) {
          LOG(WARNING)
              << "exception is catched during client connection, reason: "
              << e.what() << "\n";
        };
      });
    };
  };
}

template <typename StreamType>
http_server<StreamType>::http_server(struct private_&&, service_ptr as,
                                     string_view addr, uint16_t port)
    : service(as),
      request_body_limit(default_request_body_limit),
      request_header_limit(default_request_header_limit)
{
  acceptor =
      std::make_shared<ip::tcp::acceptor>(as->get_io_service(), tcp::v4());

  if (!acceptor) throw resource_error("could not allocate TCP acceptor");
}

template <typename StreamType>
void http_server<StreamType>::start_accept(asio::io_context& io_service)
{
  auto new_connection = std::make_shared<http_connection<StreamType>>(
      typename http_connection<StreamType>::private_{},
      io_service.get_executor(), this->shared_from_this());

  acceptor->async_accept(
      beast::get_lowest_layer(new_connection->get_stream()),
      [new_connection,
       p = std::weak_ptr<http_server<StreamType>>(this->shared_from_this())](
          const boost::system::error_code& error) {
        if (!p.expired() && !error) {
          auto ps = p.lock();
          new_connection->start();
          if (!ps->service.expired()) {
            auto service = ps->service.lock();
            ps->start_accept(service->get_io_service());
          }
        } else {
          LOG(WARNING) << "async_accept error or canceled m=" << error.message()
                       << "\n";
        }
      });
}
}  // namespace asyik

#endif
