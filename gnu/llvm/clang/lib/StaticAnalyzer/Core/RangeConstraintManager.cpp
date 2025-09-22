//== RangeConstraintManager.cpp - Manage range constraints.------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines RangeConstraintManager, a class that tracks simple
//  equality and inequality constraints on symbolic values of ProgramState.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/JsonSupport.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/RangedConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValVisitor.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <iterator>
#include <optional>

using namespace clang;
using namespace ento;

// This class can be extended with other tables which will help to reason
// about ranges more precisely.
class OperatorRelationsTable {
  static_assert(BO_LT < BO_GT && BO_GT < BO_LE && BO_LE < BO_GE &&
                    BO_GE < BO_EQ && BO_EQ < BO_NE,
                "This class relies on operators order. Rework it otherwise.");

public:
  enum TriStateKind {
    False = 0,
    True,
    Unknown,
  };

private:
  // CmpOpTable holds states which represent the corresponding range for
  // branching an exploded graph. We can reason about the branch if there is
  // a previously known fact of the existence of a comparison expression with
  // operands used in the current expression.
  // E.g. assuming (x < y) is true that means (x != y) is surely true.
  // if (x previous_operation y)  // <    | !=      | >
  //   if (x operation y)         // !=   | >       | <
  //     tristate                 // True | Unknown | False
  //
  // CmpOpTable represents next:
  // __|< |> |<=|>=|==|!=|UnknownX2|
  // < |1 |0 |* |0 |0 |* |1        |
  // > |0 |1 |0 |* |0 |* |1        |
  // <=|1 |0 |1 |* |1 |* |0        |
  // >=|0 |1 |* |1 |1 |* |0        |
  // ==|0 |0 |* |* |1 |0 |1        |
  // !=|1 |1 |* |* |0 |1 |0        |
  //
  // Columns stands for a previous operator.
  // Rows stands for a current operator.
  // Each row has exactly two `Unknown` cases.
  // UnknownX2 means that both `Unknown` previous operators are met in code,
  // and there is a special column for that, for example:
  // if (x >= y)
  //   if (x != y)
  //     if (x <= y)
  //       False only
  static constexpr size_t CmpOpCount = BO_NE - BO_LT + 1;
  const TriStateKind CmpOpTable[CmpOpCount][CmpOpCount + 1] = {
      // <      >      <=     >=     ==     !=    UnknownX2
      {True, False, Unknown, False, False, Unknown, True}, // <
      {False, True, False, Unknown, False, Unknown, True}, // >
      {True, False, True, Unknown, True, Unknown, False},  // <=
      {False, True, Unknown, True, True, Unknown, False},  // >=
      {False, False, Unknown, Unknown, True, False, True}, // ==
      {True, True, Unknown, Unknown, False, True, False},  // !=
  };

  static size_t getIndexFromOp(BinaryOperatorKind OP) {
    return static_cast<size_t>(OP - BO_LT);
  }

public:
  constexpr size_t getCmpOpCount() const { return CmpOpCount; }

  static BinaryOperatorKind getOpFromIndex(size_t Index) {
    return static_cast<BinaryOperatorKind>(Index + BO_LT);
  }

  TriStateKind getCmpOpState(BinaryOperatorKind CurrentOP,
                             BinaryOperatorKind QueriedOP) const {
    return CmpOpTable[getIndexFromOp(CurrentOP)][getIndexFromOp(QueriedOP)];
  }

  TriStateKind getCmpOpStateForUnknownX2(BinaryOperatorKind CurrentOP) const {
    return CmpOpTable[getIndexFromOp(CurrentOP)][CmpOpCount];
  }
};

//===----------------------------------------------------------------------===//
//                           RangeSet implementation
//===----------------------------------------------------------------------===//

RangeSet::ContainerType RangeSet::Factory::EmptySet{};

RangeSet RangeSet::Factory::add(RangeSet LHS, RangeSet RHS) {
  ContainerType Result;
  Result.reserve(LHS.size() + RHS.size());
  std::merge(LHS.begin(), LHS.end(), RHS.begin(), RHS.end(),
             std::back_inserter(Result));
  return makePersistent(std::move(Result));
}

RangeSet RangeSet::Factory::add(RangeSet Original, Range Element) {
  ContainerType Result;
  Result.reserve(Original.size() + 1);

  const_iterator Lower = llvm::lower_bound(Original, Element);
  Result.insert(Result.end(), Original.begin(), Lower);
  Result.push_back(Element);
  Result.insert(Result.end(), Lower, Original.end());

  return makePersistent(std::move(Result));
}

RangeSet RangeSet::Factory::add(RangeSet Original, const llvm::APSInt &Point) {
  return add(Original, Range(Point));
}

RangeSet RangeSet::Factory::unite(RangeSet LHS, RangeSet RHS) {
  ContainerType Result = unite(*LHS.Impl, *RHS.Impl);
  return makePersistent(std::move(Result));
}

RangeSet RangeSet::Factory::unite(RangeSet Original, Range R) {
  ContainerType Result;
  Result.push_back(R);
  Result = unite(*Original.Impl, Result);
  return makePersistent(std::move(Result));
}

RangeSet RangeSet::Factory::unite(RangeSet Original, llvm::APSInt Point) {
  return unite(Original, Range(ValueFactory.getValue(Point)));
}

RangeSet RangeSet::Factory::unite(RangeSet Original, llvm::APSInt From,
                                  llvm::APSInt To) {
  return unite(Original,
               Range(ValueFactory.getValue(From), ValueFactory.getValue(To)));
}

template <typename T>
void swapIterators(T &First, T &FirstEnd, T &Second, T &SecondEnd) {
  std::swap(First, Second);
  std::swap(FirstEnd, SecondEnd);
}

RangeSet::ContainerType RangeSet::Factory::unite(const ContainerType &LHS,
                                                 const ContainerType &RHS) {
  if (LHS.empty())
    return RHS;
  if (RHS.empty())
    return LHS;

  using llvm::APSInt;
  using iterator = ContainerType::const_iterator;

  iterator First = LHS.begin();
  iterator FirstEnd = LHS.end();
  iterator Second = RHS.begin();
  iterator SecondEnd = RHS.end();
  APSIntType Ty = APSIntType(First->From());
  const APSInt Min = Ty.getMinValue();

  // Handle a corner case first when both range sets start from MIN.
  // This helps to avoid complicated conditions below. Specifically, this
  // particular check for `MIN` is not needed in the loop below every time
  // when we do `Second->From() - One` operation.
  if (Min == First->From() && Min == Second->From()) {
    if (First->To() > Second->To()) {
      //    [ First    ]--->
      //    [ Second ]----->
      // MIN^
      // The Second range is entirely inside the First one.

      // Check if Second is the last in its RangeSet.
      if (++Second == SecondEnd)
        //    [ First     ]--[ First + 1 ]--->
        //    [ Second ]--------------------->
        // MIN^
        // The Union is equal to First's RangeSet.
        return LHS;
    } else {
      // case 1: [ First ]----->
      // case 2: [ First   ]--->
      //         [ Second  ]--->
      //      MIN^
      // The First range is entirely inside or equal to the Second one.

      // Check if First is the last in its RangeSet.
      if (++First == FirstEnd)
        //    [ First ]----------------------->
        //    [ Second  ]--[ Second + 1 ]---->
        // MIN^
        // The Union is equal to Second's RangeSet.
        return RHS;
    }
  }

  const APSInt One = Ty.getValue(1);
  ContainerType Result;

  // This is called when there are no ranges left in one of the ranges.
  // Append the rest of the ranges from another range set to the Result
  // and return with that.
  const auto AppendTheRest = [&Result](iterator I, iterator E) {
    Result.append(I, E);
    return Result;
  };

  while (true) {
    // We want to keep the following invariant at all times:
    // ---[ First ------>
    // -----[ Second --->
    if (First->From() > Second->From())
      swapIterators(First, FirstEnd, Second, SecondEnd);

    // The Union definitely starts with First->From().
    // ----------[ First ------>
    // ------------[ Second --->
    // ----------[ Union ------>
    // UnionStart^
    const llvm::APSInt &UnionStart = First->From();

    // Loop where the invariant holds.
    while (true) {
      // Skip all enclosed ranges.
      // ---[                  First                     ]--->
      // -----[ Second ]--[ Second + 1 ]--[ Second + N ]----->
      while (First->To() >= Second->To()) {
        // Check if Second is the last in its RangeSet.
        if (++Second == SecondEnd) {
          // Append the Union.
          // ---[ Union      ]--->
          // -----[ Second ]----->
          // --------[ First ]--->
          //         UnionEnd^
          Result.emplace_back(UnionStart, First->To());
          // ---[ Union ]----------------->
          // --------------[ First + 1]--->
          // Append all remaining ranges from the First's RangeSet.
          return AppendTheRest(++First, FirstEnd);
        }
      }

      // Check if First and Second are disjoint. It means that we find
      // the end of the Union. Exit the loop and append the Union.
      // ---[ First ]=------------->
      // ------------=[ Second ]--->
      // ----MinusOne^
      if (First->To() < Second->From() - One)
        break;

      // First is entirely inside the Union. Go next.
      // ---[ Union ----------->
      // ---- [ First ]-------->
      // -------[ Second ]----->
      // Check if First is the last in its RangeSet.
      if (++First == FirstEnd) {
        // Append the Union.
        // ---[ Union       ]--->
        // -----[ First ]------->
        // --------[ Second ]--->
        //          UnionEnd^
        Result.emplace_back(UnionStart, Second->To());
        // ---[ Union ]------------------>
        // --------------[ Second + 1]--->
        // Append all remaining ranges from the Second's RangeSet.
        return AppendTheRest(++Second, SecondEnd);
      }

      // We know that we are at one of the two cases:
      // case 1: --[ First ]--------->
      // case 2: ----[ First ]------->
      // --------[ Second ]---------->
      // In both cases First starts after Second->From().
      // Make sure that the loop invariant holds.
      swapIterators(First, FirstEnd, Second, SecondEnd);
    }

    // Here First and Second are disjoint.
    // Append the Union.
    // ---[ Union    ]--------------->
    // -----------------[ Second ]--->
    // ------[ First ]--------------->
    //       UnionEnd^
    Result.emplace_back(UnionStart, First->To());

    // Check if First is the last in its RangeSet.
    if (++First == FirstEnd)
      // ---[ Union ]--------------->
      // --------------[ Second ]--->
      // Append all remaining ranges from the Second's RangeSet.
      return AppendTheRest(Second, SecondEnd);
  }

  llvm_unreachable("Normally, we should not reach here");
}

RangeSet RangeSet::Factory::getRangeSet(Range From) {
  ContainerType Result;
  Result.push_back(From);
  return makePersistent(std::move(Result));
}

RangeSet RangeSet::Factory::makePersistent(ContainerType &&From) {
  llvm::FoldingSetNodeID ID;
  void *InsertPos;

  From.Profile(ID);
  ContainerType *Result = Cache.FindNodeOrInsertPos(ID, InsertPos);

  if (!Result) {
    // It is cheaper to fully construct the resulting range on stack
    // and move it to the freshly allocated buffer if we don't have
    // a set like this already.
    Result = construct(std::move(From));
    Cache.InsertNode(Result, InsertPos);
  }

  return Result;
}

RangeSet::ContainerType *RangeSet::Factory::construct(ContainerType &&From) {
  void *Buffer = Arena.Allocate();
  return new (Buffer) ContainerType(std::move(From));
}

const llvm::APSInt &RangeSet::getMinValue() const {
  assert(!isEmpty());
  return begin()->From();
}

const llvm::APSInt &RangeSet::getMaxValue() const {
  assert(!isEmpty());
  return std::prev(end())->To();
}

bool clang::ento::RangeSet::isUnsigned() const {
  assert(!isEmpty());
  return begin()->From().isUnsigned();
}

uint32_t clang::ento::RangeSet::getBitWidth() const {
  assert(!isEmpty());
  return begin()->From().getBitWidth();
}

APSIntType clang::ento::RangeSet::getAPSIntType() const {
  assert(!isEmpty());
  return APSIntType(begin()->From());
}

bool RangeSet::containsImpl(llvm::APSInt &Point) const {
  if (isEmpty() || !pin(Point))
    return false;

  Range Dummy(Point);
  const_iterator It = llvm::upper_bound(*this, Dummy);
  if (It == begin())
    return false;

  return std::prev(It)->Includes(Point);
}

bool RangeSet::pin(llvm::APSInt &Point) const {
  APSIntType Type(getMinValue());
  if (Type.testInRange(Point, true) != APSIntType::RTR_Within)
    return false;

  Type.apply(Point);
  return true;
}

bool RangeSet::pin(llvm::APSInt &Lower, llvm::APSInt &Upper) const {
  // This function has nine cases, the cartesian product of range-testing
  // both the upper and lower bounds against the symbol's type.
  // Each case requires a different pinning operation.
  // The function returns false if the described range is entirely outside
  // the range of values for the associated symbol.
  APSIntType Type(getMinValue());
  APSIntType::RangeTestResultKind LowerTest = Type.testInRange(Lower, true);
  APSIntType::RangeTestResultKind UpperTest = Type.testInRange(Upper, true);

  switch (LowerTest) {
  case APSIntType::RTR_Below:
    switch (UpperTest) {
    case APSIntType::RTR_Below:
      // The entire range is outside the symbol's set of possible values.
      // If this is a conventionally-ordered range, the state is infeasible.
      if (Lower <= Upper)
        return false;

      // However, if the range wraps around, it spans all possible values.
      Lower = Type.getMinValue();
      Upper = Type.getMaxValue();
      break;
    case APSIntType::RTR_Within:
      // The range starts below what's possible but ends within it. Pin.
      Lower = Type.getMinValue();
      Type.apply(Upper);
      break;
    case APSIntType::RTR_Above:
      // The range spans all possible values for the symbol. Pin.
      Lower = Type.getMinValue();
      Upper = Type.getMaxValue();
      break;
    }
    break;
  case APSIntType::RTR_Within:
    switch (UpperTest) {
    case APSIntType::RTR_Below:
      // The range wraps around, but all lower values are not possible.
      Type.apply(Lower);
      Upper = Type.getMaxValue();
      break;
    case APSIntType::RTR_Within:
      // The range may or may not wrap around, but both limits are valid.
      Type.apply(Lower);
      Type.apply(Upper);
      break;
    case APSIntType::RTR_Above:
      // The range starts within what's possible but ends above it. Pin.
      Type.apply(Lower);
      Upper = Type.getMaxValue();
      break;
    }
    break;
  case APSIntType::RTR_Above:
    switch (UpperTest) {
    case APSIntType::RTR_Below:
      // The range wraps but is outside the symbol's set of possible values.
      return false;
    case APSIntType::RTR_Within:
      // The range starts above what's possible but ends within it (wrap).
      Lower = Type.getMinValue();
      Type.apply(Upper);
      break;
    case APSIntType::RTR_Above:
      // The entire range is outside the symbol's set of possible values.
      // If this is a conventionally-ordered range, the state is infeasible.
      if (Lower <= Upper)
        return false;

      // However, if the range wraps around, it spans all possible values.
      Lower = Type.getMinValue();
      Upper = Type.getMaxValue();
      break;
    }
    break;
  }

  return true;
}

RangeSet RangeSet::Factory::intersect(RangeSet What, llvm::APSInt Lower,
                                      llvm::APSInt Upper) {
  if (What.isEmpty() || !What.pin(Lower, Upper))
    return getEmptySet();

  ContainerType DummyContainer;

  if (Lower <= Upper) {
    // [Lower, Upper] is a regular range.
    //
    // Shortcut: check that there is even a possibility of the intersection
    //           by checking the two following situations:
    //
    //               <---[  What  ]---[------]------>
    //                              Lower  Upper
    //                            -or-
    //               <----[------]----[  What  ]---->
    //                  Lower  Upper
    if (What.getMaxValue() < Lower || Upper < What.getMinValue())
      return getEmptySet();

    DummyContainer.push_back(
        Range(ValueFactory.getValue(Lower), ValueFactory.getValue(Upper)));
  } else {
    // [Lower, Upper] is an inverted range, i.e. [MIN, Upper] U [Lower, MAX]
    //
    // Shortcut: check that there is even a possibility of the intersection
    //           by checking the following situation:
    //
    //               <------]---[  What  ]---[------>
    //                    Upper             Lower
    if (What.getMaxValue() < Lower && Upper < What.getMinValue())
      return getEmptySet();

    DummyContainer.push_back(
        Range(ValueFactory.getMinValue(Upper), ValueFactory.getValue(Upper)));
    DummyContainer.push_back(
        Range(ValueFactory.getValue(Lower), ValueFactory.getMaxValue(Lower)));
  }

  return intersect(*What.Impl, DummyContainer);
}

RangeSet RangeSet::Factory::intersect(const RangeSet::ContainerType &LHS,
                                      const RangeSet::ContainerType &RHS) {
  ContainerType Result;
  Result.reserve(std::max(LHS.size(), RHS.size()));

  const_iterator First = LHS.begin(), Second = RHS.begin(),
                 FirstEnd = LHS.end(), SecondEnd = RHS.end();

  // If we ran out of ranges in one set, but not in the other,
  // it means that those elements are definitely not in the
  // intersection.
  while (First != FirstEnd && Second != SecondEnd) {
    // We want to keep the following invariant at all times:
    //
    //    ----[ First ---------------------->
    //    --------[ Second ----------------->
    if (Second->From() < First->From())
      swapIterators(First, FirstEnd, Second, SecondEnd);

    // Loop where the invariant holds:
    do {
      // Check for the following situation:
      //
      //    ----[ First ]--------------------->
      //    ---------------[ Second ]--------->
      //
      // which means that...
      if (Second->From() > First->To()) {
        // ...First is not in the intersection.
        //
        // We should move on to the next range after First and break out of the
        // loop because the invariant might not be true.
        ++First;
        break;
      }

      // We have a guaranteed intersection at this point!
      // And this is the current situation:
      //
      //    ----[   First   ]----------------->
      //    -------[ Second ------------------>
      //
      // Additionally, it definitely starts with Second->From().
      const llvm::APSInt &IntersectionStart = Second->From();

      // It is important to know which of the two ranges' ends
      // is greater.  That "longer" range might have some other
      // intersections, while the "shorter" range might not.
      if (Second->To() > First->To()) {
        // Here we make a decision to keep First as the "longer"
        // range.
        swapIterators(First, FirstEnd, Second, SecondEnd);
      }

      // At this point, we have the following situation:
      //
      //    ---- First      ]-------------------->
      //    ---- Second ]--[  Second+1 ---------->
      //
      // We don't know the relationship between First->From and
      // Second->From and we don't know whether Second+1 intersects
      // with First.
      //
      // However, we know that [IntersectionStart, Second->To] is
      // a part of the intersection...
      Result.push_back(Range(IntersectionStart, Second->To()));
      ++Second;
      // ...and that the invariant will hold for a valid Second+1
      // because First->From <= Second->To < (Second+1)->From.
    } while (Second != SecondEnd);
  }

  if (Result.empty())
    return getEmptySet();

  return makePersistent(std::move(Result));
}

RangeSet RangeSet::Factory::intersect(RangeSet LHS, RangeSet RHS) {
  // Shortcut: let's see if the intersection is even possible.
  if (LHS.isEmpty() || RHS.isEmpty() || LHS.getMaxValue() < RHS.getMinValue() ||
      RHS.getMaxValue() < LHS.getMinValue())
    return getEmptySet();

  return intersect(*LHS.Impl, *RHS.Impl);
}

RangeSet RangeSet::Factory::intersect(RangeSet LHS, llvm::APSInt Point) {
  if (LHS.containsImpl(Point))
    return getRangeSet(ValueFactory.getValue(Point));

  return getEmptySet();
}

RangeSet RangeSet::Factory::negate(RangeSet What) {
  if (What.isEmpty())
    return getEmptySet();

  const llvm::APSInt SampleValue = What.getMinValue();
  const llvm::APSInt &MIN = ValueFactory.getMinValue(SampleValue);
  const llvm::APSInt &MAX = ValueFactory.getMaxValue(SampleValue);

  ContainerType Result;
  Result.reserve(What.size() + (SampleValue == MIN));

  // Handle a special case for MIN value.
  const_iterator It = What.begin();
  const_iterator End = What.end();

  const llvm::APSInt &From = It->From();
  const llvm::APSInt &To = It->To();

  if (From == MIN) {
    // If the range [From, To] is [MIN, MAX], then result is also [MIN, MAX].
    if (To == MAX) {
      return What;
    }

    const_iterator Last = std::prev(End);

    // Try to find and unite the following ranges:
    // [MIN, MIN] & [MIN + 1, N] => [MIN, N].
    if (Last->To() == MAX) {
      // It means that in the original range we have ranges
      //   [MIN, A], ... , [B, MAX]
      // And the result should be [MIN, -B], ..., [-A, MAX]
      Result.emplace_back(MIN, ValueFactory.getValue(-Last->From()));
      // We already negated Last, so we can skip it.
      End = Last;
    } else {
      // Add a separate range for the lowest value.
      Result.emplace_back(MIN, MIN);
    }

    // Skip adding the second range in case when [From, To] are [MIN, MIN].
    if (To != MIN) {
      Result.emplace_back(ValueFactory.getValue(-To), MAX);
    }

    // Skip the first range in the loop.
    ++It;
  }

  // Negate all other ranges.
  for (; It != End; ++It) {
    // Negate int values.
    const llvm::APSInt &NewFrom = ValueFactory.getValue(-It->To());
    const llvm::APSInt &NewTo = ValueFactory.getValue(-It->From());

    // Add a negated range.
    Result.emplace_back(NewFrom, NewTo);
  }

  llvm::sort(Result);
  return makePersistent(std::move(Result));
}

// Convert range set to the given integral type using truncation and promotion.
// This works similar to APSIntType::apply function but for the range set.
RangeSet RangeSet::Factory::castTo(RangeSet What, APSIntType Ty) {
  // Set is empty or NOOP (aka cast to the same type).
  if (What.isEmpty() || What.getAPSIntType() == Ty)
    return What;

  const bool IsConversion = What.isUnsigned() != Ty.isUnsigned();
  const bool IsTruncation = What.getBitWidth() > Ty.getBitWidth();
  const bool IsPromotion = What.getBitWidth() < Ty.getBitWidth();

  if (IsTruncation)
    return makePersistent(truncateTo(What, Ty));

  // Here we handle 2 cases:
  // - IsConversion && !IsPromotion.
  //   In this case we handle changing a sign with same bitwidth: char -> uchar,
  //   uint -> int. Here we convert negatives to positives and positives which
  //   is out of range to negatives. We use convertTo function for that.
  // - IsConversion && IsPromotion && !What.isUnsigned().
  //   In this case we handle changing a sign from signeds to unsigneds with
  //   higher bitwidth: char -> uint, int-> uint64. The point is that we also
  //   need convert negatives to positives and use convertTo function as well.
  //   For example, we don't need such a convertion when converting unsigned to
  //   signed with higher bitwidth, because all the values of unsigned is valid
  //   for the such signed.
  if (IsConversion && (!IsPromotion || !What.isUnsigned()))
    return makePersistent(convertTo(What, Ty));

  assert(IsPromotion && "Only promotion operation from unsigneds left.");
  return makePersistent(promoteTo(What, Ty));
}

RangeSet RangeSet::Factory::castTo(RangeSet What, QualType T) {
  assert(T->isIntegralOrEnumerationType() && "T shall be an integral type.");
  return castTo(What, ValueFactory.getAPSIntType(T));
}

RangeSet::ContainerType RangeSet::Factory::truncateTo(RangeSet What,
                                                      APSIntType Ty) {
  using llvm::APInt;
  using llvm::APSInt;
  ContainerType Result;
  ContainerType Dummy;
  // CastRangeSize is an amount of all possible values of cast type.
  // Example: `char` has 256 values; `short` has 65536 values.
  // But in fact we use `amount of values` - 1, because
  // we can't keep `amount of values of UINT64` inside uint64_t.
  // E.g. 256 is an amount of all possible values of `char` and we can't keep
  // it inside `char`.
  // And it's OK, it's enough to do correct calculations.
  uint64_t CastRangeSize = APInt::getMaxValue(Ty.getBitWidth()).getZExtValue();
  for (const Range &R : What) {
    // Get bounds of the given range.
    APSInt FromInt = R.From();
    APSInt ToInt = R.To();
    // CurrentRangeSize is an amount of all possible values of the current
    // range minus one.
    uint64_t CurrentRangeSize = (ToInt - FromInt).getZExtValue();
    // This is an optimization for a specific case when this Range covers
    // the whole range of the target type.
    Dummy.clear();
    if (CurrentRangeSize >= CastRangeSize) {
      Dummy.emplace_back(ValueFactory.getMinValue(Ty),
                         ValueFactory.getMaxValue(Ty));
      Result = std::move(Dummy);
      break;
    }
    // Cast the bounds.
    Ty.apply(FromInt);
    Ty.apply(ToInt);
    const APSInt &PersistentFrom = ValueFactory.getValue(FromInt);
    const APSInt &PersistentTo = ValueFactory.getValue(ToInt);
    if (FromInt > ToInt) {
      Dummy.emplace_back(ValueFactory.getMinValue(Ty), PersistentTo);
      Dummy.emplace_back(PersistentFrom, ValueFactory.getMaxValue(Ty));
    } else
      Dummy.emplace_back(PersistentFrom, PersistentTo);
    // Every range retrieved after truncation potentialy has garbage values.
    // So, we have to unite every next range with the previouses.
    Result = unite(Result, Dummy);
  }

  return Result;
}

// Divide the convertion into two phases (presented as loops here).
// First phase(loop) works when casted values go in ascending order.
// E.g. char{1,3,5,127} -> uint{1,3,5,127}
// Interrupt the first phase and go to second one when casted values start
// go in descending order. That means that we crossed over the middle of
// the type value set (aka 0 for signeds and MAX/2+1 for unsigneds).
// For instance:
// 1: uchar{1,3,5,128,255} -> char{1,3,5,-128,-1}
//    Here we put {1,3,5} to one array and {-128, -1} to another
// 2: char{-128,-127,-1,0,1,2} -> uchar{128,129,255,0,1,3}
//    Here we put {128,129,255} to one array and {0,1,3} to another.
// After that we unite both arrays.
// NOTE: We don't just concatenate the arrays, because they may have
// adjacent ranges, e.g.:
// 1: char(-128, 127) -> uchar -> arr1(128, 255), arr2(0, 127) ->
//    unite -> uchar(0, 255)
// 2: uchar(0, 1)U(254, 255) -> char -> arr1(0, 1), arr2(-2, -1) ->
//    unite -> uchar(-2, 1)
RangeSet::ContainerType RangeSet::Factory::convertTo(RangeSet What,
                                                     APSIntType Ty) {
  using llvm::APInt;
  using llvm::APSInt;
  using Bounds = std::pair<const APSInt &, const APSInt &>;
  ContainerType AscendArray;
  ContainerType DescendArray;
  auto CastRange = [Ty, &VF = ValueFactory](const Range &R) -> Bounds {
    // Get bounds of the given range.
    APSInt FromInt = R.From();
    APSInt ToInt = R.To();
    // Cast the bounds.
    Ty.apply(FromInt);
    Ty.apply(ToInt);
    return {VF.getValue(FromInt), VF.getValue(ToInt)};
  };
  // Phase 1. Fill the first array.
  APSInt LastConvertedInt = Ty.getMinValue();
  const auto *It = What.begin();
  const auto *E = What.end();
  while (It != E) {
    Bounds NewBounds = CastRange(*(It++));
    // If values stop going acsending order, go to the second phase(loop).
    if (NewBounds.first < LastConvertedInt) {
      DescendArray.emplace_back(NewBounds.first, NewBounds.second);
      break;
    }
    // If the range contains a midpoint, then split the range.
    // E.g. char(-5, 5) -> uchar(251, 5)
    // Here we shall add a range (251, 255) to the first array and (0, 5) to the
    // second one.
    if (NewBounds.first > NewBounds.second) {
      DescendArray.emplace_back(ValueFactory.getMinValue(Ty), NewBounds.second);
      AscendArray.emplace_back(NewBounds.first, ValueFactory.getMaxValue(Ty));
    } else
      // Values are going acsending order.
      AscendArray.emplace_back(NewBounds.first, NewBounds.second);
    LastConvertedInt = NewBounds.first;
  }
  // Phase 2. Fill the second array.
  while (It != E) {
    Bounds NewBounds = CastRange(*(It++));
    DescendArray.emplace_back(NewBounds.first, NewBounds.second);
  }
  // Unite both arrays.
  return unite(AscendArray, DescendArray);
}

/// Promotion from unsigneds to signeds/unsigneds left.
RangeSet::ContainerType RangeSet::Factory::promoteTo(RangeSet What,
                                                     APSIntType Ty) {
  ContainerType Result;
  // We definitely know the size of the result set.
  Result.reserve(What.size());

  // Each unsigned value fits every larger type without any changes,
  // whether the larger type is signed or unsigned. So just promote and push
  // back each range one by one.
  for (const Range &R : What) {
    // Get bounds of the given range.
    llvm::APSInt FromInt = R.From();
    llvm::APSInt ToInt = R.To();
    // Cast the bounds.
    Ty.apply(FromInt);
    Ty.apply(ToInt);
    Result.emplace_back(ValueFactory.getValue(FromInt),
                        ValueFactory.getValue(ToInt));
  }
  return Result;
}

RangeSet RangeSet::Factory::deletePoint(RangeSet From,
                                        const llvm::APSInt &Point) {
  if (!From.contains(Point))
    return From;

  llvm::APSInt Upper = Point;
  llvm::APSInt Lower = Point;

  ++Upper;
  --Lower;

  // Notice that the lower bound is greater than the upper bound.
  return intersect(From, Upper, Lower);
}

LLVM_DUMP_METHOD void Range::dump(raw_ostream &OS) const {
  OS << '[' << toString(From(), 10) << ", " << toString(To(), 10) << ']';
}
LLVM_DUMP_METHOD void Range::dump() const { dump(llvm::errs()); }

LLVM_DUMP_METHOD void RangeSet::dump(raw_ostream &OS) const {
  OS << "{ ";
  llvm::interleaveComma(*this, OS, [&OS](const Range &R) { R.dump(OS); });
  OS << " }";
}
LLVM_DUMP_METHOD void RangeSet::dump() const { dump(llvm::errs()); }

REGISTER_SET_FACTORY_WITH_PROGRAMSTATE(SymbolSet, SymbolRef)

namespace {
class EquivalenceClass;
} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(ClassMap, SymbolRef, EquivalenceClass)
REGISTER_MAP_WITH_PROGRAMSTATE(ClassMembers, EquivalenceClass, SymbolSet)
REGISTER_MAP_WITH_PROGRAMSTATE(ConstraintRange, EquivalenceClass, RangeSet)

REGISTER_SET_FACTORY_WITH_PROGRAMSTATE(ClassSet, EquivalenceClass)
REGISTER_MAP_WITH_PROGRAMSTATE(DisequalityMap, EquivalenceClass, ClassSet)

namespace {
/// This class encapsulates a set of symbols equal to each other.
///
/// The main idea of the approach requiring such classes is in narrowing
/// and sharing constraints between symbols within the class.  Also we can
/// conclude that there is no practical need in storing constraints for
/// every member of the class separately.
///
/// Main terminology:
///
///   * "Equivalence class" is an object of this class, which can be efficiently
///     compared to other classes.  It represents the whole class without
///     storing the actual in it.  The members of the class however can be
///     retrieved from the state.
///
///   * "Class members" are the symbols corresponding to the class.  This means
///     that A == B for every member symbols A and B from the class.  Members of
///     each class are stored in the state.
///
///   * "Trivial class" is a class that has and ever had only one same symbol.
///
///   * "Merge operation" merges two classes into one.  It is the main operation
///     to produce non-trivial classes.
///     If, at some point, we can assume that two symbols from two distinct
///     classes are equal, we can merge these classes.
class EquivalenceClass : public llvm::FoldingSetNode {
public:
  /// Find equivalence class for the given symbol in the given state.
  [[nodiscard]] static inline EquivalenceClass find(ProgramStateRef State,
                                                    SymbolRef Sym);

  /// Merge classes for the given symbols and return a new state.
  [[nodiscard]] static inline ProgramStateRef merge(RangeSet::Factory &F,
                                                    ProgramStateRef State,
                                                    SymbolRef First,
                                                    SymbolRef Second);
  // Merge this class with the given class and return a new state.
  [[nodiscard]] inline ProgramStateRef
  merge(RangeSet::Factory &F, ProgramStateRef State, EquivalenceClass Other);

  /// Return a set of class members for the given state.
  [[nodiscard]] inline SymbolSet getClassMembers(ProgramStateRef State) const;

  /// Return true if the current class is trivial in the given state.
  /// A class is trivial if and only if there is not any member relations stored
  /// to it in State/ClassMembers.
  /// An equivalence class with one member might seem as it does not hold any
  /// meaningful information, i.e. that is a tautology. However, during the
  /// removal of dead symbols we do not remove classes with one member for
  /// resource and performance reasons. Consequently, a class with one member is
  /// not necessarily trivial. It could happen that we have a class with two
  /// members and then during the removal of dead symbols we remove one of its
  /// members. In this case, the class is still non-trivial (it still has the
  /// mappings in ClassMembers), even though it has only one member.
  [[nodiscard]] inline bool isTrivial(ProgramStateRef State) const;

  /// Return true if the current class is trivial and its only member is dead.
  [[nodiscard]] inline bool isTriviallyDead(ProgramStateRef State,
                                            SymbolReaper &Reaper) const;

  [[nodiscard]] static inline ProgramStateRef
  markDisequal(RangeSet::Factory &F, ProgramStateRef State, SymbolRef First,
               SymbolRef Second);
  [[nodiscard]] static inline ProgramStateRef
  markDisequal(RangeSet::Factory &F, ProgramStateRef State,
               EquivalenceClass First, EquivalenceClass Second);
  [[nodiscard]] inline ProgramStateRef
  markDisequal(RangeSet::Factory &F, ProgramStateRef State,
               EquivalenceClass Other) const;
  [[nodiscard]] static inline ClassSet getDisequalClasses(ProgramStateRef State,
                                                          SymbolRef Sym);
  [[nodiscard]] inline ClassSet getDisequalClasses(ProgramStateRef State) const;
  [[nodiscard]] inline ClassSet
  getDisequalClasses(DisequalityMapTy Map, ClassSet::Factory &Factory) const;

  [[nodiscard]] static inline std::optional<bool>
  areEqual(ProgramStateRef State, EquivalenceClass First,
           EquivalenceClass Second);
  [[nodiscard]] static inline std::optional<bool>
  areEqual(ProgramStateRef State, SymbolRef First, SymbolRef Second);

  /// Remove one member from the class.
  [[nodiscard]] ProgramStateRef removeMember(ProgramStateRef State,
                                             const SymbolRef Old);

  /// Iterate over all symbols and try to simplify them.
  [[nodiscard]] static inline ProgramStateRef simplify(SValBuilder &SVB,
                                                       RangeSet::Factory &F,
                                                       ProgramStateRef State,
                                                       EquivalenceClass Class);

  void dumpToStream(ProgramStateRef State, raw_ostream &os) const;
  LLVM_DUMP_METHOD void dump(ProgramStateRef State) const {
    dumpToStream(State, llvm::errs());
  }

  /// Check equivalence data for consistency.
  [[nodiscard]] LLVM_ATTRIBUTE_UNUSED static bool
  isClassDataConsistent(ProgramStateRef State);

  [[nodiscard]] QualType getType() const {
    return getRepresentativeSymbol()->getType();
  }

  EquivalenceClass() = delete;
  EquivalenceClass(const EquivalenceClass &) = default;
  EquivalenceClass &operator=(const EquivalenceClass &) = delete;
  EquivalenceClass(EquivalenceClass &&) = default;
  EquivalenceClass &operator=(EquivalenceClass &&) = delete;

  bool operator==(const EquivalenceClass &Other) const {
    return ID == Other.ID;
  }
  bool operator<(const EquivalenceClass &Other) const { return ID < Other.ID; }
  bool operator!=(const EquivalenceClass &Other) const {
    return !operator==(Other);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, uintptr_t CID) {
    ID.AddInteger(CID);
  }

  void Profile(llvm::FoldingSetNodeID &ID) const { Profile(ID, this->ID); }

private:
  /* implicit */ EquivalenceClass(SymbolRef Sym)
      : ID(reinterpret_cast<uintptr_t>(Sym)) {}

  /// This function is intended to be used ONLY within the class.
  /// The fact that ID is a pointer to a symbol is an implementation detail
  /// and should stay that way.
  /// In the current implementation, we use it to retrieve the only member
  /// of the trivial class.
  SymbolRef getRepresentativeSymbol() const {
    return reinterpret_cast<SymbolRef>(ID);
  }
  static inline SymbolSet::Factory &getMembersFactory(ProgramStateRef State);

  inline ProgramStateRef mergeImpl(RangeSet::Factory &F, ProgramStateRef State,
                                   SymbolSet Members, EquivalenceClass Other,
                                   SymbolSet OtherMembers);

  static inline bool
  addToDisequalityInfo(DisequalityMapTy &Info, ConstraintRangeTy &Constraints,
                       RangeSet::Factory &F, ProgramStateRef State,
                       EquivalenceClass First, EquivalenceClass Second);

  /// This is a unique identifier of the class.
  uintptr_t ID;
};

//===----------------------------------------------------------------------===//
//                             Constraint functions
//===----------------------------------------------------------------------===//

[[nodiscard]] LLVM_ATTRIBUTE_UNUSED bool
areFeasible(ConstraintRangeTy Constraints) {
  return llvm::none_of(
      Constraints,
      [](const std::pair<EquivalenceClass, RangeSet> &ClassConstraint) {
        return ClassConstraint.second.isEmpty();
      });
}

[[nodiscard]] inline const RangeSet *getConstraint(ProgramStateRef State,
                                                   EquivalenceClass Class) {
  return State->get<ConstraintRange>(Class);
}

[[nodiscard]] inline const RangeSet *getConstraint(ProgramStateRef State,
                                                   SymbolRef Sym) {
  return getConstraint(State, EquivalenceClass::find(State, Sym));
}

[[nodiscard]] ProgramStateRef setConstraint(ProgramStateRef State,
                                            EquivalenceClass Class,
                                            RangeSet Constraint) {
  return State->set<ConstraintRange>(Class, Constraint);
}

[[nodiscard]] ProgramStateRef setConstraints(ProgramStateRef State,
                                             ConstraintRangeTy Constraints) {
  return State->set<ConstraintRange>(Constraints);
}

//===----------------------------------------------------------------------===//
//                       Equality/diseqiality abstraction
//===----------------------------------------------------------------------===//

/// A small helper function for detecting symbolic (dis)equality.
///
/// Equality check can have different forms (like a == b or a - b) and this
/// class encapsulates those away if the only thing the user wants to check -
/// whether it's equality/diseqiality or not.
///
/// \returns true if assuming this Sym to be true means equality of operands
///          false if it means disequality of operands
///          std::nullopt otherwise
std::optional<bool> meansEquality(const SymSymExpr *Sym) {
  switch (Sym->getOpcode()) {
  case BO_Sub:
    // This case is: A - B != 0 -> disequality check.
    return false;
  case BO_EQ:
    // This case is: A == B != 0 -> equality check.
    return true;
  case BO_NE:
    // This case is: A != B != 0 -> diseqiality check.
    return false;
  default:
    return std::nullopt;
  }
}

//===----------------------------------------------------------------------===//
//                            Intersection functions
//===----------------------------------------------------------------------===//

template <class SecondTy, class... RestTy>
[[nodiscard]] inline RangeSet intersect(RangeSet::Factory &F, RangeSet Head,
                                        SecondTy Second, RestTy... Tail);

template <class... RangeTy> struct IntersectionTraits;

template <class... TailTy> struct IntersectionTraits<RangeSet, TailTy...> {
  // Found RangeSet, no need to check any further
  using Type = RangeSet;
};

template <> struct IntersectionTraits<> {
  // We ran out of types, and we didn't find any RangeSet, so the result should
  // be optional.
  using Type = std::optional<RangeSet>;
};

template <class OptionalOrPointer, class... TailTy>
struct IntersectionTraits<OptionalOrPointer, TailTy...> {
  // If current type is Optional or a raw pointer, we should keep looking.
  using Type = typename IntersectionTraits<TailTy...>::Type;
};

template <class EndTy>
[[nodiscard]] inline EndTy intersect(RangeSet::Factory &F, EndTy End) {
  // If the list contains only RangeSet or std::optional<RangeSet>, simply
  // return that range set.
  return End;
}

[[nodiscard]] LLVM_ATTRIBUTE_UNUSED inline std::optional<RangeSet>
intersect(RangeSet::Factory &F, const RangeSet *End) {
  // This is an extraneous conversion from a raw pointer into
  // std::optional<RangeSet>
  if (End) {
    return *End;
  }
  return std::nullopt;
}

template <class... RestTy>
[[nodiscard]] inline RangeSet intersect(RangeSet::Factory &F, RangeSet Head,
                                        RangeSet Second, RestTy... Tail) {
  // Here we call either the <RangeSet,RangeSet,...> or <RangeSet,...> version
  // of the function and can be sure that the result is RangeSet.
  return intersect(F, F.intersect(Head, Second), Tail...);
}

template <class SecondTy, class... RestTy>
[[nodiscard]] inline RangeSet intersect(RangeSet::Factory &F, RangeSet Head,
                                        SecondTy Second, RestTy... Tail) {
  if (Second) {
    // Here we call the <RangeSet,RangeSet,...> version of the function...
    return intersect(F, Head, *Second, Tail...);
  }
  // ...and here it is either <RangeSet,RangeSet,...> or <RangeSet,...>, which
  // means that the result is definitely RangeSet.
  return intersect(F, Head, Tail...);
}

/// Main generic intersect function.
/// It intersects all of the given range sets.  If some of the given arguments
/// don't hold a range set (nullptr or std::nullopt), the function will skip
/// them.
///
/// Available representations for the arguments are:
///   * RangeSet
///   * std::optional<RangeSet>
///   * RangeSet *
/// Pointer to a RangeSet is automatically assumed to be nullable and will get
/// checked as well as the optional version.  If this behaviour is undesired,
/// please dereference the pointer in the call.
///
/// Return type depends on the arguments' types.  If we can be sure in compile
/// time that there will be a range set as a result, the returning type is
/// simply RangeSet, in other cases we have to back off to
/// std::optional<RangeSet>.
///
/// Please, prefer optional range sets to raw pointers.  If the last argument is
/// a raw pointer and all previous arguments are std::nullopt, it will cost one
/// additional check to convert RangeSet * into std::optional<RangeSet>.
template <class HeadTy, class SecondTy, class... RestTy>
[[nodiscard]] inline
    typename IntersectionTraits<HeadTy, SecondTy, RestTy...>::Type
    intersect(RangeSet::Factory &F, HeadTy Head, SecondTy Second,
              RestTy... Tail) {
  if (Head) {
    return intersect(F, *Head, Second, Tail...);
  }
  return intersect(F, Second, Tail...);
}

//===----------------------------------------------------------------------===//
//                           Symbolic reasoning logic
//===----------------------------------------------------------------------===//

/// A little component aggregating all of the reasoning we have about
/// the ranges of symbolic expressions.
///
/// Even when we don't know the exact values of the operands, we still
/// can get a pretty good estimate of the result's range.
class SymbolicRangeInferrer
    : public SymExprVisitor<SymbolicRangeInferrer, RangeSet> {
public:
  template <class SourceType>
  static RangeSet inferRange(RangeSet::Factory &F, ProgramStateRef State,
                             SourceType Origin) {
    SymbolicRangeInferrer Inferrer(F, State);
    return Inferrer.infer(Origin);
  }

  RangeSet VisitSymExpr(SymbolRef Sym) {
    if (std::optional<RangeSet> RS = getRangeForNegatedSym(Sym))
      return *RS;
    // If we've reached this line, the actual type of the symbolic
    // expression is not supported for advanced inference.
    // In this case, we simply backoff to the default "let's simply
    // infer the range from the expression's type".
    return infer(Sym->getType());
  }

  RangeSet VisitUnarySymExpr(const UnarySymExpr *USE) {
    if (std::optional<RangeSet> RS = getRangeForNegatedUnarySym(USE))
      return *RS;
    return infer(USE->getType());
  }

  RangeSet VisitSymIntExpr(const SymIntExpr *Sym) {
    return VisitBinaryOperator(Sym);
  }

  RangeSet VisitIntSymExpr(const IntSymExpr *Sym) {
    return VisitBinaryOperator(Sym);
  }

  RangeSet VisitSymSymExpr(const SymSymExpr *SSE) {
    return intersect(
        RangeFactory,
        // If Sym is a difference of symbols A - B, then maybe we have range
        // set stored for B - A.
        //
        // If we have range set stored for both A - B and B - A then
        // calculate the effective range set by intersecting the range set
        // for A - B and the negated range set of B - A.
        getRangeForNegatedSymSym(SSE),
        // If Sym is a comparison expression (except <=>),
        // find any other comparisons with the same operands.
        // See function description.
        getRangeForComparisonSymbol(SSE),
        // If Sym is (dis)equality, we might have some information
        // on that in our equality classes data structure.
        getRangeForEqualities(SSE),
        // And we should always check what we can get from the operands.
        VisitBinaryOperator(SSE));
  }

private:
  SymbolicRangeInferrer(RangeSet::Factory &F, ProgramStateRef S)
      : ValueFactory(F.getValueFactory()), RangeFactory(F), State(S) {}

  /// Infer range information from the given integer constant.
  ///
  /// It's not a real "inference", but is here for operating with
  /// sub-expressions in a more polymorphic manner.
  RangeSet inferAs(const llvm::APSInt &Val, QualType) {
    return {RangeFactory, Val};
  }

  /// Infer range information from symbol in the context of the given type.
  RangeSet inferAs(SymbolRef Sym, QualType DestType) {
    QualType ActualType = Sym->getType();
    // Check that we can reason about the symbol at all.
    if (ActualType->isIntegralOrEnumerationType() ||
        Loc::isLocType(ActualType)) {
      return infer(Sym);
    }
    // Otherwise, let's simply infer from the destination type.
    // We couldn't figure out nothing else about that expression.
    return infer(DestType);
  }

  RangeSet infer(SymbolRef Sym) {
    return intersect(RangeFactory,
                     // Of course, we should take the constraint directly
                     // associated with this symbol into consideration.
                     getConstraint(State, Sym),
                     // Apart from the Sym itself, we can infer quite a lot if
                     // we look into subexpressions of Sym.
                     Visit(Sym));
  }

  RangeSet infer(EquivalenceClass Class) {
    if (const RangeSet *AssociatedConstraint = getConstraint(State, Class))
      return *AssociatedConstraint;

    return infer(Class.getType());
  }

  /// Infer range information solely from the type.
  RangeSet infer(QualType T) {
    // Lazily generate a new RangeSet representing all possible values for the
    // given symbol type.
    RangeSet Result(RangeFactory, ValueFactory.getMinValue(T),
                    ValueFactory.getMaxValue(T));

    // References are known to be non-zero.
    if (T->isReferenceType())
      return assumeNonZero(Result, T);

    return Result;
  }

  template <class BinarySymExprTy>
  RangeSet VisitBinaryOperator(const BinarySymExprTy *Sym) {
    // TODO #1: VisitBinaryOperator implementation might not make a good
    // use of the inferred ranges.  In this case, we might be calculating
    // everything for nothing.  This being said, we should introduce some
    // sort of laziness mechanism here.
    //
    // TODO #2: We didn't go into the nested expressions before, so it
    // might cause us spending much more time doing the inference.
    // This can be a problem for deeply nested expressions that are
    // involved in conditions and get tested continuously.  We definitely
    // need to address this issue and introduce some sort of caching
    // in here.
    QualType ResultType = Sym->getType();
    return VisitBinaryOperator(inferAs(Sym->getLHS(), ResultType),
                               Sym->getOpcode(),
                               inferAs(Sym->getRHS(), ResultType), ResultType);
  }

  RangeSet VisitBinaryOperator(RangeSet LHS, BinaryOperator::Opcode Op,
                               RangeSet RHS, QualType T);

  //===----------------------------------------------------------------------===//
  //                         Ranges and operators
  //===----------------------------------------------------------------------===//

  /// Return a rough approximation of the given range set.
  ///
  /// For the range set:
  ///   { [x_0, y_0], [x_1, y_1], ... , [x_N, y_N] }
  /// it will return the range [x_0, y_N].
  static Range fillGaps(RangeSet Origin) {
    assert(!Origin.isEmpty());
    return {Origin.getMinValue(), Origin.getMaxValue()};
  }

  /// Try to convert given range into the given type.
  ///
  /// It will return std::nullopt only when the trivial conversion is possible.
  std::optional<Range> convert(const Range &Origin, APSIntType To) {
    if (To.testInRange(Origin.From(), false) != APSIntType::RTR_Within ||
        To.testInRange(Origin.To(), false) != APSIntType::RTR_Within) {
      return std::nullopt;
    }
    return Range(ValueFactory.Convert(To, Origin.From()),
                 ValueFactory.Convert(To, Origin.To()));
  }

  template <BinaryOperator::Opcode Op>
  RangeSet VisitBinaryOperator(RangeSet LHS, RangeSet RHS, QualType T) {
    assert(!LHS.isEmpty() && !RHS.isEmpty());

    Range CoarseLHS = fillGaps(LHS);
    Range CoarseRHS = fillGaps(RHS);

    APSIntType ResultType = ValueFactory.getAPSIntType(T);

    // We need to convert ranges to the resulting type, so we can compare values
    // and combine them in a meaningful (in terms of the given operation) way.
    auto ConvertedCoarseLHS = convert(CoarseLHS, ResultType);
    auto ConvertedCoarseRHS = convert(CoarseRHS, ResultType);

    // It is hard to reason about ranges when conversion changes
    // borders of the ranges.
    if (!ConvertedCoarseLHS || !ConvertedCoarseRHS) {
      return infer(T);
    }

    return VisitBinaryOperator<Op>(*ConvertedCoarseLHS, *ConvertedCoarseRHS, T);
  }

  template <BinaryOperator::Opcode Op>
  RangeSet VisitBinaryOperator(Range LHS, Range RHS, QualType T) {
    return infer(T);
  }

  /// Return a symmetrical range for the given range and type.
  ///
  /// If T is signed, return the smallest range [-x..x] that covers the original
  /// range, or [-min(T), max(T)] if the aforementioned symmetric range doesn't
  /// exist due to original range covering min(T)).
  ///
  /// If T is unsigned, return the smallest range [0..x] that covers the
  /// original range.
  Range getSymmetricalRange(Range Origin, QualType T) {
    APSIntType RangeType = ValueFactory.getAPSIntType(T);

    if (RangeType.isUnsigned()) {
      return Range(ValueFactory.getMinValue(RangeType), Origin.To());
    }

    if (Origin.From().isMinSignedValue()) {
      // If mini is a minimal signed value, absolute value of it is greater
      // than the maximal signed value.  In order to avoid these
      // complications, we simply return the whole range.
      return {ValueFactory.getMinValue(RangeType),
              ValueFactory.getMaxValue(RangeType)};
    }

    // At this point, we are sure that the type is signed and we can safely
    // use unary - operator.
    //
    // While calculating absolute maximum, we can use the following formula
    // because of these reasons:
    //   * If From >= 0 then To >= From and To >= -From.
    //     AbsMax == To == max(To, -From)
    //   * If To <= 0 then -From >= -To and -From >= From.
    //     AbsMax == -From == max(-From, To)
    //   * Otherwise, From <= 0, To >= 0, and
    //     AbsMax == max(abs(From), abs(To))
    llvm::APSInt AbsMax = std::max(-Origin.From(), Origin.To());

    // Intersection is guaranteed to be non-empty.
    return {ValueFactory.getValue(-AbsMax), ValueFactory.getValue(AbsMax)};
  }

  /// Return a range set subtracting zero from \p Domain.
  RangeSet assumeNonZero(RangeSet Domain, QualType T) {
    APSIntType IntType = ValueFactory.getAPSIntType(T);
    return RangeFactory.deletePoint(Domain, IntType.getZeroValue());
  }

  template <typename ProduceNegatedSymFunc>
  std::optional<RangeSet> getRangeForNegatedExpr(ProduceNegatedSymFunc F,
                                                 QualType T) {
    // Do not negate if the type cannot be meaningfully negated.
    if (!T->isUnsignedIntegerOrEnumerationType() &&
        !T->isSignedIntegerOrEnumerationType())
      return std::nullopt;

    if (SymbolRef NegatedSym = F())
      if (const RangeSet *NegatedRange = getConstraint(State, NegatedSym))
        return RangeFactory.negate(*NegatedRange);

    return std::nullopt;
  }

  std::optional<RangeSet> getRangeForNegatedUnarySym(const UnarySymExpr *USE) {
    // Just get the operand when we negate a symbol that is already negated.
    // -(-a) == a
    return getRangeForNegatedExpr(
        [USE]() -> SymbolRef {
          if (USE->getOpcode() == UO_Minus)
            return USE->getOperand();
          return nullptr;
        },
        USE->getType());
  }

  std::optional<RangeSet> getRangeForNegatedSymSym(const SymSymExpr *SSE) {
    return getRangeForNegatedExpr(
        [SSE, State = this->State]() -> SymbolRef {
          if (SSE->getOpcode() == BO_Sub)
            return State->getSymbolManager().getSymSymExpr(
                SSE->getRHS(), BO_Sub, SSE->getLHS(), SSE->getType());
          return nullptr;
        },
        SSE->getType());
  }

  std::optional<RangeSet> getRangeForNegatedSym(SymbolRef Sym) {
    return getRangeForNegatedExpr(
        [Sym, State = this->State]() {
          return State->getSymbolManager().getUnarySymExpr(Sym, UO_Minus,
                                                           Sym->getType());
        },
        Sym->getType());
  }

  // Returns ranges only for binary comparison operators (except <=>)
  // when left and right operands are symbolic values.
  // Finds any other comparisons with the same operands.
  // Then do logical calculations and refuse impossible branches.
  // E.g. (x < y) and (x > y) at the same time are impossible.
  // E.g. (x >= y) and (x != y) at the same time makes (x > y) true only.
  // E.g. (x == y) and (y == x) are just reversed but the same.
  // It covers all possible combinations (see CmpOpTable description).
  // Note that `x` and `y` can also stand for subexpressions,
  // not only for actual symbols.
  std::optional<RangeSet> getRangeForComparisonSymbol(const SymSymExpr *SSE) {
    const BinaryOperatorKind CurrentOP = SSE->getOpcode();

    // We currently do not support <=> (C++20).
    if (!BinaryOperator::isComparisonOp(CurrentOP) || (CurrentOP == BO_Cmp))
      return std::nullopt;

    static const OperatorRelationsTable CmpOpTable{};

    const SymExpr *LHS = SSE->getLHS();
    const SymExpr *RHS = SSE->getRHS();
    QualType T = SSE->getType();

    SymbolManager &SymMgr = State->getSymbolManager();

    // We use this variable to store the last queried operator (`QueriedOP`)
    // for which the `getCmpOpState` returned with `Unknown`. If there are two
    // different OPs that returned `Unknown` then we have to query the special
    // `UnknownX2` column. We assume that `getCmpOpState(CurrentOP, CurrentOP)`
    // never returns `Unknown`, so `CurrentOP` is a good initial value.
    BinaryOperatorKind LastQueriedOpToUnknown = CurrentOP;

    // Loop goes through all of the columns exept the last one ('UnknownX2').
    // We treat `UnknownX2` column separately at the end of the loop body.
    for (size_t i = 0; i < CmpOpTable.getCmpOpCount(); ++i) {

      // Let's find an expression e.g. (x < y).
      BinaryOperatorKind QueriedOP = OperatorRelationsTable::getOpFromIndex(i);
      const SymSymExpr *SymSym = SymMgr.getSymSymExpr(LHS, QueriedOP, RHS, T);
      const RangeSet *QueriedRangeSet = getConstraint(State, SymSym);

      // If ranges were not previously found,
      // try to find a reversed expression (y > x).
      if (!QueriedRangeSet) {
        const BinaryOperatorKind ROP =
            BinaryOperator::reverseComparisonOp(QueriedOP);
        SymSym = SymMgr.getSymSymExpr(RHS, ROP, LHS, T);
        QueriedRangeSet = getConstraint(State, SymSym);
      }

      if (!QueriedRangeSet || QueriedRangeSet->isEmpty())
        continue;

      const llvm::APSInt *ConcreteValue = QueriedRangeSet->getConcreteValue();
      const bool isInFalseBranch =
          ConcreteValue ? (*ConcreteValue == 0) : false;

      // If it is a false branch, we shall be guided by opposite operator,
      // because the table is made assuming we are in the true branch.
      // E.g. when (x <= y) is false, then (x > y) is true.
      if (isInFalseBranch)
        QueriedOP = BinaryOperator::negateComparisonOp(QueriedOP);

      OperatorRelationsTable::TriStateKind BranchState =
          CmpOpTable.getCmpOpState(CurrentOP, QueriedOP);

      if (BranchState == OperatorRelationsTable::Unknown) {
        if (LastQueriedOpToUnknown != CurrentOP &&
            LastQueriedOpToUnknown != QueriedOP) {
          // If we got the Unknown state for both different operators.
          // if (x <= y)    // assume true
          //   if (x != y)  // assume true
          //     if (x < y) // would be also true
          // Get a state from `UnknownX2` column.
          BranchState = CmpOpTable.getCmpOpStateForUnknownX2(CurrentOP);
        } else {
          LastQueriedOpToUnknown = QueriedOP;
          continue;
        }
      }

      return (BranchState == OperatorRelationsTable::True) ? getTrueRange(T)
                                                           : getFalseRange(T);
    }

    return std::nullopt;
  }

  std::optional<RangeSet> getRangeForEqualities(const SymSymExpr *Sym) {
    std::optional<bool> Equality = meansEquality(Sym);

    if (!Equality)
      return std::nullopt;

    if (std::optional<bool> AreEqual =
            EquivalenceClass::areEqual(State, Sym->getLHS(), Sym->getRHS())) {
      // Here we cover two cases at once:
      //   * if Sym is equality and its operands are known to be equal -> true
      //   * if Sym is disequality and its operands are disequal -> true
      if (*AreEqual == *Equality) {
        return getTrueRange(Sym->getType());
      }
      // Opposite combinations result in false.
      return getFalseRange(Sym->getType());
    }

    return std::nullopt;
  }

  RangeSet getTrueRange(QualType T) {
    RangeSet TypeRange = infer(T);
    return assumeNonZero(TypeRange, T);
  }

  RangeSet getFalseRange(QualType T) {
    const llvm::APSInt &Zero = ValueFactory.getValue(0, T);
    return RangeSet(RangeFactory, Zero);
  }

  BasicValueFactory &ValueFactory;
  RangeSet::Factory &RangeFactory;
  ProgramStateRef State;
};

//===----------------------------------------------------------------------===//
//               Range-based reasoning about symbolic operations
//===----------------------------------------------------------------------===//

template <>
RangeSet SymbolicRangeInferrer::VisitBinaryOperator<BO_NE>(RangeSet LHS,
                                                           RangeSet RHS,
                                                           QualType T) {
  assert(!LHS.isEmpty() && !RHS.isEmpty());

  if (LHS.getAPSIntType() == RHS.getAPSIntType()) {
    if (intersect(RangeFactory, LHS, RHS).isEmpty())
      return getTrueRange(T);

  } else {
    // We can only lose information if we are casting smaller signed type to
    // bigger unsigned type. For e.g.,
    //    LHS (unsigned short): [2, USHRT_MAX]
    //    RHS   (signed short): [SHRT_MIN, 0]
    //
    // Casting RHS to LHS type will leave us with overlapping values
    //    CastedRHS : [0, 0] U [SHRT_MAX + 1, USHRT_MAX]
    //
    // We can avoid this by checking if signed type's maximum value is lesser
    // than unsigned type's minimum value.

    // If both have different signs then only we can get more information.
    if (LHS.isUnsigned() != RHS.isUnsigned()) {
      if (LHS.isUnsigned() && (LHS.getBitWidth() >= RHS.getBitWidth())) {
        if (RHS.getMaxValue().isNegative() ||
            LHS.getAPSIntType().convert(RHS.getMaxValue()) < LHS.getMinValue())
          return getTrueRange(T);

      } else if (RHS.isUnsigned() && (LHS.getBitWidth() <= RHS.getBitWidth())) {
        if (LHS.getMaxValue().isNegative() ||
            RHS.getAPSIntType().convert(LHS.getMaxValue()) < RHS.getMinValue())
          return getTrueRange(T);
      }
    }

    // Both RangeSets should be casted to bigger unsigned type.
    APSIntType CastingType(std::max(LHS.getBitWidth(), RHS.getBitWidth()),
                           LHS.isUnsigned() || RHS.isUnsigned());

    RangeSet CastedLHS = RangeFactory.castTo(LHS, CastingType);
    RangeSet CastedRHS = RangeFactory.castTo(RHS, CastingType);

    if (intersect(RangeFactory, CastedLHS, CastedRHS).isEmpty())
      return getTrueRange(T);
  }

  // In all other cases, the resulting range cannot be deduced.
  return infer(T);
}

template <>
RangeSet SymbolicRangeInferrer::VisitBinaryOperator<BO_Or>(Range LHS, Range RHS,
                                                           QualType T) {
  APSIntType ResultType = ValueFactory.getAPSIntType(T);
  llvm::APSInt Zero = ResultType.getZeroValue();

  bool IsLHSPositiveOrZero = LHS.From() >= Zero;
  bool IsRHSPositiveOrZero = RHS.From() >= Zero;

  bool IsLHSNegative = LHS.To() < Zero;
  bool IsRHSNegative = RHS.To() < Zero;

  // Check if both ranges have the same sign.
  if ((IsLHSPositiveOrZero && IsRHSPositiveOrZero) ||
      (IsLHSNegative && IsRHSNegative)) {
    // The result is definitely greater or equal than any of the operands.
    const llvm::APSInt &Min = std::max(LHS.From(), RHS.From());

    // We estimate maximal value for positives as the maximal value for the
    // given type.  For negatives, we estimate it with -1 (e.g. 0x11111111).
    //
    // TODO: We basically, limit the resulting range from below, but don't do
    //       anything with the upper bound.
    //
    //       For positive operands, it can be done as follows: for the upper
    //       bound of LHS and RHS we calculate the most significant bit set.
    //       Let's call it the N-th bit.  Then we can estimate the maximal
    //       number to be 2^(N+1)-1, i.e. the number with all the bits up to
    //       the N-th bit set.
    const llvm::APSInt &Max = IsLHSNegative
                                  ? ValueFactory.getValue(--Zero)
                                  : ValueFactory.getMaxValue(ResultType);

    return {RangeFactory, ValueFactory.getValue(Min), Max};
  }

  // Otherwise, let's check if at least one of the operands is negative.
  if (IsLHSNegative || IsRHSNegative) {
    // This means that the result is definitely negative as well.
    return {RangeFactory, ValueFactory.getMinValue(ResultType),
            ValueFactory.getValue(--Zero)};
  }

  RangeSet DefaultRange = infer(T);

  // It is pretty hard to reason about operands with different signs
  // (and especially with possibly different signs).  We simply check if it
  // can be zero.  In order to conclude that the result could not be zero,
  // at least one of the operands should be definitely not zero itself.
  if (!LHS.Includes(Zero) || !RHS.Includes(Zero)) {
    return assumeNonZero(DefaultRange, T);
  }

  // Nothing much else to do here.
  return DefaultRange;
}

template <>
RangeSet SymbolicRangeInferrer::VisitBinaryOperator<BO_And>(Range LHS,
                                                            Range RHS,
                                                            QualType T) {
  APSIntType ResultType = ValueFactory.getAPSIntType(T);
  llvm::APSInt Zero = ResultType.getZeroValue();

  bool IsLHSPositiveOrZero = LHS.From() >= Zero;
  bool IsRHSPositiveOrZero = RHS.From() >= Zero;

  bool IsLHSNegative = LHS.To() < Zero;
  bool IsRHSNegative = RHS.To() < Zero;

  // Check if both ranges have the same sign.
  if ((IsLHSPositiveOrZero && IsRHSPositiveOrZero) ||
      (IsLHSNegative && IsRHSNegative)) {
    // The result is definitely less or equal than any of the operands.
    const llvm::APSInt &Max = std::min(LHS.To(), RHS.To());

    // We conservatively estimate lower bound to be the smallest positive
    // or negative value corresponding to the sign of the operands.
    const llvm::APSInt &Min = IsLHSNegative
                                  ? ValueFactory.getMinValue(ResultType)
                                  : ValueFactory.getValue(Zero);

    return {RangeFactory, Min, Max};
  }

  // Otherwise, let's check if at least one of the operands is positive.
  if (IsLHSPositiveOrZero || IsRHSPositiveOrZero) {
    // This makes result definitely positive.
    //
    // We can also reason about a maximal value by finding the maximal
    // value of the positive operand.
    const llvm::APSInt &Max = IsLHSPositiveOrZero ? LHS.To() : RHS.To();

    // The minimal value on the other hand is much harder to reason about.
    // The only thing we know for sure is that the result is positive.
    return {RangeFactory, ValueFactory.getValue(Zero),
            ValueFactory.getValue(Max)};
  }

  // Nothing much else to do here.
  return infer(T);
}

template <>
RangeSet SymbolicRangeInferrer::VisitBinaryOperator<BO_Rem>(Range LHS,
                                                            Range RHS,
                                                            QualType T) {
  llvm::APSInt Zero = ValueFactory.getAPSIntType(T).getZeroValue();

  Range ConservativeRange = getSymmetricalRange(RHS, T);

  llvm::APSInt Max = ConservativeRange.To();
  llvm::APSInt Min = ConservativeRange.From();

  if (Max == Zero) {
    // It's an undefined behaviour to divide by 0 and it seems like we know
    // for sure that RHS is 0.  Let's say that the resulting range is
    // simply infeasible for that matter.
    return RangeFactory.getEmptySet();
  }

  // At this point, our conservative range is closed.  The result, however,
  // couldn't be greater than the RHS' maximal absolute value.  Because of
  // this reason, we turn the range into open (or half-open in case of
  // unsigned integers).
  //
  // While we operate on integer values, an open interval (a, b) can be easily
  // represented by the closed interval [a + 1, b - 1].  And this is exactly
  // what we do next.
  //
  // If we are dealing with unsigned case, we shouldn't move the lower bound.
  if (Min.isSigned()) {
    ++Min;
  }
  --Max;

  bool IsLHSPositiveOrZero = LHS.From() >= Zero;
  bool IsRHSPositiveOrZero = RHS.From() >= Zero;

  // Remainder operator results with negative operands is implementation
  // defined.  Positive cases are much easier to reason about though.
  if (IsLHSPositiveOrZero && IsRHSPositiveOrZero) {
    // If maximal value of LHS is less than maximal value of RHS,
    // the result won't get greater than LHS.To().
    Max = std::min(LHS.To(), Max);
    // We want to check if it is a situation similar to the following:
    //
    // <------------|---[  LHS  ]--------[  RHS  ]----->
    //  -INF        0                              +INF
    //
    // In this situation, we can conclude that (LHS / RHS) == 0 and
    // (LHS % RHS) == LHS.
    Min = LHS.To() < RHS.From() ? LHS.From() : Zero;
  }

  // Nevertheless, the symmetrical range for RHS is a conservative estimate
  // for any sign of either LHS, or RHS.
  return {RangeFactory, ValueFactory.getValue(Min), ValueFactory.getValue(Max)};
}

RangeSet SymbolicRangeInferrer::VisitBinaryOperator(RangeSet LHS,
                                                    BinaryOperator::Opcode Op,
                                                    RangeSet RHS, QualType T) {
  // We should propagate information about unfeasbility of one of the
  // operands to the resulting range.
  if (LHS.isEmpty() || RHS.isEmpty()) {
    return RangeFactory.getEmptySet();
  }

  switch (Op) {
  case BO_NE:
    return VisitBinaryOperator<BO_NE>(LHS, RHS, T);
  case BO_Or:
    return VisitBinaryOperator<BO_Or>(LHS, RHS, T);
  case BO_And:
    return VisitBinaryOperator<BO_And>(LHS, RHS, T);
  case BO_Rem:
    return VisitBinaryOperator<BO_Rem>(LHS, RHS, T);
  default:
    return infer(T);
  }
}

//===----------------------------------------------------------------------===//
//                  Constraint manager implementation details
//===----------------------------------------------------------------------===//

class RangeConstraintManager : public RangedConstraintManager {
public:
  RangeConstraintManager(ExprEngine *EE, SValBuilder &SVB)
      : RangedConstraintManager(EE, SVB), F(getBasicVals()) {}

  //===------------------------------------------------------------------===//
  // Implementation for interface from ConstraintManager.
  //===------------------------------------------------------------------===//

  bool haveEqualConstraints(ProgramStateRef S1,
                            ProgramStateRef S2) const override {
    // NOTE: ClassMembers are as simple as back pointers for ClassMap,
    //       so comparing constraint ranges and class maps should be
    //       sufficient.
    return S1->get<ConstraintRange>() == S2->get<ConstraintRange>() &&
           S1->get<ClassMap>() == S2->get<ClassMap>();
  }

  bool canReasonAbout(SVal X) const override;

  ConditionTruthVal checkNull(ProgramStateRef State, SymbolRef Sym) override;

  const llvm::APSInt *getSymVal(ProgramStateRef State,
                                SymbolRef Sym) const override;

  const llvm::APSInt *getSymMinVal(ProgramStateRef State,
                                   SymbolRef Sym) const override;

  const llvm::APSInt *getSymMaxVal(ProgramStateRef State,
                                   SymbolRef Sym) const override;

  ProgramStateRef removeDeadBindings(ProgramStateRef State,
                                     SymbolReaper &SymReaper) override;

  void printJson(raw_ostream &Out, ProgramStateRef State, const char *NL = "\n",
                 unsigned int Space = 0, bool IsDot = false) const override;
  void printValue(raw_ostream &Out, ProgramStateRef State,
                  SymbolRef Sym) override;
  void printConstraints(raw_ostream &Out, ProgramStateRef State,
                        const char *NL = "\n", unsigned int Space = 0,
                        bool IsDot = false) const;
  void printEquivalenceClasses(raw_ostream &Out, ProgramStateRef State,
                               const char *NL = "\n", unsigned int Space = 0,
                               bool IsDot = false) const;
  void printDisequalities(raw_ostream &Out, ProgramStateRef State,
                          const char *NL = "\n", unsigned int Space = 0,
                          bool IsDot = false) const;

  //===------------------------------------------------------------------===//
  // Implementation for interface from RangedConstraintManager.
  //===------------------------------------------------------------------===//

  ProgramStateRef assumeSymNE(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymEQ(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymLT(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymGT(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymLE(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymGE(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymWithinInclusiveRange(
      ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
      const llvm::APSInt &To, const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymOutsideInclusiveRange(
      ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
      const llvm::APSInt &To, const llvm::APSInt &Adjustment) override;

private:
  RangeSet::Factory F;

  RangeSet getRange(ProgramStateRef State, SymbolRef Sym);
  RangeSet getRange(ProgramStateRef State, EquivalenceClass Class);
  ProgramStateRef setRange(ProgramStateRef State, SymbolRef Sym,
                           RangeSet Range);
  ProgramStateRef setRange(ProgramStateRef State, EquivalenceClass Class,
                           RangeSet Range);

  RangeSet getSymLTRange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymGTRange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymLERange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymLERange(llvm::function_ref<RangeSet()> RS,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymGERange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
};

//===----------------------------------------------------------------------===//
//                         Constraint assignment logic
//===----------------------------------------------------------------------===//

/// ConstraintAssignorBase is a small utility class that unifies visitor
/// for ranges with a visitor for constraints (rangeset/range/constant).
///
/// It is designed to have one derived class, but generally it can have more.
/// Derived class can control which types we handle by defining methods of the
/// following form:
///
///   bool handle${SYMBOL}To${CONSTRAINT}(const SYMBOL *Sym,
///                                       CONSTRAINT Constraint);
///
/// where SYMBOL is the type of the symbol (e.g. SymSymExpr, SymbolCast, etc.)
///       CONSTRAINT is the type of constraint (RangeSet/Range/Const)
///       return value signifies whether we should try other handle methods
///          (i.e. false would mean to stop right after calling this method)
template <class Derived> class ConstraintAssignorBase {
public:
  using Const = const llvm::APSInt &;

#define DISPATCH(CLASS) return assign##CLASS##Impl(cast<CLASS>(Sym), Constraint)

#define ASSIGN(CLASS, TO, SYM, CONSTRAINT)                                     \
  if (!static_cast<Derived *>(this)->assign##CLASS##To##TO(SYM, CONSTRAINT))   \
  return false

  void assign(SymbolRef Sym, RangeSet Constraint) {
    assignImpl(Sym, Constraint);
  }

  bool assignImpl(SymbolRef Sym, RangeSet Constraint) {
    switch (Sym->getKind()) {
#define SYMBOL(Id, Parent)                                                     \
  case SymExpr::Id##Kind:                                                      \
    DISPATCH(Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/Symbols.def"
    }
    llvm_unreachable("Unknown SymExpr kind!");
  }

#define DEFAULT_ASSIGN(Id)                                                     \
  bool assign##Id##To##RangeSet(const Id *Sym, RangeSet Constraint) {          \
    return true;                                                               \
  }                                                                            \
  bool assign##Id##To##Range(const Id *Sym, Range Constraint) { return true; } \
  bool assign##Id##To##Const(const Id *Sym, Const Constraint) { return true; }

  // When we dispatch for constraint types, we first try to check
  // if the new constraint is the constant and try the corresponding
  // assignor methods.  If it didn't interrupt, we can proceed to the
  // range, and finally to the range set.
#define CONSTRAINT_DISPATCH(Id)                                                \
  if (const llvm::APSInt *Const = Constraint.getConcreteValue()) {             \
    ASSIGN(Id, Const, Sym, *Const);                                            \
  }                                                                            \
  if (Constraint.size() == 1) {                                                \
    ASSIGN(Id, Range, Sym, *Constraint.begin());                               \
  }                                                                            \
  ASSIGN(Id, RangeSet, Sym, Constraint)

  // Our internal assign method first tries to call assignor methods for all
  // constraint types that apply.  And if not interrupted, continues with its
  // parent class.
#define SYMBOL(Id, Parent)                                                     \
  bool assign##Id##Impl(const Id *Sym, RangeSet Constraint) {                  \
    CONSTRAINT_DISPATCH(Id);                                                   \
    DISPATCH(Parent);                                                          \
  }                                                                            \
  DEFAULT_ASSIGN(Id)
#define ABSTRACT_SYMBOL(Id, Parent) SYMBOL(Id, Parent)
#include "clang/StaticAnalyzer/Core/PathSensitive/Symbols.def"

  // Default implementations for the top class that doesn't have parents.
  bool assignSymExprImpl(const SymExpr *Sym, RangeSet Constraint) {
    CONSTRAINT_DISPATCH(SymExpr);
    return true;
  }
  DEFAULT_ASSIGN(SymExpr);

#undef DISPATCH
#undef CONSTRAINT_DISPATCH
#undef DEFAULT_ASSIGN
#undef ASSIGN
};

/// A little component aggregating all of the reasoning we have about
/// assigning new constraints to symbols.
///
/// The main purpose of this class is to associate constraints to symbols,
/// and impose additional constraints on other symbols, when we can imply
/// them.
///
/// It has a nice symmetry with SymbolicRangeInferrer.  When the latter
/// can provide more precise ranges by looking into the operands of the
/// expression in question, ConstraintAssignor looks into the operands
/// to see if we can imply more from the new constraint.
class ConstraintAssignor : public ConstraintAssignorBase<ConstraintAssignor> {
public:
  template <class ClassOrSymbol>
  [[nodiscard]] static ProgramStateRef
  assign(ProgramStateRef State, SValBuilder &Builder, RangeSet::Factory &F,
         ClassOrSymbol CoS, RangeSet NewConstraint) {
    if (!State || NewConstraint.isEmpty())
      return nullptr;

    ConstraintAssignor Assignor{State, Builder, F};
    return Assignor.assign(CoS, NewConstraint);
  }

  /// Handle expressions like: a % b != 0.
  template <typename SymT>
  bool handleRemainderOp(const SymT *Sym, RangeSet Constraint) {
    if (Sym->getOpcode() != BO_Rem)
      return true;
    // a % b != 0 implies that a != 0.
    if (!Constraint.containsZero()) {
      SVal SymSVal = Builder.makeSymbolVal(Sym->getLHS());
      if (auto NonLocSymSVal = SymSVal.getAs<nonloc::SymbolVal>()) {
        State = State->assume(*NonLocSymSVal, true);
        if (!State)
          return false;
      }
    }
    return true;
  }

  inline bool assignSymExprToConst(const SymExpr *Sym, Const Constraint);
  inline bool assignSymIntExprToRangeSet(const SymIntExpr *Sym,
                                         RangeSet Constraint) {
    return handleRemainderOp(Sym, Constraint);
  }
  inline bool assignSymSymExprToRangeSet(const SymSymExpr *Sym,
                                         RangeSet Constraint);

private:
  ConstraintAssignor(ProgramStateRef State, SValBuilder &Builder,
                     RangeSet::Factory &F)
      : State(State), Builder(Builder), RangeFactory(F) {}
  using Base = ConstraintAssignorBase<ConstraintAssignor>;

  /// Base method for handling new constraints for symbols.
  [[nodiscard]] ProgramStateRef assign(SymbolRef Sym, RangeSet NewConstraint) {
    // All constraints are actually associated with equivalence classes, and
    // that's what we are going to do first.
    State = assign(EquivalenceClass::find(State, Sym), NewConstraint);
    if (!State)
      return nullptr;

    // And after that we can check what other things we can get from this
    // constraint.
    Base::assign(Sym, NewConstraint);
    return State;
  }

  /// Base method for handling new constraints for classes.
  [[nodiscard]] ProgramStateRef assign(EquivalenceClass Class,
                                       RangeSet NewConstraint) {
    // There is a chance that we might need to update constraints for the
    // classes that are known to be disequal to Class.
    //
    // In order for this to be even possible, the new constraint should
    // be simply a constant because we can't reason about range disequalities.
    if (const llvm::APSInt *Point = NewConstraint.getConcreteValue()) {

      ConstraintRangeTy Constraints = State->get<ConstraintRange>();
      ConstraintRangeTy::Factory &CF = State->get_context<ConstraintRange>();

      // Add new constraint.
      Constraints = CF.add(Constraints, Class, NewConstraint);

      for (EquivalenceClass DisequalClass : Class.getDisequalClasses(State)) {
        RangeSet UpdatedConstraint = SymbolicRangeInferrer::inferRange(
            RangeFactory, State, DisequalClass);

        UpdatedConstraint = RangeFactory.deletePoint(UpdatedConstraint, *Point);

        // If we end up with at least one of the disequal classes to be
        // constrained with an empty range-set, the state is infeasible.
        if (UpdatedConstraint.isEmpty())
          return nullptr;

        Constraints = CF.add(Constraints, DisequalClass, UpdatedConstraint);
      }
      assert(areFeasible(Constraints) && "Constraint manager shouldn't produce "
                                         "a state with infeasible constraints");

      return setConstraints(State, Constraints);
    }

    return setConstraint(State, Class, NewConstraint);
  }

  ProgramStateRef trackDisequality(ProgramStateRef State, SymbolRef LHS,
                                   SymbolRef RHS) {
    return EquivalenceClass::markDisequal(RangeFactory, State, LHS, RHS);
  }

  ProgramStateRef trackEquality(ProgramStateRef State, SymbolRef LHS,
                                SymbolRef RHS) {
    return EquivalenceClass::merge(RangeFactory, State, LHS, RHS);
  }

  [[nodiscard]] std::optional<bool> interpreteAsBool(RangeSet Constraint) {
    assert(!Constraint.isEmpty() && "Empty ranges shouldn't get here");

    if (Constraint.getConcreteValue())
      return !Constraint.getConcreteValue()->isZero();

    if (!Constraint.containsZero())
      return true;

    return std::nullopt;
  }

  ProgramStateRef State;
  SValBuilder &Builder;
  RangeSet::Factory &RangeFactory;
};

bool ConstraintAssignor::assignSymExprToConst(const SymExpr *Sym,
                                              const llvm::APSInt &Constraint) {
  llvm::SmallSet<EquivalenceClass, 4> SimplifiedClasses;
  // Iterate over all equivalence classes and try to simplify them.
  ClassMembersTy Members = State->get<ClassMembers>();
  for (std::pair<EquivalenceClass, SymbolSet> ClassToSymbolSet : Members) {
    EquivalenceClass Class = ClassToSymbolSet.first;
    State = EquivalenceClass::simplify(Builder, RangeFactory, State, Class);
    if (!State)
      return false;
    SimplifiedClasses.insert(Class);
  }

  // Trivial equivalence classes (those that have only one symbol member) are
  // not stored in the State. Thus, we must skim through the constraints as
  // well. And we try to simplify symbols in the constraints.
  ConstraintRangeTy Constraints = State->get<ConstraintRange>();
  for (std::pair<EquivalenceClass, RangeSet> ClassConstraint : Constraints) {
    EquivalenceClass Class = ClassConstraint.first;
    if (SimplifiedClasses.count(Class)) // Already simplified.
      continue;
    State = EquivalenceClass::simplify(Builder, RangeFactory, State, Class);
    if (!State)
      return false;
  }

  // We may have trivial equivalence classes in the disequality info as
  // well, and we need to simplify them.
  DisequalityMapTy DisequalityInfo = State->get<DisequalityMap>();
  for (std::pair<EquivalenceClass, ClassSet> DisequalityEntry :
       DisequalityInfo) {
    EquivalenceClass Class = DisequalityEntry.first;
    ClassSet DisequalClasses = DisequalityEntry.second;
    State = EquivalenceClass::simplify(Builder, RangeFactory, State, Class);
    if (!State)
      return false;
  }

  return true;
}

bool ConstraintAssignor::assignSymSymExprToRangeSet(const SymSymExpr *Sym,
                                                    RangeSet Constraint) {
  if (!handleRemainderOp(Sym, Constraint))
    return false;

  std::optional<bool> ConstraintAsBool = interpreteAsBool(Constraint);

  if (!ConstraintAsBool)
    return true;

  if (std::optional<bool> Equality = meansEquality(Sym)) {
    // Here we cover two cases:
    //   * if Sym is equality and the new constraint is true -> Sym's operands
    //     should be marked as equal
    //   * if Sym is disequality and the new constraint is false -> Sym's
    //     operands should be also marked as equal
    if (*Equality == *ConstraintAsBool) {
      State = trackEquality(State, Sym->getLHS(), Sym->getRHS());
    } else {
      // Other combinations leave as with disequal operands.
      State = trackDisequality(State, Sym->getLHS(), Sym->getRHS());
    }

    if (!State)
      return false;
  }

  return true;
}

} // end anonymous namespace

std::unique_ptr<ConstraintManager>
ento::CreateRangeConstraintManager(ProgramStateManager &StMgr,
                                   ExprEngine *Eng) {
  return std::make_unique<RangeConstraintManager>(Eng, StMgr.getSValBuilder());
}

ConstraintMap ento::getConstraintMap(ProgramStateRef State) {
  ConstraintMap::Factory &F = State->get_context<ConstraintMap>();
  ConstraintMap Result = F.getEmptyMap();

  ConstraintRangeTy Constraints = State->get<ConstraintRange>();
  for (std::pair<EquivalenceClass, RangeSet> ClassConstraint : Constraints) {
    EquivalenceClass Class = ClassConstraint.first;
    SymbolSet ClassMembers = Class.getClassMembers(State);
    assert(!ClassMembers.isEmpty() &&
           "Class must always have at least one member!");

    SymbolRef Representative = *ClassMembers.begin();
    Result = F.add(Result, Representative, ClassConstraint.second);
  }

  return Result;
}

//===----------------------------------------------------------------------===//
//                     EqualityClass implementation details
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void EquivalenceClass::dumpToStream(ProgramStateRef State,
                                                     raw_ostream &os) const {
  SymbolSet ClassMembers = getClassMembers(State);
  for (const SymbolRef &MemberSym : ClassMembers) {
    MemberSym->dump();
    os << "\n";
  }
}

inline EquivalenceClass EquivalenceClass::find(ProgramStateRef State,
                                               SymbolRef Sym) {
  assert(State && "State should not be null");
  assert(Sym && "Symbol should not be null");
  // We store far from all Symbol -> Class mappings
  if (const EquivalenceClass *NontrivialClass = State->get<ClassMap>(Sym))
    return *NontrivialClass;

  // This is a trivial class of Sym.
  return Sym;
}

inline ProgramStateRef EquivalenceClass::merge(RangeSet::Factory &F,
                                               ProgramStateRef State,
                                               SymbolRef First,
                                               SymbolRef Second) {
  EquivalenceClass FirstClass = find(State, First);
  EquivalenceClass SecondClass = find(State, Second);

  return FirstClass.merge(F, State, SecondClass);
}

inline ProgramStateRef EquivalenceClass::merge(RangeSet::Factory &F,
                                               ProgramStateRef State,
                                               EquivalenceClass Other) {
  // It is already the same class.
  if (*this == Other)
    return State;

  // FIXME: As of now, we support only equivalence classes of the same type.
  //        This limitation is connected to the lack of explicit casts in
  //        our symbolic expression model.
  //
  //        That means that for `int x` and `char y` we don't distinguish
  //        between these two very different cases:
  //          * `x == y`
  //          * `(char)x == y`
  //
  //        The moment we introduce symbolic casts, this restriction can be
  //        lifted.
  if (getType()->getCanonicalTypeUnqualified() !=
      Other.getType()->getCanonicalTypeUnqualified())
    return State;

  SymbolSet Members = getClassMembers(State);
  SymbolSet OtherMembers = Other.getClassMembers(State);

  // We estimate the size of the class by the height of tree containing
  // its members.  Merging is not a trivial operation, so it's easier to
  // merge the smaller class into the bigger one.
  if (Members.getHeight() >= OtherMembers.getHeight()) {
    return mergeImpl(F, State, Members, Other, OtherMembers);
  } else {
    return Other.mergeImpl(F, State, OtherMembers, *this, Members);
  }
}

inline ProgramStateRef
EquivalenceClass::mergeImpl(RangeSet::Factory &RangeFactory,
                            ProgramStateRef State, SymbolSet MyMembers,
                            EquivalenceClass Other, SymbolSet OtherMembers) {
  // Essentially what we try to recreate here is some kind of union-find
  // data structure.  It does have certain limitations due to persistence
  // and the need to remove elements from classes.
  //
  // In this setting, EquialityClass object is the representative of the class
  // or the parent element.  ClassMap is a mapping of class members to their
  // parent. Unlike the union-find structure, they all point directly to the
  // class representative because we don't have an opportunity to actually do
  // path compression when dealing with immutability.  This means that we
  // compress paths every time we do merges.  It also means that we lose
  // the main amortized complexity benefit from the original data structure.
  ConstraintRangeTy Constraints = State->get<ConstraintRange>();
  ConstraintRangeTy::Factory &CRF = State->get_context<ConstraintRange>();

  // 1. If the merged classes have any constraints associated with them, we
  //    need to transfer them to the class we have left.
  //
  // Intersection here makes perfect sense because both of these constraints
  // must hold for the whole new class.
  if (std::optional<RangeSet> NewClassConstraint =
          intersect(RangeFactory, getConstraint(State, *this),
                    getConstraint(State, Other))) {
    // NOTE: Essentially, NewClassConstraint should NEVER be infeasible because
    //       range inferrer shouldn't generate ranges incompatible with
    //       equivalence classes. However, at the moment, due to imperfections
    //       in the solver, it is possible and the merge function can also
    //       return infeasible states aka null states.
    if (NewClassConstraint->isEmpty())
      // Infeasible state
      return nullptr;

    // No need in tracking constraints of a now-dissolved class.
    Constraints = CRF.remove(Constraints, Other);
    // Assign new constraints for this class.
    Constraints = CRF.add(Constraints, *this, *NewClassConstraint);

    assert(areFeasible(Constraints) && "Constraint manager shouldn't produce "
                                       "a state with infeasible constraints");

    State = State->set<ConstraintRange>(Constraints);
  }

  // 2. Get ALL equivalence-related maps
  ClassMapTy Classes = State->get<ClassMap>();
  ClassMapTy::Factory &CMF = State->get_context<ClassMap>();

  ClassMembersTy Members = State->get<ClassMembers>();
  ClassMembersTy::Factory &MF = State->get_context<ClassMembers>();

  DisequalityMapTy DisequalityInfo = State->get<DisequalityMap>();
  DisequalityMapTy::Factory &DF = State->get_context<DisequalityMap>();

  ClassSet::Factory &CF = State->get_context<ClassSet>();
  SymbolSet::Factory &F = getMembersFactory(State);

  // 2. Merge members of the Other class into the current class.
  SymbolSet NewClassMembers = MyMembers;
  for (SymbolRef Sym : OtherMembers) {
    NewClassMembers = F.add(NewClassMembers, Sym);
    // *this is now the class for all these new symbols.
    Classes = CMF.add(Classes, Sym, *this);
  }

  // 3. Adjust member mapping.
  //
  // No need in tracking members of a now-dissolved class.
  Members = MF.remove(Members, Other);
  // Now only the current class is mapped to all the symbols.
  Members = MF.add(Members, *this, NewClassMembers);

  // 4. Update disequality relations
  ClassSet DisequalToOther = Other.getDisequalClasses(DisequalityInfo, CF);
  // We are about to merge two classes but they are already known to be
  // non-equal. This is a contradiction.
  if (DisequalToOther.contains(*this))
    return nullptr;

  if (!DisequalToOther.isEmpty()) {
    ClassSet DisequalToThis = getDisequalClasses(DisequalityInfo, CF);
    DisequalityInfo = DF.remove(DisequalityInfo, Other);

    for (EquivalenceClass DisequalClass : DisequalToOther) {
      DisequalToThis = CF.add(DisequalToThis, DisequalClass);

      // Disequality is a symmetric relation meaning that if
      // DisequalToOther not null then the set for DisequalClass is not
      // empty and has at least Other.
      ClassSet OriginalSetLinkedToOther =
          *DisequalityInfo.lookup(DisequalClass);

      // Other will be eliminated and we should replace it with the bigger
      // united class.
      ClassSet NewSet = CF.remove(OriginalSetLinkedToOther, Other);
      NewSet = CF.add(NewSet, *this);

      DisequalityInfo = DF.add(DisequalityInfo, DisequalClass, NewSet);
    }

    DisequalityInfo = DF.add(DisequalityInfo, *this, DisequalToThis);
    State = State->set<DisequalityMap>(DisequalityInfo);
  }

  // 5. Update the state
  State = State->set<ClassMap>(Classes);
  State = State->set<ClassMembers>(Members);

  return State;
}

inline SymbolSet::Factory &
EquivalenceClass::getMembersFactory(ProgramStateRef State) {
  return State->get_context<SymbolSet>();
}

SymbolSet EquivalenceClass::getClassMembers(ProgramStateRef State) const {
  if (const SymbolSet *Members = State->get<ClassMembers>(*this))
    return *Members;

  // This class is trivial, so we need to construct a set
  // with just that one symbol from the class.
  SymbolSet::Factory &F = getMembersFactory(State);
  return F.add(F.getEmptySet(), getRepresentativeSymbol());
}

bool EquivalenceClass::isTrivial(ProgramStateRef State) const {
  return State->get<ClassMembers>(*this) == nullptr;
}

bool EquivalenceClass::isTriviallyDead(ProgramStateRef State,
                                       SymbolReaper &Reaper) const {
  return isTrivial(State) && Reaper.isDead(getRepresentativeSymbol());
}

inline ProgramStateRef EquivalenceClass::markDisequal(RangeSet::Factory &RF,
                                                      ProgramStateRef State,
                                                      SymbolRef First,
                                                      SymbolRef Second) {
  return markDisequal(RF, State, find(State, First), find(State, Second));
}

inline ProgramStateRef EquivalenceClass::markDisequal(RangeSet::Factory &RF,
                                                      ProgramStateRef State,
                                                      EquivalenceClass First,
                                                      EquivalenceClass Second) {
  return First.markDisequal(RF, State, Second);
}

inline ProgramStateRef
EquivalenceClass::markDisequal(RangeSet::Factory &RF, ProgramStateRef State,
                               EquivalenceClass Other) const {
  // If we know that two classes are equal, we can only produce an infeasible
  // state.
  if (*this == Other) {
    return nullptr;
  }

  DisequalityMapTy DisequalityInfo = State->get<DisequalityMap>();
  ConstraintRangeTy Constraints = State->get<ConstraintRange>();

  // Disequality is a symmetric relation, so if we mark A as disequal to B,
  // we should also mark B as disequalt to A.
  if (!addToDisequalityInfo(DisequalityInfo, Constraints, RF, State, *this,
                            Other) ||
      !addToDisequalityInfo(DisequalityInfo, Constraints, RF, State, Other,
                            *this))
    return nullptr;

  assert(areFeasible(Constraints) && "Constraint manager shouldn't produce "
                                     "a state with infeasible constraints");

  State = State->set<DisequalityMap>(DisequalityInfo);
  State = State->set<ConstraintRange>(Constraints);

  return State;
}

inline bool EquivalenceClass::addToDisequalityInfo(
    DisequalityMapTy &Info, ConstraintRangeTy &Constraints,
    RangeSet::Factory &RF, ProgramStateRef State, EquivalenceClass First,
    EquivalenceClass Second) {

  // 1. Get all of the required factories.
  DisequalityMapTy::Factory &F = State->get_context<DisequalityMap>();
  ClassSet::Factory &CF = State->get_context<ClassSet>();
  ConstraintRangeTy::Factory &CRF = State->get_context<ConstraintRange>();

  // 2. Add Second to the set of classes disequal to First.
  const ClassSet *CurrentSet = Info.lookup(First);
  ClassSet NewSet = CurrentSet ? *CurrentSet : CF.getEmptySet();
  NewSet = CF.add(NewSet, Second);

  Info = F.add(Info, First, NewSet);

  // 3. If Second is known to be a constant, we can delete this point
  //    from the constraint asociated with First.
  //
  //    So, if Second == 10, it means that First != 10.
  //    At the same time, the same logic does not apply to ranges.
  if (const RangeSet *SecondConstraint = Constraints.lookup(Second))
    if (const llvm::APSInt *Point = SecondConstraint->getConcreteValue()) {

      RangeSet FirstConstraint = SymbolicRangeInferrer::inferRange(
          RF, State, First.getRepresentativeSymbol());

      FirstConstraint = RF.deletePoint(FirstConstraint, *Point);

      // If the First class is about to be constrained with an empty
      // range-set, the state is infeasible.
      if (FirstConstraint.isEmpty())
        return false;

      Constraints = CRF.add(Constraints, First, FirstConstraint);
    }

  return true;
}

inline std::optional<bool> EquivalenceClass::areEqual(ProgramStateRef State,
                                                      SymbolRef FirstSym,
                                                      SymbolRef SecondSym) {
  return EquivalenceClass::areEqual(State, find(State, FirstSym),
                                    find(State, SecondSym));
}

inline std::optional<bool> EquivalenceClass::areEqual(ProgramStateRef State,
                                                      EquivalenceClass First,
                                                      EquivalenceClass Second) {
  // The same equivalence class => symbols are equal.
  if (First == Second)
    return true;

  // Let's check if we know anything about these two classes being not equal to
  // each other.
  ClassSet DisequalToFirst = First.getDisequalClasses(State);
  if (DisequalToFirst.contains(Second))
    return false;

  // It is not clear.
  return std::nullopt;
}

[[nodiscard]] ProgramStateRef
EquivalenceClass::removeMember(ProgramStateRef State, const SymbolRef Old) {

  SymbolSet ClsMembers = getClassMembers(State);
  assert(ClsMembers.contains(Old));

  // Remove `Old`'s Class->Sym relation.
  SymbolSet::Factory &F = getMembersFactory(State);
  ClassMembersTy::Factory &EMFactory = State->get_context<ClassMembers>();
  ClsMembers = F.remove(ClsMembers, Old);
  // Ensure another precondition of the removeMember function (we can check
  // this only with isEmpty, thus we have to do the remove first).
  assert(!ClsMembers.isEmpty() &&
         "Class should have had at least two members before member removal");
  // Overwrite the existing members assigned to this class.
  ClassMembersTy ClassMembersMap = State->get<ClassMembers>();
  ClassMembersMap = EMFactory.add(ClassMembersMap, *this, ClsMembers);
  State = State->set<ClassMembers>(ClassMembersMap);

  // Remove `Old`'s Sym->Class relation.
  ClassMapTy Classes = State->get<ClassMap>();
  ClassMapTy::Factory &CMF = State->get_context<ClassMap>();
  Classes = CMF.remove(Classes, Old);
  State = State->set<ClassMap>(Classes);

  return State;
}

// Re-evaluate an SVal with top-level `State->assume` logic.
[[nodiscard]] ProgramStateRef
reAssume(ProgramStateRef State, const RangeSet *Constraint, SVal TheValue) {
  if (!Constraint)
    return State;

  const auto DefinedVal = TheValue.castAs<DefinedSVal>();

  // If the SVal is 0, we can simply interpret that as `false`.
  if (Constraint->encodesFalseRange())
    return State->assume(DefinedVal, false);

  // If the constraint does not encode 0 then we can interpret that as `true`
  // AND as a Range(Set).
  if (Constraint->encodesTrueRange()) {
    State = State->assume(DefinedVal, true);
    if (!State)
      return nullptr;
    // Fall through, re-assume based on the range values as well.
  }
  // Overestimate the individual Ranges with the RangeSet' lowest and
  // highest values.
  return State->assumeInclusiveRange(DefinedVal, Constraint->getMinValue(),
                                     Constraint->getMaxValue(), true);
}

// Iterate over all symbols and try to simplify them. Once a symbol is
// simplified then we check if we can merge the simplified symbol's equivalence
// class to this class. This way, we simplify not just the symbols but the
// classes as well: we strive to keep the number of the classes to be the
// absolute minimum.
[[nodiscard]] ProgramStateRef
EquivalenceClass::simplify(SValBuilder &SVB, RangeSet::Factory &F,
                           ProgramStateRef State, EquivalenceClass Class) {
  SymbolSet ClassMembers = Class.getClassMembers(State);
  for (const SymbolRef &MemberSym : ClassMembers) {

    const SVal SimplifiedMemberVal = simplifyToSVal(State, MemberSym);
    const SymbolRef SimplifiedMemberSym = SimplifiedMemberVal.getAsSymbol();

    // The symbol is collapsed to a constant, check if the current State is
    // still feasible.
    if (const auto CI = SimplifiedMemberVal.getAs<nonloc::ConcreteInt>()) {
      const llvm::APSInt &SV = CI->getValue();
      const RangeSet *ClassConstraint = getConstraint(State, Class);
      // We have found a contradiction.
      if (ClassConstraint && !ClassConstraint->contains(SV))
        return nullptr;
    }

    if (SimplifiedMemberSym && MemberSym != SimplifiedMemberSym) {
      // The simplified symbol should be the member of the original Class,
      // however, it might be in another existing class at the moment. We
      // have to merge these classes.
      ProgramStateRef OldState = State;
      State = merge(F, State, MemberSym, SimplifiedMemberSym);
      if (!State)
        return nullptr;
      // No state change, no merge happened actually.
      if (OldState == State)
        continue;

      // Be aware that `SimplifiedMemberSym` might refer to an already dead
      // symbol. In that case, the eqclass of that might not be the same as the
      // eqclass of `MemberSym`. This is because the dead symbols are not
      // preserved in the `ClassMap`, hence
      // `find(State, SimplifiedMemberSym)` will result in a trivial eqclass
      // compared to the eqclass of `MemberSym`.
      // These eqclasses should be the same if `SimplifiedMemberSym` is alive.
      // --> assert(find(State, MemberSym) == find(State, SimplifiedMemberSym))
      //
      // Note that `MemberSym` must be alive here since that is from the
      // `ClassMembers` where all the symbols are alive.

      // Remove the old and more complex symbol.
      State = find(State, MemberSym).removeMember(State, MemberSym);

      // Query the class constraint again b/c that may have changed during the
      // merge above.
      const RangeSet *ClassConstraint = getConstraint(State, Class);

      // Re-evaluate an SVal with top-level `State->assume`, this ignites
      // a RECURSIVE algorithm that will reach a FIXPOINT.
      //
      // About performance and complexity: Let us assume that in a State we
      // have N non-trivial equivalence classes and that all constraints and
      // disequality info is related to non-trivial classes. In the worst case,
      // we can simplify only one symbol of one class in each iteration. The
      // number of symbols in one class cannot grow b/c we replace the old
      // symbol with the simplified one. Also, the number of the equivalence
      // classes can decrease only, b/c the algorithm does a merge operation
      // optionally. We need N iterations in this case to reach the fixpoint.
      // Thus, the steps needed to be done in the worst case is proportional to
      // N*N.
      //
      // This worst case scenario can be extended to that case when we have
      // trivial classes in the constraints and in the disequality map. This
      // case can be reduced to the case with a State where there are only
      // non-trivial classes. This is because a merge operation on two trivial
      // classes results in one non-trivial class.
      State = reAssume(State, ClassConstraint, SimplifiedMemberVal);
      if (!State)
        return nullptr;
    }
  }
  return State;
}

inline ClassSet EquivalenceClass::getDisequalClasses(ProgramStateRef State,
                                                     SymbolRef Sym) {
  return find(State, Sym).getDisequalClasses(State);
}

inline ClassSet
EquivalenceClass::getDisequalClasses(ProgramStateRef State) const {
  return getDisequalClasses(State->get<DisequalityMap>(),
                            State->get_context<ClassSet>());
}

inline ClassSet
EquivalenceClass::getDisequalClasses(DisequalityMapTy Map,
                                     ClassSet::Factory &Factory) const {
  if (const ClassSet *DisequalClasses = Map.lookup(*this))
    return *DisequalClasses;

  return Factory.getEmptySet();
}

bool EquivalenceClass::isClassDataConsistent(ProgramStateRef State) {
  ClassMembersTy Members = State->get<ClassMembers>();

  for (std::pair<EquivalenceClass, SymbolSet> ClassMembersPair : Members) {
    for (SymbolRef Member : ClassMembersPair.second) {
      // Every member of the class should have a mapping back to the class.
      if (find(State, Member) == ClassMembersPair.first) {
        continue;
      }

      return false;
    }
  }

  DisequalityMapTy Disequalities = State->get<DisequalityMap>();
  for (std::pair<EquivalenceClass, ClassSet> DisequalityInfo : Disequalities) {
    EquivalenceClass Class = DisequalityInfo.first;
    ClassSet DisequalClasses = DisequalityInfo.second;

    // There is no use in keeping empty sets in the map.
    if (DisequalClasses.isEmpty())
      return false;

    // Disequality is symmetrical, i.e. for every Class A and B that A != B,
    // B != A should also be true.
    for (EquivalenceClass DisequalClass : DisequalClasses) {
      const ClassSet *DisequalToDisequalClasses =
          Disequalities.lookup(DisequalClass);

      // It should be a set of at least one element: Class
      if (!DisequalToDisequalClasses ||
          !DisequalToDisequalClasses->contains(Class))
        return false;
    }
  }

  return true;
}

//===----------------------------------------------------------------------===//
//                    RangeConstraintManager implementation
//===----------------------------------------------------------------------===//

bool RangeConstraintManager::canReasonAbout(SVal X) const {
  std::optional<nonloc::SymbolVal> SymVal = X.getAs<nonloc::SymbolVal>();
  if (SymVal && SymVal->isExpression()) {
    const SymExpr *SE = SymVal->getSymbol();

    if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(SE)) {
      switch (SIE->getOpcode()) {
      // We don't reason yet about bitwise-constraints on symbolic values.
      case BO_And:
      case BO_Or:
      case BO_Xor:
        return false;
      // We don't reason yet about these arithmetic constraints on
      // symbolic values.
      case BO_Mul:
      case BO_Div:
      case BO_Rem:
      case BO_Shl:
      case BO_Shr:
        return false;
      // All other cases.
      default:
        return true;
      }
    }

    if (const SymSymExpr *SSE = dyn_cast<SymSymExpr>(SE)) {
      // FIXME: Handle <=> here.
      if (BinaryOperator::isEqualityOp(SSE->getOpcode()) ||
          BinaryOperator::isRelationalOp(SSE->getOpcode())) {
        // We handle Loc <> Loc comparisons, but not (yet) NonLoc <> NonLoc.
        // We've recently started producing Loc <> NonLoc comparisons (that
        // result from casts of one of the operands between eg. intptr_t and
        // void *), but we can't reason about them yet.
        if (Loc::isLocType(SSE->getLHS()->getType())) {
          return Loc::isLocType(SSE->getRHS()->getType());
        }
      }
    }

    return false;
  }

  return true;
}

ConditionTruthVal RangeConstraintManager::checkNull(ProgramStateRef State,
                                                    SymbolRef Sym) {
  const RangeSet *Ranges = getConstraint(State, Sym);

  // If we don't have any information about this symbol, it's underconstrained.
  if (!Ranges)
    return ConditionTruthVal();

  // If we have a concrete value, see if it's zero.
  if (const llvm::APSInt *Value = Ranges->getConcreteValue())
    return *Value == 0;

  BasicValueFactory &BV = getBasicVals();
  APSIntType IntType = BV.getAPSIntType(Sym->getType());
  llvm::APSInt Zero = IntType.getZeroValue();

  // Check if zero is in the set of possible values.
  if (!Ranges->contains(Zero))
    return false;

  // Zero is a possible value, but it is not the /only/ possible value.
  return ConditionTruthVal();
}

const llvm::APSInt *RangeConstraintManager::getSymVal(ProgramStateRef St,
                                                      SymbolRef Sym) const {
  const RangeSet *T = getConstraint(St, Sym);
  return T ? T->getConcreteValue() : nullptr;
}

const llvm::APSInt *RangeConstraintManager::getSymMinVal(ProgramStateRef St,
                                                         SymbolRef Sym) const {
  const RangeSet *T = getConstraint(St, Sym);
  if (!T || T->isEmpty())
    return nullptr;
  return &T->getMinValue();
}

const llvm::APSInt *RangeConstraintManager::getSymMaxVal(ProgramStateRef St,
                                                         SymbolRef Sym) const {
  const RangeSet *T = getConstraint(St, Sym);
  if (!T || T->isEmpty())
    return nullptr;
  return &T->getMaxValue();
}

//===----------------------------------------------------------------------===//
//                Remove dead symbols from existing constraints
//===----------------------------------------------------------------------===//

/// Scan all symbols referenced by the constraints. If the symbol is not alive
/// as marked in LSymbols, mark it as dead in DSymbols.
ProgramStateRef
RangeConstraintManager::removeDeadBindings(ProgramStateRef State,
                                           SymbolReaper &SymReaper) {
  ClassMembersTy ClassMembersMap = State->get<ClassMembers>();
  ClassMembersTy NewClassMembersMap = ClassMembersMap;
  ClassMembersTy::Factory &EMFactory = State->get_context<ClassMembers>();
  SymbolSet::Factory &SetFactory = State->get_context<SymbolSet>();

  ConstraintRangeTy Constraints = State->get<ConstraintRange>();
  ConstraintRangeTy NewConstraints = Constraints;
  ConstraintRangeTy::Factory &ConstraintFactory =
      State->get_context<ConstraintRange>();

  ClassMapTy Map = State->get<ClassMap>();
  ClassMapTy NewMap = Map;
  ClassMapTy::Factory &ClassFactory = State->get_context<ClassMap>();

  DisequalityMapTy Disequalities = State->get<DisequalityMap>();
  DisequalityMapTy::Factory &DisequalityFactory =
      State->get_context<DisequalityMap>();
  ClassSet::Factory &ClassSetFactory = State->get_context<ClassSet>();

  bool ClassMapChanged = false;
  bool MembersMapChanged = false;
  bool ConstraintMapChanged = false;
  bool DisequalitiesChanged = false;

  auto removeDeadClass = [&](EquivalenceClass Class) {
    // Remove associated constraint ranges.
    Constraints = ConstraintFactory.remove(Constraints, Class);
    ConstraintMapChanged = true;

    // Update disequality information to not hold any information on the
    // removed class.
    ClassSet DisequalClasses =
        Class.getDisequalClasses(Disequalities, ClassSetFactory);
    if (!DisequalClasses.isEmpty()) {
      for (EquivalenceClass DisequalClass : DisequalClasses) {
        ClassSet DisequalToDisequalSet =
            DisequalClass.getDisequalClasses(Disequalities, ClassSetFactory);
        // DisequalToDisequalSet is guaranteed to be non-empty for consistent
        // disequality info.
        assert(!DisequalToDisequalSet.isEmpty());
        ClassSet NewSet = ClassSetFactory.remove(DisequalToDisequalSet, Class);

        // No need in keeping an empty set.
        if (NewSet.isEmpty()) {
          Disequalities =
              DisequalityFactory.remove(Disequalities, DisequalClass);
        } else {
          Disequalities =
              DisequalityFactory.add(Disequalities, DisequalClass, NewSet);
        }
      }
      // Remove the data for the class
      Disequalities = DisequalityFactory.remove(Disequalities, Class);
      DisequalitiesChanged = true;
    }
  };

  // 1. Let's see if dead symbols are trivial and have associated constraints.
  for (std::pair<EquivalenceClass, RangeSet> ClassConstraintPair :
       Constraints) {
    EquivalenceClass Class = ClassConstraintPair.first;
    if (Class.isTriviallyDead(State, SymReaper)) {
      // If this class is trivial, we can remove its constraints right away.
      removeDeadClass(Class);
    }
  }

  // 2. We don't need to track classes for dead symbols.
  for (std::pair<SymbolRef, EquivalenceClass> SymbolClassPair : Map) {
    SymbolRef Sym = SymbolClassPair.first;

    if (SymReaper.isDead(Sym)) {
      ClassMapChanged = true;
      NewMap = ClassFactory.remove(NewMap, Sym);
    }
  }

  // 3. Remove dead members from classes and remove dead non-trivial classes
  //    and their constraints.
  for (std::pair<EquivalenceClass, SymbolSet> ClassMembersPair :
       ClassMembersMap) {
    EquivalenceClass Class = ClassMembersPair.first;
    SymbolSet LiveMembers = ClassMembersPair.second;
    bool MembersChanged = false;

    for (SymbolRef Member : ClassMembersPair.second) {
      if (SymReaper.isDead(Member)) {
        MembersChanged = true;
        LiveMembers = SetFactory.remove(LiveMembers, Member);
      }
    }

    // Check if the class changed.
    if (!MembersChanged)
      continue;

    MembersMapChanged = true;

    if (LiveMembers.isEmpty()) {
      // The class is dead now, we need to wipe it out of the members map...
      NewClassMembersMap = EMFactory.remove(NewClassMembersMap, Class);

      // ...and remove all of its constraints.
      removeDeadClass(Class);
    } else {
      // We need to change the members associated with the class.
      NewClassMembersMap =
          EMFactory.add(NewClassMembersMap, Class, LiveMembers);
    }
  }

  // 4. Update the state with new maps.
  //
  // Here we try to be humble and update a map only if it really changed.
  if (ClassMapChanged)
    State = State->set<ClassMap>(NewMap);

  if (MembersMapChanged)
    State = State->set<ClassMembers>(NewClassMembersMap);

  if (ConstraintMapChanged)
    State = State->set<ConstraintRange>(Constraints);

  if (DisequalitiesChanged)
    State = State->set<DisequalityMap>(Disequalities);

  assert(EquivalenceClass::isClassDataConsistent(State));

  return State;
}

RangeSet RangeConstraintManager::getRange(ProgramStateRef State,
                                          SymbolRef Sym) {
  return SymbolicRangeInferrer::inferRange(F, State, Sym);
}

ProgramStateRef RangeConstraintManager::setRange(ProgramStateRef State,
                                                 SymbolRef Sym,
                                                 RangeSet Range) {
  return ConstraintAssignor::assign(State, getSValBuilder(), F, Sym, Range);
}

//===------------------------------------------------------------------------===
// assumeSymX methods: protected interface for RangeConstraintManager.
//===------------------------------------------------------------------------===

// The syntax for ranges below is mathematical, using [x, y] for closed ranges
// and (x, y) for open ranges. These ranges are modular, corresponding with
// a common treatment of C integer overflow. This means that these methods
// do not have to worry about overflow; RangeSet::Intersect can handle such a
// "wraparound" range.
// As an example, the range [UINT_MAX-1, 3) contains five values: UINT_MAX-1,
// UINT_MAX, 0, 1, and 2.

ProgramStateRef
RangeConstraintManager::assumeSymNE(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  if (AdjustmentType.testInRange(Int, true) != APSIntType::RTR_Within)
    return St;

  llvm::APSInt Point = AdjustmentType.convert(Int) - Adjustment;
  RangeSet New = getRange(St, Sym);
  New = F.deletePoint(New, Point);

  return setRange(St, Sym, New);
}

ProgramStateRef
RangeConstraintManager::assumeSymEQ(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  if (AdjustmentType.testInRange(Int, true) != APSIntType::RTR_Within)
    return nullptr;

  // [Int-Adjustment, Int-Adjustment]
  llvm::APSInt AdjInt = AdjustmentType.convert(Int) - Adjustment;
  RangeSet New = getRange(St, Sym);
  New = F.intersect(New, AdjInt);

  return setRange(St, Sym, New);
}

RangeSet RangeConstraintManager::getSymLTRange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return F.getEmptySet();
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return getRange(St, Sym);
  }

  // Special case for Int == Min. This is always false.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Min = AdjustmentType.getMinValue();
  if (ComparisonVal == Min)
    return F.getEmptySet();

  llvm::APSInt Lower = Min - Adjustment;
  llvm::APSInt Upper = ComparisonVal - Adjustment;
  --Upper;

  RangeSet Result = getRange(St, Sym);
  return F.intersect(Result, Lower, Upper);
}

ProgramStateRef
RangeConstraintManager::assumeSymLT(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymLTRange(St, Sym, Int, Adjustment);
  return setRange(St, Sym, New);
}

RangeSet RangeConstraintManager::getSymGTRange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return getRange(St, Sym);
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return F.getEmptySet();
  }

  // Special case for Int == Max. This is always false.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Max = AdjustmentType.getMaxValue();
  if (ComparisonVal == Max)
    return F.getEmptySet();

  llvm::APSInt Lower = ComparisonVal - Adjustment;
  llvm::APSInt Upper = Max - Adjustment;
  ++Lower;

  RangeSet SymRange = getRange(St, Sym);
  return F.intersect(SymRange, Lower, Upper);
}

ProgramStateRef
RangeConstraintManager::assumeSymGT(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymGTRange(St, Sym, Int, Adjustment);
  return setRange(St, Sym, New);
}

RangeSet RangeConstraintManager::getSymGERange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return getRange(St, Sym);
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return F.getEmptySet();
  }

  // Special case for Int == Min. This is always feasible.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Min = AdjustmentType.getMinValue();
  if (ComparisonVal == Min)
    return getRange(St, Sym);

  llvm::APSInt Max = AdjustmentType.getMaxValue();
  llvm::APSInt Lower = ComparisonVal - Adjustment;
  llvm::APSInt Upper = Max - Adjustment;

  RangeSet SymRange = getRange(St, Sym);
  return F.intersect(SymRange, Lower, Upper);
}

ProgramStateRef
RangeConstraintManager::assumeSymGE(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymGERange(St, Sym, Int, Adjustment);
  return setRange(St, Sym, New);
}

RangeSet
RangeConstraintManager::getSymLERange(llvm::function_ref<RangeSet()> RS,
                                      const llvm::APSInt &Int,
                                      const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return F.getEmptySet();
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return RS();
  }

  // Special case for Int == Max. This is always feasible.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Max = AdjustmentType.getMaxValue();
  if (ComparisonVal == Max)
    return RS();

  llvm::APSInt Min = AdjustmentType.getMinValue();
  llvm::APSInt Lower = Min - Adjustment;
  llvm::APSInt Upper = ComparisonVal - Adjustment;

  RangeSet Default = RS();
  return F.intersect(Default, Lower, Upper);
}

RangeSet RangeConstraintManager::getSymLERange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  return getSymLERange([&] { return getRange(St, Sym); }, Int, Adjustment);
}

ProgramStateRef
RangeConstraintManager::assumeSymLE(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymLERange(St, Sym, Int, Adjustment);
  return setRange(St, Sym, New);
}

ProgramStateRef RangeConstraintManager::assumeSymWithinInclusiveRange(
    ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
    const llvm::APSInt &To, const llvm::APSInt &Adjustment) {
  RangeSet New = getSymGERange(State, Sym, From, Adjustment);
  if (New.isEmpty())
    return nullptr;
  RangeSet Out = getSymLERange([&] { return New; }, To, Adjustment);
  return setRange(State, Sym, Out);
}

ProgramStateRef RangeConstraintManager::assumeSymOutsideInclusiveRange(
    ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
    const llvm::APSInt &To, const llvm::APSInt &Adjustment) {
  RangeSet RangeLT = getSymLTRange(State, Sym, From, Adjustment);
  RangeSet RangeGT = getSymGTRange(State, Sym, To, Adjustment);
  RangeSet New(F.add(RangeLT, RangeGT));
  return setRange(State, Sym, New);
}

//===----------------------------------------------------------------------===//
// Pretty-printing.
//===----------------------------------------------------------------------===//

void RangeConstraintManager::printJson(raw_ostream &Out, ProgramStateRef State,
                                       const char *NL, unsigned int Space,
                                       bool IsDot) const {
  printConstraints(Out, State, NL, Space, IsDot);
  printEquivalenceClasses(Out, State, NL, Space, IsDot);
  printDisequalities(Out, State, NL, Space, IsDot);
}

void RangeConstraintManager::printValue(raw_ostream &Out, ProgramStateRef State,
                                        SymbolRef Sym) {
  const RangeSet RS = getRange(State, Sym);
  if (RS.isEmpty()) {
    Out << "<empty rangeset>";
    return;
  }
  Out << RS.getBitWidth() << (RS.isUnsigned() ? "u:" : "s:");
  RS.dump(Out);
}

static std::string toString(const SymbolRef &Sym) {
  std::string S;
  llvm::raw_string_ostream O(S);
  Sym->dumpToStream(O);
  return S;
}

void RangeConstraintManager::printConstraints(raw_ostream &Out,
                                              ProgramStateRef State,
                                              const char *NL,
                                              unsigned int Space,
                                              bool IsDot) const {
  ConstraintRangeTy Constraints = State->get<ConstraintRange>();

  Indent(Out, Space, IsDot) << "\"constraints\": ";
  if (Constraints.isEmpty()) {
    Out << "null," << NL;
    return;
  }

  std::map<std::string, RangeSet> OrderedConstraints;
  for (std::pair<EquivalenceClass, RangeSet> P : Constraints) {
    SymbolSet ClassMembers = P.first.getClassMembers(State);
    for (const SymbolRef &ClassMember : ClassMembers) {
      bool insertion_took_place;
      std::tie(std::ignore, insertion_took_place) =
          OrderedConstraints.insert({toString(ClassMember), P.second});
      assert(insertion_took_place &&
             "two symbols should not have the same dump");
    }
  }

  ++Space;
  Out << '[' << NL;
  bool First = true;
  for (std::pair<std::string, RangeSet> P : OrderedConstraints) {
    if (First) {
      First = false;
    } else {
      Out << ',';
      Out << NL;
    }
    Indent(Out, Space, IsDot)
        << "{ \"symbol\": \"" << P.first << "\", \"range\": \"";
    P.second.dump(Out);
    Out << "\" }";
  }
  Out << NL;

  --Space;
  Indent(Out, Space, IsDot) << "]," << NL;
}

static std::string toString(ProgramStateRef State, EquivalenceClass Class) {
  SymbolSet ClassMembers = Class.getClassMembers(State);
  llvm::SmallVector<SymbolRef, 8> ClassMembersSorted(ClassMembers.begin(),
                                                     ClassMembers.end());
  llvm::sort(ClassMembersSorted,
             [](const SymbolRef &LHS, const SymbolRef &RHS) {
               return toString(LHS) < toString(RHS);
             });

  bool FirstMember = true;

  std::string Str;
  llvm::raw_string_ostream Out(Str);
  Out << "[ ";
  for (SymbolRef ClassMember : ClassMembersSorted) {
    if (FirstMember)
      FirstMember = false;
    else
      Out << ", ";
    Out << "\"" << ClassMember << "\"";
  }
  Out << " ]";
  return Str;
}

void RangeConstraintManager::printEquivalenceClasses(raw_ostream &Out,
                                                     ProgramStateRef State,
                                                     const char *NL,
                                                     unsigned int Space,
                                                     bool IsDot) const {
  ClassMembersTy Members = State->get<ClassMembers>();

  Indent(Out, Space, IsDot) << "\"equivalence_classes\": ";
  if (Members.isEmpty()) {
    Out << "null," << NL;
    return;
  }

  std::set<std::string> MembersStr;
  for (std::pair<EquivalenceClass, SymbolSet> ClassToSymbolSet : Members)
    MembersStr.insert(toString(State, ClassToSymbolSet.first));

  ++Space;
  Out << '[' << NL;
  bool FirstClass = true;
  for (const std::string &Str : MembersStr) {
    if (FirstClass) {
      FirstClass = false;
    } else {
      Out << ',';
      Out << NL;
    }
    Indent(Out, Space, IsDot);
    Out << Str;
  }
  Out << NL;

  --Space;
  Indent(Out, Space, IsDot) << "]," << NL;
}

void RangeConstraintManager::printDisequalities(raw_ostream &Out,
                                                ProgramStateRef State,
                                                const char *NL,
                                                unsigned int Space,
                                                bool IsDot) const {
  DisequalityMapTy Disequalities = State->get<DisequalityMap>();

  Indent(Out, Space, IsDot) << "\"disequality_info\": ";
  if (Disequalities.isEmpty()) {
    Out << "null," << NL;
    return;
  }

  // Transform the disequality info to an ordered map of
  // [string -> (ordered set of strings)]
  using EqClassesStrTy = std::set<std::string>;
  using DisequalityInfoStrTy = std::map<std::string, EqClassesStrTy>;
  DisequalityInfoStrTy DisequalityInfoStr;
  for (std::pair<EquivalenceClass, ClassSet> ClassToDisEqSet : Disequalities) {
    EquivalenceClass Class = ClassToDisEqSet.first;
    ClassSet DisequalClasses = ClassToDisEqSet.second;
    EqClassesStrTy MembersStr;
    for (EquivalenceClass DisEqClass : DisequalClasses)
      MembersStr.insert(toString(State, DisEqClass));
    DisequalityInfoStr.insert({toString(State, Class), MembersStr});
  }

  ++Space;
  Out << '[' << NL;
  bool FirstClass = true;
  for (std::pair<std::string, EqClassesStrTy> ClassToDisEqSet :
       DisequalityInfoStr) {
    const std::string &Class = ClassToDisEqSet.first;
    if (FirstClass) {
      FirstClass = false;
    } else {
      Out << ',';
      Out << NL;
    }
    Indent(Out, Space, IsDot) << "{" << NL;
    unsigned int DisEqSpace = Space + 1;
    Indent(Out, DisEqSpace, IsDot) << "\"class\": ";
    Out << Class;
    const EqClassesStrTy &DisequalClasses = ClassToDisEqSet.second;
    if (!DisequalClasses.empty()) {
      Out << "," << NL;
      Indent(Out, DisEqSpace, IsDot) << "\"disequal_to\": [" << NL;
      unsigned int DisEqClassSpace = DisEqSpace + 1;
      Indent(Out, DisEqClassSpace, IsDot);
      bool FirstDisEqClass = true;
      for (const std::string &DisEqClass : DisequalClasses) {
        if (FirstDisEqClass) {
          FirstDisEqClass = false;
        } else {
          Out << ',' << NL;
          Indent(Out, DisEqClassSpace, IsDot);
        }
        Out << DisEqClass;
      }
      Out << "]" << NL;
    }
    Indent(Out, Space, IsDot) << "}";
  }
  Out << NL;

  --Space;
  Indent(Out, Space, IsDot) << "]," << NL;
}
