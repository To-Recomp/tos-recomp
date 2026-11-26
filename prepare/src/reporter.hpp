// SPDX-License-Identifier: GPL-3.0-or-later
// The machine-readable progress protocol (stdout) and the log file.
//
// Every stdout line is one of (fields separated by single spaces, text runs to the end of the line):
//   protocol 1 tos-prepare <version>
//   stage-start <stage> <index>/<count> <title>
//   stage-progress <stage> <done>/<total> <text>
//   progress <fraction 0..1> <text>               overall progress, the launcher's own format
//   stage-end <stage> <ok|skipped|failed|cancelled> <seconds>
//   warn <text>
//   result <ready|failed|cancelled> <exit code> <text>
// Everything else (child process output, details) goes to the log file only.
#pragma once

#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

#include "common.hpp"

namespace prep {

class Reporter {
 public:
  explicit Reporter(std::FILE* out) : out_(out) {}

  // Opens (appending) the log; rotates a log over 4 MB to <name>.1 first.
  void open_log(const fs::path& file);
  void protocol(const std::string& version);
  void set_stage(const std::string& stage, int index, int count, const std::string& title, double base, double span);
  void stage_progress(std::uint64_t done, std::uint64_t total, const std::string& text);
  void stage_end(const std::string& status, double seconds);
  void warn(const std::string& text);
  // `release <id> <status> <title>`: which release the discs were found to be (additive; the launcher ignores it).
  void release(const std::string& id, const std::string& status, const std::string& title);
  void result(const std::string& state, int code, const std::string& text);
  void log(const std::string& text);  // log file only
  const std::string& stage() const { return stage_; }

 private:
  void line(const std::string& text);  // stdout + log, flushed
  void log_locked(const std::string& text);

  std::FILE* out_;
  std::ofstream log_;
  std::mutex mu_;
  std::string stage_;
  double base_ = 0, span_ = 0;
  long bucket_ = -1;
};

}  // namespace prep
