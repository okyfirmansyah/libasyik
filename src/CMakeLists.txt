cmake_minimum_required(VERSION 3.14)

add_library(${PROJECT_NAME} 
    service.cpp 
    http_common.cpp 
    http_server_plain.cpp
    http_client.cpp 
    http_static.cpp
    http_websocket.cpp
    http_websocket_plain.cpp
    http_websocket_ssl.cpp
    tls.cpp
)

if(LIBASYIK_ENABLE_SSL_SERVER)
    target_sources(${PROJECT_NAME} PRIVATE http_server_ssl.cpp)
    # PUBLIC so that consumers (including installed ones) see make_https_server
    target_compile_definitions(${PROJECT_NAME} PUBLIC LIBASYIK_ENABLE_SSL_SERVER=1)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)

if(LIBASYIK_COVERAGE_FLAGS)
    target_compile_options(${PROJECT_NAME} PRIVATE ${LIBASYIK_COVERAGE_FLAGS})
    # PUBLIC: whatever links the instrumented static library needs libgcov
    target_link_options(${PROJECT_NAME} PUBLIC --coverage)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../external/cppcodec)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../external/Catch2/include)

# add the required libraries here
find_package(Boost COMPONENTS context atomic fiber date_time url REQUIRED)
if(Boost_FOUND)
    target_include_directories(${PROJECT_NAME} PUBLIC ${Boost_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} Boost::fiber Boost::context Boost::date_time Boost::url)
endif()

if(LIBASYIK_ENABLE_SOCI)
    find_package(SOCI REQUIRED)
    add_definitions("-DLIBASYIK_ENABLE_SOCI=${LIBASYIK_ENABLE_SOCI}")
    target_sources(${PROJECT_NAME} PRIVATE sql.cpp)
    if(SOCI_FOUND)
        target_link_libraries(${PROJECT_NAME} SOCI::soci_core SOCI::soci_postgresql SOCI::soci_sqlite3)
        # sql.hpp is public and includes SOCI headers without the soci/
        # prefix; locate them relative to the imported target (e.g.
        # /usr/local/include/soci, vcpkg's include/soci)
        get_target_property(SOCI_INCLUDE_ROOT SOCI::soci_core INTERFACE_INCLUDE_DIRECTORIES)
        list(GET SOCI_INCLUDE_ROOT 0 SOCI_INCLUDE_ROOT)
        target_include_directories(${PROJECT_NAME} PUBLIC
            ${SOCI_INCLUDE_ROOT}/soci
            ${SOCI_INCLUDE_ROOT}/soci/postgresql
            ${SOCI_INCLUDE_ROOT}/soci/sqlite3)
        # SOCI's PostgreSQL header includes libpq-fe.h (Debian/Ubuntu keep it
        # in /usr/include/postgresql)
        find_path(LIBASYIK_LIBPQ_INCLUDE_DIR libpq-fe.h PATH_SUFFIXES postgresql)
        if(LIBASYIK_LIBPQ_INCLUDE_DIR)
            target_include_directories(${PROJECT_NAME} PUBLIC ${LIBASYIK_LIBPQ_INCLUDE_DIR})
        endif()
    endif()
endif()

if(WIN32)
    # Target Windows 10+; keep <windows.h> from defining min/max and the
    # wingdi ERROR macro (which would turn LOG(ERROR) into LOG(0)).
    target_compile_definitions(${PROJECT_NAME} PUBLIC
        _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX NOGDI)
    # winmm: timeBeginPeriod() in service::run()
    # crypt32: Windows certificate store access in tls_win_certstore.cpp
    target_sources(${PROJECT_NAME} PRIVATE tls_win_certstore.cpp)
    target_link_libraries(${PROJECT_NAME} winmm crypt32)
endif()

if(MSVC)
    # /bigobj: Beast/Asio templates exceed the default COFF section limit.
    # /Zc:__cplusplus: report the real standard instead of 199711L.
    # /utf-8: sources contain non-ASCII characters.
    target_compile_options(${PROJECT_NAME} PUBLIC /bigobj /Zc:__cplusplus /utf-8)
endif()

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} OpenSSL::SSL)
