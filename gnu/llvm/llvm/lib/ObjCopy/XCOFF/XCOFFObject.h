//===- XCOFFObject.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_XCOFF_XCOFFOBJECT_H
#define LLVM_LIB_OBJCOPY_XCOFF_XCOFFOBJECT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/XCOFFObjectFile.h"
#include <vector>

namespace llvm {
namespace objcopy {
namespace xcoff {

using namespace object;

struct Section {
  XCOFFSectionHeader32 SectionHeader;
  ArrayRef<uint8_t> Contents;
  std::vector<XCOFFRelocation32> Relocations;
};

struct Symbol {
  XCOFFSymbolEntry32 Sym;
  // For now, each auxiliary symbol is only an opaque binary blob with no
  // distinction.
  StringRef AuxSymbolEntries;
};

struct Object {
  XCOFFFileHeader32 FileHeader;
  XCOFFAuxiliaryHeader32 OptionalFileHeader;
  std::vector<Section> Sections;
  std::vector<Symbol> Symbols;
  StringRef StringTable;
};

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_XCOFF_XCOFFOBJECT_H
