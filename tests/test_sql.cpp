#include <map>
#include <set>
#include <thread>

#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"
#include "libasyik/sql.hpp"

namespace asyik {
void _TEST_invoke_sql(){};

TEST_CASE("Test case to connect to the test DB", "[sql]")
{
  using namespace soci;
  auto as = asyik::make_service();

  // run the pgsql for testing:  docker run --rm -e POSTGRES_PASSWORD=test -p
  // 5432:5432 -d postgres:12-alpine
  auto pool = make_sql_pool(
      asyik::sql_backend_postgresql,
      "host=localhost dbname=postgres password=test user=postgres", 4);
  pool->set_health_check_period(1);

  {
    auto ses = pool->get_session(as);

    ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                     name varchar(255));)");
    ses->query("delete from persons");
  }

  fibers::mutex mtx;  // guards against concurrent database write
  int count_down = 0;
  for (int i = 0; i < 5; i++)
    as->execute([i, as, pool, &count_down, &mtx]() {
      int id = i;
      int id2 = i + 1000000;

      for (int j = 0; j < 4; j++) {
        auto ses = pool->get_session(as);

        std::string name = std::to_string(rand() % 1000000);
        std::string name2 = std::to_string(rand() % 1000000);

        ses->query("delete from persons where id=:id", soci::use(id));
        ses->query("delete from persons where id=:id", soci::use(id2));

        ses->query("insert into persons(id, name) values(:id, :name)", use(id),
                   use(name));
        ses->query("insert into persons(id, name) values(:id, :name)", use(id2),
                   use(name2));

        int count;
        ses->query("select count(*) from persons where id=:id1 or id=:id2",
                   use(id), use(id2), into(count));
        REQUIRE(count == 2);

        int new_id;
        std::string new_name;
        ses->query("select * from persons where id=:id", use(id), into(new_id),
                   into(new_name));

        REQUIRE(new_id == id);
        REQUIRE(!new_name.compare(name));

        ses->query("select * from persons where id=:id", use(id2), into(new_id),
                   into(new_name));

        REQUIRE(new_id == id2);
        REQUIRE(!new_name.compare(name2));
      }
      count_down++;
    });

  as->execute([&count_down, as] {
    while (count_down < 5) asyik::sleep_for(std::chrono::milliseconds(50));
    as->stop();
  });
  as->run();
}

TEST_CASE("Test rowset, prepared, execute and fetch", "[sql]")
{
  using namespace soci;
  auto as = asyik::make_service();

  auto pool = make_sql_pool(
      asyik::sql_backend_postgresql,
      "host=localhost dbname=postgres password=test user=postgres", 4);

  auto ses = pool->get_session(as);

  ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                   name varchar(255));)");
  ses->query("delete from persons");
  ses->query("insert into persons(id, name) values(1, 'Alice')");
  ses->query("insert into persons(id, name) values(2, 'Bob')");
  ses->query("insert into persons(id, name) values(3, 'Charlie')");

  // query_rows without bind params
  {
    auto rs = ses->query_rows("select id, name from persons order by id");
    std::vector<std::pair<int, std::string>> results;
    for (const auto& r : rs) {
      results.emplace_back(r.get<int>(0), r.get<std::string>(1));
    }
    REQUIRE(results.size() == 3);
    REQUIRE(results[0].first == 1);
    REQUIRE(results[0].second == "Alice");
    REQUIRE(results[1].first == 2);
    REQUIRE(results[1].second == "Bob");
    REQUIRE(results[2].first == 3);
    REQUIRE(results[2].second == "Charlie");
  }

  // query_rows with bind params
  {
    int min_id = 1;
    auto rs = ses->query_rows(
        "select id, name from persons where id > :min order by id",
        soci::use(min_id));
    std::vector<std::pair<int, std::string>> results;
    for (const auto& r : rs) {
      results.emplace_back(r.get<int>(0), r.get<std::string>(1));
    }
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].first == 2);
    REQUIRE(results[1].first == 3);
  }

  // query_rows returning empty result
  {
    auto rs = ses->query_rows("select id, name from persons where id > 999");
    int count = 0;
    for (const auto& r : rs) {
      (void)r;
      count++;
    }
    REQUIRE(count == 0);
  }

  ses->query("delete from persons");
  as->stop();
}

TEST_CASE("Test transactions", "[sql]")
{
  using namespace soci;
  auto as = asyik::make_service();

  auto pool = make_sql_pool(
      asyik::sql_backend_postgresql,
      "host=localhost dbname=postgres password=test user=postgres", 4);

  {
    auto ses = pool->get_session(as);

    ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                     name varchar(255));)");
    ses->query("delete from persons");
  }

  fibers::mutex mtx;  // guards against concurrent database write
  int count_down = 0;
  for (int i = 0; i < 5; i++)
    as->execute([i, as, pool, &count_down, &mtx]() {
      int id = i;
      int id2 = i + 1000000;

      auto ses = pool->get_session(as);

      std::string name = std::to_string(rand() % 1000000);
      std::string name2 = std::to_string(rand() % 1000000);

      ses->query("delete from persons where id=:id", use(id));
      ses->query("delete from persons where id=:id", use(id2));

      {  // commited transaction
        sql_transaction tr(ses);
        ses->query("insert into persons(id, name) values(:id, :name)", use(id),
                   use(name));
        tr.commit();
        // below transaction will be treated as no transaction exists
        ses->query("insert into persons(id, name) values(:id, :name)",
                   use(id2 + 1000), use(name2));
      }

      {  // aborted transaction
        sql_transaction tr(ses);
        ses->query("insert into persons(id, name) values(:id, :name)", use(id),
                   use(name));
        ses->query("insert into persons(id, name) values(:id, :name)", use(id2),
                   use(name2));
      }

      name2 = std::to_string(rand() % 1000000);
      {  // rollback and redo transactions
        sql_transaction tr(ses);
        ses->query("insert into persons(id, name) values(:id, :name)", use(id),
                   use(name));
        ses->query("insert into persons(id, name) values(:id, :name)", use(id2),
                   use(name2));
        tr.rollback();
        tr.begin();
        ses->query("insert into persons(id, name) values(:id, :name)", use(id2),
                   use(name2));
        tr.commit();
      }

      int count;
      ses->query(
          "select count(*) from persons where id=:id1 or id=:id2 or id=:id3",
          use(id), use(id2), use(id2 + 1000), into(count));
      REQUIRE(count == 3);

      int new_id;
      std::string new_name;
      ses->query("select * from persons where id=:id", use(id), into(new_id),
                 into(new_name));

      REQUIRE(new_id == id);
      REQUIRE(!new_name.compare(name));

      ses->query("select * from persons where id=:id", use(id2), into(new_id),
                 into(new_name));

      REQUIRE(new_id == id2);
      REQUIRE(!new_name.compare(name2));

      count_down++;
    });

  as->execute([&count_down, as] {
    while (count_down < 5) asyik::sleep_for(std::chrono::milliseconds(50));
    as->stop();
  });
  as->run();
}

TEST_CASE("Test LISTEN/NOTIFY", "[sql]")
{
  using namespace soci;
  auto as = asyik::make_service();

  auto pool = make_sql_pool(
      asyik::sql_backend_postgresql,
      "host=localhost dbname=postgres password=test user=postgres", 2);

  // session that will listen
  auto ses = pool->get_session(as);

  fibers::mutex mtx;
  fibers::condition_variable cv;
  int received = 0;
  std::string got_payload;

  ses->listen("test_channel",
              [&](const std::string& ch, const std::string& payload) {
                std::lock_guard<fibers::mutex> l(mtx);
                got_payload = payload;
                ++received;
                cv.notify_one();
              });

  // send a notification from another session
  as->execute([as, pool]() {
    auto s2 = pool->get_session(as);
    try {
      s2->query("NOTIFY test_channel, 'hello_from_test';");
    } catch (...) {
    }
  });

  // wait for the notification (with timeout) and stop the service
  as->execute([&] {
    std::unique_lock<fibers::mutex> lk(mtx);
    if (received == 0) cv.wait_for(lk, std::chrono::seconds(5));
    as->stop();
  });

  as->run();

  REQUIRE(received >= 1);
  REQUIRE(got_payload == "hello_from_test");
}

TEST_CASE("Test LISTEN/NOTIFY multiple threads and channels", "[sql]")
{
  using namespace soci;
  auto as = asyik::make_service();

  auto pool = make_sql_pool(
      asyik::sql_backend_postgresql,
      "host=localhost dbname=postgres password=test user=postgres", 4);

  // single session that listens on multiple channels
  auto listener = pool->get_session(as);

  std::vector<std::string> channels = {"chan_a", "chan_b", "chan_c"};

  fibers::mutex mtx;
  fibers::condition_variable cv;
  std::atomic<int> received(0);
  std::unordered_map<std::string, int> per_channel_count;

  for (auto& ch : channels) {
    per_channel_count[ch] = 0;
    listener->listen(
        ch, [&mtx, &cv, &received, &per_channel_count, ch](
                const std::string& channel, const std::string& payload) {
          std::lock_guard<fibers::mutex> l(mtx);
          (void)payload;
          ++received;
          ++per_channel_count[channel];
          cv.notify_one();
        });
  }

  const int num_threads = 5;
  const int sends_per_thread = 20;
  const int expected = num_threads * sends_per_thread;

  // spawn sender threads that perform NOTIFY on various channels
  std::vector<std::thread> senders;
  for (int t = 0; t < num_threads; ++t) {
    senders.emplace_back([t, sends_per_thread, &channels, pool, as]() {
      for (int i = 0; i < sends_per_thread; ++i) {
        auto s = pool->get_session(as);
        std::string ch = channels[(t + i) % channels.size()];
        std::string payload = "t" + std::to_string(t) + "_" + std::to_string(i);
        try {
          s->query(std::string("NOTIFY ") + ch + ", '" + payload + "';");
        } catch (...) {
        }
      }
    });
  }

  // wait inside the service context for all notifications or timeout
  as->execute([&] {
    std::unique_lock<fibers::mutex> lk(mtx);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (received.load() < expected) {
      // past the deadline wait_until returns at once: fail instead of spinning
      if (cv.wait_until(lk, deadline) == fibers::cv_status::timeout) break;
    }
    as->stop();
  });

  as->run();

  for (auto& th : senders) th.join();

  REQUIRE(received.load() == expected);
  // ensure all channels received something
  for (auto& ch : channels) REQUIRE(per_channel_count[ch] > 0);
}
namespace {
const char* admin_conn =
    "host=localhost dbname=postgres password=test user=postgres";

// polls cond() every 50ms until it holds or timeout_ms passes
template <typename F>
bool wait_for(F cond, int timeout_ms = 10000)
{
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!cond()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    asyik::sleep_for(std::chrono::milliseconds(50));
  }
  return true;
}

int backend_pid(sql_session_ptr ses)
{
  int pid = 0;
  ses->query("select pg_backend_pid()", soci::into(pid));
  return pid;
}

// runs f in a fiber on as and stops the service afterwards, also when f
// throws; rethrows f's exception
template <typename F>
void run_in_service(service_ptr as, F f)
{
  auto done = as->execute([as, f]() {
    try {
      f();
    } catch (...) {
      as->stop();
      throw;
    }
    as->stop();
  });
  as->run();
  done.get();
}

void terminate_backends(sql_pool_ptr admin, service_ptr as,
                        const std::string& where)
{
  auto ses = admin->get_session(as);
  ses->query("select pg_terminate_backend(pid) from pg_stat_activity where " +
             where + " and pid <> pg_backend_pid()");
}
}  // namespace

TEST_CASE("SQL pool health check renews broken sessions", "[sql]")
{
  auto as = asyik::make_service();
  auto admin = make_sql_pool(sql_backend_postgresql, admin_conn, 2);

  run_in_service(as, [=]() {
    {
      terminate_backends(admin, as, "datname = 'asyik_hc'");
      auto ses = admin->get_session(as);
      ses->query("DROP DATABASE IF EXISTS asyik_hc");
      ses->query("CREATE DATABASE asyik_hc");
    }

    {
      auto pool = make_sql_pool(
          sql_backend_postgresql,
          "host=localhost dbname=asyik_hc password=test user=postgres", 2);
      pool->set_health_check_period(1);

      // all pooled sessions are usable and backed by none of old_pids
      auto all_healthy = [&](std::set<int> old_pids) {
        try {
          auto a = pool->get_session(as);
          auto b = pool->get_session(as);
          int pa = backend_pid(a), pb = backend_pid(b);
          return !old_pids.count(pa) && !old_pids.count(pb);
        } catch (std::exception&) {
          return false;
        }
      };
      std::set<int> pids;
      {
        auto a = pool->get_session(as);
        auto b = pool->get_session(as);
        pids = {backend_pid(a), backend_pid(b)};
      }

      // backends die: the health check replaces both sessions
      terminate_backends(admin, as, "datname = 'asyik_hc'");
      REQUIRE(wait_for([&]() { return all_healthy(pids); }));

      // backends die and the database refuses new connections: renewal
      // fails and the broken sessions stay pooled...
      {
        auto ses = admin->get_session(as);
        ses->query("ALTER DATABASE asyik_hc ALLOW_CONNECTIONS false");
      }
      terminate_backends(admin, as, "datname = 'asyik_hc'");
      asyik::sleep_for(std::chrono::milliseconds(2500));
      {
        auto ses = pool->get_session(as);
        REQUIRE_THROWS(backend_pid(ses));
      }

      // ...until connections are allowed again
      {
        auto ses = admin->get_session(as);
        ses->query("ALTER DATABASE asyik_hc ALLOW_CONNECTIONS true");
      }
      REQUIRE(wait_for([&]() { return all_healthy({}); }));
    }

    // pool destroyed: give the health check thread time to notice and exit
    asyik::sleep_for(std::chrono::milliseconds(1500));

    REQUIRE(wait_for([&]() {
      try {
        terminate_backends(admin, as, "datname = 'asyik_hc'");
        admin->get_session(as)->query("DROP DATABASE IF EXISTS asyik_hc");
        return true;
      } catch (std::exception&) {
        return false;
      }
    }));
  });
}

TEST_CASE("LISTEN watcher: unlisten and backend loss", "[sql]")
{
  auto as = asyik::make_service();
  // phases are logged: a hang on CI showed only that this test was running
  LOG(INFO) << "LISTEN watcher test: connecting\n";
  auto pool = make_sql_pool(sql_backend_postgresql, admin_conn, 3);

  run_in_service(as, [=]() {
    auto listener = pool->get_session(as);
    std::map<std::string, int> received;
    auto count = [&](const std::string& channel, const std::string&) {
      received[channel]++;
    };
    auto notify = [&](const std::string& channel) {
      pool->get_session(as)->query("NOTIFY " + channel + ", 'x'");
    };

    LOG(INFO) << "LISTEN watcher test: listen x and y\n";
    listener->listen("asyik_ch_x", count);
    listener->listen("asyik_ch_y", count);
    notify("asyik_ch_x");
    notify("asyik_ch_y");
    REQUIRE(wait_for([&]() {
      return received["asyik_ch_x"] == 1 && received["asyik_ch_y"] == 1;
    }));

    LOG(INFO) << "LISTEN watcher test: unlisten x\n";
    // dropping one channel keeps the watcher running for the other
    listener->unlisten("asyik_ch_x");
    notify("asyik_ch_x");
    notify("asyik_ch_y");
    REQUIRE(wait_for([&]() { return received["asyik_ch_y"] == 2; }));
    REQUIRE(received["asyik_ch_x"] == 1);
    REQUIRE(listener->notify_running);

    LOG(INFO) << "LISTEN watcher test: unlisten y\n";
    // dropping the last channel cancels the watcher
    listener->unlisten("asyik_ch_y");
    REQUIRE(wait_for([&]() { return !listener->notify_running; }));

    LOG(INFO) << "LISTEN watcher test: listen z, kill backend\n";
    // the watcher stops when the backend goes away...
    listener->listen("asyik_ch_z", count);
    REQUIRE(listener->notify_running);
    terminate_backends(pool, as,
                       "pid = " + std::to_string(backend_pid(listener)));
    REQUIRE(wait_for([&]() { return !listener->notify_running; }));

    LOG(INFO) << "LISTEN watcher test: dead connection\n";
    // ...and LISTEN/UNLISTEN on the dead connection do not throw
    REQUIRE_NOTHROW(listener->listen("asyik_ch_w", count));
    REQUIRE_NOTHROW(listener->unlisten("asyik_ch_w"));
    REQUIRE_NOTHROW(listener->unlisten("asyik_ch_z"));
    LOG(INFO) << "LISTEN watcher test: done, stopping the service\n";
  });
  LOG(INFO) << "LISTEN watcher test: service stopped\n";
}

}  // namespace asyik