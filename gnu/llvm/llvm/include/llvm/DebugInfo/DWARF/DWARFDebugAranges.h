//===- DWARFDebugAranges.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGES_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGES_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include <cstdint>
#include <vector>

namespace llvm {
class DWARFDataExtractor;
class Error;

class DWARFContext;

class DWARFDebugAranges {
public:
  void generate(DWARFContext *CTX);
  uint64_t findAddress(uint64_t Address) const;

private:
  void clear();
  void extract(DWARFDataExtractor DebugArangesData,
               function_ref<void(Error)> RecoverableErrorHandler,
               function_ref<void(Error)> WarningHandler);

  /// Call appendRange multiple times and then call construct.
  void appendRange(uint64_t CUOffset, uint64_t LowPC, uint64_t HighPC);
  void construct();

  struct Range {
    explicit Range(uint64_t LowPC, uint64_t HighPC, uint64_t CUOffset)
      : LowPC(LowPC), Length(HighPC - LowPC), CUOffset(CUOffset) {}

    void setHighPC(uint64_t HighPC) {
      if (HighPC == -1ULL || HighPC <= LowPC)
        Length = 0;
      else
        Length = HighPC - LowPC;
    }

    uint64_t HighPC() const {
      if (Length)
        return LowPC + Length;
      return -1ULL;
    }

    bool operator<(const Range &other) const {
      return LowPC < other.LowPC;
    }

    uint64_t LowPC; /// Start of address range.
    uint64_t Length; /// End of address range (not including this address).
    uint64_t CUOffset; /// Offset of the compile unit or die.
  };

  struct RangeEndpoint {
    uint64_t Address;
    uint64_t CUOffset;
    bool IsRangeStart;

    RangeEndpoint(uint64_t Address, uint64_t CUOffset, bool IsRangeStart)
        : Address(Address), CUOffset(CUOffset), IsRangeStart(IsRangeStart) {}

    bool operator<(const RangeEndpoint &Other) const {
      return Address < Other.Address;
    }
  };

  using RangeColl = std::vector<Range>;
  using RangeCollIterator = RangeColl::const_iterator;

  std::vector<RangeEndpoint> Endpoints;
  RangeColl Aranges;
  DenseSet<uint64_t> ParsedCUOffsets;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGES_H
