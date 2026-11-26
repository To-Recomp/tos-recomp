// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic GameCube disc images for the tests and for tos-synth-discs: a handful of files in a valid file system
// table, no game data (adapted from To-Recomp/common's tests).
#pragma once

#include <algorithm>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <sstream>
#include <vector>

#include "torecomp/content.hpp"
#include "torecomp/gcm.hpp"

namespace synth {
struct Node {
  bool dir = true;
  std::string data;
  std::map<std::string, Node> kids;
};
using Bytes = std::vector<std::uint8_t>;

inline void put32(Bytes& b, std::size_t off, std::uint32_t v) {
  b[off] = v >> 24; b[off + 1] = v >> 16; b[off + 2] = v >> 8; b[off + 3] = v;
}
inline void add_file(Node& root, const std::string& path, const std::string& data) {
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
inline void flatten(const Node& n, std::uint32_t parent, std::vector<Item>& out) {
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
inline constexpr std::size_t kFst = 0x4000, kFiles = 0x10000;

inline Bytes build_image(const Node& root, const char* id, std::uint8_t disc, const Bytes* dol = nullptr) {
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
  if (dol) {
    // A real (tiny) main.dol: the room between the apploader at 0x2440 and the file system table.
    std::copy(dol->begin(), dol->end(), img.begin() + 0x3000);
  } else {
    std::fill(img.begin() + 0x3000, img.begin() + 0x3000 + 0x120, 0x22);
    std::fill(img.begin() + 0x3000, img.begin() + 0x3000 + 0x100, 0);
    put32(img, 0x3000, 0x100);
    put32(img, 0x3000 + 0x90, 0x20);
  }
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

// dumps.toml text that makes the given images (disc 1, disc 2, ...) known dumps of one release of `game_id`.
inline std::string dumps_toml(const std::string& game_id, const std::vector<std::filesystem::path>& discs) {
  std::ostringstream toml;
  toml << "format = 1\n[[release]]\nid = \"test\"\nplatform = \"gamecube\"\nregion = \"Test\"\ntitle = \"Test Game\"\n"
          "status = \"alpha\"\ntested = false\n";
  int n = 0;
  for (const auto& p : discs) {
    torecomp::gcm::Image img(p);
    const auto m = torecomp::build_manifest(img);
    toml << "[[release.disc]]\ngame_id = \"" << game_id << "\"\ndisc = " << n++ << "\nversion = 0\nsize = " << img.size()
         << "\ntitle = \"Test Game disc " << n << "\"\nserial = \"DL-DOL-" << game_id << "-" << (n - 1) << "-TST\"\ncontent_sha1 = \"" << m.content_sha1 << "\"\n";
  }
  return toml.str();
}

}  // namespace synth
