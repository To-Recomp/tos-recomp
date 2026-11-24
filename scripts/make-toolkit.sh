#!/usr/bin/env bash
# Assemble the toolkit that tos-prepare needs (docs/PREPARE.md): the recompiler, the runtime libraries,
# the headers the generated code includes, the boot harness source, dumps.toml and the symbol files.
# Everything in it is ours; nothing is derived from the game, so it may be shipped.
#
#   scripts/make-toolkit.sh --gcrecomp DIR --out DIR [--build DIR] [--jobs N] [--no-build] [--fetch-sdl3]
#                           [--windows --zig PATH]
#
#   --gcrecomp DIR  a gcrecomp checkout (default: ../gcrecomp next to this repository)
#   --out DIR       the toolkit folder to create (replaced if it exists)
#   --build DIR     gcrecomp build folder (default: $TMPDIR/gcrecomp-toolkit-build); it is configured and
#                   built unless --no-build is given (use that to reuse an existing build)
#   --jobs N        build parallelism (default 4)
#   --fetch-sdl3    build SDL3 from source for the headers the libraries are compiled against (a machine without
#                   SDL3 development files, such as CI). The toolkit still links the player's own SDL3 (-lSDL3).
#   --windows       build the toolkit of the Windows release: everything is compiled with Zig (--zig PATH, the zig
#                   executable) for x86_64-windows-gnu, from any host that runs Zig; SDL3 is built from source
#                   and linked statically into the libraries' link line, so the player needs no SDL3.
#                   The toolkit names the bundled compiler as {program_dir}/toolchain/zig/zig (docs/PREPARE.md).
#
# The toolkit's libraries are compiled by the compiler CMake picks here. A player's compiler must use
# the same C++ standard library (libstdc++ with gcc or clang on Linux), see docs/PREPARE.md.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
gcrecomp="$here/../gcrecomp"; out=; build="${TMPDIR:-/var/tmp}/gcrecomp-toolkit-build"; jobs=4; do_build=1; fetch_sdl3=OFF
windows=0; zig=; PYTHON="${PYTHON:-python3}"
while [ $# -gt 0 ]; do
  case "$1" in
    --gcrecomp) gcrecomp="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --fetch-sdl3) fetch_sdl3=ON; shift ;;
    --no-build) do_build=0; shift ;;
    --windows) windows=1; shift ;;
    --zig) zig="$2"; shift 2 ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$out" ] || { echo "--out is required" >&2; exit 2; }
[ -f "$gcrecomp/CMakeLists.txt" ] || { echo "gcrecomp not found at $gcrecomp (--gcrecomp)" >&2; exit 2; }
gcrecomp="$(cd "$gcrecomp" && pwd)"
# Windows tools (CMake, Zig) want C:/... paths; under Git Bash `pwd` gives /c/...
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
if [ "$windows" = 1 ]; then
  [ -n "$zig" ] && [ -f "$zig" ] || { echo "--windows needs --zig PATH (the zig executable; scripts/fetch-zig.sh)" >&2; exit 2; }
  exe=.exe; fetch_sdl3=ON
  export TOS_ZIG="$(native "$(cd "$(dirname "$zig")" && pwd)/$(basename "$zig")")"
else
  exe=
fi

toolchain=()
[ "$windows" = 1 ] && toolchain=(-DCMAKE_TOOLCHAIN_FILE="$(native "$here/packaging/windows/zig-toolchain.cmake")")
if [ "$do_build" = 1 ]; then
  cmake -S "$(native "$gcrecomp")" -B "$(native "$build")" -G Ninja -DCMAKE_BUILD_TYPE=Release "${toolchain[@]}" \
    -DGCRECOMP_BUILD_EXAMPLE=OFF -DGCRECOMP_BUILD_TESTS=OFF -DBUILD_TESTING=OFF -DGCRECOMP_FETCH_SDL3="$fetch_sdl3" >/dev/null
  cmake --build "$build" -j "$jobs" --target gcrecomp_recompiler gcrecomp_gp gcrecomp_runtime gcrecomp_gx gcrecomp_audio gcrecomp_input
fi
libs=(gcrecomp_gp gcrecomp_runtime gcrecomp_gx gcrecomp_audio gcrecomp_input)
for l in "${libs[@]}"; do [ -f "$build/lib$l.a" ] || { echo "missing $build/lib$l.a (build first)" >&2; exit 1; }; done
[ -f "$build/gcrecomp_recompiler$exe" ] || { echo "missing $build/gcrecomp_recompiler$exe" >&2; exit 1; }
if [ "$windows" = 1 ]; then
  [ -f "$build/_deps/sdl3-build/libSDL3.a" ] || { echo "missing the SDL3 static library in $build/_deps/sdl3-build" >&2; exit 1; }
fi

rm -rf "$out"
mkdir -p "$out/bin" "$out/lib" "$out/include" "$out/src" "$out/symbols"
cp "$build/gcrecomp_recompiler$exe" "$out/bin/"
for l in "${libs[@]}"; do cp "$build/lib$l.a" "$out/lib/"; done
if [ "$windows" = 1 ]; then
  # SDL3 (zlib licence) is linked statically into the game: ship its library and its licence text.
  cp "$build/_deps/sdl3-build/libSDL3.a" "$out/lib/"
  mkdir -p "$out/licenses"
  cp "$build/_deps/sdl3-src/LICENSE.txt" "$out/licenses/SDL3-LICENSE.txt"
fi
cp -r "$gcrecomp/include/gcrecomp" "$out/include/"
cp "$here/src/main.cpp" "$out/src/main.cpp"
cp "$here/dumps.toml" "$out/dumps.toml"
cp "$here"/symbols/GQSF.csv "$here"/symbols/GQSF.extra_funcs.txt "$out/symbols/"
cp "$here/LICENSE" "$out/LICENSE"

rev() { git -C "$1" rev-parse --short=12 HEAD 2>/dev/null || echo unknown; }
dirty() { [ -z "$(git -C "$1" status --porcelain 2>/dev/null)" ] || echo "+dirty"; }
if [ "$windows" = 1 ]; then
  # What the libraries and the static SDL3 need from Windows (the link line of gcrecomp's gp_replay, minus the
  # D3D11/XAudio2 backends this game does not use and whose import libraries Zig does not ship), then the linker's
  # stack size: the game runs deep recursion on the main thread, as `ulimit -s unlimited` allows on Linux.
  sys_libs="-lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lxinput1_4 -Wl,--stack,536870912"
else
  sys_libs=$(pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")
fi

"$PYTHON" - "$out" "$(rev "$gcrecomp")$(dirty "$gcrecomp")" "$(rev "$here")$(dirty "$here")" "$sys_libs" "$windows" <<'PY'
import json, sys
out, gcrecomp_rev, tos_rev, sys_libs, windows = sys.argv[1:6]
windows = windows == "1"
libs = ["gcrecomp_gp", "gcrecomp_runtime", "gcrecomp_gx", "gcrecomp_audio", "gcrecomp_input"]
tk = {
    "format": 1,
    "game_id": "GQSF",
    "project": "tos",
    "dumps": "dumps.toml",
    "recompiler": "bin/gcrecomp_recompiler" + (".exe" if windows else ""),
    "symbols": {"dir": "symbols", "csv": "GQSF.csv", "extra_funcs": "GQSF.extra_funcs.txt"},
    "build": {
        "include": ["include"],
        "sources": ["src/main.cpp"],
        "std": "gnu++20",
        "flags": ["-O1", "-w"],
        "libs": ["lib/lib%s.a" % l for l in libs] + (["lib/libSDL3.a"] if windows else []),
        # Linked after the libraries (not used when compiling): system libraries, then linker options.
        "system_libs": sys_libs.split() + ([] if windows else ["-pthread"]),
        # {program_dir}: the folder of tos-prepare, where the Windows archive keeps its bundled Zig.
        "compilers": (["{program_dir}/toolchain/zig/zig c++ -target x86_64-windows-gnu"] if windows else []) + ["clang++", "g++", "c++"],
        "executable": "tos_boot" + (".exe" if windows else ""),
    },
    # How to start the game, relative to the instance folder. The harness takes <main.dol> <files> <seconds>;
    # 0 is not "forever" in the harness yet, see docs/PREPARE.md (open points).
    "run": {
        "args": ["game/sys/main.dol", "game/files", "86400"],
        "env": {"GCRECOMP_CARD_IMAGE": "saves/card-a.raw"},
        "unlimited_stack": not windows,
    },
    "disk": {"generated_per_input_byte": 32, "objects_per_generated_byte": 1.6, "executable_bytes": 100000000},
    "versions": {"gcrecomp": gcrecomp_rev, "tos_recomp": tos_rev},
}
with open(out + "/toolkit.json", "w") as f:
    json.dump(tk, f, indent=2)
    f.write("\n")
PY
echo "toolkit written to $out ($(du -sh "$out" | cut -f1))"
