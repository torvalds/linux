//===- ConstantRangeList.h - A list of constant ranges ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represent a list of signed ConstantRange and do NOT support wrap around the
// end of the numeric range. Ranges in the list are ordered and not overlapping.
// Ranges should have the same bitwidth. Each range's lower should be less than
// its upper.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTRANGELIST_H
#define LLVM_IR_CONSTANTRANGELIST_H

#include "llvm/ADT/APInt.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/Support/Debug.h"
#include <cstddef>
#include <cstdint>

namespace llvm {

class raw_ostream;

/// This class represents a list of constant ranges.
class [[nodiscard]] ConstantRangeList {
  SmallVector<ConstantRange, 2> Ranges;

public:
  ConstantRangeList() = default;
  ConstantRangeList(ArrayRef<ConstantRange> RangesRef) {
    assert(isOrderedRanges(RangesRef));
    for (const ConstantRange &R : RangesRef) {
      assert(R.getBitWidth() == getBitWidth());
      Ranges.push_back(R);
    }
  }

  // Return true if the ranges are non-overlapping and increasing.
  static bool isOrderedRanges(ArrayRef<ConstantRange> RangesRef);
  static std::optional<ConstantRangeList>
  getConstantRangeList(ArrayRef<ConstantRange> RangesRef);

  ArrayRef<ConstantRange> rangesRef() const { return Ranges; }
  SmallVectorImpl<ConstantRange>::iterator begin() { return Ranges.begin(); }
  SmallVectorImpl<ConstantRange>::iterator end() { return Ranges.end(); }
  SmallVectorImpl<ConstantRange>::const_iterator begin() const {
    return Ranges.begin();
  }
  SmallVectorImpl<ConstantRange>::const_iterator end() const {
    return Ranges.end();
  }
  ConstantRange getRange(unsigned i) const { return Ranges[i]; }

  /// Return true if this list contains no members.
  bool empty() const { return Ranges.empty(); }

  /// Get the bit width of this ConstantRangeList.
  uint32_t getBitWidth() const { return 64; }

  /// Return the number of ranges in this ConstantRangeList.
  size_t size() const { return Ranges.size(); }

  /// Insert a new range to Ranges and keep the list ordered.
  void insert(const ConstantRange &NewRange);
  void insert(int64_t Lower, int64_t Upper) {
    insert(ConstantRange(APInt(64, Lower, /*isSigned=*/true),
                         APInt(64, Upper, /*isSigned=*/true)));
  }

  void subtract(const ConstantRange &SubRange);

  /// Return the range list that results from the union of this
  /// ConstantRangeList with another ConstantRangeList, "CRL".
  ConstantRangeList unionWith(const ConstantRangeList &CRL) const;

  /// Return the range list that results from the intersection of this
  /// ConstantRangeList with another ConstantRangeList, "CRL".
  ConstantRangeList intersectWith(const ConstantRangeList &CRL) const;

  /// Return true if this range list is equal to another range list.
  bool operator==(const ConstantRangeList &CRL) const {
    return Ranges == CRL.Ranges;
  }
  bool operator!=(const ConstantRangeList &CRL) const {
    return !operator==(CRL);
  }

  /// Print out the ranges to a stream.
  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const;
#endif
};

} // end namespace llvm

#endif // LLVM_IR_CONSTANTRANGELIST_H
