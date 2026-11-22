#!/usr/bin/env bash
# Build one contact sheet from a directory of frames, to look at a whole run at a glance.
#
#   scripts/contact-sheet.sh <frames-dir> <out.png> [--count 6] [--cols 3] [--size 400x300]
#
# Accepts the PPM files written by tos_boot (TOS_SCREENSHOT_DIR) and the PNG files of Dolphin's frame dump
# (Dolphin: User/Dump/Frames/framedump_N.png). Frames are picked evenly across the sorted list, so two runs
# can be compared side by side with the same --count. Needs ImageMagick (`magick`).
# The pictures are game assets: keep the output out of git.
set -euo pipefail

usage() { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
[ $# -ge 2 ] || usage 2
dir="$1"; out="$2"; shift 2
count=6; cols=3; size=400x300
while [ $# -gt 0 ]; do
  case "$1" in
    --count) count="$2"; shift 2 ;;
    --cols) cols="$2"; shift 2 ;;
    --size) size="$2"; shift 2 ;;
    -h|--help) usage 0 ;;
    *) echo "unknown option $1" >&2; usage 2 ;;
  esac
done
command -v magick >/dev/null || { echo "ImageMagick (magick) is required" >&2; exit 1; }

mapfile -t all < <(find "$dir" -maxdepth 1 -type f \( -name '*.ppm' -o -name '*.png' \) | sort -V)
n=${#all[@]}
[ "$n" -gt 0 ] || { echo "no .ppm or .png frames in $dir" >&2; exit 1; }
[ "$count" -gt "$n" ] && count=$n

picked=()
for ((i = 0; i < count; i++)); do
  idx=$(( count == 1 ? 0 : i * (n - 1) / (count - 1) ))
  picked+=("${all[$idx]}")
done
rows=$(( (count + cols - 1) / cols ))
magick montage "${picked[@]}" -tile "${cols}x${rows}" -geometry "${size}+2+2" "$out"
echo "$out: $count of $n frames"
