#!/usr/bin/env bash
# Play the game by hand and keep what the developers need to understand a problem.
#
#   scripts/play.sh [--session NAME] [--shots N] [--no-bench] [--minutes M]
#
# Starts tos_boot with a persistent memory card image, a log, a once-per-second performance line
# (GCRECOMP_BENCH) and a periodic screenshot, all under ~/.local/share/tos-recomp/<session>/ :
#
#   card.raw     the memory card (slot A); kept between sessions of the same name, so "Charger partie" works
#   run.log      everything the program printed (errors, unresolved calls, [gp] statistics, [hle] counts)
#   shots/       a screenshot every N presented frames (default 120, about every 2 s). Pictures of the game:
#                never commit them. The newest ones are the most useful after a glitch.
#
# Controls (keyboard): arrows = stick and D-pad; Enter or Z = A; X or Backspace = B; C = X; V = Y;
# Space = Start; W = Z; Q = L; E = R. A connected gamepad works as well. On a blank card the game asks to
# format it: choose "Formater".
#
# F1 (or the gamepad's Guide button) = settings overlay (To-Recomp/overlay), driven by packaging/recomplauncher.json
# (TOREC_MANIFEST): video, audio, controls, Quit. It reads and writes settings.toml and input.toml in
# RECOMPLAUNCHER_DATA (default: <session>/config).
#
# F10 = diagnostic bundle: records the next presented frame (draws, GPU state, EFB copies, pictures) into
# <session>/diag/diag-<n>-<time>/. Press it once while the problem is on screen (for example a blurred
# scene), wait two seconds, and tell the developers the session name. The bundle holds game pictures:
# never commit it.
#
# When you stop (close the window or Ctrl-C) the script prints where the files are and a short summary.
# Tell the developers the session name and what you did and saw; they read the files themselves.
# The memory card lives in the session folder: copy card.raw before experimenting if you want to keep a save.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
session="default"; every=120; bench=1; minutes=180
while [ $# -gt 0 ]; do
  case "$1" in
    --session) session="$2"; shift 2 ;;
    --shots) every="$2"; shift 2 ;;
    --no-bench) bench=0; shift ;;
    --minutes) minutes="$2"; shift 2 ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done

bin="${TOS_BIN:-$here/build/tos_boot}"
dol="${TOS_DOL:-$here/extracted/disc1/sys/main.dol}"
files="${TOS_FILES:-$here/extracted/disc1/files}"
[ -x "$bin" ] || { echo "missing $bin: build it first (scripts/regen.sh)" >&2; exit 1; }
[ -f "$dol" ] || { echo "missing $dol: extract the disc first (docs/DEVELOPING.md)" >&2; exit 1; }

dir="${XDG_DATA_HOME:-$HOME/.local/share}/tos-recomp/$session"
mkdir -p "$dir/shots"
[ -f "$dir/card.raw" ] || echo "[play] no memory card yet: the game will ask to format a new one (choose Formater)"

# keep the previous log of the same session
[ -f "$dir/run.log" ] && mv -f "$dir/run.log" "$dir/run.previous.log"
ulimit -s unlimited
export GCRECOMP_CARD_IMAGE="$dir/card.raw"
export TOS_SCREENSHOT_DIR="$dir/shots" GP_SHOT_EVERY="$every"
export GCRECOMP_DIAG_DIR="$dir/diag"
[ "$bench" = 1 ] && export GCRECOMP_BENCH=1
echo "[play] session '$dir' (log: run.log). Close the window to stop."
set +e
"$bin" "$dol" "$files" $((minutes * 60)) 2>&1 | tee "$dir/run.log" | grep -a -E '^\[(script|play|diag)\]|unknown command|Unresolved|FATAL|panic|PANIC' &
wait $!
set -e
echo
echo "[play] done. Files in $dir:"
ls -la "$dir" | tail -n +2
echo "[play] screenshots: $(ls "$dir/shots" 2>/dev/null | wc -l) ($(du -sh "$dir/shots" 2>/dev/null | cut -f1))"
echo "[play] diagnostic bundles: $(ls "$dir/diag" 2>/dev/null | wc -l) in $dir/diag"
grep -a -E '\[gp\]' "$dir/run.log" | tail -1 || true
grep -a -c -E 'Unresolved call' "$dir/run.log" | sed 's/^/[play] unresolved calls: /' || true
