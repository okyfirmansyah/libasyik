cmake_minimum_required(VERSION 3.14)
project(libasyik_test)

set(CMAKE_CXX_STANDARD 14)

add_executable(${PROJECT_NAME} test.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ../external/aixlog/include)
target_link_libraries(${PROJECT_NAME} libasyik Catch2::Catch2)
