//===- llvm/CodeGen/DwarfStringPoolEntry.h - String pool entry --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DWARFSTRINGPOOLENTRY_H
#define LLVM_CODEGEN_DWARFSTRINGPOOLENTRY_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringMap.h"

namespace llvm {

class MCSymbol;

/// Data for a string pool entry.
struct DwarfStringPoolEntry {
  static constexpr unsigned NotIndexed = -1;

  MCSymbol *Symbol = nullptr;
  uint64_t Offset = 0;
  unsigned Index = 0;

  bool isIndexed() const { return Index != NotIndexed; }
};

/// DwarfStringPoolEntry with string keeping externally.
struct DwarfStringPoolEntryWithExtString : public DwarfStringPoolEntry {
  StringRef String;
};

/// DwarfStringPoolEntryRef: Dwarf string pool entry reference.
///
/// Dwarf string pool entry keeps string value and its data.
/// There are two variants how data are represented:
///
///   1. String data in pool  - StringMapEntry<DwarfStringPoolEntry>.
///   2. External string data - DwarfStringPoolEntryWithExtString.
///
/// The external data variant allows reducing memory usage for the case
/// when string pool entry does not have data: string entry does not
/// keep any data and so no need to waste space for the full
/// DwarfStringPoolEntry. It is recommended to use external variant if not all
/// entries of dwarf string pool have corresponding DwarfStringPoolEntry.

class DwarfStringPoolEntryRef {
  /// Pointer type for "By value" string entry.
  using ByValStringEntryPtr = const StringMapEntry<DwarfStringPoolEntry> *;

  /// Pointer type for external string entry.
  using ExtStringEntryPtr = const DwarfStringPoolEntryWithExtString *;

  /// Pointer to the dwarf string pool Entry.
  PointerUnion<ByValStringEntryPtr, ExtStringEntryPtr> MapEntry = nullptr;

public:
  DwarfStringPoolEntryRef() = default;

  /// ASSUMPTION: DwarfStringPoolEntryRef keeps pointer to \p Entry,
  /// thus specified entry mustn`t be reallocated.
  DwarfStringPoolEntryRef(const StringMapEntry<DwarfStringPoolEntry> &Entry)
      : MapEntry(&Entry) {}

  /// ASSUMPTION: DwarfStringPoolEntryRef keeps pointer to \p Entry,
  /// thus specified entry mustn`t be reallocated.
  DwarfStringPoolEntryRef(const DwarfStringPoolEntryWithExtString &Entry)
      : MapEntry(&Entry) {}

  explicit operator bool() const { return !MapEntry.isNull(); }

  /// \returns symbol for the dwarf string.
  MCSymbol *getSymbol() const {
    assert(getEntry().Symbol && "No symbol available!");
    return getEntry().Symbol;
  }

  /// \returns offset for the dwarf string.
  uint64_t getOffset() const { return getEntry().Offset; }

  /// \returns index for the dwarf string.
  unsigned getIndex() const {
    assert(getEntry().isIndexed() && "Index is not set!");
    return getEntry().Index;
  }

  /// \returns string.
  StringRef getString() const {
    if (isa<ByValStringEntryPtr>(MapEntry))
      return cast<ByValStringEntryPtr>(MapEntry)->first();

    return cast<ExtStringEntryPtr>(MapEntry)->String;
  }

  /// \returns the entire string pool entry for convenience.
  const DwarfStringPoolEntry &getEntry() const {
    if (isa<ByValStringEntryPtr>(MapEntry))
      return cast<ByValStringEntryPtr>(MapEntry)->second;

    return *cast<ExtStringEntryPtr>(MapEntry);
  }

  bool operator==(const DwarfStringPoolEntryRef &X) const {
    return MapEntry.getOpaqueValue() == X.MapEntry.getOpaqueValue();
  }

  bool operator!=(const DwarfStringPoolEntryRef &X) const {
    return MapEntry.getOpaqueValue() != X.MapEntry.getOpaqueValue();
  }
};

} // end namespace llvm

#endif
