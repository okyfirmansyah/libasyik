#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <fstream>
#include <iterator>

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
using asyik_test::test_client_config;
using asyik_test::test_client_context;

namespace {

std::string peer_subject(SSL* ssl)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  X509* cert = SSL_get1_peer_certificate(ssl);
#else
  X509* cert = SSL_get_peer_certificate(ssl);
#endif
  if (!cert) return "-";
  char buf[256];
  X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf));
  X509_free(cert);
  return buf;
}

// HTTPS server presenting tests/certs/<name>.crt. GET /info answers
// "<protocol>|<sni or ->|<client certificate subject or ->".
http_server_ptr<https_stream_type> make_info_server(
    service_ptr as, uint16_t port, boost::asio::ssl::context&& ctx)
{
  auto server = make_https_server(as, std::move(ctx), "127.0.0.1", port);
  auto* srv = server.get();
  server->on_http_request(
      "/info", "GET", [srv](http_request_ptr req, const http_route_args&) {
        SSL* ssl =
            req->get_connection_handle(srv)->get_stream().native_handle();
        const char* sni = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        req->response.body = std::string(SSL_get_version(ssl)) + "|" +
                             (sni ? sni : "-") + "|" + peer_subject(ssl);
        req->response.result(200);
      });
  server->on_websocket("/ws", [](websocket_ptr ws, const http_route_args&) {
    auto s = ws->get_string();
    ws->send_string(s);
    ws->close(websocket_close_code::normal, "closed normally");
  });
  return server;
}

http_server_ptr<https_stream_type> make_info_server(
    service_ptr as, uint16_t port, const std::string& cert = "server")
{
  return make_info_server(as, port, make_test_server_ssl_context(cert));
}

std::string get_info(service_ptr as, const tls::client_context_ptr& ctx,
                     const std::string& url)
{
  auto req = http_easy_request(as, ctx, 5000, "GET", url, "", {});
  REQUIRE(req->response.result() == 200);
  return std::string(req->response.body);
}

// Runs f, which must throw tls_verify_error; returns its verify result.
template <typename F>
long expect_verify_error(F&& f)
{
  try {
    f();
  } catch (tls_verify_error& e) {
    LOG(INFO) << "expected verify error: " << e.what() << "\n";
    return e.verify_result();
  }
  FAIL("expected asyik::tls_verify_error");
  return 0;
}

}  // namespace

TEST_CASE("TLS client context configuration", "[tls]")
{
  SECTION("default context is cached and used by services")
  {
    auto a = tls::default_client_context();
    REQUIRE(a);
    REQUIRE(a == tls::default_client_context());
    REQUIRE(a->config().verify_peer);
    REQUIRE(a->config().verify_hostname);

    auto as = make_service();
    REQUIRE(as->get_tls_client_context() == a);
    as->set_tls_client_context(test_client_context());
    REQUIRE(as->get_tls_client_context() == test_client_context());
  }

  SECTION("SNI and expected host name are derived from the URL host")
  {
    // Checked on the SSL object directly, so no name resolution is involved
    // (resolvers differ on names like "localhost."). The expected name is
    // checked by verifying server.crt (SAN: localhost, 127.0.0.1, ::1) with
    // the SSL object's verification parameters.
    BIO* bio = BIO_new_file(cert_path("server.crt").c_str(), "r");
    REQUIRE(bio);
    X509* leaf = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    REQUIRE(leaf);

    // Returns "<sni or ->|<X509_V_* result of verifying server.crt>".
    auto probe = [leaf](const tls::client_context_ptr& ctx, const char* host) {
      SSL_CTX* ssl_ctx = ctx->native().native_handle();
      SSL* ssl = SSL_new(ssl_ctx);
      internal::tls::prepare_client(ssl, *ctx, host);
      const char* sni = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
      std::string result = std::string(sni ? sni : "-") + "|";

      X509_STORE_CTX* store_ctx = X509_STORE_CTX_new();
      X509_STORE_CTX_init(store_ctx, SSL_CTX_get_cert_store(ssl_ctx), leaf,
                          nullptr);
      X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx),
                             SSL_get0_param(ssl));
      X509_verify_cert(store_ctx);
      result += std::to_string(X509_STORE_CTX_get_error(store_ctx));
      X509_STORE_CTX_free(store_ctx);
      SSL_free(ssl);
      return result;
    };
    const auto ok = std::to_string(X509_V_OK);
    const auto name_mismatch = std::to_string(X509_V_ERR_HOSTNAME_MISMATCH);
    const auto ip_mismatch = std::to_string(X509_V_ERR_IP_ADDRESS_MISMATCH);

    auto ctx = test_client_context();
    REQUIRE(probe(ctx, "localhost") == "localhost|" + ok);
    // A trailing dot (fully qualified name) is stripped.
    REQUIRE(probe(ctx, "localhost.") == "localhost|" + ok);
    REQUIRE(probe(ctx, "example.com") == "example.com|" + name_mismatch);
    // IP literals: no SNI, matched against the IP SANs.
    REQUIRE(probe(ctx, "127.0.0.1") == "-|" + ok);
    REQUIRE(probe(ctx, "[::1]") == "-|" + ok);
    REQUIRE(probe(ctx, "10.1.2.3") == "-|" + ip_mismatch);

    // verify_hostname = false: SNI is still sent, any name is accepted.
    auto cfg = test_client_config();
    cfg.verify_hostname = false;
    REQUIRE(probe(tls::make_client_context(cfg), "example.com") ==
            "example.com|" + ok);

    X509_free(leaf);
  }

  SECTION("insecure() disables all verification")
  {
    auto cfg = tls::client_config::insecure();
    REQUIRE(!cfg.verify_peer);
    REQUIRE(!cfg.verify_hostname);
    REQUIRE(tls::make_client_context(cfg));
  }

  SECTION("invalid settings are rejected with invalid_input_error")
  {
    auto cfg = test_client_config();
    cfg.ca_file = cert_path("does-not-exist.crt");
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);

    cfg = test_client_config();
    cfg.cert_file = cert_path("client.crt");  // no key
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);

    cfg = test_client_config();
    cfg.cert_file = cert_path("client.crt");
    cfg.key_file = cert_path("server.key");  // key of another certificate
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);

    cfg = test_client_config();
    cfg.cert_file = cert_path("client.crt");
    cfg.key_file = cert_path("client_encrypted.key");
    cfg.key_password = "wrong password";
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);

    // Encrypted key without a password must fail, not prompt on the terminal.
    cfg.key_password.clear();
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);

    cfg = test_client_config();
    cfg.cipher_list = "NOT-A-CIPHER";
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);

    cfg = test_client_config();
    cfg.ciphersuites = "NOT_A_SUITE";
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);

    cfg = test_client_config();
    cfg.min_version = tls::version::tls1_3;
    cfg.max_version = tls::version::tls1_2;
    REQUIRE_THROWS_AS(tls::make_client_context(cfg), invalid_input_error);
  }

  SECTION("encrypted client key with the right password")
  {
    auto cfg = test_client_config();
    cfg.cert_file = cert_path("client.crt");
    cfg.key_file = cert_path("client_encrypted.key");
    cfg.key_password = "asyik-test";
    REQUIRE(tls::make_client_context(cfg));
  }
}

TEST_CASE("TLS client accepts a trusted server", "[tls][https]")
{
  auto as = make_service();
  auto server = make_info_server(as, 4101);

  run_client(as, server, [as]() {
    // IP literal: matched against the IP SAN, no SNI sent.
    auto info =
        get_info(as, test_client_context(), "https://127.0.0.1:4101/info");
    REQUIRE(info == "TLSv1.3|-|-");

    // DNS name: matched against the DNS SAN, sent as SNI.
    info = get_info(as, test_client_context(), "https://localhost:4101/info");
    REQUIRE(info == "TLSv1.3|localhost|-");

    // CA given as in-memory PEM instead of a file.
    std::ifstream f(cert_path("ca.crt"));
    std::string pem((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    auto cfg = tls::client_config{};
    cfg.use_system_ca = false;
    cfg.ca_pem = pem;
    info = get_info(as, tls::make_client_context(cfg),
                    "https://localhost:4101/info");
    REQUIRE(info == "TLSv1.3|localhost|-");

    // The service-level context is used when the call passes none.
    as->set_tls_client_context(test_client_context());
    auto req = http_easy_request(as, "GET", "https://localhost:4101/info");
    REQUIRE(req->response.result() == 200);
  });
}

TEST_CASE("TLS client rejects untrusted or mismatching certificates",
          "[tls][https]")
{
  auto as = make_service();
  auto good = make_info_server(as, 4102);
  auto untrusted = make_info_server(as, 4103, "untrusted");
  auto expired = make_info_server(as, 4104, "expired");
  auto wronghost = make_info_server(as, 4105, "wronghost");

  run_client(as, all_servers(good, untrusted, expired, wronghost), [=]() {
    // Verification is on by default: the system store does not know the
    // test CA.
    REQUIRE(expect_verify_error([&] {
              http_easy_request(as, "GET", "https://localhost:4102/info");
            }) == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);

    auto ctx = test_client_context();
    REQUIRE(expect_verify_error([&] {
              get_info(as, ctx, "https://localhost:4103/info");
            }) == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);

    REQUIRE(expect_verify_error([&] {
              get_info(as, ctx, "https://localhost:4104/info");
            }) == X509_V_ERR_CERT_HAS_EXPIRED);

    REQUIRE(expect_verify_error([&] {
              get_info(as, ctx, "https://localhost:4105/info");
            }) == X509_V_ERR_HOSTNAME_MISMATCH);

    REQUIRE(expect_verify_error([&] {
              get_info(as, ctx, "https://127.0.0.1:4105/info");
            }) == X509_V_ERR_IP_ADDRESS_MISMATCH);

    // The error hierarchy lets callers catch at any level.
    REQUIRE_THROWS_AS(get_info(as, ctx, "https://localhost:4103/info"),
                      tls_handshake_error);
    REQUIRE_THROWS_AS(get_info(as, ctx, "https://localhost:4103/info"),
                      tls_error);
    REQUIRE_THROWS_AS(get_info(as, ctx, "https://localhost:4103/info"),
                      network_error);

    // verify_hostname = false still checks the chain, not the name.
    auto cfg = test_client_config();
    cfg.verify_hostname = false;
    auto no_host = tls::make_client_context(cfg);
    REQUIRE(get_info(as, no_host, "https://localhost:4105/info").size());
    REQUIRE(expect_verify_error([&] {
              get_info(as, no_host, "https://localhost:4103/info");
            }) == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);

    // insecure() connects to anything.
    auto insecure = tls::make_client_context(tls::client_config::insecure());
    REQUIRE(get_info(as, insecure, "https://localhost:4103/info").size());
    REQUIRE(get_info(as, insecure, "https://localhost:4104/info").size());

    // A verify_callback can veto a chain OpenSSL accepted.
    cfg = test_client_config();
    cfg.verify_callback = [](bool, boost::asio::ssl::verify_context&) {
      return false;
    };
    REQUIRE_THROWS_AS(get_info(as, tls::make_client_context(cfg),
                               "https://localhost:4102/info"),
                      tls_verify_error);
  });
}

TEST_CASE("TLS client protocol version limits", "[tls][https]")
{
  auto as = make_service();
  auto server = make_info_server(as, 4106);

  auto tls13_ctx = make_test_server_ssl_context();
  SSL_CTX_set_min_proto_version(tls13_ctx.native_handle(), TLS1_3_VERSION);
  auto tls13_only = make_info_server(as, 4107, std::move(tls13_ctx));

  run_client(as, all_servers(server, tls13_only), [=]() {
    auto cfg = test_client_config();
    cfg.max_version = tls::version::tls1_2;
    auto tls12 = tls::make_client_context(cfg);
    REQUIRE(get_info(as, tls12, "https://localhost:4106/info") ==
            "TLSv1.2|localhost|-");

    cfg = test_client_config();
    cfg.cipher_list = "ECDHE-ECDSA-AES128-GCM-SHA256";
    cfg.max_version = tls::version::tls1_2;
    REQUIRE(get_info(as, tls::make_client_context(cfg),
                     "https://localhost:4106/info") == "TLSv1.2|localhost|-");

    // A handshake failure that is not about the certificate.
    REQUIRE_THROWS_AS(get_info(as, tls12, "https://localhost:4107/info"),
                      tls_handshake_error);
    try {
      get_info(as, tls12, "https://localhost:4107/info");
    } catch (tls_verify_error&) {
      FAIL("version mismatch must not be reported as a verify error");
    } catch (tls_handshake_error& e) {
      LOG(INFO) << "expected handshake error: " << e.what() << "\n";
    }

    cfg = test_client_config();
    cfg.min_version = tls::version::tls1_3;
    REQUIRE(get_info(as, tls::make_client_context(cfg),
                     "https://localhost:4107/info") == "TLSv1.3|localhost|-");
  });
}

TEST_CASE("TLS client certificate (mutual TLS)", "[tls][https]")
{
  namespace ssl = boost::asio::ssl;
  auto as = make_service();

  auto ctx = make_test_server_ssl_context();
  ctx.set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
  ctx.load_verify_file(cert_path("ca.crt"));
  auto server = make_info_server(as, 4108, std::move(ctx));

  run_client(as, server, [=]() {
    auto cfg = test_client_config();
    cfg.cert_file = cert_path("client.crt");
    cfg.key_file = cert_path("client.key");
    REQUIRE(get_info(as, tls::make_client_context(cfg),
                     "https://localhost:4108/info") ==
            "TLSv1.3|localhost|/CN=libasyik test client");

    // In-memory PEM and an encrypted key.
    auto read = [](const std::string& name) {
      std::ifstream f(cert_path(name));
      return std::string((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    };
    cfg = test_client_config();
    cfg.cert_pem = read("client.crt");
    cfg.key_pem = read("client_encrypted.key");
    cfg.key_password = "asyik-test";
    REQUIRE(get_info(as, tls::make_client_context(cfg),
                     "https://localhost:4108/info") ==
            "TLSv1.3|localhost|/CN=libasyik test client");

    // No client certificate. With TLS 1.2 the handshake itself fails.
    cfg = test_client_config();
    cfg.max_version = tls::version::tls1_2;
    REQUIRE_THROWS_AS(get_info(as, tls::make_client_context(cfg),
                               "https://localhost:4108/info"),
                      tls_handshake_error);

    // With TLS 1.3 the server rejects the client after the handshake, so the
    // failure shows up while reading the response.
    REQUIRE_THROWS(
        get_info(as, test_client_context(), "https://localhost:4108/info"));
  });
}

TEST_CASE("TLS verification for wss:// connections", "[tls][websocket]")
{
  auto as = make_service();
  auto server = make_info_server(as, 4109);
  auto untrusted = make_info_server(as, 4110, "untrusted");

  run_client(as, all_servers(server, untrusted), [=]() {
    auto ws = make_websocket_connection(as, test_client_context(),
                                        "wss://localhost:4109/ws");
    ws->send_string("halo");
    REQUIRE(ws->get_string() == "halo");
    ws->close(websocket_close_code::normal, "done");

    // Service-level context.
    as->set_tls_client_context(test_client_context());
    ws = make_websocket_connection(as, "wss://localhost:4109/ws");
    ws->send_string("again");
    REQUIRE(ws->get_string() == "again");
    ws->close(websocket_close_code::normal, "done");

    // The default context (system CAs) does not trust the test CA.
    REQUIRE(expect_verify_error([&] {
              make_websocket_connection(as, tls::default_client_context(),
                                        "wss://localhost:4109/ws");
            }) == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);

    REQUIRE(expect_verify_error([&] {
              make_websocket_connection(as, test_client_context(),
                                        "wss://localhost:4110/ws");
            }) == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);
  });
}

TEST_CASE("Process-wide default TLS client context can be replaced", "[tls]")
{
  auto as = make_service();
  auto server = make_info_server(as, 4111);

  run_client(as, server, [=]() {
    auto builtin = tls::default_client_context();
    tls::set_default_client_context(test_client_context());
    try {
      auto req = http_easy_request(as, "GET", "https://localhost:4111/info");
      REQUIRE(req->response.result() == 200);
    } catch (...) {
      tls::set_default_client_context(nullptr);
      throw;
    }

    tls::set_default_client_context(nullptr);
    auto rebuilt = tls::default_client_context();
    REQUIRE(rebuilt != test_client_context());
    REQUIRE(rebuilt->config().verify_peer);
  });
}

// Needs internet access; skip with "~[external]".
TEST_CASE("TLS against public endpoints", "[tls][external]")
{
  auto as = make_service();
  as->execute([as]() {
    try {
      auto req =
          http_easy_request(as, "GET", "https://tls-v1-2.badssl.com:1012/");
      REQUIRE(req->response.result() == 200);
      REQUIRE(req->response.body.length());

      req = http_easy_request(as, "GET", "https://sha256.badssl.com/");
      REQUIRE(req->response.result() == 200);

      REQUIRE(expect_verify_error([&] {
                http_easy_request(as, "GET", "https://expired.badssl.com/");
              }) == X509_V_ERR_CERT_HAS_EXPIRED);
      REQUIRE(expect_verify_error([&] {
                http_easy_request(as, "GET", "https://wrong.host.badssl.com/");
              }) == X509_V_ERR_HOSTNAME_MISMATCH);
      REQUIRE(expect_verify_error([&] {
                http_easy_request(as, "GET", "https://self-signed.badssl.com/");
              }) != X509_V_OK);
      REQUIRE(expect_verify_error([&] {
                http_easy_request(as, "GET",
                                  "https://untrusted-root.badssl.com/");
              }) != X509_V_OK);
    } catch (...) {
      as->stop();
      throw;
    }
    as->stop();
  });
  as->run();
}
