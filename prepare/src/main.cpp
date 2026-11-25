// SPDX-License-Identifier: GPL-3.0-or-later
// tos-prepare: from the player's two disc images to a game instance that runs without the discs.
// The design, the instance layout and the exit codes are in docs/PREPARE.md.
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>

#include "stages.hpp"
#include "toolchain.hpp"

using namespace prep;

#ifndef TOS_PREPARE_VERSION
#define TOS_PREPARE_VERSION "0.1.0"
#endif

namespace {

void usage(std::FILE* f) {
  std::fputs(
      "usage: tos-prepare <disc1> <disc2> --data DIR [options]\n"
      "       tos-prepare --data DIR --resume [options]       rebuild from the files kept in DIR (no disc)\n"
      "       tos-prepare --data DIR --check                  is DIR up to date? (exit 0 yes, 10 no)\n"
      "\n"
      "Verifies the two disc images, copies their files into DIR/game, translates the game code, builds it\n"
      "with the C++ compiler it carries and leaves DIR ready to play without the discs. The release version of this\n"
      "program contains everything it needs (compiler, libraries, recompiler); on first use it unpacks them into a\n"
      "per-user cache folder (about 400 MB, see --toolchain-cache). Nothing has to be installed.\n"
      "\n"
      "options:\n"
      "  --data DIR          the instance folder (created); needs about 2.5 GB free\n"
      "  --jobs N            parallel compiler processes (default: min(cores, free RAM / 1.5 GB))\n"
      "  --compiler CMD      use this C++ compiler instead of the one the toolkit names; e.g. \"zig c++\"\n"
      "                      ($CXX is used only with a toolkit that has no compiler of its own, a development build)\n"
      "  --toolchain-cache DIR  where the bundled build tools are unpacked (default: $TOS_RECOMP_CACHE, else the\n"
      "                      per-user cache: ~/.cache/tos-recomp or %LOCALAPPDATA%\\tos-recomp; if that cannot be\n"
      "                      written, DIR/.toolchain of --data)\n"
      "  --resume            skip the stages that are already done for the same inputs\n"
      "  --cancel-file FILE  stop cleanly (exit 130) as soon as FILE exists; it must not exist at start\n"
      "  --toolkit DIR       use this toolkit folder instead of the payload of this program or the toolkit next\n"
      "                      to it (development, see scripts/make-toolkit.sh)\n"
      "  --symbols DIR       use these symbol files instead of the toolkit's (development)\n"
      "  --dumps FILE        use this dumps.toml instead of the toolkit's (development)\n"
      "  --check             only report which stages are stale\n"
      "  --version, --help\n"
      "\n"
      "stdout is a line protocol (protocol, stage-start, stage-progress, progress, stage-end, warn, result);\n"
      "details go to DIR/logs/prepare.log.\n"
      "exit codes: 0 ok, 1 error, 2 usage, 3 dump not recognised, 4 not enough disk space, 5 compile failed,\n"
      "6 recompile failed, 7 toolkit or compiler missing, 8 extraction failed, 9 already running,\n"
      "10 stale (--check), 130 cancelled.\n",
      f);
}

fs::path default_toolkit() {
  const fs::path exe = self_exe();
  if (exe.empty()) return {};
  std::error_code ec;
  const fs::path dir = exe.parent_path();
  for (const fs::path& c : {dir / "toolkit", dir / ".." / "share" / "tos-recomp" / "toolkit", dir / ".." / "toolkit"}) {
    if (fs::exists(c / "toolkit.json", ec)) return fs::weakly_canonical(c, ec);
  }
  return {};
}

// The pre-stage of a run with a payload: makes the toolchain available (toolchain.hpp), reporting it like a stage of
// the pipeline. Takes the first place of the numbering and the first slice of the overall progress.
Toolchain unpack_toolchain(Context& ctx, const PayloadInfo& payload, bool quiet_when_present) {
  Reporter& rep = ctx.rep;
  ctx.payload_id = payload.id();
  ctx.stage_offset = 1;
  ctx.progress_start = 0.06;
  std::vector<fs::path> roots;
  if (!ctx.opt.toolchain_cache.empty()) {
    roots.push_back(ctx.opt.toolchain_cache);
  } else {
    roots.push_back(default_cache_root());
    roots.push_back(ctx.data() / ".toolchain");
  }
  // Already there (the usual run)? Then there is nothing to announce but a skipped stage.
  bool present = false;
  for (const auto& r : roots) present = present || (!r.empty() && unpacked_intact(r / from_u8(payload.id())));
  const char* title = present ? "Checking the build tools" : "Unpacking the build tools (first run only)";
  const auto started = std::chrono::steady_clock::now();
  const bool announce = !(present && quiet_when_present);
  if (announce) rep.set_stage("toolchain", 1, kStageCount + 1, title, 0.0, ctx.progress_start);
  Toolchain tc;
  try {
    tc = ensure_toolchain(
        payload, roots, ctx.cancel,
        [&](std::uint64_t done, std::uint64_t total) {
          rep.stage_progress(done, total, "Unpacking the build tools (first run only, " + human_bytes(total) + ")");
        },
        ctx.free_space);
  } catch (const Cancelled&) {
    if (announce) rep.stage_end("cancelled", 0);
    throw;
  } catch (const Error&) {
    if (announce) rep.stage_end("failed", 0);
    throw;
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  rep.log("toolchain: " + u8(tc.root) + (tc.unpacked_now ? " (unpacked now)" : " (already there)") + ", payload " + payload.sha256_hex());
  if (tc.unpacked_now) {
    rep.log("unpack: " + human_bytes(payload.unpacked_size) + " from " + human_bytes(payload.packed_size) + " in " + std::to_string(seconds) + " s, peak memory of the process " +
            human_bytes(peak_memory_kb() * 1024));
  }
  if (announce) rep.stage_end(tc.unpacked_now ? "ok" : "skipped", tc.unpacked_now ? seconds : 0.0);
  return tc;
}

}  // namespace


static int run(int argc, char** argv) {
  init_platform();
  Options opt;
  bool check = false;
  std::vector<std::string> positional;
  auto need = [&](int& i, const char* flag) -> std::string {
    if (i + 1 >= argc) {
      std::fprintf(stderr, "tos-prepare: %s needs a value\n", flag);
      std::exit(kUsage);
    }
    return argv[++i];
  };
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--help" || a == "-h") { usage(stdout); return 0; }
    if (a == "--version") { std::printf("tos-prepare %s\n", TOS_PREPARE_VERSION); return 0; }
    else if (a == "--data") opt.data = from_u8(need(i, "--data"));
    else if (a == "--jobs") {
      const std::string v = need(i, "--jobs");
      char* end = nullptr;
      const long n = std::strtol(v.c_str(), &end, 10);
      if (*end || n < 1 || n > 256) { std::fprintf(stderr, "tos-prepare: --jobs needs a number from 1 to 256\n"); return kUsage; }
      opt.jobs = static_cast<unsigned>(n);
    }
    else if (a == "--compiler") opt.compiler = need(i, "--compiler");
    else if (a == "--resume") opt.resume = true;
    else if (a == "--cancel-file") opt.cancel_file = from_u8(need(i, "--cancel-file"));
    else if (a == "--toolkit") opt.toolkit = from_u8(need(i, "--toolkit"));
    else if (a == "--toolchain-cache") opt.toolchain_cache = from_u8(need(i, "--toolchain-cache"));
    else if (a == "--symbols") opt.symbols = from_u8(need(i, "--symbols"));
    else if (a == "--dumps") opt.dumps = from_u8(need(i, "--dumps"));
    else if (a == "--check") check = true;
    else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "tos-prepare: unknown option %s\n", a.c_str()); usage(stderr); return kUsage; }
    else positional.push_back(a);
  }
  if (opt.data.empty()) { std::fprintf(stderr, "tos-prepare: --data DIR is required\n"); usage(stderr); return kUsage; }
  if (positional.size() != 0 && positional.size() != 2) {
    std::fprintf(stderr, "tos-prepare: give both disc images, or none with --resume/--check\n");
    usage(stderr);
    return kUsage;
  }
  if (positional.empty() && !opt.resume && !check) {
    std::fprintf(stderr, "tos-prepare: the two disc images are required (without them only --resume or --check work)\n");
    usage(stderr);
    return kUsage;
  }
  for (const auto& p : positional) opt.discs.push_back(from_u8(p));
  if (!opt.cancel_file.empty() && fs::exists(opt.cancel_file)) {
    std::fprintf(stderr, "tos-prepare: the cancel file %s already exists\n", u8(opt.cancel_file).c_str());
    return kUsage;
  }
  opt.data = fs::absolute(opt.data);

  Reporter rep(stdout);
  CancelToken cancel;
  cancel.set_file(opt.cancel_file);
  install_signal_handlers(cancel);

  std::error_code ec;
  fs::create_directories(opt.data, ec);
  if (ec) {
    std::fprintf(stderr, "tos-prepare: cannot create %s: %s\n", u8(opt.data).c_str(), ec.message().c_str());
    return kFailed;
  }
  // One run at a time per instance.
  const fs::path lock_path = opt.data / ".prepare.lock";
  if (!lock_for_process(lock_path)) {
    std::fprintf(stderr, "tos-prepare: another tos-prepare is working on %s\n", u8(opt.data).c_str());
    return kBusy;
  }
  rep.open_log(opt.data / layout::kLogs / "prepare.log");
  rep.protocol(TOS_PREPARE_VERSION);

  Context ctx(opt, rep, cancel);
  ctx.state = State::load(ctx.state_file());
  auto plan = std::make_shared<Plan>();
  Toolchain toolchain;  // keeps the unpacked folder in use until the program ends
  try {
    ctx.root = self_exe().parent_path();
    if (ctx.opt.toolkit.empty()) {
      // A release carries its toolkit and compiler as a payload: unpack it once, then use the unpacked folder.
      std::optional<PayloadInfo> payload;
      if (const fs::path exe = self_exe(); !exe.empty()) payload = find_payload(exe);
      if (payload) {
        toolchain = unpack_toolchain(ctx, *payload, check);
        ctx.root = toolchain.root;
        ctx.opt.toolkit = toolchain.root / "toolkit";
      } else {
        ctx.opt.toolkit = default_toolkit();
      }
    }
    if (ctx.opt.toolkit.empty()) throw Error(kNoToolchain, "no toolkit found: this tos-prepare carries none and there is none next to it; pass --toolkit DIR (scripts/make-toolkit.sh)");
    plan->toolkit = Toolkit::load(ctx.opt.toolkit, opt.symbols, opt.dumps);
    ctx.toolkit = plan->toolkit.json;
    ctx.toolkit_dir = ctx.opt.toolkit;
  } catch (const Cancelled&) {
    rep.result("cancelled", kCancelled, "cancelled");
    return kCancelled;
  } catch (const Error& e) {
    rep.result("failed", e.code(), e.what());
    return e.code();
  }
  auto stages = make_stages(ctx, plan);
  if (check) {
    ctx.state.set_file(ctx.state_file());
    const bool ok = check_stages(ctx, stages);
    return ok ? kOk : kStale;
  }
  return run_pipeline(ctx, stages, TOS_PREPARE_VERSION);
}

#if defined(_WIN32)
// The narrow main() would receive the command line in the ANSI code page: take it from the wide one instead,
// as UTF-8, so that disc and folder names with any character reach the program intact.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

int main() {
  int wargc = 0;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  if (!wargv) return kFailed;
  std::vector<std::string> args;
  for (int i = 0; i < wargc; ++i) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string a(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, a.data(), n, nullptr, nullptr);
    args.push_back(std::move(a));
  }
  LocalFree(wargv);
  std::vector<char*> ptrs;
  for (auto& a : args) ptrs.push_back(a.data());
  ptrs.push_back(nullptr);
  return run(wargc, ptrs.data());
}
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
