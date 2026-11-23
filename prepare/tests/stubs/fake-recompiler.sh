#!/bin/sh
# Test stand-in for gcrecomp_recompiler: writes three "generated" files. recomp_0001.cpp depends on the
# symbol file, the others only on the DOL, so changing the symbols changes one file.
# usage as the real one: <dol> --csv F --extra-funcs F [--rel F]... --output DIR --project NAME
# FAKE_RECOMPILER_FAIL=1 makes it fail.
[ -n "$FAKE_RECOMPILER_FAIL" ] && { echo "fake recompiler: failing on request" >&2; exit 3; }
dol=$1; shift
while [ $# -gt 0 ]; do
  case "$1" in
    --csv) csv=$2; shift 2 ;;
    --extra-funcs) extra=$2; shift 2 ;;
    --rel) rels="$rels $2"; shift 2 ;;
    --output) out=$2; shift 2 ;;
    --project) project=$2; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$out"
echo "recompiling $dol" >&2
printf '// %s\n#include "recomp_common.h"\n' "$(cksum < "$dol")" > "$out/recomp_0000.cpp"
printf '// %s %s\n#include "recomp_common.h"\n' "$(cat "$csv" "$extra")" "$rels" > "$out/recomp_0001.cpp"
printf '// %s\n#include "recomp_common.h"\n' "$project" > "$out/recomp_0002.cpp"
printf '// common header\n' > "$out/recomp_common.h"
