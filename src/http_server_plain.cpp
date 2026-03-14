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

  int one = 1;
#ifdef _WIN32
  setsockopt(p->acceptor->native_handle(), SOL_SOCKET,
             SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#else
  setsockopt(p->acceptor->native_handle(), SOL_SOCKET,
             SO_REUSEADDR | (SO_REUSEPORT * reuse_port), &one, sizeof(one));
#endif

  p->acceptor->bind(
      ip::tcp::endpoint(ip::make_address(std::string{addr}), port));
  p->acceptor->listen();
  p->start_accept(as->get_io_service());
  return p;
}

}  // namespace asyik
