//===- CombinerUtils.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CombinerUtils.h"
#include "llvm/ADT/StringSet.h"

namespace llvm {

StringRef insertStrRef(StringRef S) {
  if (S.empty())
    return {};

  static StringSet<> Pool;
  auto [It, Inserted] = Pool.insert(S);
  return It->getKey();
}

} // namespace llvm
