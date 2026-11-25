// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline.hpp"

#include <chrono>

namespace prep {

State State::load(const fs::path& file) {
  State s;
  s.file_ = file;
  std::error_code ec;
  if (!fs::exists(file, ec)) return s;
  try {
    const Json j = Json::parse(read_text(file));
    s.overall = j.get("state").str("new");
    s.tool_version = j.get("tool_version").str();
    for (const auto& [name, rec] : j.get("stages").members()) {
      StageRecord r;
      r.status = rec.get("status").str();
      r.input = rec.get("input").str();
      r.finished = rec.get("finished").str();
      r.seconds = rec.get("seconds").num();
      r.info = rec.get("info").is_object() ? rec.get("info") : Json::object();
      s.stages_[name] = std::move(r);
    }
    for (const auto& [k, v] : j.members()) {
      if (k != "state" && k != "stages" && k != "tool_version" && k != "format" && k != "tool") s.meta[k] = v;
    }
  } catch (const std::exception&) {
    // An unreadable record means nothing can be trusted: start over.
    s = State();
    s.file_ = file;
  }
  return s;
}

void State::save() const {
  Json j = meta;
  j["format"] = 1;
  j["tool"] = "tos-prepare";
  j["tool_version"] = tool_version;
  j["state"] = overall;
  j["updated"] = utc_now();
  Json stages = Json::object();
  for (const auto& [name, r] : stages_) {
    Json s = Json::object();
    s["status"] = r.status;
    s["input"] = r.input;
    s["finished"] = r.finished;
    s["seconds"] = r.seconds;
    s["info"] = r.info;
    stages[name] = std::move(s);
  }
  j["stages"] = std::move(stages);
  write_text_atomic(file_, j.dump());
}

StageRecord* State::find(const std::string& stage) {
  auto it = stages_.find(stage);
  return it == stages_.end() ? nullptr : &it->second;
}

void State::erase_from(const std::vector<std::string>& names) {
  for (const auto& n : names) stages_.erase(n);
}

int run_pipeline(Context& ctx, std::vector<std::unique_ptr<Stage>>& stages, const std::string& tool_version) {
  Reporter& rep = ctx.rep;
  double total_weight = 0;
  for (auto& s : stages) total_weight += s->weight();
  ctx.state.tool_version = tool_version;
  ctx.state.set_file(ctx.state_file());

  std::vector<std::string> names;
  for (auto& s : stages) names.push_back(s->name());

  double base = ctx.progress_start;
  int index = ctx.stage_offset;
  Stage* current = nullptr;
  std::chrono::steady_clock::time_point started;
  auto seconds_since = [](std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
  };

  try {
    ctx.state.overall = "preparing";
    ctx.state.save();
    for (auto& stage : stages) {
      current = stage.get();
      ++index;
      const double span = stage->weight() / total_weight * (1.0 - ctx.progress_start);
      rep.set_stage(stage->name(), index, static_cast<int>(stages.size()) + ctx.stage_offset, stage->title(), base, span);
      started = std::chrono::steady_clock::now();
      ctx.cancel.check();

      const std::string hash = stage->input_hash(ctx);
      StageRecord* rec = ctx.state.find(stage->name());
      const bool finished_before = rec && rec->status == "done" && rec->input == hash;
      if (ctx.opt.resume && finished_before && stage->outputs_intact(ctx)) {
        rep.stage_end("skipped", 0);
        base += span;
        continue;
      }

      // Free space, with the need computed by the stage.
      const std::uint64_t need = stage->bytes_needed(ctx);
      if (need > 0) {
        const std::uint64_t have = ctx.free_space(ctx.data());
        rep.log("space: " + stage->name() + " needs " + human_bytes(need) + " (+" + human_bytes(ctx.opt.reserve_bytes) +
                " reserve), " + human_bytes(have) + " free");
        if (have < need + ctx.opt.reserve_bytes) {
          throw Error(kNoSpace, "not enough free disk space for the " + stage->name() + " stage: " + human_bytes(need) +
                                    " needed (plus " + human_bytes(ctx.opt.reserve_bytes) + " kept free), " +
                                    human_bytes(have) + " available in " + u8(ctx.data()));
        }
      }

      StageRecord& r = ctx.state.at(stage->name());
      r = StageRecord();
      r.status = "running";
      r.input = hash;
      ctx.state.save();

      stage->run(ctx);

      StageRecord& done = ctx.state.at(stage->name());
      done.status = "done";
      done.finished = utc_now();
      done.seconds = seconds_since(started);
      ctx.state.save();
      rep.stage_end("ok", done.seconds);
      base += span;
    }
    current = nullptr;
    ctx.state.overall = "ready";
    ctx.state.save();
    rep.result("ready", kOk, "the game is prepared in " + u8(ctx.data()));
    return kOk;
  } catch (const Cancelled&) {
    if (current) {
      current->clean_partial(ctx);
      ctx.state.at(current->name()).status = "cancelled";
      rep.stage_end("cancelled", seconds_since(started));
    }
    ctx.state.overall = "cancelled";
    try { ctx.state.save(); } catch (...) {}
    rep.result("cancelled", kCancelled, "cancelled");
    return kCancelled;
  } catch (const Error& e) {
    if (current) {
      current->clean_partial(ctx);
      ctx.state.at(current->name()).status = "failed";
      rep.stage_end("failed", seconds_since(started));
    }
    ctx.state.overall = "failed";
    try { ctx.state.save(); } catch (...) {}
    rep.result("failed", e.code(), e.what());
    return e.code();
  } catch (const std::exception& e) {
    if (current) {
      try { current->clean_partial(ctx); } catch (...) {}
      ctx.state.at(current->name()).status = "failed";
      rep.stage_end("failed", seconds_since(started));
    }
    ctx.state.overall = "failed";
    try { ctx.state.save(); } catch (...) {}
    rep.result("failed", kFailed, e.what());
    return kFailed;
  }
}

}  // namespace prep
