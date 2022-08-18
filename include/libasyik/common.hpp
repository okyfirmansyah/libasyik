#ifndef LIBASYIK_COMMON_HPP
#define LIBASYIK_COMMON_HPP

#include "boost/config/helper_macros.hpp"
#include "boost/utility/string_view.hpp"
#include "version.hpp"

#define LIBASYIK_VERSION_STRING                                             \
  "Libasyik v" BOOST_STRINGIZE(LIBASYIK_VERSION_MAJOR) "." BOOST_STRINGIZE( \
      LIBASYIK_VERSION_MINOR) "." BOOST_STRINGIZE(LIBASYIK_VERSION_PATCH)

namespace asyik {
using string_view = boost::string_view;
}

#endif