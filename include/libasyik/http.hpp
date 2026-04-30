#ifndef LIBASYIK_ASYIK_HTTP_HPP
#define LIBASYIK_ASYIK_HTTP_HPP

// Main HTTP header - includes all modular headers for backward compatibility
// This file now serves as a facade that includes all split headers

#include <boost/algorithm/string/predicate.hpp>
#include <boost/any.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/url.hpp>
#include <regex>
#include <string>

#include "aixlog.hpp"
#include "asyik_fwd.hpp"
#include "boost/asio.hpp"
#include "boost/fiber/all.hpp"
#include "common.hpp"
#include "error.hpp"
#include "internal/asio_internal.hpp"
#include "internal/digestauth.hpp"
#include "service.hpp"

// Include modular headers
#include "http_client.hpp"
#include "http_server.hpp"
#include "http_types.hpp"
#include "http_websocket.hpp"
#include "profiling.hpp"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace ip = boost::asio::ip;
namespace websocket = beast::websocket;
namespace http = boost::beast::http;

namespace asyik {
void _TEST_invoke_http();

// Remaining template function implementations that need to stay in main header

inline boost::optional<std::string> deduce_boundary(
    beast::string_view content_type)
{
  boost::optional<std::string> result;

  static std::regex r1{
      R"regex(^multipart/mixed-replace\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r2{
      R"regex(^multipart/mixed-replace\s*;\s*boundary=([^"]+)$)regex",
      std::regex_constants::icase};
  static std::regex r3{
      R"regex(^multipart/mixed\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r4{R"regex(^multipart/mixed\s*;\s*boundary=([^"]+)$)regex",
                       std::regex_constants::icase};
  static std::regex r5{
      R"regex(^multipart/x-mixed-replace\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r6{
      R"regex(^multipart/x-mixed-replace\s*;\s*boundary=([^"]+)$)regex",
      std::regex_constants::icase};
  static std::regex r7{
      R"regex(^multipart/form-data\s*;\s*boundary="([^"]+)"$)regex",
      std::regex_constants::icase};
  static std::regex r8{
      R"regex(^multipart/form-data\s*;\s*boundary=([^"]+)$)regex",
      std::regex_constants::icase};

  auto match = std::cmatch();

  if (std::regex_match(content_type.begin(), content_type.end(), match, r1) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r2) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r3) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r4) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r5) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r6) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r7) ||
      std::regex_match(content_type.begin(), content_type.end(), match, r8)) {
    result = match[1].str();
  }

  return result;
};

template <typename S, typename B>
bool find_multipart_boundary(S& stream, B& buffer, const std::string& boundary)
{
  std::string boundary_line = boundary + "\r\n";
  std::string terminate_line = boundary + "--\r\n";

  while (1) {
    string_view s(asio::buffer_cast<const char*>(buffer.data()), buffer.size());

    auto pos = s.find(boundary_line);
    if (pos > s.size()) {
      if (s.find(terminate_line) < s.size()) {
        // reach end of stream
        return false;
      }
      auto b = buffer.prepare(65536);

      auto sz = stream.async_read_some(b, asyik::use_fiber_future).get();
      buffer.commit(sz);
    } else {
      buffer.consume(pos + boundary_line.length());
      return true;
    }
  }
}

template <typename S, typename Buf, typename BR, typename P>
void handle_client_auth(S& stream, http_url_scheme& scheme, Buf& buffer,
                        BR& beast_request, P& empty_parser)
{
  http::response_parser<http::string_body> resp_parser_unauth{
      std::move(empty_parser)};

  asyik::internal::http::async_read(stream, buffer, resp_parser_unauth).get();

  digest_authenticator auth(
      resp_parser_unauth.get()[http::field::www_authenticate],
      scheme.username(), scheme.password(), scheme.target(),
      beast_request.method_string(), resp_parser_unauth.get().body());

  auth.generateAuthorization();
  throw auth;
}

template <typename S, typename R, typename F>
boost::fibers::future<size_t> handle_client_request_response(
    S& stream, int timeout_ms, R& req, http_url_scheme& scheme, F& f)
{
  auto w = internal::http::async_write(stream, req->beast_request);

  http::response_parser<http::empty_body> empty_parser;
  empty_parser.eager(false);
  empty_parser.header_limit(default_response_header_limit);
  empty_parser.body_limit(default_response_body_limit);

  asyik::internal::http::async_read_header(stream, req->buffer, empty_parser)
      .get();

  if ((empty_parser.get().result() ==
       boost::beast::http::status::unauthorized) &&
      scheme.username().length() && scheme.password().length()) {
    w.get();
    handle_client_auth(stream, scheme, req->buffer, req->beast_request,
                       empty_parser);
  }

  if (empty_parser.get().count("content-type") &&
      (empty_parser.get().at("content-type").contains("multipart"))) {
    // Multipart handling

    auto boundary =
        deduce_boundary(empty_parser.get()[http::field::content_type]);

    if (!boundary)
      throw asyik::unexpected_error(
          "HTTP response error, cannot get boundary token");

    http::response_parser<http::string_body> resp_parser{
        std::move(empty_parser)};
    req->response.beast_response = resp_parser.release();

    while (find_multipart_boundary(stream, req->buffer,
                                   boundary.get_value_or(""))) {
      beast::get_lowest_layer(stream).expires_after(
          std::chrono::milliseconds(timeout_ms));

      http::response_parser<http::empty_body> empty_mpart_parser;
      empty_mpart_parser.eager(false);
      empty_mpart_parser.header_limit(default_response_header_limit);
      empty_mpart_parser.body_limit(default_response_body_limit);

      std::string status =
          "HTTP/1.1 " + std::to_string((int)resp_parser.get().result());
      status += " " + std::string(resp_parser.get().reason()) + "\r\n";

      boost::beast::error_code ec;
      empty_mpart_parser.put(asio::buffer(status), ec);

      asyik::internal::http::async_read_header(stream, req->buffer,
                                               empty_mpart_parser)
          .get();

      if (!empty_mpart_parser.content_length())
        throw asyik::unexpected_error(
            "HTTP multipart without part content-length is not "
            "supported!");

      http::response_parser<http::string_body> resp_mpart_parser{
          std::move(empty_mpart_parser)};
      asyik::internal::http::async_read(stream, req->buffer, resp_mpart_parser)
          .get();

      // proses resp_mpart_parser;
      req->multipart_response.beast_response = resp_mpart_parser.release();

      f(req);
    }
  } else {
    // non-multipart handling
    http::response_parser<http::string_body> resp_parser{
        std::move(empty_parser)};

    asyik::internal::http::async_read(stream, req->buffer, resp_parser).get();

    req->response.beast_response = resp_parser.release();
  }
  return w;
}

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(
    service_ptr as, int timeout_ms, string_view method, string_view url,
    D&& data, const std::map<string_view, string_view>& headers, F&& f,
    const digest_authenticator* auth)
{
  http_url_scheme scheme;

  BOOST_ASSERT(timeout_ms > 0);

  if (http_analyze_url(url, scheme)) {
    auto req = std::make_shared<http_request>();

    if (scheme.port() == 80 || scheme.port() == 443)
      req->headers.set("Host", scheme.host());
    else
      req->headers.set("Host", std::string(scheme.host()) + ":" +
                                   std::to_string(scheme.port()));
    req->headers.set("User-Agent", LIBASYIK_VERSION_STRING);
    req->headers.set("Accept", "*/*");

    if (auth) {
      req->headers.set("Accept-Encoding", "identity");
      req->headers.set("Authorization", auth->authorization());
    }

    // user-overidden headers
    for (const auto& item : headers) req->headers.set(item.first, item.second);

    req->body = std::forward<D>(data);
    if (req->body.length()) {
      if (!req->headers.count(http::field::content_type))
        req->headers.set("Content-Type", "text/html");
    }
    req->beast_request.version(11);
    req->method(method);
    req->target(scheme.target().length() ? scheme.target() : "/");
    req->beast_request.keep_alive(true);
    req->headers.set("Connection", "Keep-Alive");

    req->beast_request.prepare_payload();

    tcp::resolver resolver(as->get_io_service().get_executor());
    tcp::resolver::results_type results =
        internal::socket::async_resolve(resolver, std::string(scheme.host()),
                                        std::to_string(scheme.port()))
            .get();

    if (scheme.is_ssl()) {
      // The SSL context is required, and holds certificates
      ssl::context ctx(ssl::context::tlsv12_client);

      // This holds the root certificate used for verification
      // load_root_certificates(ctx);

      ctx.set_default_verify_paths();
      ctx.set_verify_mode(ssl::verify_none);  //!!!

      beast::ssl_stream<beast::tcp_stream> stream(
          asio::make_strand(as->get_io_service()), ctx);

      stream.next_layer().expires_after(std::chrono::milliseconds(timeout_ms));

      internal::socket::async_connect(beast::get_lowest_layer(stream), results)
          .get();
      internal::ssl::async_handshake(stream, ssl::stream_base::client).get();

      boost::fibers::future<size_t> is_write_done;
      try {
        is_write_done =
            handle_client_request_response(stream, timeout_ms, req, scheme, f);
      } catch (digest_authenticator& auth) {
        LOG(TRACE) << "digest_authenticator=" << auth.authorization() << "\n";

        req = http_easy_request_multipart(as, timeout_ms, method, url,
                                          std::forward<D>(data), headers,
                                          std::forward<F>(f), &auth);
      }
      try {
        is_write_done.get();
        internal::ssl::async_shutdown(stream).get();
      } catch (...) {
        // it's okay, we need both parties to close SSL gracefully,
        // but it's not always the case
        // https://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
      }

      return req;
    } else {
      beast::tcp_stream stream(asio::make_strand(as->get_io_service()));

      stream.expires_after(std::chrono::milliseconds(timeout_ms));

      internal::socket::async_connect(stream, results).get();

      boost::fibers::future<size_t> is_write_done;
      try {
        is_write_done =
            handle_client_request_response(stream, timeout_ms, req, scheme, f);
      } catch (digest_authenticator& auth) {
        LOG(TRACE) << "digest_authenticator=" << auth.authorization() << "\n";

        req = http_easy_request_multipart(as, timeout_ms, method, url,
                                          std::forward<D>(data), headers,
                                          std::forward<F>(f), &auth);
      }

      beast::error_code ec;
      try {
        is_write_done.get();
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
      } catch (...) {
        // it's okay, we need both parties to close SSL gracefully,
        // but it's not always the case
        // https://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
      }

      return req;
    }
  } else
    return nullptr;
};

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(
    service_ptr as, string_view method, string_view url, D&& data,
    const std::map<string_view, string_view>& headers, F&& f)
{
  return http_easy_request_multipart(as, 30000, method, url,
                                     std::forward<D>(data), headers,
                                     std::forward<F>(f));
}

template <typename D, typename F>
http_request_ptr http_easy_request_multipart(service_ptr as, string_view method,
                                             string_view url, D&& data, F&& f)
{
  return http_easy_request_multipart(as, method, url, std::forward<D>(data), {},
                                     std::forward<F>(f));
};

template <typename F>
http_request_ptr http_easy_request_multipart(service_ptr as, string_view method,
                                             string_view url, F&& f)

{
  return http_easy_request_multipart(as, method, url, "", std::forward<F>(f));
}

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, int timeout_ms, string_view method, string_view url,
    D&& data, const std::map<string_view, string_view>& headers)
{
  return http_easy_request_multipart(
      as, timeout_ms, method, url, std::forward<D>(data), headers,
      [](http_request_ptr req) {
        throw asyik::unexpected_error(
            "could not handle multipart response using http_easy_request(). "
            "Use http_easy_request_multipart() instead");
      });
};

template <typename D>
http_request_ptr http_easy_request(
    service_ptr as, string_view method, string_view url, D&& data,
    const std::map<string_view, string_view>& headers)
{
  return http_easy_request(as, 30000, method, url, std::forward<D>(data),
                           headers);
}

template <typename D>
http_request_ptr http_easy_request(service_ptr as, string_view method,
                                   string_view url, D&& data)
{
  return http_easy_request(as, method, url, std::forward<D>(data), {});
};

template <>
inline void http_connection<http_stream_type>::handshake_if_ssl()
{}

template <>
inline void http_connection<https_stream_type>::handshake_if_ssl()
{
  internal::ssl::async_handshake(stream, ssl::stream_base::server).get();
}

template <>
inline void http_connection<http_stream_type>::shutdown_ssl()
{}

template <>
inline void http_connection<https_stream_type>::shutdown_ssl()
{
  internal::ssl::async_shutdown(stream).get();
}

template <typename StreamType>
void http_connection<StreamType>::start()
{
  if (auto server = http_server.lock()) {
    if (auto service = server->service.lock()) {
      service->execute([p = this->shared_from_this(),
                        req_pool = server->req_pool_,
                        body_limit = server->get_request_body_limit(),
                        header_limit =
                            server->get_request_header_limit()](void) {
        // flag to ignore eos error since work has been
        // done anyway
        bool safe_to_close = false;

        try {
          ip::tcp::no_delay option(true);
          beast::get_lowest_layer(p->get_stream()).socket().set_option(option);

          p->handshake_if_ssl();

          auto asyik_req = req_pool->acquire();
          auto& req = asyik_req->beast_request;
          asyik_req->connection_wptr = http_connection_wptr<StreamType>(p);
          while (1) {
            // Single-pass read: parse header + body in one async_read call
            // (eliminates the extra fiber suspend/resume of the old two-phase
            // async_read_header + async_read approach).
            http::request_parser<http::string_body> req_parser;
            req_parser.header_limit(header_limit);
            req_parser.body_limit(body_limit);

            asyik_req->buffer.clear();
#ifdef LIBASYIK_HTTP_PROFILING
            auto _p_t0 = std::chrono::steady_clock::now();
#endif
            asyik::internal::http::async_read(p->get_stream(),
                                              asyik_req->buffer, req_parser)
                .get();
#ifdef LIBASYIK_HTTP_PROFILING
            asyik::profiling::g_http_prof.read_request.record(
                ASYIK_PROF_NS(_p_t0));
#endif
            safe_to_close = false;
            req = req_parser.release();

            asyik_req->set_url_view();
            // See if its a WebSocket upgrade request
            if (beast::websocket::is_upgrade(req)) {
              // Clients SHOULD NOT begin sending WebSocket
              // frames until the server has provided a response.
              if (asyik_req->buffer.size() != 0)
                throw unexpected_input_error(
                    "unexpected data before websocket handshake!");

              try {
                if (p->http_server.expired())
                  throw network_expired_error("server expired");

                auto server = p->http_server.lock();

                http_route_args args;
                const websocket_route_tuple& route =
                    server->find_websocket_route(req, args);

                // Construct the str ;o,\eam, transferring ownership of the
                // socket
                if (server->service.expired())
                  throw network_expired_error("service expired");

                auto service = server->service.lock();

                auto new_ws = std::make_shared<
                    websocket_impl<beast::websocket::stream<StreamType>>>(
                    typename websocket_impl<
                        beast::websocket::stream<StreamType>>::private_{},
                    std::make_shared<beast::websocket::stream<StreamType>>(
                        std::move(p->get_stream())),
                    asyik_req);

                asyik::internal::websocket::async_accept(*new_ws->ws, req)
                    .get();
                p->is_websocket = true;

                try {
                  std::get<2>(route)(new_ws, args);
                } catch (...) {
                  LOG(ERROR)
                      << "uncaught error in routing handler: " << req.target()
                      << "\n";
                }
              } catch (...) {
              }
              return;
            } else {
              // Its not a WebSocket upgrade, so
              // handle it like a normal HTTP request.
              auto& res = asyik_req->response.beast_response;

              asyik_req->response.headers.set(http::field::server,
                                              LIBASYIK_VERSION_STRING);
              asyik_req->response.headers.set(http::field::content_type,
                                              "text/html");
              asyik_req->response.beast_response.keep_alive(
                  asyik_req->beast_request.keep_alive());

              // pretty much should be improved
              try {
                if (p->http_server.expired())
                  throw network_expired_error("server expired");
                auto server = p->http_server.lock();

                try {
                  http_route_args args;
#ifdef LIBASYIK_HTTP_PROFILING
                  auto _p_t2 = std::chrono::steady_clock::now();
#endif
                  const http_route_tuple& route =
                      server->find_http_route(req, args);
#ifdef LIBASYIK_HTTP_PROFILING
                  asyik::profiling::g_http_prof.route_match.record(
                      ASYIK_PROF_NS(_p_t2));
                  auto _p_t3 = std::chrono::steady_clock::now();
#endif
                  std::get<2>(route)(asyik_req, args);
#ifdef LIBASYIK_HTTP_PROFILING
                  asyik::profiling::g_http_prof.handler.record(
                      ASYIK_PROF_NS(_p_t3));
#endif
                } catch (not_found_error& e) {
                  asyik_req->response.body = "";
                  asyik_req->response.result(404);
                  asyik_req->response.beast_response.keep_alive(false);
                }
              } catch (...) {
                asyik_req->response.body = "";
                asyik_req->response.result(500);
                asyik_req->response.beast_response.keep_alive(false);
              };

              if (asyik_req->manual_response) {
                safe_to_close = true;
                break;
              }

              asyik_req->response.beast_response.prepare_payload();
#ifdef LIBASYIK_HTTP_PROFILING
              auto _p_t4 = std::chrono::steady_clock::now();
#endif
              asyik::internal::http::async_write(p->get_stream(), res).get();
#ifdef LIBASYIK_HTTP_PROFILING
              asyik::profiling::g_http_prof.write_response.record(
                  ASYIK_PROF_NS(_p_t4));
              asyik::profiling::g_http_prof.total.record(static_cast<uint64_t>(
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now() - _p_t0)
                      .count()));
#endif

              // Close when the response says so (Connection: close, HTTP/1.0
              // without keep-alive, or unknown body length). Otherwise loop
              // back and serve the next request on this persistent connection.
              if (res.need_eof()) {
                safe_to_close = true;
                break;
              }

              // Keep-alive: reset response state before the next request.
              asyik_req->response.beast_response = http_beast_response{};
              asyik_req->manual_response = false;
              // Mark safe so that an EOF on the next async_read (client
              // closing after receiving the response) is silently ignored
              // instead of flooding the log.
              safe_to_close = true;
            }
          }
          p->shutdown_ssl();
        }
        // TODO: all HTTP 5xx and 4xx handling should be put here instead
        catch (overflow_error& e) {
          http_beast_response beast_response;
          beast_response.body() = e.what();
          beast_response.keep_alive(false);
          beast_response.result(413);

          beast_response.prepare_payload();
          http::serializer<false, http::string_body> sr{beast_response};
          asyik::internal::http::async_write(p->get_stream(), beast_response)
              .get();

          p->shutdown_ssl();
        } catch (asyik::already_closed_error& e) {
          if (!safe_to_close) {
            LOG(WARNING) << "End of stream exception is catched during client "
                            "connection, reason: "
                         << e.what() << "\n";
          }
        } catch (std::exception& e) {
          LOG(WARNING)
              << "exception is catched during client connection, reason: "
              << e.what() << "\n";
        };
      });
    };
  };
}

template <typename StreamType>
http_server<StreamType>::http_server(struct private_&&, service_ptr as,
                                     string_view addr, uint16_t port)
    : service(as),
      conn_pool_(
          std::make_shared<shared_object_pool<http_connection<StreamType>>>()),
      req_pool_(std::make_shared<shared_object_pool<http_request>>()),
      request_body_limit(default_request_body_limit),
      request_header_limit(default_request_header_limit)
{
  acceptor =
      std::make_shared<ip::tcp::acceptor>(as->get_io_service(), tcp::v4());

  if (!acceptor) throw resource_error("could not allocate TCP acceptor");
}

template <typename StreamType>
void http_server<StreamType>::start_accept(asio::io_context& io_service)
{
  acceptor->async_accept(
      asio::make_strand(io_service),
      [p = std::weak_ptr<http_server<StreamType>>(this->shared_from_this())](
          const boost::system::error_code& error, tcp::socket socket) {
        if (!p.expired() && !error) {
          auto ps = p.lock();
          auto new_connection = ps->conn_pool_->acquire(
              typename http_connection<StreamType>::private_{},
              std::move(socket), ps);
          ps->register_connection(new_connection);
          new_connection->start();
          if (!ps->service.expired()) {
            auto service = ps->service.lock();
            ps->start_accept(service->get_io_service());
          }
        } else {
          LOG(WARNING) << "async_accept error or canceled m=" << error.message()
                       << "\n";
        }
      });
}
}  // namespace asyik

#endif
