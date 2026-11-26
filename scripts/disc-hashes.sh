#!/usr/bin/env bash
# Print what dumps.toml needs for each disc image: game id, disc number, version byte, size, whole-image
# SHA-1 (compared with the one dumps.toml lists, when the game id and disc number are known there) and content_sha1.
#
#   scripts/disc-hashes.sh [--tool PATH/torecomp-disc] [--dumps dumps.toml] [--manifests DIR] DISC...
#
# DISC is an image torecomp-disc reads (iso, gcm, ciso, gcz, wia, rvz) or a .7z that holds one. A .7z is unpacked to
# a temporary folder under /var/tmp (needs 7z) and removed afterwards. --manifests DIR also keeps each disc's file
# list (name, size, SHA-1 per file) as DIR/<disc name>.tsv, to compare releases: it is game-derived, keep it out of git.
# Heavy: one image at a time, run it under `nice -n 19 ionice -c3`.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
tool="$here/../common/build/torecomp-disc"
dumps="$here/dumps.toml"
discs=()
manifests=""
while [ $# -gt 0 ]; do
  case "$1" in
    --tool) tool="$2"; shift 2 ;;
    --dumps) dumps="$2"; shift 2 ;;
    --manifests) manifests="$2"; shift 2 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) discs+=("$1"); shift ;;
  esac
done
[ ${#discs[@]} -gt 0 ] || { echo "usage: $0 [--tool torecomp-disc] [--dumps dumps.toml] DISC..." >&2; exit 2; }
[ -x "$tool" ] || { echo "$0: torecomp-disc not found at $tool (use --tool)" >&2; exit 2; }

tmp=""
cleanup() { [ -z "$tmp" ] || rm -rf "$tmp"; }
trap cleanup EXIT

for disc in "${discs[@]}"; do
  img="$disc"
  if [ "${disc##*.}" = "7z" ]; then
    command -v 7z >/dev/null || { echo "$0: 7z is needed for $disc" >&2; exit 2; }
    tmp="$(mktemp -d /var/tmp/disc-hashes.XXXXXX)"
    7z x -y -bd -o"$tmp" "$disc" >/dev/null
    img="$(find "$tmp" -type f | head -1)"
  fi
  info="$("$tool" info "$img")"
  game_id="$(sed -n 's/^game id: *\([A-Z0-9]\{4\}\).*/\1/p' <<<"$info")"
  disc_no="$(sed -n 's/^disc: *\([0-9]*\) .*version \([0-9]*\)$/\1/p' <<<"$info")"
  version="$(sed -n 's/^disc: *\([0-9]*\) .*version \([0-9]*\)$/\2/p' <<<"$info")"
  size="$(sed -n 's/^size: *\([0-9]*\)$/\1/p' <<<"$info")"
  sha1="$("$tool" hash "$img" 2>/dev/null | grep -Eo '[0-9a-f]{40}' | tail -1)"
  # torecomp-disc prints the progress and content_sha1 on stderr and the file list on stdout.
  man=/dev/null
  if [ -n "$manifests" ]; then mkdir -p "$manifests"; man="$manifests/$(basename "${disc%.*}").tsv"; fi
  content="$("$tool" manifest "$img" 2>&1 >"$man" | tr '\r' '\n' | sed -n 's/^content_sha1 = "\(.*\)"$/\1/p' | tail -1)"
  known="no entry in $(basename "$dumps")"
  if grep -q "$sha1" "$dumps" 2>/dev/null; then known="sha1 listed in $(basename "$dumps")"; fi
  printf '# %s\ngame_id = "%s"\ndisc = %s\nversion = %s\nsize = %s\nsha1 = "%s"   # %s\ncontent_sha1 = "%s"\n\n' \
    "$(basename "$disc")" "$game_id" "$disc_no" "$version" "$size" "$sha1" "$known" "$content"
  cleanup; tmp=""
done
