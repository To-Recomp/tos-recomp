#!/usr/bin/env bash
# Build the release archive the To-Recomp launcher installs (docs/RELEASING.md): ONE self-contained tos-prepare (the
# toolkit and the pinned Zig compiler packed inside it), the launcher manifest and the start program, as
# tos-recomp-<version>-linux-x86_64.tar.gz or tos-recomp-<version>-windows-x86_64.zip, plus SHA256SUMS. Nothing in it is
# derived from the game.
#
#   scripts/package-release.sh --version X.Y.Z[-rc.N] --out DIR --zig-dist DIR [--portable] [options]            (Linux)
#   scripts/package-release.sh --platform windows --version X.Y.Z[-rc.N] --out DIR --zig-dist DIR \
#                              --prepare-exe FILE --payload-tool FILE [--zig FILE] [options]
#
#   --version V       the release version without the leading v (also printed by tos-prepare --version)
#   --out DIR         where the archive and SHA256SUMS are written (created)
#   --zig-dist DIR    the unpacked pinned Zig FOR THE PLATFORM BEING PACKAGED (scripts/fetch-zig.sh --host x86_64-linux or
#                     x86_64-windows): it is trimmed (scripts/trim-zig.sh) and packed into tos-prepare, and on Linux it also
#                     builds the toolkit
#   --zig FILE        the zig executable that builds the toolkit (default: the one in --zig-dist, which must run on this
#                     host; a Linux host packaging Windows passes its own Linux zig)
#   --no-trim         pack the whole Zig
#   --linux-target T  Linux: Zig's target for the game executable (default x86_64-linux-gnu.2.31: runs on glibc 2.31 and later)
#   --gcrecomp DIR    gcrecomp checkout (default: ../gcrecomp)      --common DIR  common checkout (default: ../common)
#   --build DIR       scratch folder for the builds (default: $TMPDIR/tos-release-build)
#   --jobs N          build parallelism (default 4)
#   --portable        Linux: link tos-prepare with the C++ runtime and the disc codecs (zlib, bzip2, lzma, zstd) inside, so it
#                     runs on any distribution whatever the soname of those libraries. Needs their static libraries
#                     (Ubuntu: libstdc++ and the -dev packages; Fedora: libstdc++-static, bzip2-static, xz-static, libzstd-static)
#   --licenses DIR    licence files of what is linked into tos-prepare (the disc codecs), copied to licenses/
#   --preset N        xz preset of the payload, 0..9 with extreme (default 9: the smallest, about 2 minutes per 400 MB)
#   --prepare-exe F   windows: the tos-prepare.exe to ship (built with MSVC in release.yml: statically linked, with the disc
#                     codecs inside: cmake -S prepare ... -DTOS_PREPARE_STATIC_CODECS=ON, see docs/RELEASING.md)
#   --payload-tool F  windows: the tos-payload.exe built next to it (cmake --build ... --target tos-payload)
#   --platform P      linux (default) or windows. Windows runs on Windows (Git Bash) or on any host that can run Zig.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
version=; out=; gcrecomp="$here/../gcrecomp"; common="$here/../common"; build="${TMPDIR:-/var/tmp}/tos-release-build"; jobs=4; portable=0
platform=linux; zig_dist=; zig=; prepare_exe=; payload_tool=; licenses=; trim=1; preset=9; linux_target=x86_64-linux-gnu.2.31
PYTHON="${PYTHON:-python3}"
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --gcrecomp) gcrecomp="$2"; shift 2 ;;
    --common) common="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --portable) portable=1; shift ;;
    --platform) platform="$2"; shift 2 ;;
    --zig-dist) zig_dist="$2"; shift 2 ;;
    --zig) zig="$2"; shift 2 ;;
    --no-trim) trim=0; shift ;;
    --linux-target) linux_target="$2"; shift 2 ;;
    --prepare-exe) prepare_exe="$2"; shift 2 ;;
    --payload-tool) payload_tool="$2"; shift 2 ;;
    --licenses) licenses="$2"; shift 2 ;;
    --preset) preset="$2"; shift 2 ;;
    -h|--help) sed -n '2,31p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$version" ] && [ -n "$out" ] || { echo "--version and --out are required" >&2; exit 2; }
case "$version" in v*) echo "--version takes X.Y.Z, without the v" >&2; exit 2 ;; esac
case "$platform" in linux|windows) ;; *) echo "--platform is linux or windows" >&2; exit 2 ;; esac
[ -n "$zig_dist" ] || { echo "--zig-dist is required (scripts/fetch-zig.sh)" >&2; exit 2; }
zig_dist="$(cd "$zig_dist" && pwd)"
gcrecomp="$(cd "$gcrecomp" && pwd)"
mkdir -p "$out" "$build"; out="$(cd "$out" && pwd)"
# Windows tools (CMake, Zig) want C:/... paths; under Git Bash `pwd` gives /c/...
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

name="tos-recomp-$version-$platform-x86_64"
stage="$build/stage/$name"
payload="$build/payload"
rm -rf "$build/stage" "$payload"; mkdir -p "$stage" "$payload/toolchain"

if [ "$platform" = linux ]; then
  common="$(cd "$common" && pwd)"
  [ -f "$zig_dist/zig" ] && [ -f "$zig_dist/LICENSE" ] || { echo "$zig_dist is not an unpacked Zig for Linux (zig, LICENSE)" >&2; exit 2; }
  [ -n "$zig" ] || zig="$zig_dist/zig"
  # tos-prepare and the payload tool
  portable_args=()
  [ "$portable" = 1 ] && portable_args=(-DTOS_PREPARE_STATIC_CODECS=ON -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc")
  cmake -S "$here/prepare" -B "$build/prepare" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DTOS_COMMON_DIR="$common" -DTOS_PREPARE_VERSION="$version" "${portable_args[@]}" >/dev/null
  cmake --build "$build/prepare" -j "$jobs" --target tos-prepare tos-payload
  if [ "$portable" = 1 ] && ldd "$build/prepare/tos-prepare" | grep -E 'libz|libbz2|liblzma|libzstd|libstdc'; then
    echo "tos-prepare still depends on a codec or the C++ runtime (--portable)" >&2; exit 1
  fi
  prepare_exe="$build/prepare/tos-prepare"; payload_tool="$build/prepare/tos-payload"
  "$here/scripts/make-toolkit.sh" --gcrecomp "$gcrecomp" --out "$payload/toolkit" --build "$build/gcrecomp" --jobs "$jobs" \
    --zig "$zig" --target "$linux_target"
  zexe=zig; exe=; start_files=("$here/packaging/tos-start"); manifest="$here/packaging/recomplauncher.json"
else
  [ -n "$prepare_exe" ] && [ -n "$payload_tool" ] || { echo "--platform windows needs --prepare-exe and --payload-tool" >&2; exit 2; }
  [ -f "$prepare_exe" ] && [ -f "$payload_tool" ] || { echo "no $prepare_exe or $payload_tool" >&2; exit 2; }
  [ -f "$zig_dist/zig.exe" ] && [ -f "$zig_dist/LICENSE" ] || { echo "$zig_dist is not an unpacked Zig for Windows (zig.exe, LICENSE)" >&2; exit 2; }
  [ -n "$zig" ] || zig="$zig_dist/zig.exe"
  [ -f "$zig" ] || { echo "no zig executable at $zig" >&2; exit 2; }
  "$here/scripts/make-toolkit.sh" --zig "$zig" --target x86_64-windows-gnu --gcrecomp "$gcrecomp" --out "$payload/toolkit" \
    --build "$build/gcrecomp" --jobs "$jobs"
  zexe=zig.exe; exe=.exe; manifest="$here/packaging/recomplauncher.windows.json"
fi

# The compiler inside the payload: a copy of the pinned Zig, without what this release never uses.
cp -a "$zig_dist" "$payload/toolchain/zig"
[ "$trim" = 1 ] && "$here/scripts/trim-zig.sh" --target "$platform" "$payload/toolchain/zig"
[ -f "$payload/toolchain/zig/$zexe" ] || { echo "no $zexe in the payload" >&2; exit 1; }
rm -f "$payload/toolkit/licenses/"*.pdb 2>/dev/null || true

# The program with its payload. A Linux program is stripped BEFORE (strip drops the bytes after the last section).
cp "$prepare_exe" "$build/tos-prepare-base$exe"
[ "$platform" = linux ] && strip "$build/tos-prepare-base" 2>/dev/null || true
"$payload_tool" attach --exe "$(native "$build/tos-prepare-base$exe")" --dir "$(native "$payload")" \
  --out "$(native "$stage/tos-prepare$exe")" --preset "$preset"
chmod 755 "$stage/tos-prepare$exe"

# Start program, manifest, licences.
if [ "$platform" = linux ]; then
  cp "$here/packaging/tos-start" "$stage/"; chmod 755 "$stage/tos-start"
else
  # tos-start.exe: the launcher starts it; it starts the game (packaging/windows/tos-start.cpp).
  "$zig" c++ -target x86_64-windows-gnu -O2 -std=c++17 "$(native "$here/packaging/windows/tos-start.cpp")" \
    -o "$(native "$stage/tos-start.exe")"
  rm -f "$stage"/*.pdb  # Zig writes debug info next to the executable; not shipped
fi
cp "$manifest" "$stage/recomplauncher.json"
cp "$here/LICENSE" "$here/packaging/THIRD-PARTY-NOTICES.txt" "$stage/"
mkdir -p "$stage/licenses"
# What the payload carries, with its licence texts readable without unpacking anything.
cp "$payload/toolkit/licenses/"* "$stage/licenses/"
zl="$payload/toolchain/zig"
cp "$zl/LICENSE" "$stage/licenses/Zig-LICENSE.txt"
cp "$zl/lib/libcxx/LICENSE.TXT" "$stage/licenses/LLVM-libcxx-LICENSE.txt"
cp "$zl/lib/libcxxabi/LICENSE.TXT" "$stage/licenses/LLVM-libcxxabi-LICENSE.txt"
cp "$zl/lib/libunwind/LICENSE.TXT" "$stage/licenses/LLVM-libunwind-LICENSE.txt"
if [ "$platform" = linux ]; then cp "$zl/lib/libc/glibc/LICENSES" "$stage/licenses/glibc-LICENSES.txt"
else cp "$zl/lib/libc/mingw/COPYING" "$stage/licenses/mingw-w64-COPYING.txt"; fi
[ -z "$licenses" ] || cp -r "$licenses"/. "$stage/licenses/"

# Same name inside the archive as the launcher expects: one folder on top is unwrapped by the launcher.
if [ "$platform" = linux ]; then
  archive="$name.tar.gz"
  tar -C "$build/stage" --owner=0 --group=0 --numeric-owner --sort=name -czf "$out/$archive" "$name"
else
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
echo "wrote $out/$archive ($(du -h "$out/$archive" | cut -f1)); payload: $("$payload_tool" info "$(native "$stage/tos-prepare$exe")")"
