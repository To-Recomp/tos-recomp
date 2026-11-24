// SPDX-License-Identifier: GPL-3.0-or-later
#include "toolkit.hpp"

#include <algorithm>

#include "process.hpp"

namespace prep {

namespace {

std::vector<std::string> strings(const Json& j) {
  std::vector<std::string> out;
  for (const auto& v : j.items()) out.push_back(v.str());
  return out;
}

fs::path need_file(const fs::path& dir, const std::string& rel, const char* what) {
  if (rel.empty()) throw Error(kNoToolchain, std::string("toolkit.json does not name the ") + what);
  const fs::path p = dir / from_u8(rel);
  std::error_code ec;
  if (!fs::exists(p, ec)) {
    throw Error(kNoToolchain, std::string("the toolkit's ") + what + " is missing: " + u8(p));
  }
  return p;
}

}  // namespace

Toolkit Toolkit::load(const fs::path& dir, const fs::path& symbols_dir, const fs::path& dumps_override) {
  Toolkit tk;
  tk.dir = dir;
  const fs::path file = dir / "toolkit.json";
  std::error_code ec;
  if (!fs::exists(file, ec)) {
    throw Error(kNoToolchain, "no toolkit found: " + u8(file) +
                                  " is missing (use --toolkit, see scripts/make-toolkit.sh)");
  }
  try {
    tk.json = Json::parse(read_text(file));
  } catch (const std::exception& e) {
    throw Error(kNoToolchain, "bad " + u8(file) + ": " + e.what());
  }
  const Json& j = tk.json;
  if (j.get("format").num() != 1) throw Error(kNoToolchain, u8(file) + ": unsupported format");
  tk.game_id = j.get("game_id").str();
  tk.project = j.get("project").str("game");
  tk.dumps = dumps_override.empty() ? need_file(dir, j.get("dumps").str(), "dumps.toml") : dumps_override;
  if (!fs::exists(tk.dumps, ec)) throw Error(kNoToolchain, "dumps file not found: " + u8(tk.dumps));
  tk.recompiler = need_file(dir, j.get("recompiler").str(), "recompiler");
  const Json& sym = j.get("symbols");
  const fs::path sdir = symbols_dir.empty() ? dir / from_u8(sym.get("dir").str("symbols")) : symbols_dir;
  tk.symbols_csv = sdir / from_u8(sym.get("csv").str());
  tk.symbols_extra = sdir / from_u8(sym.get("extra_funcs").str());
  for (const auto& f : {tk.symbols_csv, tk.symbols_extra}) {
    if (!fs::exists(f, ec)) throw Error(kNoToolchain, "symbols file not found: " + u8(f));
  }
  const Json& build = j.get("build");
  for (const auto& s : strings(build.get("include"))) tk.include_dirs.push_back(need_file(dir, s, "include folder"));
  for (const auto& s : strings(build.get("sources"))) tk.sources.push_back(need_file(dir, s, "harness source"));
  for (const auto& s : strings(build.get("libs"))) tk.libs.push_back(need_file(dir, s, "runtime library"));
  tk.system_libs = strings(build.get("system_libs"));
  tk.flags = strings(build.get("flags"));
  tk.std_flag = build.get("std").str("gnu++20");
  tk.compilers = strings(build.get("compilers"));
  tk.executable = build.get("executable").str("game");
  if (tk.game_id.empty() || tk.sources.empty() || tk.libs.empty()) {
    throw Error(kNoToolchain, u8(file) + ": game_id, build.sources and build.libs are required");
  }
  return tk;
}

std::string Toolkit::content_hash() const {
  std::string acc = sha1_text(json.dump(0)) + "\n";
  for (const auto& f : libs) acc += sha1_file(f) + "\n";
  for (const auto& f : sources) acc += sha1_file(f) + "\n";
  std::vector<fs::path> headers;
  for (const auto& d : include_dirs) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_regular_file(ec)) headers.push_back(it->path());
    }
  }
  std::sort(headers.begin(), headers.end());
  for (const auto& h : headers) acc += fs::relative(h, dir).generic_u8string() + " " + sha1_file(h) + "\n";
  return sha1_text(acc);
}

std::vector<std::string> resolve_compiler(const Toolkit& tk, const std::string& requested, const fs::path& program_dir) {
  const std::string placeholder = "{program_dir}";
  auto try_command = [&](const std::string& command) -> std::vector<std::string> {
    auto argv = split_command(command);
    if (argv.empty()) return {};
    // A bundled compiler is named relative to the folder of tos-prepare; substituted per word so that a folder
    // name with spaces stays one word.
    for (auto& w : argv) {
      const std::size_t at = w.find(placeholder);
      if (at != std::string::npos) {
        if (program_dir.empty()) return {};
        w.replace(at, placeholder.size(), u8(program_dir));
      }
    }
    const std::string exe = find_in_path(argv[0]);
    if (exe.empty()) return {};
    argv[0] = exe;
    return argv;
  };
  if (!requested.empty()) {
    auto argv = try_command(requested);
    if (argv.empty()) throw Error(kNoToolchain, "the compiler '" + requested + "' was not found or is not executable");
    return argv;
  }
  if (const char* cxx = std::getenv("CXX"); cxx && *cxx) {
    auto argv = try_command(cxx);
    if (!argv.empty()) return argv;
  }
  for (const auto& c : tk.compilers) {
    auto argv = try_command(c);
    if (!argv.empty()) return argv;
  }
  std::string names;
  for (const auto& c : tk.compilers) names += (names.empty() ? "" : ", ") + c;
  throw Error(kNoToolchain, "no C++ compiler found (looked for " + names +
                                "); install clang or g++, or pass --compiler PATH");
}

}  // namespace prep
