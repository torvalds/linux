//===- DWARFDebugRangeList.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGRANGELIST_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGRANGELIST_H

#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include <cstdint>
#include <vector>

namespace llvm {

class raw_ostream;
class DWARFDataExtractor;
namespace object {
struct SectionedAddress;
}

class DWARFDebugRangeList {
public:
  struct RangeListEntry {
    /// A beginning address offset. This address offset has the size of an
    /// address and is relative to the applicable base address of the
    /// compilation unit referencing this range list. It marks the beginning
    /// of an address range.
    uint64_t StartAddress;
    /// An ending address offset. This address offset again has the size of
    /// an address and is relative to the applicable base address of the
    /// compilation unit referencing this range list. It marks the first
    /// address past the end of the address range. The ending address must
    /// be greater than or equal to the beginning address.
    uint64_t EndAddress;
    /// A section index this range belongs to.
    uint64_t SectionIndex;

    /// The end of any given range list is marked by an end of list entry,
    /// which consists of a 0 for the beginning address offset
    /// and a 0 for the ending address offset.
    bool isEndOfListEntry() const {
      return (StartAddress == 0) && (EndAddress == 0);
    }

    /// A base address selection entry consists of:
    /// 1. The value of the largest representable address offset
    /// (for example, 0xffffffff when the size of an address is 32 bits).
    /// 2. An address, which defines the appropriate base address for
    /// use in interpreting the beginning and ending address offsets of
    /// subsequent entries of the location list.
    bool isBaseAddressSelectionEntry(uint8_t AddressSize) const;
  };

private:
  /// Offset in .debug_ranges section.
  uint64_t Offset;
  uint8_t AddressSize;
  std::vector<RangeListEntry> Entries;

public:
  DWARFDebugRangeList() { clear(); }

  void clear();
  void dump(raw_ostream &OS) const;
  Error extract(const DWARFDataExtractor &data, uint64_t *offset_ptr);
  const std::vector<RangeListEntry> &getEntries() { return Entries; }

  /// getAbsoluteRanges - Returns absolute address ranges defined by this range
  /// list. Has to be passed base address of the compile unit referencing this
  /// range list.
  DWARFAddressRangesVector
  getAbsoluteRanges(std::optional<object::SectionedAddress> BaseAddr) const;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGRANGELIST_H
