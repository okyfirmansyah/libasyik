#include "libasyik/http.hpp"

#include <iostream>
#include <regex>

#include "aixlog.hpp"
#include "boost/beast/core.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/common.hpp"
#include "libasyik/internal/asio_internal.hpp"
#include "libasyik/service.hpp"

namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace beast = boost::beast;

namespace asyik {
std::string internal::route_spec_to_regex(string_view route_spc)
{
  std::string regex_spec{route_spc};

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
  regex_spec = std::regex_replace(regex_spec, string_tag, R"(([^\/\?\s]+))");

  // step 4: add ^...$ and optional trailing /
  regex_spec = "^" + regex_spec + R"(\/?(|\?[^\?\s]*)$)";

  return regex_spec;
}

bool http_analyze_url(string_view url, http_url_scheme& scheme)
{
  static std::regex re(
      R"(^(http\:\/\/|https\:\/\/|ws\:\/\/|wss\:\/\/|)(([^\/\s\:]+)(:\d{1,5})|([^\/\s\:]+)())(\/[^\:\s]*|)$)",
      std::regex_constants::icase);
  static std::regex re2(R"(^(https|wss)\:\/\/)", std::regex_constants::icase);

  std::smatch m;
  std::string str{url};
  if (std::regex_search(str, m, re)) {
    // check if ssl or not ssl
    if (m[1].str().compare("")) {
      scheme.is_ssl = std::regex_match(m[1].str(), re2);
    } else
      scheme.is_ssl = false;

    // is there any port specification or not
    if (m[3].str().length()) {
      scheme.host = m[3].str();
      scheme.port = std::atoi(&m[4].str().c_str()[1]);
    } else {
      scheme.host = m[5].str();
      scheme.port = scheme.is_ssl ? 443 : 80;
    }

    scheme.target = m[7].str();
    if (!scheme.target.length()) scheme.target = "/";

    return true;
  } else
    return false;
}

http_server_ptr<http_stream_type> make_http_server(service_ptr as,
                                                   string_view addr,
                                                   uint16_t port,
                                                   bool reuse_port)
{
  auto p = std::make_shared<http_server<http_stream_type>>(
      http_server<http_stream_type>::private_{}, as, addr, port);

  int one = 1;
  setsockopt(p->acceptor->native_handle(), SOL_SOCKET,
             SO_REUSEADDR | (SO_REUSEPORT * reuse_port), &one, sizeof(one));

  p->acceptor->bind(
      ip::tcp::endpoint(ip::address::from_string(std::string{addr}), port));
  p->acceptor->listen();
  p->start_accept(as->get_io_service());
  return p;
}

http_server_ptr<https_stream_type> make_https_server(service_ptr as,
                                                     ssl::context&& ssl,
                                                     string_view addr,
                                                     uint16_t port,
                                                     bool reuse_port)
{
  auto p = std::make_shared<http_server<https_stream_type>>(
      http_server<https_stream_type>::private_{}, as, addr, port);
  p->ssl_context = std::make_shared<ssl::context>(std::move(ssl));

  int one = 1;
  setsockopt(p->acceptor->native_handle(), SOL_SOCKET,
             SO_REUSEADDR | (SO_REUSEPORT * reuse_port), &one, sizeof(one));

  p->acceptor->bind(
      ip::tcp::endpoint(ip::address::from_string(std::string{addr}), port));
  p->acceptor->listen();

  p->start_accept(as->get_io_service());
  return p;
}

// http_connection_ptr make_http_connection(service_ptr as, string_view addr,
// string_view port)
//{
// tcp::resolver resolver(as->get_io_service().get_executor());

// tcp::resolver::results_type results=internal::socket::async_resolve(resolver,
// addr, port).get();

// auto new_connection = std::make_shared<http_connection>
//                      (http_connection::private_{},
//                      as->get_io_service().get_executor());

// // Set the timeout for the operation
// new_connection->get_socket().expires_after(std::chrono::seconds(30));

// // Make the connection on the IP address we get from a lookup
// new_connection->get_socket().async_connect(
//         results,
//         [](const beast::error_code &ec,
//            tcp::resolver::results_type::endpoint_type ep)
//         {

//         });

//  return nullptr;
//}

websocket_ptr make_websocket_connection(service_ptr as, string_view url,
                                        const int timeout)
{
  http_url_scheme scheme;

  if (http_analyze_url(url, scheme)) {
    tcp::resolver resolver(asio::make_strand(as->get_io_service()));

    tcp::resolver::results_type results =
        internal::socket::async_resolve(resolver, scheme.host,
                                        std::to_string(scheme.port))
            .get();

    if (scheme.is_ssl) {
      // The SSL context is required, and holds certificates
      ssl::context ctx(ssl::context::tlsv12_client);

      // This holds the root certificate used for verification
      // load_root_certificates(ctx);

      ctx.set_default_verify_paths();
      ctx.set_verify_mode(ssl::verify_none);  //!!!

      using stream_type =
          beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>;
      auto new_ws = std::make_shared<websocket_impl<stream_type>>(
          websocket_impl<stream_type>::private_{},
          as->get_io_service().get_executor(), scheme.host,
          std::to_string(scheme.port), scheme.target);

      new_ws->ws = std::make_shared<stream_type>(
          as->get_io_service().get_executor(), ctx);

      // Set the timeout for the operation
      beast::get_lowest_layer(*new_ws->ws)
          .expires_after(std::chrono::seconds(timeout));

      internal::socket::async_connect(beast::get_lowest_layer(*new_ws->ws),
                                      results)
          .get();

      internal::ssl::async_handshake(new_ws->ws->next_layer(),
                                     ssl::stream_base::client)
          .get();

      // Turn off the timeout on the tcp_stream, because
      // the websocket stream has its own timeout system.
      beast::get_lowest_layer(*new_ws->ws).expires_never();

      // Set suggested timeout settings for the websocket
      new_ws->ws->set_option(beast::websocket::stream_base::timeout::suggested(
          beast::role_type::client));

      // Set a decorator to change the User-Agent of the handshake
      new_ws->ws->set_option(beast::websocket::stream_base::decorator(
          [](beast::websocket::request_type& req) {
            req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-client-async");
          }));

      // Perform the websocket handshake
      if (scheme.port == 80 || scheme.port == 443)
        internal::websocket::async_handshake(*new_ws->ws, scheme.host,
                                             scheme.target)
            .get();
      else
        internal::websocket::async_handshake(
            *new_ws->ws, scheme.host + ":" + std::to_string(scheme.port),
            scheme.target)
            .get();

      return new_ws;
    } else {
      using stream_type = beast::websocket::stream<beast::tcp_stream>;
      auto new_ws = std::make_shared<websocket_impl<stream_type>>(
          websocket_impl<
              beast::websocket::stream<beast::tcp_stream>>::private_{},
          as->get_io_service().get_executor(), scheme.host,
          std::to_string(scheme.port), scheme.target);

      new_ws->ws =
          std::make_shared<stream_type>(as->get_io_service().get_executor());

      // Set the timeout for the operation
      beast::get_lowest_layer(*new_ws->ws)
          .expires_after(std::chrono::seconds(timeout));

      // Make the connection on the IP address we get from a lookup
      internal::socket::async_connect(beast::get_lowest_layer(*new_ws->ws),
                                      results)
          .get();

      // Turn off the timeout on the tcp_stream, because
      // the websocket stream has its own timeout system.
      beast::get_lowest_layer(*new_ws->ws).expires_never();

      // Set suggested timeout settings for the websocket
      new_ws->ws->set_option(beast::websocket::stream_base::timeout::suggested(
          beast::role_type::client));

      // Set a decorator to change the User-Agent of the handshake
      new_ws->ws->set_option(beast::websocket::stream_base::decorator(
          [](beast::websocket::request_type& req) {
            req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-client-async");
          }));

      // Perform the websocket handshake
      if (scheme.port == 80 || scheme.port == 443)
        internal::websocket::async_handshake(*new_ws->ws, scheme.host,
                                             scheme.target)
            .get();
      else
        internal::websocket::async_handshake(
            *new_ws->ws, scheme.host + ":" + std::to_string(scheme.port),
            scheme.target)
            .get();

      return new_ws;
    }
  } else
    return nullptr;
}

http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url)
{
  return http_easy_request(as, method, url, "");
};

}  // namespace asyik
