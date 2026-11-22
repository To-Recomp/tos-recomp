// SPDX-License-Identifier: GPL-3.0-or-later
// Boot harness: loads main.dol into emulated RAM and calls the game's entry point.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#include "gcrecomp/runtime.h"
#include "gcrecomp/gp/gp.h"
#include "recomp_funcs.h"

using namespace gcrecomp;

void register_recompiled_functions(FuncTable& table);

namespace {

constexpr uint32_t kEntryPoint = 0x80003154;  // __start of the PAL French main.dol

uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool load_dol(const char* path, Memory& mem) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::vector<uint8_t> dol((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (dol.size() < 0x100) return false;
  // The BSS range spans sections that hold data (here the small-data areas), so it is cleared first
  // and the sections are copied over it.
  const uint32_t bss_addr = be32(&dol[0xD8]);
  const uint32_t bss_size = be32(&dol[0xDC]);
  if (bss_size) std::memset(mem.translate(bss_addr), 0, bss_size);
  for (int i = 0; i < 18; ++i) {
    const uint32_t off = be32(&dol[4 * i]);
    const uint32_t addr = be32(&dol[0x48 + 4 * i]);
    const uint32_t size = be32(&dol[0x90 + 4 * i]);
    if (size == 0) continue;
    if (off + size > dol.size()) return false;
    uint8_t* dst = mem.translate(addr);
    if (!dst) return false;
    std::memcpy(dst, &dol[off], size);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <main.dol> <extracted-files-dir> [seconds]\n", argv[0]);
    return 2;
  }
  const int seconds = argc > 3 ? std::atoi(argv[3]) : 20;

  if (!runtime_init()) {
    std::fprintf(stderr, "runtime_init failed\n");
    return 1;
  }
  GameConfig config;
  std::memcpy(config.game_id, "GQSF", 4);
  std::memcpy(config.company_code, "AF", 2);
  init_low_memory(&g_mem, config);
  set_game_root(argv[2]);

  if (!load_dol(argv[1], g_mem)) {
    std::fprintf(stderr, "cannot load %s\n", argv[1]);
    return 1;
  }
  register_recompiled_functions(g_func_table);

  // Graphics: the recompiled GX code writes its command stream to the write-gather pipe, which the
  // GP module turns into OpenGL draws. TOS_SCREENSHOT_DIR saves presented frames as PPM files.
  gp::Config gp_config;
  gp_config.screenshot_dir = std::getenv("TOS_SCREENSHOT_DIR");
  if (const char* shots = std::getenv("TOS_MAX_SHOTS")) gp_config.max_screenshots = std::atoi(shots);
  const bool graphics = gp::init(gp_config);
  if (!graphics) std::fprintf(stderr, "[boot] no graphics: running without a window\n");
  g_retrace_hook = [] {
    static bool once = true;
    if (once) {
      once = false;
      const uint32_t r2 = g_ctx.r[2], r13 = g_ctx.r[13];
      std::fprintf(stderr, "[boot] r2=0x%08X r13=0x%08X; floats at r2-30448..-30436: %g %g %g %g\n", r2, r13,
                   g_mem.readf32(r2 - 30448), g_mem.readf32(r2 - 30444), g_mem.readf32(r2 - 30440), g_mem.readf32(r2 - 30436));
    }
    static int n = 0;
    if (++n == 120) {
      const uint32_t rm = g_mem.read32(g_ctx.r[13] - 31120);
      std::fprintf(stderr, "[boot] render mode pointer [r13-31120]=0x%08X:", rm);
      for (int i = 0; i < 16; ++i) std::fprintf(stderr, " %02X", g_mem.read8(rm + i));
      std::fprintf(stderr, "\n");
    }
    if (!gp::pump_events()) std::_Exit(0);
  };

  // If the game spins or stops making progress, say where.
  std::atomic<bool> done{false};
  std::thread watchdog([&] {
    uint64_t last_seq = 0;
    for (int t = 0; t < seconds * 10 && !done; ++t) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      uint32_t addr = 0, lr = 0;
      const uint64_t seq = last_dispatch(&addr, &lr);
      if (t % 50 == 49) {
        std::fprintf(stderr, "[watchdog] %llu dispatches (%s), last entered 0x%08X from 0x%08X\n",
                     (unsigned long long)seq, seq == last_seq ? "stalled" : "moving", addr, lr);
        last_seq = seq;
      }
    }
    if (!done) {
      uint32_t addr = 0, lr = 0;
      last_dispatch(&addr, &lr);
      std::fprintf(stderr, "[watchdog] stopping after %d s; last dispatch 0x%08X from 0x%08X, r1=0x%08X lr=0x%08X\n",
                   seconds, addr, lr, g_ctx.r[1], g_ctx.lr);
      const gp::Stats& st = gp::stats();
      std::fprintf(stderr, "[gp] %llu frames, %llu commands, %llu draws, %llu vertices, %llu BP / %llu XF / %llu CP writes, %llu unknown\n",
                   (unsigned long long)gp::frames_presented(), (unsigned long long)st.commands, (unsigned long long)st.draws,
                   (unsigned long long)st.vertices, (unsigned long long)st.bp_writes, (unsigned long long)st.xf_writes,
                   (unsigned long long)st.cp_writes, (unsigned long long)st.unknown_commands);
      std::_Exit(3);
    }
  });

  std::fprintf(stderr, "[boot] entering 0x%08X\n", kEntryPoint);
  func_80003154(&g_ctx, &g_mem);
  done = true;
  watchdog.join();
  std::fprintf(stderr, "[boot] entry point returned\n");
  runtime_shutdown();
  return 0;
}
