#ifndef LIBASYIK_TEST_TLS_UTIL_HPP
#define LIBASYIK_TEST_TLS_UTIL_HPP

// Helpers for the throwaway PKI in tests/certs (see tests/certs/generate.sh).

#include <boost/asio/ssl.hpp>
#include <functional>
#include <memory>
#include <string>

#include "libasyik/service.hpp"
#include "libasyik/tls.hpp"

namespace asyik_test {

inline std::string cert_path(const std::string& name)
{
  return std::string(LIBASYIK_TEST_CERT_DIR) + "/" + name;
}

// Server context presenting <name>.crt / <name>.key. Pinned to TLS 1.2+.
inline boost::asio::ssl::context make_test_server_ssl_context(
    const std::string& name = "server")
{
  namespace ssl = boost::asio::ssl;
  ssl::context ctx{ssl::context::tls_server};
  ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                  ssl::context::no_sslv3 | ssl::context::no_tlsv1 |
                  ssl::context::no_tlsv1_1);
  ctx.use_certificate_chain_file(cert_path(name + ".crt"));
  ctx.use_private_key_file(cert_path(name + ".key"), ssl::context::pem);
  return ctx;
}

// Client context that trusts only the test root CA.
inline asyik::tls::client_config test_client_config()
{
  asyik::tls::client_config cfg;
  cfg.use_system_ca = false;
  cfg.ca_file = cert_path("ca.crt");
  return cfg;
}

inline asyik::tls::client_context_ptr test_client_context()
{
  static auto ctx = asyik::tls::make_client_context(test_client_config());
  return ctx;
}

// Groups servers so run_client() closes all of them.
template <typename... Servers>
auto all_servers(Servers... servers)
{
  struct closer {
    std::function<void()> close_all;
    void close() { close_all(); }
  };
  return std::make_shared<closer>(closer{[=]() { (servers->close(), ...); }});
}

// Run f in a fiber on as, then close the server(s) and stop the service.
// This also happens when f fails a REQUIRE, so a failure cannot leave the
// service running (and the test hanging).
template <typename Server, typename F>
void run_client(asyik::service_ptr as, Server server, F f)
{
  as->execute([as, server, f]() {
    try {
      f();
    } catch (...) {
      server->close();
      as->stop();
      throw;
    }
    server->close();
    as->stop();
  });
  as->run();
}

}  // namespace asyik_test

#endif
