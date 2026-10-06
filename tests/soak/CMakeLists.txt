cmake_minimum_required(VERSION 3.14)
project(libasyik_soak)

# Long-running stability tests, run locally (not in CI). Always optimised
# with assertions and debug info, whatever the parent build type, so a given
# seed exercises the same code on every machine. See README.md.
add_executable(libasyik_soak
    soak_main.cpp
    soak_fiber.cpp
    soak_asio.cpp
    soak_http.cpp
    soak_misc.cpp
)
target_compile_options(libasyik_soak PRIVATE -O2 -g -UNDEBUG -Wall -Wno-unused-function -Wno-unused-variable)
target_compile_definitions(libasyik_soak PRIVATE
    LIBASYIK_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../certs")
target_include_directories(libasyik_soak PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../external/aixlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../external/cppcodec)
target_link_libraries(libasyik_soak PRIVATE libasyik)

if(LIBASYIK_ENABLE_SOCI)
    # the sql.pool scenario; same SOCI setup as the unit tests
    target_compile_definitions(libasyik_soak PRIVATE LIBASYIK_ENABLE_SOCI=1)
    target_include_directories(libasyik_soak PRIVATE
        /usr/include/postgresql
        /usr/local/include/soci
        /usr/local/include/soci/postgresql
        /usr/local/include/soci/sqlite3)
endif()
