#!/usr/bin/env bash
# End-to-end check of an unpacked release archive without a disc and without game data, on a machine WITHOUT a C++
# compiler and without the network: tos-prepare (the single self-contained file of the release) unpacks the payload it
# carries into a per-user cache, reads two SYNTHETIC disc images (tos-synth-discs, prepare/tests/synth_discs.cpp), runs
# the toolkit's real recompiler on their four-instruction main.dol, compiles the output together with the real boot
# harness using the Zig inside the payload and links it against the runtime libraries inside the payload; the
# resulting game executable then starts, runs the synthetic program and exits. It proves that unpacking, compiler,
# libraries, headers, recompiler and tos-prepare work together on this machine. It does not prove that the real game
# runs. Steps: first run unpacks the payload; a second run (--resume) skips every stage; deleting the cache makes the
# next run unpack again; damaging the cache does the same.
#
#   tests/release-smoke.sh --release DIR --synth tos-synth-discs[.exe] [--work DIR] [--keep] [--no-compiler | --scrub-path]
#                          [--compiler CMD] [--runner CMD]
#
#   --release DIR   the unpacked archive (the folder holding tos-prepare[.exe]; it must hold no toolkit/ or toolchain/ folder)
#   --synth FILE    the synthetic disc tool: cmake --build <build-prepare> --target tos-synth-discs
#   --work DIR      scratch folder (default: a new temporary one, removed at the end unless --keep). HOME (Linux) or
#                   LOCALAPPDATA (Windows) is set to a folder in it, so the payload is unpacked there, not in the real cache.
#   --keep          do not remove the scratch folder
#   --no-compiler   fail unless no C++ or C compiler (cc c++ gcc g++ clang clang++ zig tcc) is found on PATH: for a container
#                   or a machine that has none (the release workflow runs this in a minimal Docker image, without network)
#   --scrub-path    run the whole test with a PATH holding only the basic tools it needs (coreutils, bash, grep ...), for
#                   a machine that has compilers; implies --no-compiler. Linux only.
#   --compiler CMD  give tos-prepare this compiler instead of the one in the payload (passed as --compiler)
#   --runner CMD    run the release's programs through CMD (e.g. wine): checks a Windows archive from a Linux host.
#                   Not the same as Windows: Wine is a re-implementation. --synth must then be a Windows tool too.
# Linux and Windows (Git Bash). Exit code 0 when everything passed.
set -euo pipefail

release=; synth=; work=; keep=0; runner=(); compiler=(); no_compiler=0; scrub=0
orig_args=("$@")
while [ $# -gt 0 ]; do
  case "$1" in
    --release) release="$2"; shift 2 ;;
    --synth) synth="$2"; shift 2 ;;
    --work) work="$2"; shift 2 ;;
    --keep) keep=1; shift ;;
    --no-compiler) no_compiler=1; shift ;;
    --scrub-path) scrub=1; no_compiler=1; shift ;;
    --compiler) compiler=(--compiler "$2"); shift 2 ;;
    --runner) read -r -a runner <<<"$2"; shift 2 ;;
    -h|--help) sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
[ -n "$release" ] && [ -n "$synth" ] || { echo "--release and --synth are required" >&2; exit 2; }

# --scrub-path: start again with a PATH that holds only links to the tools below, and nothing else.
if [ "$scrub" = 1 ] && [ -z "${TOS_SMOKE_SCRUBBED:-}" ]; then
  bin="$(mktemp -d)"
  for t in bash sh env cat grep sed awk tee timeout mkdir rm ls head tail sort wc cut sleep uname dirname basename mktemp date id readlink \
           cp mv chmod tr du stat find ldd xargs seq uniq touch ln pwd; do
    p="$(command -v "$t" 2>/dev/null || true)"; [ -n "$p" ] && ln -s "$p" "$bin/$t"
  done
  rest=(); for a in "${orig_args[@]}"; do [ "$a" = --scrub-path ] || rest+=("$a"); done
  rc=0
  env -i HOME="$HOME" PATH="$bin" TOS_SMOKE_SCRUBBED=1 "$bin/bash" "$0" --no-compiler "${rest[@]}" || rc=$?
  rm -rf "$bin"
  exit "$rc"
fi

native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
exe=; windows=0; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) exe=.exe; windows=1 ;; esac
[ ${#runner[@]} -eq 0 ] || { exe=.exe; windows=1; }
release="$(cd "$release" && pwd)"
prepare="$release/tos-prepare$exe"
[ -f "$prepare" ] || { echo "no $prepare" >&2; exit 2; }
if [ -z "$work" ]; then work="$(mktemp -d)"; else mkdir -p "$work"; work="$(cd "$work" && pwd)"; fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$work"; }
trap cleanup EXIT

step() { echo "== $*"; }
fail() { echo "FAILED: $*" >&2; [ -f "$work/data/logs/prepare.log" ] && tail -n 60 "$work/data/logs/prepare.log" >&2; exit 1; }

# The release is the single file: nothing but the program may carry the toolkit and the compiler.
[ ! -e "$release/toolkit" ] && [ ! -e "$release/toolchain" ] || fail "$release has a toolkit/ or toolchain/ folder: this test is about the self-contained tos-prepare"

if [ "$no_compiler" = 1 ]; then
  step "no C++ or C compiler on this machine"
  found=0
  for c in cc c++ gcc g++ clang clang++ zig tcc; do
    if p="$(command -v "$c" 2>/dev/null)"; then echo "found $c: $p" >&2; found=1; fi
  done
  [ "$found" = 0 ] || fail "a compiler is available; the point of this test is that none is"
  echo "none of cc c++ gcc g++ clang clang++ zig tcc is on PATH ($PATH)"
fi

# The payload is unpacked into the user's cache: a fresh one in the scratch folder.
if [ "$windows" = 1 ]; then export LOCALAPPDATA="$(native "$work/home/AppData/Local")"; cache="$work/home/AppData/Local/tos-recomp"
else export HOME="$work/home"; unset XDG_CACHE_HOME TOS_RECOMP_CACHE; cache="$HOME/.cache/tos-recomp"; fi
mkdir -p "$work/home"
unset CXX CC

step "synthetic discs"
"${runner[@]}" "$synth" --out "$(native "$work/discs")"

args=("$(native "$work/discs/disc1.iso")" "$(native "$work/discs/disc2.iso")" --data "$(native "$work/data")"
      --dumps "$(native "$work/discs/dumps.toml")" --symbols "$(native "$work/discs/symbols")" --jobs 2 "${compiler[@]}")

[ ! -e "$cache" ] || fail "the cache exists before the first run"
step "tos-prepare, first run: unpack the payload, verify, extract, recompile, compile, finalize"
start=$SECONDS
set +e
"${runner[@]}" "$prepare" "${args[@]}" | tee "$work/prepare.out"
code=${PIPESTATUS[0]}
set -e
total=$((SECONDS - start))
[ "$code" = 0 ] || fail "tos-prepare exited with $code"
grep -q '^result ready 0 ' "$work/prepare.out" || fail "no 'result ready' line"
grep -q '^stage-start toolchain 1/6 ' "$work/prepare.out" || fail "the first run did not announce the toolchain unpack"
grep -q '^stage-end toolchain ok ' "$work/prepare.out" || fail "the first run did not unpack the payload"
[ "$(grep -c '^stage-end .* ok ' "$work/prepare.out")" = 6 ] || fail "not all six stages ran"
id="$(ls "$cache" | grep -E '^[0-9a-f]{16}$' | head -n1)"
[ -n "$id" ] && [ -f "$cache/$id/MANIFEST" ] || fail "no unpacked payload in $cache"
echo "unpack: $(grep '^stage-end toolchain ' "$work/prepare.out" | cut -d' ' -f4) s; whole first run: $total s; cache folder: $(du -sk "$cache/$id" | cut -f1) KiB"
grep -h 'toolchain:\|unpack:' "$work/data/logs/prepare.log" | sed 's/^/  log: /' || true
game="$work/data/bin/tos_boot$exe"
[ -f "$game" ] || fail "no game executable at $game"
echo "game executable: $(stat -c %s "$game" 2>/dev/null || wc -c <"$game") bytes; instance: $(du -sk "$work/data" | cut -f1) KiB"

if [ "$windows" = 0 ]; then
  step "the game executable needs nothing but the C library"
  echo "ldd:"; ldd "$game" | sed 's/^/  /' || true
  if ldd "$game" | grep -Ev 'libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|libutil\.so|ld-linux|linux-vdso' | grep -q .; then fail "the game executable depends on a library beyond the C library"; fi
  if command -v readelf >/dev/null 2>&1; then "$(dirname "$0")/check-linux-game.sh" "$game" || fail "the game executable is not limited to the C library"
  else echo "(readelf is not available here: the symbol versions are checked by tests/check-linux-game.sh on the build machine)"; fi
fi

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

start="$release/tos-start$exe"
if [ -f "$start" ]; then
  step "tos-start starts the game the way the launcher does (RECOMPLAUNCHER_DATA)"
  set +e
  RECOMPLAUNCHER_DATA="$(native "$work/data")" timeout 120 "${runner[@]}" "$start" >"$work/start.out" 2>&1
  code=$?
  set -e
  cat "$work/start.out"
  [ "$code" = 0 ] || fail "tos-start exited with $code"
  grep -q 'entry point returned' "$work/start.out" || fail "tos-start did not run the game to the end of the synthetic program"
  [ -d "$work/data/saves" ] || fail "tos-start did not create saves/"
fi

resume=("$prepare" --data "$(native "$work/data")" --resume --dumps "$(native "$work/discs/dumps.toml")" --symbols "$(native "$work/discs/symbols")")
step "tos-prepare --resume: the payload is there, every stage is skipped"
"${runner[@]}" "${resume[@]}" | tee "$work/resume.out"
grep -q '^stage-end toolchain skipped' "$work/resume.out" || fail "--resume unpacked the payload again"
[ "$(grep -c "^stage-end .* skipped" "$work/resume.out")" -ge 5 ] || fail "--resume did not skip the finished stages"

step "the cache is deleted: the next run unpacks again, then skips what is done"
rm -rf "$cache"
"${runner[@]}" "${resume[@]}" | tee "$work/resume2.out"
grep -q '^stage-end toolchain ok' "$work/resume2.out" || fail "the payload was not unpacked again after the cache was deleted"
[ "$(grep -c "^stage-end .* skipped" "$work/resume2.out")" -ge 4 ] || fail "the stages were not skipped after the payload was unpacked again"

step "a file of the cache is missing: the next run unpacks again"
victim="$(sed -n 's/^[0-9]* \(toolchain\/.*\/LICENSE\)$/\1/p' "$cache/$id/MANIFEST" | head -n1)"
[ -n "$victim" ] || victim="$(sed -n 's/^[0-9]* //p' "$cache/$id/MANIFEST" | head -n1)"
rm -f "$cache/$id/$victim"
"${runner[@]}" "${resume[@]}" | tee "$work/resume3.out"
grep -q '^stage-end toolchain ok' "$work/resume3.out" || fail "a damaged cache was not detected"
[ -f "$cache/$id/$victim" ] || fail "the damaged cache was not repaired"
echo "release smoke test passed"
