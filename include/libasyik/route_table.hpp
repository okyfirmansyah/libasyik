#ifndef LIBASYIK_ASYIK_ROUTE_TABLE_HPP
#define LIBASYIK_ASYIK_ROUTE_TABLE_HPP

#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <regex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "error.hpp"
#include "http_types.hpp"

namespace asyik {

/// Extract the static prefix from a route spec — the portion before the first
/// parameter tag (<int>, <string>, <path>).  Returns the full spec (minus
/// trailing slash) if there are no tags.
///
/// Examples:
///   "/api/v1/users"             → "/api/v1/users"
///   "/api/v1/users/<int>"       → "/api/v1/users/"
///   "/api/v1/<string>/items"    → "/api/v1/"
///   "/<int>"                    → "/"
inline std::string extract_static_prefix(string_view route_spec)
{
  std::string s{route_spec.data(), route_spec.size()};

  // Find first '<' — everything before it is the static prefix
  auto pos = s.find('<');
  if (pos == std::string::npos) {
    // No parameters — this is a fully static route
    // Trim trailing slash for normalisation
    if (!s.empty() && s.back() == '/') s.pop_back();
    return s;
  }

  // Return everything up to and including the last '/' before '<'
  std::string prefix = s.substr(0, pos);
  // prefix might end with '/' already; keep it so that prefix matching works
  return prefix;
}

/// Returns true if @p route_spec contains no parameter tags (<int>, <string>,
/// <path>), meaning it can be matched by exact string comparison.
inline bool is_static_route(string_view route_spec)
{
  return route_spec.find('<') == string_view::npos;
}

/// Normalise a URL path for exact-match lookup: strip query string and
/// optional trailing slash.
inline std::string normalise_path(string_view target)
{
  std::string s{target.data(), target.size()};

  // Strip query string
  auto qpos = s.find('?');
  if (qpos != std::string::npos) s.resize(qpos);

  // Strip trailing slash (but keep "/" itself)
  if (s.size() > 1 && s.back() == '/') s.pop_back();

  return s;
}

/// Three-tier route table that avoids regex for static routes and narrows
/// candidates by prefix for parameterized routes.
///
/// Tier 1: exact-match map     — O(1) for routes with no parameters
/// Tier 2: prefix-sorted list  — binary search on static prefix, then regex
/// Tier 3: fallback list       — linear scan with regex (raw regex routes)
///
/// The original insertion order is preserved within each tier, and routes
/// registered with insert_front=true go to the front of their tier.
template <typename RouteType>
class route_table {
 public:
  /// Register a route that was created from a route_spec (has known structure).
  /// @p route_spec is the original user-provided spec (e.g. "/api/v1/<int>")
  /// @p route is the fully constructed RouteType (method, compiled regex, cb)
  /// @p insert_front if true, inserts at front of the relevant tier
  void add_route(string_view route_spec, RouteType&& route,
                 bool insert_front = false)
  {
    if (is_static_route(route_spec)) {
      // Tier 1: exact match
      std::string key = normalise_path(route_spec);
      auto& vec = exact_routes_[key];
      if (insert_front)
        vec.insert(vec.begin(), std::move(route));
      else
        vec.push_back(std::move(route));
    } else {
      // Tier 2: prefix + regex
      std::string prefix = extract_static_prefix(route_spec);
      prefix_entry entry{std::move(prefix), std::move(route)};
      if (insert_front)
        prefix_routes_.insert(prefix_routes_.begin(), std::move(entry));
      else
        prefix_routes_.push_back(std::move(entry));
    }
  }

  /// Register a raw regex route (no known structure — fallback tier 3).
  void add_regex_route(RouteType&& route, bool insert_front = false)
  {
    if (insert_front)
      fallback_routes_.insert(fallback_routes_.begin(), std::move(route));
    else
      fallback_routes_.push_back(std::move(route));
  }

  /// Find the first matching route for request @p req, populating @p args.
  /// Tries tiers in order: exact → prefix → fallback.
  /// Throws not_found_error if no route matches.
  template <typename ReqType>
  const RouteType& find(const ReqType& req, http_route_args& args) const
  {
    std::string target{req.target()};
    std::string path =
        normalise_path(string_view(target.data(), target.size()));

    // ── Tier 1: exact match ──
    auto it = exact_routes_.find(path);
    if (it != exact_routes_.end()) {
      for (const auto& route : it->second) {
        const auto& method = std::get<0>(route);
        if (method.empty() || boost::iequals(method, req.method_string())) {
          // For static routes, args[0] = full match = the path itself
          args.clear();
          args.push_back(path);
          return route;
        }
      }
    }

    // ── Tier 2: prefix candidates + regex ──
    for (const auto& entry : prefix_routes_) {
      // Quick prefix check before expensive regex
      if (target.size() >= entry.prefix.size() &&
          target.compare(0, entry.prefix.size(), entry.prefix) == 0) {
        const auto& route = entry.route;
        const auto& method = std::get<0>(route);
        if (method.empty() || boost::iequals(method, req.method_string())) {
          std::smatch m;
          if (std::regex_search(target, m, std::get<1>(route))) {
            args.clear();
            for (const auto& item : m) args.push_back(item.str());
            return route;
          }
        }
      }
    }

    // ── Tier 3: fallback (raw regex routes) ──
    for (const auto& route : fallback_routes_) {
      const auto& method = std::get<0>(route);
      if (method.empty() || boost::iequals(method, req.method_string())) {
        std::smatch m;
        if (std::regex_search(target, m, std::get<1>(route))) {
          args.clear();
          for (const auto& item : m) args.push_back(item.str());
          return route;
        }
      }
    }

    throw not_found_error("route not found");
  }

  /// Access the underlying vectors (for backward compatibility / iteration)
  bool empty() const
  {
    return exact_routes_.empty() && prefix_routes_.empty() &&
           fallback_routes_.empty();
  }

 private:
  struct prefix_entry {
    std::string prefix;
    RouteType route;
  };

  // Tier 1: path → vector of routes (multiple methods on same path)
  std::unordered_map<std::string, std::vector<RouteType>> exact_routes_;

  // Tier 2: routes with known static prefix (parameterized paths)
  std::vector<prefix_entry> prefix_routes_;

  // Tier 3: raw regex routes with no known structure
  std::vector<RouteType> fallback_routes_;
};

}  // namespace asyik

#endif
