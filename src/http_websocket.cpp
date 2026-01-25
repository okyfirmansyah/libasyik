#include "aixlog.hpp"
#include "libasyik/http.hpp"
#include "libasyik/internal/asio_internal.hpp"
#include "libasyik/service.hpp"

namespace asyik {

// Forward declarations
websocket_ptr make_websocket_connection_plain(service_ptr as,
                                              const http_url_scheme& scheme,
                                              const int timeout);
websocket_ptr make_websocket_connection_ssl(service_ptr as,
                                            const http_url_scheme& scheme,
                                            const int timeout);

websocket_ptr make_websocket_connection(service_ptr as, string_view url,
                                        const int timeout)
{
  http_url_scheme scheme;

  if (http_analyze_url(url, scheme)) {
    if (scheme.is_ssl()) {
      return make_websocket_connection_ssl(as, scheme, timeout);
    } else {
      return make_websocket_connection_plain(as, scheme, timeout);
    }
  } else
    return nullptr;
}

}  // namespace asyik
