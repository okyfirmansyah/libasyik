#include "aixlog.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"

namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace ssl = asio::ssl;

namespace asyik {

http_server_ptr<https_stream_type> make_https_server(service_ptr as,
                                                     ssl::context&& ssl,
                                                     string_view addr,
                                                     uint16_t port,
                                                     bool reuse_port)
{
  auto p = std::make_shared<http_server<https_stream_type>>(
      http_server<https_stream_type>::private_{}, as, addr, port);
  p->ssl_context = std::make_shared<ssl::context>(std::move(ssl));

  internal::set_acceptor_reuse_options(*p->acceptor, reuse_port);

  p->acceptor->bind(
      ip::tcp::endpoint(ip::make_address(std::string{addr}), port));
  p->acceptor->listen();

  p->start_accept(as->get_io_service());
  return p;
}

}  // namespace asyik
