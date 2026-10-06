#include "aixlog.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"
#include "libasyik/tls.hpp"

namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace ssl = asio::ssl;

namespace asyik {

namespace internal {

http_server_ptr<https_stream_type> start_https_server(
    service_ptr as, std::shared_ptr<boost::asio::ssl::context> ctx,
    string_view addr, uint16_t port, bool reuse_port)
{
  auto p = std::make_shared<http_server<https_stream_type>>(
      http_server<https_stream_type>::private_{}, as, addr, port);
  p->ssl_context = std::move(ctx);

  internal::set_acceptor_reuse_options(*p->acceptor, reuse_port);

  p->acceptor->bind(
      ip::tcp::endpoint(ip::make_address(std::string{addr}), port));
  p->acceptor->listen();

  p->start_accept(as->get_io_service());
  return p;
}

}  // namespace internal

http_server_ptr<https_stream_type> make_https_server(
    service_ptr as, const tls::server_config& cfg, string_view addr,
    uint16_t port, bool reuse_port)
{
  return make_https_server(as, tls::make_server_context(cfg), addr, port,
                           reuse_port);
}

http_server_ptr<https_stream_type> make_https_server(
    service_ptr as, tls::server_context_ptr ctx, string_view addr,
    uint16_t port, bool reuse_port)
{
  if (!ctx) throw invalid_input_error("make_https_server: null TLS context");
  // Aliasing pointer: connections hold the ssl::context, which keeps the
  // whole server_context (and its ALPN data) alive.
  std::shared_ptr<boost::asio::ssl::context> native(ctx, &ctx->native());
  return internal::start_https_server(as, std::move(native), addr, port,
                                      reuse_port);
}

http_server_ptr<https_stream_type> make_https_server(service_ptr as,
                                                     ssl::context&& ssl,
                                                     string_view addr,
                                                     uint16_t port,
                                                     bool reuse_port)
{
  return internal::start_https_server(
      as, std::make_shared<ssl::context>(std::move(ssl)), addr, port,
      reuse_port);
}

}  // namespace asyik
