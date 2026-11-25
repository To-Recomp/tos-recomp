// SPDX-License-Identifier: GPL-3.0-or-later
// The toolchain cache: where the payload of tos-prepare (payload.hpp) is unpacked on the player's machine.
//
//   <cache>/<id>/              the unpacked payload: toolkit/, toolchain/zig/, ... and MANIFEST (written last)
//   <cache>/<id>/zig-cache/    Zig's cache (its C++ library for the target is built once and kept there)
//   <cache>/<id>.use           shared lock, held while a tos-prepare uses <id>
//   <cache>/<id>.unpack        exclusive lock, held while one unpacks it
//   <cache>/<id>.partial/      the unpacking in progress; renamed to <id> when verified (atomic on one volume)
//
// <id> is the start of the payload's SHA-256, so a new release unpacks next to the old one and removes it afterwards
// (when nothing uses it). Two tos-prepare started together: one unpacks, the other waits and uses the result. The
// folders are only ever created and renamed, never modified in place, so an interrupted run leaves at worst a
// `.partial` folder that the next run deletes.
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "payload.hpp"

namespace prep {

struct Toolchain {
  fs::path root;                    // <cache>/<id>
  bool unpacked_now = false;        // false: it was already there
  std::unique_ptr<FileLock> in_use;  // keeps the folder from being removed by another run for as long as this lives
};

// The per-user folder: $TOS_RECOMP_CACHE, else $XDG_CACHE_HOME/tos-recomp, else ~/.cache/tos-recomp (Windows:
// %LOCALAPPDATA%\tos-recomp). Empty when none can be named.
fs::path default_cache_root();

// Makes the payload's tree available in the first of `roots` that can be written to and returns it. Unpacks when
// the folder is missing or does not match its MANIFEST. `free_space` gives the free bytes of a volume (the tests
// replace it). Throws Error(kNoSpace) when the volume is too small, Error(kNoToolchain) when no root can be used,
// Error(kFailed) when the payload is damaged, Cancelled.
Toolchain ensure_toolchain(const PayloadInfo& payload, const std::vector<fs::path>& roots, CancelToken& cancel,
                           const PackProgress& progress,
                           const std::function<std::uint64_t(const fs::path&)>& free_space);

}  // namespace prep
