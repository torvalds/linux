//===- StringTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_STRINGTABLE_H
#define LLVM_DEBUGINFO_GSYM_STRINGTABLE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include <stdint.h>

namespace llvm {
namespace gsym {

/// String tables in GSYM files are required to start with an empty
/// string at offset zero. Strings must be UTF8 NULL terminated strings.
struct StringTable {
  StringRef Data;
  StringTable() = default;
  StringTable(StringRef D) : Data(D) {}
  StringRef operator[](size_t Offset) const { return getString(Offset); }
  StringRef getString(uint32_t Offset) const {
    if (Offset < Data.size()) {
      auto End = Data.find('\0', Offset);
      return Data.substr(Offset, End - Offset);
    }
    return StringRef();
  }
  void clear() { Data = StringRef(); }
};

inline raw_ostream &operator<<(raw_ostream &OS, const StringTable &S) {
  OS << "String table:\n";
  uint32_t Offset = 0;
  const size_t Size = S.Data.size();
  while (Offset < Size) {
    StringRef Str = S.getString(Offset);
    OS << HEX32(Offset) << ": \"" << Str << "\"\n";
    Offset += Str.size() + 1;
  }
  return OS;
}

} // namespace gsym
} // namespace llvm
#endif // LLVM_DEBUGINFO_GSYM_STRINGTABLE_H
