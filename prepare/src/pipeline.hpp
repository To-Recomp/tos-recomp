// SPDX-License-Identifier: GPL-3.0-or-later
// The generic part of tos-prepare: stages, the recorded state (prepare.json) and the runner that orders
// them, skips finished ones (--resume), checks free space and turns failures into exit codes. Nothing
// here knows about discs or compilers, so the tests drive it with fake stages.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "json.hpp"
#include "reporter.hpp"

namespace prep {

struct StageRecord {
  std::string status;    // "running", "done", "failed", "cancelled"
  std::string input;     // hash of everything the stage's result depends on
  std::string finished;  // UTC time
  double seconds = 0;
  Json info = Json::object();  // what the stage wants later stages (and the launcher) to know
};

// prepare.json: the instance's manifest. Written after every stage, so an interrupted run leaves a
// truthful record; "state" is "ready" only after the last stage.
class State {
 public:
  static State load(const fs::path& file);  // missing or unreadable file: empty state
  void save() const;                        // atomic
  void set_file(fs::path f) { file_ = std::move(f); }

  StageRecord* find(const std::string& stage);
  StageRecord& at(const std::string& stage) { return stages_[stage]; }
  const std::map<std::string, StageRecord>& stages() const { return stages_; }
  void erase_from(const std::vector<std::string>& stages);  // forget the records of these stages

  std::string overall = "new";  // new, preparing, ready, failed, cancelled
  Json meta = Json::object();   // other top-level members: game_id, versions, discs, run, ...
  std::string tool_version;

 private:
  fs::path file_;
  std::map<std::string, StageRecord> stages_;
};

struct Options {
  std::vector<fs::path> discs;  // empty: reuse what a previous run kept (needs --resume)
  fs::path data;
  unsigned jobs = 1;
  std::string compiler;         // empty: found by the toolkit's candidate list
  bool resume = false;
  fs::path cancel_file;
  fs::path toolkit;
  fs::path symbols;             // override of the toolkit's symbols folder
  fs::path dumps;               // override of the toolkit's dumps.toml
  std::uint64_t reserve_bytes = 256ull * 1000 * 1000;  // kept free on top of a stage's need
};

struct Context;

class Stage {
 public:
  virtual ~Stage() = default;
  virtual std::string name() const = 0;
  virtual std::string title() const = 0;
  virtual double weight() const = 0;  // share of the overall progress, relative to the other stages
  // Hash of everything this stage's result depends on. Called when the previous stages are done (it
  // may read their recorded info). Equal hash + intact outputs = the stage is skipped on --resume.
  virtual std::string input_hash(Context& ctx) = 0;
  // True when the outputs of a finished stage are still there (cheap check, no hashing of big files).
  virtual bool outputs_intact(Context& ctx) = 0;
  // Free space the stage needs on the data volume (0: none).
  virtual std::uint64_t bytes_needed(Context& ctx) = 0;
  virtual void run(Context& ctx) = 0;
  // Removes what a cancelled or failed run left half-written.
  virtual void clean_partial(Context& ctx) = 0;
};

struct Context {
  Options opt;
  Reporter& rep;
  CancelToken& cancel;
  State state;
  Json toolkit = Json::object();  // toolkit.json (see toolkit.hpp); empty in generic tests
  fs::path toolkit_dir;
  std::function<std::uint64_t(const fs::path&)> free_space = [](const fs::path& p) { return free_bytes(p); };

  Context(Options o, Reporter& r, CancelToken& c) : opt(std::move(o)), rep(r), cancel(c) {}
  fs::path data() const { return opt.data; }
  fs::path state_file() const { return opt.data / "prepare.json"; }
};

// Runs the stages in order. Returns the process exit code and prints the final `result` line.
int run_pipeline(Context& ctx, std::vector<std::unique_ptr<Stage>>& stages, const std::string& tool_version);

}  // namespace prep
