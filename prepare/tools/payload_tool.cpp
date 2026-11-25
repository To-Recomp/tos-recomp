// SPDX-License-Identifier: GPL-3.0-or-later
// tos-payload: adds a payload (the toolkit and the pinned Zig compiler, src/payload.hpp) to a tos-prepare executable,
// and inspects or unpacks one. Used by scripts/package-release.sh; it runs on the machine that builds the release.
//
//   tos-payload attach --exe tos-prepare --dir PAYLOAD_DIR --out OUT [--preset N] [--no-extreme] [--threads N]
//       OUT = the bytes of --exe, then PAYLOAD_DIR packed, then the trailer. Strip the program first (a later `strip`
//       would remove the payload); on Windows sign it after (the signature goes behind the payload). --threads: 1
//       (default) one stream, the smallest output; 0 all cores, faster and larger.
//   tos-payload info FILE            says whether FILE carries a payload, its sizes and SHA-256 (exit 1: none)
//   tos-payload extract FILE --out DIR   unpacks the payload into DIR (as tos-prepare would, MANIFEST included)
//
// Paths are taken as given (UTF-8 on Linux; on Windows the ANSI code page, which is enough for a build machine).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "payload.hpp"

using namespace prep;

namespace {

void usage(std::FILE* f) {
  std::fputs(
      "usage: tos-payload attach --exe PROGRAM --dir PAYLOAD_DIR --out OUTPUT [--preset 0..9] [--no-extreme] [--threads N]\n"
      "       tos-payload info FILE\n"
      "       tos-payload extract FILE --out DIR\n",
      f);
}

int bad_usage(const char* why) {
  std::fprintf(stderr, "tos-payload: %s\n", why);
  usage(stderr);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return bad_usage("a command is required");
  const std::string cmd = argv[1];
  if (cmd == "--help" || cmd == "-h") { usage(stdout); return 0; }
  fs::path exe, dir, out, file;
  PackOptions opt;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--exe" || a == "--dir" || a == "--out" || a == "--preset" || a == "--threads") {
      const char* v = value();
      if (!v) return bad_usage((a + " needs a value").c_str());
      if (a == "--exe") exe = from_u8(v);
      else if (a == "--dir") dir = from_u8(v);
      else if (a == "--out") out = from_u8(v);
      else if (a == "--preset") opt.preset = static_cast<unsigned>(std::atoi(v));
      else opt.threads = static_cast<unsigned>(std::atoi(v));
    } else if (a == "--no-extreme") {
      opt.extreme = false;
    } else if (!a.empty() && a[0] != '-' && file.empty()) {
      file = from_u8(a);
    } else {
      return bad_usage(("unknown argument " + a).c_str());
    }
  }
  if (opt.preset > 9) return bad_usage("--preset is 0 to 9");
  try {
    if (cmd == "attach") {
      if (exe.empty() || dir.empty() || out.empty()) return bad_usage("attach needs --exe, --dir and --out");
      std::uint64_t last_percent = 101;
      const PayloadInfo info = pack_payload(dir, exe, out, opt, [&](std::uint64_t done, std::uint64_t total) {
        const std::uint64_t pct = total ? done * 100 / total : 100;
        if (pct != last_percent && pct % 10 == 0) std::fprintf(stderr, "tos-payload: %llu%%\n", static_cast<unsigned long long>(pct));
        last_percent = pct;
      });
      std::printf("payload %s: %s packed, %s unpacked, sha256 %s\n", u8(out).c_str(), human_bytes(info.packed_size).c_str(),
                  human_bytes(info.unpacked_size).c_str(), info.sha256_hex().c_str());
      return 0;
    }
    if (cmd == "info") {
      if (file.empty()) return bad_usage("info needs a file");
      const auto info = find_payload(file);
      if (!info) { std::printf("no payload in %s\n", u8(file).c_str()); return 1; }
      std::printf("payload at %llu: %llu bytes packed, %llu unpacked, sha256 %s, id %s\n", static_cast<unsigned long long>(info->offset),
                  static_cast<unsigned long long>(info->packed_size), static_cast<unsigned long long>(info->unpacked_size),
                  info->sha256_hex().c_str(), info->id().c_str());
      return 0;
    }
    if (cmd == "extract") {
      if (file.empty() || out.empty()) return bad_usage("extract needs a file and --out");
      const auto info = find_payload(file);
      if (!info) { std::fprintf(stderr, "tos-payload: no payload in %s\n", u8(file).c_str()); return 1; }
      CancelToken never;
      const std::string manifest = unpack_payload(*info, out, never);
      write_text_atomic(out / "MANIFEST", manifest);
      std::printf("unpacked %s into %s\n", human_bytes(info->unpacked_size).c_str(), u8(out).c_str());
      return 0;
    }
    return bad_usage("unknown command");
  } catch (const std::exception& e) {
    std::fprintf(stderr, "tos-payload: %s\n", e.what());
    return 1;
  }
}
