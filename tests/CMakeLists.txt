cmake_minimum_required(VERSION 3.14)
project(libasyik_test)

SET(GCC_COVERAGE_COMPILE_FLAGS "-g -O0 -coverage -fprofile-arcs -ftest-coverage")
SET(GCC_COVERAGE_LINK_FLAGS    "-coverage -lgcov")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Werror=return-type -Wno-unused-variable")
SET(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} ${GCC_COVERAGE_COMPILE_FLAGS}" )
SET(CMAKE_EXE_LINKER_FLAGS_DEBUG  "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${GCC_COVERAGE_LINK_FLAGS}" )

add_executable(${PROJECT_NAME} test.cpp)
target_sources(${PROJECT_NAME} PRIVATE test_http.cpp test_service.cpp test_memcache.cpp test_rate_limit.cpp)

if(LIBASYIK_ENABLE_SOCI)
    target_sources(${PROJECT_NAME} PRIVATE test_sql.cpp)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/include/postgresql)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/postgresql)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/sqlite3)
endif()


target_include_directories(${PROJECT_NAME} PRIVATE ../external/aixlog/include)
target_link_libraries(${PROJECT_NAME} libasyik Catch2::Catch2)
