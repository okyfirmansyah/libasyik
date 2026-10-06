// Windows only: OpenSSL does not read the Windows certificate store, so copy
// the trusted roots into the context's X509_STORE.
//
// <wincrypt.h> #defines names such as X509_NAME that OpenSSL uses as types,
// so this file includes it first (after <windows.h>, which it needs) and
// undoes the clashing macros before any OpenSSL header.
// clang-format off
#include <windows.h>
#include <wincrypt.h>
// clang-format on

#undef X509_NAME
#undef X509_EXTENSIONS
#undef PKCS7_SIGNER_INFO
#undef OCSP_REQUEST
#undef OCSP_RESPONSE

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace asyik {
namespace internal {
namespace tls {

void load_windows_root_store(SSL_CTX* ctx)
{
  HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
  if (!store) return;

  X509_STORE* x509_store = SSL_CTX_get_cert_store(ctx);
  PCCERT_CONTEXT cert = nullptr;
  while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr) {
    const unsigned char* der = cert->pbCertEncoded;
    X509* x509 =
        d2i_X509(nullptr, &der, static_cast<long>(cert->cbCertEncoded));
    if (x509) {
      // Fails for duplicates; that is fine.
      X509_STORE_add_cert(x509_store, x509);
      X509_free(x509);
    }
  }
  CertCloseStore(store, 0);
  // Drop the duplicate-certificate errors queued on OpenSSL 1.1.
  ERR_clear_error();
}

}  // namespace tls
}  // namespace internal
}  // namespace asyik
