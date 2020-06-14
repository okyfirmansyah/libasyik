#include <iostream>
#include <regex>
#include "aixlog.hpp"
#include "boost/beast/core.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/service.hpp"
#include "catch2/catch.hpp"
#include "libasyik/common.hpp"
#include "libasyik/http.hpp"
#include "libasyik/internal/asio_internal.hpp"

namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik
{
  void _TEST_invoke_http(){};
  
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

  bool http_analyze_url(string_view url, http_url_scheme &scheme)
  {
    static std::regex re(R"(^(http\:\/\/|https\:\/\/|ws\:\/\/|wss\:\/\/|)(([^\/\:]+)(:\d{1,5})|([^\/\:]+)())(\/[^\:]*|)$)",
                         std::regex_constants::icase);
    static std::regex re2(R"(^(https|wss)\:\/\/)", std::regex_constants::icase);

    std::smatch m;
    std::string str{url};
    if (std::regex_search(str, m, re))
    {
      // check if ssl or not ssl
      if(m[1].str().compare(""))
      {
        scheme.is_ssl=
          std::regex_match(m[1].str(), 
                           re2);
          
      } else
        scheme.is_ssl = false;

      // is there any port specification or not
      if(m[3].str().length())
      {
        scheme.host=m[3].str();
        scheme.port=std::atoi(&m[4].str().c_str()[1]);
      } else
      {
        scheme.host=m[5].str();
        scheme.port=scheme.is_ssl?443:80;
      }

      scheme.target=m[7].str();
      if(!scheme.target.length())
        scheme.target="/";
      
      return true;
    }else return false;
  }

  TEST_CASE("check http_analyze_url against URL cases")
  {
    bool result;
    http_url_scheme scheme;

    result = http_analyze_url("simple.com", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("simple.com"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port==80);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("Http://protspecify.co.id", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("protspecify.co.id"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port==80);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("https://prothttps.x.y.z/", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("prothttps.x.y.z"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port==443);
    REQUIRE(scheme.is_ssl);

    result = http_analyze_url("https://protandport.x:69", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("protandport.x"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port==69);
    REQUIRE(scheme.is_ssl);

    result = http_analyze_url("simple.x.y.z:443/check", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("simple.x.y.z"));
    REQUIRE(!scheme.target.compare("/check"));
    REQUIRE(scheme.port==443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("http://simple.co.id:443/check", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("simple.co.id"));
    REQUIRE(!scheme.target.compare("/check"));
    REQUIRE(scheme.port==443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("http://10.10.10.1:443/check", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.1"));
    REQUIRE(!scheme.target.compare("/check"));
    REQUIRE(scheme.port==443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("10.10.10.1:443/", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.1"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port==443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("10.10.10.2", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.2"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port==80);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("invalid://simple.com:443/check", scheme);
    REQUIRE(!result);

    result = http_analyze_url("invalid:simple.com:443/check", scheme);
    REQUIRE(!result);

    result = http_analyze_url("simple.com:443:323/check", scheme);
    REQUIRE(!result);
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

    // tcp::resolver::results_type results=internal::socket::async_resolve(resolver, addr, port).get();

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
              asyik::internal::http::async_read(p->get_socket(), asyik_req->buffer, req).get();

              // See if its a WebSocket upgrade request
              if (beast::websocket::is_upgrade(req))
              {
                // Clients SHOULD NOT begin sending WebSocket
                // frames until the server has provided a response.
                if (asyik_req->buffer.size() != 0)
                  throw std::make_exception_ptr(std::runtime_error("unexpected data before websocket handshake!"));

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

                  auto new_ws = std::make_shared<websocket_impl<beast::websocket::stream<ip::tcp::socket>>>
                    (websocket_impl<beast::websocket::stream<ip::tcp::socket>>::private_{},
                    service->get_io_service().get_executor(),
                    std::make_shared<beast::websocket::stream<ip::tcp::socket>>(std::move(p->get_socket())),
                    asyik_req);

                  asyik::internal::websocket::async_accept(*new_ws->ws, req).get();
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

                http_response_headers &res_header = res.base();
                http_response_body &res_body = res.body();
                asyik_req->response.headers.set(http::field::server, LIBASYIK_VERSION_STRING);
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
                asyik::internal::http::async_write(p->get_socket(), res).get();

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
                                          string_view url,
                                          const int timeout)
  {
    bool result;
    http_url_scheme scheme;
    
    if(http_analyze_url(url, scheme))
    {
      tcp::resolver resolver(asio::make_strand(as->get_io_service()));

      tcp::resolver::results_type results = 
        internal::socket::async_resolve(resolver, scheme.host, std::to_string(scheme.port)).get();

      if(scheme.is_ssl)
      {
        // The SSL context is required, and holds certificates
        ssl::context ctx(ssl::context::tlsv12_client);

        // This holds the root certificate used for verification
        //load_root_certificates(ctx);

        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);

        auto new_ws = 
          std::make_shared<websocket_impl<beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>>
            (websocket_impl<beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>::private_{}, 
             as->get_io_service().get_executor(), scheme.host, std::to_string(scheme.port), scheme.target);

        new_ws->ws = std::make_shared<beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>
          (as->get_io_service().get_executor(), ctx);

        // Set the timeout for the operation
        beast::get_lowest_layer(*new_ws->ws).expires_after(std::chrono::seconds(timeout));

        tcp::resolver::results_type::endpoint_type ep =
          internal::socket::async_connect(beast::get_lowest_layer(*new_ws->ws), results).get();

        internal::ssl::async_handshake(new_ws->ws->next_layer(), ssl::stream_base::client).get();

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(beast::get_lowest_layer(*new_ws->ws)).expires_never();

        // Set suggested timeout settings for the websocket
        new_ws->ws->set_option(
          beast::websocket::stream_base::timeout::suggested(
              beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        new_ws->ws->set_option(beast::websocket::stream_base::decorator(
          [](beast::websocket::request_type &req) {
            req.set(http::field::user_agent,
                  std::string(BOOST_BEAST_VERSION_STRING) +
                      " websocket-client-async");
          }));

        //Perform the websocket handshake
        if(scheme.port==80 || scheme.port==443)
          internal::websocket::async_handshake(*new_ws->ws, scheme.host, scheme.target).get();
        else
          internal::websocket::async_handshake(*new_ws->ws, scheme.host+":"+std::to_string(scheme.port), scheme.target).get();

        return new_ws;
      }else
      {
        auto new_ws = 
          std::make_shared<websocket_impl<beast::websocket::stream<beast::tcp_stream>>>
            (websocket_impl<beast::websocket::stream<beast::tcp_stream>>::private_{}, 
             as->get_io_service().get_executor(), scheme.host, std::to_string(scheme.port), scheme.target);

        new_ws->ws = std::make_shared<beast::websocket::stream<beast::tcp_stream>>(as->get_io_service().get_executor());

        // Set the timeout for the operation
        beast::get_lowest_layer(*new_ws->ws).expires_after(std::chrono::seconds(timeout));

        // Make the connection on the IP address we get from a lookup
        tcp::resolver::results_type::endpoint_type ep =
          internal::socket::async_connect(beast::get_lowest_layer(*new_ws->ws), results).get();

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(beast::get_lowest_layer(*new_ws->ws)).expires_never();

        // Set suggested timeout settings for the websocket
        new_ws->ws->set_option(
          beast::websocket::stream_base::timeout::suggested(
              beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        new_ws->ws->set_option(beast::websocket::stream_base::decorator(
          [](beast::websocket::request_type &req) {
            req.set(http::field::user_agent,
                  std::string(BOOST_BEAST_VERSION_STRING) +
                      " websocket-client-async");
          }));

        //Perform the websocket handshake
        if(scheme.port==80 || scheme.port==443)
          internal::websocket::async_handshake(*new_ws->ws, scheme.host, scheme.target).get();
        else
          internal::websocket::async_handshake(*new_ws->ws, scheme.host+":"+std::to_string(scheme.port), scheme.target).get();

        return new_ws;
      }
    }else return nullptr;
  }
  
  http_request_ptr http_easy_request(service_ptr as, 
                                     string_view method, string_view url)
  {
    return http_easy_request(as, method, url, "");
  };

  TEST_CASE("Create http server and client, do some websocket communications", "[websocket]")
  {
    auto as = asyik::make_service();

    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    server->on_websocket("/<int>/name/<string>", [](auto ws, auto args) {
      while (1)
      {
        auto s = ws->get_string();
        ws->send_string(args[1] + "." + s + "-" + args[2]);
      }
    });

    int success = 0;
    for (int i = 0; i < 8; i++)
      as->execute([=, &success]() {
        asyik::sleep_for(std::chrono::milliseconds(50 * i));

        asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "ws://127.0.0.1:4004/" + std::to_string(i) + "/name/hash");

        ws->send_string("halo");
        auto s = ws->get_string();
        if (!s.compare(std::to_string(i) + ".halo-hash"))
          success++;

        ws->send_string("there");
        s = ws->get_string();
        if (!s.compare(std::to_string(i) + ".there-hash"))
          success++;

        ws->close(websocket_close_code::normal, "closed normally");
      });

    // check SSL websocket client(using external WSS server)
    as->execute([=, &success]() {
      asyik::sleep_for(std::chrono::milliseconds(50));

      asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "wss://echo.websocket.org");

      ws->send_string("halo");
      auto s = ws->get_string();
      if (!s.compare("halo"))
        success++;

      ws->send_string("there");
      s = ws->get_string();
      if (!s.compare("there"))
        success++;

      ws->close(websocket_close_code::normal, "closed normally");
    });

    // task to timeout entire service runtime
    as->execute([=, &success]() {
      for (int i = 0; i < 200; i++) //timeout 20s
      {
        if (success >= 18)
          break;
        asyik::sleep_for(std::chrono::milliseconds(100));
      }
      as->stop();
    });

    as->run();
    REQUIRE(success == 18);
  }

  TEST_CASE("Create http server and client, do some http communications", "[http]")
  {
    auto as = asyik::make_service();

    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    server->on_websocket("/<int>/test/<string>", [](auto ws, auto args) {
      ws->send_string(args[1]);
      ws->send_string(args[2]);

      auto s = ws->get_string();
      ws->send_string(s);

      ws->close(websocket_close_code::normal, "closed normally");
    });

    server->on_http_request("/post-only/<int>/name/<string>", "POST", [](auto req, auto args) {
      req->response.headers.set("x-test-reply", "amiiin");
      req->response.headers.set("content-type", "text/json");
      std::string x_test{req->headers["x-test"]};
      req->response.body = req->body + "-" + args[1] + "-" + args[2] + "-" + x_test;
      req->response.result(200);
    });

    server->on_http_request("/any/<int>/", [](auto req, auto args) {
      req->response.headers.set("x-test-reply", "amiin");
      req->response.headers.set("content-type", "text/json");
      std::string s{req->method()};
      std::string x_test{req->headers["x-test"]};
      req->response.body = req->body + "-" + s + "-" + args[1] + "-" + x_test;
      req->response.result(200);
    });

    asyik::sleep_for(std::chrono::milliseconds(100));
    as->execute([as](){
      auto req = asyik::http_easy_request(as, "POST", "http://127.0.0.1:4004/post-only/30/name/listed", "hehe", {{"x-test", "ok"}});

      REQUIRE(req->response.result()==200);
      std::string x_test_reply{req->response.headers["x-test-reply"]};
      std::string body=req->response.body;
      REQUIRE(!body.compare("hehe-30-listed-ok"));
      REQUIRE(!x_test_reply.compare("amiiin"));

      req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/any/999/");
    
      REQUIRE(req->response.result()==200);
      REQUIRE(!req->response.body.compare("-GET-999-"));
      REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

      req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/any/123/", "", {{"x-test", "sip"}});
    
      REQUIRE(req->response.result()==200);
      REQUIRE(!req->response.body.compare("-GET-123-sip"));
      REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

      req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/not-found");
      REQUIRE(req->response.result()==404);

      // negative tests (TODO)
      //req = asyik::http_easy_request(as, "GET", "3878sad9das7d97d8safdsfd.com/not-found");
      //REQUIRE(req->response.result()==404);

      // SSL Test
      req = asyik::http_easy_request(as, "GET", "https://tls-v1-2.badssl.com:1012/");
      REQUIRE(req->response.result()==200);
      REQUIRE(req->response.body.length());

      req = asyik::http_easy_request(as, "GET", "https://tls-v1-0.badssl.com:1012/");
      REQUIRE(req->response.result()==200);
      REQUIRE(req->response.body.length());

      // SSL Negative tests (TODO)

      as->stop();
    });

    // auto client = asyik::make_http_connection(as, "127.0.0.1", "80");
    // auto req=std::make_shared<http_request>();
    // client->send_request(req);
  
    as->run();
  }


} // namespace asyik
