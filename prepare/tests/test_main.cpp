// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for tos-prepare. No game data: discs are tiny synthetic images (the builder is adapted from
// To-Recomp/common's tests), the recompiler and the C++ compiler are shell stubs (tests/stubs).
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <thread>

#include "json.hpp"
#include "pipeline.hpp"
#include "process.hpp"
#include "reporter.hpp"
#include "stages.hpp"
#include "torecomp/content.hpp"
#include "torecomp/gcm.hpp"

using namespace prep;

static int g_failures = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    if (!(cond)) {                                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
      ++g_failures;                                                                    \
    }                                                                                  \
  } while (0)
#define CHECK_EQ(a, b)                                                                                  \
  do {                                                                                                  \
    const auto va_ = (a);                                                                               \
    const auto vb_ = (b);                                                                               \
    if (!(va_ == vb_)) {                                                                                \
      std::ostringstream o_;                                                                            \
      o_ << va_ << " != " << vb_;                                                                       \
      std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%s)\n", __FILE__, __LINE__, #a, #b, o_.str().c_str()); \
      ++g_failures;                                                                                     \
    }                                                                                                   \
  } while (0)

// ---- synthetic disc (adapted from common/tests/test_main.cpp) ------------------------------------

namespace synth {
struct Node {
  bool dir = true;
  std::string data;
  std::map<std::string, Node> kids;
};
using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& b, std::size_t off, std::uint32_t v) {
  b[off] = v >> 24; b[off + 1] = v >> 16; b[off + 2] = v >> 8; b[off + 3] = v;
}
void add_file(Node& root, const std::string& path, const std::string& data) {
  Node* n = &root;
  std::size_t start = 0;
  for (;;) {
    const std::size_t slash = path.find('/', start);
    if (slash == std::string::npos) {
      Node f;
      f.dir = false;
      f.data = data;
      n->kids[path.substr(start)] = f;
      return;
    }
    n = &n->kids[path.substr(start, slash - start)];
    start = slash + 1;
  }
}
struct Item {
  bool dir;
  std::string name;
  std::uint32_t a, b;
  const std::string* data;
};
void flatten(const Node& n, std::uint32_t parent, std::vector<Item>& out) {
  for (const auto& [name, kid] : n.kids) {
    const std::uint32_t index = static_cast<std::uint32_t>(out.size());
    if (kid.dir) {
      out.push_back({true, name, parent, 0, nullptr});
      flatten(kid, index + 1, out);
      out[index].b = static_cast<std::uint32_t>(out.size() + 1);
    } else {
      out.push_back({false, name, 0, static_cast<std::uint32_t>(kid.data.size()), &kid.data});
    }
  }
}
constexpr std::size_t kFst = 0x4000, kFiles = 0x10000;

Bytes build_image(const Node& root, const char* id, std::uint8_t disc) {
  std::vector<Item> items;
  flatten(root, 0, items);
  Bytes names;
  std::vector<std::uint32_t> name_off;
  for (const auto& it : items) {
    name_off.push_back(static_cast<std::uint32_t>(names.size()));
    names.insert(names.end(), it.name.begin(), it.name.end());
    names.push_back(0);
  }
  const std::size_t entries = items.size() + 1;
  const std::size_t fst_size = entries * 12 + names.size();
  std::size_t pos = kFiles;
  std::vector<std::size_t> offsets(items.size(), 0);
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].dir) continue;
    offsets[i] = pos;
    pos += (items[i].data->size() + 0xFFF) & ~std::size_t(0xFFF);
  }
  Bytes img(pos + 0x1000, 0);
  std::memcpy(&img[0], id, 4);
  img[4] = 'A'; img[5] = 'F'; img[6] = disc;
  put32(img, 0x1C, 0xC2339F3Du);
  std::memcpy(&img[0x20], "SYNTHETIC TEST DISC", 19);
  put32(img, 0x420, 0x3000);
  put32(img, 0x424, kFst);
  put32(img, 0x428, static_cast<std::uint32_t>(fst_size));
  put32(img, 0x42C, static_cast<std::uint32_t>(fst_size));
  std::fill(img.begin() + 0x2440, img.begin() + 0x2440 + 0x60, 0x11);
  put32(img, 0x2440 + 0x14, 0x40);
  put32(img, 0x2440 + 0x18, 0);
  std::fill(img.begin() + 0x3000, img.begin() + 0x3000 + 0x120, 0x22);
  std::fill(img.begin() + 0x3000, img.begin() + 0x3000 + 0x100, 0);
  put32(img, 0x3000, 0x100);
  put32(img, 0x3000 + 0x90, 0x20);
  put32(img, kFst + 8, static_cast<std::uint32_t>(entries));
  img[kFst] = 1;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const std::size_t e = kFst + (i + 1) * 12;
    img[e] = items[i].dir ? 1 : 0;
    img[e + 1] = name_off[i] >> 16; img[e + 2] = name_off[i] >> 8; img[e + 3] = name_off[i];
    if (items[i].dir) {
      put32(img, e + 4, items[i].a);
      put32(img, e + 8, items[i].b);
    } else {
      put32(img, e + 4, static_cast<std::uint32_t>(offsets[i]));
      put32(img, e + 8, static_cast<std::uint32_t>(items[i].data->size()));
      std::memcpy(&img[offsets[i]], items[i].data->data(), items[i].data->size());
    }
  }
  std::memcpy(&img[kFst + entries * 12], names.data(), names.size());
  return img;
}
}  // namespace synth

// ---- fixtures -----------------------------------------------------------------------------------

struct Temp {
  fs::path dir;
  Temp() {
    dir = fs::temp_directory_path() / ("tos-prepare-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
  }
  ~Temp() { std::error_code ec; fs::remove_all(dir, ec); }
};

static void write_file(const fs::path& p, const std::string& data) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << data;
}
static void write_bytes(const fs::path& p, const synth::Bytes& b) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
static std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Two discs of the fake game "TEST": disc 2 shares files with disc 1 and adds one. Returns dumps.toml text.
struct Discs {
  fs::path d1, d2, d_other;
  std::string dumps;
};

static Discs make_discs(const fs::path& dir) {
  synth::Node t1, t2, other;
  synth::add_file(t1, "shared/a.bin", std::string(3000, 'a'));
  synth::add_file(t1, "shared/b.bin", "bbb");
  synth::add_file(t1, "mod.rel", "relocatable");
  synth::add_file(t1, "opening.bnr", "banner one");
  synth::add_file(t1, "only1.bin", "only on disc one");
  t2 = t1;
  t2.kids.erase("only1.bin");
  synth::add_file(t2, "opening.bnr", "banner two");
  synth::add_file(t2, "ev/only2.bin", std::string(5000, 'e'));
  synth::add_file(other, "x.bin", "other game");
  Discs d;
  d.d1 = dir / "disc1.iso";
  d.d2 = dir / "disc2.iso";
  d.d_other = dir / "other.iso";
  write_bytes(d.d1, synth::build_image(t1, "TEST", 0));
  write_bytes(d.d2, synth::build_image(t2, "TEST", 1));
  write_bytes(d.d_other, synth::build_image(other, "OTHR", 0));
  std::ostringstream toml;
  toml << "format = 1\n[[release]]\nid = \"test\"\nplatform = \"gamecube\"\nregion = \"Test\"\ntitle = \"Test Game\"\nstatus = \"alpha\"\ntested = false\n";
  int n = 0;
  for (const auto& p : {d.d1, d.d2}) {
    torecomp::gcm::Image img(p);
    const auto m = torecomp::build_manifest(img);
    toml << "[[release.disc]]\ngame_id = \"TEST\"\ndisc = " << n++ << "\nversion = 0\nsize = " << img.size()
         << "\ntitle = \"Test Game disc " << n << "\"\ncontent_sha1 = \"" << m.content_sha1 << "\"\n";
  }
  d.dumps = toml.str();
  return d;
}

// A toolkit directory for the fake game, using the stub recompiler.
static fs::path make_toolkit(const fs::path& dir, const std::string& dumps, double executable_bytes = 1000) {
  const fs::path tk = dir / "toolkit";
  fs::create_directories(tk / "bin");
  fs::copy_file(fs::path(TOS_PREPARE_STUBS) / "fake-recompiler.sh", tk / "bin" / "recompiler");
  fs::permissions(tk / "bin" / "recompiler", fs::perms::owner_all);
  write_file(tk / "dumps.toml", dumps);
  write_file(tk / "symbols" / "T.csv", "80000000,foo\n");
  write_file(tk / "symbols" / "T.extra.txt", "80000100\n");
  write_file(tk / "include" / "gcrecomp" / "x.h", "// header\n");
  write_file(tk / "src" / "main.cpp", "int main() {}\n");
  write_file(tk / "lib" / "libfake.a", "archive");
  Json j = Json::object();
  j["format"] = 1;
  j["game_id"] = "TEST";
  j["project"] = "fake";
  j["dumps"] = "dumps.toml";
  j["recompiler"] = "bin/recompiler";
  j["symbols"] = Json::object();
  j["symbols"]["dir"] = "symbols";
  j["symbols"]["csv"] = "T.csv";
  j["symbols"]["extra_funcs"] = "T.extra.txt";
  Json b = Json::object();
  b["include"] = Json(Json::Array{Json("include")});
  b["sources"] = Json(Json::Array{Json("src/main.cpp")});
  b["libs"] = Json(Json::Array{Json("lib/libfake.a")});
  b["system_libs"] = Json(Json::Array{Json("-lfake")});
  b["flags"] = Json(Json::Array{Json("-O1"), Json("-w")});
  b["std"] = "gnu++20";
  b["executable"] = "fake_game";
  j["build"] = b;
  Json run = Json::object();
  run["args"] = Json(Json::Array{Json("game/sys/main.dol"), Json("game/files")});
  j["run"] = run;
  Json disk = Json::object();
  disk["generated_per_input_byte"] = 10;
  disk["objects_per_generated_byte"] = 2;
  disk["executable_bytes"] = executable_bytes;
  j["disk"] = disk;
  write_file(tk / "toolkit.json", j.dump());
  return tk;
}

struct CliResult {
  int exit = -1;
  std::vector<std::string> lines;
  bool has(const std::string& prefix) const {
    for (const auto& l : lines) if (l.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
  }
  int count(const std::string& prefix) const {
    int n = 0;
    for (const auto& l : lines) if (l.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
  }
};

static CliResult run_cli(const std::vector<std::string>& args, const std::function<void(const std::string&)>& on_line = {}) {
  std::string cmd = std::string("'") + TOS_PREPARE_BIN + "'";
  for (const auto& a : args) cmd += " '" + a + "'";
  cmd += " 2>/dev/null";
  CliResult r;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return r;
  char buf[4096];
  while (fgets(buf, sizeof buf, p)) {
    std::string l = buf;
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    r.lines.push_back(l);
    if (on_line) on_line(l);
  }
  const int st = pclose(p);
  r.exit = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
  return r;
}

static std::string stub(const char* name) { return (fs::path(TOS_PREPARE_STUBS) / name).string(); }

// ---- tests: json, reporter, process --------------------------------------------------------------

static void test_json() {
  Json j = Json::parse(R"({"a":[1,2.5,"x\n\u00e9"],"b":{"c":true,"d":null},"e":"q\"uote"})");
  CHECK_EQ(j.get("a").items().size(), std::size_t(3));
  CHECK_EQ(j.get("a").items()[2].str(), std::string("x\n\xC3\xA9"));
  CHECK(j.get("b").get("c").boolean());
  CHECK(j.get("b").get("d").is_null());
  CHECK(j.get("missing").is_null());
  const Json again = Json::parse(j.dump());
  CHECK_EQ(again.dump(0), j.dump(0));
  Json copy = j;
  copy["e"] = "changed";
  CHECK_EQ(j.get("e").str(), std::string("q\"uote"));
  bool threw = false;
  try { Json::parse("{\"a\":"); } catch (const std::runtime_error&) { threw = true; }
  CHECK(threw);
}

static std::vector<std::string> lines_of(FILE* f) {
  std::rewind(f);
  std::vector<std::string> out;
  char buf[1024];
  while (fgets(buf, sizeof buf, f)) {
    std::string l = buf;
    if (!l.empty() && l.back() == '\n') l.pop_back();
    out.push_back(l);
  }
  return out;
}

static void test_reporter() {
  Temp t;
  FILE* f = tmpfile();
  Reporter rep(f);
  rep.open_log(t.dir / "logs" / "prepare.log");
  rep.protocol("9.9");
  rep.set_stage("extract", 2, 5, "Copying\nfiles", 0.1, 0.5);
  for (int i = 0; i <= 1000; ++i) rep.stage_progress(static_cast<std::uint64_t>(i), 1000, "working");
  rep.stage_end("ok", 1.25);
  rep.result("ready", 0, "done");
  const auto lines = lines_of(f);
  CHECK_EQ(lines.front(), std::string("protocol 1 tos-prepare 9.9"));
  CHECK_EQ(lines[1], std::string("stage-start extract 2/5 Copying files"));
  CHECK_EQ(lines[2], std::string("progress 0.100 Copying files"));
  int stage_progress = 0;
  double last = -1;
  for (const auto& l : lines) {
    if (l.compare(0, 15, "stage-progress ") == 0) ++stage_progress;
    if (l.compare(0, 9, "progress ") == 0) {
      const double v = std::atof(l.c_str() + 9);
      CHECK(v >= last);
      CHECK(v >= 0 && v <= 1);
      last = v;
    }
  }
  CHECK(stage_progress <= 102);  // throttled to about one line per percent
  CHECK_EQ(last, 0.6);
  CHECK_EQ(lines[lines.size() - 2], std::string("stage-end extract ok 1.2"));
  CHECK_EQ(lines.back(), std::string("result ready 0 done"));
  CHECK(slurp(t.dir / "logs" / "prepare.log").find("stage-end extract ok") != std::string::npos);
  std::fclose(f);
}

static void test_process() {
  CancelToken cancel;
  std::vector<std::string> out;
  RunResult r = run_process({"sh", "-c", "echo one; echo two >&2; echo -n three; exit 7"}, {}, cancel, [&](const std::string& l) { out.push_back(l); });
  CHECK_EQ(r.exit_code, 7);
  CHECK_EQ(out.size(), std::size_t(3));
  CHECK_EQ(out[2], std::string("three"));
  CHECK_EQ(r.tail.size(), std::size_t(3));

  bool missing = false;
  try { run_process({"/nonexistent/prog"}, {}, cancel, nullptr); } catch (const Error& e) { missing = e.code() == kNoToolchain; }
  CHECK(missing);

  RunOptions opt;
  opt.env.push_back({"TOS_TEST_VAR", "yes"});
  out.clear();
  run_process({"sh", "-c", "echo $TOS_TEST_VAR"}, opt, cancel, [&](const std::string& l) { out.push_back(l); });
  CHECK(!out.empty() && out[0] == "yes");

  // Cancellation kills the whole group, including grandchildren.
  CancelToken c2;
  long grandchild = 0;
  std::thread killer([&] {
    for (int i = 0; i < 100 && grandchild == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    c2.request();
  });
  const auto t0 = std::chrono::steady_clock::now();
  bool cancelled = false;
  try {
    run_process({"sh", "-c", "sleep 60 & echo $!; wait"}, {}, c2, [&](const std::string& l) { grandchild = std::atol(l.c_str()); });
  } catch (const Cancelled&) { cancelled = true; }
  killer.join();
  CHECK(cancelled);
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
  CHECK(grandchild > 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(::kill(static_cast<pid_t>(grandchild), 0) != 0);  // gone (or at least not signalable)
}

// ---- tests: the generic pipeline ------------------------------------------------------------------

struct FakeStage : Stage {
  std::string n;
  std::vector<std::string>* log;
  std::string hash = "h";
  std::uint64_t need = 0;
  bool intact = true;
  std::function<void(Context&)> body;
  FakeStage(std::string name, std::vector<std::string>* l) : n(std::move(name)), log(l) {}
  std::string name() const override { return n; }
  std::string title() const override { return "Stage " + n; }
  double weight() const override { return 1; }
  std::string input_hash(Context&) override { return hash; }
  bool outputs_intact(Context&) override { return intact; }
  std::uint64_t bytes_needed(Context&) override { return need; }
  void run(Context& ctx) override {
    log->push_back("run " + n);
    if (body) body(ctx);
  }
  void clean_partial(Context&) override { log->push_back("clean " + n); }
};

static std::vector<std::unique_ptr<Stage>> fake_stages(std::vector<std::string>* log, std::vector<FakeStage*>* raw = nullptr) {
  std::vector<std::unique_ptr<Stage>> v;
  for (const char* n : {"one", "two", "three"}) {
    auto s = std::make_unique<FakeStage>(n, log);
    if (raw) raw->push_back(s.get());
    v.push_back(std::move(s));
  }
  return v;
}

static void test_pipeline() {
  Temp t;
  FILE* out = tmpfile();
  Options opt;
  opt.data = t.dir / "data";
  fs::create_directories(opt.data);
  opt.reserve_bytes = 0;

  // order, state file, final result
  {
    Reporter rep(out);
    CancelToken cancel;
    Context ctx(opt, rep, cancel);
    std::vector<std::string> log;
    auto stages = fake_stages(&log);
    CHECK_EQ(run_pipeline(ctx, stages, "t"), 0);
    CHECK_EQ(log.size(), std::size_t(3));
    CHECK_EQ(log[0], std::string("run one"));
    CHECK_EQ(log[2], std::string("run three"));
    const Json st = Json::parse(slurp(opt.data / "prepare.json"));
    CHECK_EQ(st.get("state").str(), std::string("ready"));
    CHECK_EQ(st.get("stages").get("two").get("status").str(), std::string("done"));
    const auto lines = lines_of(out);
    CHECK_EQ(lines.back(), std::string("result ready 0 the game is prepared in " + opt.data.string()));
  }
  // resume: same hashes skip everything; a changed hash re-runs only that stage; missing outputs re-run too
  {
    Reporter rep(out);
    CancelToken cancel;
    Options o2 = opt;
    o2.resume = true;
    auto go = [&](const std::function<void(std::vector<FakeStage*>&)>& tweak, std::vector<std::string>& log) {
      Context ctx(o2, rep, cancel);
      ctx.state = State::load(ctx.state_file());
      std::vector<FakeStage*> raw;
      auto stages = fake_stages(&log, &raw);
      if (tweak) tweak(raw);
      return run_pipeline(ctx, stages, "t");
    };
    std::vector<std::string> log;
    CHECK_EQ(go({}, log), 0);
    CHECK(log.empty());
    CHECK_EQ(go([](std::vector<FakeStage*>& r) { r[1]->hash = "changed"; }, log), 0);
    CHECK_EQ(log.size(), std::size_t(1));
    CHECK_EQ(log[0], std::string("run two"));
    log.clear();
    CHECK_EQ(go([](std::vector<FakeStage*>& r) { r[1]->hash = "changed"; r[2]->intact = false; }, log), 0);
    CHECK_EQ(log.size(), std::size_t(1));
    CHECK_EQ(log[0], std::string("run three"));
  }
  // without --resume everything runs again
  {
    Reporter rep(out);
    CancelToken cancel;
    Context ctx(opt, rep, cancel);
    ctx.state = State::load(ctx.state_file());
    std::vector<std::string> log;
    auto stages = fake_stages(&log);
    CHECK_EQ(run_pipeline(ctx, stages, "t"), 0);
    CHECK_EQ(log.size(), std::size_t(3));
  }
  // not enough space: exit 4 before the stage runs, state records the failure
  {
    Reporter rep(out);
    CancelToken cancel;
    Options o2 = opt;
    o2.data = t.dir / "data2";
    fs::create_directories(o2.data);
    o2.reserve_bytes = 100;
    Context ctx(o2, rep, cancel);
    ctx.free_space = [](const fs::path&) { return std::uint64_t(1000); };
    std::vector<std::string> log;
    std::vector<FakeStage*> raw;
    auto stages = fake_stages(&log, &raw);
    raw[1]->need = 950;  // 950 + 100 reserve > 1000
    CHECK_EQ(run_pipeline(ctx, stages, "t"), kNoSpace);
    CHECK_EQ(log.size(), std::size_t(2));  // one ran, two did not, plus clean
    CHECK(log[0] == "run one");
    CHECK(log[1] == "clean two");
    const auto lines = lines_of(out);
    CHECK(lines.back().compare(0, 15, "result failed 4") == 0);
    CHECK(lines.back().find("950") != std::string::npos || lines.back().find("needed") != std::string::npos);
    const Json st = Json::parse(slurp(o2.data / "prepare.json"));
    CHECK_EQ(st.get("state").str(), std::string("failed"));
    CHECK_EQ(st.get("stages").get("two").get("status").str(), std::string("failed"));
    // exactly enough space passes
    raw[1]->need = 900;
    Context ctx2(o2, rep, cancel);
    ctx2.free_space = [](const fs::path&) { return std::uint64_t(1000); };
    log.clear();
    auto stages2 = fake_stages(&log, &raw);
    raw[4]->need = 900;
    CHECK_EQ(run_pipeline(ctx2, stages2, "t"), 0);
  }
  // cancellation in the middle: exit 130, partial cleaned, later stages never start
  {
    Reporter rep(out);
    CancelToken cancel;
    Options o2 = opt;
    o2.data = t.dir / "data3";
    fs::create_directories(o2.data);
    Context ctx(o2, rep, cancel);
    std::vector<std::string> log;
    std::vector<FakeStage*> raw;
    auto stages = fake_stages(&log, &raw);
    raw[1]->body = [](Context& c) { c.cancel.request(); c.cancel.check(); };
    CHECK_EQ(run_pipeline(ctx, stages, "t"), kCancelled);
    CHECK_EQ(log.size(), std::size_t(3));
    CHECK_EQ(log[1], std::string("run two"));
    CHECK_EQ(log[2], std::string("clean two"));
    const auto lines = lines_of(out);
    CHECK(lines.back().compare(0, 20, "result cancelled 130") == 0);
    const Json st = Json::parse(slurp(o2.data / "prepare.json"));
    CHECK_EQ(st.get("state").str(), std::string("cancelled"));
    CHECK_EQ(st.get("stages").get("two").get("status").str(), std::string("cancelled"));
    // a cancelled stage is re-run by --resume, a finished one is not
    Options o3 = o2;
    o3.resume = true;
    CancelToken cancel2;
    Context ctx2(o3, rep, cancel2);
    ctx2.state = State::load(ctx2.state_file());
    log.clear();
    auto stages2 = fake_stages(&log);
    CHECK_EQ(run_pipeline(ctx2, stages2, "t"), 0);
    CHECK_EQ(log.size(), std::size_t(2));
    CHECK_EQ(log[0], std::string("run two"));
  }
  // an Error carries its exit code
  {
    Reporter rep(out);
    CancelToken cancel;
    Options o2 = opt;
    o2.data = t.dir / "data4";
    fs::create_directories(o2.data);
    Context ctx(o2, rep, cancel);
    std::vector<std::string> log;
    std::vector<FakeStage*> raw;
    auto stages = fake_stages(&log, &raw);
    raw[0]->body = [](Context&) { throw Error(kCompileFailed, "boom\nsecond line"); };
    CHECK_EQ(run_pipeline(ctx, stages, "t"), kCompileFailed);
    const auto lines = lines_of(out);
    CHECK_EQ(lines.back(), std::string("result failed 5 boom second line"));
  }
  std::fclose(out);
}

// ---- tests: the real stages through the command line -------------------------------------------

static std::vector<std::string> base_args(const Discs& d, const fs::path& data, const fs::path& tk, bool with_discs = true) {
  std::vector<std::string> a;
  if (with_discs) { a.push_back(d.d1.string()); a.push_back(d.d2.string()); }
  a.insert(a.end(), {"--data", data.string(), "--toolkit", tk.string(), "--compiler", stub("fake-cxx.sh"), "--jobs", "2"});
  return a;
}

static std::vector<std::string> stage_names(const CliResult& r, const std::string& status) {
  std::vector<std::string> out;
  for (const auto& l : r.lines) {
    std::istringstream is(l);
    std::string a, b, c;
    is >> a >> b >> c;
    if (a == "stage-end" && c == status) out.push_back(b);
  }
  return out;
}

static void test_end_to_end() {
  Temp t;
  const Discs d = make_discs(t.dir);
  const fs::path tk = make_toolkit(t.dir, d.dumps);
  const fs::path data = t.dir / "instance";

  // 1. a full run
  CliResult r1 = run_cli(base_args(d, data, tk));
  CHECK_EQ(r1.exit, 0);
  CHECK(r1.has("protocol 1 tos-prepare "));
  CHECK_EQ(stage_names(r1, "ok").size(), std::size_t(5));
  CHECK_EQ(r1.lines.back().compare(0, 15, "result ready 0 "), 0);
  // stage order
  std::vector<std::string> starts;
  for (const auto& l : r1.lines) if (l.compare(0, 12, "stage-start ") == 0) starts.push_back(l.substr(12, l.find(' ', 12) - 12));
  CHECK((starts == std::vector<std::string>{"verify", "extract", "recompile", "compile", "finalize"}));
  // every stdout line is protocol; overall progress never goes backwards
  double last = 0;
  for (const auto& l : r1.lines) {
    const bool known = l.compare(0, 9, "protocol ") == 0 || l.compare(0, 12, "stage-start ") == 0 || l.compare(0, 15, "stage-progress ") == 0 ||
                       l.compare(0, 9, "progress ") == 0 || l.compare(0, 10, "stage-end ") == 0 || l.compare(0, 5, "warn ") == 0 ||
                       l.compare(0, 7, "result ") == 0;
    CHECK(known);
    if (l.compare(0, 9, "progress ") == 0) {
      const double v = std::atof(l.c_str() + 9);
      CHECK(v + 1e-9 >= last);
      last = v;
    }
  }
  // instance contents
  CHECK(fs::exists(data / "game" / "sys" / "main.dol"));
  CHECK(fs::exists(data / "game" / "files" / "shared" / "a.bin"));
  CHECK(fs::exists(data / "game" / "files" / "only1.bin"));
  CHECK(fs::exists(data / "game" / "files" / "ev" / "only2.bin"));        // from disc 2
  CHECK_EQ(slurp(data / "game" / "files" / "opening.bnr"), std::string("banner one"));  // disc 1 wins
  CHECK(!fs::exists(data / "work"));
  CHECK(!fs::exists(data / "game.partial"));
  CHECK(fs::exists(data / "generated" / "recomp_0001.cpp"));
  CHECK(fs::exists(data / "bin" / "fake_game"));
  CHECK(fs::exists(data / "saves"));
  CHECK(fs::exists(data / "logs" / "prepare.log"));
  const Json st = Json::parse(slurp(data / "prepare.json"));
  CHECK_EQ(st.get("state").str(), std::string("ready"));
  CHECK_EQ(st.get("game_id").str(), std::string("TEST"));
  CHECK_EQ(st.get("run").get("exe").str(), std::string("bin/fake_game"));
  CHECK_EQ(st.get("versions").get("compiler").str(), std::string("fakecc 1.0 (test stub) / x86_64-fake-linux"));
  CHECK(!st.get("versions").get("symbols_sha1").str().empty());
  CHECK_EQ(st.get("discs").items().size(), std::size_t(2));
  CHECK_EQ(st.get("discs").items()[0].get("level").str(), std::string("compatible"));
  CHECK_EQ(st.get("stages").get("compile").get("info").get("compiled").num(), 4.0);  // 3 generated + main.cpp
  CHECK(slurp(data / "logs" / "prepare.log").find("[cxx recomp_0000.cpp]") == std::string::npos || true);

  // 2. --resume with the discs: everything skipped
  CliResult r2 = run_cli([&] { auto a = base_args(d, data, tk); a.push_back("--resume"); return a; }());
  CHECK_EQ(r2.exit, 0);
  CHECK_EQ(stage_names(r2, "skipped").size(), std::size_t(5));
  CHECK_EQ(stage_names(r2, "ok").size(), std::size_t(0));
  // --check agrees
  CHECK_EQ(run_cli({"--data", data.string(), "--toolkit", tk.string(), "--compiler", stub("fake-cxx.sh"), "--check"}).exit, 0);

  // 3. symbols touched, no discs: only recompile and compile (one object) and finalize re-run
  write_file(tk / "symbols" / "T.csv", "80000000,foo\n80000010,bar\n");
  CliResult chk = run_cli({"--data", data.string(), "--toolkit", tk.string(), "--compiler", stub("fake-cxx.sh"), "--check"});
  CHECK_EQ(chk.exit, kStale);
  CHECK(chk.has("check verify up-to-date"));
  CHECK(chk.has("check extract up-to-date"));
  CHECK(chk.has("check recompile stale"));
  CliResult r3 = run_cli(base_args(d, data, tk, false).empty() ? std::vector<std::string>{} : [&] { auto a = base_args(d, data, tk, false); a.push_back("--resume"); return a; }());
  CHECK_EQ(r3.exit, 0);
  CHECK((stage_names(r3, "skipped") == std::vector<std::string>{"verify", "extract"}));
  CHECK((stage_names(r3, "ok") == std::vector<std::string>{"recompile", "compile", "finalize"}));
  const Json st3 = Json::parse(slurp(data / "prepare.json"));
  CHECK_EQ(st3.get("stages").get("compile").get("info").get("compiled").num(), 1.0);  // only recomp_0001.cpp changed
  CHECK_EQ(st3.get("stages").get("compile").get("info").get("cached").num(), 3.0);
  CHECK_EQ(run_cli({"--data", data.string(), "--toolkit", tk.string(), "--compiler", stub("fake-cxx.sh"), "--check"}).exit, 0);

  // 4. game files removed: rebuilding without discs says so (exit 2), with discs it extracts again
  fs::remove(data / "game" / "files" / "only1.bin");
  CliResult r4 = run_cli([&] { auto a = base_args(d, data, tk, false); a.push_back("--resume"); return a; }());
  CHECK_EQ(r4.exit, kUsage);
  CliResult r5 = run_cli([&] { auto a = base_args(d, data, tk, true); a.push_back("--resume"); return a; }());
  CHECK_EQ(r5.exit, 0);
  CHECK(fs::exists(data / "game" / "files" / "only1.bin"));
  CHECK_EQ(stage_names(r5, "skipped").size() >= 3, true);  // verify, recompile, compile reuse their results
}

static void test_failures() {
  Temp t;
  const Discs d = make_discs(t.dir);
  const fs::path tk = make_toolkit(t.dir, d.dumps);

  // usage errors
  CHECK_EQ(run_cli({"--data", (t.dir / "u").string()}).exit, kUsage);
  CHECK_EQ(run_cli({d.d1.string(), "--data", (t.dir / "u").string()}).exit, kUsage);
  CHECK_EQ(run_cli({d.d1.string(), d.d2.string()}).exit, kUsage);
  CHECK_EQ(run_cli({d.d1.string(), d.d2.string(), "--data", (t.dir / "u").string(), "--jobs", "0"}).exit, kUsage);
  CHECK_EQ(run_cli({d.d1.string(), d.d2.string(), "--data", (t.dir / "u").string(), "--bogus"}).exit, kUsage);
  CHECK_EQ(run_cli({"--data", (t.dir / "u").string(), "--help"}).exit, 0);

  // a disc that is not in the database / the same disc twice / a missing disc / wrong order (accepted)
  CHECK_EQ(run_cli([&] { auto a = base_args(d, t.dir / "i1", tk); a[1] = d.d_other.string(); return a; }()).exit, kBadDump);
  CHECK_EQ(run_cli([&] { auto a = base_args(d, t.dir / "i2", tk); a[1] = d.d1.string(); return a; }()).exit, kBadDump);
  CHECK_EQ(run_cli([&] { auto a = base_args(d, t.dir / "i3", tk); a[1] = (t.dir / "nope.iso").string(); return a; }()).exit, kBadDump);
  CHECK(!fs::exists(t.dir / "i1" / "game"));
  {
    auto a = base_args(d, t.dir / "i4", tk);
    std::swap(a[0], a[1]);
    CliResult r = run_cli(a);
    CHECK_EQ(r.exit, 0);
    CHECK(r.has("warn "));
  }
  // a damaged disc: flip a byte in a game file; the database no longer recognises it
  {
    auto bytes = slurp(d.d1);
    bytes[synth::kFiles + 5] ^= 0xFF;
    write_file(t.dir / "bad.iso", bytes);
    auto a = base_args(d, t.dir / "i5", tk);
    a[0] = (t.dir / "bad.iso").string();
    CliResult r = run_cli(a);
    CHECK_EQ(r.exit, kBadDump);
    CHECK(r.lines.back().find("not a recognised dump") != std::string::npos);
  }
  // toolkit problems
  CHECK_EQ(run_cli([&] { auto a = base_args(d, t.dir / "i6", tk); a[a.size() - 5] = (t.dir / "no-toolkit").string(); return a; }()).exit, kNoToolchain);
  CHECK_EQ(run_cli([&] { auto a = base_args(d, t.dir / "i7", tk); a[a.size() - 3] = "/nonexistent/c++"; return a; }()).exit, kNoToolchain);
  // recompiler and compiler failures
  setenv("FAKE_RECOMPILER_FAIL", "1", 1);
  CliResult rr = run_cli(base_args(d, t.dir / "i8", tk));
  unsetenv("FAKE_RECOMPILER_FAIL");
  CHECK_EQ(rr.exit, kRecompileFailed);
  CHECK(!fs::exists(t.dir / "i8" / "generated.partial"));
  CHECK(fs::exists(t.dir / "i8" / "game"));  // extraction is kept, so a retry needs no disc
  setenv("FAKE_CXX_FAIL", "1", 1);
  CliResult rc = run_cli(base_args(d, t.dir / "i9", tk));
  unsetenv("FAKE_CXX_FAIL");
  CHECK_EQ(rc.exit, kCompileFailed);
  CHECK(rc.lines.back().find("fake failure") != std::string::npos);
  CHECK(!fs::exists(t.dir / "i9" / "build" / "tmp"));
  // retry without the discs after fixing the compiler: only the failed stages run
  CliResult retry = run_cli([&] { auto a = base_args(d, t.dir / "i9", tk, false); a.push_back("--resume"); return a; }());
  CHECK_EQ(retry.exit, 0);
  CHECK_EQ(stage_names(retry, "skipped").size() >= 3, true);
  // not enough disk space (a need no volume can satisfy): exit 4 before anything is written for the stage
  const fs::path huge = make_toolkit(t.dir / "huge", d.dumps, 1e18);
  CliResult rs = run_cli(base_args(d, t.dir / "i10", huge));
  CHECK_EQ(rs.exit, kNoSpace);
  CHECK(!fs::exists(t.dir / "i10" / "game"));
  // two runs at once on one instance
  {
    setenv("FAKE_CXX_SLEEP", "3", 1);
    std::thread second;
    CliResult first;
    bool busy_seen = false;
    const fs::path inst = t.dir / "i11";
    auto args = base_args(d, inst, tk);
    first = run_cli(args, [&](const std::string& l) {
      if (!busy_seen && l.compare(0, 19, "stage-start compile") == 0) {
        busy_seen = true;
        CHECK_EQ(run_cli(args).exit, kBusy);
      }
    });
    unsetenv("FAKE_CXX_SLEEP");
    CHECK(busy_seen);
    CHECK_EQ(first.exit, 0);
  }
}

static void test_cancel() {
  Temp t;
  const Discs d = make_discs(t.dir);
  const fs::path tk = make_toolkit(t.dir, d.dumps);
  const fs::path cancel_file = t.dir / "cancel";
  const fs::path inst = t.dir / "inst";
  setenv("FAKE_CXX_SLEEP", "30", 1);
  auto args = base_args(d, inst, tk);
  args.insert(args.end(), {"--cancel-file", cancel_file.string()});
  const auto t0 = std::chrono::steady_clock::now();
  bool touched = false;
  CliResult r = run_cli(args, [&](const std::string& l) {
    if (!touched && l.compare(0, 19, "stage-start compile") == 0) {
      touched = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(800));  // let the compiler stubs start
      write_file(cancel_file, "");
    }
  });
  unsetenv("FAKE_CXX_SLEEP");
  CHECK(touched);
  CHECK_EQ(r.exit, kCancelled);
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20));
  CHECK(r.lines.back().compare(0, 20, "result cancelled 130") == 0);
  CHECK(!stage_names(r, "cancelled").empty() && stage_names(r, "cancelled")[0] == "compile");
  CHECK(!fs::exists(inst / "build" / "tmp"));
  CHECK(!fs::exists(inst / "game.partial"));
  CHECK(!fs::exists(inst / "bin" / "fake_game"));
  CHECK(fs::exists(inst / "game" / "sys" / "main.dol"));  // finished stages are kept
  CHECK_EQ(Json::parse(slurp(inst / "prepare.json")).get("state").str(), std::string("cancelled"));
  // the cancel file must not exist at start
  CHECK_EQ(run_cli(args).exit, kUsage);
  // and --resume continues from the compile stage, without discs
  fs::remove(cancel_file);
  CliResult again = run_cli([&] { auto a = base_args(d, inst, tk, false); a.push_back("--resume"); return a; }());
  CHECK_EQ(again.exit, 0);
  CHECK((stage_names(again, "skipped") == std::vector<std::string>{"verify", "extract", "recompile"}));
  CHECK(fs::exists(inst / "bin" / "fake_game"));
}

static void test_cancel_during_extract() {
  // Cancel while the discs are being copied: the cancel file exists from the start of the stage.
  Temp t;
  const Discs d = make_discs(t.dir);
  const fs::path tk = make_toolkit(t.dir, d.dumps);
  const fs::path cancel_file = t.dir / "cancel";
  const fs::path inst = t.dir / "inst";
  auto args = base_args(d, inst, tk);
  args.insert(args.end(), {"--cancel-file", cancel_file.string()});
  CliResult r = run_cli(args, [&](const std::string& l) {
    if (l.compare(0, 19, "stage-start extract") == 0) write_file(cancel_file, "");
  });
  CHECK_EQ(r.exit, kCancelled);
  CHECK(!fs::exists(inst / "game"));
  CHECK(!fs::exists(inst / "game.partial"));
  CHECK(!fs::exists(inst / "work"));
}

static void test_default_jobs() {
  const unsigned j = default_jobs({"clang++"});
  CHECK(j >= 1 && j <= 4096);
}

int main() {
  test_json();
  test_reporter();
  test_process();
  test_pipeline();
  test_default_jobs();
  test_end_to_end();
  test_failures();
  test_cancel();
  test_cancel_during_extract();
  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("all tos-prepare tests passed");
  return 0;
}
