// Raw Boost.Asio I/O driven from fibers with asyik::use_fiber_future: TCP
// echo with partial writes and aborting clients, timer storms with
// cancellation, UDP and the resolver.

#include <boost/asio.hpp>

#include "libasyik/internal/use_fiber_future.hpp"
#include "soak.hpp"

namespace fibers = boost::fibers;
namespace asio = boost::asio;
using asio::ip::tcp;
using asio::ip::udp;
using asyik::service_ptr;
using asyik::use_fiber_future;
using namespace std::chrono_literals;

namespace {

bool is_aborted(const boost::system::system_error& e)
{
  return e.code() == asio::error::operation_aborted;
}

// Echo server: every accepted connection is echoed back by its own fiber
// until the peer goes away. active counts live connection fibers.
struct echo_server {
  std::shared_ptr<tcp::acceptor> acceptor;
  std::shared_ptr<std::atomic<int>> active =
      std::make_shared<std::atomic<int>>(0);

  echo_server(service_ptr as, uint16_t port)
  {
    acceptor = std::make_shared<tcp::acceptor>(as->get_io_service());
    tcp::endpoint ep(asio::ip::make_address("127.0.0.1"), port);
    acceptor->open(ep.protocol());
    acceptor->set_option(asio::socket_base::reuse_address(true));
    acceptor->bind(ep);
    acceptor->listen(1024);
    as->execute([as, acc = acceptor, active = active]() {
      while (true) {
        auto sock = std::make_shared<tcp::socket>(as->get_io_service());
        try {
          acc->async_accept(*sock, use_fiber_future).get();
        } catch (boost::system::system_error&) {
          return;  // acceptor closed
        }
        (*active)++;
        as->execute([sock, active]() {
          std::vector<char> buf(64 * 1024);
          try {
            while (true) {
              size_t n =
                  sock->async_read_some(asio::buffer(buf), use_fiber_future)
                      .get();
              asio::async_write(*sock, asio::buffer(buf.data(), n),
                                use_fiber_future)
                  .get();
            }
          } catch (boost::system::system_error&) {
            // peer closed or reset
          }
          (*active)--;
        });
      }
    });
  }
};

}  // namespace

SOAK_SCENARIO(tcp_echo, "asio.tcp_echo",
              "TCP echo through use_fiber_future: messages up to 256KB "
              "written in random pieces while a second fiber reads them "
              "back; some clients abort mid-message")
{
  soak::service_thread server_thread, client_a, client_b;
  auto server_as = server_thread.get();
  std::vector<service_ptr> clients{client_a.get(), client_b.get()};

  for (uint64_t round = 0; ctx.time_left(); round++) {
    ctx.phase("starting echo server");
    uint16_t port = soak::next_port();
    auto server =
        soak::run_on(server_as, [&]() { return echo_server(server_as, port); });

    ctx.phase("echo clients");
    std::vector<fibers::future<void>> running;
    for (int c = 0; c < 48; c++) {
      auto as = clients[c % clients.size()];
      running.push_back(as->execute([&ctx, as, port,
                                     stream = round * 64 + c]() {
        auto rng = ctx.rng(stream);
        auto sock = std::make_shared<tcp::socket>(as->get_io_service());
        sock->async_connect({asio::ip::make_address("127.0.0.1"), port},
                            use_fiber_future)
            .get();
        int messages = 1 + rng() % 8;
        for (int m = 0; m < messages; m++) {
          std::string msg = soak::make_payload(rng(), 1 + rng() % (256 * 1024));
          bool abort = rng() % 12 == 0;

          // reader: collects exactly msg.size() bytes
          auto reader = as->execute([sock, n = msg.size()]() {
            std::string got(n, '\0');
            asio::async_read(*sock, asio::buffer(&got[0], n), use_fiber_future)
                .get();
            return got;
          });
          size_t off = 0;
          while (off < msg.size()) {
            size_t piece =
                (std::min)(msg.size() - off, size_t(1 + rng() % (64 * 1024)));
            asio::async_write(*sock, asio::buffer(msg.data() + off, piece),
                              use_fiber_future)
                .get();
            off += piece;
            if (abort && off > msg.size() / 2) break;
          }
          if (abort) {
            boost::system::error_code ec;
            sock->close(ec);  // the reader fails with operation_aborted
            try {
              reader.get();
            } catch (boost::system::system_error&) {
            }
            ctx.count("aborted connections");
            ctx.progress();
            return;
          }
          SOAK_CHECK(ctx, reader.get() == msg, "echoed bytes differ");
          ctx.count("messages");
          ctx.count("bytes", msg.size());
          ctx.progress();
        }
        ctx.count("connections");
      }));
    }
    for (auto& f : running) f.get();

    // every server connection fiber must notice its client is gone
    ctx.phase("waiting for server connections to end");
    auto deadline = soak::clock::now() + 5s;
    while (*server.active > 0 && soak::clock::now() < deadline)
      std::this_thread::sleep_for(10ms);
    SOAK_CHECK(ctx, *server.active == 0,
               std::to_string(server.active->load()) +
                   " server connection fiber(s) did not end");
    soak::run_on(server_as, [&]() {
      boost::system::error_code ec;
      server.acceptor->close(ec);
    });
  }
  for (auto* t : {&server_thread, &client_a, &client_b}) t->stop_and_join(ctx);
}

SOAK_SCENARIO(timers, "asio.timers",
              "storms of steady_timer waits with random cancellation, the "
              "packaged-function form, and timers of another service's "
              "io_context")
{
  soak::service_thread a_thread, b_thread;
  auto a = a_thread.get(), b = b_thread.get();

  for (uint64_t round = 0; ctx.time_left(); round++) {
    ctx.phase("timer storm");
    auto rng = ctx.rng(round);
    struct timer_state {
      std::unique_ptr<asio::steady_timer> t;
      std::atomic<bool> cancelled{false};
    };
    const int n = 400;
    auto timers = std::make_shared<std::vector<timer_state>>(n);
    std::vector<fibers::future<void>> waiting;

    soak::run_on(a, [&]() {
      for (int i = 0; i < n; i++) {
        auto& ts = (*timers)[i];
        ts.t = std::make_unique<asio::steady_timer>(
            a->get_io_service(), std::chrono::milliseconds(rng() % 30));
        bool packaged = rng() % 3 == 0;
        waiting.push_back(a->execute([&ctx, timers, i, packaged]() {
          auto& ts = (*timers)[i];
          bool aborted = false;
          if (packaged) {
            aborted =
                ts.t
                    ->async_wait(use_fiber_future(
                        [](boost::system::error_code ec) { return bool(ec); }))
                    .get();
          } else {
            try {
              ts.t->async_wait(use_fiber_future).get();
            } catch (boost::system::system_error& e) {
              SOAK_CHECK(ctx, is_aborted(e), e.what());
              aborted = true;
            }
          }
          if (aborted) {
            SOAK_CHECK(ctx, ts.cancelled, "aborted without cancel");
            ctx.count("cancelled waits");
          } else {
            SOAK_CHECK(ctx, soak::clock::now() >= ts.t->expiry(),
                       "timer completed before its expiry");
            ctx.count("completed waits");
          }
          ctx.progress();
        }));
      }
      // canceller, on the same service as the timers it touches
      a->execute([timers, n, seed = rng()]() {
        std::mt19937_64 r(seed);
        for (int k = 0; k < n / 3; k++) {
          asyik::sleep_for(std::chrono::microseconds(r() % 100));
          auto& ts = (*timers)[r() % n];
          ts.cancelled = true;
          ts.t->cancel();
        }
      });
    });
    for (auto& f : waiting) f.get();

    // a timer of b's io_context awaited by fibers of a: completion arrives
    // from b's thread
    ctx.phase("cross-service timers");
    std::vector<fibers::future<void>> cross;
    for (int i = 0; i < 50; i++)
      cross.push_back(a->execute([&ctx, b, ms = int(rng() % 10)]() {
        asio::steady_timer t(b->get_io_service(),
                             std::chrono::milliseconds(ms));
        t.async_wait(use_fiber_future).get();
        ctx.count("cross-service waits");
        ctx.progress();
      }));
    for (auto& f : cross) f.get();
  }
  a_thread.stop_and_join(ctx);
  b_thread.stop_and_join(ctx);
}

SOAK_SCENARIO(udp_resolve, "asio.udp_resolve",
              "UDP echo between services (datagrams checked byte for byte, "
              "losses counted) and repeated async_resolve")
{
  soak::service_thread server_thread, client_thread;
  auto server_as = server_thread.get(), client_as = client_thread.get();

  for (uint64_t round = 0; ctx.time_left(); round++) {
    ctx.phase("udp echo");
    uint16_t port = soak::next_port();
    auto server_sock = soak::run_on(server_as, [&]() {
      auto s = std::make_shared<udp::socket>(
          server_as->get_io_service(),
          udp::endpoint(asio::ip::make_address("127.0.0.1"), port));
      server_as->execute([s]() {
        std::vector<char> buf(65536);
        udp::endpoint from;
        try {
          while (true) {
            size_t n =
                s->async_receive_from(asio::buffer(buf), from, use_fiber_future)
                    .get();
            s->async_send_to(asio::buffer(buf.data(), n), from,
                             use_fiber_future)
                .get();
          }
        } catch (boost::system::system_error&) {
        }
      });
      return s;
    });

    std::vector<fibers::future<void>> clients;
    for (int c = 0; c < 32; c++)
      clients.push_back(client_as->execute([&ctx, client_as, port,
                                            stream = round * 64 + c]() {
        auto rng = ctx.rng(stream);
        auto s = std::make_shared<udp::socket>(client_as->get_io_service(),
                                               udp::v4());
        udp::endpoint server(asio::ip::make_address("127.0.0.1"), port);
        std::vector<char> buf(65536);
        for (int i = 0; i < 20; i++) {
          std::string msg = soak::make_payload(rng(), 1 + rng() % 8192);
          s->async_send_to(asio::buffer(msg), server, use_fiber_future).get();
          // give up on a lost datagram after 500ms (an expired handler can
          // still be queued after this fiber is gone: hold the socket weakly)
          asio::steady_timer timeout(client_as->get_io_service(), 500ms);
          timeout.async_wait([w = std::weak_ptr<udp::socket>(s)](
                                 boost::system::error_code ec) {
            if (auto sock = w.lock())
              if (!ec) sock->cancel();
          });
          udp::endpoint from;
          try {
            size_t n =
                s->async_receive_from(asio::buffer(buf), from, use_fiber_future)
                    .get();
            timeout.cancel();
            SOAK_CHECK(ctx, std::string(buf.data(), n) == msg,
                       "datagram differs");
            ctx.count("datagrams");
          } catch (boost::system::system_error& e) {
            SOAK_CHECK(ctx, is_aborted(e), e.what());
            ctx.count("datagrams lost");
            break;  // a late reply would be mistaken for the next one
          }
          ctx.progress();
        }
      }));
    for (auto& f : clients) f.get();
    soak::run_on(server_as, [&]() {
      boost::system::error_code ec;
      server_sock->close(ec);
    });

    ctx.phase("resolver");
    std::vector<fibers::future<void>> lookups;
    for (int i = 0; i < 20; i++)
      lookups.push_back(client_as->execute([&ctx, client_as]() {
        tcp::resolver r(client_as->get_io_service());
        auto results =
            r.async_resolve("localhost", "80", use_fiber_future).get();
        SOAK_CHECK(ctx, !results.empty(), "localhost did not resolve");
        ctx.count("lookups");
        ctx.progress();
      }));
    for (auto& f : lookups) f.get();
  }
  server_thread.stop_and_join(ctx);
  client_thread.stop_and_join(ctx);
}
