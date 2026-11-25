# tos-recomp

to-recomp/tos-recomp

Static recompilation of Tales of Symphonia (GameCube, 2003), the pilot project. Reference dump: PAL French, game ID `GQSF` (two discs). Other regions and platforms are tracked in `dumps.toml` once it exists. The game is prepared once from your own dump, then runs without it.

Status: M0, repository skeleton only.

## Platforms

Releases (`docs/RELEASING.md`) are archives for the [To-Recomp launcher](https://github.com/To-Recomp/launcher), for **Linux x86_64** (SteamOS included) and **Windows x86_64**, and the player installs **nothing**: no C++ compiler, no SDL3 development files, no Visual Studio, no download. The one file `tos-prepare` carries the recompiler, a pinned C++ compiler (Zig), the runtime libraries, SDL3 and everything else the build needs, and unpacks them once into the user's cache folder. The player's own discs are turned into the game on the player's machine by it (`docs/PREPARE.md`); **nothing derived from a game is distributed**, no generated code and no game executable. CI proves, on a machine without a compiler and without network, that the single file prepares synthetic discs into a game executable that runs (Linux, in containers; Windows, on a runner with its compilers removed from PATH); the real game has not been run on real Windows hardware or a Steam Deck yet.

Not affiliated with or endorsed by Bandai Namco Entertainment, Sony Interactive Entertainment, Nintendo or any game publisher or developer. This project contains no game files; you bring your own copy of the game.

## AI assistance

This project is developed with the help of AI coding assistants (large language models). A large part of the code, tests, documentation and issues is written by AI agents; the maintainer directs the work and reviews it. The result can still contain mistakes: check what matters to you before relying on it. Generated code is under the same licence as the rest of the repository.

License: GPL v3 (`LICENSE`). Contributions: `CONTRIBUTING.md`.
