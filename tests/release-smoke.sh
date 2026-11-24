#!/usr/bin/env bash
# End-to-end check of an unpacked release archive without a disc and without game data: tos-prepare reads two
# SYNTHETIC disc images (tos-synth-discs, prepare/tests/synth_discs.cpp), runs the toolkit's real recompiler on their
# four-instruction main.dol, compiles the output together with the real boot harness using the release's compiler
# and links it against the release's runtime libraries; the resulting game executable then starts, runs the
# synthetic program and exits. It proves that compiler, libraries, headers, recompiler and tos-prepare work together
# on this machine. It does not prove that the real game runs.
#
#   tests/release-smoke.sh --release DIR --synth tos-synth-discs[.exe] [--work DIR] [--keep] [--runner CMD]
#
#   --release DIR   the unpacked archive (the folder holding tos-prepare[.exe] and toolkit/)
#   --synth FILE    the synthetic disc tool: cmake --build <build-prepare> --target tos-synth-discs
#   --work DIR      scratch folder (default: a new temporary one, removed at the end unless --keep)
#   --keep          do not remove the scratch folder
#   --compiler CMD  give tos-prepare this compiler instead of the one in the archive (passed as --compiler)
#   --runner CMD    run the release's programs through CMD (e.g. wine): checks a Windows archive from a Linux host.
#                   Not the same as Windows: Wine is a re-implementation. --synth must then be a Windows tool too.
# Linux and Windows (Git Bash). Exit code 0 when everything passed.
set -euo pipefail

release=; synth=; work=; keep=0; runner=(); compiler=()
while [ $# -gt 0 ]; do
  case "$1" in
    --release) release="$2"; shift 2 ;;
    --synth) synth="$2"; shift 2 ;;
    --work) work="$2"; shift 2 ;;
    --keep) keep=1; shift ;;
    --compiler) compiler=(--compiler "$2"); shift 2 ;;
    --runner) read -r -a runner <<<"$2"; shift 2 ;;
    -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$release" ] && [ -n "$synth" ] || { echo "--release and --synth are required" >&2; exit 2; }
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
exe=; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) exe=.exe ;; esac
[ ${#runner[@]} -eq 0 ] || exe=.exe
release="$(cd "$release" && pwd)"
prepare="$release/tos-prepare$exe"
[ -f "$prepare" ] || { echo "no $prepare" >&2; exit 2; }
if [ -z "$work" ]; then work="$(mktemp -d)"; else mkdir -p "$work"; work="$(cd "$work" && pwd)"; fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$work"; }
trap cleanup EXIT

step() { echo "== $*"; }
fail() { echo "FAILED: $*" >&2; [ -f "$work/data/logs/prepare.log" ] && tail -n 60 "$work/data/logs/prepare.log" >&2; exit 1; }

step "synthetic discs"
"${runner[@]}" "$synth" --out "$(native "$work/discs")"

args=("$(native "$work/discs/disc1.iso")" "$(native "$work/discs/disc2.iso")" --data "$(native "$work/data")"
      --dumps "$(native "$work/discs/dumps.toml")" --symbols "$(native "$work/discs/symbols")" --jobs 2 "${compiler[@]}")

step "tos-prepare (verify, extract, recompile, compile, finalize)"
set +e
"${runner[@]}" "$prepare" "${args[@]}" | tee "$work/prepare.out"
code=${PIPESTATUS[0]}
set -e
[ "$code" = 0 ] || fail "tos-prepare exited with $code"
grep -q '^result ready 0 ' "$work/prepare.out" || fail "no 'result ready' line"
game="$work/data/bin/tos_boot$exe"
[ -f "$game" ] || fail "no game executable at $game"

step "tos-prepare --check (nothing stale)"
"${runner[@]}" "$prepare" --data "$(native "$work/data")" --dumps "$(native "$work/discs/dumps.toml")" --symbols "$(native "$work/discs/symbols")" --check \
  || fail "--check reported a stale stage (exit $?)"

step "the game executable runs the synthetic program"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
set +e
(cd "$work/data" && timeout 120 "${runner[@]}" "$game" game/sys/main.dol game/files 60) >"$work/game.out" 2>&1
code=$?
set -e
cat "$work/game.out"
[ "$code" = 0 ] || fail "the game executable exited with $code"
grep -q 'entry point returned' "$work/game.out" || fail "the game did not reach the end of the synthetic program"

step "tos-prepare --resume skips everything"
"${runner[@]}" "$prepare" --data "$(native "$work/data")" --resume --dumps "$(native "$work/discs/dumps.toml")" --symbols "$(native "$work/discs/symbols")" \
  | tee "$work/resume.out"
[ "$(grep -c '^stage-end .* skipped' "$work/resume.out")" -ge 4 ] || fail "--resume did not skip the finished stages"
echo "release smoke test passed"
