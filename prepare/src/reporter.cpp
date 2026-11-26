// SPDX-License-Identifier: GPL-3.0-or-later
#include "reporter.hpp"

#include <cmath>

namespace prep {

void Reporter::open_log(const fs::path& file) {
  std::lock_guard<std::mutex> lock(mu_);
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  if (fs::exists(file, ec) && fs::file_size(file, ec) > 4u * 1024 * 1024) {
    fs::path old = file;
    old += ".1";
    fs::rename(file, old, ec);
  }
  log_.open(file, std::ios::app);
  log_locked("=== tos-prepare run " + utc_now() + " ===");
}

void Reporter::log_locked(const std::string& text) {
  if (!log_.is_open()) return;
  log_ << text << '\n';
  log_.flush();
}

void Reporter::log(const std::string& text) {
  std::lock_guard<std::mutex> lock(mu_);
  log_locked(text);
}

void Reporter::line(const std::string& text) {
  // caller holds mu_
  std::fputs(text.c_str(), out_);
  std::fputc('\n', out_);
  std::fflush(out_);
  log_locked(text);
}

void Reporter::protocol(const std::string& version) {
  std::lock_guard<std::mutex> lock(mu_);
  line("protocol 1 tos-prepare " + version);
}

void Reporter::set_stage(const std::string& stage, int index, int count, const std::string& title, double base,
                         double span) {
  std::lock_guard<std::mutex> lock(mu_);
  stage_ = stage;
  base_ = base;
  span_ = span;
  bucket_ = -1;
  line("stage-start " + stage + " " + std::to_string(index) + "/" + std::to_string(count) + " " + one_line(title));
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.3f", base);
  line(std::string("progress ") + buf + " " + one_line(title));
}

void Reporter::stage_progress(std::uint64_t done, std::uint64_t total, const std::string& text) {
  std::lock_guard<std::mutex> lock(mu_);
  if (total == 0) return;
  if (done > total) done = total;
  // At most one line per percent of the stage, and always the last one.
  const long bucket = static_cast<long>(done * 100 / total);
  if (bucket == bucket_ && done != total) return;
  bucket_ = bucket;
  line("stage-progress " + stage_ + " " + std::to_string(done) + "/" + std::to_string(total) + " " + one_line(text));
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.3f", base_ + span_ * static_cast<double>(done) / static_cast<double>(total));
  line(std::string("progress ") + buf + " " + one_line(text));
}

void Reporter::stage_end(const std::string& status, double seconds) {
  std::lock_guard<std::mutex> lock(mu_);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", seconds);
  line("stage-end " + stage_ + " " + status + " " + buf);
}

void Reporter::warn(const std::string& text) {
  std::lock_guard<std::mutex> lock(mu_);
  line("warn " + one_line(text));
}

void Reporter::release(const std::string& id, const std::string& status, const std::string& title) {
  std::lock_guard<std::mutex> lock(mu_);
  line("release " + id + " " + status + " " + one_line(title));
}

void Reporter::result(const std::string& state, int code, const std::string& text) {
  std::lock_guard<std::mutex> lock(mu_);
  line("result " + state + " " + std::to_string(code) + " " + one_line(text));
}

}  // namespace prep
