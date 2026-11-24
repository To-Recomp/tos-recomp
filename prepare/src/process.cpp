// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"

#include <sstream>

namespace prep {

std::vector<std::string> split_command(const std::string& command) {
  std::vector<std::string> out;
  std::istringstream in(command);
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

}  // namespace prep
