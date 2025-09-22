//===- llvm/Testing/ADT/StringMap.h ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TESTING_ADT_STRINGMAP_H_
#define LLVM_TESTING_ADT_STRINGMAP_H_

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Testing/ADT/StringMapEntry.h"
#include <ostream>
#include <sstream>

namespace llvm {

/// Support for printing to std::ostream, for use with e.g. producing more
/// useful error messages with Google Test.
template <typename T>
std::ostream &operator<<(std::ostream &OS, const StringMap<T> &M) {
  if (M.empty()) {
    return OS << "{ }";
  }

  std::vector<std::string> Lines;
  for (const auto &E : M) {
    std::ostringstream SS;
    SS << E << ",";
    Lines.push_back(SS.str());
  }
  llvm::sort(Lines);
  Lines.insert(Lines.begin(), "{");
  Lines.insert(Lines.end(), "}");

  return OS << llvm::formatv("{0:$[\n]}",
                             make_range(Lines.begin(), Lines.end()))
                   .str();
}

} // namespace llvm

#endif
