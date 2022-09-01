#ifndef LIBASYIK_ASYIK_SQL_HPP
#define LIBASYIK_ASYIK_SQL_HPP

#include <list>
#include <string>

#include "aixlog.hpp"
#include "asyik_fwd.hpp"
#include "boost/algorithm/string/predicate.hpp"
#include "boost/fiber/all.hpp"
#include "common.hpp"
#include "internal/soci_internal.hpp"
#include "service.hpp"
#include "soci-postgresql.h"
#include "soci-sqlite3.h"
#include "soci.h"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;

namespace asyik {
void _TEST_invoke_sql();

static auto& sql_backend_postgresql = soci::postgresql;
static auto& sql_backend_sqlite3 = soci::sqlite3;

template <typename F, typename C>
sql_pool_ptr make_sql_pool(F&& factory, C&& connectString, size_t num_pool = 2);

class sql_pool : public std::enable_shared_from_this<sql_pool> {
 private:
  struct private_ {};

 public:
  ~sql_pool(){};
  sql_pool& operator=(const sql_pool&) = delete;
  sql_pool() = delete;
  sql_pool(const sql_pool&) = delete;
  sql_pool(sql_pool&&) = default;
  sql_pool& operator=(sql_pool&&) = default;

  sql_pool(struct private_) : health_check_period(60){};
  sql_session_ptr get_session(service_ptr as);
  void set_health_check_period(int sec) { health_check_period = sec; }

 private:
  fibers::mutex mtx_;
  std::list<std::unique_ptr<soci::session>> soci_sessions;
  std::atomic<int> health_check_period;

  friend class sql_session;
  template <typename F, typename C>
  friend sql_pool_ptr make_sql_pool(F&&, C&&, size_t);
};

template <typename F, typename C>
sql_pool_ptr make_sql_pool(F&& factory, C&& connectString, size_t num_pool)
{
  auto p = std::make_shared<sql_pool>(sql_pool::private_{});

  for (size_t i = 0; i < num_pool; i++) {
    std::lock_guard<fibers::mutex> l(p->mtx_);

    std::unique_ptr<soci::session> s(new soci::session(
        std::forward<F>(factory), std::forward<C>(connectString)));
    p->soci_sessions.push_back(std::move(s));
  };

  std::thread th([w = sql_pool_wptr(p), &f = std::forward<F>(factory),
                  connectString = std::string{connectString}]() {
    int i = 0;
    while (true) {
      sql_pool_ptr p;
      sleep_for(std::chrono::seconds(1));
      if ((p = w.lock())) {
        if (++i >= p->health_check_period) {
          i = 0;
          // test every soci_session here
          std::unique_lock<fibers::mutex> l(p->mtx_);
          auto sessions = std::move(p->soci_sessions);
          p->soci_sessions.resize(0);
          while (sessions.size()) {
            auto sql = std::move(sessions.front());
            sessions.pop_front();
            l.unlock();
            try {
              *sql << "SELECT 1;";
              l.lock();
              p->soci_sessions.push_back(std::move(sql));
            } catch (std::exception& e) {
              try {
                LOG(WARNING) << "health check failed: " << e.what()
                             << "\nrenew SOCI session...\n";

                std::unique_ptr<soci::session> s(
                    new soci::session(std::forward<F>(f), connectString));
                l.lock();
                p->soci_sessions.push_back(std::move(s));
              } catch (std::exception& e) {
                LOG(WARNING)
                    << "renew SOCI session failed: " << e.what() << "\n";
                l.lock();
                p->soci_sessions.push_back(std::move(sql));
              }
            }
          }
        }
        p.reset();
      } else
        break;
    }
  });
  th.detach();

  return p;
}  // namespace asyik

class sql_session : public std::enable_shared_from_this<sql_session> {
 private:
  struct private_ {};

 public:
  ~sql_session()
  {
    if (auto p = pool.lock()) {
      std::lock_guard<fibers::mutex> l(p->mtx_);
      p->soci_sessions.push_back(std::move(soci_session));
    }
  }
  sql_session& operator=(const sql_session&) = delete;
  sql_session() = delete;
  sql_session(const sql_session&) = delete;
  sql_session(sql_session&&) = default;
  sql_session& operator=(sql_session&&) = default;

  sql_session(struct private_){};

  template <typename... Args>
  void query(string_view s, Args&&... args)
  {
    service
        ->async([&args..., s, ses = soci_session.get()]() {
          ((*ses << s), ..., std::forward<Args>(args));
        })
        .get();
  };

  void begin();
  void commit();
  void rollback();

 private:
  sql_pool_wptr pool;
  service_ptr service;

 public:
  std::unique_ptr<soci::session> soci_session;

  friend class sql_pool;
};

class sql_transaction : public std::enable_shared_from_this<sql_transaction> {
 public:
  ~sql_transaction()
  {
    if (!handled) rollback();
  }
  sql_transaction& operator=(const sql_transaction&) = delete;
  sql_transaction() = delete;
  sql_transaction(const sql_transaction&) = delete;
  sql_transaction(sql_transaction&&) = default;
  sql_transaction& operator=(sql_transaction&&) = default;

  sql_transaction(sql_session_ptr ses) : session(ses), handled(false)
  {
    session->begin();
  };

  void begin()
  {
    handled = false;
    session->begin();
  }

  void commit()
  {
    session->commit();
    handled = true;
  }

  void rollback()
  {
    session->rollback();
    handled = true;
  }

 private:
  sql_session_ptr session;
  bool handled;

  friend class sql_session;
};

}  // namespace asyik

#endif
