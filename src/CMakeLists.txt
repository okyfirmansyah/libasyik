cmake_minimum_required(VERSION 3.14)

add_library(${PROJECT_NAME} service.cpp http.cpp)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)

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
        target_include_directories(${PROJECT_NAME} PRIVATE /usr/include/postgresql)
        target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci)
        target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/postgresql)
        target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/sqlite3)
        target_link_libraries(${PROJECT_NAME} SOCI::soci_core SOCI::soci_postgresql SOCI::soci_sqlite3)
    endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} OpenSSL::SSL)
