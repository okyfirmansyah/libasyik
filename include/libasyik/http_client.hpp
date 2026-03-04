#ifndef LIBASYIK_ASYIK_HTTP_CLIENT_HPP
#define LIBASYIK_ASYIK_HTTP_CLIENT_HPP

#include <boost/any.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/fiber/all.hpp>
#include <boost/optional.hpp>
#include <boost/url.hpp>
#include <map>
#include <string>

#include "asyik_fwd.hpp"
#include "boost/asio.hpp"
#include "common.hpp"
#include "error.hpp"
#include "http_types.hpp"
#include "internal/asio_internal.hpp"
#include "internal/digestauth.hpp"
#include "service.hpp"

namespace fibers = boost::fibers;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

namespace asyik {

// HTTP client request functions
http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url);

template <typename D>
http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url, D&& data);

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, string_view method, string_view url, D&& data,
    const std::map<string_view, string_view>& headers);

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, int timeout_ms, string_view method, string_view url,
    D&& data, const std::map<string_view, string_view>& headers);

// Multipart request functions
template <typename D, typename F>
http_request_ptr http_easy_request_multipart(
    service_ptr as, int timeout_ms, string_view method, string_view url,
    D&& data, const std::map<string_view, string_view>& headers, F&& f,
    const digest_authenticator* auth = nullptr);

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(
    service_ptr as, string_view method, string_view url, D&& data,
    const std::map<string_view, string_view>& headers, F&& f);

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(service_ptr as, string_view method,
                                             string_view url, D&& data, F&& f);

template <typename F>
http_request_ptr http_easy_request_multipart(service_ptr as, string_view method,
                                             string_view url, F&& f);

// URL scheme parsing
struct http_url_scheme {
  boost::urls::url_view get_url_view() const { return uv; }

  bool is_ssl() const { return is_ssl_; }
  string_view host() const
  {
    return string_view(uv.encoded_host().data(), uv.encoded_host().length());
  }
  string_view authority() const
  {
    return string_view(uv.authority().data(), uv.authority().size());
  }
  string_view username() const { return username_; }
  string_view password() const { return password_; }
  uint16_t port() const { return port_; }
  string_view target() const
  {
    return string_view(uv.encoded_resource().data(),
                       uv.encoded_resource().size());
  }

 private:
  boost::urls::url_view uv;
  std::string username_;
  std::string password_;
  bool is_ssl_;
  uint16_t port_;

  friend bool http_analyze_url(string_view u, http_url_scheme& scheme);
};

bool http_analyze_url(string_view url, http_url_scheme& scheme);

// HTTP request class
class http_request : public std::enable_shared_from_this<http_request> {
 private:
  struct private_ {};

 public:
  ~http_request(){};
  http_request& operator=(const http_request&) = delete;
  http_request(const http_request&) = delete;
  http_request(http_request&&) = default;
  http_request& operator=(http_request&&) = default;

  http_request()
      : beast_request(),
        headers(beast_request.base()),
        body(beast_request.body()),
        response(),
        multipart_response(),
        manual_response(false){};

  http_beast_request beast_request;
  http_request_headers& headers;
  http_request_body& body;
  boost::urls::url_view uv;

  template <typename S>
  inline auto get_connection_handle(S server)
      -> decltype(server->get_request_connection(*this))
  {
    return server->get_request_connection(*this);
  }

  string_view target() const { return beast_request.target(); };

  void target(string_view t) { beast_request.target(t); };

  string_view method() const { return beast_request.method_string(); };

  void method(string_view verb)
  {
    beast_request.method(beast::http::string_to_verb(verb));
  };

  void set_url_view() { uv = boost::urls::url_view(target()); }

  boost::urls::url_view& get_url_view() { return uv; }

  struct response {
    response()
        : beast_response(),
          headers(beast_response.base()),
          body(beast_response.body()){};
    http_beast_response beast_response;
    http_response_headers& headers;
    http_response_body& body;
    http_result result() const { return beast_response.result_int(); };
    void result(http_result res) { beast_response.result(res); };
  } response, multipart_response;

  void activate_direct_response_handling() { manual_response = true; }

 private:
  boost::beast::flat_buffer buffer;
  bool manual_response;
  boost::any connection_wptr;

  template <typename StreamType>
  friend class http_server;

  template <typename StreamType>
  friend class http_connection;

  template <typename D, typename F>
  friend http_request_ptr http_easy_request_multipart(
      service_ptr as, int timeout_ms, string_view method, string_view url,
      D&& data, const std::map<string_view, string_view>& headers, F&& f,
      const digest_authenticator* auth);

  template <typename S, typename R, typename F>
  friend boost::fibers::future<size_t> handle_client_request_response(
      S& stream, int timeout_ms, R& req, http_url_scheme& scheme, F& f);
};

// Helper functions for multipart handling
inline boost::optional<std::string> deduce_boundary(
    beast::string_view content_type);

template <typename S, typename B>
bool find_multipart_boundary(S& stream, B& buffer, const std::string& boundary);

template <typename S, typename Buf, typename BR, typename P>
void handle_client_auth(S& stream, http_url_scheme& scheme, Buf& buffer,
                        BR& beast_request, P& empty_parser);

template <typename S, typename R, typename F>
boost::fibers::future<size_t> handle_client_request_response(
    S& stream, int timeout_ms, R& req, http_url_scheme& scheme, F& f);

// Template implementations must be included from main http.hpp

}  // namespace asyik

#endif
