#include <iostream>
#include <regex>
#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/service.hpp"
#include "catch2/catch.hpp"
#include "libasyik/common.hpp"
#include "libasyik/sql.hpp"
#include "libasyik/internal/soci_internal.hpp"

namespace fibers = boost::fibers;

namespace asyik
{
  void _TEST_invoke_sql(){};

  sql_session_ptr sql_pool::get_session(service_ptr as)
  {
    auto session = std::make_shared<sql_session>(sql_session::private_{});

    {
      std::lock_guard<fibers::mutex> l(mtx_);
      while (soci_sessions.empty())
      {
        mtx_.unlock();
        asyik::sleep_for(std::chrono::milliseconds(5));
        mtx_.lock();
      }
      session->soci_session = std::move(soci_sessions.back());
      soci_sessions.pop_back();
    }
    session->pool = shared_from_this();
    session->service = as;

    return session;
  }

  void sql_session::begin()
  {
    service->async([ses = soci_session.get()]() {
             ses->begin();
           })
        .get();
  }

  void sql_session::commit()
  {
    service->async([ses = soci_session.get()]() {
             ses->commit();
           })
        .get();
  }

  void sql_session::rollback()
  {
    service->async([ses = soci_session.get()]() {
             ses->rollback();
           })
        .get();
  }

  TEST_CASE("Test case to connect to the test DB")
  {
    using namespace soci;
    auto as = asyik::make_service();

    // num_pool = 1, SOCI's sqlite not really support multi-pool :(
    auto pool = make_sql_pool(asyik::sql_backend_sqlite3, "test.db", 1);

    {
      auto ses = pool->get_session(as);

      ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                     name varchar(255));)");
      ses->query("delete from persons");
    }

    fibers::mutex mtx; // guards against concurrent database write
    int count_down = 0;
    for (int i = 0; i < 5; i++)
      as->execute([i, as, pool, &count_down, &mtx]() {
        int id = i;
        int id2 = i + 1000000;

        for (int j = 0; j < 4; j++)
        {
          auto ses = pool->get_session(as);

          std::string name = std::to_string(rand() % 1000000);
          std::string name2 = std::to_string(rand() % 1000000);

          ses->query("delete from persons where id=:id", soci::use(id));
          ses->query("delete from persons where id=:id", soci::use(id2));

          ses->query("insert into persons(id, name) values(:id, :name)", use(id), use(name));
          ses->query("insert into persons(id, name) values(:id, :name)", use(id2), use(name2));

          int count;
          ses->query("select count(*) from persons where id=:id1 or id=:id2", use(id), use(id2), into(count));
          REQUIRE(count == 2);

          int new_id;
          std::string new_name;
          ses->query("select * from persons where id=:id", use(id), into(new_id), into(new_name));

          REQUIRE(new_id == id);
          REQUIRE(!new_name.compare(name));

          ses->query("select * from persons where id=:id", use(id2), into(new_id), into(new_name));

          REQUIRE(new_id == id2);
          REQUIRE(!new_name.compare(name2));
        }
        count_down++;
      });

    as->execute([&count_down, as] {
      while (count_down < 5)
        asyik::sleep_for(std::chrono::milliseconds(50));
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

    // num_pool = 1, SOCI's sqlite not really support multi-pool :(
    auto pool = make_sql_pool(asyik::sql_backend_sqlite3, "test.db", 1);

    {
      auto ses = pool->get_session(as);

      ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                     name varchar(255));)");
      ses->query("delete from persons");
    }

    fibers::mutex mtx; // guards against concurrent database write
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

        {//commited transaction
          sql_transaction tr(ses);
          ses->query("insert into persons(id, name) values(:id, :name)", use(id), use(name));
          tr.commit();
          // below transaction will be treated as no transaction exists
          ses->query("insert into persons(id, name) values(:id, :name)", use(id2+1), use(name2));
        }

        {//aborted transaction
          sql_transaction tr(ses);
          ses->query("insert into persons(id, name) values(:id, :name)", use(id), use(name));
          ses->query("insert into persons(id, name) values(:id, :name)", use(id2), use(name2));
        }

        name2 = std::to_string(rand() % 1000000);
        {//rollback and redo transactions
          sql_transaction tr(ses);
          ses->query("insert into persons(id, name) values(:id, :name)", use(id), use(name));
          ses->query("insert into persons(id, name) values(:id, :name)", use(id2), use(name2));
          tr.rollback();
          tr.begin();
          ses->query("insert into persons(id, name) values(:id, :name)", use(id2), use(name2));
          tr.commit();
        }

        int count;
        ses->query("select count(*) from persons where id=:id1 or id=:id2 or id=:id3", use(id), use(id2), use(id2+1), into(count));
        REQUIRE(count == 3);

        int new_id;
        std::string new_name;
        ses->query("select * from persons where id=:id", use(id), into(new_id), into(new_name));

        REQUIRE(new_id == id);
        REQUIRE(!new_name.compare(name));

        ses->query("select * from persons where id=:id", use(id2), into(new_id), into(new_name));

        REQUIRE(new_id == id2);
        REQUIRE(!new_name.compare(name2));
        
        count_down++;
      });

    as->execute([&count_down, as] {
      while (count_down < 5)
        asyik::sleep_for(std::chrono::milliseconds(50));
      as->stop();
    });
    as->run();
  }
} // namespace asyik
