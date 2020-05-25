cmake_minimum_required(VERSION 3.14)
project(libasyik)

set(CMAKE_CXX_STANDARD 14)

add_library(${PROJECT_NAME} mainloop.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# add the required libraries here
find_package(Boost COMPONENTS context fiber REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} Boost::fiber Boost::context)
endif()

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

