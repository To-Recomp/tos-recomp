#!/usr/bin/env bash
# Rebuild the recompiler, regenerate the C++ from main.dol, and rebuild the boot harness.
#
# Layout assumed (the workspace used by the project): this repository, gcrecomp and common are
# sibling directories. main.dol must already be extracted to extracted/disc1/sys/main.dol
# (see docs/DEVELOPING.md).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
gcrecomp="${GCRECOMP_DIR:-$here/../gcrecomp}"
dol="$here/extracted/disc1/sys/main.dol"
out="$here/generated/gcrecomp"

[ -f "$dol" ] || { echo "missing $dol: extract disc 1 first (docs/DEVELOPING.md)" >&2; exit 1; }

cmake -S "$gcrecomp" -B "$gcrecomp/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGCRECOMP_BUILD_RUNTIME=OFF -DGCRECOMP_BUILD_EXAMPLE=OFF >/dev/null
cmake --build "$gcrecomp/build" --target gcrecomp_recompiler

rm -rf "$out"
"$gcrecomp/build/gcrecomp_recompiler" "$dol" \
  --csv "$here/symbols/GQSF.csv" \
  --extra-funcs "$here/symbols/GQSF.extra_funcs.txt" \
  --output "$out" --project tos

cmake -S "$here" -B "$here/build" -G Ninja -DGCRECOMP_DIR="$gcrecomp"
cmake --build "$here/build"
