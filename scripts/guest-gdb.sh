#!/usr/bin/env bash
# Inspect the running recompiled game with gdb through guest-function breakpoints.
#
#   scripts/guest-gdb.sh [--seconds N] [--break func_8016EF28 ...] [--bt DEPTH]
#                        [--args "r3 r4 r5"] [--mem "0x80359940:8"] [--ignore N] [-- extra tos_boot args]
#
# The binary has no debug info, so this relies on the generated calling convention: a guest function
# `func_XXXXXXXX(PPCContext* ctx, Memory* mem)` receives ctx in rdi and mem in rsi; guest register rN is a
# 32-bit word at ctx+4*N, and guest RAM is the host pointer stored at mem+8 (guest 0x80xxxxxx maps to
# ram + (addr & 0x1FFFFFF)). Words in RAM are big-endian: gdb's x/wx shows them byte-swapped.
#
# Each breakpoint prints the requested guest registers and memory words, then a host backtrace
# (guest call chain), and continues. Needs a built harness (scripts/regen.sh) and `gdb`.
#
# Environment: TOS_BIN (default build/tos_boot), TOS_DOL, TOS_FILES, TOS_SCRIPT, GCRECOMP_AUDIO.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
bin="${TOS_BIN:-$here/build/tos_boot}"
dol="${TOS_DOL:-$here/extracted/disc1/sys/main.dol}"
files="${TOS_FILES:-$here/extracted/disc1/files}"
seconds=20; depth=8; regs=""; mems=(); breaks=(); ignore=0; extra=()

usage() { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    --seconds) seconds="$2"; shift 2 ;;
    --break) breaks+=("$2"); shift 2 ;;
    --bt) depth="$2"; shift 2 ;;
    --args) regs="$2"; shift 2 ;;
    --mem) mems+=("$2"); shift 2 ;;
    --ignore) ignore="$2"; shift 2 ;;
    -h|--help) usage 0 ;;
    --) shift; extra=("$@"); break ;;
    *) echo "unknown option $1" >&2; usage 2 ;;
  esac
done
[ ${#breaks[@]} -gt 0 ] || { echo "need at least one --break func_XXXXXXXX" >&2; usage 2; }
[ -x "$bin" ] || { echo "missing $bin: run scripts/regen.sh first" >&2; exit 1; }

gdbfile="$(mktemp)"; trap 'rm -f "$gdbfile"' EXIT
{
  echo "set pagination off"; echo "set confirm off"
  i=0
  for b in "${breaks[@]}"; do
    i=$((i + 1))
    echo "break *$b"
    [ "$ignore" -gt 0 ] && echo "ignore $i $ignore"
    echo "commands $i"; echo "silent"
    echo "printf \"--- $b\\n\""
    for r in $regs; do
      n="${r#r}"
      echo "printf \"  r$n=0x%08x\\n\", *(unsigned int*)(\$rdi+4*$n)"
    done
    for m in "${mems[@]}"; do
      addr="${m%%:*}"; words="${m##*:}"; [ "$addr" = "$m" ] && words=1
      echo "printf \"  mem $addr:\\n\""
      echo "x/${words}wx *(unsigned char**)(\$rsi+8) + ($addr & 0x1FFFFFF)"
    done
    echo "bt $depth"; echo "continue"; echo "end"
  done
  echo "run \"$dol\" \"$files\" $seconds ${extra[*]:-}"
  echo "quit"
} > "$gdbfile"

ulimit -s unlimited
# Output of the game itself goes to stdout/stderr of tos_boot; keep only gdb's breakpoint reports.
gdb -nx -q -batch -x "$gdbfile" "$bin" 2>&1 | grep -a -E '^(---|  r[0-9]|  mem|#|0x[0-9a-f]+:)' | sed 's/(gcrecomp::PPCContext\*, gcrecomp::Memory\*)//'
