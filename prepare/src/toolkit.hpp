// SPDX-License-Identifier: GPL-3.0-or-later
// The toolkit: everything tos-prepare needs that is ours and not derived from the game, found next to the
// program (or given with --toolkit): the recompiler, the runtime libraries, the headers the generated
// code includes, the boot harness source, the symbol files and dumps.toml. Described by toolkit.json.
// scripts/make-toolkit.sh builds one from the gcrecomp and tos-recomp checkouts.
#pragma once

#include <string>
#include <vector>

#include "common.hpp"
#include "json.hpp"

namespace prep {

struct Toolkit {
  fs::path dir;
  Json json;

  std::string game_id;
  std::string project;
  fs::path dumps;
  fs::path recompiler;
  fs::path symbols_csv;
  fs::path symbols_extra;
  std::vector<fs::path> include_dirs;
  std::vector<fs::path> sources;     // harness sources compiled with the generated code
  std::vector<fs::path> libs;        // static libraries, in link order
  std::vector<std::string> system_libs;
  std::vector<std::string> flags;
  std::string std_flag;
  std::vector<std::string> compilers;  // candidates, in order of preference
  std::string executable;

  // Loads <dir>/toolkit.json and checks that every file it names exists; throws Error(kNoToolchain).
  // `symbols_dir` / `dumps_override`, when not empty, replace the toolkit's own copies.
  static Toolkit load(const fs::path& dir, const fs::path& symbols_dir, const fs::path& dumps_override);

  // Hash of toolkit.json, the libraries, the harness sources and every header: what a compiled object
  // depends on besides the generated code and the compiler.
  std::string content_hash() const;
};

// Compiler command: --compiler, else $CXX, else the first candidate found on PATH. Returns argv
// (a command such as "zig c++" has several words); throws Error(kNoToolchain).
std::vector<std::string> resolve_compiler(const Toolkit& tk, const std::string& requested);

}  // namespace prep
