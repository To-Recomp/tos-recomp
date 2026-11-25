// SPDX-License-Identifier: GPL-3.0-or-later
// SHA-256 (FIPS 180-4), for the integrity of the payload tos-prepare carries (payload.hpp). common has SHA-1 only,
// which is what the disc databases use; a payload hash should not be SHA-1.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace prep {

class Sha256 {
 public:
  using Digest = std::array<std::uint8_t, 32>;

  Sha256();
  void update(const void* data, std::size_t size);
  Digest finish();  // the object must not be used afterwards

  static std::string hex(const Digest& d);
  static std::string hex_of(const void* data, std::size_t size);

 private:
  void block(const std::uint8_t* p);

  std::uint32_t h_[8];
  std::uint8_t buf_[64];
  std::size_t fill_ = 0;
  std::uint64_t total_ = 0;
};

}  // namespace prep
