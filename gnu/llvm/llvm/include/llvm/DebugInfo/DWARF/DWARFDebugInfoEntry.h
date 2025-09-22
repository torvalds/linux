//===- DWARFDebugInfoEntry.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGINFOENTRY_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGINFOENTRY_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include <cstdint>

namespace llvm {

class DWARFUnit;
class DWARFDataExtractor;

/// DWARFDebugInfoEntry - A DIE with only the minimum required data.
class DWARFDebugInfoEntry {
  /// Offset within the .debug_info of the start of this entry.
  uint64_t Offset = 0;

  /// Index of the parent die. UINT32_MAX if there is no parent.
  uint32_t ParentIdx = UINT32_MAX;

  /// Index of the sibling die. Zero if there is no sibling.
  uint32_t SiblingIdx = 0;

  const DWARFAbbreviationDeclaration *AbbrevDecl = nullptr;

public:
  DWARFDebugInfoEntry() = default;

  /// Extracts a debug info entry, which is a child of a given unit,
  /// starting at a given offset. If DIE can't be extracted, returns false and
  /// doesn't change OffsetPtr.
  /// High performance extraction should use this call.
  bool extractFast(const DWARFUnit &U, uint64_t *OffsetPtr,
                   const DWARFDataExtractor &DebugInfoData, uint64_t UEndOffset,
                   uint32_t ParentIdx);

  uint64_t getOffset() const { return Offset; }

  /// Returns index of the parent die.
  std::optional<uint32_t> getParentIdx() const {
    if (ParentIdx == UINT32_MAX)
      return std::nullopt;

    return ParentIdx;
  }

  /// Returns index of the sibling die.
  std::optional<uint32_t> getSiblingIdx() const {
    if (SiblingIdx == 0)
      return std::nullopt;

    return SiblingIdx;
  }

  /// Set index of sibling.
  void setSiblingIdx(uint32_t Idx) { SiblingIdx = Idx; }

  dwarf::Tag getTag() const {
    return AbbrevDecl ? AbbrevDecl->getTag() : dwarf::DW_TAG_null;
  }

  bool hasChildren() const { return AbbrevDecl && AbbrevDecl->hasChildren(); }

  const DWARFAbbreviationDeclaration *getAbbreviationDeclarationPtr() const {
    return AbbrevDecl;
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGINFOENTRY_H
