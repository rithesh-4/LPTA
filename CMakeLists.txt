cmake_minimum_required(VERSION 3.20)

project(LPTA_Test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# LLVM 22.1.8 installation
# ------------------------------------------------------------

set(LLVM_DIR
    "C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/lib/cmake/llvm"
)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR = ${LLVM_DIR}")

# LLVM's required include directories / definitions
include_directories(${LLVM_INCLUDE_DIRS})

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
    lpta_test.cpp
)

# ------------------------------------------------------------
# LLVM components required by our program
# ------------------------------------------------------------

llvm_map_components_to_libnames(
    LLVM_LIBS
    core
    irreader
    passes
    analysis
    transformutils
)

target_link_libraries(
    lpta_test
    PRIVATE
    ${LLVM_LIBS}
)