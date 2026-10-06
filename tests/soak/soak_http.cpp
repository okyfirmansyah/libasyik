// HTTP, HTTPS and WebSocket scenarios: mixed request load on multi-threaded
// servers, misbehaving raw-socket clients, and WebSocket sessions closed in
// every possible way.

#include <filesystem>
#include <fstream>

#include "libasyik/http.hpp"
#include "libasyik/internal/use_fiber_future.hpp"
#include "soak.hpp"

namespace fibers = boost::fibers;
namespace asio = boost::asio;
using asio::ip::tcp;
using asyik::http_request_ptr;
using asyik::http_route_args;
using asyik::service_ptr;
using asyik::use_fiber_future;
using asyik::websocket_ptr;
using namespace std::chrono_literals;

namespace {

std::string cert_path(const std::string& name)
{
  return std::string(LIBASYIK_TEST_CERT_DIR) + "/" + name;
}

asyik::tls::server_config server_tls()
{
  asyik::tls::server_config cfg;
  cfg.cert_file = cert_path("server.crt");
  cfg.key_file = cert_path("server.key");
  return cfg;
}

asyik::tls::client_context_ptr client_tls()
{
  static auto ctx = []() {
    asyik::tls::client_config cfg;
    cfg.use_system_ca = false;
    cfg.ca_file = cert_path("ca.crt");
    return asyik::tls::make_client_context(cfg);
  }();
  return ctx;
}

struct static_files {
  std::string dir;
  std::vector<std::string> contents;

  explicit static_files(std::mt19937_64& rng)
  {
    dir = (std::filesystem::temp_directory_path() /
           ("asyik_soak_" + std::to_string(rng())))
              .string();
    std::filesystem::create_directories(dir);
    for (int i = 0; i < 8; i++) {
      contents.push_back(soak::make_payload(rng(), 1 + rng() % (256 * 1024)));
      std::ofstream(dir + "/f" + std::to_string(i) + ".bin", std::ios::binary)
          << contents.back();
    }
  }
  ~static_files()
  {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

template <typename Server>
void add_routes(Server server, const static_files& files)
{
  server->on_http_request("/echo", "POST",
                          [](http_request_ptr req, const http_route_args&) {
                            req->response.body = req->body;
                            req->response.result(200);
                          });
  server->on_http_request(
      "/gen/<int>/<int>", "GET",
      [](http_request_ptr req, const http_route_args& args) {
        req->response.body =
            soak::make_payload(std::stoull(args[1]), std::stoul(args[2]));
        req->response.result(200);
      });
  server->on_http_request(
      "/async/<int>", "GET",
      [](http_request_ptr req, const http_route_args& args) {
        uint64_t key = std::stoull(args[1]);
        auto sum = asyik::get_current_service()
                       ->async([key]() {
                         return soak::checksum(soak::make_payload(key, 4096));
                       })
                       .get();
        req->response.body = std::to_string(sum);
        req->response.result(200);
      });
  server->on_http_request(
      "/sleep/<int>", "GET",
      [](http_request_ptr req, const http_route_args& args) {
        asyik::sleep_for(std::chrono::milliseconds(std::stoi(args[1])));
        req->response.body = "slept";
        req->response.result(200);
      });
  server->on_http_request("/throw", "GET",
                          [](http_request_ptr, const http_route_args&) {
                            throw std::runtime_error("handler failure");
                          });
  server->serve_static("/static", files.dir);
}

// One random request with its expected outcome checked.
void random_request(soak::context& ctx, std::mt19937_64& rng, service_ptr as,
                    const std::string& base, bool tls,
                    const static_files& files)
{
  auto tls_ctx = tls ? client_tls() : nullptr;
  auto get = [&](const std::string& path,
                 const std::map<boost::string_view, boost::string_view>&
                     headers = {}) {
    return asyik::http_easy_request(as, tls_ctx, 10000, "GET", base + path, "",
                                    headers);
  };
  int kind = rng() % 9;
  switch (kind) {
    case 0: {
      std::string body = soak::make_payload(rng(), rng() % (256 * 1024));
      auto req = asyik::http_easy_request(as, tls_ctx, 10000, "POST",
                                          base + "/echo", body, {});
      SOAK_CHECK(ctx, req->response.result() == 200, "echo status");
      SOAK_CHECK(ctx, req->response.body == body, "echo body differs");
      ctx.count("echo");
      break;
    }
    case 1: {
      uint64_t key = rng() % 1000000000;
      size_t n = rng() % (512 * 1024);
      auto req = get("/gen/" + std::to_string(key) + "/" + std::to_string(n));
      SOAK_CHECK(ctx, req->response.result() == 200, "gen status");
      SOAK_CHECK(ctx, req->response.body == soak::make_payload(key, n),
                 "generated body differs");
      ctx.count("generated");
      break;
    }
    case 2: {
      uint64_t key = rng() % 1000000000;
      auto req = get("/async/" + std::to_string(key));
      SOAK_CHECK(ctx,
                 req->response.body == std::to_string(soak::checksum(
                                           soak::make_payload(key, 4096))),
                 "async route result differs");
      ctx.count("async route");
      break;
    }
    case 3: {
      auto req = get("/sleep/" + std::to_string(rng() % 50));
      SOAK_CHECK(ctx, req->response.body == "slept", "sleep route");
      ctx.count("sleep route");
      break;
    }
    case 4: {
      auto req = get("/throw");
      SOAK_CHECK(ctx, req->response.result() == 500, "throwing handler");
      ctx.count("500");
      break;
    }
    case 5: {
      auto req = get("/no/such/route");
      SOAK_CHECK(ctx, req->response.result() == 404, "unknown route");
      ctx.count("404");
      break;
    }
    case 6:
    case 7: {
      int i = rng() % files.contents.size();
      const auto& content = files.contents[i];
      std::string path = "/static/f" + std::to_string(i) + ".bin";
      if (kind == 6) {
        auto req = get(path);
        SOAK_CHECK(ctx, req->response.result() == 200, "static status");
        SOAK_CHECK(ctx, req->response.body == content, "static body differs");
        ctx.count("static");
      } else {
        size_t a = rng() % content.size();
        size_t b = a + rng() % (content.size() - a);
        std::string range =
            "bytes=" + std::to_string(a) + "-" + std::to_string(b);
        auto req = get(path, {{"Range", range}});
        SOAK_CHECK(ctx, req->response.result() == 206, "range status");
        SOAK_CHECK(ctx, req->response.body == content.substr(a, b - a + 1),
                   "range body differs");
        ctx.count("static range");
      }
      break;
    }
    case 8: {
      // the client gives up long before the handler answers
      try {
        asyik::http_easy_request(as, tls_ctx, 100, "GET", base + "/sleep/600",
                                 "", {});
        ctx.fail("request should have timed out");
      } catch (soak::failure&) {
        throw;
      } catch (std::exception&) {
        ctx.count("client timeouts");
      }
      break;
    }
  }
  ctx.progress();
}

void mixed_load(soak::context& ctx, bool tls)
{
  const int n_server = 2, n_client = 2, fibers_per_client = tls ? 12 : 24,
            requests_per_fiber = 20;
  std::vector<std::unique_ptr<soak::service_thread>> server_threads,
      client_threads;
  for (int i = 0; i < n_server; i++)
    server_threads.emplace_back(new soak::service_thread);
  for (int i = 0; i < n_client; i++)
    client_threads.emplace_back(new soak::service_thread);
  auto rng = ctx.rng(0);
  static_files files(rng);

  for (uint64_t round = 0; ctx.time_left(); round++) {
    ctx.phase("starting servers");
    uint16_t port = soak::next_port();
    // SO_REUSEPORT: the kernel spreads connections over both threads
    std::vector<std::function<void()>> closers;
    for (auto& st : server_threads) {
      auto as = st->get();
      closers.push_back(soak::run_on(as, [&]() -> std::function<void()> {
        if (tls) {
          auto s = asyik::make_https_server(as, server_tls(), "127.0.0.1", port,
                                            true);
          add_routes(s, files);
          return [s]() { s->close(); };
        }
        auto s = asyik::make_http_server(as, "127.0.0.1", port, true);
        add_routes(s, files);
        return [s]() { s->close(); };
      }));
    }

    ctx.phase("requests");
    std::string base = std::string(tls ? "https" : "http") +
                       "://127.0.0.1:" + std::to_string(port);
    std::vector<fibers::future<void>> running;
    for (int c = 0; c < n_client * fibers_per_client; c++) {
      auto as = client_threads[c % n_client]->get();
      running.push_back(as->execute([&, as, stream = round * 256 + c]() {
        auto r = ctx.rng(stream);
        for (int i = 0; i < requests_per_fiber && ctx.time_left(); i++)
          random_request(ctx, r, as, base, tls, files);
      }));
    }
    for (auto& f : running) f.get();

    ctx.phase("closing servers");
    for (size_t i = 0; i < closers.size(); i++)
      soak::run_on(server_threads[i]->get(), closers[i]);
  }
  for (auto& t : server_threads) t->stop_and_join(ctx);
  for (auto& t : client_threads) t->stop_and_join(ctx);
}

}  // namespace

SOAK_SCENARIO(http_mixed, "http.mixed_load",
              "random requests (echo, generated bodies, static files and "
              "ranges, async() in handlers, 404/500, client timeouts) against "
              "two SO_REUSEPORT server threads")
{
  mixed_load(ctx, false);
}

SOAK_SCENARIO(https_mixed, "https.mixed_load",
              "the http.mixed_load request mix over TLS")
{
  mixed_load(ctx, true);
}

namespace {

// Reads until the peer closes or timeout passes; returns what was received.
std::string read_all(service_ptr as, tcp::socket& sock,
                     std::chrono::milliseconds timeout, bool* timed_out)
{
  // shared with the timer handler, which may still be queued after we return
  struct state {
    bool alive = true, expired = false;
    tcp::socket* sock;
  };
  auto st = std::make_shared<state>();
  st->sock = &sock;
  asio::steady_timer timer(as->get_io_service(), timeout);
  timer.async_wait([st](boost::system::error_code ec) {
    if (!ec && st->alive) {
      st->expired = true;
      boost::system::error_code ignored;
      st->sock->cancel(ignored);
    }
  });
  std::string got;
  char buf[4096];
  try {
    while (true)
      got.append(
          buf, sock.async_read_some(asio::buffer(buf), use_fiber_future).get());
  } catch (boost::system::system_error&) {
  }
  st->alive = false;
  if (timed_out) *timed_out = st->expired;
  return got;
}

int count_of(const std::string& s, const std::string& what)
{
  int n = 0;
  for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1))
    n++;
  return n;
}

}  // namespace

SOAK_SCENARIO(hostile, "http.hostile_clients",
              "raw-socket clients that disconnect early, send partial or "
              "oversized requests, trickle bytes, pipeline or half-close, "
              "while the server must stay responsive")
{
  soak::service_thread server_thread, client_thread, probe_thread;
  auto server_as = server_thread.get(), client_as = client_thread.get(),
       probe_as = probe_thread.get();

  for (uint64_t round = 0; ctx.time_left(); round++) {
    uint16_t port = soak::next_port();
    auto server = soak::run_on(server_as, [&]() {
      auto s = asyik::make_http_server(server_as, "127.0.0.1", port);
      s->set_request_body_limit(64 * 1024);
      s->on_http_request("/ok",
                         [](http_request_ptr req, const http_route_args&) {
                           req->response.body = "ok";
                           req->response.result(200);
                         });
      return s;
    });

    // probe: a well-behaved client must keep getting fast answers
    std::atomic<bool> probing{true};
    auto probe = probe_as->execute([&, probe_as, port]() {
      while (probing) {
        auto start = soak::clock::now();
        auto req = asyik::http_easy_request(
            probe_as, 5000, "GET",
            "http://127.0.0.1:" + std::to_string(port) + "/ok", "", {});
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      soak::clock::now() - start)
                      .count();
        SOAK_CHECK(ctx, req->response.result() == 200, "probe status");
        SOAK_CHECK(ctx, ms < 1000, "probe took " + std::to_string(ms) + "ms");
        ctx.count("probe requests");
        asyik::sleep_for(20ms);
      }
    });

    ctx.phase("hostile clients");
    std::vector<fibers::future<void>> clients;
    for (int c = 0; c < 64; c++)
      clients.push_back(client_as->execute([&, client_as, port,
                                            stream = round * 128 + c]() {
        auto rng = ctx.rng(stream);
        tcp::socket sock(client_as->get_io_service());
        sock.async_connect({asio::ip::make_address("127.0.0.1"), port},
                           use_fiber_future)
            .get();
        auto send = [&](const std::string& s) {
          asio::async_write(sock, asio::buffer(s), use_fiber_future).get();
        };
        const std::string get_ok = "GET /ok HTTP/1.1\r\nHost: x\r\n\r\n";
        switch (rng() % 8) {
          case 0:  // connect and leave
            ctx.count("drop after connect");
            break;
          case 1:  // partial header
            send("GET /ok HTTP/1.1\r\nHo");
            ctx.count("partial header");
            break;
          case 2:  // body shorter than announced
            send(
                "POST /ok HTTP/1.1\r\nHost: x\r\nContent-Length: 1000\r\n\r\n"
                "0123456789");
            ctx.count("truncated body");
            break;
          case 3: {  // slowloris: one byte at a time, then give up
            for (char ch : get_ok.substr(0, 20)) {
              send(std::string(1, ch));
              asyik::sleep_for(std::chrono::milliseconds(rng() % 40));
            }
            ctx.count("slowloris");
            break;
          }
          case 4: {  // larger than the body limit
            // the server answers 413 and closes while we are still sending,
            // so the upload may fail with a reset; then the 413 can be lost
            try {
              send(
                  "POST /ok HTTP/1.1\r\nHost: x\r\nContent-Length: "
                  "100000\r\n\r\n" +
                  std::string(100000, 'x'));
            } catch (boost::system::system_error&) {
              ctx.count("413 (reset during upload)");
              break;
            }
            bool timed_out;
            auto resp = read_all(client_as, sock, 3000ms, &timed_out);
            SOAK_CHECK(ctx, !timed_out, "no answer to an oversized body");
            SOAK_CHECK(
                ctx, resp.find(" 413 ") != std::string::npos,
                "oversized body not answered with 413: " + resp.substr(0, 40));
            ctx.count("413");
            break;
          }
          case 5: {  // garbage
            send("HELLO THERE\r\n\r\n");
            bool timed_out;
            auto resp = read_all(client_as, sock, 3000ms, &timed_out);
            SOAK_CHECK(ctx, !timed_out, "garbage request left open");
            SOAK_CHECK(ctx, resp.find(" 200 ") == std::string::npos,
                       "garbage request answered with 200");
            ctx.count("garbage");
            break;
          }
          case 6: {  // pipelined requests, then close
            send(get_ok + get_ok + get_ok +
                 "GET /ok HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
            bool timed_out;
            auto resp = read_all(client_as, sock, 3000ms, &timed_out);
            SOAK_CHECK(ctx, !timed_out, "pipelined connection not closed");
            SOAK_CHECK(ctx, count_of(resp, " 200 ") == 4,
                       "pipelined responses: " +
                           std::to_string(count_of(resp, " 200 ")));
            ctx.count("pipelined");
            break;
          }
          case 7: {  // half-close after a complete request
            send(get_ok);
            sock.shutdown(tcp::socket::shutdown_send);
            bool timed_out;
            auto resp = read_all(client_as, sock, 3000ms, &timed_out);
            SOAK_CHECK(ctx, resp.find(" 200 ") != std::string::npos,
                       "no response after half-close");
            ctx.count("half-close");
            break;
          }
        }
        boost::system::error_code ec;
        sock.close(ec);
        ctx.progress();
      }));
    for (auto& f : clients) f.get();
    probing = false;
    probe.get();
    soak::run_on(server_as, [&]() { server->close(); });
  }
  server_thread.stop_and_join(ctx);
  client_thread.stop_and_join(ctx);
  probe_thread.stop_and_join(ctx);
}

SOAK_SCENARIO(ws_sessions, "ws.sessions",
              "concurrent ws:// and wss:// echo sessions (text and binary) "
              "ended by the server, by the client, or by dropping the "
              "connection; every server handler must finish")
{
  soak::service_thread server_thread, client_a, client_b;
  auto server_as = server_thread.get();
  std::vector<service_ptr> clients{client_a.get(), client_b.get()};
  auto active = std::make_shared<std::atomic<int>>(0);

  // /ws/<limit>/<mode>: echo, closing after <limit> messages (0 = never)
  auto handler = [active](websocket_ptr ws, const http_route_args& args) {
    struct guard {
      std::shared_ptr<std::atomic<int>> n;
      ~guard() { (*n)--; }
    } g{active};
    (*active)++;
    int limit = std::stoi(args[1]);
    bool binary = args[2] == "bin";
    try {
      for (int count = 1;; count++) {
        if (binary) {
          std::vector<uint8_t> b(70000);
          b.resize(ws->read_basic_buffer(b));
          ws->write_basic_buffer(b);
        } else {
          ws->send_string(ws->get_string());
        }
        if (limit && count == limit) {
          ws->close(asyik::websocket_close_code::normal, "limit");
          return;
        }
      }
    } catch (std::exception&) {
      // client went away
    }
  };

  for (uint64_t round = 0; ctx.time_left(); round++) {
    uint16_t port = soak::next_port(), tls_port = soak::next_port();
    auto closers = soak::run_on(server_as, [&]() {
      auto s = asyik::make_http_server(server_as, "127.0.0.1", port);
      auto t = asyik::make_https_server(server_as, server_tls(), "127.0.0.1",
                                        tls_port);
      s->on_websocket("/ws/<int>/<string>", handler);
      t->on_websocket("/ws/<int>/<string>", handler);
      return std::function<void()>([s, t]() {
        s->close();
        t->close();
      });
    });

    ctx.phase("sessions");
    std::vector<fibers::future<void>> sessions;
    for (int c = 0; c < 48; c++) {
      auto as = clients[c % clients.size()];
      sessions.push_back(as->execute([&, as, stream = round * 128 + c]() {
        auto rng = ctx.rng(stream);
        bool tls = rng() % 3 == 0, binary = rng() % 2;
        int limit = rng() % 3 ? 0 : 1 + rng() % 10;
        int messages = 1 + rng() % 15;
        std::string url = std::string(tls ? "wss" : "ws") + "://127.0.0.1:" +
                          std::to_string(tls ? tls_port : port) + "/ws/" +
                          std::to_string(limit) + (binary ? "/bin" : "/text");
        auto ws = asyik::make_websocket_connection(
            as, tls ? client_tls() : nullptr, url);
        SOAK_CHECK(ctx, ws != nullptr, "websocket connect");
        for (int m = 1; m <= messages; m++) {
          std::string msg = soak::make_payload(rng(), 1 + rng() % 65536);
          bool server_closed = limit && m > limit;
          try {
            std::string got;
            if (binary) {
              ws->write_basic_buffer(
                  std::vector<uint8_t>(msg.begin(), msg.end()));
              // read_basic_buffer() fills a caller-sized buffer
              std::vector<uint8_t> b(70000);
              b.resize(ws->read_basic_buffer(b));
              got.assign(b.begin(), b.end());
            } else {
              ws->send_string(msg);
              got = ws->get_string();
            }
            SOAK_CHECK(ctx, !server_closed, "echo after the server closed");
            SOAK_CHECK(ctx, got == msg, "websocket echo differs");
            ctx.count("messages");
          } catch (soak::failure&) {
            throw;
          } catch (std::exception& e) {
            SOAK_CHECK(ctx, server_closed,
                       std::string("websocket error: ") + e.what());
            ctx.count("closed by server");
            return;
          }
          ctx.progress();
        }
        if (rng() % 2) {
          ws->close(asyik::websocket_close_code::normal, "done");
          ctx.count("closed by client");
        } else {
          ctx.count("dropped by client");  // ws destroyed without close
        }
      }));
    }
    for (auto& f : sessions) f.get();

    ctx.phase("waiting for server handlers to end");
    auto deadline = soak::clock::now() + 5s;
    while (*active > 0 && soak::clock::now() < deadline)
      std::this_thread::sleep_for(10ms);
    SOAK_CHECK(
        ctx, *active == 0,
        std::to_string(active->load()) + " websocket handler(s) still running");
    soak::run_on(server_as, closers);
  }
  server_thread.stop_and_join(ctx);
  client_a.stop_and_join(ctx);
  client_b.stop_and_join(ctx);
}
