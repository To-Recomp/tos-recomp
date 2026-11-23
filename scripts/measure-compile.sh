#!/usr/bin/env bash
# Measure how long the generated C++ takes to compile, and how much memory one compiler process needs.
#
#   scripts/measure-compile.sh --gen DIR --include DIR [--cxx c++] [--flags "-O1"] [--jobs 4]
#                              [--ccache] [--unity N] [--out DIR] [--link LIBDIR]
#
#   --gen DIR       output of gcrecomp_recompiler (recomp_*.cpp, rel_*.cpp, headers)
#   --include DIR   gcrecomp's include/ directory (the generated code includes gcrecomp/*.h)
#   --cxx CMD       compiler command (default c++); may be "zig c++" (split on spaces)
#   --flags STR     extra flags, default "-O1" (-w -std=gnu++20 are always added, as in CMakeLists.txt)
#   --jobs N        parallel compilations (default 4, as the launcher would use)
#   --ccache        run the compiler through ccache (cache in $CCACHE_DIR, default OUT/ccache: pass the same CCACHE_DIR twice for a warm run)
#   --unity N       concatenate N generated .cpp files per translation unit (#include of N files)
#   --out DIR       where objects and the per-file table go (default $TMPDIR/measure-compile)
#
# Prints the wall time for the whole set, the sum of CPU seconds, and the largest resident set of ONE
# compiler process (peak memory is about --jobs times that). Run it under `nice -n 19 ionice -c3` on a
# machine somebody is using. The per-file table (seconds, KB, file) is OUT/files.tsv.
# Nothing here reads the game: the input is the generated code, which stays on the machine.
set -euo pipefail

gen=; inc=; cxx=c++; flags="-O1"; jobs=4; use_ccache=0; unity=0
out="${TMPDIR:-/var/tmp}/measure-compile"
while [ $# -gt 0 ]; do
  case "$1" in
    --gen) gen="$2"; shift 2 ;;
    --include) inc="$2"; shift 2 ;;
    --cxx) cxx="$2"; shift 2 ;;
    --flags) flags="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --ccache) use_ccache=1; shift ;;
    --unity) unity="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -d "$gen" ] && [ -d "$inc" ] || { echo "--gen and --include are required" >&2; exit 2; }
gen="$(cd "$gen" && pwd)"; inc="$(cd "$inc" && pwd)"
rm -rf "$out"; mkdir -p "$out/obj" "$out/src"

sources=("$gen"/*.cpp)
if [ "$unity" -gt 0 ]; then
  n=0; i=0; : >/dev/null
  for f in "${sources[@]}"; do
    if [ $((n % unity)) -eq 0 ]; then i=$((i + 1)); u="$out/src/unity_$i.cpp"; : >"$u"; fi
    printf '#include "%s"\n' "$f" >>"$u"; n=$((n + 1))
  done
  sources=("$out/src"/unity_*.cpp)
fi

export CCACHE_DIR="${CCACHE_DIR:-$out/ccache}"
cmd=($cxx)
[ "$use_ccache" = 1 ] && cmd=(ccache "${cmd[@]}")
export MC_CMD="${cmd[*]}" MC_FLAGS="$flags -w -std=gnu++20 -I$inc -I$gen" MC_OUT="$out"

compile_one() {
  local f="$1" o="$MC_OUT/obj/$(basename "$1" .cpp).o"
  # shellcheck disable=SC2086
  /usr/bin/time -f "%e\t%M\t$(basename "$f")" -a -o "$MC_OUT/files.tsv" $MC_CMD $MC_FLAGS -c "$f" -o "$o"
}
export -f compile_one
: >"$out/files.tsv"
start=$(date +%s.%N)
printf '%s\n' "${sources[@]}" | xargs -P "$jobs" -I{} bash -c 'compile_one {}'
end=$(date +%s.%N)

awk -F'\t' -v s="$start" -v e="$end" -v j="$jobs" -v c="$MC_CMD" -v fl="$flags" -v u="$unity" '
  {cpu+=$1; if($2>m){m=$2;mf=$3}; n++}
  END{printf "compiler=%s flags=\"%s\" jobs=%d unity=%d files=%d wall=%.1fs cpu_sum=%.1fs max_rss_one=%.0fMB (%s) est_peak=%.1fGB\n", c, fl, j, u, n, e-s, cpu, m/1024, mf, m*j/1048576}' "$out/files.tsv"
du -sk "$out/obj" | awk '{printf "objects=%.0fMB\n", $1/1024}'
