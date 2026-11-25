// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.hpp"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "torecomp/sha1.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define PSAPI_VERSION 2
#include <psapi.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace prep {

namespace {
volatile std::sig_atomic_t g_signal = 0;
#if !defined(_WIN32)
void on_signal(int sig) { g_signal = sig; }
#endif
}  // namespace

bool CancelToken::poll() {
  if (flag_.load()) return true;
  if (g_signal != 0) {
    flag_.store(true);
    return true;
  }
  if (!file_.empty()) {
    std::error_code ec;
    if (fs::exists(file_, ec)) {
      flag_.store(true);
      return true;
    }
  }
  return false;
}

#if defined(_WIN32)
namespace {
// Runs on a thread of its own: Ctrl+C, Ctrl+Break, closing the console window, logoff and shutdown all
// become the same cancellation request as SIGINT on POSIX.
BOOL WINAPI on_console_event(DWORD) {
  g_signal = SIGINT;
  return TRUE;
}
}  // namespace

void install_signal_handlers(CancelToken&) { SetConsoleCtrlHandler(on_console_event, TRUE); }
#else
void install_signal_handlers(CancelToken&) {
  struct sigaction sa {};
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // no SA_RESTART: blocking reads return and see the flag
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}
#endif

std::string u8(const fs::path& p) { return p.u8string(); }
fs::path from_u8(const std::string& utf8) { return fs::u8path(utf8); }

#if defined(_WIN32)
fs::path self_exe() {
  std::wstring buf(1024, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    if (n < buf.size()) {
      buf.resize(n);
      return fs::path(buf);
    }
    buf.resize(buf.size() * 2);
  }
}

std::uint64_t available_memory_kb() {
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof m;
  if (!GlobalMemoryStatusEx(&m)) return 0;
  return m.ullAvailPhys / 1024;
}

std::uint64_t peak_memory_kb() {
  PROCESS_MEMORY_COUNTERS c{};
  c.cb = sizeof c;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c)) return 0;
  return c.PeakWorkingSetSize / 1024;
}

bool lock_for_process(const fs::path& file) {
  // Share mode 0: nobody else can open the file while this handle lives, and Windows closes it with the process.
  HANDLE h = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  return h != INVALID_HANDLE_VALUE;
}

struct FileLock::Impl {
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~Impl() {
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);  // releases the lock
  }
};

std::unique_ptr<FileLock> FileLock::try_acquire(const fs::path& file, bool exclusive) {
  auto impl = std::make_unique<Impl>();
  impl->handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (impl->handle == INVALID_HANDLE_VALUE) return nullptr;
  OVERLAPPED ov{};
  if (!LockFileEx(impl->handle, LOCKFILE_FAIL_IMMEDIATELY | (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0, 1, 0, &ov)) return nullptr;
  return std::unique_ptr<FileLock>(new FileLock(std::move(impl)));
}

void init_platform() {
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  SetConsoleOutputCP(CP_UTF8);
}
#else
fs::path self_exe() {
  std::error_code ec;
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path() : exe;
}

std::uint64_t available_memory_kb() {
  std::ifstream in("/proc/meminfo");
  std::string key, unit;
  double kb = 0;
  while (in >> key >> kb >> unit) {
    if (key == "MemAvailable:") return static_cast<std::uint64_t>(kb);
  }
  return 0;
}

std::uint64_t peak_memory_kb() {
  std::ifstream in("/proc/self/status");
  std::string line;
  while (std::getline(in, line)) {
    if (line.compare(0, 6, "VmHWM:") == 0) return std::strtoull(line.c_str() + 6, nullptr, 10);
  }
  return 0;
}

bool lock_for_process(const fs::path& file) {
  const int fd = ::open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  return fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0;
}

struct FileLock::Impl {
  int fd = -1;
  ~Impl() {
    if (fd >= 0) ::close(fd);  // releases the lock
  }
};

std::unique_ptr<FileLock> FileLock::try_acquire(const fs::path& file, bool exclusive) {
  auto impl = std::make_unique<Impl>();
  impl->fd = ::open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (impl->fd < 0) return nullptr;
  if (::flock(impl->fd, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) return nullptr;
  return std::unique_ptr<FileLock>(new FileLock(std::move(impl)));
}

void init_platform() {}
#endif

FileLock::FileLock(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileLock::~FileLock() = default;

std::unique_ptr<FileLock> FileLock::acquire(const fs::path& file, bool exclusive, CancelToken& cancel) {
  for (;;) {
    if (auto lock = try_acquire(file, exclusive)) return lock;
    // Distinguish "held" from "cannot be opened": a file that cannot even be created will never be free.
    std::error_code ec;
    if (!fs::exists(file, ec) && !std::ofstream(file, std::ios::app)) throw Error(kFailed, "cannot create the lock file " + u8(file));
    cancel.check();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

std::string sha1_file(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw Error(kFailed, "cannot read " + u8(file));
  torecomp::Sha1 h;
  std::vector<char> buf(1 << 20);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    h.update(buf.data(), static_cast<std::size_t>(in.gcount()));
  }
  return torecomp::Sha1::hex(h.finish());
}

std::string sha1_text(const std::string& text) { return torecomp::Sha1::hex_of(text.data(), text.size()); }

std::string read_text(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw Error(kFailed, "cannot read " + u8(file));
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_text_atomic(const fs::path& file, const std::string& text) {
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    if (!out) throw Error(kFailed, "cannot write " + u8(tmp));
  }
  std::error_code ec;
  fs::rename(tmp, file, ec);
  if (ec) throw Error(kFailed, "cannot rename " + u8(tmp) + ": " + ec.message());
}

std::string utc_now() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string human_bytes(std::uint64_t b) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(b);
  int u = 0;
  while (v >= 1000 && u < 4) {
    v /= 1000;
    ++u;
  }
  char buf[32];
  std::snprintf(buf, sizeof buf, u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
  return buf;
}

std::uint64_t free_bytes(const fs::path& dir) {
  fs::path p = fs::absolute(dir);
  std::error_code ec;
  while (!p.empty() && !fs::exists(p, ec)) {
    if (p == p.parent_path()) break;
    p = p.parent_path();
  }
  const auto info = fs::space(p, ec);
  if (ec) throw Error(kFailed, "cannot read the free space of " + u8(p) + ": " + ec.message());
  return info.available;
}

std::uint64_t tree_bytes(const fs::path& dir) {
  std::uint64_t total = 0;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return 0;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2)) total += it->file_size(e2);
  }
  return total;
}

std::string one_line(std::string text) {
  for (char& c : text) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return text;
}

}  // namespace prep
