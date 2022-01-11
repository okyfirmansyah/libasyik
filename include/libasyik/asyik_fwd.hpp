#ifndef LIBASYIK_ASYIK_FWD_HPP
#define LIBASYIK_ASYIK_FWD_HPP

#include <memory>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

namespace asyik
{

    class service;
    using service_ptr = std::shared_ptr<service>;
    using service_wptr = std::weak_ptr<service>;

    template <typename StreamType>
    class http_connection;
    template <typename StreamType>
    using http_connection_ptr = std::shared_ptr<http_connection<StreamType>>;
    template <typename StreamType>
    using http_connection_wptr = std::weak_ptr<http_connection<StreamType>>;

    class websocket;
    using websocket_ptr = std::shared_ptr<websocket>;

    template <typename StreamType>
    class http_server;
    template <typename StreamType>
    using http_server_ptr = std::shared_ptr<http_server<StreamType>>;
    template <typename StreamType>
    using http_server_wptr = std::weak_ptr<http_server<StreamType>>;

    class http_request;
    using http_request_ptr = std::shared_ptr<http_request>;
    using http_request_wptr = std::weak_ptr<http_request>;
    using http_result = uint16_t;

    using http_stream_type = boost::asio::ip::tcp::socket;
    using https_stream_type = boost::beast::ssl_stream<http_stream_type&>;

    class sql_pool;
    using sql_pool_wptr = std::weak_ptr<sql_pool>;
    using sql_pool_ptr = std::shared_ptr<sql_pool>;

    class sql_session;
    using sql_session_wptr = std::weak_ptr<sql_session>;
    using sql_session_ptr = std::shared_ptr<sql_session>;

    template <class Key, class T, int expiry, int segments, typename thread_policy>
    class memcache;

} // namespace asyik

#endif  // LIBASYIK_ASYIK_FWD_HPP