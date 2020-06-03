#ifndef LIBASYIK_COMMON_HPP
#define LIBASYIK_COMMON_HPP

#include "boost/fiber/all.hpp"
#include "boost/utility/string_view.hpp"
#include "version.hpp"

#define LIBASYIK_VERSION_STRING "Libasyik v" BOOST_STRINGIZE(BOOST_BEAST_MAJOR) "."  \
                                             BOOST_STRINGIZE(BOOST_BEAST_MINOR) "."  \
                                             BOOST_STRINGIZE(BOOST_BEAST_PATCH)

namespace asyik
{
    using string_view = boost::string_view;
}

#endif