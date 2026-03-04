// Enable strptime and timegm on Linux/glibc.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "libasyik/http_static.hpp"

// Full http_request definition (http_static.hpp only has the forward decl
// via http_types.hpp → asyik_fwd.hpp)
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

#include "aixlog.hpp"
#include "libasyik/error.hpp"
#include "libasyik/http_client.hpp"

namespace asyik {
namespace internal {

// ---------------------------------------------------------------------------
// MIME type table
// ---------------------------------------------------------------------------

std::string get_mime_type(const std::string& ext)
{
  static const std::unordered_map<std::string, std::string> mime_map = {
      {".html", "text/html; charset=utf-8"},
      {".htm", "text/html; charset=utf-8"},
      {".css", "text/css"},
      {".js", "application/javascript"},
      {".mjs", "application/javascript"},
      {".json", "application/json"},
      {".xml", "application/xml"},
      {".txt", "text/plain; charset=utf-8"},
      {".md", "text/plain; charset=utf-8"},
      {".svg", "image/svg+xml"},
      {".png", "image/png"},
      {".jpg", "image/jpeg"},
      {".jpeg", "image/jpeg"},
      {".gif", "image/gif"},
      {".webp", "image/webp"},
      {".ico", "image/x-icon"},
      {".bmp", "image/bmp"},
      {".woff", "font/woff"},
      {".woff2", "font/woff2"},
      {".ttf", "font/ttf"},
      {".otf", "font/otf"},
      {".mp4", "video/mp4"},
      {".webm", "video/webm"},
      {".mp3", "audio/mpeg"},
      {".wav", "audio/wav"},
      {".ogg", "audio/ogg"},
      {".pdf", "application/pdf"},
      {".zip", "application/zip"},
      {".gz", "application/gzip"},
      {".wasm", "application/wasm"},
  };
  auto it = mime_map.find(ext);
  return (it != mime_map.end()) ? it->second : "application/octet-stream";
}

// ---------------------------------------------------------------------------
// ETag helpers
// ---------------------------------------------------------------------------

std::string make_etag(long long mtime, long long size)
{
  std::ostringstream oss;
  oss << '"' << std::hex << mtime << '-' << std::hex << size << '"';
  return oss.str();
}

// ---------------------------------------------------------------------------
// HTTP date helpers (RFC 7231)
// ---------------------------------------------------------------------------

static std::string format_http_date(time_t t)
{
  struct tm tm_buf;
  gmtime_r(&t, &tm_buf);
  char buf[64];
  strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_buf);
  return std::string(buf);
}

static time_t parse_http_date(const std::string& s)
{
  struct tm t = {};
  // RFC 7231 preferred: "Sun, 06 Nov 1994 08:49:37 GMT"
  if (strptime(s.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &t)) return timegm(&t);
  // RFC 850: "Sunday, 06-Nov-94 08:49:37 GMT"
  if (strptime(s.c_str(), "%A, %d-%b-%y %H:%M:%S GMT", &t)) return timegm(&t);
  // ANSI C: "Sun Nov  6 08:49:37 1994"
  if (strptime(s.c_str(), "%a %b %e %H:%M:%S %Y", &t)) return timegm(&t);
  return static_cast<time_t>(-1);
}

// ---------------------------------------------------------------------------
// Percent-decode a URI path component
// ---------------------------------------------------------------------------

static std::string percent_decode(const std::string& encoded)
{
  std::string result;
  result.reserve(encoded.size());
  for (size_t i = 0; i < encoded.size(); ++i) {
    if (encoded[i] == '%' && i + 2 < encoded.size() &&
        isxdigit(static_cast<unsigned char>(encoded[i + 1])) &&
        isxdigit(static_cast<unsigned char>(encoded[i + 2]))) {
      char hex[3] = {encoded[i + 1], encoded[i + 2], '\0'};
      result += static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
    } else {
      result += encoded[i];
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// Low-level file read helpers
// ---------------------------------------------------------------------------

/// Read [offset, offset+length) bytes from fd into buf.
static bool read_range(int fd, char* buf, off_t offset, size_t length)
{
  if (lseek(fd, offset, SEEK_SET) == static_cast<off_t>(-1)) return false;
  size_t remaining = length;
  while (remaining > 0) {
    ssize_t n = read(fd, buf + (length - remaining), remaining);
    if (n <= 0) return false;
    remaining -= static_cast<size_t>(n);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Handler factory
// ---------------------------------------------------------------------------

http_route_callback make_static_file_handler(std::string url_prefix,
                                             std::string root_dir,
                                             static_file_config cfg)
{
  // Canonicalise root_dir at registration time so we fail fast on bad config.
  char canon_buf[PATH_MAX];
  if (!realpath(root_dir.c_str(), canon_buf)) {
    LOG(ERROR) << "serve_static: cannot resolve root_dir '" << root_dir
               << "': " << strerror(errno) << "\n";
    return [](http_request_ptr req, const http_route_args&) {
      req->response.result(404);
      req->response.body = "Not Found";
    };
  }

  // canonical_root always ends with '/' for easy prefix comparison.
  std::string canonical_root = canon_buf;
  if (canonical_root.back() != '/') canonical_root += '/';

  // Normalise prefix: no trailing slash.
  if (!url_prefix.empty() && url_prefix.back() == '/') url_prefix.pop_back();

  return [url_prefix, canonical_root, cfg](http_request_ptr req,
                                           const http_route_args&) {
    // ─── ① Extract sub-path (strip query string and fragment) ─────────────
    std::string target = std::string(req->target());
    {
      auto p = target.find('?');
      if (p != std::string::npos) target.erase(p);
      p = target.find('#');
      if (p != std::string::npos) target.erase(p);
    }

    // Strip the url_prefix from the front.
    if (target.size() >= url_prefix.size() &&
        target.compare(0, url_prefix.size(), url_prefix) == 0)
      target.erase(0, url_prefix.size());

    if (target.empty()) target = "/";

    // ─── ② Percent-decode ────────────────────────────────────────────────
    target = percent_decode(target);

    // Reject null bytes immediately.
    if (target.find('\0') != std::string::npos) {
      req->response.result(400);
      req->response.body = "Bad Request";
      return;
    }

    // Build full path: canonical_root + subpath (skip leading '/').
    std::string subpath =
        (!target.empty() && target[0] == '/') ? target.substr(1) : target;
    std::string full_path = canonical_root + subpath;

    // ─── ③ Canonicalise and guard against path traversal ─────────────────
    char resolved_buf[PATH_MAX];
    std::string real_path;

    if (realpath(full_path.c_str(), resolved_buf)) {
      real_path = resolved_buf;
    } else {
      req->response.result(404);
      req->response.body = "Not Found";
      return;
    }

    // The resolved path must start with canonical_root (which ends with '/'),
    // OR it must be canonical_root itself without the trailing slash.
    bool inside =
        (real_path.compare(0, canonical_root.size(), canonical_root) == 0) ||
        (real_path ==
         canonical_root.substr(0, canonical_root.size() - 1) /* root itself */);
    if (!inside) {
      req->response.result(403);
      req->response.body = "Forbidden";
      return;
    }

    // ─── ④ stat() ─────────────────────────────────────────────────────────
    struct stat st;
    if (stat(real_path.c_str(), &st) != 0) {
      req->response.result(404);
      req->response.body = "Not Found";
      return;
    }

    // ─── ⑤ Directory → index file ────────────────────────────────────────
    if (S_ISDIR(st.st_mode)) {
      if (real_path.back() != '/') real_path += '/';
      real_path += cfg.index_file;

      char resolved2[PATH_MAX];
      if (!realpath(real_path.c_str(), resolved2)) {
        req->response.result(404);
        req->response.body = "Not Found";
        return;
      }
      real_path = resolved2;

      // Re-check traversal after following index file path.
      if (real_path.compare(0, canonical_root.size(), canonical_root) != 0) {
        req->response.result(403);
        req->response.body = "Forbidden";
        return;
      }

      if (stat(real_path.c_str(), &st) != 0) {
        req->response.result(404);
        req->response.body = "Not Found";
        return;
      }
      if (S_ISDIR(st.st_mode)) {
        req->response.result(403);
        req->response.body = "Forbidden";
        return;
      }
    }

    if (!S_ISREG(st.st_mode)) {
      req->response.result(403);
      req->response.body = "Forbidden";
      return;
    }

    long long mtime = static_cast<long long>(st.st_mtime);
    long long fsize = static_cast<long long>(st.st_size);

    // ─── ⑥ ETag and Last-Modified (conditional GET) ──────────────────────
    std::string etag_val = make_etag(mtime, fsize);
    std::string last_modified_val = format_http_date(st.st_mtime);

    if (cfg.enable_etag) {
      std::string inm = std::string(req->headers["If-None-Match"]);
      if (!inm.empty() && (inm == etag_val || inm == "*")) {
        req->response.result(304);
        req->response.headers.set("ETag", etag_val);
        req->response.headers.set("Cache-Control", cfg.cache_control);
        return;
      }
    }

    if (cfg.enable_last_modified) {
      std::string ims_str = std::string(req->headers["If-Modified-Since"]);
      if (!ims_str.empty()) {
        time_t ims = parse_http_date(ims_str);
        if (ims != static_cast<time_t>(-1) && st.st_mtime <= ims) {
          req->response.result(304);
          req->response.headers.set("Last-Modified", last_modified_val);
          req->response.headers.set("Cache-Control", cfg.cache_control);
          return;
        }
      }
    }

    // ─── Derive MIME type from extension ─────────────────────────────────
    std::string ext;
    {
      auto dot = real_path.rfind('.');
      auto slash = real_path.rfind('/');
      if (dot != std::string::npos &&
          (slash == std::string::npos || dot > slash))
        ext = real_path.substr(dot);
    }
    std::string mime = get_mime_type(ext);

    // ─── ⑦ Range request (206 Partial Content) ───────────────────────────
    if (cfg.enable_range && fsize > 0) {
      std::string range_hdr = std::string(req->headers["Range"]);
      if (!range_hdr.empty() && range_hdr.compare(0, 6, "bytes=") == 0) {
        std::string spec = range_hdr.substr(6);
        auto dash = spec.find('-');
        if (dash != std::string::npos) {
          std::string s_start = spec.substr(0, dash);
          std::string s_end = spec.substr(dash + 1);
          long long range_start = 0, range_end = fsize - 1;
          bool range_valid = false;
          try {
            if (s_start.empty()) {
              // Suffix range: bytes=-N (last N bytes)
              long long n = std::stoll(s_end);
              if (n > 0) {
                range_start = (n >= fsize) ? 0 : fsize - n;
                range_end = fsize - 1;
                range_valid = true;
              }
            } else {
              range_start = std::stoll(s_start);
              range_end = s_end.empty() ? (fsize - 1) : std::stoll(s_end);
              if (range_start >= 0 && range_end < fsize &&
                  range_start <= range_end)
                range_valid = true;
            }
          } catch (...) {
          }

          if (range_valid) {
            long long range_len = range_end - range_start + 1;
            std::string body(static_cast<size_t>(range_len), '\0');

            int fd = open(real_path.c_str(), O_RDONLY);
            if (fd < 0) {
              req->response.result(500);
              req->response.body = "Internal Server Error";
              return;
            }
            bool ok = read_range(fd, &body[0], static_cast<off_t>(range_start),
                                 static_cast<size_t>(range_len));
            close(fd);

            if (!ok) {
              req->response.result(500);
              req->response.body = "Internal Server Error";
              return;
            }

            req->response.result(206);
            req->response.body = std::move(body);
            req->response.headers.set("Content-Type", mime);
            req->response.headers.set(
                "Content-Range", "bytes " + std::to_string(range_start) + "-" +
                                     std::to_string(range_end) + "/" +
                                     std::to_string(fsize));
            req->response.headers.set("Accept-Ranges", "bytes");
            if (cfg.enable_etag) req->response.headers.set("ETag", etag_val);
            if (cfg.enable_last_modified)
              req->response.headers.set("Last-Modified", last_modified_val);
            req->response.headers.set("Cache-Control", cfg.cache_control);
            return;
          }
        }
      }
    }

    // ─── ⑧ Full file read ─────────────────────────────────────────────────
    std::string body;
    if (fsize > 0) {
      body.resize(static_cast<size_t>(fsize));
      int fd = open(real_path.c_str(), O_RDONLY);
      if (fd < 0) {
        req->response.result(500);
        req->response.body = "Internal Server Error";
        return;
      }
      bool ok = read_range(fd, &body[0], 0, static_cast<size_t>(fsize));
      close(fd);

      if (!ok) {
        req->response.result(500);
        req->response.body = "Internal Server Error";
        return;
      }
    }

    // ─── ⑨ Build 200 response ─────────────────────────────────────────────
    req->response.result(200);
    req->response.body = std::move(body);
    req->response.headers.set("Content-Type", mime);
    req->response.headers.set("Accept-Ranges", "bytes");
    if (cfg.enable_etag) req->response.headers.set("ETag", etag_val);
    if (cfg.enable_last_modified)
      req->response.headers.set("Last-Modified", last_modified_val);
    req->response.headers.set("Cache-Control", cfg.cache_control);
  };
}

}  // namespace internal
}  // namespace asyik
