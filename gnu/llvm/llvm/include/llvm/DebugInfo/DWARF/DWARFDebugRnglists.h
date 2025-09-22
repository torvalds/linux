//===- DWARFDebugRnglists.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGRNGLISTS_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGRNGLISTS_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFListTable.h"
#include <cstdint>

namespace llvm {

class Error;
class raw_ostream;
class DWARFUnit;
class DWARFDataExtractor;
struct DIDumpOptions;
namespace object {
struct SectionedAddress;
}

/// A class representing a single range list entry.
struct RangeListEntry : public DWARFListEntryBase {
  /// The values making up the range list entry. Most represent a range with
  /// a start and end address or a start address and a length. Others are
  /// single value base addresses or end-of-list with no values. The unneeded
  /// values are semantically undefined, but initialized to 0.
  uint64_t Value0;
  uint64_t Value1;

  Error extract(DWARFDataExtractor Data, uint64_t *OffsetPtr);
  void
  dump(raw_ostream &OS, uint8_t AddrSize, uint8_t MaxEncodingStringLength,
       uint64_t &CurrentBase, DIDumpOptions DumpOpts,
       llvm::function_ref<std::optional<object::SectionedAddress>(uint32_t)>
           LookupPooledAddress) const;
  bool isSentinel() const { return EntryKind == dwarf::DW_RLE_end_of_list; }
};

/// A class representing a single rangelist.
class DWARFDebugRnglist : public DWARFListType<RangeListEntry> {
public:
  /// Build a DWARFAddressRangesVector from a rangelist.
  DWARFAddressRangesVector getAbsoluteRanges(
      std::optional<object::SectionedAddress> BaseAddr, uint8_t AddressByteSize,
      function_ref<std::optional<object::SectionedAddress>(uint32_t)>
          LookupPooledAddress) const;

  /// Build a DWARFAddressRangesVector from a rangelist.
  DWARFAddressRangesVector
  getAbsoluteRanges(std::optional<object::SectionedAddress> BaseAddr,
                    DWARFUnit &U) const;
};

class DWARFDebugRnglistTable : public DWARFListTableBase<DWARFDebugRnglist> {
public:
  DWARFDebugRnglistTable()
      : DWARFListTableBase(/* SectionName    = */ ".debug_rnglists",
                           /* HeaderString   = */ "ranges:",
                           /* ListTypeString = */ "range") {}
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGRNGLISTS_H
