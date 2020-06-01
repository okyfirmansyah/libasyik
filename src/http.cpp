#include <iostream>
#include <regex>
#include "aixlog.hpp"
#include "boost/beast/core.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/service.hpp"
#include "catch2/catch.hpp"
#include "libasyik/common.hpp"
#include "libasyik/http.hpp"

namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik
{
  std::string internal::route_spec_to_regex(const std::string &route_spc)
  {
    std::string regex_spec = route_spc;

    // step 1: trim trailing .
    if (regex_spec[regex_spec.length() - 1] == '/')
      regex_spec = regex_spec.substr(0, regex_spec.length() - 1);

    // step 2: add regex escaping
    std::regex specialChars{R"([-[\]{}/()*+?.,\^$|#])"};
    regex_spec = std::regex_replace(regex_spec, specialChars, R"(\$&)");

    // step 3: replace <int> and <string>
    std::regex int_tag{R"(<\s*int\s*>)"};
    regex_spec = std::regex_replace(regex_spec, int_tag, R"(([0-9]+))");
    std::regex string_tag{R"(<\s*string\s*>)"};
    regex_spec = std::regex_replace(regex_spec, string_tag, R"(([^\/\s]+))");

    // step 4: add ^...$ and optional trailing /
    regex_spec = "^" + regex_spec + R"(\/?$)";

    return regex_spec;
  }

  TEST_CASE("check routing spec to regex spec conversion")
  {
    std::string spec = "/api/v1/face-match";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/?$)"));

    spec = "/api/v1/face-match/";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/?$)"));

    spec = "/api/v1/face-match/<int>/";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/([0-9]+)\/?$)"));

    spec = "/api/v1/face-match/<string>/n/< int  >";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/([^\/\s]+)\/n\/([0-9]+)\/?$)"));
  }

  http_server::http_server(struct private_ &&, service_ptr as, const std::string &addr, uint16_t port)
      : service(as)
  {
    acceptor = std::make_shared<ip::tcp::acceptor>(as->get_io_service(),
                                                   ip::tcp::endpoint(ip::address::from_string(addr), port), true);
    if (!acceptor)
      throw std::runtime_error("could not allocate TCP acceptor");
  }

  http_server_ptr make_http_server(service_ptr as, const std::string &addr, uint16_t port)
  {
    auto p = std::make_shared<http_server>(http_server::private_{}, as, addr, port);

    p->start_accept(as->get_io_service());
    return p;
  }

  void http_server::start_accept(asio::io_context &io_service)
  {
    auto new_connection = std::make_shared<http_connection>(http_connection::private_{}, io_service.get_executor(), shared_from_this());

    acceptor->async_accept(
        new_connection->get_socket(),
        [new_connection, p = std::weak_ptr<http_server>(shared_from_this())](const boost::system::error_code &error) {
          if (!p.expired() && !error)
          {
            auto ps = p.lock();
            new_connection->start();
            if (!ps->service.expired())
            {
              auto service = ps->service.lock();
              ps->start_accept(service->get_io_service());
            }
          }
          else
          {
            LOG(WARNING) << "async_accept error or canceled";
          }
        });
  }

  http_connection_ptr make_http_connection(service_ptr as, const std::string &addr, const std::string &port)
  {
    // tcp::resolver resolver(as->get_io_service().get_executor());

    // tcp::resolver::results_type results=internal::socket::async_resolve(resolver, addr, port);

    // auto new_connection = std::make_shared<http_connection>
    //                      (http_connection::private_{}, as->get_io_service().get_executor());

    // // Set the timeout for the operation
    // new_connection->get_socket().expires_after(std::chrono::seconds(30));

    // // Make the connection on the IP address we get from a lookup
    // new_connection->get_socket().async_connect(
    //         results,
    //         [](const beast::error_code &ec,
    //            tcp::resolver::results_type::endpoint_type ep)
    //         {

    //         });

    return nullptr;
  }

  void http_connection::start()
  {
    if (auto server = http_server.lock())
    {
      if (auto service = server->service.lock())
      {
        service->execute([p = shared_from_this()](void) {
          try
          {
            ip::tcp::no_delay option(true);
            p->get_socket().set_option(option);

            // This buffer is required for reading HTTP messages
            //boost::beast::flat_buffer buffer;

            // Read the HTTP request ourselves
            //http_beast_request req;
            auto asyik_req = std::make_shared<http_request>();
            auto &req = asyik_req->beast_request;
            while (1)
            {
              asyik::internal::http::async_read(p->get_socket(), asyik_req->buffer, req);

              // See if its a WebSocket upgrade request
              if (beast::websocket::is_upgrade(req))
              {
                // Clients SHOULD NOT begin sending WebSocket
                // frames until the server has provided a response.
                if (asyik_req->buffer.size() != 0)
                  throw std::make_exception_ptr(std::runtime_error("unexpected data before websocket handshake!"));

                LOG(INFO) << "\ntarget()=" << req.target() << "\n";
                LOG(INFO) << "\nmethod_string()=" << req.method_string() << "\n";
                LOG(INFO) << "\nreq.base().at('Host')=" << req.base().at("Host") << "\n";

                try
                {
                  if (p->http_server.expired())
                    throw std::make_exception_ptr(std::runtime_error("server expired"));

                  auto server = p->http_server.lock();

                  http_route_args args;
                  const websocket_route_tuple &route = server->find_websocket_route(req, args);

                  // Construct the str ;o,\eam, transferring ownership of the socket
                  if (server->service.expired())
                    throw std::make_exception_ptr(std::runtime_error("service expired"));

                  auto service = server->service.lock();

                  auto new_ws = std::make_shared<websocket>(websocket::private_{},
                                                            service->get_io_service().get_executor(),
                                                            std::make_shared<beast::websocket::stream<ip::tcp::socket>>(std::move(p->get_socket())),
                                                            asyik_req);

                  asyik::internal::websocket::async_accept(*new_ws->ws_server, req);
                  p->is_websocket = true;

                  try
                  {
                    std::get<2>(route)(new_ws, args);
                  }
                  catch (...)
                  {
                    LOG(ERROR) << "uncaught error in routing handler: " << req.target() << "\n";
                  }
                }
                catch (...)
                {
                }
                return;
              }
              else
              {
                // Its not a WebSocket upgrade, so
                // handle it like a normal HTTP request.
                auto &res = asyik_req->response.beast_response;

                res.set(http::field::server, "LIBASYIK 0.1.1(Powered by Boost::Beast)");
                res.set(http::field::content_type, "text/html");
                res.keep_alive(req.keep_alive());
                http_response_headers &res_header = res.base();
                http_response_body &res_body = res.body();
                asyik_req->response.headers.set(http::field::server, "LIBASYIK 0.1.1(Powered by Boost::Beast)");
                asyik_req->response.headers.set(http::field::content_type, "text/html");
                asyik_req->response.beast_response.keep_alive(asyik_req->beast_request.keep_alive());

                // pretty much should be improved
                try
                {
                  if (p->http_server.expired())
                    throw std::make_exception_ptr(std::runtime_error("server expired"));
                  auto server = p->http_server.lock();

                  bool found = false;
                  try
                  {
                    http_route_args args;
                    const http_route_tuple &route = server->find_http_route(req, args);
                    found = true;

                    std::get<2>(route)(asyik_req, args);
                  }
                  catch (...)
                  {
                    asyik_req->response.body = "";
                    if (!found)
                      asyik_req->response.result(404);
                    else
                      asyik_req->response.result(500);
                    asyik_req->response.beast_response.keep_alive(false);
                  }
                }
                catch (...)
                {
                  asyik_req->response.body = "";
                  asyik_req->response.result(500);
                  asyik_req->response.beast_response.keep_alive(false);
                };

                asyik_req->response.beast_response.prepare_payload();
                http::serializer<false, http::string_body> sr{res};
                asyik::internal::http::async_write(p->get_socket(), res);

                if (!req.need_eof())
                  break;
              }
            }
          }
          catch (std::exception &e)
          {
            LOG(WARNING) << "exception is catched during client connection, reason: " << e.what() << "\n";
          };
        });
      };
    };
  }

  websocket_ptr make_websocket_connection(service_ptr as,
                                          const std::string &host,
                                          const std::string &port,
                                          const std::string &path,
                                          const int timeout)
  {
    tcp::resolver resolver(as->get_io_service().get_executor());

    tcp::resolver::results_type results = internal::socket::async_resolve(resolver, host, port);

    auto new_ws = std::make_shared<websocket>(websocket::private_{}, as->get_io_service().get_executor(), host, port, path);

    new_ws->ws_client = std::make_shared<beast::websocket::stream<beast::tcp_stream>>(as->get_io_service().get_executor());

    // Set the timeout for the operation
    beast::get_lowest_layer(*new_ws->ws_client).expires_after(std::chrono::seconds(timeout));

    // Make the connection on the IP address we get from a lookup
    tcp::resolver::results_type::endpoint_type ep =
        internal::socket::async_connect(beast::get_lowest_layer(*new_ws->ws_client), results);

    // Turn off the timeout on the tcp_stream, because
    // the websocket stream has its own timeout system.
    beast::get_lowest_layer(beast::get_lowest_layer(*new_ws->ws_client)).expires_never();

    // Set suggested timeout settings for the websocket
    new_ws->ws_client->set_option(
        beast::websocket::stream_base::timeout::suggested(
            beast::role_type::client));

    // Set a decorator to change the User-Agent of the handshake
    new_ws->ws_client->set_option(beast::websocket::stream_base::decorator(
        [](beast::websocket::request_type &req) {
          req.set(http::field::user_agent,
                  std::string(BOOST_BEAST_VERSION_STRING) +
                      " websocket-client-async");
        }));

    //Perform the websocket handshake
    internal::websocket::async_handshake(*new_ws->ws_client, host, path);

    return new_ws;
  }

  std::string websocket::get_string()
  {
    std::string message;
    auto buffer = asio::dynamic_buffer(message);

    if (is_server_connection)
      internal::websocket::async_read(*ws_server, buffer);
    else
      internal::websocket::async_read(*ws_client, buffer);
    return message;
  }

  void websocket::send_string(const std::string &str)
  {
    if (is_server_connection)
      internal::websocket::async_write(*ws_server, asio::buffer(str.c_str(), str.length()));
    else
      internal::websocket::async_write(*ws_client, asio::buffer(str.c_str(), str.length()));
  }

  void websocket::close(websocket_close_code code, const std::string &reason)
  {
    const boost::beast::websocket::close_reason cr(code, reason);
    if (is_server_connection)
      internal::websocket::async_close(*ws_server, cr);
    else
      internal::websocket::async_close(*ws_client, cr);
  }

} // namespace asyik
