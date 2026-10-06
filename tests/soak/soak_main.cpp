// libasyik soak runner. See tests/soak/README.md.

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "soak.hpp"

namespace soak {

std::vector<scenario>& registry()
{
  static std::vector<scenario> r;
  return r;
}

resources sample_resources()
{
  resources r;
  if (DIR* d = opendir("/proc/self/fd")) {
    while (readdir(d)) r.fds++;
    closedir(d);
    r.fds -= 3;  // ".", ".." and the DIR's own descriptor
  }
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("Threads:", 0) == 0) r.threads = std::stol(line.substr(8));
    if (line.rfind("VmRSS:", 0) == 0) r.rss_kb = std::stol(line.substr(6));
  }
  return r;
}

std::string make_payload(uint64_t key, size_t n)
{
  std::string s(n, '\0');
  uint64_t x = key * 0x9E3779B97F4A7C15ull + 1;
  for (size_t i = 0; i < n; i++) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s[i] = static_cast<char>('a' + x % 26);
  }
  return s;
}

uint64_t checksum(const std::string& s)
{
  uint64_t h = 1469598103934665603ull;  // FNV-1a
  for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
  return h;
}

// Warnings logged by libasyik, counted per scenario. A forced exit means a
// fiber was still alive when its service stopped: always a failure.
static std::atomic<uint64_t> log_warnings{0}, forced_exits{0};

static void install_log_counter()
{
  // the first service sets up the default console sink; add ours after it
  std::thread([]() {
    asyik::make_service()->set_default_log_severity(
        asyik::log_severity::warning);
  }).join();
  AixLog::Log::instance().add_logsink(std::make_shared<AixLog::SinkCallback>(
      AixLog::Filter(AixLog::Severity::warning),
      [](const AixLog::Metadata&, const std::string& message) {
        log_warnings++;
        if (message.find("forcing exit") != std::string::npos ||
            message.find("still active") != std::string::npos)
          forced_exits++;
      }));
}

static std::atomic<uint16_t> port_counter{0};
// below the Linux ephemeral range (32768+), where client sockets could hold it
static uint16_t port_base = 21100;

uint16_t next_port() { return port_base + (port_counter++ % 800); }

}  // namespace soak

namespace {

struct options {
  int duration_s = 30;
  uint64_t seed = 0;
  bool seed_given = false;
  std::vector<std::string> only;
  int stall_s = 60;
  bool list = false;
};

std::vector<std::string> split(const std::string& s)
{
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ','))
    if (!item.empty()) out.push_back(item);
  return out;
}

void usage()
{
  std::cout
      << "usage: libasyik_soak [options]\n"
         "  --duration=SECONDS   time per scenario (default 30)\n"
         "  --seed=N             random seed (default: random, printed)\n"
         "  --scenario=A,B       run only these (prefix match, e.g. http.)\n"
         "  --stall=SECONDS      abort when a scenario makes no progress "
         "this long (default 60)\n"
         "  --port-base=N        first TCP port to use (default 21100)\n"
         "  --list               list scenarios\n";
}

bool selected(const options& o, const std::string& name)
{
  if (o.only.empty()) return true;
  for (const auto& p : o.only)
    if (name.compare(0, p.size(), p) == 0) return true;
  return false;
}

}  // namespace

int main(int argc, char** argv)
{
  options o;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto value = [&](const char* key) -> const char* {
      size_t n = strlen(key);
      return a.compare(0, n, key) == 0 ? a.c_str() + n : nullptr;
    };
    if (auto v = value("--duration="))
      o.duration_s = std::atoi(v);
    else if (auto v = value("--seed=")) {
      o.seed = std::strtoull(v, nullptr, 10);
      o.seed_given = true;
    } else if (auto v = value("--scenario="))
      o.only = split(v);
    else if (auto v = value("--stall="))
      o.stall_s = std::atoi(v);
    else if (auto v = value("--port-base="))
      soak::port_base = static_cast<uint16_t>(std::atoi(v));
    else if (a == "--list")
      o.list = true;
    else {
      usage();
      return a == "--help" || a == "-h" ? 0 : 2;
    }
  }

  auto& all = soak::registry();
  std::sort(all.begin(), all.end(),
            [](const auto& a, const auto& b) { return a.name < b.name; });
  if (o.list) {
    for (const auto& s : all)
      std::cout << s.name << "\n    " << s.description << "\n";
    return 0;
  }
  if (!o.seed_given) o.seed = std::random_device{}();

  soak::install_log_counter();
  std::cout << "libasyik soak: seed=" << o.seed << " duration=" << o.duration_s
            << "s/scenario\n";

  // Watchdog: the scenario under test must keep reporting progress.
  std::atomic<soak::context*> current{nullptr};
  std::thread([&]() {
    uint64_t last = 0;
    auto last_change = soak::clock::now();
    soak::context* last_ctx = nullptr;
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      auto* ctx = current.load();
      if (!ctx) continue;
      uint64_t p = ctx->progress_count();
      if (ctx != last_ctx || p != last) {
        last = p;
        last_ctx = ctx;
        last_change = soak::clock::now();
      } else if (soak::clock::now() - last_change >
                 std::chrono::seconds(o.stall_s)) {
        std::cout << "\nSTALL: " << ctx->name << " made no progress for "
                  << o.stall_s << "s, phase: " << ctx->current_phase()
                  << "\nreproduce: libasyik_soak --scenario=" << ctx->name
                  << " --seed=" << o.seed << " --duration=" << o.duration_s
                  << std::endl;
        std::_Exit(3);
      }
    }
  }).detach();

  int failed = 0, ran = 0;
  for (const auto& s : all) {
    if (!selected(o, s.name)) continue;
    ran++;
    soak::context ctx(s.name, o.seed, std::chrono::seconds(o.duration_s));
    std::cout << "\n== " << s.name << "\n" << std::flush;

    auto before = soak::sample_resources();
    uint64_t warnings_before = soak::log_warnings,
             forced_before = soak::forced_exits;
    auto start = soak::clock::now();
    current = &ctx;
    try {
      s.run(ctx);
    } catch (soak::failure&) {
    } catch (std::exception& e) {
      try {
        ctx.fail(std::string("uncaught exception: ") + e.what());
      } catch (soak::failure&) {
      }
    }
    current = nullptr;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       soak::clock::now() - start)
                       .count();

    // Descriptors closed by finished fibers can lag a little behind.
    auto after = soak::sample_resources();
    for (int i = 0; i < 40 && after.fds > before.fds; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      after = soak::sample_resources();
    }
    uint64_t forced = soak::forced_exits - forced_before;
    if (!ctx.failed() && forced) {
      try {
        ctx.fail(std::to_string(forced) +
                 " service stop(s) found fibers still running (see the "
                 "\"still active\" warnings above)");
      } catch (soak::failure&) {
      }
    }
    if (!ctx.failed() && after.fds > before.fds) {
      try {
        ctx.fail("file descriptor leak: " + std::to_string(before.fds) +
                 " -> " + std::to_string(after.fds));
      } catch (soak::failure&) {
      }
    }

    for (const auto& c : ctx.counters())
      std::cout << "   " << c.first << ": " << c.second << "\n";
    if (uint64_t w = soak::log_warnings - warnings_before)
      std::cout << "   libasyik warnings logged: " << w << "\n";
    std::cout << "   fds " << before.fds << " -> " << after.fds << ", threads "
              << before.threads << " -> " << after.threads << ", rss "
              << before.rss_kb / 1024 << " -> " << after.rss_kb / 1024
              << " MB, " << elapsed << " ms\n";
    if (ctx.failed()) {
      failed++;
      std::cout << "   FAILED: " << ctx.failure_message()
                << "\n   reproduce: libasyik_soak --scenario=" << s.name
                << " --seed=" << o.seed << " --duration=" << o.duration_s
                << "\n";
    } else if (!ctx.skipped().empty()) {
      std::cout << "   SKIPPED: " << ctx.skipped() << "\n";
    } else {
      std::cout << "   ok\n";
    }
    std::cout << std::flush;
  }

  if (ran == 0) {
    std::cout << "no scenario matches; see --list\n";
    return 2;
  }
  std::cout << "\n"
            << ran - failed << "/" << ran << " scenarios passed (seed "
            << o.seed << ")\n"
            << std::flush;
  // Skip static destructors: a failed scenario may have left threads behind.
  std::_Exit(failed ? 1 : 0);
}
