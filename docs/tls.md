## TLS (HTTPS and WSS)

Libasyik uses OpenSSL through Boost.Asio for `https://` and `wss://`. Include
`libasyik/tls.hpp` (it is also pulled in by `libasyik/http.hpp`).

### Client: secure by default

Every `https://` and `wss://` client connection:

- verifies the server certificate chain against the system trust store
  (Windows: the ROOT certificate store; elsewhere OpenSSL's default paths, which
  also honour `SSL_CERT_FILE` / `SSL_CERT_DIR`);
- checks that the certificate matches the URL host (DNS name, or IP address for
  URLs such as `https://10.0.0.5/`);
- sends the host name as SNI (not for IP addresses);
- negotiates TLS 1.2 or TLS 1.3.

No code is needed for public servers:

```c++
auto req = asyik::http_easy_request(as, "GET", "https://example.com/");
```

### Upgrading from older versions

Existing code compiles unchanged; these behaviours changed:

- **Clients verify server certificates.** Earlier releases accepted any
  certificate. Requests to servers with self-signed, expired or mismatching
  certificates now throw `asyik::tls_verify_error`. Trust your private CA with
  `ca_file` (below) or, for testing only, use `client_config::insecure()`.
- **Clients send SNI and can negotiate TLS 1.3** (previously TLS 1.2 only).
- **HTTPS servers drop connections that do not finish the TLS handshake
  within 10 seconds**, and wait at most 2 seconds for a client's close_notify
  when closing. Both are adjustable (see [Timeouts](#timeouts)).
- **Failed server handshakes are logged at DEBUG**, not WARNING.
- **`server->close()` also closes WebSockets** whose handler is still running.
- **An encrypted private key without `key_password` is rejected** with
  `invalid_input_error` instead of OpenSSL prompting on the terminal.

### Custom client settings

Build a `tls::client_context` from a `tls::client_config` once and reuse it.
Contexts are immutable and safe to share between services and threads.

```c++
asyik::tls::client_config cfg;
cfg.ca_file = "/etc/myapp/internal-ca.pem";  // trusted in addition to system CAs
// cfg.use_system_ca = false;                // trust only ca_file
auto ctx = asyik::tls::make_client_context(cfg);
```

Pass it per call, per service, or for the whole process. A per-call context
takes priority over the service's, which takes priority over the
process-wide default:

```c++
// 1. per call
asyik::http_easy_request(as, ctx, 10000 /*ms*/, "GET", url, "", {});
asyik::make_websocket_connection(as, ctx, "wss://internal.example/ws");

// 2. per service (every request made through `as`)
as->set_tls_client_context(ctx);

// 3. process-wide default (nullptr restores the built-in one)
asyik::tls::set_default_client_context(ctx);
```

`client_config` fields:

| Field | Default | Meaning |
|---|---|---|
| `verify_peer` | `true` | Verify the certificate chain |
| `verify_hostname` | `true` | Require the certificate to match the URL host |
| `use_system_ca` | `true` | Trust the operating system's CAs |
| `ca_file` / `ca_path` / `ca_pem` | empty | Extra trusted CAs (PEM file, hashed directory, in-memory PEM) |
| `cert_file` / `cert_pem`, `key_file` / `key_pem` | empty | Client certificate for mutual TLS |
| `key_password` | empty | Password for an encrypted private key. An encrypted key without it is rejected (OpenSSL's terminal prompt is never used) |
| `min_version` / `max_version` | `tls1_2` / `tls1_3` | Allowed protocol versions |
| `cipher_list` / `ciphersuites` | OpenSSL defaults | TLS 1.2 cipher string / TLS 1.3 suites |
| `verify_callback` | none | Extra per-certificate check (e.g. pinning); `preverified` already includes the standard checks |

Invalid settings (unreadable file, key not matching the certificate, wrong key
password, bad cipher string, ...) make `make_client_context()` throw
`asyik::invalid_input_error`.

#### Mutual TLS (client certificate)

```c++
asyik::tls::client_config cfg;
cfg.cert_file = "client.crt";
cfg.key_file = "client.key";
cfg.key_password = "secret";  // only if the key is encrypted
auto ctx = asyik::tls::make_client_context(cfg);
```

#### Disabling verification (testing only)

```c++
auto insecure = asyik::tls::make_client_context(
    asyik::tls::client_config::insecure());
```

The connection is still encrypted, but anyone on the network path can
intercept it. A warning is logged the first time such a context is created.

### Errors

```
asyik::network_error
└── asyik::tls_error            any TLS-layer failure
    └── asyik::tls_handshake_error   the client handshake failed
        └── asyik::tls_verify_error  the server certificate was rejected
```

`tls_verify_error::verify_result()` returns the OpenSSL `X509_V_ERR_*` code,
and `what()` says why, for example:

```
[asyik::tls_verify_error]certificate of 'example.com' rejected: Hostname mismatch: ...
```

Timeouts and dropped connections keep their usual exceptions
(`network_timeout_error`, `already_closed_error`).

### Server

Pass a `tls::server_config` to `make_https_server()`:

```c++
asyik::tls::server_config cfg;
cfg.cert_file = "/etc/myapp/fullchain.pem";  // leaf first, then intermediates
cfg.key_file = "/etc/myapp/privkey.pem";

auto server = asyik::make_https_server(as, cfg, "0.0.0.0", 443);
```

The defaults follow Mozilla's "intermediate" profile:

| Field | Default | Meaning |
|---|---|---|
| `cert_file` / `cert_pem`, `key_file` / `key_pem` | required | Certificate chain and private key |
| `key_password` | empty | Password for an encrypted private key. An encrypted key without it is rejected (OpenSSL's terminal prompt is never used) |
| `min_version` / `max_version` | `tls1_2` / `tls1_3` | Allowed protocol versions |
| `cipher_list` | `tls::mozilla_intermediate_ciphers` | TLS 1.2 ciphers: forward secrecy and AEAD only |
| `ciphersuites` | OpenSSL defaults | TLS 1.3 suites |
| `alpn` | `{"http/1.1"}` | ALPN protocols, most preferred first. Clients offering none of them still connect, without ALPN. Empty disables ALPN |
| `session_tickets` | `true` | Stateless session resumption. Session-ID resumption works either way |

Compression and renegotiation are always disabled. The certificate and key
are checked when the context is built: a missing file, wrong key password or
key that does not match the certificate throws `asyik::invalid_input_error`
straight away, not on the first connection.

To share one context between several servers (for example one per thread
with `reuse_port`), build it once:

```c++
auto ctx = asyik::tls::make_server_context(cfg);
auto server = asyik::make_https_server(as, ctx, "0.0.0.0", 443, true);
```

The older `make_https_server(as, ssl::context&&, ...)` overload is still
available for hand-configured Asio contexts; it does not apply these
defaults.

#### Timeouts

```c++
server->set_tls_handshake_timeout(std::chrono::seconds(10));  // default 10s
server->set_tls_shutdown_timeout(std::chrono::seconds(2));    // default 2s
```

- **Handshake timeout:** a new connection must finish the TLS handshake within
  this time or it is dropped. Without it, a client that connects and sends
  nothing would hold a connection forever. It covers the handshake only; an
  established keep-alive connection may stay idle longer.
- **Shutdown timeout:** when closing a connection the server sends
  close_notify and waits this long for the client's before closing the
  socket anyway.

Zero or a negative value disables the limit. Both apply to every
`make_https_server()` overload.

Failed handshakes (port scanners, plain HTTP sent to the HTTPS port, idle
clients, untrusted client certificates) are logged at DEBUG level, since
they are routine on a public port.
