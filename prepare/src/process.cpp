// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sstream>

extern char** environ;

namespace prep {

std::vector<std::string> split_command(const std::string& command) {
  std::vector<std::string> out;
  std::istringstream in(command);
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

std::string find_in_path(const std::string& name) {
  if (name.empty()) return {};
  auto executable = [](const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0;
  };
  if (name.find('/') != std::string::npos) return executable(name) ? name : std::string();
  const char* path = std::getenv("PATH");
  if (!path) return {};
  std::istringstream in(path);
  for (std::string dir; std::getline(in, dir, ':');) {
    if (dir.empty()) continue;
    fs::path p = fs::path(dir) / name;
    if (executable(p)) return p.string();
  }
  return {};
}

namespace {

void kill_group(pid_t pid, int sig) { ::kill(-pid, sig); }

}  // namespace

RunResult run_process(const std::vector<std::string>& argv, const RunOptions& options, CancelToken& cancel,
                      const std::function<void(const std::string&)>& on_line) {
  RunResult result;
  if (argv.empty()) throw Error(kFailed, "empty command");

  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) != 0) throw Error(kFailed, std::string("pipe: ") + std::strerror(errno));

  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
  posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
  int devnull = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (devnull >= 0) posix_spawn_file_actions_adddup2(&fa, devnull, 0);
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setpgroup(&attr, 0);  // own process group: killpg reaches everything it starts
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

  std::vector<std::string> env_strings;
  for (char** e = environ; e && *e; ++e) env_strings.emplace_back(*e);
  for (const auto& [k, v] : options.env) {
    const std::string prefix = k + "=";
    bool replaced = false;
    for (auto& s : env_strings) {
      if (s.compare(0, prefix.size(), prefix) == 0) { s = prefix + v; replaced = true; }
    }
    if (!replaced) env_strings.push_back(prefix + v);
  }
  std::vector<char*> envp;
  for (auto& s : env_strings) envp.push_back(s.data());
  envp.push_back(nullptr);
  std::vector<char*> args;
  std::vector<std::string> argv_copy = argv;
  for (auto& s : argv_copy) args.push_back(s.data());
  args.push_back(nullptr);

  const auto t0 = std::chrono::steady_clock::now();
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, args[0], &fa, &attr, args.data(), envp.data());
  posix_spawn_file_actions_destroy(&fa);
  posix_spawnattr_destroy(&attr);
  ::close(fds[1]);
  if (devnull >= 0) ::close(devnull);
  if (rc != 0) {
    ::close(fds[0]);
    throw Error(kNoToolchain, "cannot start " + argv[0] + ": " + std::strerror(rc));
  }
  result.started = true;

  std::string pending;
  auto emit = [&](const std::string& l) {
    result.tail.push_back(l);
    if (result.tail.size() > options.tail_lines) result.tail.erase(result.tail.begin());
    if (on_line) on_line(l);
  };
  bool cancelled = false;
  char buf[8192];
  for (;;) {
    if (cancel.poll()) {
      cancelled = true;
      break;
    }
    pollfd p{fds[0], POLLIN, 0};
    const int pr = ::poll(&p, 1, 100);
    if (pr < 0 && errno != EINTR) break;
    if (pr <= 0) continue;
    const ssize_t n = ::read(fds[0], buf, sizeof buf);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;  // EOF: every holder of the pipe exited
    pending.append(buf, static_cast<std::size_t>(n));
    std::size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
      emit(pending.substr(0, nl));
      pending.erase(0, nl + 1);
    }
  }
  ::close(fds[0]);
  if (!pending.empty() && !cancelled) emit(pending);

  if (cancelled) {
    const pid_t pgid = pid;
    kill_group(pgid, SIGTERM);
    // Give the group a moment to leave, then make sure nothing in it survives.
    bool reaped = false;
    for (int i = 0; i < 30 && !reaped; ++i) {
      int st;
      reaped = ::waitpid(pid, &st, WNOHANG) == pid;
      if (!reaped) ::usleep(100 * 1000);
    }
    kill_group(pgid, SIGKILL);
    if (!reaped) {
      int st;
      ::waitpid(pid, &st, 0);
    }
    throw Cancelled();
  }

  int status = 0;
  rusage ru{};
  while (::wait4(pid, &status, 0, &ru) < 0 && errno == EINTR) {
  }
  // Anything the child left running in its group is not ours to keep.
  kill_group(pid, SIGKILL);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  result.max_rss_kb = ru.ru_maxrss;
  result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return result;
}

}  // namespace prep
