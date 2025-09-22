//===- SymbolSize.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_SYMBOLSIZE_H
#define LLVM_OBJECT_SYMBOLSIZE_H

#include "llvm/Object/ObjectFile.h"

namespace llvm {
namespace object {

struct SymEntry {
  symbol_iterator I;
  uint64_t Address;
  unsigned Number;
  unsigned SectionID;
};

int compareAddress(const SymEntry *A, const SymEntry *B);

std::vector<std::pair<SymbolRef, uint64_t>>
computeSymbolSizes(const ObjectFile &O);

}
} // namespace llvm

#endif
