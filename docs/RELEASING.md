# Releasing

A release is what the [To-Recomp launcher](https://github.com/To-Recomp/launcher) installs for Tales of Symphonia. It contains no game data: the player's own discs are turned into a game on the player's machine by `tos-prepare` (`docs/PREPARE.md`).

## What a release contains

Pushing a tag `vX.Y.Z` (or `vX.Y.Z-rc.N`, published as a pre-release) runs `.github/workflows/release.yml`, which builds both archives and publishes them in **one** release (the launcher picks the `.tar.gz` or the `.zip` for its operating system):

| Asset | Content |
| --- | --- |
| `tos-recomp-<v>-linux-x86_64.tar.gz` | `tos-prepare`, `toolkit/` (recompiler, runtime libraries, headers, harness source, `dumps.toml`, symbols), `tos-start`, `recomplauncher.json`, `LICENSE` |
| `tos-recomp-<v>-windows-x86_64.zip` | `tos-prepare.exe`, `tos-start.exe`, `toolkit/` (the same, built with Zig for Windows, with SDL3 inside), `toolchain/zig/` (the C++ compiler, with its `LICENSE`), `recomplauncher.json`, `LICENSE`, `THIRD-PARTY-NOTICES.txt`, `licenses/` |
| `SHA256SUMS` | checksum of both archives (the launcher also checks the SHA-256 GitHub keeps for each asset) |

`recomplauncher.json` (`packaging/`; `packaging/recomplauncher.windows.json` for the Windows archive: `.exe` names and `bin/tos_boot.exe`) asks the launcher for the two discs, runs `tos-prepare <disc1> <disc2> --data {data} --resume` as the `prepare` step and `tos-start` as `start`. `--resume` makes a rebuild after an update skip what is unchanged and, with `keeps_copy`, work from the files kept in `{data}/game` without the discs. The launcher needs no change for Windows: `program` is a list of names and the launcher takes the first one that exists in the archive.

### Linux

Players need a C++ compiler (clang preferred) and the SDL3 development files, and about 2.5 GB free.

### Windows

Players need nothing installed and no administrator rights; about 2.5 GB free for the game plus the archive (about 110 MB, 440 MB unpacked, most of it Zig).

- **Compiler**: a pinned [Zig](https://ziglang.org/) (MIT) is unpacked inside the archive as `toolchain/zig/` and is what `tos-prepare.exe` runs (`zig c++ -target x86_64-windows-gnu`: clang, lld and libc++). It writes its cache into the instance (`<data>\build\zig-cache`), so removing the instance removes everything. `packaging/windows/zig.pin` holds the version, download URLs and SHA-256 (the values of `https://ziglang.org/download/index.json`); `scripts/fetch-zig.sh` downloads it, falls back to the community mirrors on a slow download, and refuses a file whose SHA-256 differs from the pin; `--check-index` compares the pin with the live index (the release workflow uses it).
- **Libraries**: the runtime libraries, SDL3 (built from source, statically linked) and the recompiler are built with the **same** Zig, so the player's compile and the libraries share one C++ library. `scripts/make-toolkit.sh --windows --zig <zig>` does it and works on Windows (Git Bash) and on Linux (cross-compiling). `packaging/windows/zig-toolchain.cmake` is the CMake toolchain file.
- **`tos-prepare.exe`** is built with MSVC (static runtime) with the disc codecs (zlib, bzip2, liblzma, zstd) linked in from vcpkg's `x64-windows-static`, so it needs no DLL. Its licence texts go to `licenses/`.
- **`tos-start.exe`** (`packaging/windows/tos-start.cpp`, built with the same Zig) is the counterpart of the `tos-start` shell script.
- **Zig upgrades**: change `packaging/windows/zig.pin` from `index.json` and cut a release candidate; the smoke test below tells whether the libraries still link.

What the workflow proves on a `windows-latest` runner for every build (also in pull requests): the archive unpacks; `tos-prepare.exe --version` and `--check` run; it depends on no codec or Visual C++ runtime DLL; nothing game-derived is inside; and `tests/release-smoke.sh` runs the **real** `tos-prepare.exe` from the unpacked archive on two **synthetic** disc images (`tos-synth-discs`, `prepare/tests/synth_discs.cpp`; the main.dol has four instructions): verify, extract, the real recompiler, compile of the generated code and of the real boot harness with the bundled Zig, link against the shipped libraries, then it starts the resulting game executable, which runs the synthetic program and exits. **Not proved by it**: that the real game (a 2.9 MB DOL, 1,667 files) runs on real Windows hardware (graphics, audio, input, speed, the 512 MB stack reservation being enough), the disc 2 path of the real discs, and the build time and memory on a player's machine.

## Cutting a release

1. Merge what must be in it. Don't tag for docs-only changes: each release makes every player build the game again.
2. `git tag -s vX.Y.Z && git push origin vX.Y.Z` (use `-rc.N` for a test build; a pre-release is not what the launcher installs, except when the repository has only pre-releases).
3. Check the run, then the release page: both archives and `SHA256SUMS` must be there. The `publish` job runs only when the Linux and the Windows builds both passed.

The workflow builds against the default branches of `To-Recomp/common` and `To-Recomp/gcrecomp`; the revisions used are recorded in `toolkit/toolkit.json` (`versions`).

A pull request that touches the packaging builds the archives without publishing them. Locally:

```
# Linux
scripts/package-release.sh --version 0.0.0-local --out /var/tmp/tos-rel

# Windows archive, on Windows (Git Bash; tos-prepare.exe built as in release.yml) or cross-built on Linux with a Linux Zig
scripts/fetch-zig.sh --host x86_64-windows --out /var/tmp/zig-win
scripts/package-release.sh --platform windows --version 0.0.0-local --out /var/tmp/tos-rel \
    --zig-dist /var/tmp/zig-win/zig-x86_64-windows-0.17.1 --prepare-exe path/to/tos-prepare.exe [--zig path/to/linux/zig]

# Check an unpacked archive without a disc (needs the synthetic disc tool: cmake --build build-prepare --target tos-synth-discs)
tests/release-smoke.sh --release <unpacked archive> --synth build-prepare/tos-synth-discs
```

On the development machine Zig 0.17.1 failed under Wine (its build cache, "manifest_create"), so Wine was only used for the recompiler and the game executable; the compile of the smoke test needs real Windows or a Linux Zig (`--compiler`, `--runner wine` for the rest).
