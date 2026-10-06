// Component scenarios: memcache, rate_limit, object pool and the SQL session
// pool.

#include <set>

#include "libasyik/memcache.hpp"
#include "libasyik/object_pool.hpp"
#include "libasyik/rate_limit.hpp"
#include "soak.hpp"
#ifdef LIBASYIK_ENABLE_SOCI
#include "libasyik/sql.hpp"
#endif

namespace fibers = boost::fibers;
using asyik::service_ptr;
using namespace std::chrono_literals;

namespace {

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

// "<key>:<version>:" followed by filler; anything else is corruption.
std::string cache_value(const std::string& key, uint64_t version, size_t pad)
{
  return key + ":" + std::to_string(version) + ":" + std::string(pad, 'x');
}

bool valid_cache_value(const std::string& key, const std::string& v)
{
  if (v.compare(0, key.size() + 1, key + ":") != 0) return false;
  size_t p = key.size() + 1, digits = 0;
  while (p < v.size() && isdigit(static_cast<unsigned char>(v[p])))
    p++, digits++;
  if (!digits || p >= v.size() || v[p] != ':') return false;
  for (p++; p < v.size(); p++)
    if (v[p] != 'x') return false;
  return true;
}

}  // namespace

SOAK_SCENARIO(memcache_mt, "memcache.mt_churn",
              "make_memcache_mt shared by fibers of two services and plain "
              "threads: put/get/at/erase on few keys with a 1s expiry; every "
              "value read must be intact")
{
  soak::service_thread a_thread, b_thread;
  auto a = a_thread.get(), b = b_thread.get();
  auto cache = soak::run_on(a, [&]() {
    return asyik::make_memcache_mt<std::string, std::string, 1>(a);
  });

  while (ctx.time_left()) {
    ctx.phase("churn");
    std::atomic<uint64_t> version{0};
    auto worker = [&, cache](uint64_t stream) {
      auto rng = ctx.rng(stream);
      for (int i = 0; i < 2000; i++) {
        std::string key = "k" + std::to_string(rng() % 16);
        try {
          switch (rng() % 5) {
            case 0:
            case 1:
              cache->put(key, cache_value(key, version++, rng() % 200));
              ctx.count("put");
              break;
            case 2: {
              std::string v = cache->get(key);  // copy right away
              SOAK_CHECK(ctx, valid_cache_value(key, v),
                         "corrupted value from get(): " + v.substr(0, 60));
              ctx.count("get");
              break;
            }
            case 3: {
              std::string v = cache->at(key);
              SOAK_CHECK(ctx, valid_cache_value(key, v),
                         "corrupted value from at(): " + v.substr(0, 60));
              ctx.count("at");
              break;
            }
            case 4:
              cache->erase(key);
              ctx.count("erase");
              break;
          }
        } catch (std::out_of_range&) {
          ctx.count("miss");
        }
        if (i % 100 == 0) ctx.progress();
      }
    };
    std::vector<fibers::future<void>> fibers_running;
    for (int f = 0; f < 4; f++) {
      fibers_running.push_back(a->execute([=]() { worker(f); }));
      fibers_running.push_back(b->execute([=]() { worker(16 + f); }));
    }
    std::vector<std::thread> threads;
    for (int t = 0; t < 2; t++)
      threads.push_back(guarded_thread(ctx, [=]() { worker(32 + t); }));
    for (auto& f : fibers_running) {
      try {
        f.get();
      } catch (soak::failure&) {
      }
    }
    for (auto& t : threads) t.join();
    if (ctx.failed()) break;
  }
  soak::run_on(a, [&]() { cache.reset(); });
  a_thread.stop_and_join(ctx);
  b_thread.stop_and_join(ctx);
}

SOAK_SCENARIO(memcache_expiry, "memcache.expiry",
              "single-thread make_memcache used by many fibers of its "
              "service; entries must vanish after their lifetime")
{
  soak::service_thread st;
  auto as = st.get();
  soak::run_on(as, [&]() {
    auto cache = asyik::make_memcache<int, std::string, 1, 4>(as);
    while (ctx.time_left()) {
      ctx.phase("fill");
      std::vector<fibers::future<void>> users;
      for (int f = 0; f < 32; f++)
        users.push_back(as->execute([&, f]() {
          for (int i = 0; i < 200; i++) {
            int key = f * 1000 + i;
            cache->put(key, std::to_string(key));
            SOAK_CHECK(ctx, cache->get(key) == std::to_string(key),
                       "fresh entry missing");
            if (i % 16 == 0) boost::this_fiber::yield();
          }
          ctx.progress();
        }));
      for (auto& u : users) u.get();

      ctx.phase("waiting for expiry");
      asyik::sleep_for(1300ms);  // lifetime 1s + one 250ms segment
      for (int f = 0; f < 32; f++) {
        bool gone = false;
        try {
          cache->at(f * 1000);
        } catch (std::out_of_range&) {
          gone = true;
        }
        SOAK_CHECK(ctx, gone, "entry outlived its expiry");
      }
      ctx.count("rounds");
      ctx.progress();
    }
  });
  st.stop_and_join(ctx);
}

SOAK_SCENARIO(rate_limit, "rate_limit.bounds",
              "fibers race for tokens of fresh keys; the grants per key must "
              "stay within bucket + rate * elapsed")
{
  soak::service_thread st;
  auto as = st.get();
  const unsigned bucket = 50, rate = 100;  // tokens, tokens per second
  soak::run_on(as, [&]() {
    auto limiter = asyik::make_rate_limit_memory(as, bucket, rate);
    for (uint64_t round = 0; ctx.time_left(); round++) {
      ctx.phase("racing");
      std::string key = "key" + std::to_string(round);
      std::atomic<unsigned> granted{0};
      auto start = soak::clock::now();
      std::vector<fibers::future<void>> racers;
      for (int f = 0; f < 16; f++)
        racers.push_back(as->execute([&]() {
          for (int i = 0; i < 40; i++) {
            granted += limiter->checkpoint(key);
            asyik::sleep_for(std::chrono::milliseconds(i % 5));
          }
        }));
      for (auto& r : racers) r.get();
      double elapsed =
          std::chrono::duration<double>(soak::clock::now() - start).count();
      unsigned upper = bucket + unsigned(rate * elapsed) + 1;
      SOAK_CHECK(ctx, granted <= upper,
                 std::to_string(granted.load()) + " tokens granted, at most " +
                     std::to_string(upper) + " allowed");
      SOAK_CHECK(ctx, granted >= bucket,
                 "a fresh key granted only " + std::to_string(granted.load()));
      ctx.count("tokens granted", granted);
      ctx.count("rounds");
      ctx.progress();
    }
  });
  st.stop_and_join(ctx);
}

SOAK_SCENARIO(object_pool, "object_pool.churn",
              "shared_object_pool used from many threads: live objects "
              "must never overlap or change")
{
  struct item {
    uint64_t id;
    uint64_t words[30];
    explicit item(uint64_t id) : id(id)
    {
      for (auto& w : words) w = id * 2654435761u;
    }
    bool intact() const
    {
      for (auto w : words)
        if (w != id * 2654435761u) return false;
      return true;
    }
  };
  auto pool = std::make_shared<asyik::shared_object_pool<item>>();
  while (ctx.time_left()) {
    std::atomic<uint64_t> next_id{1};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++)
      threads.push_back(guarded_thread(ctx, [&, t]() {
        auto rng = ctx.rng(t);
        std::vector<std::shared_ptr<item>> live;
        for (int i = 0; i < 20000; i++) {
          if (live.size() < 64 && rng() % 2) {
            live.push_back(pool->acquire(next_id++));
          } else if (!live.empty()) {
            size_t k = rng() % live.size();
            SOAK_CHECK(ctx, live[k]->intact(), "pooled object overwritten");
            live[k] = live.back();
            live.pop_back();
          }
          if (i % 1000 == 0) ctx.progress();
        }
        for (auto& p : live)
          SOAK_CHECK(ctx, p->intact(), "pooled object overwritten");
      }));
    for (auto& t : threads) t.join();
    ctx.count("rounds");
  }
}

#ifdef LIBASYIK_ENABLE_SOCI
SOAK_SCENARIO(sql_pool, "sql.pool",
              "concurrent queries through make_sql_pool while backends are "
              "killed; the pool must recover and keep every confirmed row "
              "(ASYIK_SOAK_PG=<connect string>, skipped without a database)")
{
  const char* env = std::getenv("ASYIK_SOAK_PG");
  std::string conn =
      env ? env : "host=localhost dbname=postgres password=test user=postgres";
  try {
    soak::service_thread probe;
    soak::run_on(probe.get(), [&]() {
      asyik::make_sql_pool(asyik::sql_backend_postgresql, conn, 1);
    });
    probe.stop_and_join(ctx);
  } catch (soak::failure&) {
    throw;
  } catch (std::exception& e) {
    ctx.skip(std::string("no database: ") + e.what());
    return;
  }

  soak::service_thread a_thread, b_thread;
  auto a = a_thread.get(), b = b_thread.get();
  std::string table = "asyik_soak_" + std::to_string(ctx.seed % 1000000);
  // tagged, so the killer only ever terminates this pool's connections
  std::string pool_conn = conn + " application_name=asyik_soak";
  auto pool = soak::run_on(a, [&]() {
    auto p = asyik::make_sql_pool(asyik::sql_backend_postgresql, pool_conn, 4);
    p->set_health_check_period(1);
    auto ses = p->get_session(a);
    ses->query("DROP TABLE IF EXISTS " + table);
    ses->query("CREATE TABLE " + table + " (id bigint primary key)");
    return p;
  });

  std::atomic<long> next_id{1}, confirmed{0}, attempted{0};
  std::atomic<bool> killing{true};
  // kills the pool's backends now and then, from a separate connection
  auto killer = guarded_thread(ctx, [&]() {
    soci::session admin(soci::postgresql, conn);
    while (killing) {
      std::this_thread::sleep_for(1500ms);
      admin << "select pg_terminate_backend(pid) from pg_stat_activity "
               "where application_name = 'asyik_soak'";
      ctx.count("backend kills");
    }
  });

  std::vector<fibers::future<void>> workers;
  for (int w = 0; w < 12; w++) {
    auto as = w % 2 ? a : b;
    workers.push_back(as->execute([&, as]() {
      while (ctx.time_left()) {
        long id = next_id++;
        attempted++;
        try {
          auto ses = pool->get_session(as);
          ses->query("INSERT INTO " + table + " VALUES (" + std::to_string(id) +
                     ")");
          confirmed++;
          long n = 0;
          ses->query("SELECT count(*) FROM " + table +
                         " WHERE id = " + std::to_string(id),
                     soci::into(n));
          SOAK_CHECK(ctx, n == 1, "confirmed row not found");
          ctx.count("queries ok");
        } catch (soak::failure&) {
          throw;
        } catch (std::exception&) {
          ctx.count("queries failed");
          asyik::sleep_for(50ms);
        }
        ctx.progress();
      }
    }));
  }
  for (auto& w : workers) w.get();
  killing = false;
  killer.join();

  // after the kills stop, the health check must bring every session back
  ctx.phase("recovery");
  soak::run_on(a, [&]() {
    auto deadline = soak::clock::now() + 10s;
    bool healthy = false;
    while (!healthy && soak::clock::now() < deadline) {
      try {
        std::vector<asyik::sql_session_ptr> all;
        for (int i = 0; i < 4; i++) all.push_back(pool->get_session(a));
        for (auto& s : all) s->query("SELECT 1");
        healthy = true;
      } catch (std::exception&) {
        asyik::sleep_for(200ms);
      }
    }
    SOAK_CHECK(ctx, healthy, "pool did not recover after the kills");
    long rows = 0;
    pool->get_session(a)->query("SELECT count(*) FROM " + table,
                                soci::into(rows));
    // a query killed after its commit counts as failed, yet its row exists
    SOAK_CHECK(ctx, rows >= confirmed && rows <= attempted,
               std::to_string(rows) + " rows, " +
                   std::to_string(confirmed.load()) + " confirmed, " +
                   std::to_string(attempted.load()) + " attempted");
    pool->get_session(a)->query("DROP TABLE " + table);
  });
  a_thread.stop_and_join(ctx);
  b_thread.stop_and_join(ctx);
}
#endif
