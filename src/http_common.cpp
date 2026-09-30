#include <regex>

#include "aixlog.hpp"
#include "libasyik/http.hpp"

namespace asyik {
namespace internal {

std::string route_spec_to_regex(string_view route_spc)
{
  std::string regex_spec{route_spc};

  // Warn if the spec appears to contain raw regex metacharacters.
  // Users who need raw regex should use on_http_request_regex() instead.
  {
    static const std::regex raw_regex_hint{
        R"(\(|\)|\\[dDwWsS]|\[.+\]|\\.\*|\\.\+)"};
    if (std::regex_search(regex_spec, raw_regex_hint)) {
      LOG(WARNING)
          << "route_spec '" << regex_spec
          << "' appears to contain raw regex characters which will be "
             "escaped. Use on_http_request_regex() for raw regex patterns.\n";
    }
  }

  // step 1: trim trailing .
  if (regex_spec[regex_spec.length() - 1] == '/')
    regex_spec = regex_spec.substr(0, regex_spec.length() - 1);

  // step 2: add regex escaping
  std::regex specialChars{R"([-[\]{}/()*+?.,\^$|#])"};
  regex_spec = std::regex_replace(regex_spec, specialChars, R"(\$&)");

  // step 3: replace <int>, <string>, and <path>
  std::regex int_tag{R"(<\s*int\s*>)"};
  regex_spec = std::regex_replace(regex_spec, int_tag, R"(([0-9]+))");
  std::regex string_tag{R"(<\s*string\s*>)"};
  regex_spec = std::regex_replace(regex_spec, string_tag, R"(([^\/\?\s]+))");
  std::regex path_tag{R"(<\s*path\s*>)"};
  regex_spec = std::regex_replace(regex_spec, path_tag, R"(([^?#\s]*))");

  // step 4: add ^...$ and optional trailing /
  regex_spec = "^" + regex_spec + R"(\/?(|\?[^\?\s]*)$)";

  return regex_spec;
}

void set_acceptor_reuse_options(ip::tcp::acceptor& acceptor, bool reuse_port)
{
#ifdef _WIN32
  // Windows has no SO_REUSEPORT, and its SO_REUSEADDR lets any socket steal
  // the port (non-deterministic delivery), so neither emulates Linux. Bind
  // exclusively instead; TIME_WAIT does not block re-binding on Windows.
  // A second acceptor on the same port therefore fails with address_in_use.
  if (reuse_port)
    LOG(WARNING) << "reuse_port (SO_REUSEPORT) is not supported on Windows, "
                    "ignored\n";
  BOOL one = TRUE;
  ::setsockopt(acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&one), sizeof(one));
#else
  int one = 1;
  setsockopt(acceptor.native_handle(), SOL_SOCKET,
             SO_REUSEADDR | (SO_REUSEPORT * reuse_port), &one, sizeof(one));
#endif
}

}  // namespace internal

bool http_analyze_url(string_view u, http_url_scheme& scheme)
{
  namespace url = boost::urls;
  boost::system::result<url::url_view> r = url::parse_uri(u);

  if (!r.has_error() && r.has_value()) {
    scheme.uv = r.value();

    if (!scheme.uv.host().length()) {
      LOG(ERROR) << "error on http_analyze_url, missing URL host()\n";
      return false;
    }
    scheme.is_ssl_ = (scheme.uv.scheme_id() == url::scheme::https) ||
                     (scheme.uv.scheme_id() == url::scheme::wss);

    scheme.port_ = scheme.uv.port_number();
    if (!scheme.port_ && scheme.uv.has_scheme())
      scheme.port_ = (scheme.is_ssl_ ? 443 : 80);
    scheme.username_ = scheme.uv.user();
    scheme.password_ = scheme.uv.password();

    return true;
  } else {
    LOG(ERROR) << "error=" << r.error().message() << "\n";
    return false;
  }
}

}  // namespace asyik
