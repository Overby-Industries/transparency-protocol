cmake_minimum_required(VERSION 3.16)
project(otb1 LANGUAGES CXX VERSION 0.1.0)

# libotb1 -- the C++ reference implementation of OTB-1. The format itself is
# defined by schema/otb-1.schema.json; see cpp/include/otb1/otb1.hpp.
#
# Consume it from another CMake project with:
#
#     add_subdirectory(path/to/transparency-protocol)
#     target_link_libraries(your_target PRIVATE otb1::otb1)
#
# That builds only the library. The tests and the otb1_check tool are built
# when this repository is the top-level project.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(OTB1_IS_TOP_LEVEL ON)
else()
    set(OTB1_IS_TOP_LEVEL OFF)
endif()

option(OTB1_BUILD_TESTS "Build the libotb1 test suite" ${OTB1_IS_TOP_LEVEL})
option(OTB1_BUILD_TOOLS "Build the otb1_check command-line tool" ${OTB1_IS_TOP_LEVEL})

add_library(otb1 STATIC
    cpp/src/json.cpp
    cpp/src/time.cpp
    cpp/src/otb1.cpp
)

add_library(otb1::otb1 ALIAS otb1)

target_compile_features(otb1 PUBLIC cxx_std_17)
set_target_properties(otb1 PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(otb1 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(otb1 PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(otb1 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

if(OTB1_BUILD_TOOLS)
    add_executable(otb1_check cpp/tools/otb1_check.cpp)
    target_link_libraries(otb1_check PRIVATE otb1)
    # The tool's JSON-only mode drives the parser directly, which is private
    # to the library.
    target_include_directories(otb1_check PRIVATE cpp/src)
    if(MSVC)
        target_compile_options(otb1_check PRIVATE /utf-8)
    endif()
endif()

if(OTB1_BUILD_TESTS)
    enable_testing()

    add_executable(otb1_tests
        cpp/tests/main.cpp
        cpp/tests/json_tests.cpp
        cpp/tests/time_tests.cpp
        cpp/tests/otb1_tests.cpp
    )
    target_link_libraries(otb1_tests PRIVATE otb1)
    target_include_directories(otb1_tests PRIVATE cpp/src)
    # The codec tests read examples/ and tests/invalid/ from this repository:
    # the same files the JSON Schema is validated against.
    target_compile_definitions(otb1_tests PRIVATE
        OTB1_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(otb1_tests PRIVATE /utf-8)
    endif()

    add_test(NAME otb1_tests COMMAND otb1_tests)
endif()
