#!/usr/bin/env bash
# Build the release archive the To-Recomp launcher installs (docs/RELEASING.md): tos-prepare, the toolkit,
# the launcher manifest and the start program, as tos-recomp-<version>-linux-x86_64.tar.gz or
# tos-recomp-<version>-windows-x86_64.zip, plus SHA256SUMS. Nothing in it is derived from the game.
#
#   scripts/package-release.sh --version X.Y.Z[-rc.N] --out DIR [--gcrecomp DIR] [--common DIR] [--build DIR]
#                              [--jobs N] [--fetch-sdl3] [--portable]
#   scripts/package-release.sh --platform windows --version X.Y.Z[-rc.N] --out DIR --zig-dist DIR --prepare-exe FILE
#                              [--zig FILE] [--licenses DIR] [--gcrecomp DIR] [--build DIR] [--jobs N]
#
#   --version V     the release version without the leading v (also printed by tos-prepare --version)
#   --out DIR       where the archive and SHA256SUMS are written (created)
#   --gcrecomp DIR  gcrecomp checkout (default: ../gcrecomp)      --common DIR  common checkout (default: ../common)
#   --build DIR     scratch folder for the builds (default: $TMPDIR/tos-release-build)
#   --jobs N        build parallelism (default 4)
#   --fetch-sdl3    passed to make-toolkit.sh (no SDL3 development files on this machine)
#   --platform P    linux (default) or windows. Windows needs Zig (scripts/fetch-zig.sh) and runs on Windows (Git Bash)
#                   or on any host that can run Zig: the runtime libraries and the recompiler are built with it
#   --zig-dist DIR  windows: the unpacked Zig for x86_64-windows that the archive bundles in toolchain/zig
#   --zig FILE      windows: the zig executable that builds the libraries (default: the one in --zig-dist, which
#                   needs a Windows host; a Linux host passes its own Linux zig)
#   --prepare-exe F windows: the tos-prepare.exe to ship (built with MSVC in release.yml: statically linked, with the
#                   disc codecs inside: cmake -S prepare ... -DTOS_PREPARE_STATIC_CODECS=ON, see docs/RELEASING.md)
#   --licenses DIR  windows: licence files of what is linked into tos-prepare.exe, copied to licenses/
#   --portable      link tos-prepare with the C++ runtime and the disc codecs (zlib, bzip2, lzma, zstd) inside, so it
#                   runs on any distribution whatever the soname of those libraries. Needs their static libraries
#                   (Ubuntu: libstdc++ and the -dev packages; Fedora: libstdc++-static, bzip2-static, xz-static, libzstd-static)
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
version=; out=; gcrecomp="$here/../gcrecomp"; common="$here/../common"; build="${TMPDIR:-/var/tmp}/tos-release-build"; jobs=4; fetch=(); portable=0
platform=linux; zig_dist=; zig=; prepare_exe=; licenses=; PYTHON="${PYTHON:-python3}"
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
    --platform) platform="$2"; shift 2 ;;
    --zig-dist) zig_dist="$2"; shift 2 ;;
    --zig) zig="$2"; shift 2 ;;
    --prepare-exe) prepare_exe="$2"; shift 2 ;;
    --licenses) licenses="$2"; shift 2 ;;
    -h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$version" ] && [ -n "$out" ] || { echo "--version and --out are required" >&2; exit 2; }
case "$version" in v*) echo "--version takes X.Y.Z, without the v" >&2; exit 2 ;; esac
case "$platform" in linux|windows) ;; *) echo "--platform is linux or windows" >&2; exit 2 ;; esac
gcrecomp="$(cd "$gcrecomp" && pwd)"
mkdir -p "$out" "$build"; out="$(cd "$out" && pwd)"
# Windows tools (CMake, Zig) want C:/... paths; under Git Bash `pwd` gives /c/...
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

name="tos-recomp-$version-$platform-x86_64"
stage="$build/stage/$name"
rm -rf "$build/stage"; mkdir -p "$stage"

if [ "$platform" = linux ]; then
  common="$(cd "$common" && pwd)"
  # tos-prepare
  portable_args=()
  [ "$portable" = 1 ] && portable_args=(-DTOS_PREPARE_STATIC_CODECS=ON -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc")
  cmake -S "$here/prepare" -B "$build/prepare" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DTOS_COMMON_DIR="$common" -DTOS_PREPARE_VERSION="$version" "${portable_args[@]}" >/dev/null
  cmake --build "$build/prepare" -j "$jobs" --target tos-prepare
  if [ "$portable" = 1 ] && ldd "$build/prepare/tos-prepare" | grep -E 'libz|libbz2|liblzma|libzstd|libstdc'; then
    echo "tos-prepare still depends on a codec or the C++ runtime (--portable)" >&2; exit 1
  fi

  "$here/scripts/make-toolkit.sh" --gcrecomp "$gcrecomp" --out "$stage/toolkit" --build "$build/gcrecomp" --jobs "$jobs" "${fetch[@]}"
  cp "$build/prepare/tos-prepare" "$stage/tos-prepare"
  cp "$here/packaging/tos-start" "$here/packaging/recomplauncher.json" "$here/LICENSE" "$stage/"
  chmod 755 "$stage/tos-prepare" "$stage/tos-start"
  strip "$stage/tos-prepare" "$stage/toolkit/bin/gcrecomp_recompiler" 2>/dev/null || true

  # Same name inside the archive as the launcher expects: one folder on top is unwrapped by the launcher.
  archive="$name.tar.gz"
  tar -C "$build/stage" --owner=0 --group=0 --numeric-owner --sort=name -czf "$out/$archive" "$name"
else
  [ -n "$zig_dist" ] && [ -n "$prepare_exe" ] || { echo "--platform windows needs --zig-dist and --prepare-exe" >&2; exit 2; }
  zig_dist="$(cd "$zig_dist" && pwd)"
  [ -f "$prepare_exe" ] || { echo "no $prepare_exe" >&2; exit 2; }
  [ -f "$zig_dist/zig.exe" ] && [ -f "$zig_dist/LICENSE" ] || { echo "$zig_dist is not an unpacked Zig for Windows (zig.exe, LICENSE)" >&2; exit 2; }
  [ -n "$zig" ] || zig="$zig_dist/zig.exe"
  [ -f "$zig" ] || { echo "no zig executable at $zig" >&2; exit 2; }

  "$here/scripts/make-toolkit.sh" --windows --zig "$zig" --gcrecomp "$gcrecomp" --out "$stage/toolkit" --build "$build/gcrecomp" --jobs "$jobs"
  cp "$prepare_exe" "$stage/tos-prepare.exe"
  # tos-start.exe: the launcher starts it; it starts the game (packaging/windows/tos-start.cpp).
  "$zig" c++ -target x86_64-windows-gnu -O2 -std=c++17 "$(native "$here/packaging/windows/tos-start.cpp")" \
    -o "$(native "$stage/tos-start.exe")"
  rm -f "$stage"/*.pdb  # Zig writes debug info next to the executable; not shipped
  cp "$here/packaging/recomplauncher.windows.json" "$stage/recomplauncher.json"
  cp "$here/LICENSE" "$here/packaging/windows/THIRD-PARTY-NOTICES.txt" "$stage/"
  mkdir -p "$stage/toolchain" "$stage/licenses"
  cp -r "$zig_dist" "$stage/toolchain/zig"
  cp "$stage/toolkit/licenses/"* "$stage/licenses/"
  [ -z "$licenses" ] || cp -r "$licenses"/. "$stage/licenses/"

  # Same name inside the archive as the launcher expects: one folder on top is unwrapped by the launcher.
  archive="$name.zip"
  rm -f "$out/$archive"
  "$PYTHON" - "$build/stage" "$name" "$(native "$out/$archive")" <<'PY'
import os, sys, zipfile
root, name, target = sys.argv[1:4]
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for base, dirs, files in os.walk(os.path.join(root, name)):
        dirs.sort()
        for f in sorted(files):
            path = os.path.join(base, f)
            z.write(path, os.path.relpath(path, root).replace(os.sep, "/"))
PY
fi

( cd "$out" && sha256sum "$archive" > SHA256SUMS )
echo "wrote $out/$archive ($(du -h "$out/$archive" | cut -f1))"
