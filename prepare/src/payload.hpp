// SPDX-License-Identifier: GPL-3.0-or-later
// The payload: a folder tree (the toolkit and the pinned Zig compiler, docs/RELEASING.md) compressed and appended to
// the tos-prepare executable, so that the release is ONE file that needs nothing installed on the player's machine.
//
// File layout of a tos-prepare that carries a payload:
//
//   [ executable as the linker wrote it ][ packed stream ][ trailer, 64 bytes ][ certificate table, if signed ]
//
//   packed stream  an .xz file (LZMA2, CRC64) of the archive stream below
//   archive stream "TOSA" u32 version(1), then entries, then an end entry; integers little-endian:
//                    u8 kind (1 file, 2 directory, 0 end)
//                    u16 path length, path bytes (UTF-8, '/' separators, relative, no "." or ".." component)
//                    u8 flags (bit 0: executable)           -- files only
//                    u64 size, size bytes                   -- files only
//   trailer        char[8] "TOSPAYLD", u32 version(1), u32 0, u64 packed size, u64 archive stream size,
//                  u8[32] SHA-256 of the packed stream
//
// Why a trailer and not a linker section or a resource: the payload is added to the finished executable by
// `tos-payload attach` (the same executable is also built without one for development and the tests), and it is found
// from the end of the file. `strip` rewrites an ELF file and drops the bytes it does not know, so a release is
// stripped BEFORE the payload is attached (scripts/package-release.sh); Windows code signing (Authenticode) appends
// its certificate table after the overlay, which is why the end is looked up in the PE header (payload_end), not
// taken as the file size. A tos-prepare with a payload is a valid program for the OS loader: the OS ignores the bytes
// after the last section.
//
// The payload is unpacked once per user into a cache folder named after the payload's SHA-256 (toolchain.hpp).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "common.hpp"
#include "sha256.hpp"

namespace prep {

constexpr std::size_t kTrailerSize = 64;

struct PayloadInfo {
  fs::path file;                 // the executable that carries it
  std::uint64_t offset = 0;      // where the packed stream starts
  std::uint64_t packed_size = 0;
  std::uint64_t unpacked_size = 0;  // size of the archive stream, a little over the sum of the file sizes
  Sha256::Digest sha256{};          // of the packed stream
  std::string id() const;           // the first 16 hex digits of the SHA-256: the cache folder's name
  std::string sha256_hex() const { return Sha256::hex(sha256); }
};

// The end of the program proper: the file size, or the start of the certificate table when `file` is a signed PE
// executable. Reads the headers only.
std::uint64_t payload_end(const fs::path& file);

// The payload carried by `exe`; nullopt when it has none (no trailer). Throws Error(kFailed) when a trailer is
// there but not credible (sizes beyond the file).
std::optional<PayloadInfo> find_payload(const fs::path& exe);

// Compression of the packed stream.
struct PackOptions {
  unsigned preset = 9;       // xz preset 0..9 (9 = 64 MiB dictionary)
  bool extreme = true;       // xz --extreme: slower, a little smaller
  unsigned threads = 1;      // 1: one stream, the smallest output; 0: all cores (blocks are compressed apart: larger output, more memory)
};
using PackProgress = std::function<void(std::uint64_t done_bytes, std::uint64_t total_bytes)>;

// Writes `out`: the bytes of `base_exe` followed by the packed stream of the folder `dir` and the trailer. `base_exe`
// may be a program that already carries a payload (it is replaced). Symlinks and special files are refused: nothing
// in a release needs them and Windows has no portable equivalent. Returns what was written.
PayloadInfo pack_payload(const fs::path& dir, const fs::path& base_exe, const fs::path& out, const PackOptions& opt,
                         const PackProgress& progress = {});

// Unpacks the payload into `dest` (created; it must not hold anything the caller wants to keep): decompresses while
// computing the SHA-256 of the packed stream, and throws Error(kFailed) at the end when it differs from the trailer's
// (the caller then discards `dest`). `progress` receives the bytes of the archive stream written so far.
// Returns the manifest: "<size> <path>" lines of every file, sorted (also written to <dest>/MANIFEST by the caller).
std::string unpack_payload(const PayloadInfo& info, const fs::path& dest, CancelToken& cancel, const PackProgress& progress = {});

// True when `root` holds a complete unpacked payload: MANIFEST exists and every file it lists is there with the
// size it lists. A stat per file: fast enough for every run.
bool unpacked_intact(const fs::path& root);

// Is a safe relative path: non-empty, '/'-separated, no empty, "." or ".." component, no ':' or '\\', no leading '/'.
bool safe_relative_path(const std::string& p);

}  // namespace prep
