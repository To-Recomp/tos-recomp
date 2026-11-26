// SPDX-License-Identifier: GPL-3.0-or-later
#include "stages.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include "process.hpp"
#include "torecomp/content.hpp"
#include "torecomp/dumps.hpp"
#include "torecomp/gcm.hpp"
#include "torecomp/hostpath.hpp"

namespace prep {

namespace {

constexpr const char* kExtractLayout = "extract-layout-1";
constexpr const char* kRecompileScheme = "recompile-1";
constexpr const char* kCompileScheme = "compile-1";

fs::path under(const Context& ctx, const char* rel) { return ctx.data() / rel; }

void remove_tree(const fs::path& p) {
  std::error_code ec;
  fs::remove_all(torecomp::hostpath::long_path(p), ec);
}

std::string join(const std::vector<std::string>& v, const char* sep = " ") {
  std::string s;
  for (const auto& x : v) s += (s.empty() ? "" : sep) + x;
  return s;
}

// How to find the verified image of a disc: its Redump serial, and the entry when known.
std::string redump_name(const torecomp::DiscRecord& d) {
  std::string s = d.serial.empty() ? "'" + d.title + "'" : "Redump " + d.serial;
  if (d.redump_id) s += " (redump.org/disc/" + std::to_string(*d.redump_id) + ")";
  return s;
}

// ---------------------------------------------------------------------------------------------
// verify

class VerifyStage : public Stage {
 public:
  explicit VerifyStage(std::shared_ptr<Plan> plan) : plan_(std::move(plan)) {}
  std::string name() const override { return "verify"; }
  std::string title() const override { return "Checking the disc images"; }
  double weight() const override { return 0.10; }

  std::string input_hash(Context& ctx) override {
    if (ctx.opt.discs.empty()) {
      // Rebuild without the discs: what was verified before stands, if there is a record of it.
      StageRecord* rec = ctx.state.find(name());
      if (rec && rec->status == "done") return rec->input;
      throw Error(kUsage, "no disc images given and nothing was verified before: pass both discs");
    }
    std::string acc = "verify-1\n" + sha1_file(plan_->toolkit.dumps) + "\n";
    for (const auto& d : ctx.opt.discs) {
      std::error_code ec;
      const auto size = fs::file_size(d, ec);
      if (ec) throw Error(kBadDump, "cannot read the disc image " + u8(d) + ": " + ec.message());
      const auto mtime = static_cast<long long>(fs::last_write_time(d, ec).time_since_epoch().count());
      acc += u8(fs::absolute(d)) + " " + std::to_string(size) + " " + std::to_string(mtime) + "\n";
    }
    return sha1_text(acc);
  }
  bool outputs_intact(Context& ctx) override {
    StageRecord* rec = ctx.state.find(name());
    return rec && rec->info.get("discs").is_array() && !rec->info.get("discs").items().empty();
  }
  std::uint64_t bytes_needed(Context&) override { return 0; }
  void clean_partial(Context&) override {}

  void run(Context& ctx) override {
    torecomp::Database db;
    try {
      db = torecomp::Database::load(plan_->toolkit.dumps);
    } catch (const std::exception& e) {
      throw Error(kNoToolchain, std::string("cannot read the dump database: ") + e.what());
    }
    struct Found {
      fs::path path;
      std::string format;
      torecomp::Identification id;
      std::uint64_t bytes = 0, dol_bytes = 0, rel_bytes = 0;
    };
    std::vector<Found> found;
    const std::size_t n = ctx.opt.discs.size();
    std::uint64_t last = 0;
    for (std::size_t i = 0; i < n; ++i) {
      Found f;
      f.path = ctx.opt.discs[i];
      try {
        torecomp::gcm::Image img(f.path);
        f.format = img.format();
        auto progress = [&, i](std::uint64_t done, std::uint64_t total) {
          ctx.cancel.check();
          const std::uint64_t v = i * 1000 + (total ? done * 1000 / total : 1000);
          last = std::max(last, v);
          ctx.rep.stage_progress(last, n * 1000, "Checking disc " + std::to_string(i + 1) + " of " + std::to_string(n));
        };
        f.id = torecomp::identify(db, img, progress);
        f.bytes = img.main_dol().size + img.apploader().size + 0x2440;
        f.dol_bytes = img.main_dol().size;
        for (const auto& e : img.files()) {
          f.bytes += e.size;
          if (e.path.find('/') == std::string::npos && e.path.size() > 4 && e.path.compare(e.path.size() - 4, 4, ".rel") == 0) {
            f.rel_bytes += e.size;
          }
        }
        if (!f.id.release || !f.id.disc) {
          throw Error(kBadDump, u8(f.path) + " is not a recognised dump (it is not a known dump of " + plan_->toolkit.game_id + ")" +
                                    (f.id.sha1.empty() ? std::string() : ". whole-image SHA-1 " + f.id.sha1));
        }
        const auto& dd = *f.id.disc;
        if (f.id.level == torecomp::Level::Unknown) {
          // The header names a known release but the game files differ (another dump, scrubbed, rebuilt, damaged):
          // not blocking, the player may know what they are doing. The build goes on, the player is told which
          // image to look for.
          ctx.rep.warn(u8(f.path) + " is not the retail data of '" + dd.title + "' (its game files differ from the known dump), "
                       "the build may fail or the game may not work. The verified image is " + redump_name(dd) + ", " +
                       std::to_string(dd.size) + " bytes. whole-image SHA-1 " + f.id.sha1 + ", content_sha1 " + f.id.content_sha1);
        }
        if (dd.game_id != plan_->toolkit.game_id) {
          ctx.rep.warn(u8(f.path) + " is " + f.id.release->title + " (" + dd.game_id + "), this build is made for " +
                       plan_->toolkit.game_id + ": the recompiled code and the symbols are those of " + plan_->toolkit.game_id +
                       ", it will probably not work.");
        }
        ctx.rep.log(u8(f.path) + ": " + torecomp::to_string(f.id.level) + ", " + f.id.release->title + ", disc " +
                    std::to_string(f.id.disc->disc_number + 1) + ", " + f.format);
      } catch (const torecomp::Error& e) {
        throw Error(kBadDump, u8(f.path) + ": " + e.what());
      }
      found.push_back(std::move(f));
    }

    for (std::size_t i = 1; i < found.size(); ++i) {
      if (found[i].id.release != found[0].id.release) {
        throw Error(kBadDump, "the disc images belong to different releases: '" + found[0].id.release->title + "' and '" +
                                  found[i].id.release->title + "'");
      }
    }
    std::vector<torecomp::Identification> ids;
    for (const auto& f : found) ids.push_back(f.id);
    // An unverified image still stands for the disc its header names.
    for (auto& id : ids) {
      if (id.level == torecomp::Level::Unknown) id.level = torecomp::Level::Compatible;
    }
    const auto missing = torecomp::missing_discs(*found[0].id.release, ids);
    if (!missing.empty()) {
      std::string list;
      for (unsigned m : missing) list += (list.empty() ? "" : ", ") + std::to_string(m + 1);
      throw Error(kBadDump, "missing disc " + list + " of '" + found[0].id.release->title +
                                "' (or two of the images are the same disc)");
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
      return a.id.disc->disc_number < b.id.disc->disc_number;
    });
    if (found[0].path != ctx.opt.discs[0]) ctx.rep.warn("the disc images were given in the wrong order; using disc 1 first");

    Json info = Json::object();
    Json discs = Json::array();
    for (const auto& f : found) {
      Json d = Json::object();
      d["disc"] = static_cast<int>(f.id.disc->disc_number + 1);
      d["file"] = u8(fs::absolute(f.path));
      d["format"] = f.format;
      d["level"] = f.id.level == torecomp::Level::RedumpVerified ? "redump-verified"
                   : f.id.level == torecomp::Level::Compatible ? "compatible" : "unverified";
      d["sha1"] = f.id.sha1;
      d["content_sha1"] = f.id.level == torecomp::Level::Unknown ? f.id.content_sha1 : f.id.disc->content_sha1;
      d["bytes"] = f.bytes;
      d["dol_bytes"] = f.dol_bytes;
      d["rel_bytes"] = f.rel_bytes;
      discs.push(std::move(d));
    }
    info["discs"] = std::move(discs);
    info["release"] = found[0].id.release->id;
    info["title"] = found[0].id.release->title;
    ctx.state.at(name()).info = std::move(info);
    ctx.state.meta["game_id"] = plan_->toolkit.game_id;
    ctx.state.meta["release"] = found[0].id.release->title;
  }

 private:
  std::shared_ptr<Plan> plan_;
};

// ---------------------------------------------------------------------------------------------
// extract

struct ManifestEntry {
  std::string path, host, sha1;
  std::uint64_t size = 0;
  int disc = 1;
};

std::string manifest_text(const std::vector<ManifestEntry>& entries, const std::string& dol_sha1,
                          const std::vector<std::pair<std::string, std::string>>& rels) {
  std::ostringstream o;
  o << "dol\t" << dol_sha1 << "\n";
  for (const auto& r : rels) o << "rel\t" << r.first << "\t" << r.second << "\n";
  for (const auto& e : entries) o << "file\t" << e.size << "\t" << e.sha1 << "\t" << e.path << "\t" << e.host << "\t" << e.disc << "\n";
  return o.str();
}

class ExtractStage : public Stage {
 public:
  explicit ExtractStage(std::shared_ptr<Plan> plan) : plan_(std::move(plan)) {}
  std::string name() const override { return "extract"; }
  std::string title() const override { return "Copying the game files from the discs"; }
  double weight() const override { return 0.25; }

  std::string input_hash(Context& ctx) override {
    std::string acc = std::string(kExtractLayout) + "\n";
    for (const auto& d : ctx.state.at("verify").info.get("discs").items()) acc += d.get("content_sha1").str() + "\n";
    return sha1_text(acc);
  }
  bool outputs_intact(Context& ctx) override {
    const fs::path game = under(ctx, layout::kGame);
    std::error_code ec;
    std::ifstream in(game / "manifest.tsv");
    if (!in) return false;
    std::string line;
    std::size_t count = 0;
    while (std::getline(in, line)) {
      std::istringstream ls(line);
      std::string kind;
      std::getline(ls, kind, '\t');
      if (kind != "file") continue;
      std::string size, sha, path, host;
      std::getline(ls, size, '\t');
      std::getline(ls, sha, '\t');
      std::getline(ls, path, '\t');
      std::getline(ls, host, '\t');
      const fs::path f = game / "files" / torecomp::hostpath::to_fs(host);
      if (!fs::is_regular_file(f, ec) || fs::file_size(f, ec) != std::stoull(size)) return false;
      ++count;
    }
    return count > 0 && fs::exists(game / "sys" / "main.dol", ec);
  }
  std::uint64_t bytes_needed(Context& ctx) override {
    // Peak: disc 1 kept + disc 2 staged before the merge, then what the later stages write.
    std::uint64_t need = 0;
    for (const auto& d : ctx.state.at("verify").info.get("discs").items()) need += static_cast<std::uint64_t>(d.get("bytes").num());
    return need + later_stages_bytes(ctx);
  }
  void clean_partial(Context& ctx) override {
    remove_tree(under(ctx, layout::kGamePartial));
    remove_tree(under(ctx, layout::kWork));
  }

  static std::uint64_t later_stages_bytes(Context& ctx) {
    const Json& disk = ctx.toolkit.get("disk");
    std::uint64_t input = 0;
    const auto& discs = ctx.state.at("verify").info.get("discs").items();
    if (!discs.empty()) input = static_cast<std::uint64_t>(discs[0].get("dol_bytes").num() + discs[0].get("rel_bytes").num());
    const double gen = disk.get("generated_per_input_byte").num(0) * static_cast<double>(input);
    return static_cast<std::uint64_t>(gen + gen * disk.get("objects_per_generated_byte").num(0) + disk.get("executable_bytes").num(0));
  }

  void run(Context& ctx) override {
    const auto& discs = ctx.state.at("verify").info.get("discs").items();
    if (ctx.opt.discs.empty()) throw Error(kUsage, "the extracted game files are missing or changed: pass both discs to extract them again");
    // Map recorded disc number -> the file the user gave (verify may have reordered them).
    std::map<int, fs::path> files;
    for (const auto& d : discs) files[static_cast<int>(d.get("disc").num())] = d.get("file").str();
    std::uint64_t total = 0;
    for (const auto& d : discs) total += static_cast<std::uint64_t>(d.get("bytes").num());

    const fs::path partial = under(ctx, layout::kGamePartial);
    const fs::path work = under(ctx, layout::kWork);
    remove_tree(partial);
    remove_tree(work);
    std::error_code ec;
    fs::create_directories(ctx.data(), ec);

    std::vector<ManifestEntry> entries;
    std::string dol_sha1;
    std::vector<std::pair<std::string, std::string>> rels;
    std::uint64_t offset = 0;
    std::vector<std::string> conflicts;
    try {
      for (const auto& d : discs) {
        const int number = static_cast<int>(d.get("disc").num());
        const fs::path target = number == 1 ? partial : work / "disc2";
        torecomp::gcm::Image img(files[number]);
        auto progress = [&](std::uint64_t done, std::uint64_t t) {
          ctx.cancel.check();
          (void)t;
          ctx.rep.stage_progress(offset + done, total, "Copying the files of disc " + std::to_string(number));
        };
        torecomp::Manifest m = torecomp::extract(img, target, d.get("content_sha1").str(), progress);
        offset += static_cast<std::uint64_t>(d.get("bytes").num());

        if (number == 1) {
          for (const auto& l : m.lines) {
            if (l.kind == "dol") dol_sha1 = l.sha1;
            if (l.kind != "file") continue;
            entries.push_back({l.path, l.host_path.empty() ? l.path : l.host_path, l.sha1, l.size, 1});
            if (l.path.find('/') == std::string::npos && l.path.size() > 4 && l.path.compare(l.path.size() - 4, 4, ".rel") == 0) {
              rels.emplace_back(l.path, l.sha1);
            }
          }
        } else {
          // Merge: what disc 1 already has is dropped, what only this disc has moves into place.
          std::map<std::string, std::size_t> have;
          for (std::size_t i = 0; i < entries.size(); ++i) have[entries[i].path] = i;
          for (const auto& l : m.lines) {
            if (l.kind != "file") continue;
            const std::string host = l.host_path.empty() ? l.path : l.host_path;
            auto it = have.find(l.path);
            if (it != have.end()) {
              if (entries[it->second].sha1 != l.sha1) conflicts.push_back(l.path);
              continue;
            }
            const fs::path from = work / "disc2" / "files" / torecomp::hostpath::to_fs(host);
            const fs::path to = partial / "files" / torecomp::hostpath::to_fs(host);
            if (fs::exists(torecomp::hostpath::long_path(to), ec)) {
              throw Error(kExtractFailed, "disc 2 file " + l.path + " collides with another file on this file system");
            }
            fs::create_directories(torecomp::hostpath::long_path(to).parent_path(), ec);
            fs::rename(torecomp::hostpath::long_path(from), torecomp::hostpath::long_path(to), ec);
            if (ec) throw Error(kExtractFailed, "cannot move " + u8(from) + ": " + ec.message());
            entries.push_back({l.path, host, l.sha1, l.size, 2});
          }
        }
      }
      std::sort(entries.begin(), entries.end(), [](const ManifestEntry& a, const ManifestEntry& b) { return a.path < b.path; });
      std::ostringstream paths;
      bool identity = true;
      for (const auto& e : entries) {
        paths << e.path << "\t" << e.host << "\n";
        identity = identity && e.path == e.host;
      }
      write_text_atomic(partial / "paths.tsv", paths.str());
      write_text_atomic(partial / "manifest.tsv", manifest_text(entries, dol_sha1, rels));
      if (!identity) {
        ctx.rep.warn("some disc file names were renamed for this file system (see game/paths.tsv); the game looks files up by their disc name");
      }
      remove_tree(work);
      remove_tree(under(ctx, layout::kGame));
      fs::rename(partial, under(ctx, layout::kGame), ec);
      if (ec) throw Error(kExtractFailed, "cannot move the extracted files into place: " + ec.message());
    } catch (const torecomp::Error& e) {
      throw Error(kExtractFailed, std::string("extracting the discs failed: ") + e.what());
    } catch (const fs::filesystem_error& e) {
      throw Error(kExtractFailed, std::string("extracting the discs failed: ") + e.what());
    }

    Json info = Json::object();
    info["files"] = static_cast<std::uint64_t>(entries.size());
    info["dol_sha1"] = dol_sha1;
    Json r = Json::array();
    std::uint64_t input = 0;
    for (const auto& [n, s] : rels) {
      Json j = Json::object();
      j["name"] = n;
      j["sha1"] = s;
      r.push(std::move(j));
    }
    info["rels"] = std::move(r);
    Json c = Json::array();
    for (const auto& x : conflicts) c.push(x);
    info["disc2_conflicts"] = std::move(c);
    (void)input;
    ctx.state.at(name()).info = std::move(info);
    for (const auto& x : conflicts) ctx.rep.log("disc 2 differs from disc 1 in " + x + " (disc 1's copy kept)");
  }

 private:
  std::shared_ptr<Plan> plan_;
};

// ---------------------------------------------------------------------------------------------
// recompile

class RecompileStage : public Stage {
 public:
  explicit RecompileStage(std::shared_ptr<Plan> plan) : plan_(std::move(plan)) {}
  std::string name() const override { return "recompile"; }
  std::string title() const override { return "Translating the game code"; }
  double weight() const override { return 0.02; }

  std::string input_hash(Context& ctx) override {
    const Json& ex = ctx.state.at("extract").info;
    std::string acc = std::string(kRecompileScheme) + "\n" + ex.get("dol_sha1").str() + "\n";
    for (const auto& r : ex.get("rels").items()) acc += r.get("name").str() + " " + r.get("sha1").str() + "\n";
    acc += sha1_file(plan_->toolkit.symbols_csv) + "\n" + sha1_file(plan_->toolkit.symbols_extra) + "\n";
    acc += sha1_file(plan_->toolkit.recompiler) + "\n" + plan_->toolkit.project + "\n";
    return sha1_text(acc);
  }
  bool outputs_intact(Context& ctx) override { return generated_intact(under(ctx, layout::kGenerated)); }
  std::uint64_t bytes_needed(Context& ctx) override {
    std::uint64_t input = 0;
    const auto& discs = ctx.state.at("verify").info.get("discs").items();
    if (!discs.empty()) input = static_cast<std::uint64_t>(discs[0].get("dol_bytes").num() + discs[0].get("rel_bytes").num());
    return static_cast<std::uint64_t>(ctx.toolkit.get("disk").get("generated_per_input_byte").num(0) * static_cast<double>(input));
  }
  void clean_partial(Context& ctx) override { remove_tree(under(ctx, layout::kGeneratedPartial)); }

  static bool generated_intact(const fs::path& dir) {
    std::ifstream in(dir / "generated.tsv");
    if (!in) return false;
    std::string line;
    std::error_code ec;
    std::size_t n = 0;
    while (std::getline(in, line)) {
      std::istringstream ls(line);
      std::string size, sha, file;
      std::getline(ls, size, '\t');
      std::getline(ls, sha, '\t');
      std::getline(ls, file, '\t');
      if (!fs::is_regular_file(dir / file, ec) || fs::file_size(dir / file, ec) != std::stoull(size)) return false;
      ++n;
    }
    return n > 0;
  }

  void run(Context& ctx) override {
    const fs::path out = under(ctx, layout::kGeneratedPartial);
    remove_tree(out);
    std::error_code ec;
    fs::create_directories(out, ec);
    const fs::path game = under(ctx, layout::kGame);
    std::vector<std::string> argv = {u8(plan_->toolkit.recompiler), u8((game / "sys" / "main.dol")),
                                     "--csv", u8(plan_->toolkit.symbols_csv),
                                     "--extra-funcs", u8(plan_->toolkit.symbols_extra)};
    for (const auto& r : ctx.state.at("extract").info.get("rels").items()) {
      argv.push_back("--rel");
      argv.push_back((game / "files" / r.get("name").str()).string());
    }
    argv.insert(argv.end(), {"--output", u8(out), "--project", plan_->toolkit.project});
    ctx.rep.stage_progress(0, 1, "Translating the game code");
    ctx.rep.log("run: " + join(argv));
    const RunResult r = run_process(argv, {}, ctx.cancel, [&](const std::string& l) { ctx.rep.log("[recompiler] " + l); });
    if (r.exit_code != 0) {
      throw Error(kRecompileFailed, "the recompiler failed (exit " + std::to_string(r.exit_code) + "): " + join(r.tail, " | "));
    }
    // generated.tsv: size, sha1, name of every generated file (what the compile stage hashes).
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(out)) {
      if (e.is_regular_file()) files.push_back(e.path().filename());
    }
    std::sort(files.begin(), files.end());
    std::ostringstream tsv;
    std::uint64_t bytes = 0, sources = 0;
    for (const auto& f : files) {
      const std::uint64_t size = fs::file_size(out / f);
      bytes += size;
      if (f.extension() == ".cpp") ++sources;
      tsv << size << "\t" << sha1_file(out / f) << "\t" << u8(f) << "\n";
    }
    if (sources == 0) throw Error(kRecompileFailed, "the recompiler produced no C++ files");
    write_text_atomic(out / "generated.tsv", tsv.str());
    remove_tree(under(ctx, layout::kGenerated));
    fs::rename(out, under(ctx, layout::kGenerated), ec);
    if (ec) throw Error(kFailed, "cannot move the generated code into place: " + ec.message());
    ctx.rep.stage_progress(1, 1, "Translating the game code");
    Json info = Json::object();
    info["files"] = static_cast<std::uint64_t>(files.size());
    info["sources"] = sources;
    info["bytes"] = bytes;
    info["manifest_sha1"] = sha1_file(under(ctx, layout::kGenerated) / "generated.tsv");
    ctx.state.at(name()).info = std::move(info);
  }

 private:
  std::shared_ptr<Plan> plan_;
};

// ---------------------------------------------------------------------------------------------
// compile

std::string compiler_id(const std::vector<std::string>& cxx, const RunOptions& run, CancelToken& cancel) {
  std::string id;
  for (const char* flag : {"--version", "-dumpmachine"}) {
    std::vector<std::string> argv = cxx;
    argv.push_back(flag);
    const RunResult r = run_process(argv, run, cancel, nullptr);
    const bool answered = r.exit_code == 0 && !r.tail.empty();
    if (!answered && flag == std::string("--version")) {
      throw Error(kNoToolchain, "the compiler " + cxx[0] + " does not answer " + flag + ": it is not usable");
    }
    if (answered) {
      id += (id.empty() ? "" : " / ") + (flag == std::string("--version") ? r.tail.front() : r.tail.back());
    } else {
      // `zig c++ -target x86_64-linux-gnu.2.31 -dumpmachine` is refused (the glibc version is not a triple): what the
      // command line says about the target stands for the answer, so that another target is another compiler id.
      std::string rest;
      for (std::size_t i = 1; i < cxx.size(); ++i) rest += (rest.empty() ? "" : " ") + cxx[i];
      id += " / " + rest;
    }
  }
  return id;
}

class CompileStage : public Stage {
 public:
  explicit CompileStage(std::shared_ptr<Plan> plan) : plan_(std::move(plan)) {}
  std::string name() const override { return "compile"; }
  std::string title() const override { return "Building the game"; }
  double weight() const override { return 0.61; }

  std::string input_hash(Context& ctx) override {
    resolve(ctx);
    return sha1_text(std::string(kCompileScheme) + "\n" + cc_id_ + "\n" + flags_key() + "\n" + plan_->toolkit.content_hash() + "\n" +
                     ctx.state.at("recompile").info.get("manifest_sha1").str());
  }
  bool outputs_intact(Context& ctx) override {
    std::error_code ec;
    return fs::is_regular_file(exe_path(ctx), ec);
  }
  std::uint64_t bytes_needed(Context& ctx) override {
    const Json& disk = ctx.toolkit.get("disk");
    const double gen = ctx.state.at("recompile").info.get("bytes").num();
    const double objects = gen * disk.get("objects_per_generated_byte").num(0);
    const double have = static_cast<double>(tree_bytes(under(ctx, layout::kBuild)));
    return static_cast<std::uint64_t>(std::max(0.0, objects - have) + disk.get("executable_bytes").num(0));
  }
  void clean_partial(Context& ctx) override { remove_tree(under(ctx, layout::kBuild) / "tmp"); }

  void run(Context& ctx) override {
    resolve(ctx);
    const Toolkit& tk = plan_->toolkit;
    const fs::path gen = under(ctx, layout::kGenerated);
    const fs::path obj = under(ctx, layout::kBuild) / "obj";
    const fs::path tmp = under(ctx, layout::kBuild) / "tmp";
    remove_tree(tmp);
    std::error_code ec;
    fs::create_directories(obj, ec);
    fs::create_directories(tmp, ec);

    // Header hash: the generated headers are part of every object.
    std::string headers_acc;
    struct Unit {
      fs::path source;
      std::uint64_t size = 0;
      std::string key;
      fs::path object;
      bool cached = false;
    };
    std::vector<Unit> units;
    {
      std::ifstream in(gen / "generated.tsv");
      std::string line;
      std::vector<std::tuple<std::uint64_t, std::string, std::string>> rows;
      while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string size, sha, file;
        std::getline(ls, size, '\t');
        std::getline(ls, sha, '\t');
        std::getline(ls, file, '\t');
        rows.emplace_back(std::stoull(size), sha, file);
      }
      for (const auto& [size, sha, file] : rows) {
        if (fs::path(file).extension() != ".cpp") headers_acc += file + " " + sha + "\n";
      }
      const std::string base = cc_id_ + "\n" + flags_key() + "\n" + tk.content_hash() + "\n" + sha1_text(headers_acc) + "\n";
      for (const auto& [size, sha, file] : rows) {
        if (fs::path(file).extension() != ".cpp") continue;
        units.push_back({gen / file, size, sha1_text(base + sha), {}, false});
      }
      for (const auto& s : tk.sources) {
        units.push_back({s, static_cast<std::uint64_t>(fs::file_size(s, ec)), sha1_text(base + sha1_file(s) + u8(s.filename())), {}, false});
      }
    }
    std::uint64_t total = 0;
    std::set<std::string> live;
    for (auto& u : units) {
      u.object = obj / (u.key + ".o");
      u.cached = fs::exists(u.object, ec);
      total += u.size;
      live.insert(u.key + ".o");
    }
    // Largest first: the long files start early and the short ones fill the gaps.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < units.size(); ++i) {
      if (!units[i].cached) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return units[a].size > units[b].size; });

    const unsigned requested_jobs = ctx.opt.jobs ? ctx.opt.jobs : default_jobs(plan_->compiler);
    std::atomic<std::uint64_t> done_bytes{0};
    std::uint64_t cached_bytes = 0;
    for (auto& u : units) {
      if (u.cached) cached_bytes += u.size;
    }
    done_bytes = cached_bytes;
    std::atomic<std::size_t> next{0}, finished{units.size() - order.size()};
    std::atomic<bool> stop{false};
    std::mutex err_mu;
    std::exception_ptr first_error;
    auto text = [&] {
      return "Compiling the game (" + std::to_string(finished.load()) + "/" + std::to_string(units.size()) + " files)";
    };
    ctx.rep.stage_progress(done_bytes, total, text());
    ctx.rep.log("compile: " + std::to_string(order.size()) + " of " + std::to_string(units.size()) + " files to compile with " +
                std::to_string(requested_jobs) + " jobs, the rest is cached");

    // Three quarters of the memory available now, shared by the compiler processes (0 = unknown: no limit).
    const std::uint64_t available_mb = available_memory_kb() / 1024;
    MemoryGate gate(available_mb == 0 ? ~std::uint64_t(0) : available_mb * 3 / 4);
    auto worker = [&] {
      try {
        for (;;) {
          if (stop.load()) return;
          const std::size_t k = next.fetch_add(1);
          if (k >= order.size()) return;
          Unit& u = units[order[k]];
          const std::uint64_t need_mb = estimate_compile_mb(plan_->compiler, u.size);
          if (!gate.acquire(need_mb, [&] { return stop.load() || ctx.cancel.poll(); })) return;
          struct Release { MemoryGate& g; std::uint64_t mb; ~Release() { g.release(mb); } } release{gate, need_mb};
          const fs::path tmp_obj = tmp / (u.key + ".o");
          std::vector<std::string> argv = plan_->compiler;
          argv.push_back("-std=" + tk.std_flag);
          for (const auto& f : tk.flags) argv.push_back(f);
          for (const auto& d : tk.include_dirs) argv.push_back("-I" + u8(d));
          argv.push_back("-I" + u8(gen));
          argv.insert(argv.end(), {"-c", u8(u.source), "-o", u8(tmp_obj)});
          ctx.rep.log("run: " + join(argv));
          const RunResult r = run_process(argv, plan_->compiler_run, ctx.cancel, [&](const std::string& l) { ctx.rep.log("[cxx " + u8(u.source.filename()) + "] " + l); });
          if (r.exit_code != 0) {
            throw Error(kCompileFailed, "compiling " + u8(u.source.filename()) + " failed (exit " + std::to_string(r.exit_code) +
                                            "): " + join(r.tail, " | "));
          }
          std::error_code e2;
          fs::rename(tmp_obj, u.object, e2);
          if (e2) throw Error(kFailed, "cannot store " + u8(u.object) + ": " + e2.message());
          ctx.rep.log("compiled " + u8(u.source.filename()) + " in " + std::to_string(r.seconds) + " s, " +
                      std::to_string(r.max_rss_kb / 1024) + " MB peak");
          done_bytes += u.size;
          ++finished;
          ctx.rep.stage_progress(done_bytes, total, text());
        }
      } catch (...) {
        std::lock_guard<std::mutex> lock(err_mu);
        if (!first_error) first_error = std::current_exception();
        stop = true;
      }
    };
    std::vector<std::thread> pool;
    const unsigned jobs = std::max(1u, std::min<unsigned>(requested_jobs, static_cast<unsigned>(std::max<std::size_t>(order.size(), 1))));
    for (unsigned i = 0; i < jobs; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    if (first_error) std::rethrow_exception(first_error);
    ctx.cancel.check();

    // Link.
    ctx.rep.stage_progress(total, total, "Linking the game");
    const fs::path exe = exe_path(ctx);
    fs::create_directories(exe.parent_path(), ec);
    const fs::path tmp_exe = tmp / (tk.executable + ".new");
    std::vector<std::string> argv = plan_->compiler;
    argv.insert(argv.end(), {"-o", u8(tmp_exe)});
    for (const auto& u : units) argv.push_back(u8(u.object));
    for (const auto& l : tk.libs) argv.push_back(u8(l));
    for (const auto& l : tk.system_libs) argv.push_back(l);
    ctx.rep.log("run: " + join(argv));
    const RunResult r = run_process(argv, plan_->compiler_run, ctx.cancel, [&](const std::string& l) { ctx.rep.log("[link] " + l); });
    if (r.exit_code != 0) throw Error(kCompileFailed, "linking the game failed (exit " + std::to_string(r.exit_code) + "): " + join(r.tail, " | "));
    fs::rename(tmp_exe, exe, ec);
    if (ec) throw Error(kFailed, "cannot install " + u8(exe) + ": " + ec.message());
    remove_tree(tmp);
    // Forget objects nothing refers to any more (an older generation of the generated code).
    for (const auto& e : fs::directory_iterator(obj)) {
      if (!live.count(u8(e.path().filename()))) fs::remove(e.path(), ec);
    }

    Json info = Json::object();
    info["compiler"] = cc_id_;
    info["flags"] = flags_key();
    info["objects"] = static_cast<std::uint64_t>(units.size());
    info["compiled"] = static_cast<std::uint64_t>(order.size());
    info["cached"] = static_cast<std::uint64_t>(units.size() - order.size());
    info["executable"] = std::string(layout::kBin) + "/" + tk.executable;
    info["executable_bytes"] = static_cast<std::uint64_t>(fs::file_size(exe, ec));
    ctx.state.at(name()).info = std::move(info);
    ctx.state.meta["compiler"] = cc_id_;
  }

 private:
  void resolve(Context& ctx) {
    if (!cc_id_.empty()) return;
    std::string ignored_cxx;
    plan_->compiler = resolve_compiler(plan_->toolkit, ctx.opt.compiler, ctx.root, &ignored_cxx);
    if (!ignored_cxx.empty()) {
      ctx.rep.log("CXX=" + ignored_cxx + " is ignored: the libraries of this toolkit are built with the bundled compiler (use --compiler to override)");
    }
    // Zig keeps a cache of the C++ library it builds for the target on first use: inside the instance, so
    // that it is removed with it and nothing is written elsewhere on the player's disk.
    if (fs::path(plan_->compiler[0]).stem() == "zig" && !std::getenv("ZIG_GLOBAL_CACHE_DIR")) {
      plan_->compiler_run.env.push_back({"ZIG_GLOBAL_CACHE_DIR", u8(under(ctx, layout::kBuild) / "zig-cache")});
    }
    cc_id_ = compiler_id(plan_->compiler, plan_->compiler_run, ctx.cancel);
    ctx.rep.log("compiler: " + join(plan_->compiler) + " (" + cc_id_ + ")");
  }
  std::string flags_key() const { return plan_->toolkit.std_flag + " " + join(plan_->toolkit.flags) + " | " + join(plan_->toolkit.system_libs); }
  fs::path exe_path(Context& ctx) const { return under(ctx, layout::kBin) / plan_->toolkit.executable; }

  std::shared_ptr<Plan> plan_;
  std::string cc_id_;
};

// ---------------------------------------------------------------------------------------------
// finalize

class FinalizeStage : public Stage {
 public:
  explicit FinalizeStage(std::shared_ptr<Plan> plan) : plan_(std::move(plan)) {}
  std::string name() const override { return "finalize"; }
  std::string title() const override { return "Finishing the instance"; }
  double weight() const override { return 0.02; }
  std::string input_hash(Context& ctx) override {
    return sha1_text("finalize-1\n" + ctx.state.at("compile").input + "\n" + ctx.state.at("extract").input);
  }
  bool outputs_intact(Context& ctx) override {
    std::error_code ec;
    return fs::is_directory(under(ctx, layout::kSaves), ec) && ctx.state.meta.has("run");
  }
  std::uint64_t bytes_needed(Context&) override { return 0; }
  void clean_partial(Context&) override {}

  void run(Context& ctx) override {
    std::error_code ec;
    for (const char* d : {layout::kSaves, layout::kConfig, layout::kLogs}) fs::create_directories(under(ctx, d), ec);
    const Toolkit& tk = plan_->toolkit;
    Json versions = tk.json.get("versions").is_object() ? tk.json.get("versions") : Json::object();
    versions["toolkit_sha1"] = tk.content_hash();
    versions["symbols_sha1"] = sha1_text(sha1_file(tk.symbols_csv) + sha1_file(tk.symbols_extra));
    versions["recompiler_sha1"] = sha1_file(tk.recompiler);
    versions["compiler"] = ctx.state.at("compile").info.get("compiler");
    versions["tos_prepare"] = ctx.state.tool_version;
    if (!ctx.payload_id.empty()) versions["payload"] = ctx.payload_id;
    ctx.state.meta["versions"] = std::move(versions);

    Json discs = Json::array();
    for (const auto& d : ctx.state.at("verify").info.get("discs").items()) {
      Json j = Json::object();
      j["disc"] = d.get("disc");
      j["level"] = d.get("level");
      j["content_sha1"] = d.get("content_sha1");
      discs.push(std::move(j));
    }
    ctx.state.meta["discs"] = std::move(discs);

    // How to start the game, relative to the data folder (the launcher resolves them against {data}).
    Json run = Json::object();
    run["exe"] = ctx.state.at("compile").info.get("executable");
    run["args"] = tk.json.get("run").get("args").is_array() ? tk.json.get("run").get("args") : Json::array();
    run["env"] = tk.json.get("run").get("env").is_object() ? tk.json.get("run").get("env") : Json::object();
    run["unlimited_stack"] = tk.json.get("run").get("unlimited_stack").boolean(false);
    run["cwd"] = ".";
    ctx.state.meta["run"] = std::move(run);
    Json paths = Json::object();
    paths["game"] = layout::kGame;
    paths["saves"] = layout::kSaves;
    paths["config"] = layout::kConfig;
    paths["log"] = std::string(layout::kLogs) + "/prepare.log";
    paths["cache"] = Json::array();
    paths["cache"].push(layout::kGenerated);
    paths["cache"].push(layout::kBuild);
    ctx.state.meta["paths"] = std::move(paths);
  }

 private:
  std::shared_ptr<Plan> plan_;
};

}  // namespace

std::vector<std::unique_ptr<Stage>> make_stages(Context&, std::shared_ptr<Plan> plan) {
  std::vector<std::unique_ptr<Stage>> v;
  v.push_back(std::make_unique<VerifyStage>(plan));
  v.push_back(std::make_unique<ExtractStage>(plan));
  v.push_back(std::make_unique<RecompileStage>(plan));
  v.push_back(std::make_unique<CompileStage>(plan));
  v.push_back(std::make_unique<FinalizeStage>(plan));
  if (static_cast<int>(v.size()) != kStageCount) throw std::logic_error("kStageCount does not match the stages");
  return v;
}

std::uint64_t estimate_compile_mb(const std::vector<std::string>& compiler_argv, std::uint64_t source_bytes) {
  double per_mb = 120;  // g++
  if (!compiler_argv.empty()) {
    if (fs::path(compiler_argv[0]).stem() == "zig") per_mb = 135;
    else if (compiler_argv[0].find("clang") != std::string::npos) per_mb = 60;
  }
  return 150 + static_cast<std::uint64_t>(per_mb * static_cast<double>(source_bytes) / 1e6);
}

bool MemoryGate::acquire(std::uint64_t mb, const std::function<bool()>& stop) {
  std::unique_lock<std::mutex> lock(mu_);
  for (;;) {
    if (stop()) return false;
    if (running_ == 0 || used_ + mb <= budget_) break;
    cv_.wait_for(lock, std::chrono::milliseconds(100));
  }
  used_ += mb;
  ++running_;
  return true;
}

void MemoryGate::release(std::uint64_t mb) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    used_ -= mb;
    --running_;
  }
  cv_.notify_all();
}

unsigned default_jobs(const std::vector<std::string>& compiler_argv) {
  unsigned cores = std::thread::hardware_concurrency();
  if (cores == 0) cores = 2;
  double per_job_gb = 1.5;  // g++ -O1 on the largest generated file: 1.2 GB; clang++: 0.6 GB
  if (!compiler_argv.empty() &&
      (compiler_argv[0].find("clang") != std::string::npos || fs::path(compiler_argv[0]).stem() == "zig")) {
    per_job_gb = 0.8;  // Zig's C++ compiler is clang
  }
  const std::uint64_t kb = available_memory_kb();
  if (kb != 0) {
    const unsigned by_ram = static_cast<unsigned>(static_cast<double>(kb) / 1048576.0 / per_job_gb);
    return std::max(1u, std::min(cores, by_ram));
  }
  return std::max(1u, cores / 2);
}

bool check_stages(Context& ctx, std::vector<std::unique_ptr<Stage>>& stages) {
  bool all = true;
  bool upstream_ok = true;
  for (auto& s : stages) {
    std::string status = "stale";
    if (upstream_ok) {
      StageRecord* rec = ctx.state.find(s->name());
      try {
        const std::string hash = s->input_hash(ctx);
        if (rec && rec->status == "done" && rec->input == hash && s->outputs_intact(ctx)) status = "up-to-date";
      } catch (const Error&) {
      }
    }
    if (status != "up-to-date") upstream_ok = false, all = false;
    std::printf("check %s %s\n", s->name().c_str(), status.c_str());
  }
  std::fflush(stdout);
  return all;
}

}  // namespace prep
