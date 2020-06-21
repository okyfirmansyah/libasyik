Libasyik's SQL APIs are provided from library [SOCI](https://github.com/SOCI/soci).

#### Basic Usage
```c++
#include "libasyik/service.hpp"
#include "libasyik/sql.hpp"

void some_handler(asyik::service_ptr as)
{
    // SQlite3 doesn't really support concurrent access, so we set pool=1
    auto pool = make_sql_pool(asyik::sql_backend_sqlite3, "test.db", 1);
    auto ses = pool->get_session(as);

    ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                      name varchar(255));)");
    int id=1;
    int out_id;
    std::string name;
    ses->query("select * from persons where id=:id", soci::use(id), 
                                                     soci::into(out_id),
                                                     soci::into(name));
    ...
}
```

#### Using Postgres
Same as above, but the connector specs on make_sql_pool changed to:
```c++
#include "libasyik/service.hpp"
#include "libasyik/sql.hpp"

void some_handler(asyik::service_ptr as)
{
    auto pool = make_sql_pool(asyik::sql_backend_postgresql, 
                              "host=localhost dbname=postgres password=test user=postgres", 
                              4 // use 4 connection pooling
                             );
    auto ses = pool->get_session(as);

    ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                      name varchar(255));)");
    int id=1;
    int out_id;
    std::string name;
    ses->query("select * from persons where id=:id", soci::use(id), 
                                                     soci::into(out_id),
                                                     soci::into(name));
                                                     
    ...
}
```

#### Using Transactions
```c++
void some_handler(asyik::service_ptr as, int id, int id2)
{
  auto ses = pool->get_session(as);

  std::string name = std::to_string(rand() % 1000000);
  std::string name2 = std::to_string(rand() % 1000000);

  ses->query("delete from persons where id=:id", use(id));
  ses->query("delete from persons where id=:id", use(id2));

  //commited transaction
  {
    sql_transaction tr(ses);
    ses->query("insert into persons(id, name) values(:id, :name)", use(id), use(name));
    tr.commit();
          
    // below transaction will be treated as no transaction exists(immediate commit)
    ses->query("insert into persons(id, name) values(:id, :name)", use(id2 + 1000), use(name2));
  }

  //aborted transaction(take no effect), because no commit() is invoked until
  //transaction goes out of scope
  {
    sql_transaction tr(ses);
    ses->query("insert into persons(id, name) values(:id, :name)", use(id), use(name));
    ses->query("insert into persons(id, name) values(:id, :name)", use(id2), use(name2));
  }
}
```
