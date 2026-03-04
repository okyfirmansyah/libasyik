#ifndef LIBASYIK_ASYIK_HTTP_STATIC_HPP
#define LIBASYIK_ASYIK_HTTP_STATIC_HPP

#include <string>

#include "common.hpp"
#include "http_types.hpp"

namespace asyik {

/// Configuration for static file serving behaviour.
struct static_file_config {
  /// Compute and send an ETag header; honour If-None-Match → 304.
  bool enable_etag = true;
  /// Send a Last-Modified header; honour If-Modified-Since → 304.
  bool enable_last_modified = true;
  /// Honour "Range: bytes=A-B" and return 206 Partial Content.
  bool enable_range = true;
  /// Value for the Cache-Control response header.
  std::string cache_control = "public, max-age=3600";
  /// File served when the URL resolves to a directory.
  std::string index_file = "index.html";
};

namespace internal {

/// Returns the MIME type string for a file extension (including the leading
/// dot, e.g. ".html"). Falls back to "application/octet-stream" for unknown
/// extensions.
std::string get_mime_type(const std::string& ext);

/// Builds a quoted ETag value from a file's mtime and size.
std::string make_etag(long long mtime, long long size);

/// Creates a type-erased http_route_callback that serves files from
/// @p root_dir, stripping @p url_prefix from every request target.
/// All heavy logic (I/O, caching headers, range handling) stays in the .cpp.
http_route_callback make_static_file_handler(std::string url_prefix,
                                             std::string root_dir,
                                             static_file_config cfg);

}  // namespace internal
}  // namespace asyik

#endif  // LIBASYIK_ASYIK_HTTP_STATIC_HPP
