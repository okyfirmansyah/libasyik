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

#### Listening for notifications (PostgreSQL LISTEN / NOTIFY)

The library exposes a small, convenient wrapper on top of PostgreSQL's
LISTEN/NOTIFY mechanism via `sql_session`:

- `void listen(const std::string& channel, notify_handler_t handler)`
  - Start listening on `channel`. `handler` is called when a notification
    for that channel arrives: `handler(channel, payload)`.
- `void unlisten(const std::string& channel)`
  - Stop listening on `channel`.
- `void unlisten_all()`
  - Stop listening on all channels and cancel background watchers.

Implementation notes:

- The listen/watch implementation uses libpq's socket together with
  `boost::asio::posix::stream_descriptor` to wait for notifications without
  busy-polling. Notification handlers are dispatched through the session's
  `service` so they execute in the same worker context as other async tasks.
- `sql_session`'s destructor calls `unlisten_all()` to ensure the watcher is
  stopped and handlers are cleared before the underlying SOCI session is
  returned to the pool.
- Channel names should be simple, trusted identifiers (alphanumeric and
  underscores). If channel names are constructed from untrusted input, quote
  or validate them to avoid SQL injection.

Example — single channel
```c++
// assume `as` is a service_ptr and `pool` is a sql_pool_ptr
auto ses = pool->get_session(as);

ses->listen("my_channel", [](const std::string& channel,
                              const std::string& payload) {
  // runs on the service worker
  std::cout << "got notify on " << channel << ": " << payload << "\n";
});

// send a notification from another session
auto sender = pool->get_session(as);
sender->query("NOTIFY my_channel, 'hello_world';");

// later, stop listening
ses->unlisten("my_channel");
```

Example — multiple channels and threads
```c++
auto listener = pool->get_session(as);

std::atomic<int> received{0};
std::mutex mtx;
std::condition_variable cv;

for (const auto &ch : {"chan1","chan2","chan3"}) {
  listener->listen(ch, [&](const std::string& channel, const std::string& payload){
    {
      std::lock_guard<std::mutex> g(mtx);
      ++received;
    }
    cv.notify_one();
  });
}

// spawn multiple threads that notify different channels
std::vector<std::thread> threads;
for (int i = 0; i < 4; ++i) {
  threads.emplace_back([pool, as, i]() {
    auto s = pool->get_session(as);
    std::string ch = (i % 2 == 0) ? "chan1" : "chan2";
    s->query(std::string("NOTIFY ") + ch + ", 'p" + std::to_string(i) + "';");
  });
}

// wait for notifications (simple timeout)
{
  std::unique_lock<std::mutex> lk(mtx);
  cv.wait_for(lk, std::chrono::seconds(5), [&]{ return received.load() >= 4; });
}

for (auto &t : threads) t.join();

listener->unlisten_all();
```

Quick tips
- For local testing start Postgres quickly with Docker:
  `docker run --rm -e POSTGRES_PASSWORD=test -p 5432:5432 -d postgres:12-alpine`
- Tests in this repository include a simple `LISTEN/NOTIFY` test in
  `tests/test_sql.cpp` demonstrating usage and thread-safety.
