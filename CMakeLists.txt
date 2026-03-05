cmake_minimum_required(VERSION 3.14)
project(libasyik)

SET(LIBASYIK_ENABLE_SOCI ON CACHE BOOL "Libasyik Enable SOCI")
SET(LIBASYIK_ENABLE_SSL_SERVER ON CACHE BOOL "Libasyik Enable SSL/HTTPS Server Support")

if(LIBASYIK_ENABLE_SSL_SERVER)
    add_definitions("-DLIBASYIK_ENABLE_SSL_SERVER=${LIBASYIK_ENABLE_SSL_SERVER}")
endif()

add_subdirectory(external/Catch2)
add_subdirectory(src)
add_subdirectory(tests)

# Optional benchmark executables (built in Release/-O3 regardless of parent build type)
# Enable with:  cmake -DLIBASYIK_BUILD_BENCHMARKS=ON ..
option(LIBASYIK_BUILD_BENCHMARKS "Build HTTP benchmark server executables" OFF)
if(LIBASYIK_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
                                                  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_include_directories(${PROJECT_NAME} INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
                                                     "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_include_directories(${PROJECT_NAME} PRIVATE external/aixlog/include)
target_include_directories(${PROJECT_NAME} PRIVATE external/cppcodec)
target_include_directories(${PROJECT_NAME} INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/external>")

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}
  DESTINATION  "${CMAKE_INSTALL_LIBDIR}"
)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/external/aixlog/include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/external/"
)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/external/cppcodec/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/external/"
)

install(
    FILES ${PROJECT_NAME}-config.cmake DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)

install(
    EXPORT ${PROJECT_NAME}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)