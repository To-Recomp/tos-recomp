// SPDX-License-Identifier: GPL-3.0-or-later
#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace prep {

namespace {
const Json::Array kNoItems;
const Json::Object kNoMembers;
const Json kNull;
}  // namespace

const Json::Array& Json::items() const { return kind_ == Kind::Array ? *arr_ : kNoItems; }
const Json::Object& Json::members() const { return kind_ == Kind::Object ? *obj_ : kNoMembers; }

Json& Json::operator[](const std::string& key) {
  if (kind_ == Kind::Null) *this = object();
  if (kind_ != Kind::Object) throw std::runtime_error("json: not an object");
  // Deep copy when shared, so a copied value never aliases its source.
  if (obj_.use_count() > 1) obj_ = std::make_shared<Object>(*obj_);
  return (*obj_)[key];
}

const Json& Json::get(const std::string& key) const {
  if (kind_ != Kind::Object) return kNull;
  auto it = obj_->find(key);
  return it == obj_->end() ? kNull : it->second;
}

void Json::push(Json v) {
  if (kind_ == Kind::Null) *this = array();
  if (kind_ != Kind::Array) throw std::runtime_error("json: not an array");
  if (arr_.use_count() > 1) arr_ = std::make_shared<Array>(*arr_);
  arr_->push_back(std::move(v));
}

static void write_string(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void Json::write(std::string& out, int indent, int depth) const {
  auto nl = [&](int d) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<std::size_t>(indent * d), ' ');
  };
  switch (kind_) {
    case Kind::Null: out += "null"; break;
    case Kind::Bool: out += bool_ ? "true" : "false"; break;
    case Kind::Number: {
      char buf[40];
      if (num_ == std::floor(num_) && std::fabs(num_) < 9e15) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(num_));
      } else {
        std::snprintf(buf, sizeof buf, "%.6g", num_);
      }
      out += buf;
      break;
    }
    case Kind::String: write_string(out, str_); break;
    case Kind::Array: {
      if (arr_->empty()) { out += "[]"; break; }
      out += '[';
      bool first = true;
      for (const auto& v : *arr_) {
        if (!first) out += ',';
        first = false;
        nl(depth + 1);
        v.write(out, indent, depth + 1);
      }
      nl(depth);
      out += ']';
      break;
    }
    case Kind::Object: {
      if (obj_->empty()) { out += "{}"; break; }
      out += '{';
      bool first = true;
      for (const auto& [k, v] : *obj_) {
        if (!first) out += ',';
        first = false;
        nl(depth + 1);
        write_string(out, k);
        out += indent > 0 ? ": " : ":";
        v.write(out, indent, depth + 1);
      }
      nl(depth);
      out += '}';
      break;
    }
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  write(out, indent, 0);
  if (indent > 0) out += '\n';
  return out;
}

class JsonParser {
 public:
  explicit JsonParser(const std::string& t) : t_(t) {}
  Json parse() {
    Json v = value();
    ws();
    if (p_ != t_.size()) fail("trailing characters");
    return v;
  }

 private:
  [[noreturn]] void fail(const char* what) const {
    throw std::runtime_error(std::string("json: ") + what + " at offset " + std::to_string(p_));
  }
  void ws() {
    while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\n' || t_[p_] == '\t' || t_[p_] == '\r')) ++p_;
  }
  bool eat(char c) {
    ws();
    if (p_ < t_.size() && t_[p_] == c) { ++p_; return true; }
    return false;
  }
  void expect(char c) {
    if (!eat(c)) fail("unexpected character");
  }
  Json value() {
    ws();
    if (p_ >= t_.size()) fail("unexpected end");
    if (++depth_ > 64) fail("too deep");
    Json out;
    const char c = t_[p_];
    if (c == '{') {
      ++p_;
      out = Json::object();
      if (!eat('}')) {
        do {
          ws();
          std::string k = string();
          expect(':');
          out[k] = value();
        } while (eat(','));
        expect('}');
      }
    } else if (c == '[') {
      ++p_;
      out = Json::array();
      if (!eat(']')) {
        do { out.push(value()); } while (eat(','));
        expect(']');
      }
    } else if (c == '"') {
      out = Json(string());
    } else if (t_.compare(p_, 4, "true") == 0) {
      p_ += 4; out = Json(true);
    } else if (t_.compare(p_, 5, "false") == 0) {
      p_ += 5; out = Json(false);
    } else if (t_.compare(p_, 4, "null") == 0) {
      p_ += 4;
    } else {
      char* end = nullptr;
      const double d = std::strtod(t_.c_str() + p_, &end);
      if (end == t_.c_str() + p_) fail("unexpected character");
      p_ = static_cast<std::size_t>(end - t_.c_str());
      out = Json(d);
    }
    --depth_;
    return out;
  }
  std::string string() {
    if (p_ >= t_.size() || t_[p_] != '"') fail("string expected");
    ++p_;
    std::string s;
    while (p_ < t_.size() && t_[p_] != '"') {
      char c = t_[p_++];
      if (c != '\\') { s += c; continue; }
      if (p_ >= t_.size()) fail("bad escape");
      c = t_[p_++];
      switch (c) {
        case 'n': s += '\n'; break;
        case 't': s += '\t'; break;
        case 'r': s += '\r'; break;
        case 'b': s += '\b'; break;
        case 'f': s += '\f'; break;
        case 'u': {
          if (p_ + 4 > t_.size()) fail("bad escape");
          unsigned cp = static_cast<unsigned>(std::strtoul(t_.substr(p_, 4).c_str(), nullptr, 16));
          p_ += 4;
          if (cp < 0x80) {
            s += static_cast<char>(cp);
          } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
          } else {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
          }
          break;
        }
        default: s += c;  // " \ /
      }
    }
    if (p_ >= t_.size()) fail("unterminated string");
    ++p_;
    return s;
  }

  const std::string& t_;
  std::size_t p_ = 0;
  int depth_ = 0;
};

Json Json::parse(const std::string& text) { return JsonParser(text).parse(); }

}  // namespace prep
