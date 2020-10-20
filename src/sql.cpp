#include <iostream>
#include <regex>
#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/service.hpp"
#include "libasyik/common.hpp"
#include "libasyik/sql.hpp"
#include "libasyik/internal/soci_internal.hpp"

namespace fibers = boost::fibers;

namespace asyik
{
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
      session->soci_session = std::move(soci_sessions.front());
      soci_sessions.pop_front();
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
} // namespace asyik
