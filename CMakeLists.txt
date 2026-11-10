cmake_minimum_required(VERSION 3.16)

project(
    pikaid
    VERSION 1.0.0
    DESCRIPTION "Official C implementation of the pikaid identifier format"
    HOMEPAGE_URL "https://github.com/pikaid/pikaid-c"
    LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(PIKAID_BUILD_TESTS "Build the pikaid test suite" ON)
option(PIKAID_BUILD_EXAMPLES "Build the pikaid examples" ON)
option(PIKAID_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PIKAID_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PIKAID_ENABLE_COVERAGE "Build with statement coverage instrumentation" OFF)
option(PIKAID_REQUIRE_SCHEMA_VALIDATION
    "Fail the schema check when Python jsonschema is unavailable" OFF)

if(PIKAID_ENABLE_SANITIZERS AND PIKAID_ENABLE_COVERAGE)
    message(FATAL_ERROR "PIKAID_ENABLE_SANITIZERS and PIKAID_ENABLE_COVERAGE are mutually exclusive.")
endif()

if(PIKAID_ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

if(PIKAID_ENABLE_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "PIKAID_ENABLE_COVERAGE is not supported with MSVC by this project.")
    endif()
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

add_library(pikaid
    src/pikaid.c
    src/pikaid_clock.c
    src/pikaid_entropy.c)
add_library(pikaid::pikaid ALIAS pikaid)

target_compile_features(pikaid PUBLIC c_std_99)

set_target_properties(pikaid PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    EXPORT_NAME pikaid)

if(BUILD_SHARED_LIBS)
    # Only PIKAID_API symbols are exported; internal entropy and clock helpers
    # stay private to the shared object.
    target_compile_definitions(pikaid PRIVATE PIKAID_BUILDING_LIBRARY)
    target_compile_definitions(pikaid PUBLIC PIKAID_SHARED)
    set(PIKAID_PC_DEFINES " -DPIKAID_SHARED")
else()
    set(PIKAID_PC_DEFINES "")
endif()

target_include_directories(pikaid
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(WIN32)
    target_link_libraries(pikaid PRIVATE bcrypt)
endif()

if(MSVC)
    target_compile_options(pikaid PRIVATE /W4)
    if(PIKAID_WARNINGS_AS_ERRORS)
        target_compile_options(pikaid PRIVATE /WX)
    endif()
else()
    target_compile_options(pikaid PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wcast-qual
        -Wwrite-strings)
    if(PIKAID_WARNINGS_AS_ERRORS)
        target_compile_options(pikaid PRIVATE -Werror)
    endif()
endif()

if(PIKAID_BUILD_EXAMPLES)
    add_executable(pikaid_quickstart examples/quickstart.c)
    target_link_libraries(pikaid_quickstart PRIVATE pikaid::pikaid)
endif()

if(BUILD_TESTING AND PIKAID_BUILD_TESTS)
    add_subdirectory(tests)
endif()

install(TARGETS pikaid
    EXPORT pikaidTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES include/pikaid.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(PIKAID_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/pikaid)

install(EXPORT pikaidTargets
    FILE pikaidTargets.cmake
    NAMESPACE pikaid::
    DESTINATION ${PIKAID_CMAKE_INSTALL_DIR})

configure_package_config_file(
    cmake/pikaidConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/pikaidConfig.cmake
    INSTALL_DESTINATION ${PIKAID_CMAKE_INSTALL_DIR})

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/pikaidConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/pikaidConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/pikaidConfigVersion.cmake
    DESTINATION ${PIKAID_CMAKE_INSTALL_DIR})

# Relocatable pkg-config metadata.
#
# `cmake --install ... --prefix <dir>` is the documented way to install into a
# staging directory, and it does not reconfigure the build. Deriving the prefix
# from ${pcfiledir} keeps the generated file correct for whatever prefix is
# chosen at install time. The .pc file itself lives in
# <prefix>/<libdir>/pkgconfig, so walk back one level for "pkgconfig" plus one
# per component of CMAKE_INSTALL_LIBDIR.
#
# If a destination was configured as an absolute path the .pc file is no longer
# positioned relative to the prefix, so fall back to the configure-time prefix
# for that entry only.
set(PIKAID_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
if(NOT IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    string(REPLACE "/" ";" _pikaid_pc_libdir_parts "${CMAKE_INSTALL_LIBDIR}")
    list(LENGTH _pikaid_pc_libdir_parts _pikaid_pc_libdir_depth)
    set(_pikaid_pc_parent "..")
    foreach(_pikaid_pc_part RANGE 1 ${_pikaid_pc_libdir_depth})
        string(APPEND _pikaid_pc_parent "/..")
    endforeach()
    set(PIKAID_PC_PREFIX "\${pcfiledir}/${_pikaid_pc_parent}")
endif()

if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(PIKAID_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
    set(PIKAID_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(PIKAID_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
else()
    set(PIKAID_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
endif()

configure_file(cmake/pikaid.pc.in ${CMAKE_CURRENT_BINARY_DIR}/pikaid.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pikaid.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
