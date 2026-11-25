// SPDX-License-Identifier: GPL-3.0-or-later
// The five stages of the Tales of Symphonia pipeline and the instance folder layout (docs/PREPARE.md).
#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "pipeline.hpp"
#include "process.hpp"
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

// The stages make_stages builds; main announces the toolchain unpack as "1/(kStageCount + 1)" before they exist.
constexpr int kStageCount = 5;

// Everything the stages share and that is decided once per run.
struct Plan {
  Toolkit toolkit;
  std::vector<std::string> compiler;  // argv prefix of the C++ compiler (empty until the compile stage needs it)
  RunOptions compiler_run;            // environment the compiler is started with (a bundled Zig keeps its cache in the instance)
};

// Builds the pipeline for `ctx` (the toolkit must be loaded). Compiler resolution is deferred to the
// compile stage so that verify/extract/recompile do not need one.
std::vector<std::unique_ptr<Stage>> make_stages(Context& ctx, std::shared_ptr<Plan> plan);

// Peak memory of one compiler process on a generated file of `source_bytes`, in MB: measured on the real game's files
// (10.6 MB source: 1.4 GB with Zig's clang -g0, 0.63 GB with clang, 1.2 GB with g++; a 4 MB file needs about a third).
std::uint64_t estimate_compile_mb(const std::vector<std::string>& compiler_argv, std::uint64_t source_bytes);

// Keeps the compiler processes of a run inside a memory budget: a process starts when the estimates of those running
// plus its own fit, or when none is running (a file bigger than the budget still gets compiled, alone). The jobs
// count alone cannot do this: the largest files are all at the front of the queue.
class MemoryGate {
 public:
  explicit MemoryGate(std::uint64_t budget_mb) : budget_(budget_mb) {}
  // Waits for room; false when `stop` became true meanwhile (nothing is held then).
  bool acquire(std::uint64_t mb, const std::function<bool()>& stop);
  void release(std::uint64_t mb);

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::uint64_t budget_, used_ = 0;
  unsigned running_ = 0;
};

// Number of parallel compiler jobs for a machine: min(cores, available RAM / memory per compiler).
unsigned default_jobs(const std::vector<std::string>& compiler_argv);

// --check: for every stage "up-to-date" or "stale" without changing anything. Returns true when all
// stages are up to date.
bool check_stages(Context& ctx, std::vector<std::unique_ptr<Stage>>& stages);

}  // namespace prep
