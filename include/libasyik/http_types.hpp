#ifndef LIBASYIK_ASYIK_HTTP_TYPES_HPP
#define LIBASYIK_ASYIK_HTTP_TYPES_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <functional>
#include <regex>
#include <string>
#include <vector>

#include "asyik_fwd.hpp"

namespace asyik {

// Default limits
static const size_t default_request_body_limit = 1 * 1024 * 1024;
static const size_t default_request_header_limit = 1 * 1024 * 1024;
static const size_t default_response_body_limit = 64 * 1024 * 1024;
static const size_t default_response_header_limit = 1 * 1024 * 1024;

// Type aliases
using http_route_args = std::vector<std::string>;

using http_beast_request =
    boost::beast::http::request<boost::beast::http::string_body>;
using http_beast_response =
    boost::beast::http::response<boost::beast::http::string_body>;
using http_request_headers = http_beast_request::header_type;
using http_request_body = http_beast_request::body_type::value_type;
using http_response_headers = http_beast_response::header_type;
using http_response_body = http_beast_response::body_type::value_type;

using http_route_callback =
    std::function<void(http_request_ptr, const http_route_args&)>;
using http_route_tuple =
    std::tuple<std::string, std::regex, http_route_callback>;

using websocket_route_callback =
    std::function<void(websocket_ptr, const http_route_args&)>;
using websocket_route_tuple =
    std::tuple<std::string, std::regex, websocket_route_callback>;

using websocket_close_code = boost::beast::websocket::close_code;

}  // namespace asyik

#endif
