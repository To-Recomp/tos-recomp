#!/usr/bin/env bash
# Assemble the toolkit that tos-prepare needs (docs/PREPARE.md): the recompiler, the runtime libraries,
# the headers the generated code includes, the boot harness source, dumps.toml and the symbol files.
# Everything in it is ours; nothing is derived from the game, so it may be shipped.
#
#   scripts/make-toolkit.sh --gcrecomp DIR --out DIR [--build DIR] [--jobs N] [--no-build] [--fetch-sdl3]
#
#   --gcrecomp DIR  a gcrecomp checkout (default: ../gcrecomp next to this repository)
#   --out DIR       the toolkit folder to create (replaced if it exists)
#   --build DIR     gcrecomp build folder (default: $TMPDIR/gcrecomp-toolkit-build); it is configured and
#                   built unless --no-build is given (use that to reuse an existing build)
#   --jobs N        build parallelism (default 4)
#   --fetch-sdl3    build SDL3 from source for the headers the libraries are compiled against (a machine without
#                   SDL3 development files, such as CI). The toolkit still links the player's own SDL3 (-lSDL3).
#
# The toolkit's libraries are compiled by the compiler CMake picks here. A player's compiler must use
# the same C++ standard library (libstdc++ with gcc or clang on Linux), see docs/PREPARE.md.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
gcrecomp="$here/../gcrecomp"; out=; build="${TMPDIR:-/var/tmp}/gcrecomp-toolkit-build"; jobs=4; do_build=1; fetch_sdl3=OFF
while [ $# -gt 0 ]; do
  case "$1" in
    --gcrecomp) gcrecomp="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --fetch-sdl3) fetch_sdl3=ON; shift ;;
    --no-build) do_build=0; shift ;;
    -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$out" ] || { echo "--out is required" >&2; exit 2; }
[ -f "$gcrecomp/CMakeLists.txt" ] || { echo "gcrecomp not found at $gcrecomp (--gcrecomp)" >&2; exit 2; }
gcrecomp="$(cd "$gcrecomp" && pwd)"

if [ "$do_build" = 1 ]; then
  cmake -S "$gcrecomp" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DGCRECOMP_BUILD_EXAMPLE=OFF -DGCRECOMP_BUILD_TESTS=OFF -DBUILD_TESTING=OFF -DGCRECOMP_FETCH_SDL3="$fetch_sdl3" >/dev/null
  cmake --build "$build" -j "$jobs" --target gcrecomp_recompiler gcrecomp_gp gcrecomp_runtime gcrecomp_gx gcrecomp_audio gcrecomp_input
fi
libs=(gcrecomp_gp gcrecomp_runtime gcrecomp_gx gcrecomp_audio gcrecomp_input)
for l in "${libs[@]}"; do [ -f "$build/lib$l.a" ] || { echo "missing $build/lib$l.a (build first)" >&2; exit 1; }; done
[ -x "$build/gcrecomp_recompiler" ] || { echo "missing $build/gcrecomp_recompiler" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out/bin" "$out/lib" "$out/include" "$out/src" "$out/symbols"
cp "$build/gcrecomp_recompiler" "$out/bin/"
for l in "${libs[@]}"; do cp "$build/lib$l.a" "$out/lib/"; done
cp -r "$gcrecomp/include/gcrecomp" "$out/include/"
cp "$here/src/main.cpp" "$out/src/main.cpp"
cp "$here/dumps.toml" "$out/dumps.toml"
cp "$here"/symbols/GQSF.csv "$here"/symbols/GQSF.extra_funcs.txt "$out/symbols/"
cp "$here/LICENSE" "$out/LICENSE"

rev() { git -C "$1" rev-parse --short=12 HEAD 2>/dev/null || echo unknown; }
dirty() { [ -z "$(git -C "$1" status --porcelain 2>/dev/null)" ] || echo "+dirty"; }
sys_libs=$(pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")

python3 - "$out" "$(rev "$gcrecomp")$(dirty "$gcrecomp")" "$(rev "$here")$(dirty "$here")" "$sys_libs" <<'PY'
import json, sys
out, gcrecomp_rev, tos_rev, sys_libs = sys.argv[1:5]
libs = ["gcrecomp_gp", "gcrecomp_runtime", "gcrecomp_gx", "gcrecomp_audio", "gcrecomp_input"]
tk = {
    "format": 1,
    "game_id": "GQSF",
    "project": "tos",
    "dumps": "dumps.toml",
    "recompiler": "bin/gcrecomp_recompiler",
    "symbols": {"dir": "symbols", "csv": "GQSF.csv", "extra_funcs": "GQSF.extra_funcs.txt"},
    "build": {
        "include": ["include"],
        "sources": ["src/main.cpp"],
        "std": "gnu++20",
        "flags": ["-O1", "-w"],
        "libs": ["lib/lib%s.a" % l for l in libs],
        "system_libs": sys_libs.split() + ["-pthread"],
        "compilers": ["clang++", "g++", "c++"],
        "executable": "tos_boot",
    },
    # How to start the game, relative to the instance folder. The harness takes <main.dol> <files> <seconds>;
    # 0 is not "forever" in the harness yet, see docs/PREPARE.md (open points).
    "run": {
        "args": ["game/sys/main.dol", "game/files", "86400"],
        "env": {"GCRECOMP_CARD_IMAGE": "saves/card-a.raw"},
        "unlimited_stack": True,
    },
    "disk": {"generated_per_input_byte": 32, "objects_per_generated_byte": 1.6, "executable_bytes": 100000000},
    "versions": {"gcrecomp": gcrecomp_rev, "tos_recomp": tos_rev},
}
with open(out + "/toolkit.json", "w") as f:
    json.dump(tk, f, indent=2)
    f.write("\n")
PY
echo "toolkit written to $out ($(du -sh "$out" | cut -f1))"
