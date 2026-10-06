// Static file serving tests – uses ports 4100, 4101, 4102, 4103.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

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
void rmrf(const std::string& path)
{
  std::error_code ec;
  std::filesystem::remove_all(std::filesystem::u8path(path), ec);
}

/// Create a unique directory under the system temp dir and return its path
/// (with '/' separators).
std::string make_temp_dir(const char* prefix = "asyik_static_")
{
  static std::atomic<unsigned> counter{0};
  auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 100; ++attempt) {
    auto p = std::filesystem::temp_directory_path() /
             (prefix + std::to_string(stamp) + "_" + std::to_string(counter++));
    if (std::filesystem::create_directory(p)) return p.generic_u8string();
  }
  FAIL("could not create temp directory");
  return {};
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
  std::filesystem::create_directory(std::filesystem::u8path(root + "/sub"));
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

TEST_CASE("serve_static: path traversal is blocked", "[static_file][http]")
{
  std::string root = make_temp_dir();
  write_file(root, "safe.txt", "safe content");

  // A file outside the root that must never be served: a sibling directory
  // of root, reachable from root_dir via "..".
  std::string outside = make_temp_dir("asyik_outside_");
  write_file(outside, "secret.txt", "TOP SECRET");
  std::string outside_name =
      std::filesystem::u8path(outside).filename().generic_u8string();

  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4102);
  server->serve_static("/assets", root);

  as->execute([as, outside_name]() {
    auto base = std::string("http://127.0.0.1:4102");

    // Normal request still works.
    auto req = asyik::http_easy_request(as, "GET", base + "/assets/safe.txt");
    REQUIRE(req->response.result() == 200);

    // URL-encoded traversal (%2e%2e = ".."):
    // /assets/%2e%2e%2fasyik_outside_XXXX%2fsecret.txt
    // After percent-decode → /assets/../asyik_outside_XXXX/secret.txt
    // → realpath resolves outside root → 403.
    // (The Boost.URL client will send the encoded form to the server.)
    std::string encoded_traversal =
        base + "/assets/%2e%2e%2f" + outside_name + "%2fsecret.txt";
    req = asyik::http_easy_request(as, "GET", encoded_traversal);
    REQUIRE((req->response.result() == 403 || req->response.result() == 404));
    REQUIRE(req->response.body.find("TOP SECRET") == std::string::npos);

    as->stop();
  });

  as->run();
  rmrf(root);
  rmrf(outside);
}

// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("serve_static: unusual paths dates and ranges", "[static_file][http]")
{
  std::string root = make_temp_dir();
  std::string outside = make_temp_dir("asyik_static_outside_");
  write_file(root, "data.txt", "0123456789");
  write_file(outside, "secret.txt", "secret");
  std::filesystem::create_directory(root + "/noindex");
  std::filesystem::create_directories(root + "/dirindex/index.html");
#ifndef _WIN32
  std::filesystem::create_directory(root + "/escape");
  std::filesystem::create_symlink(outside + "/secret.txt",
                                  root + "/escape/index.html");
  REQUIRE(::mkfifo((root + "/pipe").c_str(), 0600) == 0);
#endif

  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4103);
  server->serve_static("/files", root);
  server->serve_static("/missing", root + "/does/not/exist");

  as->execute([as]() {
    std::string base = "http://127.0.0.1:4103";
    auto get = [&](const std::string& path,
                   const std::map<boost::string_view, boost::string_view>&
                       headers = {}) {
      return asyik::http_easy_request(as, 5000, "GET", base + path, "",
                                      headers);
    };

    // root_dir that cannot be resolved: every request is 404
    REQUIRE(get("/missing/data.txt")->response.result() == 404);

    // decoded NUL byte
    REQUIRE(get("/files/data%00.txt")->response.result() == 400);

    // directory without index file
    REQUIRE(get("/files/noindex/")->response.result() == 404);
    // index file that is a directory
    REQUIRE(get("/files/dirindex/")->response.result() == 403);
#ifndef _WIN32
    // index file that is a symlink leaving the root
    REQUIRE(get("/files/escape/")->response.result() == 403);
    // not a regular file
    REQUIRE(get("/files/pipe")->response.result() == 403);
#endif

    // If-Modified-Since in the obsolete formats RFC 7231 still accepts
    REQUIRE(get("/files/data.txt",
                {{"If-Modified-Since", "Saturday, 01-Jan-50 00:00:00 GMT"}})
                ->response.result() == 304);
    REQUIRE(get("/files/data.txt",
                {{"If-Modified-Since", "Fri Jan  1 00:00:00 2099"}})
                ->response.result() == 304);
    // unparsable date is ignored
    REQUIRE(get("/files/data.txt", {{"If-Modified-Since", "yesterday"}})
                ->response.result() == 200);

    // malformed range is ignored: full body
    auto req = get("/files/data.txt", {{"Range", "bytes=abc-3"}});
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "0123456789");

    as->stop();
  });

  as->run();
  rmrf(root);
  rmrf(outside);
}
