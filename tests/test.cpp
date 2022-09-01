#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/memcache.hpp"
#include "libasyik/rate_limit.hpp"
#include "libasyik/service.hpp"
#ifdef LIBASYIK_SOCI_ENABLE
#include "libasyik/sql.hpp"
#endif

using namespace asyik;

// quick hack because somehow Catch2 will miss libasyik's test cases if
// we dont do this:
TEST_CASE("Invoke all test units", "[asyik]")
{
  _TEST_invoke_service();
  _TEST_invoke_http();
#ifdef LIBASYIK_SOCI_ENABLE
  _TEST_invoke_sql();
#endif

  _TEST_invoke_memcache();
  _TEST_invoke_rate_limit();
}
