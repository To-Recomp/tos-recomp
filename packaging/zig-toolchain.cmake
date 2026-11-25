# CMake toolchain file: build for a target with Zig's clang, on any host that can run Zig. This is the compiler the
# releases ship (inside tos-prepare, docs/RELEASING.md), so the runtime libraries they contain and the game code
# built on the player's machine use the same C++ library (libc++).
#
#   TOS_ZIG=/path/to/zig TOS_ZIG_TARGET=x86_64-linux-gnu.2.31 \
#     cmake -S gcrecomp -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=packaging/zig-toolchain.cmake ...
#
# Read from the environment (or -D...):
#   TOS_ZIG          the zig executable (required)
#   TOS_ZIG_TARGET   Zig's target triple: x86_64-windows-gnu, or x86_64-linux-gnu.<glibc version> (required). The
#                    glibc suffix is the oldest glibc the result runs on; Zig builds against that version's headers and
#                    symbol versions whatever the glibc of the build machine is.
#   TOS_ZIG_HOST_INCLUDE  Linux only: ";"-separated host include folders appended to the search path AFTER Zig's own
#                    libc headers (-idirafter), for the headers only the build machine has (X11, Wayland, ALSA ... that
#                    SDL3 compiles against and loads with dlopen at run time). Default: /usr/include and its
#                    multiarch subfolder when they exist.
# The target is baseline x86-64: no -mcpu=native.
foreach(_v TOS_ZIG TOS_ZIG_TARGET TOS_ZIG_HOST_INCLUDE)
  if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
    set(${_v} "$ENV{${_v}}")
  endif()
endforeach()
if(NOT TOS_ZIG)
  message(FATAL_ERROR "set TOS_ZIG (environment or -DTOS_ZIG=) to the zig executable")
endif()
if(NOT TOS_ZIG_TARGET)
  message(FATAL_ERROR "set TOS_ZIG_TARGET (environment or -DTOS_ZIG_TARGET=), e.g. x86_64-windows-gnu or x86_64-linux-gnu.2.31")
endif()
file(TO_CMAKE_PATH "${TOS_ZIG}" TOS_ZIG)

if(TOS_ZIG_TARGET MATCHES "windows")
  set(CMAKE_SYSTEM_NAME Windows)
  set(CMAKE_SYSTEM_PROCESSOR AMD64)
  # There is no resource compiler in Zig's package; nothing here needs one.
  set(CMAKE_RC_COMPILER_WORKS TRUE)
elseif(TOS_ZIG_TARGET MATCHES "^x86_64-linux")
  set(CMAKE_SYSTEM_NAME Linux)
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
else()
  message(FATAL_ERROR "TOS_ZIG_TARGET ${TOS_ZIG_TARGET}: only x86_64-windows-gnu and x86_64-linux-gnu.<glibc> are supported")
endif()

if(CMAKE_HOST_WIN32)
  set(CMAKE_C_COMPILER "${TOS_ZIG}")
  set(CMAKE_C_COMPILER_ARG1 "cc")
  set(CMAKE_CXX_COMPILER "${TOS_ZIG}")
  set(CMAKE_CXX_COMPILER_ARG1 "c++")
  # CMake before 3.30 does not hand the "cc"/"c++" argument to the projects it builds to test the compiler.
  list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_C_COMPILER_ARG1 CMAKE_CXX_COMPILER_ARG1)
else()
  # Older CMake (Ubuntu 22.04 has 3.22) loses the "cc"/"c++" argument in its compiler tests: use two one-line scripts.
  foreach(_lang cc c++)
    set(_wrapper "${CMAKE_BINARY_DIR}/tos-zig/zig-${_lang}")
    file(WRITE "${_wrapper}" "#!/bin/sh\nexec \"${TOS_ZIG}\" ${_lang} \"$@\"\n")
    file(CHMOD "${_wrapper}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  endforeach()
  set(CMAKE_C_COMPILER "${CMAKE_BINARY_DIR}/tos-zig/zig-cc")
  set(CMAKE_CXX_COMPILER "${CMAKE_BINARY_DIR}/tos-zig/zig-c++")
endif()
set(_tos_flags "-target ${TOS_ZIG_TARGET}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(NOT TOS_ZIG_HOST_INCLUDE)
    foreach(_d /usr/include /usr/include/x86_64-linux-gnu)
      if(IS_DIRECTORY "${_d}")
        list(APPEND TOS_ZIG_HOST_INCLUDE "${_d}")
      endif()
    endforeach()
  endif()
  # Zig is not a compiler CMake can ask where the distribution keeps its libraries: Debian and Ubuntu use
  # /usr/lib/x86_64-linux-gnu. SDL3 looks there for X11, ALSA ... to know which drivers to build (it opens them with
  # dlopen at run time, it does not link them).
  set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
  foreach(_d ${TOS_ZIG_HOST_INCLUDE})
    string(APPEND _tos_flags " -idirafter ${_d}")
  endforeach()
endif()
set(CMAKE_C_FLAGS_INIT "${_tos_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_tos_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-target ${TOS_ZIG_TARGET}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target ${TOS_ZIG_TARGET}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-target ${TOS_ZIG_TARGET}")
# Static libraries: `zig ar` (archives go through the same tool on every host).
set(CMAKE_C_CREATE_STATIC_LIBRARY "\"${TOS_ZIG}\" ar qc <TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "\"${TOS_ZIG}\" ar qc <TARGET> <OBJECTS>")
