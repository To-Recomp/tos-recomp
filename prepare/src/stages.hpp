// SPDX-License-Identifier: GPL-3.0-or-later
// The five stages of the Tales of Symphonia pipeline and the instance folder layout (docs/PREPARE.md).
#pragma once

#include <memory>
#include <vector>

#include "pipeline.hpp"
#include "toolkit.hpp"

namespace prep {

// Instance folder ("{data}") layout; every name is relative to the data folder.
namespace layout {
inline const char* kGame = "game";                  // extracted game files, kept: a rebuild needs no disc
inline const char* kGamePartial = "game.partial";   // being written; renamed to game/ when complete
inline const char* kWork = "work";                  // staging for disc 2; removed after the merge
inline const char* kGenerated = "generated";        // recompiler output: a cache, deletable
inline const char* kGeneratedPartial = "generated.partial";
inline const char* kBuild = "build";                // build/obj/<key>.o object cache, build/tmp
inline const char* kBin = "bin";                    // the game executable
inline const char* kSaves = "saves";                // memory card images
inline const char* kConfig = "config";
inline const char* kLogs = "logs";                  // logs/prepare.log
}  // namespace layout

// Everything the stages share and that is decided once per run.
struct Plan {
  Toolkit toolkit;
  std::vector<std::string> compiler;  // argv prefix of the C++ compiler (empty until the compile stage needs it)
};

// Builds the pipeline for `ctx` (the toolkit must be loaded). Compiler resolution is deferred to the
// compile stage so that verify/extract/recompile do not need one.
std::vector<std::unique_ptr<Stage>> make_stages(Context& ctx, std::shared_ptr<Plan> plan);

// Number of parallel compiler jobs for a machine: min(cores, available RAM / memory per compiler).
unsigned default_jobs(const std::vector<std::string>& compiler_argv);

// --check: for every stage "up-to-date" or "stale" without changing anything. Returns true when all
// stages are up to date.
bool check_stages(Context& ctx, std::vector<std::unique_ptr<Stage>>& stages);

}  // namespace prep
