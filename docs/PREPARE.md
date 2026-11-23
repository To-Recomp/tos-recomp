# Prepare: from two discs to a playable game

Issue: [To-Recomp/tos-recomp#3](https://github.com/To-Recomp/tos-recomp/issues/3).

Decisions this design follows (owner's review): the recompilation runs **on the player's machine, once, from the player's own discs**; nothing derived from the game is distributed; the result runs **without the discs**; the extracted game files stay in the instance, so a rebuild after an update needs no disc; saves use the console's memory card image only.

Status: this document is the design (PR 1, with the measurements). `tos-prepare` (PR 2, in `prepare/`) implements it for Linux. Measurements marked **pending** were not done yet.

Origin: the stage/progress/exit-code approach is adapted from the owner's `fepor-recomp prepare` (Rust, GPL-2.0: `crates/cli/src/commands/generate.rs`, ADR 0018 "the output folder is the game", ADR 0023 "toolchain for players"). Nothing was copied; the ideas reused are `progress` lines, copying the disc files into the instance so the game runs alone, and a marker written last.

## 1. The program

`tos-prepare` is **one C++17 program in this repository** (`prepare/`, CMake), linked with `To-Recomp/common` (disc reading, verification, extraction). Why C++ and not Rust, shell or Python:

- The player already needs a C++ compiler for the compile stage; a second runtime (Python, Rust toolchain) would be one more thing to ship or install. A single static executable next to the launcher is the smallest thing that works on Linux, Windows and SteamOS.
- `common` is C++17 and already does everything the first two stages need (hash-verified extraction of ISO/CISO/GCZ/WIA/RVZ, Windows-safe names). Calling it as a library avoids parsing the output of `torecomp-disc`.
- Shell scripts do not run on Windows; the launcher is Tauri (Rust) but linking it with the recompiler would tie the launcher to every game's pipeline.

Build: `cmake -S prepare -B build-prepare -G Ninja -DTOS_COMMON_DIR=../common && cmake --build build-prepare && ctest --test-dir build-prepare` (as with gcrecomp, `common` is a sibling checkout by default; `-DTOS_COMMON_DIR` moves it). Tests need no game data (synthetic discs, stub recompiler and stub compiler, see `prepare/tests`).

### Command line

```
tos-prepare <disc1> <disc2> --data DIR [--jobs N] [--compiler CMD] [--resume] [--cancel-file F]
tos-prepare --data DIR --resume          rebuild from the files kept in DIR, no disc needed
tos-prepare --data DIR --check           exit 0 if DIR is up to date for this program, 10 if not
   --toolkit DIR   (default: next to the program)     --symbols DIR / --dumps FILE   development overrides
```

- The discs may be given in either order (disc 2 first gets a `warn`). Any format `common` reads is accepted.
- `--jobs` defaults to `min(cores, free RAM / 1.5 GB)` (0.8 GB per job when the compiler is clang): see section 4 for the per-process memory.
- `--compiler` is a command, split on spaces (`"zig c++"` works). Default: `$CXX`, then `clang++`, `g++`, `c++` from the toolkit's list.
- `--cancel-file F`: when `F` exists the program stops cleanly. It must not exist at start (exit 2); the program never deletes it. SIGINT/SIGTERM do the same, which is how a launcher without a file can cancel.
- `--resume` skips every stage whose recorded input hash is unchanged and whose outputs are still there. Without `--resume` all stages run again.

### Toolkit

Everything the program needs that is ours (so it can be shipped; nothing is game-derived) lives in the **toolkit** folder described by `toolkit.json`: `bin/gcrecomp_recompiler`, `lib/libgcrecomp_*.a`, `include/gcrecomp/` (what the generated code includes), `src/main.cpp` (the boot harness), `dumps.toml`, `symbols/`, and the link line, compiler flags, how to start the game and the disk estimates. `scripts/make-toolkit.sh --gcrecomp ../gcrecomp --out DIR` builds one (1.9 MB). The recompiler and the runtime libraries are prebuilt on our side, so the player's machine only compiles the generated code (36 files) plus the harness. `tos-prepare` finds the toolkit in `<exe dir>/toolkit`, `<exe dir>/../share/tos-recomp/toolkit` or `--toolkit`.

### Stages

| # | Stage | What it does | Needs the discs |
|---|---|---|---|
| 1 | `verify` | `common`'s `identify` against `dumps.toml` for each disc: **Redump verified** or **Compatible**; both discs of the release present, no duplicate. | yes |
| 2 | `extract` | Extracts disc 1 completely and the files only disc 2 has, into `game/`; each extraction is hashed and refused if it does not match the database. | yes |
| 3 | `recompile` | `gcrecomp_recompiler main.dol --csv --extra-funcs --rel ... --output generated --project tos`, as `scripts/regen.sh` does (0.5 s). | no |
| 4 | `compile` | Compiles `generated/*.cpp` and the harness with the C++ compiler (object cache keyed by content), links with the runtime libraries into `bin/tos_boot`. | no |
| 5 | `finalize` | Creates `saves/ config/ logs/`, writes the versions and the way to start the game into `prepare.json`. | no |

**What disc 2 adds** (measured with `torecomp-disc manifest` on the owner's two scrubbed dumps): the discs have the same `main.dol`, apploader and the same 1,667 files (1,076 MB); they differ in `opening.bnr` (banner, 7.5 KB) and in the content of the story: disc 1 alone has 16 files (256 MB: `EV/sce_001_009.afs` ... `sce_071_083.afs`, movies `as1 s01 s03 s07 s08`, `ndog01..03.bin`), disc 2 alone has 9 files (313 MB: `EV/sce_084_090.afs` ... `sce_121_132.afs`, movies `as2 as3 s09 s10`). So the instance holds one merged tree (1.65 GB instead of 2.8 GB): disc 1 whole, plus disc 2's nine files. On a differing file the disc 1 copy wins and the difference is logged (`opening.bnr`). **Not tested:** whether the game, when it asks for disc 2, accepts the merged tree (disc-id checks in the DVD HLE). That is a gcrecomp question to settle by playing past the disc change; see open points.

### Progress protocol (stdout)

One line each, flushed; everything else (child output, details) goes to `logs/prepare.log`:

```
protocol 1 tos-prepare 0.1.0
stage-start extract 2/5 Copying the game files from the discs
stage-progress extract 734003200/2919956480 Copying the files of disc 1     (done/total; units are bytes, files or per-mille by stage)
progress 0.167 Copying the files of disc 1        overall 0..1, the launcher's own `progress <fraction> <text>` format
stage-end extract ok 41.3                         ok | skipped | failed | cancelled, seconds
warn <text>
result ready 0 the game is prepared in <dir>      ready | failed | cancelled, exit code, text
```

Deviation from the first sketch (`progress <stage> <done>/<total> <message>`): the launcher already parses `progress <fraction> <text>` (`launcher/src-tauri/src/library/run.rs`, `docs/library.md`), and a line `progress extract 3/10 ...` would not parse there. So the structured per-stage line is `stage-progress`, and `progress` keeps the launcher's format. Lines are throttled to one per percent of a stage. Stage weights of the overall bar: verify 0.10, extract 0.25, recompile 0.02, compile 0.61, finalize 0.02 (**to be adjusted from the end-to-end timings**).

### Exit codes

| Code | Meaning |
|---|---|
| 0 | prepared (or already up to date) |
| 1 | unexpected error |
| 2 | bad usage; also a rebuild without discs when the kept files are missing; cancel file already present |
| 3 | a disc is missing, unreadable, from another game/release, a duplicate, or not a known dump (message gives the SHA-1 and content_sha1) |
| 4 | not enough free disk space (message gives need and available) |
| 5 | the C++ compiler or the linker failed (message gives the file and the last lines) |
| 6 | the recompiler failed |
| 7 | toolkit missing or inconsistent, or no usable C++ compiler |
| 8 | extraction failed (read error, hash mismatch, I/O) |
| 9 | another `tos-prepare` is working on the same `--data` (flock on `.prepare.lock`) |
| 10 | `--check` only: the instance is stale |
| 130 | cancelled |

### Free space

Before each stage that writes a lot, the program computes the need and refuses with exit 4 if `free < need + 256 MB`: extract = both discs' bytes (disc 2 is staged before the merge) **plus** what the later stages will need, so a doomed run fails in the first minute, not after the extraction; recompile = 32 B per input byte (DOL + RELs; measured 24); compile = 1.6 × generated bytes minus the object cache already there, plus the executable (estimates in `toolkit.json`, `disk`). The launcher's own 1 GB check is lower than the 2.5 GB the whole run needs.

### Cancellation and partial results

SIGINT/SIGTERM or the cancel file are noticed within 100 ms. Every child process runs in its own process group, which is killed (SIGTERM, then SIGKILL after 3 s). Common's extraction throws out of its progress callback and removes what it wrote; `game.partial/`, `work/`, `generated.partial/` and `build/tmp/` are removed. Finished stages are kept; the interrupted one is marked `cancelled` in `prepare.json`, and `--resume` runs it again. Files are written under a temporary name and renamed (state file, objects, executable), so a kill -9 leaves at worst a `.tmp`/`partial` name the next run deletes. If `tos-prepare` itself is killed with SIGKILL its children are not (no pdeath signal with `posix_spawn`); the launcher should signal the process group.

### Resume

Each stage records `input` = a hash of what its result depends on:

- verify: dumps.toml + each disc's path, size, mtime (without discs: the recorded result stands);
- extract: the two `content_sha1`s + the layout version;
- recompile: DOL SHA-1, RELs SHA-1, both symbol files, the recompiler binary, project name;
- compile: compiler id (`--version` + `-dumpmachine`), flags, **toolkit hash** (toolkit.json, libraries, harness source, all headers), and the hash of the generated files. Inside the stage each object is cached by `sha1(compiler, flags, toolkit hash, generated headers, source)`: a changed symbol that changes 3 of 36 files recompiles 3 files. Unused objects are deleted after a successful link.

A stage is skipped when `--resume` is given, its record is `done` with the same hash and its outputs exist (`game/manifest.tsv` files with sizes, `generated/generated.tsv`, the executable). A stage that reruns and produces the same bytes does not invalidate the next one. `--check` evaluates the same rule without running anything: this is what a launcher update uses to show `needs rebuild` (exit 10 + one `check <stage> up-to-date|stale` line each).

## 2. Instance folder

Consistent with `launcher/docs/library.md`: the launcher's instance has `instance.json`, `data/` (= `{data}`), `prepare.log`, `game.log`; `tos-prepare --data {data}` fills `{data}`:

```
{data}/
  prepare.json     the manifest, written after every stage ("state": ready | preparing | failed | cancelled)
  game/            KEPT. sys/ (disc 1) + files/ (merged) + paths.tsv + manifest.tsv. Rebuilds start here.
  generated/       cache, deletable (regenerated in 0.5 s from game/): the 98 MB of C++
  build/obj/       cache, deletable: objects keyed by content (75-139 MB), makes the next build incremental
  bin/tos_boot     the game executable
  saves/           memory card image (card-a.raw): the console's own format, nothing else
  config/          reserved for the settings the launcher edits (settings-manifest)
  logs/prepare.log appended per run, rotated at 4 MB (the launcher also keeps its own prepare.log from stdout)
  .prepare.lock
```

`prepare.json` has what the launcher needs: `state`; per-stage `status/input/seconds/info`; `discs` (disc number, level `redump-verified|compatible`, content_sha1); `versions` (`gcrecomp`, `tos_recomp` commits from the toolkit, `toolkit_sha1`, `symbols_sha1`, `recompiler_sha1`, `compiler`, `tos_prepare`); `run` (`exe`, `args`, `env`, `unlimited_stack`, relative to `{data}`); `paths`. The launcher's `needs rebuild` is `tos-prepare --check` (or comparing `versions` with the new release); the launcher's `prepare.keeps_copy` is `game/`.

Start line the launcher should use (relative to `{data}`; `unlimited_stack` means `ulimit -s unlimited`, which `scripts/run.sh` also does): `bin/tos_boot game/sys/main.dol game/files 86400` with `GCRECOMP_CARD_IMAGE=saves/card-a.raw`. The harness's third argument is a number of seconds, there is no "forever" yet (an open point: the harness is a development tool, the final game executable is a later step).

## 3. The compiler question

How the 98 MB of generated C++ (36 `.cpp`, between 9 KB and 10.5 MB each, 5,601 functions) is compiled on the player's machine. Measured on the dev machine (Ryzen 7 9800X3D, 16 threads, 60 GB RAM shared with a running desktop, `nice -n 19`, `-j4`, GCC 16.2, clang 23.1, ccache 4.13, runtime headers from gcrecomp `25da416`) with `scripts/measure-compile.sh`. "RSS" is the largest resident set of **one** compiler process (peak memory ≈ jobs × RSS).

| Compiler, flags | Wall (-j4) | CPU sum | RSS of one | Objects |
|---|---|---|---|---|
| g++ -O1 (today's `CMakeLists.txt`) | 103.4 s | 318 s | 1,206 MB | 75 MB |
| g++ -Og | 37.9 s | 118 s | 991 MB | 49 MB |
| g++ -O0 | 29.7 s | 96 s | 1,004 MB | 78 MB |
| clang++ -O1 | **80.6 s** | 253 s | **566 MB** | 139 MB |
| clang++ -O0 | **9.9 s** | 34 s | 494 MB | 73 MB |
| clang++ -O1 through ccache, cold | 71.1 s | 220 s | 566 MB | 139 MB |
| clang++ -O1 through ccache, warm | **0.1 s** | 0 s | | 139 MB |
| g++ -O1, unity (6 generated files per unit) | 75.3 s | 247 s | **2,980 MB** | 74 MB |

(Times at `nice -n 19` on a machine in use; the last two lines of an otherwise idle machine would be faster. The CPU sums are the stable numbers.)

Readings:

- **clang is leaner than gcc**: 0.6 GB vs 1.2 GB per process at -O1, 20% faster. At `-j4` the peak is 2.2 GB (clang) or 4.7 GB (gcc); a 16 GB Steam Deck can afford `-j8` with clang, not with gcc.
- **The cost is optimisation, not parsing**: clang -O0 is 8 times faster than -O1 (10 s vs 81 s). Whether -O0 is playable is the question: the earlier observation was 53% CPU at -O0 vs 30% at -O1 during the opening movie; the speed of each level on the title and the movie is **pending** (needs the game running, see section 6).
- **Unity builds are worse**: 3 GB per process, no faster. **Splitting** helps only through load balance: the three largest files take 25-37 s each, so at -j8 and above the longest file dominates (a 8-thread machine finishes in about max(253/8, 37) = 37 s of wall at best). `--funcs-per-file 50` instead of 200 would balance better: **pending**.
- **ccache** (or our own object cache, which does the same thing keyed by content) turns an unchanged rebuild into 0.1 s. Per-file caching only pays after a *partial* change; the symbol files are what changes between releases, and a symbol edit that changes few functions changes few files: **pending** (count of changed files after a real symbol update).
- **Generated code size**: 98 MB for a 2.9 MB DOL + 1.2 MB of RELs (24 bytes per input byte), mostly because each instruction becomes a line plus a comment. Reducing it is a recompiler matter (gcrecomp) and would cut compile time proportionally; not attempted here.

### What players have

- **Linux** (Fedora, Ubuntu, Arch): usually no compiler on a desktop install; `gcc-c++` or `clang` is one package. A COPR/RPM can `Requires: clang` (or `gcc-c++`) and `SDL3`; a Flatpak would carry its own SDK.
- **SteamOS (Steam Deck)**: read-only root, no compiler by default; `pacman` needs `steamos-readonly disable`, which a launcher must not ask.
- **Windows**: no compiler by default (MSVC Build Tools is a 3+ GB download and a licence acceptance).

### Bundling candidates (sizes from the vendors' release pages, 2026-11; **no toolchain was downloaded or run in this session**, see "pending")

| Candidate | Download | Notes |
|---|---|---|
| Zig 0.17.1 (`zig c++` = clang + lld + libc++ + libc headers + cross targets) | **57.4 MB** (`x86_64-linux` tar.xz, sha256 3581863e...b17a), **100 MB** (`x86_64-windows` zip), 53 MB aarch64-linux | one pinned archive per OS, single directory, no installer; verified by SHA-256 published in `ziglang.org/download/index.json`. Uses libc++, so **the runtime libraries must be built with the same toolchain** (libstdc++ and libc++ objects do not mix). |
| llvm-mingw 20261117 (clang + lld + mingw-w64 for Windows) | 173-191 MB zip | Windows only; C++ ABI of MinGW. |
| Official LLVM release (`clang+llvm`) | 1.2 GB (zstd) - 2.0 GB (xz) Linux | far too big; includes everything. |
| System compiler | 0 | Linux only. |

### Recommendation (per platform; the open items are marked)

1. **Linux: the system compiler.** `tos-prepare` takes `--compiler`/`$CXX`/`clang++`/`g++`/`c++`, in that order, and says what to install when there is none (exit 7). Prefer clang: half the memory and faster. The package `Requires` clang or g++ and SDL3. *Implemented.*
2. **Windows and SteamOS: a pinned Zig, downloaded once by the launcher** (57-100 MB, SHA-256 checked, kept in the launcher's library), `--compiler "<zig>/zig c++"`. Rationale: one small archive per OS, same compiler everywhere, nothing to install system-wide. The runtime libraries are **built by us with Zig** and shipped in the toolkit (the runtime is ours, not game-derived), so one stdlib is in play. *Open, not implemented here: the Zig build of the runtime libraries and SDL3, linking SDL3 on Windows, an end-to-end test on Windows and SteamOS (no machine here).* Asking SteamOS players to install a compiler is not an option; Zig runs unpacked in their home folder.
3. **No prebuilt game executable and no prebuilt generated code**: both are game-derived and must not be distributed. Only the runtime libraries and the harness object may be prebuilt (the harness is compiled on the player's machine for now: it includes a generated header).
4. **Build speed for the first run**: with clang, -O1 takes about 80 s at -j4 on a fast desktop (253 CPU-seconds: about 4 minutes on a 4-core/8-thread Steam Deck; **[INFERENCE]**, not measured there). A progress bar covers this; an `-O0` first build in 10 s is an option if the speed is acceptable (pending measurement) with a later `-O1` rebuild in the background. The default stays `-O1` as today.

## 4. Memory and jobs

One compiler process needs 0.6 GB (clang) to 1.2 GB (gcc) at -O1 on the largest generated file. `--jobs` auto = `min(cores, MemAvailable / 0.8 GB)` for clang, `/1.5 GB` otherwise. The largest-first schedule keeps the 4 or 8 workers busy until the end.

## 5. Not in scope / limitations / follow-ups

- Windows packaging and macOS: `tos-prepare` uses POSIX (`posix_spawn`, process groups, `flock`, `/proc`). The platform parts are `process.cpp` and `common.cpp`; a Windows port needs job objects instead of process groups.
- Bundled toolchain download (Zig, SHA-256, cache in the launcher's library) and its integration in the launcher: not implemented.
- Launcher integration: the launcher's `prepare` command can be `tos-prepare {input:disc1} {input:disc2} --data {data} --cancel-file ...`; the `keeps_copy` rebuild is `tos-prepare --data {data} --resume` (no discs). Not wired here.
- `gcrecomp` looks game files up by their disc name and ignores `paths.tsv`; names that `common` has to rename on Windows would not be found (a `warn` is printed when any was renamed). On the TOS discs on Linux every path is unchanged.
- The harness `tos_boot` is the boot/debug harness (stops after N seconds, prints watchdog lines). A final game executable needs its own `main.cpp`.
- `common`'s `extract` always writes a whole disc, hence disc 2 is staged and merged (1.4 GB extra at the peak). A filter in `common` would avoid it.
- Disc 2 merged tree acceptance by the game: untested.

## 6. Pending (waiting for a free machine)

The end-to-end run on the owner's ISOs (title screen offscreen, `[gp]` frames, no unknown commands), the resume runs, the speed of -O0/-O1/-Og builds in the game, Zig compile time on the generated code and the unpacked size of the Zig toolchain, `--funcs-per-file` balance, and the symbol-change file count. This section is replaced by the numbers when they exist.
