#!/usr/bin/env bash
# Build the Linux release archive in a container, exactly as the release workflow does (Ubuntu 22.04 with the packages
# of packaging/linux-build-deps.txt), then run the smoke test on it in plain containers WITHOUT a C++ compiler and
# WITHOUT network (docs/RELEASING.md). For a developer's machine or the owner's: it needs Docker or Podman and a
# network for the downloads (Zig, SDL3 sources), nothing installed besides.
#
#   scripts/docker-release.sh --out DIR [--version V] [--jobs N] [--gcrecomp DIR] [--common DIR] [--images "A B"] [--no-smoke]
#
#   --out DIR       the archive, SHA256SUMS and the work folders go here (created)
#   --version V     version of the archive (default 0.0.0-local)
#   --jobs N        parallelism and CPU limit of the build container (default 4)
#   --gcrecomp DIR / --common DIR   checkouts (default: ../gcrecomp and ../common next to this repository)
#   --images "A B"  images the smoke test runs in (default "ubuntu:22.04 debian:stable-slim")
#   --no-smoke      only build the archive
#   DOCKER=podman   use another container engine
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
engine="${DOCKER:-docker}"
out=; version=0.0.0-local; jobs=4; gcrecomp="$here/../gcrecomp"; common="$here/../common"; images="ubuntu:22.04 debian:stable-slim"; smoke=1
while [ $# -gt 0 ]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --version) version="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --gcrecomp) gcrecomp="$2"; shift 2 ;;
    --common) common="$2"; shift 2 ;;
    --images) images="$2"; shift 2 ;;
    --no-smoke) smoke=0; shift ;;
    -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$out" ] || { echo "--out is required" >&2; exit 2; }
mkdir -p "$out"; out="$(cd "$out" && pwd)"
gcrecomp="$(cd "$gcrecomp" && pwd)"; common="$(cd "$common" && pwd)"
uid="$(id -u):$(id -g)"

echo "== Zig"
"$here/scripts/fetch-zig.sh" --host x86_64-linux --out "$out/zig" --cache "$out/zig" | tail -n1
zdir="$(ls -d "$out"/zig/zig-x86_64-linux-*/ | head -n1)"

echo "== build image"
"$engine" build -q -t tos-recomp-release-build -f - "$here/packaging" <<'DOCKERFILE'
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
COPY linux-build-deps.txt /deps.txt
RUN apt-get update && grep -v '^#' /deps.txt | xargs apt-get install -y --no-install-recommends && rm -rf /var/lib/apt/lists/*
DOCKERFILE

mkdir -p "$out/dist" "$out/build" "$out/codec-licenses"
echo "== build the archive"
"$engine" run --rm --cpus "$jobs" --user "$uid" -e HOME=/tmp -e TMPDIR=/tmp \
  -v "$here:/work/tos-recomp:ro" -v "$gcrecomp:/work/gcrecomp:ro" -v "$common:/work/common:ro" \
  -v "$zdir:/zig:ro" -v "$out:/out" tos-recomp-release-build bash -ec '
    for pkg in zlib1g:zlib libbz2-1.0:bzip2 liblzma5:liblzma libzstd1:zstd; do
      cp "/usr/share/doc/${pkg%%:*}/copyright" "/out/codec-licenses/${pkg##*:}-copyright.txt"
    done
    /work/tos-recomp/scripts/package-release.sh --version "'"$version"'" --out /out/dist --build /out/build --jobs '"$jobs"' \
      --gcrecomp /work/gcrecomp --common /work/common --zig-dist /zig --portable --licenses /out/codec-licenses
    cmake -S /work/tos-recomp/prepare -B /out/build/synth -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTOS_COMMON_DIR=/work/common \
      -DTOS_PREPARE_STATIC_CODECS=ON -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" >/dev/null
    cmake --build /out/build/synth --target tos-synth-discs'

rm -rf "$out/release" && mkdir -p "$out/release"
tar -xzf "$out"/dist/*.tar.gz -C "$out/release"
rel="$(echo "$out"/release/*)"
ls -l "$out"/dist/*.tar.gz "$rel/tos-prepare"
[ "$smoke" = 1 ] || exit 0

for image in $images; do
  echo "== smoke test in $image (no compiler, no network)"
  work="$out/smoke-${image//[:\/]/_}"; rm -rf "$work"; mkdir -p "$work"
  "$engine" run --rm --network none --user "$uid" -v "$here:/src:ro" -v "$rel:/release:ro" -v "$out/build/synth:/synth:ro" -v "$work:/work" \
    "$image" bash /src/tests/release-smoke.sh --release /release --synth /synth/tos-synth-discs --work /work/run --keep --no-compiler
  "$here/tests/check-linux-game.sh" "$work/run/data/bin/tos_boot" --glibc 2.31
done
echo "release built and smoke-tested: $(ls "$out"/dist/*.tar.gz)"
