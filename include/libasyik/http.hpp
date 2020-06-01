#ifndef LIBASYIK_ASYIK_HTTP_HPP
#define LIBASYIK_ASYIK_HTTP_HPP
#include <string>
#include <regex>
#include "boost/fiber/all.hpp"
#include "aixlog.hpp"
#include "boost/asio.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include "service.hpp"
#include "internal_api.hpp"
#include "boost/algorithm/string/predicate.hpp"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ip = boost::asio::ip;
namespace websocket = beast::websocket;

namespace asyik
{
  class http_connection;
  using http_connection_ptr = std::shared_ptr<http_connection>;
  using http_connection_wptr = std::weak_ptr<http_connection>;

  class websocket;
  using websocket_ptr = std::shared_ptr<websocket>;

  using http_route_args = std::vector<std::string>;

  class http_server;
  using http_server_ptr = std::shared_ptr<http_server>;
  using http_server_wptr = std::weak_ptr<http_server>;

  class http_request;
  using http_request_ptr = std::shared_ptr<http_request>;
  using http_request_wptr = std::weak_ptr<http_request>;
  using http_result = uint16_t;

  using http_beast_request = boost::beast::http::request<boost::beast::http::string_body>;
  using http_beast_response = boost::beast::http::response<http::string_body>;
  using http_request_headers = http_beast_request::header_type;
  using http_request_body = http_beast_request::body_type::value_type;
  using http_response_headers = http_beast_response::header_type;
  using http_response_body = http_beast_response::body_type::value_type;

  using http_route_callback = std::function<void(http_request_ptr, const http_route_args &)>;
  using http_route_tuple = std::tuple<std::string, std::regex, http_route_callback>;

  using websocket_route_callback = std::function<void(websocket_ptr, const http_route_args &)>;
  using websocket_route_tuple = std::tuple<std::string, std::regex, websocket_route_callback>;

  using websocket_close_code = boost::beast::websocket::close_code;

  http_server_ptr make_http_server(service_ptr as, const std::string &addr, uint16_t port = 80);
  http_connection_ptr make_http_connection(service_ptr as, const std::string &addr, const std::string &port);
  websocket_ptr make_websocket_connection(service_ptr as,
                                          const std::string &addr,
                                          const std::string &port,
                                          const std::string &path,
                                          int timeout = 10);

  namespace internal
  {
    std::string route_spec_to_regex(const std::string &route_spc);
  }

  class http_server : public std::enable_shared_from_this<http_server>
  {
  private:
    struct private_
    {
    };

  public:
    ~http_server(){};
    http_server &operator=(const http_server &) = delete;
    http_server() = delete;
    http_server(const http_server &) = delete;
    http_server(http_server &&) = default;
    http_server &operator=(http_server &&) = default;

    http_server(struct private_ &&, service_ptr as, const std::string &addr, uint16_t port);

    template <typename T>
    void on_http_request(const std::string &route_spec, T &&cb)
    {
      std::regex re(internal::route_spec_to_regex(route_spec));
      on_http_request_regex(re, "", std::forward<T>(cb));
    }

    template <typename T>
    void on_http_request(const std::string &route_spec, const std::string &method, T &&cb)
    {
      std::regex re(internal::route_spec_to_regex(route_spec));
      on_http_request_regex(re, method, std::forward<T>(cb));
    }

    template <typename R, typename M, typename T>
    void on_http_request_regex(R &&r, M &&m, T &&cb)
    {
      http_routes.push_back({std::forward<M>(m), std::forward<R>(r), std::forward<T>(cb)});
    }

    template <typename T>
    void on_websocket(const std::string &route_spec, T &&cb)
    {
      std::regex re(internal::route_spec_to_regex(route_spec));
      on_websocket_regex(re, std::forward<T>(cb));
    }

    template <typename R, typename T>
    void on_websocket_regex(R &&r, T &&cb)
    {
      ws_routes.push_back({"", std::forward<R>(r), std::forward<T>(cb)});
    }

  private:
    void start_accept(asio::io_context &io_service);

    template <typename RouteType, typename ReqType>
    const RouteType &find_route(const std::vector<RouteType> &routeList, const ReqType &req, http_route_args &a) const
    {
      auto it = find_if(routeList.begin(), routeList.end(),
                        [&req, &a](const RouteType &tuple) -> bool {
                          std::smatch m;
                          auto method = std::get<0>(tuple);
                          if (!method.compare("") || boost::iequals(method, req.method_string()))
                          {
                            std::string s{req.target()};
                            if (std::regex_search(s, m, std::get<1>(tuple)))
                            {
                              a.clear();
                              std::for_each(m.begin(), m.end(),
                                            [&a](auto item) {
                                              a.push_back(item.str());
                                            });
                              return true;
                            }
                            else
                              return false;
                          }
                          else
                            return false;
                        });
      if (it != routeList.end())
        return *it;
      else
        throw std::make_exception_ptr(std::runtime_error("route not found"));
    }

    template <typename ReqType>
    const websocket_route_tuple &find_websocket_route(const ReqType &req, http_route_args &a) const
    {
      return find_route(ws_routes, req, a);
    }

    template <typename ReqType>
    const http_route_tuple &find_http_route(const ReqType &req, http_route_args &a) const
    {
      return find_route(http_routes, req, a);
    }

    std::shared_ptr<ip::tcp::acceptor> acceptor;
    service_wptr service;

    std::vector<http_route_tuple> http_routes;
    std::vector<websocket_route_tuple> ws_routes;

    friend class http_connection;
    friend http_server_ptr make_http_server(service_ptr, const std::string &, uint16_t);
  };

  class http_connection : public std::enable_shared_from_this<http_connection>
  {
  private:
    struct private_
    {
    };

  public:
    ~http_connection(){};
    http_connection &operator=(const http_connection &) = delete;
    http_connection() = delete;
    http_connection(const http_connection &) = delete;
    http_connection(http_connection &&) = default;
    http_connection &operator=(http_connection &&) = default;

    // constructor for server connection
    template <typename executor_type>
    http_connection(struct private_ &&, const executor_type &io_service, http_server_ptr server)
        : http_server(http_server_wptr(server)),
          socket(io_service),
          is_websocket(false),
          is_server_connection(true){};

    // constructor for client connection
    template <typename executor_type>
    http_connection(struct private_ &&, const executor_type &io_service)
        : http_server(nullptr),
          socket(io_service),
          is_websocket(false),
          is_server_connection(false){};

    ip::tcp::socket &get_socket() { return socket; };

  private:
    void start();

    http_server_wptr http_server;
    ip::tcp::socket socket;
    bool is_websocket;
    bool is_server_connection;

    friend class http_server;
    friend http_connection_ptr make_http_connection(service_ptr as, const std::string &addr, const std::string &port);
  };

  class http_request : public std::enable_shared_from_this<http_request>
  {
  private:
    struct private_
    {
    };

  public:
    ~http_request(){};
    http_request &operator=(const http_request &) = delete;
    //http_request() = delete;
    http_request(const http_request &) = delete;
    http_request(http_request &&) = default;
    http_request &operator=(http_request &&) = default;

    // http_request(http_beast_request &request, http_beast_response &response)
    // :beast_request(request),
    //  headers(beast_request.base()),
    //  body(beast_request.body()),
    //  response({response, response.base(), response.body()})
    // {
    // };

    http_request()
        : beast_request(),
          headers(beast_request.base()),
          body(beast_request.body()),
          response(){};

    http_beast_request beast_request;
    http_request_headers &headers;
    http_request_body &body;

    string_view target() const
    {
      return beast_request.target();
    };

    void target(string_view t)
    {
      beast_request.target(t);
    };

    string_view method() const
    {
      return beast_request.method_string();
    };

    void method(string_view verb)
    {
      beast_request.method(beast::http::string_to_verb(verb));
    };

    struct response
    {
      response() : beast_response(),
                   headers(beast_response.base()),
                   body(beast_response.body()){};
      http_beast_response beast_response;
      http_response_headers &headers;
      http_response_body &body;
      http_result result() const
      {
        return beast_response.result_int();
      };
      void result(http_result res)
      {
        beast_response.result(res);
      };
    } response;

  private:
    boost::beast::flat_buffer buffer;

    friend class http_connection;
  };

  class websocket : public std::enable_shared_from_this<websocket>
  {
  private:
    struct private_
    {
    };

  public:
    ~websocket(){};
    websocket &operator=(const websocket &) = delete;
    websocket() = delete;
    websocket(const websocket &) = delete;
    websocket(websocket &&) = default;
    websocket &operator=(websocket &&) = default;

    template <typename executor_type>
    websocket(struct private_ &&, const executor_type &io_service, const std::string &host_, const std::string &port_, const std::string &path_)
        : host(host_),
          port(port_),
          path(path_),
          is_server_connection(false){};

    template <typename executor_type, typename ws_type, typename req_type>
    websocket(struct private_ &&, const executor_type &io_service, ws_type &&ws, req_type &&req)
        : ws_server(std::forward<ws_type>(ws)),
          request(std::forward<req_type>(req)),
          is_server_connection(true){};

    //API
    std::string get_string();
    void send_string(const std::string &s);
    void close(websocket_close_code code)
    {
      close(code, "NORMAL");
    }
    void close(websocket_close_code code, const std::string &reason);
    http_request_ptr request;

  private:
    friend class http_connection;
    friend websocket_ptr make_websocket_connection(service_ptr as,
                                                   const std::string &host,
                                                   const std::string &port,
                                                   const std::string &path,
                                                   int timeout);
    std::string host;
    std::string port;
    std::string path;

    bool is_server_connection;
    std::shared_ptr<beast::websocket::stream<ip::tcp::socket>> ws_server;
    std::shared_ptr<beast::websocket::stream<beast::tcp_stream>> ws_client;
    };
} // namespace asyik

#endif
