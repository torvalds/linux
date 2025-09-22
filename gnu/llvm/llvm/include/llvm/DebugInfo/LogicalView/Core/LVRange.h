//===-- LVRange.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVRange class, which is used to describe a debug
// information range.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVRANGE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVRANGE_H

#include "llvm/ADT/IntervalTree.h"
#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"

namespace llvm {
namespace logicalview {

using LVAddressRange = std::pair<LVAddress, LVAddress>;

class LVRangeEntry final {
  LVAddress Lower = 0;
  LVAddress Upper = 0;
  LVScope *Scope = nullptr;

public:
  using RangeType = LVAddress;

  LVRangeEntry() = delete;
  LVRangeEntry(LVAddress LowerAddress, LVAddress UpperAddress, LVScope *Scope)
      : Lower(LowerAddress), Upper(UpperAddress), Scope(Scope) {}

  RangeType lower() const { return Lower; }
  RangeType upper() const { return Upper; }
  LVAddressRange addressRange() const {
    return LVAddressRange(lower(), upper());
  }
  LVScope *scope() const { return Scope; }
};

// Class to represent a list of range addresses associated with a
// scope; the addresses are stored in ascending order and can overlap.
using LVRangeEntries = std::vector<LVRangeEntry>;

class LVRange final : public LVObject {
  /// Map of where a user value is live, and its location.
  using LVRangesTree = IntervalTree<LVAddress, LVScope *>;
  using LVAllocator = LVRangesTree::Allocator;

  LVAllocator Allocator;
  LVRangesTree RangesTree;
  LVRangeEntries RangeEntries;
  LVAddress Lower = MaxAddress;
  LVAddress Upper = 0;

public:
  LVRange() : LVObject(), RangesTree(Allocator) {}
  LVRange(const LVRange &) = delete;
  LVRange &operator=(const LVRange &) = delete;
  ~LVRange() = default;

  void addEntry(LVScope *Scope, LVAddress LowerAddress, LVAddress UpperAddress);
  void addEntry(LVScope *Scope);
  LVScope *getEntry(LVAddress Address) const;
  LVScope *getEntry(LVAddress LowerAddress, LVAddress UpperAddress) const;
  bool hasEntry(LVAddress Low, LVAddress High) const;
  LVAddress getLower() const { return Lower; }
  LVAddress getUpper() const { return Upper; }

  const LVRangeEntries &getEntries() const { return RangeEntries; }

  void clear() {
    RangeEntries.clear();
    Lower = MaxAddress;
    Upper = 0;
  }
  bool empty() const { return RangeEntries.empty(); }
  void sort();

  void startSearch();
  void endSearch() {}

  void print(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const override { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVRANGE_H
