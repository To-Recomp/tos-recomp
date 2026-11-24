# tos-recomp

to-recomp/tos-recomp

Static recompilation of Tales of Symphonia (GameCube, 2003), the pilot project. Reference dump: PAL French, game ID `GQSF` (two discs). Other regions and platforms are tracked in `dumps.toml` once it exists. The game is prepared once from your own dump, then runs without it.

Status: M0, repository skeleton only.

## Platforms

Releases (`docs/RELEASING.md`) are archives for the [To-Recomp launcher](https://github.com/To-Recomp/launcher), for **Linux x86_64** (needs a C++ compiler and the SDL3 development files) and **Windows x86_64** (nothing to install: the archive carries its own compiler, Zig). The player's own discs are turned into the game on the player's machine by `tos-prepare` (`docs/PREPARE.md`). The Windows build is proved by CI on a Windows runner up to a synthetic game that links and runs; the real game has not been run on real Windows hardware yet.

Not affiliated with or endorsed by Bandai Namco Entertainment, Sony Interactive Entertainment, Nintendo or any game publisher or developer. This project contains no game files; you bring your own copy of the game.

## AI assistance

This project is developed with the help of AI coding assistants (large language models). A large part of the code, tests, documentation and issues is written by AI agents; the maintainer directs the work and reviews it. The result can still contain mistakes: check what matters to you before relying on it. Generated code is under the same licence as the rest of the repository.

License: GPL v3 (`LICENSE`). Contributions: `CONTRIBUTING.md`.
