#ifndef LIBASYIK_ASYIK_TLS_HPP
#define LIBASYIK_ASYIK_TLS_HPP

#include <boost/asio/ssl.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "asyik_fwd.hpp"
#include "common.hpp"
#include "error.hpp"
#include "internal/asio_internal.hpp"

namespace asyik {
namespace tls {

enum class version { tls1_2, tls1_3 };

// Settings for outgoing TLS connections (https:// and wss://).
//
// The defaults are secure: the server certificate chain is verified against
// the system trust store and must match the host name (or IP address) of the
// URL. Use client_config::insecure() only for testing.
struct client_config {
  // Verify the server certificate chain against the trusted CAs.
  bool verify_peer = true;
  // Require the certificate to match the URL host (DNS name or IP address).
  // Only meaningful when verify_peer is true.
  bool verify_hostname = true;

  // Trust the operating system's CA store (Windows: the ROOT certificate
  // store; elsewhere: OpenSSL's default paths, which honour SSL_CERT_FILE and
  // SSL_CERT_DIR).
  bool use_system_ca = true;
  // Additional trusted CAs, used together with the system store.
  std::string ca_file;  // PEM bundle
  std::string ca_path;  // OpenSSL hashed directory
  std::string ca_pem;   // PEM bundle held in memory

  // Client certificate, for servers that require mutual TLS. Use either the
  // *_file or the *_pem form. key_password decrypts an encrypted key.
  std::string cert_file;
  std::string cert_pem;
  std::string key_file;
  std::string key_pem;
  std::string key_password;

  version min_version = version::tls1_2;
  version max_version = version::tls1_3;

  // Empty means OpenSSL's defaults.
  std::string cipher_list;   // TLS 1.2, OpenSSL cipher string
  std::string ciphersuites;  // TLS 1.3, e.g. "TLS_AES_128_GCM_SHA256"

  // Optional extra check run for every certificate in the chain. preverified
  // already includes the chain and host name checks.
  std::function<bool(bool preverified, boost::asio::ssl::verify_context&)>
      verify_callback;

  // No certificate or host name verification. Traffic is encrypted but not
  // authenticated, so anyone on the network path can intercept it.
  static client_config insecure()
  {
    client_config cfg;
    cfg.verify_peer = false;
    cfg.verify_hostname = false;
    cfg.use_system_ca = false;
    return cfg;
  }
};

// A configured, immutable OpenSSL client context. Build it once and share it:
// it is safe to use from many services and threads at the same time.
class client_context {
 private:
  struct private_ {};

 public:
  client_context(struct private_&&, const client_config& cfg);
  client_context(const client_context&) = delete;
  client_context& operator=(const client_context&) = delete;

  const client_config& config() const { return cfg_; }
  boost::asio::ssl::context& native() { return ctx_; }

 private:
  client_config cfg_;
  boost::asio::ssl::context ctx_;

  friend client_context_ptr make_client_context(const client_config& cfg);
};

// Throws asyik::invalid_input_error when the configuration cannot be applied
// (unreadable CA/certificate/key, key not matching certificate, bad cipher
// string, ...).
client_context_ptr make_client_context(const client_config& cfg = {});

// The process-wide context used when neither the call nor the service
// specifies one. Created on first use with a default client_config.
client_context_ptr default_client_context();

// Replace the process-wide default. Pass nullptr to restore the built-in one.
void set_default_client_context(client_context_ptr ctx);

// Mozilla "intermediate" TLS 1.2 cipher list: forward secrecy and AEAD only.
// (TLS 1.3 suites are all strong and configured separately.)
inline constexpr const char* mozilla_intermediate_ciphers =
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
    "DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:"
    "DHE-RSA-CHACHA20-POLY1305";

// Settings for an HTTPS server (make_https_server()).
//
// The defaults follow Mozilla's "intermediate" profile: TLS 1.2 and 1.3,
// forward-secret AEAD ciphers only, no compression, no renegotiation.
struct server_config {
  // Certificate chain (leaf first, then intermediates) and its private key.
  // Use either the *_file or the *_pem form. key_password decrypts an
  // encrypted key.
  std::string cert_file;
  std::string cert_pem;
  std::string key_file;
  std::string key_pem;
  std::string key_password;

  version min_version = version::tls1_2;
  version max_version = version::tls1_3;

  std::string cipher_list = mozilla_intermediate_ciphers;  // TLS 1.2
  std::string ciphersuites;  // TLS 1.3; empty means OpenSSL's defaults

  // ALPN protocols offered to clients, most preferred first. A client that
  // offers none of them still connects, without ALPN. Empty disables ALPN.
  std::vector<std::string> alpn = {"http/1.1"};

  // Stateless session resumption (RFC 5077 tickets). Session-ID resumption
  // stays available either way.
  bool session_tickets = true;
};

// A configured, immutable OpenSSL server context. One context can be shared
// by several servers (e.g. one per thread with reuse_port).
class server_context {
 private:
  struct private_ {};

 public:
  server_context(struct private_&&, const server_config& cfg);
  server_context(const server_context&) = delete;
  server_context& operator=(const server_context&) = delete;

  const server_config& config() const { return cfg_; }
  boost::asio::ssl::context& native() { return ctx_; }

 private:
  server_config cfg_;
  std::string alpn_wire_;  // cfg_.alpn in ALPN wire format
  boost::asio::ssl::context ctx_;

  friend server_context_ptr make_server_context(const server_config& cfg);
};

// Throws asyik::invalid_input_error when the configuration cannot be applied
// (missing or unreadable certificate/key, key not matching certificate, wrong
// key password, bad cipher string, invalid ALPN name, ...).
server_context_ptr make_server_context(const server_config& cfg);

}  // namespace tls

namespace internal {
namespace tls {

// Set SNI and host name verification on a stream that has not handshaken yet.
// host is the URL host; IPv6 brackets are accepted.
void prepare_client(SSL* ssl, const asyik::tls::client_context& ctx,
                    string_view host);

// Must be called from a catch block around the client handshake. Rethrows
// TLS failures as tls_verify_error / tls_handshake_error; anything else (e.g.
// timeouts) is rethrown unchanged.
[[noreturn]] void rethrow_client_handshake_error(
    SSL* ssl, const asyik::tls::client_context& ctx, string_view host);

template <typename SslStream>
void client_handshake(SslStream& stream, const asyik::tls::client_context& ctx,
                      string_view host)
{
  prepare_client(stream.native_handle(), ctx, host);
  try {
    stream
        .async_handshake(boost::asio::ssl::stream_base::client,
                         use_fiber_future)
        .get();
  } catch (...) {
    rethrow_client_handshake_error(stream.native_handle(), ctx, host);
  }
}

}  // namespace tls
}  // namespace internal
}  // namespace asyik

#endif  // LIBASYIK_ASYIK_TLS_HPP
