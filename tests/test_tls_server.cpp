#include <openssl/ssl.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"
#include "libasyik/tls.hpp"
#include "test_tls_util.hpp"

using namespace asyik;
using asyik_test::all_servers;
using asyik_test::cert_path;
using asyik_test::make_test_server_ssl_context;
using asyik_test::run_client;
using asyik_test::test_client_context;

namespace {

namespace ssl = boost::asio::ssl;
namespace bhttp = boost::beast::http;
using tls_stream = boost::beast::ssl_stream<boost::beast::tcp_stream>;
using clock_type = std::chrono::steady_clock;
using std::chrono::milliseconds;

double seconds_since(clock_type::time_point t0)
{
  return std::chrono::duration<double>(clock_type::now() - t0).count();
}

std::string read_file(const std::string& name)
{
  std::ifstream f(cert_path(name));
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

tls::server_config test_server_config(const std::string& name = "server")
{
  tls::server_config cfg;
  cfg.cert_file = cert_path(name + ".crt");
  cfg.key_file = cert_path(name + ".key");
  return cfg;
}

// GET /info answers "<protocol>|<ALPN or ->|<SNI or ->".
template <typename Server>
void add_info_route(Server server)
{
  auto* srv = server.get();
  server->on_http_request(
      "/info", "GET", [srv](http_request_ptr req, const http_route_args&) {
        SSL* ssl =
            req->get_connection_handle(srv)->get_stream().native_handle();
        const unsigned char* alpn = nullptr;
        unsigned int alpn_len = 0;
        SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
        const char* sni = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        req->response.body =
            std::string(SSL_get_version(ssl)) + "|" +
            (alpn_len
                 ? std::string(reinterpret_cast<const char*>(alpn), alpn_len)
                 : std::string("-")) +
            "|" + (sni ? sni : "-");
        req->response.result(200);
      });
}

// --- raw clients, to do what the libasyik client cannot ---------------------

std::unique_ptr<ssl::context> raw_client_ctx(int min_version, int max_version)
{
  auto ctx = std::make_unique<ssl::context>(ssl::context::tls_client);
  ctx->set_verify_mode(ssl::verify_none);
  // Allow the client to offer anything; the server must do the refusing.
  SSL_CTX_set_security_level(ctx->native_handle(), 0);
  SSL_CTX_set_min_proto_version(ctx->native_handle(), min_version);
  SSL_CTX_set_max_proto_version(ctx->native_handle(), max_version);
  return ctx;
}

std::unique_ptr<boost::beast::tcp_stream> raw_tcp_connect(service_ptr as,
                                                          uint16_t port)
{
  auto s = std::make_unique<boost::beast::tcp_stream>(
      asio::make_strand(as->get_io_service()));
  s->expires_after(std::chrono::seconds(5));
  s->async_connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port),
                   use_fiber_future)
      .get();
  return s;
}

std::unique_ptr<tls_stream> raw_tls_connect(
    service_ptr as, ssl::context& ctx, uint16_t port,
    const std::function<void(SSL*)>& prepare = nullptr)
{
  auto s = std::make_unique<tls_stream>(asio::make_strand(as->get_io_service()),
                                        ctx);
  auto& tcp_layer = boost::beast::get_lowest_layer(*s);
  tcp_layer.expires_after(std::chrono::seconds(5));
  tcp_layer
      .async_connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port),
                     use_fiber_future)
      .get();
  if (prepare) prepare(s->native_handle());
  s->async_handshake(ssl::stream_base::client, use_fiber_future).get();
  return s;
}

void set_client_alpn(SSL* ssl, const std::vector<std::string>& protos)
{
  std::string wire;
  for (const auto& p : protos) {
    wire.push_back(static_cast<char>(p.size()));
    wire += p;
  }
  // Returns 0 on success.
  REQUIRE(SSL_set_alpn_protos(
              ssl, reinterpret_cast<const unsigned char*>(wire.data()),
              static_cast<unsigned int>(wire.size())) == 0);
}

std::string selected_alpn(SSL* ssl)
{
  const unsigned char* alpn = nullptr;
  unsigned int len = 0;
  SSL_get0_alpn_selected(ssl, &alpn, &len);
  return len ? std::string(reinterpret_cast<const char*>(alpn), len) : "-";
}

template <typename Stream>
http_beast_response raw_get(Stream& s, bool keep_alive)
{
  boost::beast::get_lowest_layer(s).expires_after(std::chrono::seconds(5));
  http_beast_request req{bhttp::verb::get, "/info", 11};
  req.set(bhttp::field::host, "localhost");
  req.keep_alive(keep_alive);
  bhttp::async_write(s, req, use_fiber_future).get();
  boost::beast::flat_buffer buffer;
  http_beast_response res;
  bhttp::async_read(s, buffer, res, use_fiber_future).get();
  return res;
}

// Read until the server closes the TCP connection. Returns the seconds it
// took, or -1 if it was still open after limit.
double wait_for_close(boost::beast::tcp_stream& s, milliseconds limit)
{
  auto t0 = clock_type::now();
  s.expires_after(limit);
  char buf[4096];
  try {
    for (;;) s.async_read_some(asio::buffer(buf), use_fiber_future).get();
  } catch (network_timeout_error&) {
    return -1;
  } catch (boost::system::system_error&) {
    // EOF or connection reset
    return seconds_since(t0);
  }
}

}  // namespace

TEST_CASE("TLS server_config validation and defaults", "[tls][tls_server]")
{
  SECTION("defaults")
  {
    tls::server_config cfg;
    REQUIRE(cfg.min_version == tls::version::tls1_2);
    REQUIRE(cfg.max_version == tls::version::tls1_3);
    REQUIRE(cfg.cipher_list == tls::mozilla_intermediate_ciphers);
    REQUIRE(cfg.alpn == std::vector<std::string>{"http/1.1"});
    REQUIRE(cfg.session_tickets);

    auto ctx = tls::make_server_context(test_server_config());
    SSL_CTX* h = ctx->native().native_handle();
    auto options = SSL_CTX_get_options(h);
    REQUIRE((options & SSL_OP_NO_COMPRESSION));
    REQUIRE((options & SSL_OP_NO_RENEGOTIATION));
    REQUIRE(!(options & SSL_OP_NO_TICKET));
    REQUIRE(SSL_CTX_get_min_proto_version(h) == TLS1_2_VERSION);
    REQUIRE(SSL_CTX_get_max_proto_version(h) == TLS1_3_VERSION);

    auto no_tickets = test_server_config();
    no_tickets.session_tickets = false;
    REQUIRE(
        (SSL_CTX_get_options(
             tls::make_server_context(no_tickets)->native().native_handle()) &
         SSL_OP_NO_TICKET));
  }

  SECTION("certificate and key sources")
  {
    // In-memory PEM
    tls::server_config cfg;
    cfg.cert_pem = read_file("server.crt");
    cfg.key_pem = read_file("server.key");
    REQUIRE(tls::make_server_context(cfg));

    // Encrypted key
    cfg = test_server_config("client");
    cfg.key_file = cert_path("client_encrypted.key");
    cfg.key_password = "asyik-test";
    REQUIRE(tls::make_server_context(cfg));

    // No ALPN at all is allowed
    cfg = test_server_config();
    cfg.alpn.clear();
    REQUIRE(tls::make_server_context(cfg));
  }

  SECTION("invalid configurations")
  {
    REQUIRE_THROWS_AS(tls::make_server_context(tls::server_config{}),
                      invalid_input_error);

    auto cfg = test_server_config();
    cfg.key_file.clear();
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.cert_file.clear();
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.key_file = cert_path("client.key");  // key of another certificate
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.cert_file = cert_path("missing.crt");
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config("client");
    cfg.key_file = cert_path("client_encrypted.key");
    cfg.key_password = "wrong";
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config("client");
    // Encrypted key without a password must fail, not prompt on the
    // terminal.
    cfg.key_file = cert_path("client_encrypted.key");
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.cipher_list = "NOT-A-CIPHER";
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.ciphersuites = "NOT_A_SUITE";
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.min_version = tls::version::tls1_3;
    cfg.max_version = tls::version::tls1_2;
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.alpn = {"http/1.1", ""};
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    cfg = test_server_config();
    cfg.alpn = {std::string(256, 'x')};
    REQUIRE_THROWS_AS(tls::make_server_context(cfg), invalid_input_error);

    auto as = make_service();
    REQUIRE_THROWS_AS(
        make_https_server(as, tls::server_context_ptr{}, "127.0.0.1", 4120),
        invalid_input_error);
  }

  SECTION("timeout defaults and setters")
  {
    auto as = make_service();
    auto server =
        make_https_server(as, test_server_config(), "127.0.0.1", 4121);
    REQUIRE(server->get_tls_handshake_timeout() == milliseconds(10000));
    REQUIRE(server->get_tls_shutdown_timeout() == milliseconds(2000));
    server->set_tls_handshake_timeout(milliseconds(1234));
    server->set_tls_shutdown_timeout(milliseconds(0));
    REQUIRE(server->get_tls_handshake_timeout() == milliseconds(1234));
    REQUIRE(server->get_tls_shutdown_timeout() == milliseconds(0));
    server->close();
  }
}

TEST_CASE("HTTPS server built from server_config", "[tls][tls_server]")
{
  auto as = make_service();
  auto ctx = tls::make_server_context(test_server_config());

  // One context shared by two servers.
  auto server = make_https_server(as, ctx, "127.0.0.1", 4122);
  auto server2 = make_https_server(as, ctx, "127.0.0.1", 4123);
  add_info_route(server);
  add_info_route(server2);
  server->on_websocket("/ws", [](websocket_ptr ws, const http_route_args&) {
    ws->send_string(ws->get_string());
    ws->close(websocket_close_code::normal, "done");
  });

  // ...and one from a config directly.
  auto server3 = make_https_server(as, test_server_config(), "127.0.0.1", 4124);
  add_info_route(server3);

  run_client(as, all_servers(server, server2, server3), [=]() {
    auto c = test_client_context();
    for (auto port : {4122, 4123, 4124}) {
      auto req = http_easy_request(
          as, c, 5000, "GET",
          "https://localhost:" + std::to_string(port) + "/info", "", {});
      REQUIRE(req->response.result() == 200);
      // The libasyik client does not offer ALPN.
      REQUIRE(req->response.body == "TLSv1.3|-|localhost");
    }

    auto ws = make_websocket_connection(as, c, "wss://localhost:4122/ws");
    ws->send_string("echo me");
    REQUIRE(ws->get_string() == "echo me");
  });
}

TEST_CASE("HTTPS server protocol and cipher policy", "[tls][tls_server]")
{
  auto as = make_service();
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4125);
  add_info_route(server);

  auto cfg = test_server_config();
  cfg.min_version = tls::version::tls1_3;
  auto tls13_only = make_https_server(as, cfg, "127.0.0.1", 4126);

  cfg = test_server_config();
  cfg.max_version = tls::version::tls1_2;
  auto tls12_only = make_https_server(as, cfg, "127.0.0.1", 4127);
  add_info_route(tls12_only);

  cfg = test_server_config();
  cfg.cipher_list = "ECDHE-ECDSA-AES128-SHA";
  auto legacy_cipher = make_https_server(as, cfg, "127.0.0.1", 4128);

  cfg = test_server_config();
  cfg.ciphersuites = "TLS_CHACHA20_POLY1305_SHA256";
  auto chacha = make_https_server(as, cfg, "127.0.0.1", 4129);

  run_client(
      as, all_servers(server, tls13_only, tls12_only, legacy_cipher, chacha),
      [=]() {
        // TLS 1.0 / 1.1 are refused even when the client offers them.
        auto old = raw_client_ctx(TLS1_VERSION, TLS1_1_VERSION);
        REQUIRE_THROWS_AS(raw_tls_connect(as, *old, 4125), tls_error);

        auto tls12 = raw_client_ctx(TLS1_2_VERSION, TLS1_2_VERSION);
        auto s = raw_tls_connect(as, *tls12, 4125);
        REQUIRE(std::string(SSL_get_version(s->native_handle())) == "TLSv1.2");
        // Mozilla intermediate: forward secrecy and AEAD.
        std::string cipher = SSL_get_cipher_name(s->native_handle());
        INFO(cipher);
        REQUIRE(cipher.rfind("ECDHE-", 0) == 0);
        REQUIRE((cipher.find("GCM") != std::string::npos ||
                 cipher.find("CHACHA20") != std::string::npos));

        // CBC/SHA1 suites are not in the default list...
        auto cbc = raw_client_ctx(TLS1_2_VERSION, TLS1_2_VERSION);
        SSL_CTX_set_cipher_list(cbc->native_handle(), "ECDHE-ECDSA-AES128-SHA");
        REQUIRE_THROWS_AS(raw_tls_connect(as, *cbc, 4125), tls_error);
        // ...but can be enabled with cipher_list.
        s = raw_tls_connect(as, *cbc, 4128);
        REQUIRE(std::string(SSL_get_cipher_name(s->native_handle())) ==
                "ECDHE-ECDSA-AES128-SHA");

        // TLS 1.3 suites
        auto tls13 = raw_client_ctx(TLS1_3_VERSION, TLS1_3_VERSION);
        s = raw_tls_connect(as, *tls13, 4129);
        REQUIRE(std::string(SSL_get_cipher_name(s->native_handle())) ==
                "TLS_CHACHA20_POLY1305_SHA256");

        // min/max_version
        REQUIRE_THROWS_AS(raw_tls_connect(as, *tls12, 4126), tls_error);
        REQUIRE_NOTHROW(raw_tls_connect(as, *tls13, 4126));
        REQUIRE_THROWS_AS(raw_tls_connect(as, *tls13, 4127), tls_error);
        auto req = http_easy_request(as, test_client_context(), 5000, "GET",
                                     "https://localhost:4127/info", "", {});
        REQUIRE(req->response.body == "TLSv1.2|-|localhost");
      });
}

TEST_CASE("HTTPS server ALPN negotiation", "[tls][tls_server]")
{
  auto as = make_service();
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4130);
  add_info_route(server);

  auto cfg = test_server_config();
  cfg.alpn.clear();
  auto no_alpn = make_https_server(as, cfg, "127.0.0.1", 4131);

  cfg = test_server_config();
  cfg.alpn = {"h2", "http/1.1"};
  auto h2_first = make_https_server(as, cfg, "127.0.0.1", 4132);

  run_client(as, all_servers(server, no_alpn, h2_first), [=]() {
    auto ctx = raw_client_ctx(TLS1_2_VERSION, TLS1_3_VERSION);
    auto alpn_with = [&](uint16_t port, std::vector<std::string> offer) {
      auto s = raw_tls_connect(as, *ctx, port, [&](SSL* ssl) {
        if (!offer.empty()) set_client_alpn(ssl, offer);
      });
      return selected_alpn(s->native_handle());
    };

    REQUIRE(alpn_with(4130, {"http/1.1"}) == "http/1.1");
    REQUIRE(alpn_with(4130, {"h2", "http/1.1"}) == "http/1.1");
    // No common protocol: the handshake still succeeds, without ALPN.
    REQUIRE(alpn_with(4130, {"h2"}) == "-");
    REQUIRE(alpn_with(4130, {}) == "-");
    // ALPN disabled on the server.
    REQUIRE(alpn_with(4131, {"http/1.1"}) == "-");
    // The server's preference order wins.
    REQUIRE(alpn_with(4132, {"http/1.1", "h2"}) == "h2");

    // The selected protocol is visible server-side, and HTTP still works.
    auto s = raw_tls_connect(as, *ctx, 4130, [&](SSL* ssl) {
      set_client_alpn(ssl, {"http/1.1"});
      SSL_set_tlsext_host_name(ssl, "localhost");
    });
    auto res = raw_get(*s, true);
    REQUIRE(res.body() == "TLSv1.3|http/1.1|localhost");
  });
}

TEST_CASE("HTTPS server session resumption", "[tls][tls_server]")
{
  auto as = make_service();
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4133);
  add_info_route(server);

  auto cfg = test_server_config();
  cfg.session_tickets = false;
  auto no_tickets = make_https_server(as, cfg, "127.0.0.1", 4134);
  add_info_route(no_tickets);

  run_client(as, all_servers(server, no_tickets), [=]() {
    // Connect twice; returns {resumed, first session had a ticket}.
    auto resume = [&](uint16_t port, int version) {
      auto ctx = raw_client_ctx(version, version);
      SSL_SESSION* session = nullptr;
      {
        auto s = raw_tls_connect(as, *ctx, port);
        // Reading a response also processes TLS 1.3 session tickets.
        REQUIRE(raw_get(*s, false).result_int() == 200);
        session = SSL_get1_session(s->native_handle());
        try {
          s->async_shutdown(use_fiber_future).get();
        } catch (...) {
        }
      }
      REQUIRE(session);
      bool had_ticket = SSL_SESSION_has_ticket(session);
      auto s = raw_tls_connect(
          as, *ctx, port, [&](SSL* ssl) { SSL_set_session(ssl, session); });
      SSL_SESSION_free(session);
      bool resumed = SSL_session_reused(s->native_handle());
      REQUIRE(raw_get(*s, false).result_int() == 200);
      return std::make_pair(resumed, had_ticket);
    };

    auto r = resume(4133, TLS1_2_VERSION);
    REQUIRE(r.first);
    REQUIRE(r.second);

    r = resume(4133, TLS1_3_VERSION);
    REQUIRE(r.first);

    // Without tickets, TLS 1.2 still resumes through the session-ID cache.
    r = resume(4134, TLS1_2_VERSION);
    REQUIRE(r.first);
    REQUIRE(!r.second);
  });
}

TEST_CASE("HTTPS server handshake timeout", "[tls][tls_server]")
{
  auto as = make_service();
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4135);
  add_info_route(server);
  server->set_tls_handshake_timeout(milliseconds(300));

  auto unlimited =
      make_https_server(as, test_server_config(), "127.0.0.1", 4136);
  unlimited->set_tls_handshake_timeout(milliseconds(0));

  // The hand-configured ssl::context overload honours it too.
  auto raw =
      make_https_server(as, make_test_server_ssl_context(), "127.0.0.1", 4137);
  raw->set_tls_handshake_timeout(milliseconds(300));

  run_client(as, all_servers(server, unlimited, raw), [=]() {
    // Connects and sends nothing.
    auto silent = raw_tcp_connect(as, 4135);
    double t = wait_for_close(*silent, milliseconds(5000));
    INFO("closed after " << t << "s");
    REQUIRE(t >= 0.2);
    REQUIRE(t < 3.0);

    silent = raw_tcp_connect(as, 4137);
    t = wait_for_close(*silent, milliseconds(5000));
    REQUIRE(t >= 0.2);
    REQUIRE(t < 3.0);

    // Sends the first bytes of a ClientHello, then stalls.
    auto partial = raw_tcp_connect(as, 4135);
    const char hello_start[] = {0x16, 0x03, 0x01, 0x02, 0x00, 0x01};
    partial
        ->async_write_some(asio::buffer(hello_start, sizeof(hello_start)),
                           use_fiber_future)
        .get();
    t = wait_for_close(*partial, milliseconds(5000));
    REQUIRE(t >= 0.2);
    REQUIRE(t < 3.0);

    // Many idle connections do not delay real clients.
    std::vector<std::unique_ptr<boost::beast::tcp_stream>> idle;
    for (int i = 0; i < 30; i++) idle.push_back(raw_tcp_connect(as, 4135));
    // 127.0.0.1, not localhost: on Windows a refused ::1 attempt adds ~2s.
    auto t0 = clock_type::now();
    auto req = http_easy_request(as, test_client_context(), 5000, "GET",
                                 "https://127.0.0.1:4135/info", "", {});
    REQUIRE(req->response.result() == 200);
    REQUIRE(seconds_since(t0) < 2.0);
    for (auto& c : idle) REQUIRE(wait_for_close(*c, milliseconds(5000)) >= 0);

    // The limit covers the handshake only: an established keep-alive
    // connection may stay idle for longer.
    auto ctx = raw_client_ctx(TLS1_2_VERSION, TLS1_3_VERSION);
    auto s = raw_tls_connect(as, *ctx, 4135);
    asyik::sleep_for(milliseconds(600));
    REQUIRE(raw_get(*s, true).result_int() == 200);
    asyik::sleep_for(milliseconds(600));
    REQUIRE(raw_get(*s, true).result_int() == 200);

    // Zero disables the limit.
    silent = raw_tcp_connect(as, 4136);
    REQUIRE(wait_for_close(*silent, milliseconds(800)) < 0);
  });
}

TEST_CASE("HTTPS server shutdown timeout", "[tls][tls_server]")
{
  auto as = make_service();
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4138);
  add_info_route(server);
  server->set_tls_shutdown_timeout(milliseconds(300));

  auto unlimited =
      make_https_server(as, test_server_config(), "127.0.0.1", 4139);
  add_info_route(unlimited);
  unlimited->set_tls_shutdown_timeout(milliseconds(0));

  run_client(as, all_servers(server, unlimited), [=]() {
    auto ctx = raw_client_ctx(TLS1_2_VERSION, TLS1_3_VERSION);

    // "Connection: close", then the client neither answers the server's
    // close_notify nor closes the socket.
    auto s = raw_tls_connect(as, *ctx, 4138);
    REQUIRE(raw_get(*s, false).result_int() == 200);
    double t =
        wait_for_close(boost::beast::get_lowest_layer(*s), milliseconds(5000));
    INFO("closed after " << t << "s");
    REQUIRE(t >= 0.15);
    REQUIRE(t < 3.0);

    // A well-behaved client completes the TLS shutdown.
    s = raw_tls_connect(as, *ctx, 4138);
    REQUIRE(raw_get(*s, false).result_int() == 200);
    try {
      s->async_shutdown(use_fiber_future).get();
    } catch (...) {
    }
    REQUIRE(wait_for_close(boost::beast::get_lowest_layer(*s),
                           milliseconds(5000)) >= 0);

    // A client that simply disconnects is fine too.
    s = raw_tls_connect(as, *ctx, 4138);
    REQUIRE(raw_get(*s, false).result_int() == 200);
    s.reset();

    // Zero waits for the client without limit.
    s = raw_tls_connect(as, *ctx, 4139);
    REQUIRE(raw_get(*s, false).result_int() == 200);
    REQUIRE(wait_for_close(boost::beast::get_lowest_layer(*s),
                           milliseconds(800)) < 0);
    s.reset();

    // The server is still healthy.
    auto req = http_easy_request(as, test_client_context(), 5000, "GET",
                                 "https://localhost:4138/info", "", {});
    REQUIRE(req->response.result() == 200);
  });
}

TEST_CASE("HTTPS server survives non-TLS and broken clients",
          "[tls][tls_server]")
{
  auto as = make_service();
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4140);
  add_info_route(server);

  run_client(as, server, [=]() {
    // Plain HTTP sent to the HTTPS port is rejected at once.
    auto c = raw_tcp_connect(as, 4140);
    std::string plain = "GET /info HTTP/1.1\r\nHost: localhost\r\n\r\n";
    c->async_write_some(asio::buffer(plain), use_fiber_future).get();
    double t = wait_for_close(*c, milliseconds(5000));
    REQUIRE(t >= 0);
    REQUIRE(t < 1.0);

    // Random bytes
    c = raw_tcp_connect(as, 4140);
    std::string garbage(2048, '\0');
    for (size_t i = 0; i < garbage.size(); i++)
      garbage[i] = static_cast<char>((i * 7919 + 13) & 0xff);
    c->async_write_some(asio::buffer(garbage), use_fiber_future).get();
    t = wait_for_close(*c, milliseconds(5000));
    REQUIRE(t >= 0);
    REQUIRE(t < 1.0);

    // Connect-and-close (port scanners), gracefully and with a TCP reset.
    for (int i = 0; i < 50; i++) {
      c = raw_tcp_connect(as, 4140);
      if (i % 2) c->socket().set_option(asio::socket_base::linger(true, 0));
      c->socket().close();
    }

    // Client gives up halfway through the handshake.
    auto ctx = raw_client_ctx(TLS1_2_VERSION, TLS1_3_VERSION);
    for (int i = 0; i < 10; i++) {
      auto s = std::make_unique<tls_stream>(
          asio::make_strand(as->get_io_service()), *ctx);
      boost::beast::get_lowest_layer(*s)
          .async_connect(
              tcp::endpoint(asio::ip::make_address("127.0.0.1"), 4140),
              use_fiber_future)
          .get();
      auto f = s->async_handshake(ssl::stream_base::client, use_fiber_future);
      asyik::sleep_for(milliseconds(1));
      boost::beast::get_lowest_layer(*s).socket().close();
      try {
        f.get();
      } catch (...) {
      }
    }

    // Still serving.
    for (int i = 0; i < 5; i++) {
      auto req = http_easy_request(as, test_client_context(), 5000, "GET",
                                   "https://localhost:4140/info", "", {});
      REQUIRE(req->response.result() == 200);
    }
  });
}

TEST_CASE("HTTPS server close() with handshakes in progress",
          "[tls][tls_server]")
{
  auto as = make_service();
  // Default 10s handshake timeout: close() must not wait for it.
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4141);

  auto t0 = clock_type::now();
  std::vector<std::unique_ptr<boost::beast::tcp_stream>> idle;
  run_client(as, server, [&, as]() {
    for (int i = 0; i < 5; i++) idle.push_back(raw_tcp_connect(as, 4141));
    asyik::sleep_for(milliseconds(50));
  });
  REQUIRE(seconds_since(t0) < 3.0);
}

TEST_CASE("server close() with active websocket handlers",
          "[tls][tls_server][websocket]")
{
  auto check = [](auto server, service_ptr as, const std::string& url) {
    std::atomic<bool> handler_done{false};
    server->on_websocket(
        "/ws", [&handler_done](websocket_ptr ws, const http_route_args&) {
          try {
            for (;;) ws->send_string(ws->get_string());
          } catch (...) {
          }
          handler_done = true;
        });

    run_client(as, server, [&, as, server]() {
      auto ws = make_websocket_connection(as, test_client_context(), url);
      ws->send_string("ping");
      REQUIRE(ws->get_string() == "ping");

      // The handler is blocked reading; close() must end it.
      server->close();
      REQUIRE_THROWS(ws->get_string());
      for (int i = 0; i < 100 && !handler_done; i++)
        asyik::sleep_for(milliseconds(10));
      REQUIRE(handler_done);
    });
  };

  {
    auto as = make_service();
    check(make_http_server(as, "127.0.0.1", 4142), as,
          "ws://localhost:4142/ws");
  }
  {
    auto as = make_service();
    check(make_https_server(as, test_server_config(), "127.0.0.1", 4143), as,
          "wss://localhost:4143/ws");
  }
}

TEST_CASE("TLS websocket keeps working after its server is gone",
          "[tls][tls_server][websocket]")
{
  auto as = make_service();
  auto server = make_https_server(as, test_server_config(), "127.0.0.1", 4144);

  // The handler hands the websocket over and returns.
  auto handed_over = std::make_shared<fibers::promise<websocket_ptr>>();
  server->on_websocket("/ws",
                       [handed_over](websocket_ptr ws, const http_route_args&) {
                         handed_over->set_value(ws);
                       });

  as->execute([as, server, handed_over]() mutable {
    try {
      auto client = make_websocket_connection(as, test_client_context(),
                                              "wss://localhost:4144/ws");
      auto server_side = handed_over->get_future().get();

      // Drop the server and with it its own reference to the TLS context.
      server->close();
      server.reset();
      asyik::sleep_for(milliseconds(50));

      server_side->send_string("still here");
      REQUIRE(client->get_string() == "still here");
      client->send_string("me too");
      REQUIRE(server_side->get_string() == "me too");
      client->close(websocket_close_code::normal, "done");
    } catch (...) {
      if (server) server->close();
      as->stop();
      throw;
    }
    as->stop();
  });
  server.reset();
  as->run();
}
