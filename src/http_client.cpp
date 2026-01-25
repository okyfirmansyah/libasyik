#include "aixlog.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"

namespace asyik {

http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url)
{
  return http_easy_request(as, method, url, "");
}

}  // namespace asyik
