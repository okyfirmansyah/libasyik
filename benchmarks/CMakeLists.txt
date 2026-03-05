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

message(STATUS "Benchmark target: bench_server (port 8080 by default)")
message(STATUS "Build with: cmake -DLIBASYIK_BUILD_BENCHMARKS=ON ..")
