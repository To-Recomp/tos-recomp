// SPDX-License-Identifier: GPL-3.0-or-later
#include "toolchain.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace prep {

namespace {

constexpr std::uint64_t kReserve = 128ull * 1000 * 1000;  // kept free besides the payload itself

bool is_id(const std::string& s) {
  if (s.size() != 16) return false;
  for (const char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// An environment variable as a path: Windows keeps them in UTF-16, and a user name with accents must reach us intact.
fs::path env_path(const char* name) {
#if defined(_WIN32)
  std::wstring wname;
  for (const char* c = name; *c; ++c) wname += static_cast<wchar_t>(*c);
  const DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
  if (n == 0) return {};
  std::wstring value(n, L'\0');
  const DWORD got = GetEnvironmentVariableW(wname.c_str(), value.data(), n);
  value.resize(got);
  return fs::path(value);
#else
  const char* v = std::getenv(name);
  return v && *v ? fs::path(v) : fs::path();
#endif
}

// Removes the other payloads' folders that nobody uses (a release replaced by a newer one).
void remove_stale(const fs::path& root, const std::string& keep) {
  std::error_code ec;
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    const std::string id = name.substr(0, name.find('.'));
    if (!is_id(id) || id == keep || !it->is_directory(ec)) continue;
    // `<id>` is removable when nobody holds its `.use` lock (shared by users); `<id>.partial` when nobody unpacks it.
    const bool partial = name.size() > 16;
    auto lock = FileLock::try_acquire(root / (id + (partial ? ".unpack" : ".use")), true);
    if (!lock) continue;
    std::error_code rm;
    fs::remove_all(it->path(), rm);
    if (rm) continue;  // Windows: a file still open somewhere; the next run tries again
    lock.reset();
    fs::remove(root / (id + ".use"), rm);
    fs::remove(root / (id + ".unpack"), rm);
  }
}

// Renames the finished folder into place. On Windows a virus scanner that is still reading the files just written
// makes renaming their folder fail for a moment ("access denied"): try again for a few seconds before giving up.
void rename_into_place(const fs::path& from, const fs::path& to, CancelToken& cancel) {
  std::error_code ec;
  for (int attempt = 0; attempt < 50; ++attempt) {
    fs::rename(from, to, ec);
    if (!ec) return;
    cancel.check();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  throw Error(kFailed, "cannot move the unpacked build tools into place (" + u8(from) + " -> " + u8(to) + "): " + ec.message());
}

}  // namespace

fs::path default_cache_root() {
  if (const fs::path d = env_path("TOS_RECOMP_CACHE"); !d.empty()) return d;
#if defined(_WIN32)
  if (const fs::path d = env_path("LOCALAPPDATA"); !d.empty()) return d / "tos-recomp";
#else
  if (const fs::path d = env_path("XDG_CACHE_HOME"); d.is_absolute()) return d / "tos-recomp";
  if (const fs::path h = env_path("HOME"); !h.empty()) return h / ".cache" / "tos-recomp";
#endif
  return {};
}

Toolchain ensure_toolchain(const PayloadInfo& payload, const std::vector<fs::path>& roots, CancelToken& cancel,
                           const PackProgress& progress,
                           const std::function<std::uint64_t(const fs::path&)>& free_space) {
  const std::string id = payload.id();
  std::string why;
  for (const fs::path& root : roots) {
    if (root.empty()) continue;
    std::error_code ec;
    fs::create_directories(root, ec);
    auto use = ec ? nullptr : FileLock::try_acquire(root / (id + ".use"), false);
    if (!use) {
      why += (why.empty() ? "" : "; ") + u8(root) + " is not writable";
      continue;
    }
    Toolchain tc;
    tc.root = root / from_u8(id);
    tc.in_use = std::move(use);
    if (unpacked_intact(tc.root)) {
      remove_stale(root, id);
      return tc;
    }

    // One unpacks, the others wait for the result.
    auto unpack_lock = FileLock::acquire(root / (id + ".unpack"), true, cancel);
    if (unpacked_intact(tc.root)) return tc;

    const std::uint64_t need = payload.unpacked_size + kReserve;
    const std::uint64_t have = free_space(root);
    if (have < need) {
      throw Error(kNoSpace, "not enough free disk space to unpack the build tools: " + human_bytes(payload.unpacked_size) +
                                " needed (plus " + human_bytes(kReserve) + " kept free), " + human_bytes(have) + " available in " + u8(root));
    }
    const fs::path partial = root / (id + ".partial");
    fs::remove_all(partial, ec);
    fs::remove_all(tc.root, ec);  // present but not intact
    try {
      const std::string manifest = unpack_payload(payload, partial, cancel, progress);
      write_text_atomic(partial / "MANIFEST", manifest);
      rename_into_place(partial, tc.root, cancel);
    } catch (...) {
      fs::remove_all(partial, ec);
      throw;
    }
    tc.unpacked_now = true;
    remove_stale(root, id);
    return tc;
  }
  throw Error(kNoToolchain, "no folder to unpack the build tools into (" + why + "); set TOS_RECOMP_CACHE to a writable folder");
}

}  // namespace prep
