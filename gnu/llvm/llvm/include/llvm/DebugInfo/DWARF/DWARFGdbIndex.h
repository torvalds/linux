//===- DWARFGdbIndex.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFGDBINDEX_H
#define LLVM_DEBUGINFO_DWARF_DWARFGDBINDEX_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <utility>

namespace llvm {

class raw_ostream;
class DataExtractor;

class DWARFGdbIndex {
  uint32_t Version;

  uint32_t CuListOffset;
  uint32_t TuListOffset;
  uint32_t AddressAreaOffset;
  uint32_t SymbolTableOffset;
  uint32_t ConstantPoolOffset;

  struct CompUnitEntry {
    uint64_t Offset; /// Offset of a CU in the .debug_info section.
    uint64_t Length; /// Length of that CU.
  };
  SmallVector<CompUnitEntry, 0> CuList;

  struct TypeUnitEntry {
    uint64_t Offset;
    uint64_t TypeOffset;
    uint64_t TypeSignature;
  };
  SmallVector<TypeUnitEntry, 0> TuList;

  struct AddressEntry {
    uint64_t LowAddress;  /// The low address.
    uint64_t HighAddress; /// The high address.
    uint32_t CuIndex;     /// The CU index.
  };
  SmallVector<AddressEntry, 0> AddressArea;

  struct SymTableEntry {
    uint32_t NameOffset; /// Offset of the symbol's name in the constant pool.
    uint32_t VecOffset;  /// Offset of the CU vector in the constant pool.
  };
  SmallVector<SymTableEntry, 0> SymbolTable;

  /// Each value is CU index + attributes.
  SmallVector<std::pair<uint32_t, SmallVector<uint32_t, 0>>, 0>
      ConstantPoolVectors;

  StringRef ConstantPoolStrings;
  uint32_t StringPoolOffset;

  void dumpCUList(raw_ostream &OS) const;
  void dumpTUList(raw_ostream &OS) const;
  void dumpAddressArea(raw_ostream &OS) const;
  void dumpSymbolTable(raw_ostream &OS) const;
  void dumpConstantPool(raw_ostream &OS) const;

  bool parseImpl(DataExtractor Data);

public:
  void dump(raw_ostream &OS);
  void parse(DataExtractor Data);

  bool HasContent = false;
  bool HasError = false;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFGDBINDEX_H
