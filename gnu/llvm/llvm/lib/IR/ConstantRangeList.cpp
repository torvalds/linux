//===- ConstantRangeList.cpp - ConstantRangeList implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ConstantRangeList.h"
#include <cstddef>

using namespace llvm;

bool ConstantRangeList::isOrderedRanges(ArrayRef<ConstantRange> RangesRef) {
  if (RangesRef.empty())
    return true;
  auto Range = RangesRef[0];
  if (Range.getLower().sge(Range.getUpper()))
    return false;
  for (unsigned i = 1; i < RangesRef.size(); i++) {
    auto CurRange = RangesRef[i];
    auto PreRange = RangesRef[i - 1];
    if (CurRange.getLower().sge(CurRange.getUpper()) ||
        CurRange.getLower().sle(PreRange.getUpper()))
      return false;
  }
  return true;
}

std::optional<ConstantRangeList>
ConstantRangeList::getConstantRangeList(ArrayRef<ConstantRange> RangesRef) {
  if (!isOrderedRanges(RangesRef))
    return std::nullopt;
  return ConstantRangeList(RangesRef);
}

void ConstantRangeList::insert(const ConstantRange &NewRange) {
  if (NewRange.isEmptySet())
    return;
  assert(!NewRange.isFullSet() && "Do not support full set");
  assert(NewRange.getLower().slt(NewRange.getUpper()));
  assert(getBitWidth() == NewRange.getBitWidth());
  // Handle common cases.
  if (empty() || Ranges.back().getUpper().slt(NewRange.getLower())) {
    Ranges.push_back(NewRange);
    return;
  }
  if (NewRange.getUpper().slt(Ranges.front().getLower())) {
    Ranges.insert(Ranges.begin(), NewRange);
    return;
  }

  auto LowerBound = lower_bound(
      Ranges, NewRange, [](const ConstantRange &a, const ConstantRange &b) {
        return a.getLower().slt(b.getLower());
      });
  if (LowerBound != Ranges.end() && LowerBound->contains(NewRange))
    return;

  // Slow insert.
  SmallVector<ConstantRange, 2> ExistingTail(LowerBound, Ranges.end());
  Ranges.erase(LowerBound, Ranges.end());
  // Merge consecutive ranges.
  if (!Ranges.empty() && NewRange.getLower().sle(Ranges.back().getUpper())) {
    APInt NewLower = Ranges.back().getLower();
    APInt NewUpper =
        APIntOps::smax(NewRange.getUpper(), Ranges.back().getUpper());
    Ranges.back() = ConstantRange(NewLower, NewUpper);
  } else {
    Ranges.push_back(NewRange);
  }
  for (auto Iter = ExistingTail.begin(); Iter != ExistingTail.end(); Iter++) {
    if (Ranges.back().getUpper().slt(Iter->getLower())) {
      Ranges.push_back(*Iter);
    } else {
      APInt NewLower = Ranges.back().getLower();
      APInt NewUpper =
          APIntOps::smax(Iter->getUpper(), Ranges.back().getUpper());
      Ranges.back() = ConstantRange(NewLower, NewUpper);
    }
  }
}

void ConstantRangeList::subtract(const ConstantRange &SubRange) {
  if (SubRange.isEmptySet() || empty())
    return;
  assert(!SubRange.isFullSet() && "Do not support full set");
  assert(SubRange.getLower().slt(SubRange.getUpper()));
  assert(getBitWidth() == SubRange.getBitWidth());
  // Handle common cases.
  if (Ranges.back().getUpper().sle(SubRange.getLower()))
    return;
  if (SubRange.getUpper().sle(Ranges.front().getLower()))
    return;

  SmallVector<ConstantRange, 2> Result;
  auto AppendRangeIfNonEmpty = [&Result](APInt Start, APInt End) {
    if (Start.slt(End))
      Result.push_back(ConstantRange(Start, End));
  };
  for (auto &Range : Ranges) {
    if (SubRange.getUpper().sle(Range.getLower()) ||
        Range.getUpper().sle(SubRange.getLower())) {
      // "Range" and "SubRange" do not overlap.
      //       L---U        : Range
      // L---U              : SubRange (Case1)
      //             L---U  : SubRange (Case2)
      Result.push_back(Range);
    } else if (Range.getLower().sle(SubRange.getLower()) &&
               SubRange.getUpper().sle(Range.getUpper())) {
      // "Range" contains "SubRange".
      //       L---U        : Range
      //        L-U         : SubRange
      // Note that ConstantRange::contains(ConstantRange) checks unsigned,
      // but we need signed checking here.
      AppendRangeIfNonEmpty(Range.getLower(), SubRange.getLower());
      AppendRangeIfNonEmpty(SubRange.getUpper(), Range.getUpper());
    } else if (SubRange.getLower().sle(Range.getLower()) &&
               Range.getUpper().sle(SubRange.getUpper())) {
      // "SubRange" contains "Range".
      //        L-U        : Range
      //       L---U       : SubRange
      continue;
    } else if (Range.getLower().sge(SubRange.getLower()) &&
               Range.getLower().sle(SubRange.getUpper())) {
      // "Range" and "SubRange" overlap at the left.
      //       L---U        : Range
      //     L---U          : SubRange
      AppendRangeIfNonEmpty(SubRange.getUpper(), Range.getUpper());
    } else {
      // "Range" and "SubRange" overlap at the right.
      //       L---U        : Range
      //         L---U      : SubRange
      assert(SubRange.getLower().sge(Range.getLower()) &&
             SubRange.getLower().sle(Range.getUpper()));
      AppendRangeIfNonEmpty(Range.getLower(), SubRange.getLower());
    }
  }

  Ranges = Result;
}

ConstantRangeList
ConstantRangeList::unionWith(const ConstantRangeList &CRL) const {
  assert(getBitWidth() == CRL.getBitWidth() &&
         "ConstantRangeList bitwidths don't agree!");
  // Handle common cases.
  if (empty())
    return CRL;
  if (CRL.empty())
    return *this;

  ConstantRangeList Result;
  size_t i = 0, j = 0;
  // "PreviousRange" tracks the lowest unioned range that is being processed.
  // Its lower is fixed and the upper may be updated over iterations.
  ConstantRange PreviousRange(getBitWidth(), false);
  if (Ranges[i].getLower().slt(CRL.Ranges[j].getLower())) {
    PreviousRange = Ranges[i++];
  } else {
    PreviousRange = CRL.Ranges[j++];
  }

  // Try to union "PreviousRange" and "CR". If they are disjoint, push
  // "PreviousRange" to the result and assign it to "CR", a new union range.
  // Otherwise, update the upper of "PreviousRange" to cover "CR". Note that,
  // the lower of "PreviousRange" is always less or equal the lower of "CR".
  auto UnionAndUpdateRange = [&PreviousRange,
                              &Result](const ConstantRange &CR) {
    if (PreviousRange.getUpper().slt(CR.getLower())) {
      Result.Ranges.push_back(PreviousRange);
      PreviousRange = CR;
    } else {
      PreviousRange = ConstantRange(
          PreviousRange.getLower(),
          APIntOps::smax(PreviousRange.getUpper(), CR.getUpper()));
    }
  };
  while (i < size() || j < CRL.size()) {
    if (j == CRL.size() ||
        (i < size() && Ranges[i].getLower().slt(CRL.Ranges[j].getLower()))) {
      // Merge PreviousRange with this.
      UnionAndUpdateRange(Ranges[i++]);
    } else {
      // Merge PreviousRange with CRL.
      UnionAndUpdateRange(CRL.Ranges[j++]);
    }
  }
  Result.Ranges.push_back(PreviousRange);
  return Result;
}

ConstantRangeList
ConstantRangeList::intersectWith(const ConstantRangeList &CRL) const {
  assert(getBitWidth() == CRL.getBitWidth() &&
         "ConstantRangeList bitwidths don't agree!");

  // Handle common cases.
  if (empty())
    return *this;
  if (CRL.empty())
    return CRL;

  ConstantRangeList Result;
  size_t i = 0, j = 0;
  while (i < size() && j < CRL.size()) {
    auto &Range = this->Ranges[i];
    auto &OtherRange = CRL.Ranges[j];

    // The intersection of two Ranges is (max(lowers), min(uppers)), and it's
    // possible that max(lowers) > min(uppers) if they don't have intersection.
    // Add the intersection to result only if it's non-empty.
    // To keep simple, we don't call ConstantRange::intersectWith() as it
    // considers the complex upper wrapped case and may result two ranges,
    // like (2, 8) && (6, 4) = {(2, 4), (6, 8)}.
    APInt Start = APIntOps::smax(Range.getLower(), OtherRange.getLower());
    APInt End = APIntOps::smin(Range.getUpper(), OtherRange.getUpper());
    if (Start.slt(End))
      Result.Ranges.push_back(ConstantRange(Start, End));

    // Move to the next Range in one list determined by the uppers.
    // For example: A = {(0, 2), (4, 8)}; B = {(-2, 5), (6, 10)}
    // We need to intersect three pairs: A0 && B0; A1 && B0; A1 && B1.
    if (Range.getUpper().slt(OtherRange.getUpper()))
      i++;
    else
      j++;
  }
  return Result;
}

void ConstantRangeList::print(raw_ostream &OS) const {
  interleaveComma(Ranges, OS, [&](ConstantRange CR) {
    OS << "(" << CR.getLower() << ", " << CR.getUpper() << ")";
  });
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ConstantRangeList::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif
