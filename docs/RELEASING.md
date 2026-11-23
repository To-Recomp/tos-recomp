# Releasing

A release is what the [To-Recomp launcher](https://github.com/To-Recomp/launcher) installs for Tales of Symphonia. It contains no game data: the player's own discs are turned into a game on the player's machine by `tos-prepare` (`docs/PREPARE.md`).

## What a release contains

Pushing a tag `vX.Y.Z` (or `vX.Y.Z-rc.N`, published as a pre-release) runs `.github/workflows/release.yml`, which publishes:

| Asset | Content |
| --- | --- |
| `tos-recomp-<v>-linux-x86_64.tar.gz` | `tos-prepare`, `toolkit/` (recompiler, runtime libraries, headers, harness source, `dumps.toml`, symbols), `tos-start`, `recomplauncher.json`, `LICENSE` |
| `SHA256SUMS` | checksum of the archive (the launcher also checks the SHA-256 GitHub keeps for it) |

`recomplauncher.json` (`packaging/`) asks the launcher for the two discs, runs `tos-prepare <disc1> <disc2> --data {data} --resume` as the `prepare` step and `tos-start` as `start`. `--resume` makes a rebuild after an update skip what is unchanged and, with `keeps_copy`, work from the files kept in `{data}/game` without the discs.

Players need a C++ compiler (clang preferred) and the SDL3 development files, and about 2.5 GB free. Linux only for now (Windows and SteamOS: `docs/PREPARE.md`, section 3).

## Cutting a release

1. Merge what must be in it. Don't tag for docs-only changes: each release makes every player build the game again.
2. `git tag -s vX.Y.Z && git push origin vX.Y.Z` (use `-rc.N` for a test build; a pre-release is not what the launcher installs, except when the repository has only pre-releases).
3. Check the run, then the release page.

The workflow builds against the default branches of `To-Recomp/common` and `To-Recomp/gcrecomp`; the revisions used are recorded in `toolkit/toolkit.json` (`versions`).

A pull request that touches the packaging builds the archive without publishing it. Locally:

```
scripts/package-release.sh --version 0.0.0-local --out /var/tmp/tos-rel
```
