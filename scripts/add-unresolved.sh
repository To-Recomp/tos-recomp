#!/usr/bin/env bash
# Collect the "[FuncTable] Unresolved call to 0xADDR" lines of a run log and append the addresses to
# symbols/GQSF.extra_funcs.txt (function entries reached only through pointers built with lis/addi, which the
# static analysis cannot discover). Run scripts/regen.sh afterwards.
#
#   scripts/add-unresolved.sh <run.log> [--extra-funcs FILE] [--dry-run]
#
# Prints the addresses it adds; addresses already listed in the file are skipped. Review them before
# committing: an unresolved call can also be a wild jump caused by an emulation bug, not a real function.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
extra="$here/symbols/GQSF.extra_funcs.txt"; dry=0
[ $# -ge 1 ] || { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
log="$1"; shift
while [ $# -gt 0 ]; do
  case "$1" in
    --extra-funcs) extra="$2"; shift 2 ;;
    --dry-run) dry=1; shift ;;
    -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -f "$log" ] || { echo "no such log: $log" >&2; exit 1; }
touch "$extra"
grep -a -o 'Unresolved call to 0x[0-9A-Fa-f]\{8\}' "$log" | sed 's/.*0x//' | tr 'a-f' 'A-F' | sort -u |
while read -r a; do
  if grep -q -i "^$a" "$extra"; then continue; fi
  echo "$a"
  [ "$dry" = 1 ] || echo "$a" >> "$extra"
done
