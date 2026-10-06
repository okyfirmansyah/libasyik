#include "libasyik/tls.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <atomic>
#include <mutex>

#include "aixlog.hpp"

namespace ssl = boost::asio::ssl;

namespace asyik {

namespace internal {
namespace tls {
#ifdef _WIN32
// Defined in tls_win_certstore.cpp (kept apart because <wincrypt.h> clashes
// with OpenSSL's type names).
void load_windows_root_store(SSL_CTX* ctx);
#endif
}  // namespace tls
}  // namespace internal

namespace tls {
namespace {

int to_openssl(version v)
{
  return v == version::tls1_3 ? TLS1_3_VERSION : TLS1_2_VERSION;
}

// Run an Asio context call, turning its error into one that says which
// setting was wrong.
template <typename F>
void apply(const char* what, F&& f)
{
  try {
    f();
  } catch (boost::system::system_error& e) {
    throw invalid_input_error(
        e.code(),
        std::string("[asyik::tls] cannot load ") + what + ": " + e.what());
  }
}

void load_system_ca(ssl::context& ctx)
{
  // Also honours SSL_CERT_FILE / SSL_CERT_DIR on every platform.
  boost::system::error_code ec;
  ctx.set_default_verify_paths(ec);
  if (ec)
    LOG(WARNING) << "[asyik::tls] cannot load default CA paths: "
                 << ec.message() << "\n";
#ifdef _WIN32
  internal::tls::load_windows_root_store(ctx.native_handle());
#endif
}

void warn_insecure_once()
{
  static std::atomic<bool> warned{false};
  if (!warned.exchange(true))
    LOG(WARNING) << "[asyik::tls] a TLS client context with certificate "
                    "verification disabled is in use; connections can be "
                    "intercepted\n";
}

void set_versions(ssl::context& ctx, version min, version max)
{
  if (to_openssl(min) > to_openssl(max))
    throw invalid_input_error(
        "[asyik::tls] min_version is higher than max_version");
  SSL_CTX_set_min_proto_version(ctx.native_handle(), to_openssl(min));
  SSL_CTX_set_max_proto_version(ctx.native_handle(), to_openssl(max));
}

void set_ciphers(ssl::context& ctx, const std::string& cipher_list,
                 const std::string& ciphersuites)
{
  SSL_CTX* h = ctx.native_handle();
  if (!cipher_list.empty() &&
      !SSL_CTX_set_cipher_list(h, cipher_list.c_str())) {
    ERR_clear_error();
    throw invalid_input_error("[asyik::tls] invalid cipher_list: " +
                              cipher_list);
  }
  if (!ciphersuites.empty() &&
      !SSL_CTX_set_ciphersuites(h, ciphersuites.c_str())) {
    ERR_clear_error();
    throw invalid_input_error("[asyik::tls] invalid ciphersuites: " +
                              ciphersuites);
  }
}

// Load a certificate chain and its private key (file or in-memory PEM).
void load_identity(ssl::context& ctx, const std::string& cert_file,
                   const std::string& cert_pem, const std::string& key_file,
                   const std::string& key_pem, const std::string& key_password,
                   const char* who)
{
  // Always install a callback: OpenSSL's default one prompts on the terminal
  // and would block the whole process. An empty password makes loading an
  // encrypted key fail instead.
  ctx.set_password_callback(
      [pw = key_password](std::size_t, ssl::context::password_purpose) {
        return pw;
      });
  if (!cert_file.empty())
    apply("cert_file", [&] { ctx.use_certificate_chain_file(cert_file); });
  else
    apply("cert_pem", [&] {
      ctx.use_certificate_chain(
          boost::asio::buffer(cert_pem.data(), cert_pem.size()));
    });
  if (!key_file.empty())
    apply("key_file",
          [&] { ctx.use_private_key_file(key_file, ssl::context::pem); });
  else
    apply("key_pem", [&] {
      ctx.use_private_key(boost::asio::buffer(key_pem.data(), key_pem.size()),
                          ssl::context::pem);
    });
  if (SSL_CTX_check_private_key(ctx.native_handle()) != 1) {
    ERR_clear_error();
    throw invalid_input_error(std::string("[asyik::tls] ") + who +
                              " private key does not match the certificate");
  }
}

// ALPN server callback: pick the first of our protocols the client offers;
// if there is none, continue without ALPN rather than failing.
int select_alpn(SSL*, const unsigned char** out, unsigned char* outlen,
                const unsigned char* in, unsigned int inlen, void* arg)
{
  const auto* wire = static_cast<const std::string*>(arg);
  unsigned char* selected = nullptr;
  if (SSL_select_next_proto(
          &selected, outlen,
          reinterpret_cast<const unsigned char*>(wire->data()),
          static_cast<unsigned int>(wire->size()), in,
          inlen) != OPENSSL_NPN_NEGOTIATED)
    return SSL_TLSEXT_ERR_NOACK;
  *out = selected;
  return SSL_TLSEXT_ERR_OK;
}

std::mutex default_ctx_mutex;
client_context_ptr default_ctx;

}  // namespace

client_context::client_context(struct private_&&, const client_config& cfg)
    : cfg_(cfg), ctx_(ssl::context::tls_client)
{
  set_versions(ctx_, cfg.min_version, cfg.max_version);
  ctx_.set_options(ssl::context::default_workarounds |
                   ssl::context::no_compression);
  set_ciphers(ctx_, cfg.cipher_list, cfg.ciphersuites);

  if (cfg.verify_peer) {
    ctx_.set_verify_mode(ssl::verify_peer);
    if (cfg.use_system_ca) load_system_ca(ctx_);
    if (!cfg.ca_file.empty())
      apply("ca_file", [&] { ctx_.load_verify_file(cfg.ca_file); });
    if (!cfg.ca_path.empty())
      apply("ca_path", [&] { ctx_.add_verify_path(cfg.ca_path); });
    if (!cfg.ca_pem.empty())
      apply("ca_pem", [&] {
        ctx_.add_certificate_authority(
            boost::asio::buffer(cfg.ca_pem.data(), cfg.ca_pem.size()));
      });
    if (cfg.verify_callback) ctx_.set_verify_callback(cfg.verify_callback);
  } else {
    ctx_.set_verify_mode(ssl::verify_none);
    warn_insecure_once();
  }

  bool has_cert = !cfg.cert_file.empty() || !cfg.cert_pem.empty();
  bool has_key = !cfg.key_file.empty() || !cfg.key_pem.empty();
  if (has_cert != has_key)
    throw invalid_input_error(
        "[asyik::tls] a client certificate needs both a certificate and a "
        "private key");
  if (has_cert)
    load_identity(ctx_, cfg.cert_file, cfg.cert_pem, cfg.key_file, cfg.key_pem,
                  cfg.key_password, "client");
}

client_context_ptr make_client_context(const client_config& cfg)
{
  return std::make_shared<client_context>(client_context::private_{}, cfg);
}

client_context_ptr default_client_context()
{
  // Held only while building the context once; never across a fiber yield.
  std::lock_guard<std::mutex> lk(default_ctx_mutex);
  if (!default_ctx) default_ctx = make_client_context();
  return default_ctx;
}

void set_default_client_context(client_context_ptr ctx)
{
  std::lock_guard<std::mutex> lk(default_ctx_mutex);
  default_ctx = std::move(ctx);
}

server_context::server_context(struct private_&&, const server_config& cfg)
    : cfg_(cfg), ctx_(ssl::context::tls_server)
{
  SSL_CTX* h = ctx_.native_handle();

  set_versions(ctx_, cfg.min_version, cfg.max_version);

  // OpenSSL 3 uses 64-bit options (bit 31 is set by SSL_OP_ALL).
  decltype(SSL_CTX_get_options(h)) options =
      SSL_OP_ALL | SSL_OP_NO_COMPRESSION | SSL_OP_SINGLE_ECDH_USE |
      SSL_OP_SINGLE_DH_USE;
#ifdef SSL_OP_NO_RENEGOTIATION
  options |= SSL_OP_NO_RENEGOTIATION;
#endif
  if (!cfg.session_tickets) options |= SSL_OP_NO_TICKET;
  SSL_CTX_set_options(h, options);
  // Let OpenSSL pick DH parameters matching the key size for DHE suites.
  SSL_CTX_set_dh_auto(h, 1);

  set_ciphers(ctx_, cfg.cipher_list, cfg.ciphersuites);

  bool has_cert = !cfg.cert_file.empty() || !cfg.cert_pem.empty();
  bool has_key = !cfg.key_file.empty() || !cfg.key_pem.empty();
  if (!has_cert || !has_key)
    throw invalid_input_error(
        "[asyik::tls] server_config needs a certificate and a private key");
  load_identity(ctx_, cfg.cert_file, cfg.cert_pem, cfg.key_file, cfg.key_pem,
                cfg.key_password, "server");

  // Needed for session resumption once client certificates are requested.
  static const unsigned char sid_ctx[] = "libasyik";
  SSL_CTX_set_session_id_context(h, sid_ctx, sizeof(sid_ctx) - 1);

  for (const auto& proto : cfg.alpn) {
    if (proto.empty() || proto.size() > 255)
      throw invalid_input_error("[asyik::tls] invalid ALPN protocol name: '" +
                                proto + "'");
    alpn_wire_.push_back(static_cast<char>(proto.size()));
    alpn_wire_ += proto;
  }
  if (!alpn_wire_.empty())
    SSL_CTX_set_alpn_select_cb(h, select_alpn, &alpn_wire_);
}

server_context_ptr make_server_context(const server_config& cfg)
{
  return std::make_shared<server_context>(server_context::private_{}, cfg);
}

}  // namespace tls

namespace internal {
namespace tls {

void prepare_client(SSL* ssl, const asyik::tls::client_context& ctx,
                    string_view host_view)
{
  std::string host{host_view};
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
    host = host.substr(1, host.size() - 2);
  if (!host.empty() && host.back() == '.') host.pop_back();

  boost::system::error_code ec;
  boost::asio::ip::make_address(host, ec);
  const bool is_ip = !ec;

  // RFC 6066: SNI carries DNS names only, never IP literals.
  if (!is_ip && !SSL_set_tlsext_host_name(ssl, host.c_str()))
    throw tls_error("[asyik::tls] cannot set SNI host name: " + host);

  const auto& cfg = ctx.config();
  if (cfg.verify_peer && cfg.verify_hostname) {
    X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
    int ok;
    if (is_ip) {
      ok = X509_VERIFY_PARAM_set1_ip_asc(param, host.c_str());
    } else {
      X509_VERIFY_PARAM_set_hostflags(param,
                                      X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
      ok = X509_VERIFY_PARAM_set1_host(param, host.c_str(), host.size());
    }
    if (!ok)
      throw tls_error("[asyik::tls] cannot set host name verification for: " +
                      host);
  }
}

void rethrow_client_handshake_error(SSL* ssl,
                                    const asyik::tls::client_context& ctx,
                                    string_view host)
{
  try {
    throw;
  } catch (tls_error& e) {
    const std::string h{host};
    // OpenSSL records a verify result even when verification is off, so only
    // trust it when we asked for verification.
    long vr = SSL_get_verify_result(ssl);
    if (ctx.config().verify_peer && vr != X509_V_OK)
      throw tls_verify_error(
          e.code(), vr,
          "[asyik::tls_verify_error]certificate of '" + h +
              "' rejected: " + X509_verify_cert_error_string(vr));
    throw tls_handshake_error(
        e.code(), "[asyik::tls_handshake_error]TLS handshake with '" + h +
                      "' failed: " + e.code().message());
  }
}

}  // namespace tls
}  // namespace internal
}  // namespace asyik
