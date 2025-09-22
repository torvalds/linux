//===- DWARFRelocMap.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFRELOCMAP_H
#define LLVM_DEBUGINFO_DWARF_DWARFRELOCMAP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/RelocationResolver.h"
#include <cstdint>

namespace llvm {

/// RelocAddrEntry contains relocated value and section index.
/// Section index is -1LL if relocation points to absolute symbol.
struct RelocAddrEntry {
  uint64_t SectionIndex;
  object::RelocationRef Reloc;
  uint64_t SymbolValue;
  std::optional<object::RelocationRef> Reloc2;
  uint64_t SymbolValue2;
  object::RelocationResolver Resolver;
};

/// In place of applying the relocations to the data we've read from disk we use
/// a separate mapping table to the side and checking that at locations in the
/// dwarf where we expect relocated values. This adds a bit of complexity to the
/// dwarf parsing/extraction at the benefit of not allocating memory for the
/// entire size of the debug info sections.
using RelocAddrMap = DenseMap<uint64_t, RelocAddrEntry>;

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFRELOCMAP_H
