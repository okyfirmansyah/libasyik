#include "libasyik/sql.hpp"

#include <libpq-fe.h>

#ifdef _WIN32
#include <boost/asio/ip/tcp.hpp>
#else
#include <unistd.h>

#include <boost/asio/posix/stream_descriptor.hpp>
#endif
#include <memory>

#include "aixlog.hpp"
#include "boost/fiber/all.hpp"
#include "libasyik/common.hpp"
#include "libasyik/internal/soci_internal.hpp"
#include "libasyik/service.hpp"

namespace fibers = boost::fibers;

namespace asyik {

// Wrap a duplicate of libpq's socket so that closing the Asio object (on
// unlisten or session destruction) never closes the socket libpq still owns.
static std::unique_ptr<sql_session::notify_stream_type> make_notify_stream(
    asio::io_context& io, int sock)
{
#ifdef _WIN32
  WSAPROTOCOL_INFOW info;
  if (WSADuplicateSocketW(static_cast<SOCKET>(sock), GetCurrentProcessId(),
                          &info) != 0)
    return nullptr;
  SOCKET dup = WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                          FROM_PROTOCOL_INFO, &info, 0, WSA_FLAG_OVERLAPPED);
  if (dup == INVALID_SOCKET) return nullptr;
  auto protocol = info.iAddressFamily == AF_INET6 ? asio::ip::tcp::v6()
                                                  : asio::ip::tcp::v4();
  auto stream = std::make_unique<sql_session::notify_stream_type>(io);
  boost::system::error_code ec;
  stream->assign(protocol, dup, ec);
  if (ec) {
    closesocket(dup);
    return nullptr;
  }
  return stream;
#else
  int fd = ::dup(sock);
  if (fd < 0) return nullptr;
  auto stream = std::make_unique<sql_session::notify_stream_type>(io);
  boost::system::error_code ec;
  stream->assign(fd, ec);
  if (ec) {
    ::close(fd);
    return nullptr;
  }
  return stream;
#endif
}

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

// libpq connection of a PostgreSQL session; nullptr for other backends
static PGconn* pg_conn(soci::session& ses)
{
  if (ses.get_backend_name() != "postgresql") return nullptr;
  auto backend =
      static_cast<soci::postgresql_session_backend*>(ses.get_backend());
  return backend ? backend->conn_ : nullptr;
}

void sql_session::begin()
{
  service
      ->async([self = this]() {
        std::lock_guard<std::mutex> l(self->conn_mtx);
        self->soci_session->begin();
        self->dispatch_notifications();
      })
      .get();
}

void sql_session::commit()
{
  service
      ->async([self = this]() {
        std::lock_guard<std::mutex> l(self->conn_mtx);
        self->soci_session->commit();
        self->dispatch_notifications();
      })
      .get();
}

void sql_session::rollback()
{
  service
      ->async([self = this]() {
        std::lock_guard<std::mutex> l(self->conn_mtx);
        self->soci_session->rollback();
        self->dispatch_notifications();
      })
      .get();
}

void sql_session::dispatch_notifications()
{
  if (!notify_running) return;
  PGconn* conn = pg_conn(*soci_session);
  if (!conn) return;

  PGnotify* n = nullptr;
  while ((n = PQnotifies(conn)) != nullptr) {
    std::string ch = n->relname ? n->relname : std::string();
    std::string payload = n->extra ? n->extra : std::string();
    PQfreemem(n);

    notify_handler_t handler;
    {
      std::lock_guard<fibers::mutex> l(notify_mtx);
      auto it = notify_handlers.find(ch);
      if (it != notify_handlers.end()) handler = it->second;
    }
    if (handler && service)
      service->execute([handler, ch, payload]() { handler(ch, payload); });
  }
}

// Runs a LISTEN/UNLISTEN command; failures (e.g. a dead connection) are
// ignored, the watcher notices those on its own.
static void run_listen_command(service_ptr service, std::mutex& conn_mtx,
                               soci::session& ses, const std::string& sql)
{
  service
      ->async([&conn_mtx, &ses, sql]() {
        std::lock_guard<std::mutex> l(conn_mtx);
        try {
          ses << sql;
        } catch (...) {
        }
      })
      .get();
}

void sql_session::listen(const std::string& channel, notify_handler_t handler)
{
  {
    std::lock_guard<fibers::mutex> l(notify_mtx);
    notify_handlers[channel] = handler;
  }

  run_listen_command(service, conn_mtx, *soci_session,
                     "LISTEN " + channel + ";");

  // watch libpq's socket for incoming notifications
  PGconn* conn = pg_conn(*soci_session);
  if (!conn) return;
  int sock = PQsocket(conn);
  if (sock < 0) return;

  if (!notify_stream) {
    notify_stream = make_notify_stream(service->get_io_service(), sock);
    if (!notify_stream) return;
  }
  if (!notify_retry)
    notify_retry =
        std::make_unique<asio::steady_timer>(service->get_io_service());

  if (notify_running.exchange(true)) return;

  // Re-arming handler; holds the session weakly so it never outlives it.
  auto weak_self = std::weak_ptr<sql_session>(shared_from_this());
  auto handler_ptr =
      std::make_shared<std::function<void(const boost::system::error_code&)>>();

  *handler_ptr = [weak_self, handler_ptr](const boost::system::error_code& ec) {
    auto self = weak_self.lock();
    if (!self) return;  // session destroyed
    if (ec) {           // cancelled by unlisten
      self->notify_running = false;
      return;
    }

    // A query running on a worker owns the connection; it reads (and then
    // dispatches) whatever arrives meanwhile. Look again shortly instead of
    // blocking the service thread. Reading here while the query waits for
    // its reply used to steal the reply and leave the query hanging.
    std::unique_lock<std::mutex> busy(self->conn_mtx, std::try_to_lock);
    if (!busy) {
      self->notify_retry->expires_after(std::chrono::milliseconds(2));
      self->notify_retry->async_wait(*handler_ptr);
      return;
    }

    PGconn* conn = pg_conn(*self->soci_session);
    if (!conn || PQconsumeInput(conn) == 0) {
      self->notify_running = false;  // connection lost
      return;
    }
    self->dispatch_notifications();
    busy.unlock();

    if (self->notify_stream && self->notify_running)
      self->notify_stream->async_wait(notify_stream_type::wait_read,
                                      *handler_ptr);
  };

  notify_stream->async_wait(notify_stream_type::wait_read, *handler_ptr);
}

void sql_session::unlisten(const std::string& channel)
{
  bool last = false;
  {
    std::lock_guard<fibers::mutex> l(notify_mtx);
    notify_handlers.erase(channel);
    last = notify_handlers.empty();
  }

  run_listen_command(service, conn_mtx, *soci_session,
                     "UNLISTEN " + channel + ";");

  if (last) unlisten_all();
}

void sql_session::unlisten_all()
{
  {
    std::lock_guard<fibers::mutex> l(notify_mtx);
    notify_handlers.clear();
  }

  run_listen_command(service, conn_mtx, *soci_session, "UNLISTEN *;");

  // stop the watcher
  boost::system::error_code ec;
  if (notify_retry) notify_retry->cancel();
  if (notify_stream) {
    notify_stream->cancel(ec);
    notify_stream.reset();
  }
  notify_running = false;
}
}  // namespace asyik
