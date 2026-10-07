// HTTP client/server edge cases: handler failures, multipart responses split
// over several writes or malformed, WebSocket upgrade errors, invalid URLs.
// Uses ports 4310-4313.

#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"
#include "test_tls_util.hpp"

using namespace asyik;
using namespace asyik_test;

namespace {

// Registers path to answer with the raw bytes in pieces, written one after the
// other with a short pause so the client sees them in separate reads.
template <typename Server>
void raw_response_route(Server server, const std::string& path,
                        std::vector<std::string> pieces)
{
  server->on_http_request(
      path, "GET",
      [server, pieces](http_request_ptr req, const http_route_args&) {
        auto connection = req->get_connection_handle(server);
        auto& stream = connection->get_stream();
        req->activate_direct_response_handling();
        for (const auto& piece : pieces) {
          asio::async_write(stream, asio::buffer(piece), use_fiber_future)
              .get();
          asyik::sleep_for(std::chrono::milliseconds(20));
        }
      });
}

std::string part(const std::string& boundary, const std::string& body,
                 bool with_length = true)
{
  std::string s = "--" + boundary + "\r\nContent-Type: text/plain\r\n";
  if (with_length)
    s += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  return s + "\r\n" + body;
}

const std::string multipart_header =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/form-data; boundary=asyikB\r\n"
    "\r\n";

}  // namespace

TEST_CASE("HTTP handler exceptions are answered with 500", "[http]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4310);
  server->on_http_request("/throws",
                          [](http_request_ptr, const http_route_args&) {
                            throw std::runtime_error("handler failed");
                          });
  server->on_http_request("/ok",
                          [](http_request_ptr req, const http_route_args&) {
                            req->response.body = "fine";
                            req->response.result(200);
                          });
  // looks like a raw regex: accepted (escaped) with a warning
  REQUIRE_NOTHROW(server->on_http_request(
      "/raw/(a|b)", [](http_request_ptr, const http_route_args&) {}));

  run_client(as, server, [=]() {
    auto req = http_easy_request(as, 5000, "GET",
                                 "http://127.0.0.1:4310/throws", "", {});
    REQUIRE(req->response.result() == 500);
    REQUIRE(req->response.body.empty());

    // the server keeps serving after a failed handler
    req =
        http_easy_request(as, 5000, "GET", "http://127.0.0.1:4310/ok", "", {});
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "fine");
  });
}

TEST_CASE("multipart responses split over several writes", "[http]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4311);

  std::string p1 = part("asyikB", "first part");
  raw_response_route(server, "/parts",
                     {multipart_header, p1.substr(0, 10), p1.substr(10),
                      part("asyikB", "second"), "--asyikB--\r\n"});
  raw_response_route(
      server, "/no-boundary",
      {"HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace\r\n\r\n"});
  raw_response_route(server, "/no-length",
                     {multipart_header, part("asyikB", "x", false)});

  run_client(as, server, [=]() {
    std::vector<std::string> bodies;
    auto req = http_easy_request_multipart(
        as, "GET", "http://127.0.0.1:4311/parts", [&](http_request_ptr r) {
          bodies.push_back(r->multipart_response.body);
        });
    REQUIRE(req);
    REQUIRE(bodies == std::vector<std::string>{"first part", "second"});

    // plain http_easy_request cannot handle a multipart response
    REQUIRE_THROWS_AS(http_easy_request(as, 5000, "GET",
                                        "http://127.0.0.1:4311/parts", "", {}),
                      unexpected_error);

    auto ignore = [](http_request_ptr) {};
    REQUIRE_THROWS_AS(
        http_easy_request_multipart(
            as, "GET", "http://127.0.0.1:4311/no-boundary", ignore),
        unexpected_error);
    REQUIRE_THROWS_AS(http_easy_request_multipart(
                          as, "GET", "http://127.0.0.1:4311/no-length", ignore),
                      unexpected_error);
  });
}

TEST_CASE("WebSocket upgrade errors", "[http][websocket]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4312);
  server->on_websocket("/ws", [](websocket_ptr ws, const http_route_args&) {
    ws->send_string(ws->get_string());
  });

  run_client(as, server, [=]() {
    // no WebSocket route for the path: the upgrade is refused
    REQUIRE_THROWS(make_websocket_connection(as, "ws://127.0.0.1:4312/none"));

    // frames sent before the handshake response: the server drops the
    // connection instead of upgrading
    asio::ip::tcp::socket sock(as->get_io_service());
    sock.async_connect({asio::ip::make_address("127.0.0.1"), 4312},
                       use_fiber_future)
        .get();
    std::string upgrade =
        "GET /ws HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
        "too early";
    asio::async_write(sock, asio::buffer(upgrade), use_fiber_future).get();
    std::string received;
    try {
      while (true) {
        char buf[512];
        auto n =
            sock.async_read_some(asio::buffer(buf), use_fiber_future).get();
        received.append(buf, n);
      }
    } catch (boost::system::system_error&) {
      // connection closed by the server
    }
    REQUIRE(received.find("101") == std::string::npos);

    // the server still accepts well-behaved clients
    auto ws = make_websocket_connection(as, "ws://127.0.0.1:4312/ws");
    ws->send_string("hello");
    REQUIRE(ws->get_string() == "hello");
  });
}

TEST_CASE("Clients return nullptr for unparsable URLs", "[http]")
{
  auto as = make_service();
  as->execute([as]() {
    REQUIRE(http_easy_request(as, 5000, "GET", "not a url", "", {}) == nullptr);
    REQUIRE(make_websocket_connection(as, "not a url") == nullptr);
    as->stop();
  });
  as->run();
}

TEST_CASE("Pipelined requests are all answered", "[http]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4313);
  server->on_http_request("/n/<int>", [](http_request_ptr req,
                                         const http_route_args& args) {
    req->response.body = "#" + args[1];
    req->response.result(200);
  });

  run_client(as, server, [=]() {
    asio::ip::tcp::socket sock(as->get_io_service());
    sock.async_connect({asio::ip::make_address("127.0.0.1"), 4313},
                       use_fiber_future)
        .get();
    // three requests in one write: the server reads past the first one
    std::string requests =
        "GET /n/1 HTTP/1.1\r\nHost: x\r\n\r\n"
        "GET /n/2 HTTP/1.1\r\nHost: x\r\n\r\n"
        "GET /n/3 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    asio::async_write(sock, asio::buffer(requests), use_fiber_future).get();

    // read until the server closes; give up after 3s instead of hanging
    asio::steady_timer timeout(as->get_io_service(), std::chrono::seconds(3));
    bool timed_out = false;
    timeout.async_wait([&](boost::system::error_code ec) {
      if (!ec) {
        timed_out = true;
        sock.close();
      }
    });
    std::string received;
    try {
      while (true) {
        char buf[1024];
        received.append(
            buf,
            sock.async_read_some(asio::buffer(buf), use_fiber_future).get());
      }
    } catch (boost::system::system_error&) {
    }
    timeout.cancel();
    asyik::sleep_for(std::chrono::milliseconds(10));  // let the timer finish

    REQUIRE(!timed_out);
    auto first = received.find("#1"), second = received.find("#2"),
         third = received.find("#3");
    REQUIRE(first != std::string::npos);
    REQUIRE(second != std::string::npos);
    REQUIRE(third != std::string::npos);
    REQUIRE(first < second);
    REQUIRE(second < third);
  });
}
