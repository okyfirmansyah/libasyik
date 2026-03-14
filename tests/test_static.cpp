// Static file serving tests – uses ports 4100, 4101, 4102.

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <filesystem>
#include <fstream>
#include <string>

#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Test helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Write @p content to @p dir / @p name, returning the full path.
std::string write_file(const std::string& dir, const std::string& name,
                       const std::string& content)
{
  std::string path = dir + "/" + name;
  std::ofstream f(path, std::ios::binary);
  REQUIRE(f.is_open());
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  return path;
}

/// Recursively remove a directory tree.
void rmrf(const std::string& path) {
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

/// Create a temporary directory and return its path (forward slashes).
std::string make_temp_dir(const char* /*tmpl*/ = nullptr)
{
  auto tmp = std::filesystem::temp_directory_path();
  std::string name = "asyik_static_" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto dir = tmp / name;
  std::filesystem::create_directories(dir);
  std::string result = dir.string();
  // Normalize to forward slashes for consistency with libasyik path handling.
  for (auto& c : result)
    if (c == '\\') c = '/';
  return result;
}

/// Portable mkdir.
void portable_mkdir(const std::string& path) {
#ifdef _WIN32
  _mkdir(path.c_str());
#else
  ::mkdir(path.c_str(), 0755);
#endif
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Unit tests for internal helpers (no network)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("get_mime_type returns correct values", "[static_file][unit]")
{
  using asyik::internal::get_mime_type;

  REQUIRE(get_mime_type(".html").find("text/html") != std::string::npos);
  REQUIRE(get_mime_type(".css") == "text/css");
  REQUIRE(get_mime_type(".js").find("javascript") != std::string::npos);
  REQUIRE(get_mime_type(".json") == "application/json");
  REQUIRE(get_mime_type(".svg") == "image/svg+xml");
  REQUIRE(get_mime_type(".png") == "image/png");
  REQUIRE(get_mime_type(".wasm") == "application/wasm");
  REQUIRE(get_mime_type(".unknown") == "application/octet-stream");
  REQUIRE(get_mime_type("") == "application/octet-stream");
}

TEST_CASE("make_etag produces quoted hex string", "[static_file][unit]")
{
  using asyik::internal::make_etag;

  auto etag = make_etag(1234567890LL, 65536LL);
  REQUIRE(etag.front() == '"');
  REQUIRE(etag.back() == '"');
  REQUIRE(etag.find('-') != std::string::npos);

  // Same inputs → same ETag (deterministic).
  REQUIRE(make_etag(1234567890LL, 65536LL) == etag);

  // Different inputs → different ETags.
  REQUIRE(make_etag(1234567890LL, 65537LL) != etag);
  REQUIRE(make_etag(1234567891LL, 65536LL) != etag);
}

TEST_CASE("route_spec_to_regex supports <path> wildcard", "[static_file][unit]")
{
  using asyik::internal::route_spec_to_regex;

  // <path> should capture everything including slashes.
  std::string spec = "/files/<path>";
  std::string re_str = route_spec_to_regex(spec);
  std::regex re(re_str);
  std::smatch m;

  // Matches flat file.
  std::string s1 = "/files/image.png";
  REQUIRE(std::regex_search(s1, m, re));
  REQUIRE(m[1].str() == "image.png");

  // Matches nested path.
  std::string s2 = "/files/a/b/c.js";
  REQUIRE(std::regex_search(s2, m, re));
  REQUIRE(m[1].str() == "a/b/c.js");

  // Does NOT match a different prefix.
  std::string s3 = "/other/file.txt";
  REQUIRE(!std::regex_search(s3, m, re));
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration tests – spin up a real server on each port
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("serve_static: basic GET and MIME types", "[static_file][http]")
{
  std::string root = make_temp_dir();

  write_file(root, "index.html", "<html>Index</html>");
  write_file(root, "hello.html", "<html>Hello</html>");
  write_file(root, "style.css", "body{color:red}");
  write_file(root, "app.js", "console.log(1);");
  write_file(root, "data.json", R"({"ok":true})");
  write_file(root, "image.png", "\x89PNG\r\n\x1a\n");

  // Sub-directory
  portable_mkdir(root + "/sub");
  write_file(root + "/sub", "page.html", "<html>Sub</html>");

  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4100);
  server->serve_static("/static", root);

  as->execute([as, root]() {
    auto base = std::string("http://127.0.0.1:4100");

    // ── HTML file ────────────────────────────────────────────────────────────
    auto req = asyik::http_easy_request(as, "GET", base + "/static/hello.html");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "<html>Hello</html>");
    REQUIRE(req->response.headers["Content-Type"].find("text/html") !=
            boost::beast::string_view::npos);

    // ── CSS ─────────────────────────────────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/style.css");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "body{color:red}");
    REQUIRE(req->response.headers["Content-Type"] == "text/css");

    // ── JavaScript ──────────────────────────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/app.js");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.headers["Content-Type"].find("javascript") !=
            boost::beast::string_view::npos);

    // ── JSON ─────────────────────────────────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/data.json");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.headers["Content-Type"].find("json") !=
            boost::beast::string_view::npos);

    // ── PNG (binary body round-trip) ──────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/image.png");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.headers["Content-Type"] == "image/png");
    REQUIRE(req->response.body.substr(0, 4) == "\x89PNG");

    // ── Nested sub-directory file ────────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/sub/page.html");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "<html>Sub</html>");

    // ── Directory URL → index.html ───────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "<html>Index</html>");

    // ── Query string is ignored ──────────────────────────────────────────
    req = asyik::http_easy_request(as, "GET",
                                   base + "/static/style.css?v=42&x=y");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "body{color:red}");

    // ── 404 for non-existent file ────────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/missing.txt");
    REQUIRE(req->response.result() == 404);

    // ── Accept-Ranges header present ─────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", base + "/static/hello.html");
    REQUIRE(req->response.headers["Accept-Ranges"] == "bytes");

    as->stop();
  });

  as->run();
  rmrf(root);
}

// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("serve_static: ETag and Last-Modified conditional GET",
          "[static_file][http]")
{
  std::string root = make_temp_dir();
  write_file(root, "data.txt", "Hello ETag World");

  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4101);
  server->serve_static("/files", root);

  as->execute([as]() {
    auto url = std::string("http://127.0.0.1:4101/files/data.txt");

    // ── First request → receive ETag and Last-Modified ──────────────────
    auto req = asyik::http_easy_request(as, "GET", url);
    REQUIRE(req->response.result() == 200);

    std::string etag = std::string(req->response.headers["ETag"]);
    std::string last_mod = std::string(req->response.headers["Last-Modified"]);
    REQUIRE(!etag.empty());
    REQUIRE(!last_mod.empty());
    REQUIRE(etag.front() == '"');  // must be a quoted string
    REQUIRE(etag.back() == '"');

    // ── If-None-Match with matching ETag → 304 ─────────────────────────
    req = asyik::http_easy_request(
        as, "GET", url, "", {{"If-None-Match", boost::string_view(etag)}});
    REQUIRE(req->response.result() == 304);
    REQUIRE(req->response.body.empty());

    // ── If-None-Match with wildcard → 304 ─────────────────────────────
    req =
        asyik::http_easy_request(as, "GET", url, "", {{"If-None-Match", "*"}});
    REQUIRE(req->response.result() == 304);

    // ── If-None-Match with wrong ETag → 200 ───────────────────────────
    req = asyik::http_easy_request(as, "GET", url, "",
                                   {{"If-None-Match", "\"wrong-etag\""}});
    REQUIRE(req->response.result() == 200);

    // ── If-Modified-Since with future date → 304 ──────────────────────
    req = asyik::http_easy_request(
        as, "GET", url, "",
        {{"If-Modified-Since", "Thu, 01 Jan 2099 00:00:00 GMT"}});
    REQUIRE(req->response.result() == 304);

    // ── If-Modified-Since with past date → 200 ─────────────────────────
    req = asyik::http_easy_request(
        as, "GET", url, "",
        {{"If-Modified-Since", "Thu, 01 Jan 1970 00:00:00 GMT"}});
    REQUIRE(req->response.result() == 200);

    as->stop();
  });

  as->run();
  rmrf(root);
}

// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("serve_static: Range requests (206 Partial Content)",
          "[static_file][http]")
{
  std::string root = make_temp_dir();
  // 16-byte known payload.
  write_file(root, "data.bin", "0123456789ABCDEF");

  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4102);
  server->serve_static("/range", root);

  as->execute([as]() {
    auto url = std::string("http://127.0.0.1:4102/range/data.bin");

    // ── bytes=0-9 → first 10 bytes ──────────────────────────────────────
    auto req =
        asyik::http_easy_request(as, "GET", url, "", {{"Range", "bytes=0-9"}});
    REQUIRE(req->response.result() == 206);
    REQUIRE(req->response.body == "0123456789");
    {
      std::string cr = std::string(req->response.headers["Content-Range"]);
      REQUIRE(cr.find("bytes 0-9/16") != std::string::npos);
    }

    // ── bytes=10-15 → last 6 bytes ──────────────────────────────────────
    req = asyik::http_easy_request(as, "GET", url, "",
                                   {{"Range", "bytes=10-15"}});
    REQUIRE(req->response.result() == 206);
    REQUIRE(req->response.body == "ABCDEF");
    {
      std::string cr = std::string(req->response.headers["Content-Range"]);
      REQUIRE(cr.find("bytes 10-15/16") != std::string::npos);
    }

    // ── Open-ended range: bytes=8- → bytes 8 to end ─────────────────────
    req = asyik::http_easy_request(as, "GET", url, "", {{"Range", "bytes=8-"}});
    REQUIRE(req->response.result() == 206);
    REQUIRE(req->response.body == "89ABCDEF");

    // ── Suffix range: bytes=-4 → last 4 bytes ───────────────────────────
    req = asyik::http_easy_request(as, "GET", url, "", {{"Range", "bytes=-4"}});
    REQUIRE(req->response.result() == 206);
    REQUIRE(req->response.body == "CDEF");

    // ── Without Range header → full file (200) ──────────────────────────
    req = asyik::http_easy_request(as, "GET", url);
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "0123456789ABCDEF");

    as->stop();
  });

  as->run();
  rmrf(root);
}

// ─────────────────────────────────────────────────────────────────────────────

#ifndef _WIN32
TEST_CASE("serve_static: path traversal is blocked", "[static_file][http]")
{
  std::string root = make_temp_dir();
  write_file(root, "safe.txt", "safe content");

  // A file outside the root that must never be served.
  // We can't guarantee /etc/passwd exists but we can check a path
  // that resolves above root_dir.
  std::string outside = make_temp_dir("/tmp/asyik_outside_XXXXXX");
  write_file(outside, "secret.txt", "TOP SECRET");

  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4102);
  server->serve_static("/assets", root);

  as->execute([as, outside]() {
    auto base = std::string("http://127.0.0.1:4102");

    // Normal request still works.
    auto req = asyik::http_easy_request(as, "GET", base + "/assets/safe.txt");
    REQUIRE(req->response.result() == 200);

    // URL-encoded traversal (%2e%2e = ".."):
    // /assets/%2e%2e/%2e%2e/tmp/asyik_outside_XXXXXX/secret.txt
    // After percent-decode → /assets/../../tmp/.../secret.txt
    // → realpath resolves above root → 403.
    // (The Boost.URL client will send the encoded form to the server.)
    std::string encoded_traversal = base + "/assets/%2e%2e%2f%2e%2e%2f" +
                                    outside.substr(1) +  // strip leading '/'
                                    "%2fsecret.txt";
    req = asyik::http_easy_request(as, "GET", encoded_traversal);
    REQUIRE((req->response.result() == 403 || req->response.result() == 404));

    as->stop();
  });

  as->run();
  rmrf(root);
  rmrf(outside);
}
#endif  // !_WIN32
