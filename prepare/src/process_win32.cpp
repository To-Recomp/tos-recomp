// SPDX-License-Identifier: GPL-3.0-or-later
// Windows implementation of process.hpp: CreateProcess, one Job Object per child (killing the job kills the
// whole process tree, like killpg on POSIX), output through an anonymous pipe.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <vector>

#include "process.hpp"

namespace prep {

namespace {

std::wstring widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string win_error(DWORD code) {
  wchar_t* buf = nullptr;
  const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
  std::string text = n && buf ? narrow(std::wstring(buf, n)) : "error " + std::to_string(code);
  if (buf) LocalFree(buf);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  return text;
}

// One argument as CommandLineToArgvW / the MSVC runtime will read it back.
void quote_argument(std::wstring& out, const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    out += arg;
    return;
  }
  out += L'"';
  for (std::size_t i = 0;; ++i) {
    std::size_t backslashes = 0;
    while (i < arg.size() && arg[i] == L'\\') ++i, ++backslashes;
    if (i == arg.size()) {
      out.append(backslashes * 2, L'\\');
      break;
    }
    if (arg[i] == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out += L'"';
    } else {
      out.append(backslashes, L'\\');
      out += arg[i];
    }
  }
  out += L'"';
}

bool ieq(const std::wstring& a, const std::wstring& b) {
  return a.size() == b.size() && CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// The environment block: the current environment with `extra` set (names are case-insensitive).
std::wstring environment_block(const std::vector<std::pair<std::string, std::string>>& extra) {
  std::vector<std::wstring> vars;
  if (wchar_t* env = GetEnvironmentStringsW()) {
    for (const wchar_t* p = env; *p; p += std::wcslen(p) + 1) vars.emplace_back(p);
    FreeEnvironmentStringsW(env);
  }
  for (const auto& [k, v] : extra) {
    const std::wstring name = widen(k);
    bool replaced = false;
    for (auto& s : vars) {
      const std::size_t eq = s.find(L'=', 1);  // "=C:=C:\\" style entries start with '='
      if (eq != std::wstring::npos && ieq(s.substr(0, eq), name)) {
        s = name + L"=" + widen(v);
        replaced = true;
      }
    }
    if (!replaced) vars.push_back(name + L"=" + widen(v));
  }
  std::wstring block;
  for (const auto& s : vars) {
    block += s;
    block += L'\0';
  }
  block += L'\0';
  return block;
}

bool has_separator(const std::string& name) { return name.find_first_of("/\\:") != std::string::npos; }

bool is_file(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

// `p` as given, then with the .exe suffix when it has no extension.
std::string with_exe(const fs::path& p) {
  if (is_file(p)) return u8(p);
  if (!p.has_extension()) {
    fs::path q = p;
    q += ".exe";
    if (is_file(q)) return u8(q);
  }
  return {};
}

struct Handle {
  HANDLE h = nullptr;
  Handle() = default;
  explicit Handle(HANDLE v) : h(v) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() { reset(); }
  void reset(HANDLE v = nullptr) {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = v;
  }
  explicit operator bool() const { return h && h != INVALID_HANDLE_VALUE; }
};

}  // namespace

std::string find_in_path(const std::string& name) {
  if (name.empty()) return {};
  if (has_separator(name)) return with_exe(from_u8(name));
  const std::wstring path_var = [] {
    const DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring v(n, L'\0');
    if (n) {
      v.resize(GetEnvironmentVariableW(L"PATH", v.data(), n));
    }
    return v;
  }();
  std::wstringstream in(path_var);
  for (std::wstring dir; std::getline(in, dir, L';');) {
    if (dir.empty()) continue;
    if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
    const std::string found = with_exe(fs::path(dir) / from_u8(name));
    if (!found.empty()) return found;
  }
  return {};
}

RunResult run_process(const std::vector<std::string>& argv, const RunOptions& options, CancelToken& cancel,
                      const std::function<void(const std::string&)>& on_line) {
  RunResult result;
  if (argv.empty()) throw Error(kFailed, "empty command");

  // Output pipe. Only the write end is inherited, and only by this child: the explicit handle list below keeps
  // the pipes of compilers started at the same moment by other threads out of it.
  SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
  Handle read_end, write_end, nul, err_nul;
  {
    HANDLE r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, &inherit, 0)) throw Error(kFailed, "pipe: " + win_error(GetLastError()));
    read_end.reset(r);
    write_end.reset(w);
    SetHandleInformation(read_end.h, HANDLE_FLAG_INHERIT, 0);
  }
  if (options.discard_stderr) {
    err_nul.reset(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  }
  nul.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

  std::wstring command_line;
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (i) command_line += L' ';
    quote_argument(command_line, widen(argv[i]));
  }
  std::wstring env = environment_block(options.env);

  std::vector<HANDLE> inherited = {write_end.h};  // no duplicates: the call rejects them
  if (nul) inherited.push_back(nul.h);
  if (err_nul) inherited.push_back(err_nul.h);
  SIZE_T list_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
  std::vector<char> list_storage(list_size);
  auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list_storage.data());
  if (!InitializeProcThreadAttributeList(list, 1, 0, &list_size)) throw Error(kFailed, "attribute list: " + win_error(GetLastError()));
  UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(), inherited.size() * sizeof(HANDLE), nullptr, nullptr);

  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof si;
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = nul ? nul.h : INVALID_HANDLE_VALUE;
  si.StartupInfo.hStdOutput = write_end.h;
  si.StartupInfo.hStdError = err_nul ? err_nul.h : write_end.h;
  si.lpAttributeList = list;

  // The whole process tree lives in a job that dies with its handle (and with this process).
  Handle job(CreateJobObjectW(nullptr, nullptr));
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof limits);
  }

  const auto t0 = std::chrono::steady_clock::now();
  PROCESS_INFORMATION pi{};
  // Suspended until it is in the job; its own process group so that a console Ctrl+C reaches tos-prepare
  // (which cancels and kills the job) and not the compiler directly; no console window.
  const DWORD flags = CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
  const BOOL created = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, flags, env.data(), nullptr, &si.StartupInfo, &pi);
  const DWORD create_error = GetLastError();
  DeleteProcThreadAttributeList(list);
  write_end.reset();
  nul.reset();
  err_nul.reset();
  if (!created) throw Error(kNoToolchain, "cannot start " + argv[0] + ": " + win_error(create_error));
  Handle process(pi.hProcess), thread(pi.hThread);
  if (job) AssignProcessToJobObject(job.h, process.h);
  ResumeThread(thread.h);
  result.started = true;

  auto kill_tree = [&] {
    if (job) {
      TerminateJobObject(job.h, 1);
    } else {
      TerminateProcess(process.h, 1);
    }
  };

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
    DWORD avail = 0;
    if (!PeekNamedPipe(read_end.h, nullptr, 0, nullptr, &avail, nullptr)) break;  // broken pipe: every holder exited
    if (avail == 0) {
      Sleep(10);
      continue;
    }
    DWORD n = 0;
    if (!ReadFile(read_end.h, buf, static_cast<DWORD>(std::min<std::size_t>(sizeof buf, avail)), &n, nullptr) || n == 0) break;
    pending.append(buf, n);
    std::size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
      std::string l = pending.substr(0, nl);
      if (!l.empty() && l.back() == '\r') l.pop_back();
      emit(l);
      pending.erase(0, nl + 1);
    }
  }
  read_end.reset();
  if (!pending.empty() && !cancelled) emit(pending);

  if (cancelled) {
    kill_tree();
    WaitForSingleObject(process.h, 10000);
    throw Cancelled();
  }

  WaitForSingleObject(process.h, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(process.h, &code);
  result.exit_code = static_cast<int>(code);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    if (QueryInformationJobObject(job.h, JobObjectExtendedLimitInformation, &info, sizeof info, nullptr)) {
      result.max_rss_kb = static_cast<long>(info.PeakJobMemoryUsed / 1024);
    }
  }
  // Anything the child left running is not ours to keep.
  kill_tree();
  result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return result;
}

}  // namespace prep
