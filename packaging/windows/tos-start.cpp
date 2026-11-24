// SPDX-License-Identifier: GPL-3.0-or-later
// tos-start.exe: starts the game that tos-prepare built in the instance's data folder (docs/PREPARE.md, section 2).
// The Windows counterpart of packaging/tos-start (a shell script): the launcher runs it with RECOMPLAUNCHER_DATA set.
// It changes to the data folder, points the game at its memory card image in saves/, runs bin\tos_boot.exe with
// the game's arguments plus whatever it was given, waits, and returns the game's exit code. The game lives in a
// Job Object that ends with this process, so closing or killing the launcher's child ends the game too.
//
//   tos-start.exe [extra arguments for the game]
//
// Built with Zig's C++ compiler by scripts/package-release.sh. No stack setting is needed here: the game
// executable is linked with a 512 MB stack reserve by tos-prepare (toolkit.json), which Windows applies to the
// main thread and to every thread the game starts.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

namespace {

void fail(const wchar_t* text, const std::wstring& detail = L"") {
  std::fwprintf(stderr, L"tos-start: %ls%ls\n", text, detail.c_str());
}

// One argument as CommandLineToArgvW reads it back.
void quote(std::wstring& out, const std::wstring& arg) {
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

std::wstring env(const wchar_t* name) {
  const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
  if (n == 0) return {};
  std::wstring v(n, L'\0');
  v.resize(GetEnvironmentVariableW(name, v.data(), n));
  return v;
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

}  // namespace

int run() {
  const std::wstring data = env(L"RECOMPLAUNCHER_DATA");
  if (data.empty()) {
    fail(L"started by the launcher only (RECOMPLAUNCHER_DATA is not set)");
    return 1;
  }
  if (!SetCurrentDirectoryW(data.c_str())) {
    fail(L"cannot enter ", data);
    return 1;
  }
  const std::wstring exe = L"bin\\tos_boot.exe";
  if (!exists(exe)) {
    fail((data + L"\\" + exe + L" is missing: build the game first (tos-prepare)").c_str());
    return 1;
  }
  CreateDirectoryW(L"saves", nullptr);
  std::wstring card = data;
  if (!card.empty() && card.back() != L'\\' && card.back() != L'/') card += L'\\';
  card += L"saves\\card-a.raw";
  SetEnvironmentVariableW(L"GCRECOMP_CARD_IMAGE", card.c_str());

  std::wstring command;
  quote(command, exe);
  for (const wchar_t* fixed : {L"game/sys/main.dol", L"game/files", L"86400"}) {
    command += L' ';
    quote(command, fixed);
  }
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    command += L' ';
    quote(command, argv[i]);
  }
  if (argv) LocalFree(argv);

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
  }
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
    fail(L"cannot start ", exe);
    return 1;
  }
  if (job) AssignProcessToJobObject(job, pi.hProcess);
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}

int main() { return run(); }
