#ifndef LIBASYIK_ASYIK_ASIO_INTERNAL_HPP
#define LIBASYIK_ASYIK_ASIO_INTERNAL_HPP
#include <string>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/fiber/all.hpp>

namespace asio = boost::asio;
namespace fibers = boost::fibers;
namespace ip = boost::asio::ip;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = ip::tcp;

namespace asyik
{
    namespace internal
    {
        namespace socket
        {
            template <typename... Args>
            size_t async_read(Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();
                boost::asio::async_read(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const boost::system::error_code &ec,
                                                size_t sz) mutable {
                        if (!ec)
                            prom.set_value(sz);
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });
                return future.get();
            };

            template <typename... Args>
            size_t async_write(Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();
                boost::asio::async_write(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const boost::system::error_code &ec,
                                                size_t sz) mutable {
                        if (!ec)
                            prom.set_value(sz);
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("write_error")));
                    });
                return future.get();
            };

            template <typename... Args>
            size_t async_read_until(Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();
                boost::asio::async_read_until(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const boost::system::error_code &ec,
                                                size_t sz) mutable {
                        if (!ec)
                        {
                            prom.set_value(sz);
                        }
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });
                return future.get();
            };

            template <typename T>
            void async_timer_wait(T &&p)
            {
                boost::fibers::promise<void> promise;
                auto future = promise.get_future();

                p.async_wait(
                    [prom = std::move(promise)](const boost::system::error_code &) mutable {
                        prom.set_value();
                    });

                return future.get();
            };

            template <typename Resolver, typename... Args>
            tcp::resolver::results_type async_resolve(Resolver &res, Args &&... args)
            {
                boost::fibers::promise<tcp::resolver::results_type> promise;
                auto future = promise.get_future();

                res.async_resolve(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const beast::error_code &ec,
                                                tcp::resolver::results_type res) mutable {
                        if (!ec)
                            prom.set_value(res);
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });
                return future.get();
            };

            template <typename Conn, typename... Args>
            tcp::resolver::results_type::endpoint_type async_connect(Conn &con, Args &&... args)
            {
                boost::fibers::promise<tcp::resolver::results_type::endpoint_type> promise;
                auto future = promise.get_future();

                con.async_connect(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const beast::error_code &ec,
                                                tcp::resolver::results_type::endpoint_type res) mutable {
                        if (!ec)
                            prom.set_value(res);
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });
                return future.get();
            }
        } // namespace socket

        namespace http
        {
            template <typename... Args>
            size_t async_read(Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();
                boost::beast::http::async_read(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const boost::system::error_code &ec,
                                                size_t sz) mutable {
                        if (!ec)
                            prom.set_value(sz);
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("write_error")));
                    });
                return future.get();
            };

            template <typename... Args>
            size_t async_write(Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();
                boost::beast::http::async_write(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const boost::system::error_code &ec,
                                                size_t sz) mutable {
                        if (!ec)
                            prom.set_value(sz);
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("write_error")));
                    });
                return future.get();
            };
        } // namespace http

        namespace ssl
        {
            template <typename Conn, typename... Args>
            auto async_handshake(Conn &con, Args &&... args)
            {
                boost::fibers::promise<void> promise;
                auto future = promise.get_future();

                con.async_handshake(std::forward<Args>(args)...,
                                    [prom = std::move(promise)](const beast::error_code &ec) mutable {
                                    if (!ec)
                                        prom.set_value();
                                    else
                                        prom.set_exception(std::make_exception_ptr(
                                            std::runtime_error("read_error")));
                               });

                return std::move(future);
            }

            template <typename Conn, typename... Args>
            auto async_shutdown(Conn &con, Args &&... args)
            {
                boost::fibers::promise<void> promise;
                auto future = promise.get_future();

                con.async_shutdown(std::forward<Args>(args)...,
                                   [prom = std::move(promise)](const beast::error_code &ec) mutable {
                                   if (!ec)
                                       prom.set_value();
                                   else
                                       prom.set_exception(std::make_exception_ptr(
                                           std::runtime_error("read_error")));
                                    });

                return std::move(future);
            }
        }

        namespace websocket
        {
            template <typename Conn, typename... Args>
            size_t async_read(Conn &con, Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();

                con.async_read(std::forward<Args>(args)...,
                               [prom = std::move(promise)](const beast::error_code &ec,
                                                           size_t bytes_transferred) mutable {
                                   if (!ec)
                                       prom.set_value(bytes_transferred);
                                   else
                                       prom.set_exception(std::make_exception_ptr(
                                           std::runtime_error("read_error")));
                               });
                return future.get();
            }

            template <typename Conn, typename... Args>
            size_t async_read_some(Conn &con, Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();

                con.async_read_some(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const beast::error_code &ec,
                                                size_t bytes_transferred) mutable {
                        if (!ec)
                            prom.set_value(bytes_transferred);
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });
                return future.get();
            }

            template <typename Conn, typename... Args>
            size_t async_write(Conn &con, Args &&... args)
            {
                boost::fibers::promise<size_t> promise;
                auto future = promise.get_future();

                con.async_write(std::forward<Args>(args)...,
                                [prom = std::move(promise)](const beast::error_code &ec,
                                                            size_t bytes_transferred) mutable {
                                    if (!ec)
                                        prom.set_value(bytes_transferred);
                                    else
                                        prom.set_exception(std::make_exception_ptr(
                                            std::runtime_error("read_error")));
                                });
                return future.get();
            }

            template <typename Conn, typename... Args>
            void async_accept(Conn &con, Args &&... args)
            {
                boost::fibers::promise<void> promise;
                auto future = promise.get_future();

                con.async_accept(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const beast::error_code &ec) mutable {
                        if (!ec)
                            prom.set_value();
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });

                return future.get();
            }

            template <typename Conn, typename... Args>
            void async_handshake(Conn &con, Args &&... args)
            {
                boost::fibers::promise<void> promise;
                auto future = promise.get_future();

                con.async_handshake(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const beast::error_code &ec) mutable {
                        if (!ec)
                            prom.set_value();
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });

                return future.get();
            }

            template <typename Conn, typename... Args>
            auto async_close(Conn &con, Args &&... args)
            {
                boost::fibers::promise<void> promise;
                auto future = promise.get_future();

                con.async_close(
                    std::forward<Args>(args)...,
                    [prom = std::move(promise)](const beast::error_code &ec) mutable {
                        if (!ec)
                            prom.set_value();
                        else
                            prom.set_exception(
                                std::make_exception_ptr(std::runtime_error("read_error")));
                    });

                return future;
            }
        } // namespace websocket
    }     //namespace internal
} // namespace asyik

#endif // NFXCHNG_API_H
