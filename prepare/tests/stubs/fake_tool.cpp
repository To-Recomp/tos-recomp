// SPDX-License-Identifier: GPL-3.0-or-later
// Test stand-ins for the programs tos-prepare runs, one executable per role, built from this file with
// -DFAKE_TOOL_<ROLE> (see ../../CMakeLists.txt). Portable: the same code runs on POSIX and Windows.
//
//   fake-cxx         a C++ compiler driver. Compiling copies the source into the object; linking writes an
//                    "executable" (a text file). FAKE_CXX_SLEEP=N sleeps N seconds per compilation,
//                    FAKE_CXX_FAIL=1 fails compilations.
//   fake-recompiler  gcrecomp_recompiler: writes three "generated" files. recomp_0001.cpp depends on the
//                    symbol files, the others only on the DOL, so changing the symbols changes one file.
//                    Same command line as the real one: <dol> --csv F --extra-funcs F [--rel F]... --output DIR
//                    --project NAME. FAKE_RECOMPILER_FAIL=1 makes it fail.
//   fake-proc        a process to run through run_process:
//                      lines          prints "one" (stdout), "two" (stderr), "three" (no newline), exits 7
//                      env NAME       prints the value of the environment variable
//                      sleeper        starts a grandchild that sleeps 60 s, prints its process id, then waits for it
//                      sleep SECONDS  sleeps (the grandchild)
//                      alive PID      exits 0 when that process is running, 1 when not
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace {

void sleep_seconds(const char* s) { std::this_thread::sleep_for(std::chrono::seconds(std::atoi(s))); }

std::string read_all(const std::string& path) {
  std::ifstream in(fs::u8path(path), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

#if defined(FAKE_TOOL_CXX)
int tool(int argc, char** argv) {
  // The queries may follow other words ("fake-cxx -target T --version"). Like `zig c++ -target x86_64-linux-gnu.2.31
  // -dumpmachine`, -dumpmachine is refused when a target carries a glibc version.
  bool version = false, dumpmachine = false, glibc_target = false;
  for (int i = 1; i < argc; ++i) {
    version = version || !std::strcmp(argv[i], "--version");
    dumpmachine = dumpmachine || !std::strcmp(argv[i], "-dumpmachine");
    glibc_target = glibc_target || !std::strncmp(argv[i], "x86_64-linux-gnu.", 17);
  }
  if (version) { std::puts("fakecc 1.0 (test stub)"); return 0; }
  if (dumpmachine) {
    if (glibc_target) { std::fputs("error: version '.2.31' in target triple is invalid\n", stderr); return 1; }
    std::puts("x86_64-fake-linux");
    return 0;
  }
  bool compile = false;
  std::string out, src;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-c" && i + 1 < argc) { compile = true; src = argv[++i]; }
    else if (a == "-o" && i + 1 < argc) out = argv[++i];
    // everything else (flags, objects, libraries) is accepted and ignored
  }
  if (compile) {
    if (const char* s = std::getenv("FAKE_CXX_SLEEP"); s && *s) sleep_seconds(s);
    if (const char* f = std::getenv("FAKE_CXX_FAIL"); f && *f) {
      std::fprintf(stderr, "%s: error: fake failure\n", src.c_str());
      return 1;
    }
    std::ofstream(fs::u8path(out), std::ios::binary) << read_all(src);
  } else {
    std::ofstream(fs::u8path(out), std::ios::binary) << "fake game\n";
  }
  return 0;
}
#elif defined(FAKE_TOOL_RECOMPILER)
std::uint64_t checksum(const std::string& s) {
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
  return h;
}

int tool(int argc, char** argv) {
  if (const char* f = std::getenv("FAKE_RECOMPILER_FAIL"); f && *f) {
    std::fputs("fake recompiler: failing on request\n", stderr);
    return 3;
  }
  if (argc < 2) return 2;
  const std::string dol = argv[1];
  std::string csv, extra, rels, out, project;
  for (int i = 2; i + 1 < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--csv") csv = argv[++i];
    else if (a == "--extra-funcs") extra = argv[++i];
    else if (a == "--rel") rels += " " + std::string(argv[++i]);
    else if (a == "--output") out = argv[++i];
    else if (a == "--project") project = argv[++i];
  }
  const fs::path dir = fs::u8path(out);
  fs::create_directories(dir);
  std::fprintf(stderr, "recompiling %s\n", dol.c_str());
  const std::string inc = "#include \"recomp_common.h\"\n";
  std::ofstream(dir / "recomp_0000.cpp", std::ios::binary) << "// " << checksum(read_all(dol)) << "\n" << inc;
  std::ofstream(dir / "recomp_0001.cpp", std::ios::binary)
      << "// " << read_all(csv) << read_all(extra) << " " << rels << "\n" << inc;
  std::ofstream(dir / "recomp_0002.cpp", std::ios::binary) << "// " << project << "\n" << inc;
  std::ofstream(dir / "recomp_common.h", std::ios::binary) << "// common header\n";
  return 0;
}
#elif defined(FAKE_TOOL_PROC)
bool alive(long pid) {
#if defined(_WIN32)
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
  if (!h) return false;
  DWORD code = 0;
  const bool running = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return running;
#else
  return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

int tool(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "lines") {
    std::fputs("one\n", stdout);
    std::fflush(stdout);
    std::fputs("two\n", stderr);
    std::fflush(stderr);
    std::fputs("three", stdout);
    std::fflush(stdout);
    return 7;
  }
  if (mode == "env" && argc > 2) {
    const char* v = std::getenv(argv[2]);
    std::printf("%s\n", v ? v : "(unset)");
    return 0;
  }
  if (mode == "sleep" && argc > 2) {
    sleep_seconds(argv[2]);
    return 0;
  }
  if (mode == "alive" && argc > 2) return alive(std::atol(argv[2])) ? 0 : 1;
  if (mode == "sleeper") {
#if defined(_WIN32)
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" sleep 60";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) return 1;
    std::printf("%lu\n", static_cast<unsigned long>(pi.dwProcessId));
    std::fflush(stdout);
    WaitForSingleObject(pi.hProcess, INFINITE);
#else
    pid_t pid = 0;
    char sleep_arg[] = "sleep", sixty[] = "60";
    char* args[] = {argv[0], sleep_arg, sixty, nullptr};
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, args, environ) != 0) return 1;
    std::printf("%ld\n", static_cast<long>(pid));
    std::fflush(stdout);
    int st = 0;
    waitpid(pid, &st, 0);
#endif
    return 0;
  }
  return 2;
}
#else
#error "build with -DFAKE_TOOL_CXX, -DFAKE_TOOL_RECOMPILER or -DFAKE_TOOL_PROC"
#endif

}  // namespace

int main(int argc, char** argv) { return tool(argc, argv); }
