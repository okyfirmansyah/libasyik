#ifndef LIBASYIK_ERROR_HPP
#define LIBASYIK_ERROR_HPP

#include <boost/asio/error.hpp>
#include <stdexcept>

namespace asyik {

#define ASYIK_DEFINE_RUNTIME_ERROR(child_error, parent_error, default_code) \
  class child_error : public parent_error {                                 \
   public:                                                                  \
    child_error(const std::string& s) : parent_error(default_code, s) {}    \
                                                                            \
    template <typename T>                                                   \
    child_error(T&& t, const std::string& s)                                \
        : parent_error(std::forward<T>(t), s)                               \
    {}                                                                      \
  }

// obsolete(too generic)
ASYIK_DEFINE_RUNTIME_ERROR(io_error, boost::system::system_error,
                           boost::asio::error::connection_reset);
ASYIK_DEFINE_RUNTIME_ERROR(network_error, io_error,
                           boost::asio::error::network_down);

ASYIK_DEFINE_RUNTIME_ERROR(network_timeout_error, network_error,
                           boost::asio::error::timed_out);
ASYIK_DEFINE_RUNTIME_ERROR(network_unreachable_error, network_error,
                           boost::asio::error::host_unreachable);
ASYIK_DEFINE_RUNTIME_ERROR(network_expired_error, network_error,
                           boost::asio::error::shut_down);

ASYIK_DEFINE_RUNTIME_ERROR(file_error, io_error,
                           boost::asio::error::bad_descriptor);
ASYIK_DEFINE_RUNTIME_ERROR(resource_error, io_error,
                           boost::asio::error::bad_descriptor);

ASYIK_DEFINE_RUNTIME_ERROR(input_error, boost::system::system_error,
                           boost::asio::error::invalid_argument);
ASYIK_DEFINE_RUNTIME_ERROR(invalid_input_error, input_error,
                           boost::asio::error::invalid_argument);
ASYIK_DEFINE_RUNTIME_ERROR(out_of_range_error, input_error,
                           boost::asio::error::invalid_argument);
ASYIK_DEFINE_RUNTIME_ERROR(not_found_error, input_error,
                           boost::asio::error::invalid_argument);
ASYIK_DEFINE_RUNTIME_ERROR(unexpected_input_error, input_error,
                           boost::asio::error::invalid_argument);
ASYIK_DEFINE_RUNTIME_ERROR(overflow_error, input_error,
                           boost::asio::error::invalid_argument);

ASYIK_DEFINE_RUNTIME_ERROR(unexpected_error, boost::system::system_error,
                           boost::asio::error::shut_down);
ASYIK_DEFINE_RUNTIME_ERROR(already_expired_error, boost::system::system_error,
                           boost::asio::error::shut_down);
ASYIK_DEFINE_RUNTIME_ERROR(already_closed_error, boost::system::system_error,
                           boost::asio::error::shut_down);
ASYIK_DEFINE_RUNTIME_ERROR(already_exists_error, boost::system::system_error,
                           boost::asio::error::bad_descriptor);
ASYIK_DEFINE_RUNTIME_ERROR(timeout_error, boost::system::system_error,
                           boost::asio::error::timed_out);
ASYIK_DEFINE_RUNTIME_ERROR(service_terminated_error,
                           boost::system::system_error,
                           boost::asio::error::shut_down);

}  // namespace asyik

#endif