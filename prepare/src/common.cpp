// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.hpp"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

#include "torecomp/sha1.hpp"

namespace prep {

namespace {
volatile std::sig_atomic_t g_signal = 0;
void on_signal(int sig) { g_signal = sig; }
}  // namespace

bool CancelToken::poll() {
  if (flag_.load()) return true;
  if (g_signal != 0) {
    flag_.store(true);
    return true;
  }
  if (!file_.empty()) {
    std::error_code ec;
    if (fs::exists(file_, ec)) {
      flag_.store(true);
      return true;
    }
  }
  return false;
}

void install_signal_handlers(CancelToken&) {
  struct sigaction sa {};
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // no SA_RESTART: blocking reads return and see the flag
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

std::string sha1_file(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw Error(kFailed, "cannot read " + file.string());
  torecomp::Sha1 h;
  std::vector<char> buf(1 << 20);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    h.update(buf.data(), static_cast<std::size_t>(in.gcount()));
  }
  return torecomp::Sha1::hex(h.finish());
}

std::string sha1_text(const std::string& text) { return torecomp::Sha1::hex_of(text.data(), text.size()); }

std::string read_text(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw Error(kFailed, "cannot read " + file.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_text_atomic(const fs::path& file, const std::string& text) {
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    if (!out) throw Error(kFailed, "cannot write " + tmp.string());
  }
  std::error_code ec;
  fs::rename(tmp, file, ec);
  if (ec) throw Error(kFailed, "cannot rename " + tmp.string() + ": " + ec.message());
}

std::string utc_now() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string human_bytes(std::uint64_t b) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(b);
  int u = 0;
  while (v >= 1000 && u < 4) {
    v /= 1000;
    ++u;
  }
  char buf[32];
  std::snprintf(buf, sizeof buf, u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
  return buf;
}

std::uint64_t free_bytes(const fs::path& dir) {
  fs::path p = fs::absolute(dir);
  std::error_code ec;
  while (!p.empty() && !fs::exists(p, ec)) {
    if (p == p.parent_path()) break;
    p = p.parent_path();
  }
  const auto info = fs::space(p, ec);
  if (ec) throw Error(kFailed, "cannot read the free space of " + p.string() + ": " + ec.message());
  return info.available;
}

std::uint64_t tree_bytes(const fs::path& dir) {
  std::uint64_t total = 0;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return 0;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2)) total += it->file_size(e2);
  }
  return total;
}

std::string one_line(std::string text) {
  for (char& c : text) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return text;
}

}  // namespace prep
