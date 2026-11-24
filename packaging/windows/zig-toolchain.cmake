# CMake toolchain file: build for x86_64 Windows (MinGW-w64 ABI) with Zig's clang, on Windows or on any host
# that can run Zig (a Linux machine cross-compiles the Windows release this way). This is the compiler the
# Windows release ships, so the runtime libraries it contains and the game code built on the player's machine
# use the same C++ library (libc++).
#
#   TOS_ZIG=/path/to/zig cmake -S gcrecomp -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=packaging/windows/zig-toolchain.cmake ...
#
# TOS_ZIG is read from the environment (or -DTOS_ZIG=...). The target is baseline x86-64: no -mcpu=native.
if(NOT TOS_ZIG)
  set(TOS_ZIG "$ENV{TOS_ZIG}")
endif()
if(NOT TOS_ZIG)
  message(FATAL_ERROR "set TOS_ZIG (environment or -DTOS_ZIG=) to the zig executable")
endif()
file(TO_CMAKE_PATH "${TOS_ZIG}" TOS_ZIG)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER "${TOS_ZIG}")
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_CXX_COMPILER "${TOS_ZIG}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")
set(CMAKE_C_FLAGS_INIT "-target x86_64-windows-gnu")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-windows-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-target x86_64-windows-gnu")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target x86_64-windows-gnu")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-target x86_64-windows-gnu")
# There is no resource compiler in Zig's package; nothing here needs one.
set(CMAKE_RC_COMPILER_WORKS TRUE)
# Static libraries: `zig ar` (archives go through the same tool on every host).
set(CMAKE_C_CREATE_STATIC_LIBRARY "\"${TOS_ZIG}\" ar qc <TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "\"${TOS_ZIG}\" ar qc <TARGET> <OBJECTS>")
