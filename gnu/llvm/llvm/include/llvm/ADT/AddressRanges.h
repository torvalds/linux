//===- AddressRanges.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ADDRESSRANGES_H
#define LLVM_ADT_ADDRESSRANGES_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <optional>
#include <stdint.h>

namespace llvm {

/// A class that represents an address range. The range is specified using
/// a start and an end address: [Start, End).
class AddressRange {
public:
  AddressRange() {}
  AddressRange(uint64_t S, uint64_t E) : Start(S), End(E) {
    assert(Start <= End);
  }
  uint64_t start() const { return Start; }
  uint64_t end() const { return End; }
  uint64_t size() const { return End - Start; }
  uint64_t empty() const { return size() == 0; }
  bool contains(uint64_t Addr) const { return Start <= Addr && Addr < End; }
  bool contains(const AddressRange &R) const {
    return Start <= R.Start && R.End <= End;
  }
  bool intersects(const AddressRange &R) const {
    return Start < R.End && R.Start < End;
  }
  bool operator==(const AddressRange &R) const {
    return Start == R.Start && End == R.End;
  }
  bool operator!=(const AddressRange &R) const { return !(*this == R); }
  bool operator<(const AddressRange &R) const {
    return std::make_pair(Start, End) < std::make_pair(R.Start, R.End);
  }

private:
  uint64_t Start = 0;
  uint64_t End = 0;
};

/// The AddressRangesBase class presents the base functionality for the
/// normalized address ranges collection. This class keeps a sorted vector
/// of AddressRange-like objects and can perform searches efficiently.
/// The address ranges are always sorted and never contain any invalid,
/// empty or intersected address ranges.

template <typename T> class AddressRangesBase {
protected:
  using Collection = SmallVector<T>;
  Collection Ranges;

public:
  void clear() { Ranges.clear(); }
  bool empty() const { return Ranges.empty(); }
  bool contains(uint64_t Addr) const {
    return find(Addr, Addr + 1) != Ranges.end();
  }
  bool contains(AddressRange Range) const {
    return find(Range.start(), Range.end()) != Ranges.end();
  }
  void reserve(size_t Capacity) { Ranges.reserve(Capacity); }
  size_t size() const { return Ranges.size(); }

  std::optional<T> getRangeThatContains(uint64_t Addr) const {
    typename Collection::const_iterator It = find(Addr, Addr + 1);
    if (It == Ranges.end())
      return std::nullopt;

    return *It;
  }

  typename Collection::const_iterator begin() const { return Ranges.begin(); }
  typename Collection::const_iterator end() const { return Ranges.end(); }

  const T &operator[](size_t i) const {
    assert(i < Ranges.size());
    return Ranges[i];
  }

  bool operator==(const AddressRangesBase<T> &RHS) const {
    return Ranges == RHS.Ranges;
  }

protected:
  typename Collection::const_iterator find(uint64_t Start, uint64_t End) const {
    if (Start >= End)
      return Ranges.end();

    auto It =
        std::partition_point(Ranges.begin(), Ranges.end(), [=](const T &R) {
          return AddressRange(R).start() <= Start;
        });

    if (It == Ranges.begin())
      return Ranges.end();

    --It;
    if (End > AddressRange(*It).end())
      return Ranges.end();

    return It;
  }
};

/// The AddressRanges class helps normalize address range collections.
/// This class keeps a sorted vector of AddressRange objects and can perform
/// insertions and searches efficiently. Intersecting([100,200), [150,300))
/// and adjacent([100,200), [200,300)) address ranges are combined during
/// insertion.
class AddressRanges : public AddressRangesBase<AddressRange> {
public:
  Collection::const_iterator insert(AddressRange Range) {
    if (Range.empty())
      return Ranges.end();

    auto It = llvm::upper_bound(Ranges, Range);
    auto It2 = It;
    while (It2 != Ranges.end() && It2->start() <= Range.end())
      ++It2;
    if (It != It2) {
      Range = {Range.start(), std::max(Range.end(), std::prev(It2)->end())};
      It = Ranges.erase(It, It2);
    }
    if (It != Ranges.begin() && Range.start() <= std::prev(It)->end()) {
      --It;
      *It = {It->start(), std::max(It->end(), Range.end())};
      return It;
    }

    return Ranges.insert(It, Range);
  }
};

class AddressRangeValuePair {
public:
  operator AddressRange() const { return Range; }

  AddressRange Range;
  int64_t Value = 0;
};

inline bool operator==(const AddressRangeValuePair &LHS,
                       const AddressRangeValuePair &RHS) {
  return LHS.Range == RHS.Range && LHS.Value == RHS.Value;
}

/// AddressRangesMap class maps values to the address ranges.
/// It keeps normalized address ranges and corresponding values.
/// This class keeps a sorted vector of AddressRangeValuePair objects
/// and can perform insertions and searches efficiently.
/// Intersecting([100,200), [150,300)) ranges splitted into non-conflicting
/// parts([100,200), [200,300)). Adjacent([100,200), [200,300)) address
/// ranges are not combined during insertion.
class AddressRangesMap : public AddressRangesBase<AddressRangeValuePair> {
public:
  void insert(AddressRange Range, int64_t Value) {
    if (Range.empty())
      return;

    // Search for range which is less than or equal incoming Range.
    auto It = std::partition_point(Ranges.begin(), Ranges.end(),
                                   [=](const AddressRangeValuePair &R) {
                                     return R.Range.start() <= Range.start();
                                   });

    if (It != Ranges.begin())
      It--;

    while (!Range.empty()) {
      // Inserted range does not overlap with any range.
      // Store it into the Ranges collection.
      if (It == Ranges.end() || Range.end() <= It->Range.start()) {
        Ranges.insert(It, {Range, Value});
        return;
      }

      // Inserted range partially overlaps with current range.
      // Store not overlapped part of inserted range.
      if (Range.start() < It->Range.start()) {
        It = Ranges.insert(It, {{Range.start(), It->Range.start()}, Value});
        It++;
        Range = {It->Range.start(), Range.end()};
        continue;
      }

      // Inserted range fully overlaps with current range.
      if (Range.end() <= It->Range.end())
        return;

      // Inserted range partially overlaps with current range.
      // Remove overlapped part from the inserted range.
      if (Range.start() < It->Range.end())
        Range = {It->Range.end(), Range.end()};

      It++;
    }
  }
};

} // namespace llvm

#endif // LLVM_ADT_ADDRESSRANGES_H
