#include <openssl/evp.h>

#include <map>
#include <regex>

#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"
#include "test_tls_util.hpp"

using namespace asyik;
using namespace asyik_test;

namespace {

std::string md5_hex(const std::string& s)
{
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  EVP_Digest(s.data(), s.size(), md, &len, EVP_md5(), nullptr);
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (unsigned int i = 0; i < len; i++) {
    out += hex[md[i] >> 4];
    out += hex[md[i] & 0xf];
  }
  return out;
}

// key=value / key="value" pairs of an Authorization header
std::map<std::string, std::string> parse_digest(std::string h)
{
  std::map<std::string, std::string> m;
  if (h.compare(0, 7, "Digest ")) return m;
  std::regex re{R"re((\w+)=(?:"([^"]*)"|([^,\s]*)))re"};
  for (std::sregex_iterator it(h.begin() + 7, h.end(), re), end; it != end;
       ++it)
    m[(*it)[1]] = (*it)[2].matched ? (*it)[2].str() : (*it)[3].str();
  return m;
}

struct digest_server_state {
  int requests = 0;
  int authorized = 0;
  std::string last_authorization;
  std::string last_body;
};

// Protects path with a digest challenge. strict=true verifies the response
// hash the way an RFC 2617 server does (MD5, qop absent or "auth"); otherwise
// any Digest Authorization with a response field is accepted.
template <typename Server>
std::shared_ptr<digest_server_state> add_digest_route(
    Server server, const std::string& path, const std::string& challenge,
    const std::string& password, bool strict = true)
{
  auto state = std::make_shared<digest_server_state>();
  server->on_http_request(path, [=](http_request_ptr req,
                                    const http_route_args&) {
    state->requests++;
    std::string auth{req->headers["Authorization"]};
    state->last_authorization = auth;
    auto p = parse_digest(auth);

    bool ok = p.count("response") > 0;
    if (ok && strict) {
      std::string ha1 =
          md5_hex(p["username"] + ":" + p["realm"] + ":" + password);
      std::string ha2 = md5_hex(std::string(req->method()) + ":" + p["uri"]);
      std::string expected =
          p.count("qop")
              ? md5_hex(ha1 + ":" + p["nonce"] + ":" + p["nc"] + ":" +
                        p["cnonce"] + ":" + p["qop"] + ":" + ha2)
              : md5_hex(ha1 + ":" + p["nonce"] + ":" + ha2);
      ok = expected == p["response"];
    }

    if (!ok) {
      req->response.headers.set("WWW-Authenticate", challenge);
      req->response.body = "unauthorized";
      req->response.result(401);
      return;
    }
    state->authorized++;
    state->last_body = req->body;
    req->response.body = "welcome " + p["username"] + ":" + req->body;
    req->response.result(200);
  });
  return state;
}

const std::string rfc2617_challenge =
    "Digest realm=\"asyik\", qop=\"auth\", "
    "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", "
    "opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"";

}  // namespace

TEST_CASE("digest auth: qop=auth challenge", "[http][digest]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4300);
  auto state = add_digest_route(server, "/secret", rfc2617_challenge, "s3cret");

  run_client(as, server, [=]() {
    auto req = http_easy_request(
        as, "GET", "http://alice:s3cret@127.0.0.1:4300/secret", "", {});
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "welcome alice:");
    REQUIRE(state->requests == 2);

    auto p = parse_digest(state->last_authorization);
    REQUIRE(p["realm"] == "asyik");
    REQUIRE(p["uri"] == "/secret");
    REQUIRE(p["qop"] == "auth");
    REQUIRE(p["opaque"] == "5ccc069c403ebaf9f0171e9517f40e41");
    REQUIRE(p["cnonce"].size() >= 8);

    SECTION("POST body survives the authenticated retry")
    {
      auto post = http_easy_request(as, "POST",
                                    "http://alice:s3cret@127.0.0.1:4300/secret",
                                    std::string("payload"), {});
      REQUIRE(post->response.result() == 200);
      REQUIRE(state->last_body == "payload");
      REQUIRE(post->response.body == "welcome alice:payload");
    }
  });
}

TEST_CASE("digest auth: RFC 2069 challenge without qop", "[http][digest]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4301);
  auto state = add_digest_route(
      server, "/secret", "Digest realm=\"legacy\", nonce=\"abc123\"", "pw");

  run_client(as, server, [=]() {
    auto req = http_easy_request(as, "GET",
                                 "http://bob:pw@127.0.0.1:4301/secret", "", {});
    REQUIRE(req->response.result() == 200);
    auto p = parse_digest(state->last_authorization);
    REQUIRE(p.count("qop") == 0);
    REQUIRE(p.count("opaque") == 0);
  });
}

TEST_CASE("digest auth: failed authentication is returned without retry",
          "[http][digest]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4302);
  auto digest =
      add_digest_route(server, "/secret", rfc2617_challenge, "s3cret");
  auto basic =
      add_digest_route(server, "/basic", "Basic realm=\"asyik\"", "s3cret");

  run_client(as, server, [=]() {
    SECTION("wrong password")
    {
      auto req = http_easy_request(
          as, 5000, "GET", "http://alice:wrong@127.0.0.1:4302/secret", "", {});
      REQUIRE(req->response.result() == 401);
      REQUIRE(req->response.body == "unauthorized");
      REQUIRE(digest->requests == 2);
    }
    SECTION("challenge that is not a digest challenge")
    {
      auto req = http_easy_request(
          as, 5000, "GET", "http://alice:s3cret@127.0.0.1:4302/basic", "", {});
      REQUIRE(req->response.result() == 401);
      REQUIRE(req->response.body == "unauthorized");
      REQUIRE(basic->requests == 1);
    }
    SECTION("no credentials in the URL")
    {
      auto req = http_easy_request(as, 5000, "GET",
                                   "http://127.0.0.1:4302/secret", "", {});
      REQUIRE(req->response.result() == 401);
      REQUIRE(digest->requests == 1);
    }
  });
}

TEST_CASE("digest auth: auth-int and MD5-sess challenges", "[http][digest]")
{
  auto as = make_service();
  auto server = make_http_server(as, "127.0.0.1", 4303);
  auto auth_int = add_digest_route(
      server, "/int", "Digest realm=\"r\", qop=\"auth-int\", nonce=\"n1\"",
      "pw", false);
  auto sess = add_digest_route(
      server, "/sess",
      "Digest realm=\"r\", qop=auth, algorithm=MD5-sess, nonce=\"n2\"", "pw",
      false);

  run_client(as, server, [=]() {
    // auth-int is only chosen when the 401 response has a body
    auto req =
        http_easy_request(as, "GET", "http://u:pw@127.0.0.1:4303/int", "", {});
    REQUIRE(req->response.result() == 200);
    REQUIRE(parse_digest(auth_int->last_authorization)["qop"] == "auth-int");

    req =
        http_easy_request(as, "GET", "http://u:pw@127.0.0.1:4303/sess", "", {});
    REQUIRE(req->response.result() == 200);
    REQUIRE(parse_digest(sess->last_authorization)["qop"] == "auth");
  });
}

TEST_CASE("digest auth over HTTPS", "[http][digest][tls]")
{
  auto as = make_service();
  auto server =
      make_https_server(as, make_test_server_ssl_context(), "127.0.0.1", 4304);
  auto state = add_digest_route(server, "/secret", rfc2617_challenge, "s3cret");

  run_client(as, server, [=]() {
    auto req =
        http_easy_request(as, test_client_context(), 5000, "GET",
                          "https://alice:s3cret@localhost:4304/secret", "", {});
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "welcome alice:");
    REQUIRE(state->requests == 2);
  });
}
