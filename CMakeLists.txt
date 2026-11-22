cmake_minimum_required(VERSION 3.20)
project(tos_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# gcrecomp (the To-Recomp fork) provides the runtime libraries.
set(GCRECOMP_DIR "${CMAKE_SOURCE_DIR}/../gcrecomp" CACHE PATH "Path to the gcrecomp checkout")
set(GCRECOMP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(GCRECOMP_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(${GCRECOMP_DIR} gcrecomp EXCLUDE_FROM_ALL)

# Code generated from the player's own main.dol (never committed).
set(GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated/gcrecomp" CACHE PATH "Output of gcrecomp_recompiler")
file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS "${GENERATED_DIR}/*.cpp")
if(NOT GENERATED_SOURCES)
  message(FATAL_ERROR "No generated sources in ${GENERATED_DIR}. Run gcrecomp_recompiler first.")
endif()

add_executable(tos_boot src/main.cpp ${GENERATED_SOURCES})
target_include_directories(tos_boot PRIVATE ${GENERATED_DIR})
target_compile_options(tos_boot PRIVATE -O1 -w)
target_link_libraries(tos_boot PRIVATE gcrecomp_runtime gcrecomp_gx gcrecomp_audio gcrecomp_input pthread)
