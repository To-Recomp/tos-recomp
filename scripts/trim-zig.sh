#!/usr/bin/env bash
# Remove from an unpacked Zig what a release never uses, to make the download smaller (docs/RELEASING.md).
#
#   scripts/trim-zig.sh --target linux|windows DIR
#
#   --target T   the one operating system the release compiles for (linux: x86_64-linux-gnu, windows: x86_64-windows-gnu).
#                Zig carries the C library sources and headers of every operating system it can target; the release
#                needs the one it compiles for. What is removed, below DIR/lib:
#                  - libc/ of the other operating systems (musl, wasi, the BSDs, macOS, MinGW-w64 or glibc) and their
#                    include folders, and for a Linux release the Windows headers (86 MB, the biggest part);
#                  - libtsan (thread sanitizer runtime), lldb, docs, build-web, init and doc/: nothing the C++ compiler runs.
#                Nothing is deleted that this target's own compile reads. That is checked, not assumed: the release
#                smoke test (tests/release-smoke.sh) compiles, links and runs a game with the trimmed compiler.
#                The licence files of what is removed are not needed: the licence files of what stays are untouched.
#   DIR          the unpacked Zig (the folder holding the zig executable and lib/); modified in place, a copy is the
#                caller's job. Prints the bytes saved.
set -euo pipefail

target=; dir=
while [ $# -gt 0 ]; do
  case "$1" in
    --target) target="$2"; shift 2 ;;
    -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) dir="$1"; shift ;;
  esac
done
case "$target" in linux|windows) ;; *) echo "--target is linux or windows" >&2; exit 2 ;; esac
[ -d "${dir:-}/lib/libc" ] || { echo "$dir is not an unpacked Zig (no lib/libc)" >&2; exit 2; }
dir="$(cd "$dir" && pwd)"

size() { du -sk "$dir" | cut -f1; }
before="$(size)"

drop() { local p; for p in "$@"; do [ -e "$p" ] && rm -rf -- "$p"; done; return 0; }
libc="$dir/lib/libc"
shopt -s nullglob

# Never used by the C++ compiler.
drop "$dir/doc" "$dir/lib/docs" "$dir/lib/build-web" "$dir/lib/init" "$dir/lib/lldb" "$dir/lib/libtsan"

# The C libraries of the other operating systems.
drop "$libc"/{musl,wasi,freebsd,netbsd,openbsd,darwin}
drop "$libc"/include/*-{freebsd,netbsd,openbsd}-* "$libc"/include/any-darwin-any "$libc"/include/*-wasi-* \
     "$libc"/include/generic-{freebsd,netbsd,openbsd,musl} "$libc"/include/*-musl* "$libc"/include/*-macos-*
case "$target" in
  linux)   drop "$libc"/mingw "$libc"/include/any-windows-any "$libc"/include/*-windows-* ;;
  windows) drop "$libc"/glibc "$libc"/include/generic-glibc "$libc"/include/*-linux-* ;;
esac

after="$(size)"
echo "trim-zig: $target: $(( (before - after) / 1024 )) MiB removed ($(( before / 1024 )) -> $(( after / 1024 )) MiB)"
