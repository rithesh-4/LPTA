# Build the standalone unit test binary
cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Match the main build's compiler setup
if(NOT DEFINED LLVM_DIR OR LLVM_DIR STREQUAL "")
    if(DEFINED ENV{LLVM_DIR})
        set(LLVM_DIR "$ENV{LLVM_DIR}")
    endif()
endif()

find_package(LLVM REQUIRED CONFIG)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(test_utilities
    test_utilities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Detection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Tracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/JsonWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Snapshots.cpp
)

llvm_map_components_to_libnames(LLVM_LIBS core irreader passes analysis transformutils support)

target_include_directories(test_utilities PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${LLVM_INCLUDE_DIRS}
)

target_link_libraries(test_utilities PRIVATE ${LLVM_LIBS})
