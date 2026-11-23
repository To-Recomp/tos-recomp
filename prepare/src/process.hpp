// SPDX-License-Identifier: GPL-3.0-or-later
// Running child processes: each in its own process group, output merged and read line by line, killed
// (the whole group) when cancellation is requested.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"

namespace prep {

struct RunResult {
  int exit_code = -1;      // exit status, or 128 + signal when killed by a signal
  bool started = false;
  double seconds = 0;
  long max_rss_kb = 0;
  std::vector<std::string> tail;  // the last lines of output (for error messages)
};

struct RunOptions {
  std::vector<std::pair<std::string, std::string>> env;  // added to the environment
  std::size_t tail_lines = 20;
};

// Throws Cancelled after killing the group when `cancel` fires, Error(kNoToolchain) when the program
// cannot be started. A non-zero exit is reported in the result, not thrown.
RunResult run_process(const std::vector<std::string>& argv, const RunOptions& options, CancelToken& cancel,
                      const std::function<void(const std::string&)>& on_line);

// Splits a command such as "zig c++" on spaces (no quoting).
std::vector<std::string> split_command(const std::string& command);

// Finds `name` on PATH (or returns it when it contains a slash and is executable); empty when absent.
std::string find_in_path(const std::string& name);

}  // namespace prep
