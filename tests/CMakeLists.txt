cmake_minimum_required(VERSION 3.14)
project(libasyik_test)

if(NOT MSVC)
    SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Werror=return-type -Wno-unused-variable")
endif()

add_executable(${PROJECT_NAME} test.cpp)
if(LIBASYIK_COVERAGE_FLAGS)
    # header-only parts of libasyik are compiled into the test TUs
    target_compile_options(${PROJECT_NAME} PRIVATE ${LIBASYIK_COVERAGE_FLAGS})
endif()
target_sources(${PROJECT_NAME} PRIVATE test_http.cpp test_digest_auth.cpp test_http_edge.cpp test_service.cpp test_memcache.cpp test_rate_limit.cpp test_static.cpp test_tls.cpp test_tls_server.cpp)
target_compile_definitions(${PROJECT_NAME} PRIVATE LIBASYIK_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")

if(LIBASYIK_ENABLE_SOCI)
    target_sources(${PROJECT_NAME} PRIVATE test_sql.cpp)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/include/postgresql)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/postgresql)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/sqlite3)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ../external/cppcodec/)
target_include_directories(${PROJECT_NAME} PRIVATE ../external/aixlog/include)
target_link_libraries(${PROJECT_NAME} libasyik Catch2::Catch2)
