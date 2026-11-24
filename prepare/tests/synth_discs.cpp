// SPDX-License-Identifier: GPL-3.0-or-later
// tos-synth-discs: writes two tiny synthetic GameCube disc images, the dumps.toml that makes them "known dumps",
// and minimal symbol files, into a folder. They carry no game data: sys/main.dol is a four-instruction program.
// Used by the release smoke test (tests/release-smoke.sh) to run the real tos-prepare, recompiler, compiler and
// runtime libraries end to end without a disc.
//
//   tos-synth-discs --out DIR [--game-id GQSF]
//
// writes DIR/disc1.iso, DIR/disc2.iso, DIR/dumps.toml, DIR/symbols/<id>.csv and DIR/symbols/<id>.extra_funcs.txt.
// The program returns 42 from the game's entry point (0x80003154 for GQSF's boot harness).
#include <cstdio>
#include <cstring>
#include <fstream>

#include "synth_disc.hpp"

namespace fs = std::filesystem;

static synth::Bytes make_dol() {
  synth::Bytes dol(0x100 + 0x80, 0);
  synth::put32(dol, 0x00, 0x100);        // text section 0: file offset, address, size
  synth::put32(dol, 0x48, 0x80003100);
  synth::put32(dol, 0x90, 0x80);
  synth::put32(dol, 0xD8, 0x80004000);   // a small BSS, as the harness clears it
  synth::put32(dol, 0xDC, 0x100);
  synth::put32(dol, 0xE0, 0x80003154);   // entry point
  // 0x80003154: li r3, 42 ; blr
  synth::put32(dol, 0x100 + 0x54, 0x3860002A);
  synth::put32(dol, 0x100 + 0x58, 0x4E800020);
  return dol;
}

static void write(const fs::path& p, const void* data, std::size_t size) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  if (!out) {
    std::fprintf(stderr, "tos-synth-discs: cannot write %s\n", p.string().c_str());
    std::exit(1);
  }
}

static void write(const fs::path& p, const std::string& text) { write(p, text.data(), text.size()); }

int main(int argc, char** argv) {
  fs::path out;
  std::string id = "GQSF";
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) out = fs::u8path(argv[++i]);
    else if (a == "--game-id" && i + 1 < argc) id = argv[++i];
    else {
      std::fprintf(stderr, "usage: tos-synth-discs --out DIR [--game-id GQSF]\n");
      return a == "--help" ? 0 : 2;
    }
  }
  if (out.empty() || id.size() != 4) {
    std::fprintf(stderr, "usage: tos-synth-discs --out DIR [--game-id GQSF]  (the id has four characters)\n");
    return 2;
  }
  synth::Node d1, d2;
  synth::add_file(d1, "shared/a.bin", std::string(3000, 'a'));
  synth::add_file(d1, "only1.bin", "only on disc one");
  d2 = d1;
  d2.kids.erase("only1.bin");
  synth::add_file(d2, "ev/only2.bin", std::string(5000, 'e'));
  const synth::Bytes dol = make_dol();
  const synth::Bytes i1 = synth::build_image(d1, id.c_str(), 0, &dol);
  const synth::Bytes i2 = synth::build_image(d2, id.c_str(), 1, &dol);
  write(out / "disc1.iso", i1.data(), i1.size());
  write(out / "disc2.iso", i2.data(), i2.size());
  write(out / "dumps.toml", synth::dumps_toml(id, {out / "disc1.iso", out / "disc2.iso"}));
  write(out / "symbols" / (id + ".csv"), "# synthetic: no symbols\n");
  write(out / "symbols" / (id + ".extra_funcs.txt"), "# synthetic: no extra functions\n");
  std::printf("wrote %s\n", out.generic_string().c_str());
  return 0;
}
