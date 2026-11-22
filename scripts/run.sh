#!/usr/bin/env bash
# Run the boot harness against the extracted disc 1.
#
#   scripts/run.sh [seconds]
#
# Useful environment variables (all optional):
#   TOS_SCRIPT="150:U,175:A"   scripted controller input: frame:button, held five frames (A B X Y S U D L R)
#   TOS_SCREENSHOT_DIR=dir     save presented frames as PPM files (game pictures: keep them out of git)
#   GP_SHOT_EVERY=N            save every Nth presented frame only
#   TOS_MAX_SHOTS=N            stop saving after N files
#   GP_TRACE=1                 trace draws, XF loads and EFB copies
#   GP_DUMP_TEX=dir            dump the first decoded textures as PPM files
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
seconds="${1:-20}"
cd "$here"
ulimit -s unlimited
exec ./build/tos_boot extracted/disc1/sys/main.dol extracted/disc1/files "$seconds"
