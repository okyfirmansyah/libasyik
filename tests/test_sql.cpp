#include <thread>

#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/service.hpp"
#include "libasyik/sql.hpp"

namespace asyik {
void _TEST_invoke_sql(){};

TEST_CASE("Test case to connect to the test DB")
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

TEST_CASE("Test rowset, prepared, execute and fetch")
{
  // prepare
  // rowset<row> r;
  // sql << "select * from persons", into(r);
}

TEST_CASE("Test transactions")
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

TEST_CASE("Test LISTEN/NOTIFY")
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

TEST_CASE("Test LISTEN/NOTIFY multiple threads and channels")
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
      cv.wait_until(lk, deadline);
    }
    as->stop();
  });

  as->run();

  for (auto& th : senders) th.join();

  REQUIRE(received.load() == expected);
  // ensure all channels received something
  for (auto& ch : channels) REQUIRE(per_channel_count[ch] > 0);
}
}  // namespace asyik