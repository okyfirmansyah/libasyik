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
)

if(LIBASYIK_ENABLE_SSL_SERVER)
    target_sources(${PROJECT_NAME} PRIVATE http_server_ssl.cpp)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../external/cppcodec)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../external/Catch2/include)

# Windows-specific compile definitions and link libraries
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(${PROJECT_NAME} ws2_32 mswsock)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
endif()

# add the required libraries here
find_package(Boost COMPONENTS context atomic fiber date_time url REQUIRED)
if(Boost_FOUND)
    target_link_libraries(${PROJECT_NAME} Boost::fiber Boost::context Boost::date_time Boost::url)
endif()

if(LIBASYIK_ENABLE_SOCI)
    find_package(SOCI REQUIRED)
    add_definitions("-DLIBASYIK_ENABLE_SOCI=${LIBASYIK_ENABLE_SOCI}")
    target_sources(${PROJECT_NAME} PRIVATE sql.cpp)
    if(SOCI_FOUND)
        if(NOT WIN32)
            target_include_directories(${PROJECT_NAME} PRIVATE /usr/include/postgresql)
            target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci)
            target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/postgresql)
            target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include/soci/sqlite3)
        else()
            # On Windows with vcpkg, find_package(SOCI) provides SOCI::* targets
            # with INTERFACE_INCLUDE_DIRECTORIES pointing to the vcpkg include dir.
            # Add SOCI subdirectories so bare #include "soci.h" / "soci-postgresql.h" work.
            find_package(PostgreSQL QUIET)
            if(PostgreSQL_FOUND)
                target_include_directories(${PROJECT_NAME} PRIVATE ${PostgreSQL_INCLUDE_DIRS})
            endif()
            # vcpkg installs SOCI headers under include/soci/, include/soci/postgresql/, etc.
            target_include_directories(${PROJECT_NAME} PRIVATE ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/soci)
            target_include_directories(${PROJECT_NAME} PRIVATE ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/soci/postgresql)
            target_include_directories(${PROJECT_NAME} PRIVATE ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/soci/sqlite3)
        endif()
        target_link_libraries(${PROJECT_NAME} SOCI::soci_core SOCI::soci_postgresql SOCI::soci_sqlite3)
    endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} OpenSSL::SSL)
