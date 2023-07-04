cmake_minimum_required(VERSION 3.14)
project(libasyik)

SET(LIBASYIK_ENABLE_SOCI ON CACHE BOOL "Libasyik Enable SOCI")

add_subdirectory(external/Catch2)
add_subdirectory(src)
add_subdirectory(tests)

target_include_directories(${PROJECT_NAME} PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
                                                  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_include_directories(${PROJECT_NAME} INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
                                                     "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_include_directories(${PROJECT_NAME} PRIVATE external/aixlog/include)
target_include_directories(${PROJECT_NAME} INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/external>")

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)

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
    FILES ${PROJECT_NAME}-config.cmake DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)

install(
    EXPORT ${PROJECT_NAME}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)