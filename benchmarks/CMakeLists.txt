cmake_minimum_required(VERSION 3.14)
project(libasyik_bench)

# ── Release build with full optimizations ─────────────────────────────────
# Always build benchmarks in Release mode regardless of parent build type
set(BENCH_COMPILE_FLAGS -O3 -DNDEBUG -march=native)

add_executable(bench_server libasyik/bench_server.cpp)

target_compile_options(bench_server PRIVATE ${BENCH_COMPILE_FLAGS})

# Inherit include paths that libasyik sources use
target_include_directories(bench_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/aixlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/cppcodec
)

# Link against the libasyik target defined by the parent CMakeLists.txt
target_link_libraries(bench_server PRIVATE libasyik)

# ── HTTP pipeline profiling ────────────────────────────────────────────────
# Enable with:  cmake -DLIBASYIK_BENCH_PROFILING=ON ..
# Adds per-stage timing to http_connection::start() and prints a breakdown
# table every ASYIK_PROF_INTERVAL seconds (default 5) at runtime.
#
# Note: the flag must be applied to *both* libasyik (where the server-side
# HTTP template is instantiated) and bench_server (which hosts the reporter
# thread and uses the inline profiling globals).
option(LIBASYIK_BENCH_PROFILING "Profile HTTP pipeline stages in bench_server" OFF)
if(LIBASYIK_BENCH_PROFILING)
    message(STATUS "HTTP pipeline profiling ENABLED (-DLIBASYIK_HTTP_PROFILING)")
    target_compile_definitions(libasyik   PRIVATE LIBASYIK_HTTP_PROFILING)
    target_compile_definitions(bench_server PRIVATE LIBASYIK_HTTP_PROFILING)
endif()

message(STATUS "Benchmark target: bench_server (port 8080 by default)")

# ── bench_beast: raw Boost.Beast direct async server (no libasyik) ────────────
# Re-running find_package here is idempotent; it reuses the Boost installation
# already discovered by src/CMakeLists.txt.  bench_beast intentionally does NOT
# link libasyik — the whole point is to measure the raw Beast/Asio ceiling.
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_executable(bench_beast beast/bench_beast.cpp)

target_compile_features(bench_beast PRIVATE cxx_std_17)
target_compile_options(bench_beast PRIVATE ${BENCH_COMPILE_FLAGS})

target_include_directories(bench_beast PRIVATE ${Boost_INCLUDE_DIR})

# Boost.Beast and Boost.Asio are header-only on Linux — only Threads needed.
target_link_libraries(bench_beast PRIVATE Threads::Threads)

message(STATUS "Benchmark target: bench_beast (port 8086 by default)")

# ── bench_asio: raw Boost.Asio TCP server with hand-rolled HTTP (no Beast) ───
# Bypasses Beast entirely — only uses Boost.Asio for TCP + manual HTTP parsing.
# This isolates whether Beast's HTTP parser/serializer layer adds overhead.
add_executable(bench_asio asio/bench_asio.cpp)

target_compile_features(bench_asio PRIVATE cxx_std_17)
target_compile_options(bench_asio PRIVATE ${BENCH_COMPILE_FLAGS})

target_include_directories(bench_asio PRIVATE ${Boost_INCLUDE_DIR})

target_link_libraries(bench_asio PRIVATE Threads::Threads)

message(STATUS "Benchmark target: bench_asio (port 8085 by default)")

# ── bench_libasyik_raw: libasyik fibers + raw Asio TCP (no Beast HTTP) ───────
# Uses asyik::service for fiber scheduling but bypasses the built-in HTTP server.
# TCP accept/read/write use raw Asio async APIs + use_fiber_future.
add_executable(bench_libasyik_raw libasyik_raw/bench_libasyik_raw.cpp)

target_compile_features(bench_libasyik_raw PRIVATE cxx_std_17)
target_compile_options(bench_libasyik_raw PRIVATE ${BENCH_COMPILE_FLAGS})

target_include_directories(bench_libasyik_raw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/aixlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/cppcodec
)

target_link_libraries(bench_libasyik_raw PRIVATE libasyik)

message(STATUS "Benchmark target: bench_libasyik_raw (port 8091 by default)")

# ── bench_drogon: Drogon HTTP framework benchmark ────────────────────────────
find_package(Drogon QUIET)
if(Drogon_FOUND)
    add_executable(bench_drogon drogon/bench_drogon.cpp)
    target_compile_features(bench_drogon PRIVATE cxx_std_17)
    target_compile_options(bench_drogon PRIVATE ${BENCH_COMPILE_FLAGS})
    target_link_libraries(bench_drogon PRIVATE Drogon::Drogon)
    message(STATUS "Benchmark target: bench_drogon (port 8093 by default)")
else()
    message(WARNING "Drogon not found — bench_drogon will NOT be built. "
                    "Install drogon or build from https://github.com/drogonframework/drogon")
endif()

# ── bench_asio_iouring: Boost.Asio with io_uring backend (no epoll) ──────────
# Same code as bench_asio, but compiled with BOOST_ASIO_HAS_IO_URING=1 and
# BOOST_ASIO_DISABLE_EPOLL=1 so that io_context uses io_uring under the hood.
find_library(URING_LIB uring)
find_path(URING_INCLUDE_DIR liburing.h)

if(URING_LIB AND URING_INCLUDE_DIR)
    # Extra find_package calls needed for libasyik_iouring library rebuild
    find_package(Boost COMPONENTS context atomic fiber date_time url REQUIRED)
    find_package(OpenSSL REQUIRED)

    # Asio-over-io_uring variant
    add_executable(bench_asio_iouring asio/bench_asio.cpp)
    target_compile_features(bench_asio_iouring PRIVATE cxx_std_17)
    target_compile_options(bench_asio_iouring PRIVATE ${BENCH_COMPILE_FLAGS})
    target_include_directories(bench_asio_iouring PRIVATE ${Boost_INCLUDE_DIR} ${URING_INCLUDE_DIR})
    target_compile_definitions(bench_asio_iouring PRIVATE
        BOOST_ASIO_HAS_IO_URING=1
        BOOST_ASIO_DISABLE_EPOLL=1
    )
    target_link_libraries(bench_asio_iouring PRIVATE Threads::Threads ${URING_LIB})
    message(STATUS "Benchmark target: bench_asio_iouring (Asio + io_uring backend, port 8088 by default)")

    # Beast-over-io_uring variant
    add_executable(bench_beast_iouring beast/bench_beast.cpp)
    target_compile_features(bench_beast_iouring PRIVATE cxx_std_17)
    target_compile_options(bench_beast_iouring PRIVATE ${BENCH_COMPILE_FLAGS})
    target_include_directories(bench_beast_iouring PRIVATE ${Boost_INCLUDE_DIR} ${URING_INCLUDE_DIR})
    target_compile_definitions(bench_beast_iouring PRIVATE
        BOOST_ASIO_HAS_IO_URING=1
        BOOST_ASIO_DISABLE_EPOLL=1
    )
    target_link_libraries(bench_beast_iouring PRIVATE Threads::Threads ${URING_LIB})
    message(STATUS "Benchmark target: bench_beast_iouring (Beast + io_uring backend, port 8089 by default)")

    # Raw io_uring (liburing only, no Boost)
    add_executable(bench_io_uring io_uring/bench_io_uring.cpp)
    target_compile_features(bench_io_uring PRIVATE cxx_std_17)
    target_compile_options(bench_io_uring PRIVATE ${BENCH_COMPILE_FLAGS})
    target_include_directories(bench_io_uring PRIVATE ${URING_INCLUDE_DIR})
    target_link_libraries(bench_io_uring PRIVATE Threads::Threads ${URING_LIB})
    message(STATUS "Benchmark target: bench_io_uring (raw liburing, port 8087 by default)")

    # libasyik with io_uring backend — we MUST rebuild the entire library with
    # the io_uring defines because Boost.Asio is header-only and the reactor
    # choice is baked in at compile time.  Simply adding the defines to the
    # executable is NOT enough — the Asio templates instantiated inside
    # libasyik's .cpp files (service.cpp, http_server_plain.cpp, etc.) would
    # still use epoll.
    #
    # Solution: build a separate static library "libasyik_iouring" from the
    # same sources, with BOOST_ASIO_HAS_IO_URING + BOOST_ASIO_DISABLE_EPOLL.
    set(LIBASYIK_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_library(libasyik_iouring STATIC
        ${LIBASYIK_SRC_DIR}/service.cpp
        ${LIBASYIK_SRC_DIR}/http_common.cpp
        ${LIBASYIK_SRC_DIR}/http_server_plain.cpp
        ${LIBASYIK_SRC_DIR}/http_client.cpp
        ${LIBASYIK_SRC_DIR}/http_static.cpp
        ${LIBASYIK_SRC_DIR}/http_websocket.cpp
        ${LIBASYIK_SRC_DIR}/http_websocket_plain.cpp
        ${LIBASYIK_SRC_DIR}/http_websocket_ssl.cpp
    )
    target_compile_features(libasyik_iouring PUBLIC cxx_std_17)
    target_compile_options(libasyik_iouring PRIVATE ${BENCH_COMPILE_FLAGS})
    target_compile_definitions(libasyik_iouring PUBLIC
        BOOST_ASIO_HAS_IO_URING=1
        BOOST_ASIO_DISABLE_EPOLL=1
    )
    target_include_directories(libasyik_iouring PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_include_directories(libasyik_iouring PRIVATE
        ${LIBASYIK_SRC_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/cppcodec
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/Catch2/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/aixlog/include
        ${URING_INCLUDE_DIR}
    )
    # Same Boost dependencies as the main libasyik
    target_include_directories(libasyik_iouring PUBLIC ${Boost_INCLUDE_DIR})
    target_link_libraries(libasyik_iouring
        Boost::fiber Boost::context Boost::date_time Boost::url
        Threads::Threads OpenSSL::SSL ${URING_LIB}
    )

    # Now the benchmark executable links against the io_uring-enabled library
    add_executable(bench_server_iouring libasyik/bench_server.cpp)
    target_compile_options(bench_server_iouring PRIVATE ${BENCH_COMPILE_FLAGS})
    target_compile_definitions(bench_server_iouring PRIVATE
        BOOST_ASIO_HAS_IO_URING=1
        BOOST_ASIO_DISABLE_EPOLL=1
    )
    target_include_directories(bench_server_iouring PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/aixlog/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../external/cppcodec
        ${URING_INCLUDE_DIR}
    )
    target_link_libraries(bench_server_iouring PRIVATE libasyik_iouring)
    message(STATUS "Benchmark target: bench_server_iouring (libasyik recompiled with io_uring, port 8090 by default)")
else()
    message(WARNING "liburing not found — io_uring benchmark targets will NOT be built. "
                    "Install liburing-dev or build from https://github.com/axboe/liburing")
endif()

message(STATUS "Build with: cmake -DLIBASYIK_BUILD_BENCHMARKS=ON ..")

# ── TCMalloc support for all benchmark targets ────────────────────────────────
if(LIBASYIK_USE_TCMALLOC)
    target_link_libraries(bench_server PRIVATE tcmalloc_lib)
    target_link_libraries(bench_beast PRIVATE tcmalloc_lib)
    target_link_libraries(bench_asio PRIVATE tcmalloc_lib)
    target_link_libraries(bench_libasyik_raw PRIVATE tcmalloc_lib)
    if(Drogon_FOUND)
        target_link_libraries(bench_drogon PRIVATE tcmalloc_lib)
    endif()
    if(URING_LIB AND URING_INCLUDE_DIR)
        target_link_libraries(bench_asio_iouring PRIVATE tcmalloc_lib)
        target_link_libraries(bench_beast_iouring PRIVATE tcmalloc_lib)
        target_link_libraries(bench_io_uring PRIVATE tcmalloc_lib)
        target_link_libraries(libasyik_iouring tcmalloc_lib)
        target_link_libraries(bench_server_iouring PRIVATE tcmalloc_lib)
    endif()
endif()

