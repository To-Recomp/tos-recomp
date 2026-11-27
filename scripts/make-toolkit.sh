#!/usr/bin/env bash
# Assemble the toolkit that tos-prepare needs (docs/PREPARE.md): the recompiler, the runtime libraries,
# the headers the generated code includes, the boot harness source, dumps.toml and the symbol files.
# Everything in it is ours; nothing is derived from the game, so it may be shipped.
#
#   scripts/make-toolkit.sh --gcrecomp DIR --out DIR [--overlay DIR] [--build DIR] [--jobs N] [--no-build] [--fetch-sdl3]
#                           [--zig PATH --target TRIPLE]
#
#   --gcrecomp DIR  a gcrecomp checkout (default: ../gcrecomp next to this repository)
#   --overlay DIR   a To-Recomp/overlay checkout, the in-game settings menu (default: ../overlay next to this repository)
#   --out DIR       the toolkit folder to create (replaced if it exists)
#   --build DIR     gcrecomp build folder (default: $TMPDIR/gcrecomp-toolkit-build); it is configured and
#                   built unless --no-build is given (use that to reuse an existing build)
#   --jobs N        build parallelism (default 4)
#   --fetch-sdl3    build SDL3 from source for the headers the libraries are compiled against (a machine without
#                   SDL3 development files). Without --zig the toolkit still links the machine's own SDL3 (-lSDL3).
#   --zig PATH      build the toolkit of a RELEASE: everything is compiled with this Zig (the zig executable) for
#                   --target, from any host that runs Zig; SDL3 is built from source and linked statically into the
#                   game, so nothing has to be installed where the game is built or played. The toolkit names the
#                   compiler that ships in the release, {root}/toolchain/zig/zig, and refuses $CXX (its libraries
#                   are built with Zig's libc++, which another compiler's C++ library does not mix with).
#   --target TRIPLE with --zig: x86_64-windows-gnu, or x86_64-linux-gnu.<oldest glibc to run on>, e.g. x86_64-linux-gnu.2.31
#                   (packaging/zig-toolchain.cmake). On Linux SDL3 loads X11, Wayland, ALSA, PulseAudio ... with dlopen at
#                   run time: the machine that builds needs their HEADERS (CI installs them), the player's machine
#                   needs nothing. The result is checked for that: see tests/check-linux-game.sh.
#
# Without --zig the libraries are compiled by the compiler CMake picks here, and a player's compiler must use the same
# C++ standard library (libstdc++ with gcc or clang on Linux): a development toolkit.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
gcrecomp="$here/../gcrecomp"; overlay="$here/../overlay"; out=; build="${TMPDIR:-/var/tmp}/gcrecomp-toolkit-build"; jobs=4; do_build=1; fetch_sdl3=OFF
zig=; target=; PYTHON="${PYTHON:-python3}"
while [ $# -gt 0 ]; do
  case "$1" in
    --gcrecomp) gcrecomp="$2"; shift 2 ;;
    --overlay) overlay="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --fetch-sdl3) fetch_sdl3=ON; shift ;;
    --no-build) do_build=0; shift ;;
    --zig) zig="$2"; shift 2 ;;
    --target) target="$2"; shift 2 ;;
    -h|--help) sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$out" ] || { echo "--out is required" >&2; exit 2; }
[ -f "$gcrecomp/CMakeLists.txt" ] || { echo "gcrecomp not found at $gcrecomp (--gcrecomp)" >&2; exit 2; }
[ -f "$overlay/CMakeLists.txt" ] || { echo "overlay not found at $overlay (--overlay)" >&2; exit 2; }
gcrecomp="$(cd "$gcrecomp" && pwd)"
overlay="$(cd "$overlay" && pwd)"
# Windows tools (CMake, Zig) want C:/... paths; under Git Bash `pwd` gives /c/...
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
windows=0; linux_zig=0; exe=
if [ -n "$zig" ]; then
  [ -f "$zig" ] || { echo "no zig executable at $zig (scripts/fetch-zig.sh)" >&2; exit 2; }
  case "$target" in
    x86_64-windows-gnu) windows=1; exe=.exe ;;
    x86_64-linux-gnu.*) linux_zig=1 ;;
    *) echo "--zig needs --target x86_64-windows-gnu or x86_64-linux-gnu.<glibc version>" >&2; exit 2 ;;
  esac
  fetch_sdl3=ON
  export TOS_ZIG="$(native "$(cd "$(dirname "$zig")" && pwd)/$(basename "$zig")")" TOS_ZIG_TARGET="$target"
elif [ -n "$target" ]; then
  echo "--target needs --zig" >&2; exit 2
fi
with_zig=$((windows + linux_zig))

toolchain=()
[ "$with_zig" = 1 ] && toolchain=(-DCMAKE_TOOLCHAIN_FILE="$(native "$here/packaging/zig-toolchain.cmake")")
if [ "$do_build" = 1 ]; then
  cmake -S "$(native "$gcrecomp")" -B "$(native "$build")" -G Ninja -DCMAKE_BUILD_TYPE=Release "${toolchain[@]}" \
    -DGCRECOMP_BUILD_EXAMPLE=OFF -DGCRECOMP_BUILD_TESTS=OFF -DBUILD_TESTING=OFF -DGCRECOMP_FETCH_SDL3="$fetch_sdl3" \
    -DGCRECOMP_OVERLAY_DIR="$(native "$overlay")" -DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=ON >/dev/null
  cmake --build "$build" -j "$jobs" --target gcrecomp_recompiler gcrecomp_gp torecomp_overlay torecomp_overlay_core torecomp_imgui gcrecomp_runtime gcrecomp_gx gcrecomp_audio gcrecomp_input
fi
# The overlay (libtorecomp_overlay, its manifest core and Dear ImGui) follows gcrecomp_gp, which uses it; the player's
# compile only links them. They are built in subfolders of the build: look them up.
libs=(gcrecomp_gp torecomp_overlay torecomp_overlay_core torecomp_imgui gcrecomp_runtime gcrecomp_gx gcrecomp_audio gcrecomp_input)
libfile() { find "$build" -name "lib$1.a" -not -path '*/CMakeFiles/*' 2>/dev/null | head -n 1; }
for l in "${libs[@]}"; do [ -n "$(libfile "$l")" ] || { echo "missing lib$l.a under $build (build first)" >&2; exit 1; }; done
[ -f "$build/gcrecomp_recompiler$exe" ] || { echo "missing $build/gcrecomp_recompiler$exe" >&2; exit 1; }
if [ "$with_zig" = 1 ]; then
  [ -f "$build/_deps/sdl3-build/libSDL3.a" ] || { echo "missing the SDL3 static library in $build/_deps/sdl3-build" >&2; exit 1; }
fi

rm -rf "$out"
mkdir -p "$out/bin" "$out/lib" "$out/include" "$out/src" "$out/symbols"
cp "$build/gcrecomp_recompiler$exe" "$out/bin/"
for l in "${libs[@]}"; do cp "$(libfile "$l")" "$out/lib/"; done
mkdir -p "$out/licenses"
# Dear ImGui (MIT) is linked into every game: ship its licence, found through the cache of the build.
imgui_license="$(sed -n 's/^TOREC_IMGUI_LICENSE:[A-Z]*=//p' "$build/CMakeCache.txt" 2>/dev/null | head -n 1)"
[ -f "$imgui_license" ] || { echo "no Dear ImGui licence: TOREC_IMGUI_LICENSE in $build/CMakeCache.txt is '${imgui_license}' (build with the imgui source fetched)" >&2; exit 1; }
cp "$imgui_license" "$out/licenses/ImGui-LICENSE.txt"
# nlohmann/json (MIT, header only) is compiled into libtorecomp_overlay_core.a: ship its licence (fetched by the build).
json_license="$build/_deps/nlohmann_json-src/LICENSE.MIT"
[ -f "$json_license" ] || { echo "no nlohmann/json licence at $json_license" >&2; exit 1; }
cp "$json_license" "$out/licenses/nlohmann-json-LICENSE.txt"
# The overlay's font (Cabin, SIL OFL 1.1) is compiled into libtorecomp_overlay.a: ship its licence too.
[ -f "$overlay/src/ui/assets/OFL.txt" ] || { echo "missing $overlay/src/ui/assets/OFL.txt" >&2; exit 1; }
cp "$overlay/src/ui/assets/OFL.txt" "$out/licenses/OverlayFont-OFL.txt"
cp "$overlay/LICENSE" "$out/licenses/overlay-LICENSE.txt"
cp "$gcrecomp/LICENSE" "$out/licenses/gcrecomp-LICENSE.txt"
if [ "$with_zig" = 1 ]; then
  # SDL3 (zlib licence) is linked statically into the game: ship its library and its licence text.
  cp "$build/_deps/sdl3-build/libSDL3.a" "$out/lib/"
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
elif [ "$linux_zig" = 1 ]; then
  # The static SDL3 opens libX11, libwayland-client, libasound, libpulse, libGL ... itself with dlopen: nothing but the
  # C library is linked (glibc 2.31 keeps dlopen in libdl and threads in libpthread).
  sys_libs="-lm -ldl -lpthread"
else
  sys_libs=$(pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")
fi

"$PYTHON" - "$out" "$(rev "$gcrecomp")$(dirty "$gcrecomp")" "$(rev "$here")$(dirty "$here")" "$sys_libs" "$windows" "$linux_zig" "$target" <<'PY'
import json, sys
out, gcrecomp_rev, tos_rev, sys_libs, windows, linux_zig, target = sys.argv[1:8]
windows, linux_zig = windows == "1", linux_zig == "1"
libs = ["gcrecomp_gp", "torecomp_overlay", "torecomp_overlay_core", "torecomp_imgui", "gcrecomp_runtime", "gcrecomp_gx", "gcrecomp_audio", "gcrecomp_input"]
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
        # Zig's clang writes debug information unless told not to: -g0 makes the objects of the real game 6 times
        # smaller (15.7 MB instead of 94 MB for the biggest file) and the compiler 45% leaner (1.4 GB instead of 2.6 GB).
        "flags": ["-O1", "-w"] + (["-g0"] if windows or linux_zig else []),
        "libs": ["lib/lib%s.a" % l for l in libs] + (["lib/libSDL3.a"] if windows or linux_zig else []),
        # Linked after the libraries (not used when compiling): system libraries, then linker options.
        "system_libs": sys_libs.split() + ([] if windows or linux_zig else ["-pthread"]),
        # {root}: the folder holding toolkit/ and toolchain/ (tos-prepare's unpacked payload). A release names only its
        # bundled Zig and refuses $CXX: the libraries above are built with Zig's libc++.
        "compilers": ["{root}/toolchain/zig/zig c++ -target " + target] if windows or linux_zig else ["clang++", "g++", "c++"],
        "compiler_env": not (windows or linux_zig),
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
