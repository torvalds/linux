//===- ArchitectureSet.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the architecture set.
//
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace MachO {

ArchitectureSet::ArchitectureSet(const std::vector<Architecture> &Archs)
    : ArchitectureSet() {
  for (auto Arch : Archs) {
    if (Arch == AK_unknown)
      continue;
    set(Arch);
  }
}

size_t ArchitectureSet::count() const {
  // popcnt
  size_t Cnt = 0;
  for (unsigned i = 0; i < sizeof(ArchSetType) * 8; ++i)
    if (ArchSet & (1U << i))
      ++Cnt;
  return Cnt;
}

ArchitectureSet::operator std::string() const {
  if (empty())
    return "[(empty)]";

  std::string result;
  auto size = count();
  for (auto arch : *this) {
    result.append(std::string(getArchitectureName(arch)));
    size -= 1;
    if (size)
      result.append(" ");
  }
  return result;
}

ArchitectureSet::operator std::vector<Architecture>() const {
  std::vector<Architecture> archs;
  for (auto arch : *this) {
    if (arch == AK_unknown)
      continue;
    archs.emplace_back(arch);
  }
  return archs;
}

void ArchitectureSet::print(raw_ostream &os) const { os << std::string(*this); }

raw_ostream &operator<<(raw_ostream &os, ArchitectureSet set) {
  set.print(os);
  return os;
}

} // end namespace MachO.
} // end namespace llvm.
