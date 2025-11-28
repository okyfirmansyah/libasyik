#include "libasyik/sql.hpp"

#include <libpq-fe.h>

#include <boost/asio/posix/stream_descriptor.hpp>
#include <memory>

#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/common.hpp"
#include "libasyik/internal/soci_internal.hpp"
#include "libasyik/service.hpp"

namespace fibers = boost::fibers;

namespace asyik {
sql_session_ptr sql_pool::get_session(service_ptr as)
{
  auto session = std::make_shared<sql_session>(sql_session::private_{});

  {
    std::lock_guard<fibers::mutex> l(mtx_);
    while (soci_sessions.empty()) {
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
  service->async([ses = soci_session.get()]() { ses->begin(); }).get();
}

void sql_session::commit()
{
  service->async([ses = soci_session.get()]() { ses->commit(); }).get();
}

void sql_session::rollback()
{
  service->async([ses = soci_session.get()]() { ses->rollback(); }).get();
}

void sql_session::listen(const std::string& channel, notify_handler_t handler)
{
  // register handler
  {
    std::lock_guard<fibers::mutex> l(notify_mtx);
    notify_handlers[channel] = handler;
  }

  // issue LISTEN command on the DB connection
  service
      ->async([ses = soci_session.get(), channel]() {
        try {
          *ses << (std::string("LISTEN ") + channel + ";");
        } catch (...) {
        }
      })
      .get();

  // start watcher using libpq socket
  try {
    auto backend = static_cast<soci::postgresql_session_backend*>(
        soci_session->get_backend());
    if (!backend) return;
    PGconn* conn = backend->conn_;
    if (!conn) return;

    int sock = PQsocket(conn);
    if (sock < 0) return;

    // create stream_descriptor if not present (C++11 compatible)
    if (!notify_stream) {
      notify_stream.reset(
          new asio::posix::stream_descriptor(service->get_io_service(), sock));
    }

    if (notify_running) return;
    notify_running = true;

    // shared, re-entrant handler which uses weak_ptr to avoid touching
    // the session after it has been destroyed.
    auto weak_self = std::weak_ptr<sql_session>(shared_from_this());
    auto handler_ptr = std::make_shared<
        std::function<void(const boost::system::error_code&)>>();

    *handler_ptr = [weak_self,
                    handler_ptr](const boost::system::error_code& ec) {
      auto self = weak_self.lock();
      if (!self) return;  // session destroyed, abort

      if (ec) {
        std::lock_guard<fibers::mutex> l(self->notify_mtx);
        self->notify_running = false;
        return;
      }

      // obtain current PGconn from the live session backend
      PGconn* conn = nullptr;
      try {
        auto backend = static_cast<soci::postgresql_session_backend*>(
            self->soci_session->get_backend());
        if (backend) conn = backend->conn_;
      } catch (...) {
      }
      if (!conn) {
        std::lock_guard<fibers::mutex> l(self->notify_mtx);
        self->notify_running = false;
        return;
      }

      if (PQconsumeInput(conn) == 0) {
        std::lock_guard<fibers::mutex> l(self->notify_mtx);
        self->notify_running = false;
        return;
      }

      PGnotify* n = nullptr;
      while ((n = PQnotifies(conn)) != nullptr) {
        std::string ch = n->relname ? n->relname : std::string();
        std::string payload = n->extra ? n->extra : std::string();
        PQfreemem(n);

        // copy handler under lock
        sql_session::notify_handler_t hcopy;
        {
          std::lock_guard<fibers::mutex> l(self->notify_mtx);
          auto it = self->notify_handlers.find(ch);
          if (it != self->notify_handlers.end()) hcopy = it->second;
        }

        if (hcopy) {
          // dispatch via service; get service from locked session
          auto svc = self->service;
          if (svc) svc->execute([hcopy, ch, payload]() { hcopy(ch, payload); });
        }
      }

      // re-arm async wait using the live session
      try {
        if (self->notify_stream && self->notify_running) {
          self->notify_stream->async_wait(
              asio::posix::stream_descriptor::wait_read, *handler_ptr);
        }
      } catch (...) {
        std::lock_guard<fibers::mutex> l(self->notify_mtx);
        self->notify_running = false;
      }
    };

    // start first wait
    notify_stream->async_wait(asio::posix::stream_descriptor::wait_read,
                              *handler_ptr);
  } catch (...) {
  }
}

void sql_session::unlisten(const std::string& channel)
{
  // remove handler
  bool last = false;
  {
    std::lock_guard<fibers::mutex> l(notify_mtx);
    notify_handlers.erase(channel);
    last = notify_handlers.empty();
  }

  // issue UNLISTEN
  service
      ->async([ses = soci_session.get(), channel]() {
        try {
          *ses << (std::string("UNLISTEN ") + channel + ";");
        } catch (...) {
        }
      })
      .get();

  if (last) unlisten_all();
}

void sql_session::unlisten_all()
{
  {
    std::lock_guard<fibers::mutex> l(notify_mtx);
    notify_handlers.clear();
  }

  // issue UNLISTEN *
  service
      ->async([ses = soci_session.get()]() {
        try {
          *ses << std::string("UNLISTEN *;");
        } catch (...) {
        }
      })
      .get();

  // cancel and clear descriptor
  try {
    if (notify_stream) {
      boost::system::error_code ec;
      notify_stream->cancel(ec);
      notify_stream.reset();
    }
  } catch (...) {
  }
  std::lock_guard<fibers::mutex> l(notify_mtx);
  notify_running = false;
}
}  // namespace asyik
