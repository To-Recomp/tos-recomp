cmake_minimum_required(VERSION 3.20)
project(tos_prepare LANGUAGES CXX)

# tos-prepare: one command from the player's two disc images to a game instance (docs/PREPARE.md).
# A single C++17 program that reuses To-Recomp/common (disc reading, verification, extraction). The release
# version carries the recompiler, the C++ compiler and the libraries as a payload (src/payload.hpp), so the
# player needs nothing besides the program itself.
#
#   cmake -S prepare -B build-prepare -G Ninja [-DTOS_COMMON_DIR=../common]
#   cmake --build build-prepare && ctest --test-dir build-prepare

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The common checkout: a sibling of this repository, like gcrecomp (see ../CMakeLists.txt).
set(TOS_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../common" CACHE PATH "Path to the To-Recomp/common checkout")
if(NOT EXISTS "${TOS_COMMON_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "common not found at ${TOS_COMMON_DIR}: set -DTOS_COMMON_DIR=<path to To-Recomp/common>")
endif()
set(_tos_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
# Release builds carry the disc codecs (zlib, bzip2, lzma, zstd): their sonames differ between distributions
# (libbz2.so.1.0 on Debian, libbz2.so.1 on Fedora). Looking for static libraries only, after project().
option(TOS_PREPARE_STATIC_CODECS "Link zlib, bzip2, lzma and zstd statically" OFF)
if(TOS_PREPARE_STATIC_CODECS)
  set(_tos_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()
add_subdirectory("${TOS_COMMON_DIR}" common EXCLUDE_FROM_ALL)
if(TOS_PREPARE_STATIC_CODECS)
  set(CMAKE_FIND_LIBRARY_SUFFIXES "${_tos_suffixes}")
endif()
set(BUILD_TESTING "${_tos_testing}")

set(TOS_PREPARE_VERSION "0.1.0" CACHE STRING "Version printed by tos-prepare --version")

if(WIN32)
  set(TOS_PREPARE_PROCESS_SOURCE src/process_win32.cpp)
else()
  set(TOS_PREPARE_PROCESS_SOURCE src/process_posix.cpp)
endif()
add_library(tos_prepare_core STATIC
  src/common.cpp src/json.cpp src/reporter.cpp src/process.cpp ${TOS_PREPARE_PROCESS_SOURCE} src/pipeline.cpp src/toolkit.cpp src/stages.cpp
  src/sha256.cpp src/payload.cpp src/toolchain.cpp)
target_include_directories(tos_prepare_core PUBLIC src)
target_link_libraries(tos_prepare_core PUBLIC torecomp_common)
target_compile_definitions(tos_prepare_core PUBLIC TOS_PREPARE_VERSION="${TOS_PREPARE_VERSION}")
if(MSVC)
  target_compile_options(tos_prepare_core PUBLIC /utf-8)
  target_compile_options(tos_prepare_core PRIVATE /W4)
else()
  target_compile_options(tos_prepare_core PRIVATE -Wall -Wextra)
endif()
if(WIN32)
  target_compile_definitions(tos_prepare_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
find_package(Threads REQUIRED)
target_link_libraries(tos_prepare_core PUBLIC Threads::Threads)

add_executable(tos-prepare src/main.cpp)
target_link_libraries(tos-prepare PRIVATE tos_prepare_core)

# Adds the payload (toolkit + compiler) to a tos-prepare and inspects it: runs on the machine that builds a release
# (scripts/package-release.sh, docs/RELEASING.md).
add_executable(tos-payload tools/payload_tool.cpp)
target_link_libraries(tos-payload PRIVATE tos_prepare_core)

# Synthetic discs for the release smoke test (tests/release-smoke.sh): no game data. Built on request:
#   cmake --build build-prepare --target tos-synth-discs
add_executable(tos-synth-discs EXCLUDE_FROM_ALL tests/synth_discs.cpp)
target_link_libraries(tos-synth-discs PRIVATE tos_prepare_core)

include(CTest)
if(BUILD_TESTING)
  # Stand-ins for the recompiler, the C++ compiler and a child process (tests/stubs/fake_tool.cpp): portable programs,
  # so that the tests run the same on Linux and Windows.
  foreach(role cxx recompiler proc)
    string(TOUPPER ${role} role_upper)
    add_executable(fake-${role} tests/stubs/fake_tool.cpp)
    target_compile_definitions(fake-${role} PRIVATE FAKE_TOOL_${role_upper})
    set_target_properties(fake-${role} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/stubs")
    foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
      set_target_properties(fake-${role} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_CURRENT_BINARY_DIR}/stubs")
    endforeach()
  endforeach()
  add_executable(tos_prepare_tests tests/test_main.cpp)
  target_link_libraries(tos_prepare_tests PRIVATE tos_prepare_core)
  target_compile_definitions(tos_prepare_tests PRIVATE TOS_PREPARE_BIN="$<TARGET_FILE:tos-prepare>"
                             TOS_PREPARE_STUBS="${CMAKE_CURRENT_BINARY_DIR}/stubs")
  add_dependencies(tos_prepare_tests tos-prepare fake-cxx fake-recompiler fake-proc)
  add_test(NAME tos_prepare_tests COMMAND tos_prepare_tests)
endif()

install(TARGETS tos-prepare RUNTIME DESTINATION bin)
