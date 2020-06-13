#ifndef LIBASYIK_ASYIK_SQL_HPP
#define LIBASYIK_ASYIK_SQL_HPP
#include <string>
#include <regex>
#include "boost/fiber/all.hpp"
#include "aixlog.hpp"
#include "service.hpp"
#include "boost/algorithm/string/predicate.hpp"
#include "common.hpp"
#include "internal/soci_internal.hpp"
#include "soci.h"
#include "soci-sqlite3.h"
#include "soci-postgresql.h"

namespace fibers = boost::fibers;
using fiber = boost::fibers::fiber;

namespace asyik
{
    void _TEST_invoke_sql();

    static auto &sql_backend_postgresql = soci::postgresql;
    static auto &sql_backend_sqlite3 = soci::sqlite3;

    class sql_pool;
    using sql_pool_wptr = std::weak_ptr<sql_pool>;
    using sql_pool_ptr = std::shared_ptr<sql_pool>;

    class sql_session;
    using sql_session_wptr = std::weak_ptr<sql_session>;
    using sql_session_ptr = std::shared_ptr<sql_session>;

    template <typename F, typename C>
    sql_pool_ptr make_sql_pool(F &&factory, C &&connectString, size_t num_pool = 2);

    class sql_pool : public std::enable_shared_from_this<sql_pool>
    {
    private:
        struct private_
        {
        };

    public:
        ~sql_pool(){};
        sql_pool &operator=(const sql_pool &) = delete;
        sql_pool() = delete;
        sql_pool(const sql_pool &) = delete;
        sql_pool(sql_pool &&) = default;
        sql_pool &operator=(sql_pool &&) = default;

        sql_pool(struct private_){};
        sql_session_ptr get_session(service_ptr as);

    private:
        fibers::mutex mtx_;
        std::vector<std::unique_ptr<soci::session>> soci_sessions;

        friend class sql_session;
        template <typename F, typename C>
        friend sql_pool_ptr make_sql_pool(F &&, C &&, size_t);
    };

    template <typename F, typename C>
    sql_pool_ptr make_sql_pool(F &&factory, C &&connectString, size_t num_pool)
    {
        auto p = std::make_shared<sql_pool>(sql_pool::private_{});

        for (int i = 0; i < num_pool; i++)
        {
            std::lock_guard<fibers::mutex> l(p->mtx_);
            auto s = std::make_unique<soci::session>(std::forward<F>(factory), std::forward<C>(connectString));
            p->soci_sessions.push_back(std::move(s));
        };

        return p;
    }

    class sql_session : public std::enable_shared_from_this<sql_session>
    {
    private:
        struct private_
        {
        };

    public:
        ~sql_session()
        {
            if (auto p = pool.lock())
            {
                std::lock_guard<fibers::mutex> l(p->mtx_);
                p->soci_sessions.push_back(std::move(soci_session));
            }
        }
        sql_session &operator=(const sql_session &) = delete;
        sql_session() = delete;
        sql_session(const sql_session &) = delete;
        sql_session(sql_session &&) = default;
        sql_session &operator=(sql_session &&) = default;

        sql_session(struct private_){};

        template <typename... Args>
        void query(const std::string s, Args &&... args)
        {
            service->async([&args..., s, ses = soci_session.get()]() {
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

    class sql_transaction : public std::enable_shared_from_this<sql_transaction>
    {
    public:
        ~sql_transaction()
        {
            if (!handled)
                rollback();
        }
        sql_transaction &operator=(const sql_transaction &) = delete;
        sql_transaction() = delete;
        sql_transaction(const sql_transaction &) = delete;
        sql_transaction(sql_transaction &&) = default;
        sql_transaction &operator=(sql_transaction &&) = default;

        sql_transaction(sql_session_ptr ses)
        :session(ses), handled(false)
        {
            session->begin();
        };

        void begin()
        {
            handled=false;
            session->begin();
        }

        void commit()
        {
            session->commit();
            handled=true;
        }

        void rollback()
        {
            session->rollback();
            handled=true;
        }

    private:
        sql_session_ptr session;
        bool handled;

        friend class sql_session;
    };

} // namespace asyik

#endif
