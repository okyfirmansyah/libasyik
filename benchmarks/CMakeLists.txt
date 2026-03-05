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
message(STATUS "Build with: cmake -DLIBASYIK_BUILD_BENCHMARKS=ON ..")

