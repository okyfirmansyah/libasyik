#include <regex>

#include "aixlog.hpp"
#include "libasyik/http.hpp"

namespace asyik {
namespace internal {

std::string route_spec_to_regex(string_view route_spc)
{
  std::string regex_spec{route_spc};

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
