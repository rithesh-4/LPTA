cmake_minimum_required(VERSION 3.20)

project(LPTA_Test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# LLVM discovery
# Try LLVM_DIR cmake variable first, then environment variable,
# then a sensible default path.
# ------------------------------------------------------------

if(NOT DEFINED LLVM_DIR OR LLVM_DIR STREQUAL "")
    if(DEFINED ENV{LLVM_DIR})
        set(LLVM_DIR "$ENV{LLVM_DIR}")
    else()
        message(FATAL_ERROR
            "LLVM_DIR not set. Please set it via:\n"
            "  cmake -DLLVM_DIR=<path-to-llvm-cmake> ...\n"
            "or set the LLVM_DIR environment variable.")
    endif()
endif()

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR = ${LLVM_DIR}")

# LLVM's required include directories / definitions


separate_arguments(
    LLVM_DEFINITIONS_LIST
    NATIVE_COMMAND
    ${LLVM_DEFINITIONS}
)

add_definitions(${LLVM_DEFINITIONS_LIST})

# ------------------------------------------------------------
# LPTA test executable
# ------------------------------------------------------------

add_executable(
    lpta_test
    src/main.cpp
    src/globals.cpp
    src/Metrics.cpp
    src/Detection.cpp
    src/Snapshots.cpp
    src/Codegen.cpp
    src/Tracker.cpp
    src/JsonWriter.cpp
)

# ------------------------------------------------------------
# LLVM components required by our program
# ------------------------------------------------------------

llvm_map_components_to_libnames(
    LLVM_LIBS
    core
    support
    irreader
    passes
    analysis
    transformutils
    target
    codegen
    x86asmparser
    x86codegen
    x86desc
    x86info
)

target_include_directories(lpta_test PRIVATE ${LLVM_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/inc)

target_link_libraries(
    lpta_test
    PRIVATE
    ${LLVM_LIBS}
)

# ------------------------------------------------------------
# Unit test executable
# ------------------------------------------------------------

add_executable(
    test_utilities
    tests/test_utilities.cpp
    src/globals.cpp
    src/Metrics.cpp
    src/Detection.cpp
    src/Tracker.cpp
    src/JsonWriter.cpp
    src/Snapshots.cpp
)

target_include_directories(test_utilities PRIVATE ${LLVM_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(test_utilities PRIVATE ${LLVM_LIBS})
