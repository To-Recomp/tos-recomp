#!/usr/bin/env bash
# Build the release archive the To-Recomp launcher installs (docs/RELEASING.md): tos-prepare, the toolkit,
# the launcher manifest and the start script, as tos-recomp-<version>-linux-x86_64.tar.gz plus SHA256SUMS.
# Nothing in it is derived from the game.
#
#   scripts/package-release.sh --version X.Y.Z[-rc.N] --out DIR [--gcrecomp DIR] [--common DIR] [--build DIR]
#                              [--jobs N] [--fetch-sdl3] [--portable]
#
#   --version V     the release version without the leading v (also printed by tos-prepare --version)
#   --out DIR       where the archive and SHA256SUMS are written (created)
#   --gcrecomp DIR  gcrecomp checkout (default: ../gcrecomp)      --common DIR  common checkout (default: ../common)
#   --build DIR     scratch folder for the builds (default: $TMPDIR/tos-release-build)
#   --jobs N        build parallelism (default 4)
#   --fetch-sdl3    passed to make-toolkit.sh (no SDL3 development files on this machine)
#   --portable      link tos-prepare with the C++ runtime and the disc codecs (zlib, bzip2, lzma, zstd) inside, so it
#                   runs on any distribution whatever the soname of those libraries. Needs their static libraries
#                   (Ubuntu: libstdc++ and the -dev packages; Fedora: libstdc++-static, bzip2-static, xz-static, libzstd-static)
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
version=; out=; gcrecomp="$here/../gcrecomp"; common="$here/../common"; build="${TMPDIR:-/var/tmp}/tos-release-build"; jobs=4; fetch=(); portable=0
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --gcrecomp) gcrecomp="$2"; shift 2 ;;
    --common) common="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --fetch-sdl3) fetch=(--fetch-sdl3); shift ;;
    --portable) portable=1; shift ;;
    -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$version" ] && [ -n "$out" ] || { echo "--version and --out are required" >&2; exit 2; }
case "$version" in v*) echo "--version takes X.Y.Z, without the v" >&2; exit 2 ;; esac
gcrecomp="$(cd "$gcrecomp" && pwd)"; common="$(cd "$common" && pwd)"
mkdir -p "$out" "$build"; out="$(cd "$out" && pwd)"

# tos-prepare
portable_args=()
[ "$portable" = 1 ] && portable_args=(-DCMAKE_FIND_LIBRARY_SUFFIXES=.a -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc")
cmake -S "$here/prepare" -B "$build/prepare" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DTOS_COMMON_DIR="$common" -DTOS_PREPARE_VERSION="$version" "${portable_args[@]}" >/dev/null
cmake --build "$build/prepare" -j "$jobs" --target tos-prepare

name="tos-recomp-$version-linux-x86_64"
stage="$build/stage/$name"
rm -rf "$build/stage"; mkdir -p "$stage"
"$here/scripts/make-toolkit.sh" --gcrecomp "$gcrecomp" --out "$stage/toolkit" --build "$build/gcrecomp" --jobs "$jobs" "${fetch[@]}"
cp "$build/prepare/tos-prepare" "$stage/tos-prepare"
cp "$here/packaging/tos-start" "$here/packaging/recomplauncher.json" "$here/LICENSE" "$stage/"
chmod 755 "$stage/tos-prepare" "$stage/tos-start"
strip "$stage/tos-prepare" "$stage/toolkit/bin/gcrecomp_recompiler" 2>/dev/null || true

# Same name inside the archive as the launcher expects: one folder on top is unwrapped by the launcher.
tar -C "$build/stage" --owner=0 --group=0 --numeric-owner --sort=name -czf "$out/$name.tar.gz" "$name"
( cd "$out" && sha256sum "$name.tar.gz" > SHA256SUMS )
echo "wrote $out/$name.tar.gz ($(du -h "$out/$name.tar.gz" | cut -f1))"
