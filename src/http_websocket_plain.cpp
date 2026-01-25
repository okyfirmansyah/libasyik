#include "aixlog.hpp"
#include "libasyik/http.hpp"
#include "libasyik/internal/asio_internal.hpp"
#include "libasyik/service.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;

namespace asyik {

websocket_ptr make_websocket_connection_plain(service_ptr as,
                                              const http_url_scheme& scheme,
                                              const int timeout)
{
  tcp::resolver resolver(asio::make_strand(as->get_io_service()));

  tcp::resolver::results_type results =
      internal::socket::async_resolve(resolver, std::string(scheme.host()),
                                      std::to_string(scheme.port()))
          .get();

  using stream_type = beast::websocket::stream<beast::tcp_stream>;
  auto new_ws = std::make_shared<websocket_impl<stream_type>>(
      websocket_impl<beast::websocket::stream<beast::tcp_stream>>::private_{},
      scheme.host(), std::to_string(scheme.port()), scheme.target());

  new_ws->ws =
      std::make_shared<stream_type>(asio::make_strand(as->get_io_service()));

  // Set the timeout for the operation
  beast::get_lowest_layer(*new_ws->ws)
      .expires_after(std::chrono::seconds(timeout));

  // Make the connection on the IP address we get from a lookup
  internal::socket::async_connect(beast::get_lowest_layer(*new_ws->ws), results)
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
  if (scheme.port() == 80 || scheme.port() == 443)
    internal::websocket::async_handshake(
        *new_ws->ws, scheme.host(),
        scheme.target().length() ? scheme.target() : "/")
        .get();
  else
    internal::websocket::async_handshake(
        *new_ws->ws,
        std::string(scheme.host()) + ":" + std::to_string(scheme.port()),
        scheme.target())
        .get();

  return new_ws;
}

}  // namespace asyik
