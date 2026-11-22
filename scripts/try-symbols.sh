#!/usr/bin/env bash
# Try a symbols CSV without touching the shared checkout: regenerate the recompiled code from it into
# a work directory, build tos_boot there, run the scripted new-game input and print a summary.
#
#   scripts/try-symbols.sh --csv symbols/GQSF.csv --work /var/tmp/names-a \
#       --dol extracted/disc1/sys/main.dol --files extracted/disc1/files \
#       [--gcrecomp DIR] [--script FILE] [--seconds 190] [--skip-run]
#
# Writes WORK/gen (generated C++), WORK/build, WORK/run (screenshots *.ppm, run.log, summary of the
# [script] and [hle] lines in WORK/summary.txt). Delete WORK when done (it holds game-derived code and
# screenshots, never commit it). Needs gcrecomp (default $GCRECOMP_DIR or ../gcrecomp) and the
# extra funcs list symbols/GQSF.extra_funcs.txt of this repository.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
gcrecomp="${GCRECOMP_DIR:-$here/../gcrecomp}"
csv="$here/symbols/GQSF.csv"; work=""; dol=""; files=""; script=""; seconds=190; run=1
while [ $# -gt 0 ]; do
  case "$1" in
    --csv) csv="$2"; shift 2 ;; --work) work="$2"; shift 2 ;; --dol) dol="$2"; shift 2 ;;
    --files) files="$2"; shift 2 ;; --gcrecomp) gcrecomp="$2"; shift 2 ;; --script) script="$2"; shift 2 ;;
    --seconds) seconds="$2"; shift 2 ;; --skip-run) run=0; shift ;;
    -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$work" ] && [ -n "$dol" ] && [ -n "$files" ] || { echo "need --work --dol --files" >&2; exit 2; }
script="${script:-$gcrecomp/tools/input-scripts/tos-new-game.txt}"
export TMPDIR="${TMPDIR:-/var/tmp}"
mkdir -p "$work"; work="$(cd "$work" && pwd)"
rc="$work/recompiler"
cmake -S "$gcrecomp" -B "$rc" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGCRECOMP_BUILD_RUNTIME=OFF -DGCRECOMP_BUILD_EXAMPLE=OFF >/dev/null
nice -n 19 cmake --build "$rc" --target gcrecomp_recompiler -j4 >/dev/null
rm -rf "$work/gen"
"$rc/gcrecomp_recompiler" "$dol" --csv "$csv" --extra-funcs "$here/symbols/GQSF.extra_funcs.txt" \
  --output "$work/gen" --project tos >"$work/recompiler.log" 2>&1
cmake -S "$here" -B "$work/build" -G Ninja -DGCRECOMP_DIR="$gcrecomp" -DGENERATED_DIR="$work/gen" >/dev/null
nice -n 19 cmake --build "$work/build" --target tos_boot -j4 2>&1 | tail -3
[ "$run" = 1 ] || exit 0
nice -n 19 ionice -c3 "$gcrecomp/tools/run-scripted.sh" --bin "$work/build/tos_boot" --dol "$dol" --files "$files" \
  --script "$script" --seconds "$seconds" --out "$work/run" | tee "$work/summary.txt"
grep -E '^\[hle\]|^\[gp\] [0-9]+ frames' "$work/run/run.log" | tail -60 >> "$work/summary.txt" || true
echo "summary: $work/summary.txt"
