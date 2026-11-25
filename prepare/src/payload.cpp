// SPDX-License-Identifier: GPL-3.0-or-later
#include "payload.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#ifdef TORECOMP_HAVE_LZMA
#include <lzma.h>
#endif

namespace prep {

namespace {

constexpr char kMagic[8] = {'T', 'O', 'S', 'P', 'A', 'Y', 'L', 'D'};
constexpr char kStreamMagic[4] = {'T', 'O', 'S', 'A'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kChunk = 1u << 20;
constexpr std::size_t kMaxPath = 1024;

enum Kind : std::uint8_t { kEnd = 0, kFile = 1, kDir = 2 };

void put_le(std::string& s, std::uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) s += static_cast<char>((v >> (8 * i)) & 0xff);
}
std::uint64_t get_le(const std::uint8_t* p, int bytes) {
  std::uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return v;
}

std::uint64_t file_size_of(const fs::path& f) {
  std::error_code ec;
  const auto n = fs::file_size(f, ec);
  if (ec) throw Error(kFailed, "cannot read the size of " + u8(f) + ": " + ec.message());
  return n;
}

bool read_at(std::ifstream& in, std::uint64_t offset, void* buf, std::size_t n) {
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(static_cast<char*>(buf), static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(in.gcount()) == n;
}

#ifndef TORECOMP_HAVE_LZMA
[[noreturn]] void no_lzma() { throw Error(kFailed, "this tos-prepare was built without liblzma: it cannot read or write a payload"); }
#endif

}  // namespace

std::string PayloadInfo::id() const { return sha256_hex().substr(0, 16); }

bool safe_relative_path(const std::string& p) {
  if (p.empty() || p.size() > kMaxPath || p[0] == '/') return false;
  std::size_t start = 0;
  while (start <= p.size()) {
    std::size_t end = p.find('/', start);
    if (end == std::string::npos) end = p.size();
    const std::string comp = p.substr(start, end - start);
    if (comp.empty() || comp == "." || comp == "..") return false;
    start = end + 1;
  }
  for (const unsigned char c : p) {
    if (c == '\\' || c == ':' || c < 0x20) return false;
  }
  return true;
}

std::uint64_t payload_end(const fs::path& file) {
  const std::uint64_t size = file_size_of(file);
  std::ifstream in(file, std::ios::binary);
  if (!in) throw Error(kFailed, "cannot read " + u8(file));
  std::uint8_t dos[64];
  if (!read_at(in, 0, dos, sizeof dos) || dos[0] != 'M' || dos[1] != 'Z') return size;
  const std::uint64_t pe = get_le(dos + 0x3c, 4);
  std::uint8_t head[24];
  if (pe + sizeof head > size || !read_at(in, pe, head, sizeof head) || std::memcmp(head, "PE\0\0", 4) != 0) return size;
  const std::uint64_t opt = pe + 24;
  std::uint8_t magic[2];
  if (!read_at(in, opt, magic, 2)) return size;
  const std::uint64_t kind = get_le(magic, 2);
  const std::uint64_t dirs = opt + (kind == 0x20b ? 112 : kind == 0x10b ? 96 : 0);
  if (dirs == opt) return size;
  std::uint8_t security[8];  // data directory 4: the certificate table; its "address" is a file offset
  if (!read_at(in, dirs + 4 * 8, security, sizeof security)) return size;
  const std::uint64_t at = get_le(security, 4), len = get_le(security + 4, 4);
  if (len == 0 || at == 0 || at > size || at + len > size) return size;
  return at;
}

std::optional<PayloadInfo> find_payload(const fs::path& exe) {
  const std::uint64_t end = payload_end(exe);
  if (end < kTrailerSize) return std::nullopt;
  std::ifstream in(exe, std::ios::binary);
  if (!in) throw Error(kFailed, "cannot read " + u8(exe));
  std::uint8_t t[kTrailerSize];
  if (!read_at(in, end - kTrailerSize, t, sizeof t)) throw Error(kFailed, "cannot read the end of " + u8(exe));
  if (std::memcmp(t, kMagic, sizeof kMagic) != 0) return std::nullopt;
  if (get_le(t + 8, 4) != kVersion) throw Error(kFailed, u8(exe) + " carries a payload of a version this program does not know");
  PayloadInfo info;
  info.file = exe;
  info.packed_size = get_le(t + 16, 8);
  info.unpacked_size = get_le(t + 24, 8);
  std::memcpy(info.sha256.data(), t + 32, 32);
  if (info.packed_size == 0 || info.packed_size > end - kTrailerSize) {
    throw Error(kFailed, u8(exe) + ": the payload trailer is damaged (the file was cut or modified after packing)");
  }
  info.offset = end - kTrailerSize - info.packed_size;
  return info;
}

// ------------------------------------------------------------------------------------------------ packing

#ifdef TORECOMP_HAVE_LZMA
namespace {

// Compresses what is written to it into an output file, hashing and counting the packed bytes.
class XzSink {
 public:
  XzSink(std::ostream& out, const PackOptions& opt) : out_(out) {
    const std::uint32_t preset = opt.preset | (opt.extreme ? LZMA_PRESET_EXTREME : 0);
    unsigned threads = opt.threads;
    lzma_ret r;
    if (threads == 1) {
      r = lzma_easy_encoder(&strm_, preset, LZMA_CHECK_CRC64);
    } else {
      if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
      lzma_mt mt{};
      mt.threads = threads;
      mt.preset = preset;
      mt.check = LZMA_CHECK_CRC64;
      r = lzma_stream_encoder_mt(&strm_, &mt);
    }
    if (r != LZMA_OK) throw Error(kFailed, "cannot start the LZMA encoder (" + std::to_string(r) + ")");
  }
  ~XzSink() { lzma_end(&strm_); }

  void write(const void* data, std::size_t n) { run(data, n, LZMA_RUN); }
  void finish() { run(nullptr, 0, LZMA_FINISH); }
  std::uint64_t packed() const { return packed_; }
  Sha256::Digest digest() { return hash_.finish(); }

 private:
  void run(const void* data, std::size_t n, lzma_action action) {
    strm_.next_in = static_cast<const std::uint8_t*>(data);
    strm_.avail_in = n;
    for (;;) {
      std::uint8_t buf[1 << 16];
      strm_.next_out = buf;
      strm_.avail_out = sizeof buf;
      const lzma_ret r = lzma_code(&strm_, action);
      const std::size_t got = sizeof buf - strm_.avail_out;
      if (got > 0) {
        out_.write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(got));
        if (!out_) throw Error(kFailed, "cannot write the packed payload (disk full?)");
        hash_.update(buf, got);
        packed_ += got;
      }
      if (r == LZMA_STREAM_END) return;
      if (r != LZMA_OK) throw Error(kFailed, "the LZMA encoder failed (" + std::to_string(r) + ")");
      if (action == LZMA_RUN && strm_.avail_in == 0 && strm_.avail_out != 0) return;
    }
  }

  std::ostream& out_;
  lzma_stream strm_ = LZMA_STREAM_INIT;
  Sha256 hash_;
  std::uint64_t packed_ = 0;
};

struct Entry {
  std::string path;
  fs::path file;
  bool dir = false;
  bool exec = false;
  std::uint64_t size = 0;
};

std::vector<Entry> list_tree(const fs::path& dir) {
  std::vector<Entry> v;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::none, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path& p = it->path();
    const fs::file_status st = it->symlink_status(ec);
    if (fs::is_symlink(st) || (!fs::is_directory(st) && !fs::is_regular_file(st))) {
      throw Error(kFailed, "the payload folder holds a symbolic link or special file, which a payload does not carry: " + u8(p));
    }
    Entry e;
    e.path = u8(fs::relative(p, dir, ec));
    std::replace(e.path.begin(), e.path.end(), '\\', '/');
    if (!safe_relative_path(e.path)) throw Error(kFailed, "a name in the payload folder cannot be carried: " + u8(p));
    e.file = p;
    e.dir = fs::is_directory(st);
    e.exec = !e.dir && (st.permissions() & fs::perms::owner_exec) != fs::perms::none;
    if (!e.dir) e.size = file_size_of(p);
    v.push_back(std::move(e));
  }
  if (ec) throw Error(kFailed, "cannot read " + u8(dir) + ": " + ec.message());
  std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
  return v;
}

}  // namespace
#endif

PayloadInfo pack_payload(const fs::path& dir, const fs::path& base_exe, const fs::path& out, const PackOptions& opt,
                         const PackProgress& progress) {
#ifndef TORECOMP_HAVE_LZMA
  (void)dir; (void)base_exe; (void)out; (void)opt; (void)progress;
  no_lzma();
#else
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) throw Error(kFailed, "not a folder: " + u8(dir));
  if (fs::equivalent(base_exe, out, ec)) throw Error(kFailed, "the output must not be the program it is made from");
  const std::vector<Entry> entries = list_tree(dir);
  std::uint64_t total = 8 + 1;
  for (const Entry& e : entries) total += 1 + 2 + e.path.size() + (e.dir ? 0 : 1 + 8 + e.size);

  // The program, without a payload it may already carry.
  std::uint64_t base_len = file_size_of(base_exe);
  if (const auto old = find_payload(base_exe)) base_len = old->offset;
  if (payload_end(base_exe) != file_size_of(base_exe)) {
    throw Error(kFailed, "a signed program cannot take a payload: attach it first, then sign");
  }

  std::ofstream o(out, std::ios::binary | std::ios::trunc);
  if (!o) throw Error(kFailed, "cannot create " + u8(out));
  {
    std::ifstream in(base_exe, std::ios::binary);
    if (!in) throw Error(kFailed, "cannot read " + u8(base_exe));
    std::vector<char> buf(kChunk);
    for (std::uint64_t left = base_len; left > 0;) {
      const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
      in.read(buf.data(), static_cast<std::streamsize>(n));
      if (static_cast<std::size_t>(in.gcount()) != n) throw Error(kFailed, "cannot read " + u8(base_exe));
      o.write(buf.data(), static_cast<std::streamsize>(n));
      left -= n;
    }
  }

  XzSink xz(o, opt);
  std::uint64_t done = 0;
  auto put = [&](const void* p, std::size_t n) {
    xz.write(p, n);
    done += n;
    if (progress) progress(done, total);
  };
  std::string head(kStreamMagic, 4);
  put_le(head, kVersion, 4);
  put(head.data(), head.size());
  std::vector<char> buf(kChunk);
  for (const Entry& e : entries) {
    std::string h;
    h += static_cast<char>(e.dir ? kDir : kFile);
    put_le(h, e.path.size(), 2);
    h += e.path;
    if (!e.dir) {
      h += static_cast<char>(e.exec ? 1 : 0);
      put_le(h, e.size, 8);
    }
    put(h.data(), h.size());
    if (e.dir) continue;
    std::ifstream in(e.file, std::ios::binary);
    if (!in) throw Error(kFailed, "cannot read " + u8(e.file));
    for (std::uint64_t left = e.size; left > 0;) {
      const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
      in.read(buf.data(), static_cast<std::streamsize>(n));
      if (static_cast<std::size_t>(in.gcount()) != n) throw Error(kFailed, "cannot read " + u8(e.file) + " (changed while packing?)");
      put(buf.data(), n);
      left -= n;
    }
  }
  const char end = static_cast<char>(kEnd);
  put(&end, 1);
  xz.finish();

  PayloadInfo info;
  info.file = out;
  info.packed_size = xz.packed();
  info.unpacked_size = done;
  info.sha256 = xz.digest();
  info.offset = base_len;
  std::string t(kMagic, sizeof kMagic);
  put_le(t, kVersion, 4);
  put_le(t, 0, 4);
  put_le(t, info.packed_size, 8);
  put_le(t, info.unpacked_size, 8);
  t.append(reinterpret_cast<const char*>(info.sha256.data()), info.sha256.size());
  o.write(t.data(), static_cast<std::streamsize>(t.size()));
  o.flush();
  if (!o) throw Error(kFailed, "cannot write " + u8(out) + " (disk full?)");
  return info;
#endif
}

// ----------------------------------------------------------------------------------------------- unpacking

#ifdef TORECOMP_HAVE_LZMA
namespace {

// Consumes the archive stream a piece at a time and writes what it describes below `dest`.
class ArchiveReader {
 public:
  ArchiveReader(fs::path dest, const PackProgress& progress, std::uint64_t total) : dest_(std::move(dest)), progress_(progress), total_(total) {}
  ~ArchiveReader() { close_file(); }

  void feed(const std::uint8_t* p, std::size_t n) {
    consumed_ += n;
    while (n > 0) {
      if (state_ == State::kDone) throw Error(kFailed, "the payload has data after its end");
      if (state_ == State::kData) {
        const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(n, left_));
        if (k > 0 && std::fwrite(p, 1, k, file_) != k) throw Error(kFailed, "cannot write " + u8(current_) + " (disk full?)");
        left_ -= k; p += k; n -= k;
        if (left_ == 0) {
          close_file();
          state_ = State::kKind;
          want_ = 1;
        }
        continue;
      }
      const std::size_t k = std::min(n, want_ - acc_.size());
      acc_.insert(acc_.end(), p, p + k);
      p += k; n -= k;
      if (acc_.size() == want_) step();
    }
    if (progress_) progress_(consumed_, total_);
  }

  void finish() {
    if (state_ != State::kDone) throw Error(kFailed, "the payload ends too early");
  }
  std::uint64_t consumed() const { return consumed_; }
  std::string manifest() const {
    std::string s;
    for (const auto& [path, size] : files_) s += std::to_string(size) + " " + path + "\n";
    return s;
  }

 private:
  enum class State { kHeader, kKind, kPathLen, kPath, kFlags, kSize, kData, kDone };

  void close_file() {
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

  void step() {
    const std::vector<std::uint8_t> a = std::move(acc_);
    acc_.clear();
    switch (state_) {
      case State::kHeader:
        if (std::memcmp(a.data(), kStreamMagic, 4) != 0 || get_le(a.data() + 4, 4) != kVersion) throw Error(kFailed, "the payload is not in a format this program reads");
        state_ = State::kKind; want_ = 1;
        break;
      case State::kKind:
        kind_ = a[0];
        if (kind_ == kEnd) { state_ = State::kDone; want_ = 0; }
        else if (kind_ == kFile || kind_ == kDir) { state_ = State::kPathLen; want_ = 2; }
        else throw Error(kFailed, "the payload holds an entry of an unknown kind");
        break;
      case State::kPathLen:
        want_ = static_cast<std::size_t>(get_le(a.data(), 2));
        if (want_ == 0 || want_ > kMaxPath) throw Error(kFailed, "the payload holds a bad path");
        state_ = State::kPath;
        break;
      case State::kPath: {
        rel_.assign(a.begin(), a.end());
        if (!safe_relative_path(rel_)) throw Error(kFailed, "the payload holds an unsafe path: " + rel_);
        current_ = dest_ / from_u8(rel_);
        if (kind_ == kDir) {
          std::error_code ec;
          fs::create_directories(current_, ec);
          if (ec) throw Error(kFailed, "cannot create " + u8(current_) + ": " + ec.message());
          state_ = State::kKind; want_ = 1;
        } else {
          state_ = State::kFlags; want_ = 1;
        }
        break;
      }
      case State::kFlags:
        exec_ = (a[0] & 1) != 0;
        state_ = State::kSize; want_ = 8;
        break;
      case State::kSize: {
        left_ = get_le(a.data(), 8);
        std::error_code ec;
        fs::create_directories(current_.parent_path(), ec);
#if defined(_WIN32)
        file_ = _wfopen(current_.c_str(), L"wb");
#else
        file_ = std::fopen(current_.c_str(), "wb");
#endif
        if (!file_) throw Error(kFailed, "cannot create " + u8(current_));
        files_[rel_] = left_;
        if (exec_) {
          fs::permissions(current_, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
        }
        if (left_ == 0) { close_file(); state_ = State::kKind; want_ = 1; }
        else state_ = State::kData;
        break;
      }
      default:
        break;
    }
  }

  fs::path dest_;
  const PackProgress& progress_;
  std::uint64_t total_;
  State state_ = State::kHeader;
  std::size_t want_ = 8;
  std::vector<std::uint8_t> acc_;
  std::uint8_t kind_ = 0;
  std::string rel_;
  fs::path current_;
  bool exec_ = false;
  std::uint64_t left_ = 0;
  std::uint64_t consumed_ = 0;
  std::FILE* file_ = nullptr;
  std::map<std::string, std::uint64_t> files_;
};

}  // namespace
#endif

std::string unpack_payload(const PayloadInfo& info, const fs::path& dest, CancelToken& cancel, const PackProgress& progress) {
#ifndef TORECOMP_HAVE_LZMA
  (void)info; (void)dest; (void)cancel; (void)progress;
  no_lzma();
#else
  std::error_code ec;
  fs::create_directories(dest, ec);
  if (ec) throw Error(kFailed, "cannot create " + u8(dest) + ": " + ec.message());
  std::ifstream in(info.file, std::ios::binary);
  if (!in) throw Error(kFailed, "cannot read " + u8(info.file));
  in.seekg(static_cast<std::streamoff>(info.offset));

  lzma_stream strm = LZMA_STREAM_INIT;
  if (lzma_stream_decoder(&strm, UINT64_MAX, 0) != LZMA_OK) throw Error(kFailed, "cannot start the LZMA decoder");
  struct End { lzma_stream* s; ~End() { lzma_end(s); } } end{&strm};

  ArchiveReader reader(dest, progress, info.unpacked_size);
  Sha256 hash;
  std::vector<std::uint8_t> inbuf(kChunk), outbuf(kChunk);
  std::uint64_t left = info.packed_size;
  bool stream_end = false;
  while (!stream_end) {
    cancel.check();
    if (strm.avail_in == 0) {
      if (left == 0) throw Error(kFailed, "the payload is truncated");
      const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, inbuf.size()));
      in.read(reinterpret_cast<char*>(inbuf.data()), static_cast<std::streamsize>(n));
      if (static_cast<std::size_t>(in.gcount()) != n) throw Error(kFailed, "cannot read the payload from " + u8(info.file));
      hash.update(inbuf.data(), n);
      left -= n;
      strm.next_in = inbuf.data();
      strm.avail_in = n;
    }
    strm.next_out = outbuf.data();
    strm.avail_out = outbuf.size();
    const lzma_ret r = lzma_code(&strm, LZMA_RUN);
    const std::size_t got = outbuf.size() - strm.avail_out;
    if (got > 0) reader.feed(outbuf.data(), got);
    if (r == LZMA_STREAM_END) stream_end = true;
    else if (r != LZMA_OK) throw Error(kFailed, "the payload is damaged (LZMA error " + std::to_string(r) + ")");
  }
  // Whatever of the packed bytes the decoder did not need still counts for the hash.
  if (strm.avail_in > 0) throw Error(kFailed, "the payload has data after the compressed stream");
  while (left > 0) {
    const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, inbuf.size()));
    in.read(reinterpret_cast<char*>(inbuf.data()), static_cast<std::streamsize>(n));
    if (static_cast<std::size_t>(in.gcount()) != n) throw Error(kFailed, "cannot read the payload from " + u8(info.file));
    hash.update(inbuf.data(), n);
    left -= n;
  }
  reader.finish();
  if (reader.consumed() != info.unpacked_size) throw Error(kFailed, "the payload's size is not the one recorded in its trailer");
  if (hash.finish() != info.sha256) throw Error(kFailed, "the payload's SHA-256 is not the one recorded in its trailer: the file is damaged");
  return reader.manifest();
#endif
}

bool unpacked_intact(const fs::path& root) {
  std::ifstream in(root / "MANIFEST");
  if (!in) return false;
  std::string line;
  std::size_t files = 0;
  while (std::getline(in, line)) {
    const std::size_t sp = line.find(' ');
    if (sp == std::string::npos || sp == 0) return false;
    std::uint64_t size = 0;
    try { size = std::stoull(line.substr(0, sp)); } catch (const std::exception&) { return false; }
    const std::string rel = line.substr(sp + 1);
    if (!safe_relative_path(rel)) return false;
    std::error_code ec;
    const fs::path p = root / from_u8(rel);
    if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != size || ec) return false;
    ++files;
  }
  return files > 0;
}

}  // namespace prep
