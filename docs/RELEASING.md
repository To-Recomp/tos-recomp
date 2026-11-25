# Releasing

A release is what the [To-Recomp launcher](https://github.com/To-Recomp/launcher) installs for Tales of Symphonia. It contains no game data: the player's own discs are turned into a game on the player's machine by `tos-prepare` (`docs/PREPARE.md`).

## What a release contains

Pushing a tag `vX.Y.Z` (or `vX.Y.Z-rc.N`, published as a pre-release) runs `.github/workflows/release.yml`, which builds both archives and publishes them in **one** release (the launcher picks the `.tar.gz` or the `.zip` for its operating system):

| Asset | Content |
| --- | --- |
| `tos-recomp-<v>-linux-x86_64.tar.gz` | `tos-prepare` (one file: the program with everything it needs inside, see below), `tos-start`, `recomplauncher.json`, `LICENSE`, `THIRD-PARTY-NOTICES.txt`, `licenses/` |
| `tos-recomp-<v>-windows-x86_64.zip` | `tos-prepare.exe` (the same), `tos-start.exe`, `recomplauncher.json`, `LICENSE`, `THIRD-PARTY-NOTICES.txt`, `licenses/` |
| `SHA256SUMS` | checksum of both archives (the launcher also checks the SHA-256 GitHub keeps for each asset) |

`recomplauncher.json` (`packaging/`; `packaging/recomplauncher.windows.json` for the Windows archive: `.exe` names and `bin/tos_boot.exe`) asks the launcher for the two discs, runs `tos-prepare <disc1> <disc2> --data {data} --resume` as the `prepare` step and `tos-start` as `start`. `--resume` makes a rebuild after an update skip what is unchanged and, with `keeps_copy`, work from the files kept in `{data}/game` without the discs. The launcher needs no change: `program` is a list of names and the launcher takes the first one that exists in the archive.

### One file, nothing to install

The rule behind it: **nothing derived from a game is distributed** (no generated C++, no recompiled game executable: the legal risk is ours to avoid), and **the player installs nothing by hand**. So the game is still built on the player's machine from the player's own discs, and `tos-prepare` carries everything that building needs. The release's `tos-prepare` is the program (about 3 MB) followed by a compressed **payload** with the toolkit (the recompiler, the runtime libraries, SDL3, the headers, the boot harness source, the symbols, `dumps.toml`) and the pinned **Zig** compiler. On first use it unpacks the payload into a per-user cache folder, verified and atomically (`docs/PREPARE.md`, "The payload and the toolchain cache"); then it works as before. No system compiler, no SDL3 development package, no MSVC, no download: it works offline. This covers SteamOS/Steam Deck (read-only root, no compiler): the cache is under the user's home.

| | Linux | Windows |
| --- | --- | --- |
| Compiler | Zig, `-target x86_64-linux-gnu.2.31`: the game executable needs glibc 2.31 or later, whatever the distribution | Zig, `-target x86_64-windows-gnu` |
| C++ library | libc++ (static), built from Zig's sources on the first compile of an instance (about 7 s) | the same |
| SDL3 | built from source with Zig and linked statically; it loads X11, Wayland, KMS/DRM, ALSA, PulseAudio, OpenGL, udev and D-Bus with `dlopen` at run time if the system has them. The build machine needs their **headers**, the player's none | built from source, statically linked, Windows system libraries only |
| The game executable needs | `libc`, `libm`, `libdl`, `libpthread` (checked with `readelf` for every build: `tests/check-linux-game.sh`) | Windows' own DLLs |
| `tos-prepare` | built with GCC on Ubuntu 22.04, libstdc++, libgcc and the disc codecs linked statically: needs glibc 2.35 or later (Ubuntu 22.04, Debian 12, Fedora 36, SteamOS 3 ...) | built with MSVC, static runtime, disc codecs from vcpkg linked statically |
| Payload cache | `$TOS_RECOMP_CACHE`, `~/.cache/tos-recomp` | `%LOCALAPPDATA%\tos-recomp` |

`$CXX` is ignored by a release (its libraries are built with Zig's libc++; another compiler's C++ library does not mix): `--compiler` still works for developers.

- **Zig**: `packaging/zig.pin` holds the version, the download URLs and the SHA-256 of both hosts (the values of `https://ziglang.org/download/index.json`); `scripts/fetch-zig.sh` downloads one, falls back to the community mirrors on a slow download, and refuses a file whose SHA-256 differs from the pin; `--check-index` compares the pin with the live index (the release workflow uses it). Zig upgrades: change the pin from `index.json` and cut a release candidate; the smoke test tells whether the libraries still link.
- **Trimmed Zig**: Zig carries the C library sources and headers of every operating system it can target. `scripts/trim-zig.sh` removes, from the copy that goes into the payload, those of the other operating systems (musl, WASI, the BSDs, macOS and, per release, MinGW-w64 headers or glibc), plus documentation, the thread sanitizer runtime, lldb pretty printers and the init templates. Nothing else is touched. That it still compiles is proved by the smoke test, not assumed (it compiles the real boot harness and links the real libraries with the trimmed compiler).
- **Libraries**: the runtime libraries, SDL3 and the recompiler are built with the **same** Zig, so the player's compile and the libraries share one C++ library. `scripts/make-toolkit.sh --zig <zig> --target <triple>` does it (`packaging/zig-toolchain.cmake` is the CMake toolchain file); it works on Linux and Windows (Git Bash). The compile flags are `-O1 -g0`: without `-g0` Zig's clang writes debug information, which made the objects of the real game 6 times larger (94 MB instead of 15.7 MB for the biggest file) and the compiler 1.8 times hungrier (2.6 GB instead of 1.4 GB); `tos-prepare` also keeps the compiler processes within three quarters of the memory available (`MemoryGate`), because the biggest files are all at the front of the queue.
- **Adding the payload**: `scripts/package-release.sh` strips `tos-prepare` (Linux), then `tos-payload attach` appends the packed payload and the trailer (`prepare/tools/payload_tool.cpp`). Strip *before*: `strip` removes bytes after the last section. On Windows the signature, if one is added later, goes behind the payload and `tos-prepare.exe` finds the payload from the PE header. Compression is xz (LZMA2, preset 9 extreme, one stream, 64 MiB dictionary).
- **Licences**: `THIRD-PARTY-NOTICES.txt` and `licenses/` cover everything inside (Zig MIT, LLVM/libc++ Apache-2.0 with the LLVM exception, glibc headers (Linux) or mingw-w64 (Windows) as Zig ships them, SDL3 zlib, gcrecomp MIT, the disc codecs, our GPL-3.0). Combining MIT, zlib, BSD and Apache-2.0 components with a GPL-3.0 program in one executable file is allowed, and Zig is a separate program that `tos-prepare` starts, not a library it links; the sources are the pinned upstream releases and our repositories. **This is the maintainers' reading, not legal advice.**

### What CI proves, and on what

For every build, pull requests too (`.github/workflows/release.yml`):

- **Linux**: the archive is built on `ubuntu-22.04` with the packages of `packaging/linux-build-deps.txt`; `tos-prepare` has no libz, libbz2, liblzma, libzstd or libstdc++ dependency; the archive holds one `tos-prepare` and no `toolkit/` or `toolchain/` folder; nothing game-derived is in it. Then `tests/release-smoke.sh` runs the **real, single-file** `tos-prepare`, mounted read-only, in plain `ubuntu:22.04` and `debian:stable-slim` containers started with `--network none` and holding **no** C or C++ compiler (the script checks `cc c++ gcc g++ clang clang++ zig tcc` are not on PATH), on two **synthetic** disc images (`tos-synth-discs`, `prepare/tests/synth_discs.cpp`; the `main.dol` has four instructions): first run unpacks the payload into `$HOME/.cache/tos-recomp`, verifies the discs, extracts, runs the real recompiler, compiles the generated code and the real boot harness with the Zig from the payload, links against the shipped libraries and SDL3, and the game executable starts, runs the synthetic program and exits 0; `ldd` shows only the C library; a second run (`--resume`) skips every stage; deleting the cache makes the next run unpack again; deleting one file of the cache makes the next run repair it. On the build machine `tests/check-linux-game.sh` checks with `readelf` that the executable asks for no library beyond the C library and for no glibc symbol newer than 2.31.
- **Windows**: the same on a `windows-latest` runner, with the runner's compilers removed from PATH (Git Bash's own tools only), unpacking into `%LOCALAPPDATA%`: `tos-prepare.exe` from the unpacked zip, no toolkit or toolchain folder beside it, MSVC-built, static runtime, no DLL beyond the system's.
- Offline operation is proved on Linux (`--network none`). The Windows runner is online; nothing in the payload code or in Zig's use here opens a connection (**[INFERENCE]**, not observed).

**Not proved by it**: that the real game (a 2.9 MB DOL: 98 MB of generated C++) compiles within the time and memory expected on every machine (it was compiled on the development machine only: see the numbers below), and that it plays: OpenGL 3.3 window through SDL3, audio, keyboard and pads, frame rate. Nobody has run the Zig-built game on a Steam Deck or on real Windows hardware; an old distribution whose glibc is below 2.35 cannot run `tos-prepare` itself (**not tried**).

Unverified (the first player or the owner will find out):

- the real game from the player's discs through the launcher, on Linux with the Zig-built libc++ executable, and on Windows (time, memory, `--jobs` default; whether a 512 MB stack reservation is enough on Windows; the unlimited main stack on Linux is `tos-start`'s `ulimit -s unlimited`);
- the disc 2 path on the real discs (the smoke test merges the files of a synthetic disc 2, not the real 1,667 files, nor does it show that the game accepts the merged tree);
- the launcher unpacking the archive and starting `tos-prepare` and `tos-start`, and whether a console window shows on Windows for these programs;
- a virus scanner on Windows that holds the freshly unpacked files: the final rename is retried for five seconds, nothing else is done about it (**not tried**);
- paths: the longest file in the Windows payload is 104 characters below its folder; `%LOCALAPPDATA%\tos-recomp\<16 hex>\` adds about 60 more to a normal user's profile path, a data folder deep enough to push the compile command lines over 260 characters is untested;
- older Windows versions and Windows on ARM (x64 emulation): not tried.

### Sizes and times

Measured on the development machine (Ryzen 7 9800X3D, 16 threads, NVMe, a desktop in use; Docker with 4 CPUs for the builds) with the Linux archive built by `scripts/docker-release.sh` from this change (Zig 0.17.1), the first release candidate that carries a payload being not yet published:

| | Before (rc.5, system compiler) | Self-contained |
| --- | --- | --- |
| Linux archive (`.tar.gz`) | 1.6 MB | **53.3 MB** (`tos-prepare` 54.7 MB: 2.5 MB program + 52.3 MB payload) |
| Windows archive (`.zip`) | 111 MB (379 MB unpacked: Zig and toolkit next to the program) | **not measured here** (no Windows machine): about 100 MB for Zig's tree, trimmed and xz-packed it should be a little over 50 MB **[INFERENCE]**, CI prints the real size in the job summary |
| Payload, unpacked (Linux) | | 299.8 MB: Zig trimmed 254 MB (407 MB before trimming) + toolkit 49 MB; 304 MB on disk |
| First-run unpack (Linux, `tos-prepare` log) | | 2.3 s, peak memory of the process 74 MB (warm page cache, NVMe; a slow SD card will be bound by writing 300 MB: **not measured**) |
| Later runs | | 0 s: one `stat` per file of the cache |
| Zig's libc++ for the target | | built by Zig on the first compile of an instance, 7 s |
| Synthetic game, whole first run in a container, 2 jobs | | 12-13 s (unpack 2.3 s, compile stage 9.6 s) |
| Synthetic game, instance on disk | | 123 MB (the synthetic discs' copy included) |
| The real game with the shipped compiler and toolkit (Linux target, `-O1 -g0`) | | all 40 generated files and the harness compiled with 3 jobs in 185 s wall (`nice -n 19`, machine in use), biggest file 57 s and 1.5 GB; objects 132 MB; link 7 s; executable 103 MB, `ldd`: libc, libm, libdl, libpthread only, newest glibc symbol 2.30. The game was **not run** (the discs' files are the owner's and the display was in use) |

The disk a player needs: the cache (about 300 MB, shared by every instance of the same release and removed by the next release) plus the instance (about 2.5 GB, as before).

Why xz: on Zig's Linux tree (390 MB as a tar) `xz -9e` gives 56.8 MB, `zstd -19 --long=27` 61.9 MB and `zstd -22 --long=27` 60.9 MB, so xz is 7-8% smaller; zstd would unpack faster, which does not matter next to writing the files.


## Cutting a release

1. Merge what must be in it. Don't tag for docs-only changes: each release makes every player build the game again.
2. `git tag -s vX.Y.Z && git push origin vX.Y.Z` (use `-rc.N` for a test build; a pre-release is not what the launcher installs, except when the repository has only pre-releases).
3. Check the run, then the release page: both archives and `SHA256SUMS` must be there. The `publish` job runs only when the Linux and the Windows builds both passed.

The workflow builds against the default branches of `To-Recomp/common` and `To-Recomp/gcrecomp`; the revisions used are recorded in `toolkit/toolkit.json` (`versions`).

A pull request that touches the packaging builds the archives without publishing them. Locally:

```
# Linux archive and its smoke test, exactly as CI does it (Docker or Podman, network for the downloads only)
scripts/docker-release.sh --out /var/tmp/tos-rel --version 0.0.0-local

# Linux archive without Docker (needs g++, cmake, ninja, the static codec libraries and the SDL3 build headers: packaging/linux-build-deps.txt)
scripts/fetch-zig.sh --host x86_64-linux --out /var/tmp/zig-linux
scripts/package-release.sh --version 0.0.0-local --out /var/tmp/tos-rel --zig-dist /var/tmp/zig-linux/zig-x86_64-linux-0.17.1 --portable

# Windows archive, on Windows (Git Bash; tos-prepare.exe and tos-payload.exe built as in release.yml)
scripts/fetch-zig.sh --host x86_64-windows --out /var/tmp/zig-win
scripts/package-release.sh --platform windows --version 0.0.0-local --out /var/tmp/tos-rel \
    --zig-dist /var/tmp/zig-win/zig-x86_64-windows-0.17.1 --prepare-exe path/to/tos-prepare.exe --payload-tool path/to/tos-payload.exe

# Check an unpacked archive without a disc, on a machine that has no compiler (or --scrub-path to hide them), see tests/release-smoke.sh
tests/release-smoke.sh --release <unpacked archive> --synth build-prepare/tos-synth-discs --no-compiler
```

What is inside a payload, and unpacking one by hand: `tos-payload info tos-prepare`, `tos-payload extract tos-prepare --out DIR`.
