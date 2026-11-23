// SPDX-License-Identifier: GPL-3.0-or-later
// A small JSON value: enough for prepare.json and toolkit.json (objects keep a sorted key order so the
// files are reproducible). Numbers are doubles, integers up to 2^53 round-trip.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace prep {

class Json {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool b) : kind_(Kind::Bool), bool_(b) {}
  Json(int n) : kind_(Kind::Number), num_(n) {}
  Json(unsigned n) : kind_(Kind::Number), num_(n) {}
  Json(std::int64_t n) : kind_(Kind::Number), num_(static_cast<double>(n)) {}
  Json(std::uint64_t n) : kind_(Kind::Number), num_(static_cast<double>(n)) {}
  Json(double n) : kind_(Kind::Number), num_(n) {}
  Json(const char* s) : kind_(Kind::String), str_(s) {}
  Json(std::string s) : kind_(Kind::String), str_(std::move(s)) {}
  Json(Array a) : kind_(Kind::Array), arr_(std::make_shared<Array>(std::move(a))) {}
  Json(Object o) : kind_(Kind::Object), obj_(std::make_shared<Object>(std::move(o))) {}

  static Json object() { return Json(Object{}); }
  static Json array() { return Json(Array{}); }

  Kind kind() const { return kind_; }
  bool is_null() const { return kind_ == Kind::Null; }
  bool is_object() const { return kind_ == Kind::Object; }
  bool is_array() const { return kind_ == Kind::Array; }
  bool is_string() const { return kind_ == Kind::String; }

  // Typed reads; the `or` variants return the fallback when the value is missing or of another type.
  std::string str(const std::string& fallback = {}) const { return kind_ == Kind::String ? str_ : fallback; }
  double num(double fallback = 0) const { return kind_ == Kind::Number ? num_ : fallback; }
  bool boolean(bool fallback = false) const { return kind_ == Kind::Bool ? bool_ : fallback; }
  const Array& items() const;
  const Object& members() const;

  // Object access: operator[] inserts, get() returns Null for a missing key (never inserts).
  Json& operator[](const std::string& key);
  const Json& get(const std::string& key) const;
  bool has(const std::string& key) const { return kind_ == Kind::Object && obj_->count(key) != 0; }
  void push(Json v);

  std::string dump(int indent = 2) const;
  static Json parse(const std::string& text);  // throws std::runtime_error

 private:
  void write(std::string& out, int indent, int depth) const;
  // Copy on write is not needed: values are small and copied rarely; keep containers deep-copied.
  Kind kind_ = Kind::Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  std::shared_ptr<Array> arr_;
  std::shared_ptr<Object> obj_;
  friend class JsonParser;
};

}  // namespace prep
