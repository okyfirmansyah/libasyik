// Fiber scheduling scenarios: execute()/async() under contention, nested
// cross-service chains, fiber synchronisation primitives across threads and
// stopping services under load.

#include <boost/asio/steady_timer.hpp>

#include "libasyik/internal/use_fiber_future.hpp"
#include "soak.hpp"

namespace fibers = boost::fibers;
using asyik::service_ptr;
using namespace std::chrono_literals;

namespace {

struct task_error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Runs f on a new thread; soak failures are already recorded in ctx, other
// exceptions are turned into failures.
template <typename F>
std::thread guarded_thread(soak::context& ctx, F f)
{
  return std::thread([&ctx, f]() mutable {
    try {
      f();
    } catch (soak::failure&) {
    } catch (std::exception& e) {
      try {
        ctx.fail(std::string("thread: ") + e.what());
      } catch (soak::failure&) {
      }
    }
  });
}

std::vector<std::unique_ptr<soak::service_thread>> start_services(int n)
{
  std::vector<std::unique_ptr<soak::service_thread>> s;
  for (int i = 0; i < n; i++) s.emplace_back(new soak::service_thread);
  return s;
}

void stop_services(soak::context& ctx,
                   std::vector<std::unique_ptr<soak::service_thread>>& s)
{
  ctx.phase("stopping services");
  for (auto& st : s) st->stop_and_join(ctx);
}

// async() bookkeeping must balance once all tasks have finished.
void check_async_stats(soak::context& ctx)
{
  auto deadline = soak::clock::now() + 5s;
  auto s = asyik::service::get_async_stats();
  while (s.task_started != s.task_terminated && soak::clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
    s = asyik::service::get_async_stats();
  }
  SOAK_CHECK(ctx, s.task_started == s.task_terminated,
             "async tasks started " + std::to_string(s.task_started) +
                 ", terminated " + std::to_string(s.task_terminated));
}

}  // namespace

SOAK_SCENARIO(execute_storm, "fiber.execute_storm",
              "foreign threads and cross-service fibers flood execute()/"
              "async() with temporary arguments, random exceptions; services "
              "are recreated every round")
{
  const int n_services = 4, n_threads = 4, per_thread = 400;
  for (uint64_t round = 0; ctx.time_left(); round++) {
    ctx.phase("starting services");
    auto services = start_services(n_services);

    // One submitter: a mix of execute() and async() on random services,
    // checking every result.
    auto submit = [&](uint64_t stream, bool in_fiber) {
      auto rng = ctx.rng(stream);
      struct pending_t {
        fibers::future<uint64_t> f;
        uint64_t expected;
        bool throws;
      };
      std::vector<pending_t> pending;
      auto drain = [&]() {
        for (auto& p : pending) {
          try {
            uint64_t v = p.f.get();
            SOAK_CHECK(ctx, !p.throws, "task should have thrown");
            SOAK_CHECK(ctx, v == p.expected, "wrong result");
            ctx.count(in_fiber ? "fiber ok" : "thread ok");
          } catch (task_error&) {
            SOAK_CHECK(ctx, p.throws, "unexpected exception");
            ctx.count("exceptions");
          }
          ctx.progress();
        }
        pending.clear();
      };
      for (int i = 0; i < per_thread && ctx.time_left(); i++) {
        auto& as = services[rng() % n_services]->get();
        std::string payload = soak::make_payload(rng(), rng() % 2048);
        bool throws = rng() % 10 == 0;
        auto work = [throws](const std::string& p) {
          if (throws) throw task_error("requested");
          return soak::checksum(p);
        };
        // the payload is passed as a temporary on purpose
        auto f = rng() % 2 ? as->execute(work, std::string(payload))
                           : as->async(work, std::string(payload));
        pending.push_back({std::move(f), soak::checksum(payload), throws});
        if (pending.size() >= 64) drain();
      }
      drain();
    };

    ctx.phase("submitting");
    std::vector<std::thread> threads;
    for (int t = 0; t < n_threads; t++)
      threads.push_back(
          guarded_thread(ctx, [&, t]() { submit(round * 64 + t, false); }));
    // fibers of each service submitting to the others
    std::vector<fibers::future<void>> fiber_submitters;
    for (int s = 0; s < n_services; s++)
      fiber_submitters.push_back(services[s]->get()->execute(
          [&, s]() { submit(round * 64 + 32 + s, true); }));
    for (auto& t : threads) t.join();
    for (auto& f : fiber_submitters) {
      try {
        f.get();
      } catch (soak::failure&) {
      }
    }

    stop_services(ctx, services);
    if (ctx.failed()) return;
    check_async_stats(ctx);
    ctx.count("rounds");
  }
}

namespace {

enum class hop { execute_same, execute_other, async_worker, sleep };

struct chain_plan {
  std::vector<hop> hops;
  bool throws;
};

uint64_t fold(uint64_t v, hop h) { return v * 31 + static_cast<int>(h) + 1; }

uint64_t expected_result(const chain_plan& plan)
{
  uint64_t v = 7;
  for (auto h : plan.hops) v = fold(v, h);
  return v;
}

// Follows plan from step i: every hop moves the rest of the chain to another
// fiber, service or worker thread and waits for it there.
uint64_t run_chain(const std::shared_ptr<chain_plan>& plan, size_t i,
                   uint64_t v, const std::vector<service_ptr>& services,
                   service_ptr here)
{
  if (i == plan->hops.size()) {
    if (plan->throws) throw task_error("chain end");
    return v;
  }
  hop h = plan->hops[i];
  uint64_t next = fold(v, h);
  auto rest = [=, &services](service_ptr where) {
    return run_chain(plan, i + 1, next, services, where);
  };
  switch (h) {
    case hop::execute_same:
      return here->execute([=]() { return rest(here); }).get();
    case hop::execute_other: {
      auto other = services[next % services.size()];
      return other->execute([=]() { return rest(other); }).get();
    }
    case hop::async_worker:
      return here->async([=]() { return rest(here); }).get();
    case hop::sleep:
      asyik::sleep_for(std::chrono::microseconds(next % 2000));
      return rest(here);
  }
  return 0;
}

}  // namespace

SOAK_SCENARIO(nested_chains, "fiber.nested_chains",
              "concurrent random chains of execute() (same and other "
              "service), async() and sleeps, with exceptions thrown from the "
              "far end")
{
  const int n_services = 3, chains = 120;
  ctx.phase("starting services");
  auto threads = start_services(n_services);
  std::vector<service_ptr> services;
  for (auto& t : threads) services.push_back(t->get());

  auto rng = ctx.rng(1);
  while (ctx.time_left()) {
    ctx.phase("running chains");
    std::vector<
        std::pair<std::shared_ptr<chain_plan>, fibers::future<uint64_t>>>
        running;
    for (int c = 0; c < chains; c++) {
      auto plan = std::make_shared<chain_plan>();
      int depth = 1 + rng() % 8;
      for (int d = 0; d < depth; d++)
        plan->hops.push_back(static_cast<hop>(rng() % 4));
      plan->throws = rng() % 8 == 0;
      auto start = services[rng() % n_services];
      running.emplace_back(plan, start->execute([plan, start, &services]() {
        return run_chain(plan, 0, 7, services, start);
      }));
    }
    for (auto& r : running) {
      try {
        uint64_t v = r.second.get();
        SOAK_CHECK(ctx, !r.first->throws, "chain should have thrown");
        SOAK_CHECK(ctx, v == expected_result(*r.first), "wrong chain result");
        ctx.count("chains ok");
      } catch (task_error&) {
        SOAK_CHECK(ctx, r.first->throws, "unexpected exception from chain");
        ctx.count("chains thrown");
      }
      ctx.progress();
    }
  }
  stop_services(ctx, threads);
  check_async_stats(ctx);
}

SOAK_SCENARIO(sync_primitives, "fiber.sync_primitives",
              "buffered_channel, mutex and condition_variable shared by "
              "fibers of several services and plain threads")
{
  const int n_services = 3, producers = 6, items = 2000;
  auto threads = start_services(n_services);
  std::vector<service_ptr> services;
  for (auto& t : threads) services.push_back(t->get());

  while (ctx.time_left()) {
    // ── channel: every (producer, seq) arrives exactly once ──
    ctx.phase("channel");
    using item = std::pair<int, int>;
    auto ch = std::make_shared<fibers::buffered_channel<item>>(16);
    std::vector<std::vector<std::atomic<int>>> seen(producers);
    for (auto& v : seen) v = std::vector<std::atomic<int>>(items);
    std::atomic<int> received{0};

    auto consume = [&]() {
      item it;
      while (ch->pop(it) == fibers::channel_op_status::success) {
        seen[it.first][it.second]++;
        received++;
        ctx.progress();
      }
    };
    std::vector<fibers::future<void>> producing;
    for (int p = 0; p < producers; p++)
      producing.push_back(services[p % 2]->execute([&, p]() {
        for (int i = 0; i < items; i++) {
          ch->push(item{p, i});
          if (i % 64 == 0) boost::this_fiber::yield();
        }
      }));
    auto fiber_consumer = services[2]->execute(consume);
    std::thread thread_consumer = guarded_thread(ctx, consume);
    for (auto& f : producing) f.get();
    ch->close();
    fiber_consumer.get();
    thread_consumer.join();
    SOAK_CHECK(ctx, received == producers * items, "items lost or duplicated");
    for (auto& v : seen)
      for (auto& n : v) SOAK_CHECK(ctx, n == 1, "item not received once");

    // ── mutex + condition_variable across services and threads ──
    ctx.phase("mutex");
    fibers::mutex m;
    fibers::condition_variable cv;
    long counter = 0;
    const int per_worker = 500, fiber_workers = 3 * n_services,
              thread_workers = 2;
    const long total = long(per_worker) * (fiber_workers + thread_workers);
    auto increment = [&]() {
      for (int i = 0; i < per_worker; i++) {
        std::unique_lock<fibers::mutex> l(m);
        long v = counter;
        if (i % 16 == 0) boost::this_fiber::yield();  // hold the lock a while
        counter = v + 1;
        if (counter == total) cv.notify_all();
      }
      ctx.progress(per_worker);
    };
    auto waiter = services[0]->execute([&]() {
      std::unique_lock<fibers::mutex> l(m);
      return cv.wait_for(l, 20s, [&]() { return counter == total; });
    });
    std::vector<fibers::future<void>> workers;
    for (int w = 0; w < fiber_workers; w++)
      workers.push_back(services[w % n_services]->execute(increment));
    std::vector<std::thread> worker_threads;
    for (int w = 0; w < thread_workers; w++)
      worker_threads.push_back(guarded_thread(ctx, increment));
    for (auto& f : workers) f.get();
    for (auto& t : worker_threads) t.join();
    SOAK_CHECK(ctx, counter == total, "lost increments");
    SOAK_CHECK(ctx, waiter.get(), "condition_variable waiter not woken");
    ctx.count("rounds");
  }
  stop_services(ctx, threads);
}

SOAK_SCENARIO(stop_under_load, "fiber.stop_under_load",
              "services are stopped at random moments while their fibers "
              "sleep, wait on timers, promises and async(); every service "
              "thread must finish")
{
  for (uint64_t round = 0; ctx.time_left(); round++) {
    auto rng = ctx.rng(round);
    ctx.phase("starting");
    soak::service_thread st;
    auto as = st.get();
    // shared: a fiber outliving a failed stop must not touch this frame
    auto started = std::make_shared<std::atomic<int>>(0);

    std::vector<std::shared_ptr<fibers::promise<int>>> promises;
    for (int i = 0; i < 20; i++) {
      auto p = std::make_shared<fibers::promise<int>>();
      auto f = std::make_shared<fibers::future<int>>(p->get_future());
      promises.push_back(p);
      as->execute([f, started]() {
        (*started)++;
        try {
          f->get();
        } catch (...) {
        }
      });
    }
    for (int i = 0; i < 30; i++) {
      int kind = rng() % 4, ms = rng() % 150;
      as->execute([as, kind, ms, started]() {
        (*started)++;
        try {
          switch (kind) {
            case 0:
              asyik::sleep_for(std::chrono::milliseconds(ms));
              break;
            case 1: {
              boost::asio::steady_timer t(as->get_io_service(),
                                          std::chrono::milliseconds(ms));
              t.async_wait(asyik::use_fiber_future).get();
              break;
            }
            case 2:
              as->async([ms]() {
                  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                }).get();
              break;
            case 3:
              // sleep, then start I/O: the case that used to hang run()
              boost::this_fiber::sleep_for(std::chrono::milliseconds(ms));
              boost::asio::steady_timer t(as->get_io_service(), 1ms);
              t.async_wait(asyik::use_fiber_future).get();
              break;
          }
        } catch (...) {
          // interrupted by the stop: fine
        }
      });
    }

    // promises are fulfilled from another thread, some after the stop
    auto fulfiller = guarded_thread(ctx, [&, delay = rng() % 200]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      for (auto& p : promises) p->set_value(1);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(rng() % 200));
    ctx.phase("stopping");
    st.stop_and_join(ctx, 5s);
    fulfiller.join();
    ctx.count("services stopped");
    ctx.count("fibers", *started);
    ctx.progress();
  }
  check_async_stats(ctx);
}
