#!/usr/bin/env bash
# Rebuild the recompiler, regenerate the C++ from main.dol, and rebuild the boot harness.
#
# This is the DEVELOPER shortcut (repo-local extracted/, generated/ and build/, incremental with ninja).
# Players use `tos-prepare` (docs/PREPARE.md): discs in, instance folder out, same recompiler arguments.
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
# The game loads relocatable modules (FR_m_Top2Btl.rel: the battle code; FR_m_Top2field.rel) from the disc
# root; they are recompiled in the same run as the DOL (the .so files are data, not modules).
rel_args=()
for rel in "$here"/extracted/disc1/files/*.rel; do
  [ -f "$rel" ] && rel_args+=(--rel "$rel")
done
"$gcrecomp/build/gcrecomp_recompiler" "$dol" \
  --csv "$here/symbols/GQSF.csv" \
  --extra-funcs "$here/symbols/GQSF.extra_funcs.txt" \
  "${rel_args[@]}" \
  --output "$out" --project tos

cmake -S "$here" -B "$here/build" -G Ninja -DGCRECOMP_DIR="$gcrecomp"
cmake --build "$here/build"
