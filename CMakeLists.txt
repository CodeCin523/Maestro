cmake_minimum_required(VERSION 3.17)

# ------------------------------------------------------------------------------
# Project
# ------------------------------------------------------------------------------
project(
    MAESTRO
    VERSION 1.2.0
    LANGUAGES C
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MAESTRO_BUILD_TESTS "Build MAESTRO tests" OFF)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
find_package(Harp REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED xcb)
    pkg_check_modules(X11 REQUIRED x11)
    pkg_check_modules(X11_XCB REQUIRED x11-xcb)
endif()

# ------------------------------------------------------------------------------
# Interface
# ------------------------------------------------------------------------------
add_library(MaestroInterface INTERFACE)
add_library(MAESTRO::Interface ALIAS MaestroInterface)

set_target_properties(
    MaestroInterface PROPERTIES
    EXPORT_NAME Interface
)

target_include_directories(
    MaestroInterface INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
    MaestroInterface INTERFACE
    HARP::Interface
)

# ------------------------------------------------------------------------------
# Shared Library
# ------------------------------------------------------------------------------
add_library(Maestro SHARED)
add_library(MAESTRO::Maestro ALIAS Maestro)

set_target_properties(
    Maestro PROPERTIES
    EXPORT_NAME Maestro
    PREFIX ""
)

target_sources(
    Maestro PRIVATE
    src/maestro_package.c
    src/impl/maestro_logger.c
)

if(WIN32)
    target_sources(
        Maestro PRIVATE
        src/impl/maestro_window_win32.c
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(
        Maestro PRIVATE
        src/impl/maestro_window_linux.c
    )
endif()

target_compile_features(
    Maestro PUBLIC
    c_std_23
)

target_link_libraries(
    Maestro PUBLIC
    MaestroInterface
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(
        Maestro PRIVATE
        ${XCB_LIBRARIES}
        ${X11_LIBRARIES}
        ${X11_XCB_LIBRARIES}
    )
    target_include_directories(
        Maestro PRIVATE
        ${XCB_INCLUDE_DIRS}
        ${X11_INCLUDE_DIRS}
        ${X11_XCB_INCLUDE_DIRS}
    )
endif()

target_include_directories(
    Maestro
    PRIVATE
        src
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(MAESTRO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Installation
# ------------------------------------------------------------------------------
include(GNUInstallDirs)

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    TARGETS
        MaestroInterface
        Maestro
    EXPORT MaestroTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    EXPORT MaestroTargets
    FILE MaestroTargets.cmake
    NAMESPACE MAESTRO::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Maestro
)

# ------------------------------------------------------------------------------
# Package Configuration
# ------------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/MaestroConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    cmake/PackageConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MaestroConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Maestro
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/MaestroConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/MaestroConfigVersion.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/Maestro
)
