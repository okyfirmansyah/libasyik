#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"
#include "libasyik/sql.hpp"

using namespace asyik;

// quick hack because somehow Catch2 will miss libasyik's test cases if
// we dont do this:
TEST_CASE("Invoke all test units", "[asyik]")
{
  _TEST_invoke_service();
  _TEST_invoke_http();
  _TEST_invoke_sql();
}