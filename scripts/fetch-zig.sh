#!/usr/bin/env bash
# Download and unpack the pinned Zig release (packaging/windows/zig.pin), checking its SHA-256.
# Zig (MIT licence) is what the Windows archive bundles as the C++ compiler, and what builds the runtime
# libraries it contains (docs/RELEASING.md). Works on Linux and, under Git Bash, on Windows.
#
#   scripts/fetch-zig.sh --host x86_64-windows|x86_64-linux --out DIR [--check-index]
#
#   --host H        which Zig package to fetch (default: the machine this runs on)
#   --out DIR       unpack here: DIR/zig-<host>-<version>/ (the folder is replaced when it exists); the path
#                   of the zig executable is printed on the last line
#   --check-index   also compare the pinned URL, size and SHA-256 with https://ziglang.org/download/index.json
#                   and fail on a difference (needs python3)
#   --cache DIR     keep the downloaded archive in DIR and reuse it when its checksum matches (default: --out)
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
pin="$here/packaging/windows/zig.pin"
host=; out=; cache=; check_index=0
while [ $# -gt 0 ]; do
  case "$1" in
    --host) host="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --cache) cache="$2"; shift 2 ;;
    --check-index) check_index=1; shift ;;
    -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$out" ] || { echo "--out is required" >&2; exit 2; }
if [ -z "$host" ]; then
  case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) host=x86_64-windows ;; *) host=x86_64-linux ;; esac
fi
version="$(awk '$1=="version"{print $2}' "$pin")"
read -r sha size url < <(awk -v h="$host" '$1==h{print $2, $3, $4}' "$pin")
[ -n "${url:-}" ] || { echo "no pin for host $host in $pin" >&2; exit 2; }
mkdir -p "$out"; out="$(cd "$out" && pwd)"
cache="${cache:-$out}"; mkdir -p "$cache"
file="$cache/$(basename "$url")"

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

if [ "$check_index" = 1 ]; then
  "${PYTHON:-python3}" - "$version" "$host" "$sha" "$size" "$url" <<'PY'
import json, sys, urllib.request
version, host, sha, size, url = sys.argv[1:6]
with urllib.request.urlopen("https://ziglang.org/download/index.json", timeout=60) as r:
    entry = json.load(r)[version][host]
bad = [(k, a, b) for k, a, b in (("shasum", sha, entry["shasum"]), ("size", size, entry["size"]), ("tarball", url, entry["tarball"])) if str(a) != str(b)]
for k, a, b in bad:
    print("zig.pin %s %s differs from ziglang.org: pinned %s, index %s" % (host, k, a, b), file=sys.stderr)
sys.exit(1 if bad else 0)
PY
  echo "zig.pin matches ziglang.org/download/index.json ($version, $host)" >&2
fi

if [ ! -f "$file" ] || [ "$(sha256 "$file")" != "$sha" ]; then
  echo "downloading $url" >&2
  # ziglang.org first; a download that stalls (under 100 kB/s for 30 s) or fails falls through to the community mirrors
  # of the pin file. A mirror is not trusted: the SHA-256 below decides.
  got_file=0
  for source in "$url" $(awk '$1=="mirror"{print $2 "/" "'"$(basename "$url")"'?source=tos-recomp"}' "$pin"); do
    echo "downloading $source" >&2
    if curl -fL --retry 2 --retry-delay 3 --speed-limit 100000 --speed-time 30 -o "$file.part" "$source"; then got_file=1; break; fi
  done
  [ "$got_file" = 1 ] || { echo "could not download Zig from ziglang.org or any mirror" >&2; exit 1; }
  mv "$file.part" "$file"
fi
got="$(sha256 "$file")"
[ "$got" = "$sha" ] || { echo "SHA-256 of $file is $got, expected $sha" >&2; rm -f "$file"; exit 1; }

dir="$out/zig-$host-$version"
rm -rf "$dir"
case "$file" in
  *.tar.xz) tar -C "$out" -xJf "$file" ;;
  # zip: python's zipfile, because Git Bash has no unzip and tar there cannot be relied on for zip files
  *.zip) "${PYTHON:-python3}" - "$file" "$out" <<'PY'
import sys, zipfile
zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])
PY
  ;;
  *) echo "unknown archive type $file" >&2; exit 1 ;;
esac
# Windows zips are named zig-x86_64-windows-<v>; older layouts differ: accept what unpacked.
if [ ! -d "$dir" ]; then
  alt="$(ls -d "$out"/zig-*"$version"* 2>/dev/null | grep -v '\.\(zip\|tar\.xz\)$' | head -n1 || true)"
  [ -n "$alt" ] && mv "$alt" "$dir"
fi
exe="$dir/zig"; [ -f "$exe.exe" ] && exe="$exe.exe"
[ -f "$exe" ] || { echo "no zig executable in $dir" >&2; exit 1; }
echo "$exe"
