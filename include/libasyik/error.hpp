#ifndef LIBASYIK_ERROR_HPP
#define LIBASYIK_ERROR_HPP

#include <stdexcept>

namespace asyik
{

#define ASYIK_DEFINE_RUNTIME_ERROR(child_error, parent_error) \
class child_error : public parent_error \
{ \
    public: \
    child_error(const std::string &s):parent_error(s) \
    {} \
} \

ASYIK_DEFINE_RUNTIME_ERROR(io_error, std::runtime_error);
ASYIK_DEFINE_RUNTIME_ERROR(network_error, io_error);
ASYIK_DEFINE_RUNTIME_ERROR(network_timeout_error, network_error);
ASYIK_DEFINE_RUNTIME_ERROR(network_unreachable_error, network_error);
ASYIK_DEFINE_RUNTIME_ERROR(network_expired_error, network_error);
ASYIK_DEFINE_RUNTIME_ERROR(file_error, io_error);
ASYIK_DEFINE_RUNTIME_ERROR(resource_error, io_error);

ASYIK_DEFINE_RUNTIME_ERROR(input_error, std::runtime_error);
ASYIK_DEFINE_RUNTIME_ERROR(invalid_input_error, input_error);
ASYIK_DEFINE_RUNTIME_ERROR(out_of_range_error, input_error);
ASYIK_DEFINE_RUNTIME_ERROR(not_found_error, input_error);
ASYIK_DEFINE_RUNTIME_ERROR(unexpected_input_error, input_error);

ASYIK_DEFINE_RUNTIME_ERROR(unexpected_error, std::runtime_error);
ASYIK_DEFINE_RUNTIME_ERROR(already_expired_error, std::runtime_error);
ASYIK_DEFINE_RUNTIME_ERROR(already_closed_error, std::runtime_error);
ASYIK_DEFINE_RUNTIME_ERROR(already_exists_error, std::runtime_error);
ASYIK_DEFINE_RUNTIME_ERROR(timeout_error, std::runtime_error);

}

#endif