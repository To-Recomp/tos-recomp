#!/usr/bin/env bash
# Does a game executable built by the Linux release run on a machine with nothing but a C library?
# Checks the dynamic section and the symbol versions of an ELF executable (needs readelf from binutils; this is a
# check for the machine that builds the release, the player's machine does not need it):
#   - the only shared libraries it asks for are the C library's own (libc, libm, libdl, libpthread, librt, libutil and
#     the dynamic loader): no X11, Wayland, ALSA, PulseAudio, OpenGL, SDL3, libstdc++ or libgcc_s, which SDL3
#     loads at run time with dlopen if the player's system has them;
#   - it needs no glibc symbol newer than the version the release targets (default 2.31).
#
#   tests/check-linux-game.sh EXECUTABLE [--glibc 2.31]
set -euo pipefail

exe=; glibc=2.31
while [ $# -gt 0 ]; do
  case "$1" in
    --glibc) glibc="$2"; shift 2 ;;
    -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) exe="$1"; shift ;;
  esac
done
[ -f "$exe" ] || { echo "usage: $0 EXECUTABLE [--glibc VERSION]" >&2; exit 2; }
command -v readelf >/dev/null || { echo "readelf (binutils) is needed" >&2; exit 2; }

status=0
needed="$(readelf -d "$exe" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')"
echo "needed libraries:"; echo "$needed" | sed 's/^/  /'
bad="$(echo "$needed" | grep -Ev '^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|libutil\.so\.1|ld-linux-x86-64\.so\.2)$' || true)"
if [ -n "$bad" ]; then echo "FAILED: libraries beyond the C library are linked in:"; echo "$bad" | sed 's/^/  /'; status=1; fi

newest="$(readelf --dyn-syms --wide "$exe" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -n1)"
echo "newest glibc symbol version: ${newest:-none} (limit $glibc)"
if [ -n "$newest" ] && [ "$(printf '%s\n%s\n' "$newest" "$glibc" | sort -V | tail -n1)" != "$glibc" ]; then
  echo "FAILED: needs glibc $newest, newer than $glibc"; status=1
fi
if readelf --dyn-syms --wide "$exe" | grep -q 'GLIBCXX\|CXXABI'; then echo "FAILED: needs libstdc++ symbols"; status=1; fi
[ "$status" = 0 ] && echo "ok: $exe needs only the C library"
exit "$status"
