#include "aixlog.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"

namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;

namespace asyik {

http_server_ptr<http_stream_type> make_http_server(service_ptr as,
                                                   string_view addr,
                                                   uint16_t port,
                                                   bool reuse_port)
{
  auto p = std::make_shared<http_server<http_stream_type>>(
      http_server<http_stream_type>::private_{}, as, addr, port);

  internal::set_acceptor_reuse_options(*p->acceptor, reuse_port);

  p->acceptor->bind(
      ip::tcp::endpoint(ip::make_address(std::string{addr}), port));
  p->acceptor->listen();
  p->start_accept(as->get_io_service());
  return p;
}

}  // namespace asyik
