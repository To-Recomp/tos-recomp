// SPDX-License-Identifier: GPL-3.0-or-later
// Exit codes, errors, cancellation and small helpers shared by every part of tos-prepare.
#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace prep {

namespace fs = std::filesystem;

// Stable exit codes (documented in docs/PREPARE.md; the launcher maps them to messages).
enum ExitCode : int {
  kOk = 0,
  kFailed = 1,           // unexpected error (I/O, bug)
  kUsage = 2,            // bad command line
  kBadDump = 3,          // a disc is missing, unreadable, not the right game, or not a known dump
  kNoSpace = 4,          // not enough free disk space
  kCompileFailed = 5,    // the C++ compiler or linker failed
  kRecompileFailed = 6,  // the recompiler failed
  kNoToolchain = 7,      // toolkit or C++ compiler not found / unusable
  kExtractFailed = 8,    // reading the image or writing the game files failed
  kBusy = 9,             // another tos-prepare is working on the same data folder
  kStale = 10,          // --check: the instance needs a rebuild
  kCancelled = 130,      // cancelled by signal or cancel file (128 + SIGINT)
};

// A failure with the exit code to report.
class Error : public std::runtime_error {
 public:
  Error(int code, const std::string& message) : std::runtime_error(message), code_(code) {}
  int code() const { return code_; }

 private:
  int code_;
};

// Thrown when cancellation is noticed; never a failure.
class Cancelled : public std::exception {
 public:
  const char* what() const noexcept override { return "cancelled"; }
};

// Cancellation: SIGINT/SIGTERM (via install_signal_handlers) or the existence of a file. Long operations
// call poll() / check() often; process.cpp kills child process groups when it sees it.
class CancelToken {
 public:
  void request() { flag_.store(true); }
  void set_file(fs::path file) { file_ = std::move(file); }
  bool poll();           // true once cancellation was requested (signal, request() or the cancel file)
  void check() { if (poll()) throw Cancelled(); }

 private:
  std::atomic<bool> flag_{false};
  fs::path file_;
};

// Routes SIGINT and SIGTERM to `token` (one token per process).
void install_signal_handlers(CancelToken& token);

// Paths and text cross process boundaries as UTF-8 (protocol lines, child command lines, toolkit.json).
// On POSIX these are the identity; on Windows, fs::path::string() would use the ANSI code page.
std::string u8(const fs::path& p);
fs::path from_u8(const std::string& utf8);

// The running program's own file (empty when it cannot be found).
fs::path self_exe();
// Memory the machine can give to compiler processes now, in KiB; 0 when unknown.
std::uint64_t available_memory_kb();
// The highest resident memory this process has used so far, in KiB; 0 when unknown (measurements for the log).
std::uint64_t peak_memory_kb();
// Takes an exclusive lock on `file` (created if needed) for the lifetime of the process; false when another
// process holds it. The lock goes away with the process, whatever the way it ends.
bool lock_for_process(const fs::path& file);
// An advisory lock on a file, released by the destructor or when the process ends, however it ends. Shared locks
// coexist with each other, an exclusive lock with nothing. Used for the toolchain cache (toolchain.hpp): whoever
// uses an unpacked toolchain holds it shared; whoever unpacks or removes one holds it exclusive.
class FileLock {
 public:
  ~FileLock();
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;

  // nullptr when the lock is held by someone else (or the file cannot be opened).
  static std::unique_ptr<FileLock> try_acquire(const fs::path& file, bool exclusive);
  // Waits, polling every 100 ms, until the lock is free; throws Cancelled when `cancel` fires meanwhile and
  // Error(kFailed) when the file cannot be opened.
  static std::unique_ptr<FileLock> acquire(const fs::path& file, bool exclusive, CancelToken& cancel);

 private:
  struct Impl;
  explicit FileLock(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// Process-wide setup: UTF-8 console output, binary stdout (no CR LF translation on Windows). No-op elsewhere.
void init_platform();

std::string sha1_file(const fs::path& file);
std::string sha1_text(const std::string& text);
std::string read_text(const fs::path& file);  // throws Error(kFailed) when unreadable
// Writes through a temporary file and renames, so a reader never sees a half-written file.
void write_text_atomic(const fs::path& file, const std::string& text);
std::string utc_now();                         // 2026-11-23T20:41:00Z
std::string human_bytes(std::uint64_t bytes);  // "1.4 GB"
std::uint64_t free_bytes(const fs::path& dir); // free space of the volume holding `dir` (nearest existing parent)
std::uint64_t tree_bytes(const fs::path& dir); // sum of regular file sizes below dir (0 when missing)
std::string one_line(std::string text);        // newlines to spaces, for protocol lines

}  // namespace prep
