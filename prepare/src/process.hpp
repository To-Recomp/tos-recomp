// SPDX-License-Identifier: GPL-3.0-or-later
// Running child processes: each in its own process group (a Job Object on Windows), output merged and read
// line by line, killed (the whole tree) when cancellation is requested. process_posix.cpp and
// process_win32.cpp implement run_process and find_in_path.
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
  bool discard_stderr = false;  // standard error goes nowhere instead of being merged into the output lines
};

// The first line of text is the program, found like a shell does (PATH; on Windows also with an .exe suffix).
// Throws Cancelled after killing the group when `cancel` fires, Error(kNoToolchain) when the program
// cannot be started. A non-zero exit is reported in the result, not thrown.
RunResult run_process(const std::vector<std::string>& argv, const RunOptions& options, CancelToken& cancel,
                      const std::function<void(const std::string&)>& on_line);

// Splits a command such as "zig c++" on spaces (no quoting).
std::vector<std::string> split_command(const std::string& command);

// Finds `name` on PATH (or returns it when it contains a path separator and is executable; on Windows a
// name without extension also matches name.exe); empty when absent. The result is UTF-8.
std::string find_in_path(const std::string& name);

}  // namespace prep
