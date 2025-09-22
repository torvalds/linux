//===- ConstantRange.cpp - ConstantRange implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represent a range of possible values that may occur when the program is run
// for an integral value.  This keeps track of a lower and upper bound for the
// constant, which MAY wrap around the end of the numeric range.  To do this, it
// keeps track of a [lower, upper) bound, which specifies an interval just like
// STL iterators.  When used with boolean values, the following are important
// ranges (other integral ranges use min/max values for special range values):
//
//  [F, F) = {}     = Empty set
//  [T, F) = {T}
//  [F, T) = {F}
//  [T, T) = {F, T} = Full set
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

using namespace llvm;

ConstantRange::ConstantRange(uint32_t BitWidth, bool Full)
    : Lower(Full ? APInt::getMaxValue(BitWidth) : APInt::getMinValue(BitWidth)),
      Upper(Lower) {}

ConstantRange::ConstantRange(APInt V)
    : Lower(std::move(V)), Upper(Lower + 1) {}

ConstantRange::ConstantRange(APInt L, APInt U)
    : Lower(std::move(L)), Upper(std::move(U)) {
  assert(Lower.getBitWidth() == Upper.getBitWidth() &&
         "ConstantRange with unequal bit widths");
  assert((Lower != Upper || (Lower.isMaxValue() || Lower.isMinValue())) &&
         "Lower == Upper, but they aren't min or max value!");
}

ConstantRange ConstantRange::fromKnownBits(const KnownBits &Known,
                                           bool IsSigned) {
  if (Known.hasConflict())
    return getEmpty(Known.getBitWidth());
  if (Known.isUnknown())
    return getFull(Known.getBitWidth());

  // For unsigned ranges, or signed ranges with known sign bit, create a simple
  // range between the smallest and largest possible value.
  if (!IsSigned || Known.isNegative() || Known.isNonNegative())
    return ConstantRange(Known.getMinValue(), Known.getMaxValue() + 1);

  // If we don't know the sign bit, pick the lower bound as a negative number
  // and the upper bound as a non-negative one.
  APInt Lower = Known.getMinValue(), Upper = Known.getMaxValue();
  Lower.setSignBit();
  Upper.clearSignBit();
  return ConstantRange(Lower, Upper + 1);
}

KnownBits ConstantRange::toKnownBits() const {
  // TODO: We could return conflicting known bits here, but consumers are
  // likely not prepared for that.
  if (isEmptySet())
    return KnownBits(getBitWidth());

  // We can only retain the top bits that are the same between min and max.
  APInt Min = getUnsignedMin();
  APInt Max = getUnsignedMax();
  KnownBits Known = KnownBits::makeConstant(Min);
  if (std::optional<unsigned> DifferentBit =
          APIntOps::GetMostSignificantDifferentBit(Min, Max)) {
    Known.Zero.clearLowBits(*DifferentBit + 1);
    Known.One.clearLowBits(*DifferentBit + 1);
  }
  return Known;
}

ConstantRange ConstantRange::makeAllowedICmpRegion(CmpInst::Predicate Pred,
                                                   const ConstantRange &CR) {
  if (CR.isEmptySet())
    return CR;

  uint32_t W = CR.getBitWidth();
  switch (Pred) {
  default:
    llvm_unreachable("Invalid ICmp predicate to makeAllowedICmpRegion()");
  case CmpInst::ICMP_EQ:
    return CR;
  case CmpInst::ICMP_NE:
    if (CR.isSingleElement())
      return ConstantRange(CR.getUpper(), CR.getLower());
    return getFull(W);
  case CmpInst::ICMP_ULT: {
    APInt UMax(CR.getUnsignedMax());
    if (UMax.isMinValue())
      return getEmpty(W);
    return ConstantRange(APInt::getMinValue(W), std::move(UMax));
  }
  case CmpInst::ICMP_SLT: {
    APInt SMax(CR.getSignedMax());
    if (SMax.isMinSignedValue())
      return getEmpty(W);
    return ConstantRange(APInt::getSignedMinValue(W), std::move(SMax));
  }
  case CmpInst::ICMP_ULE:
    return getNonEmpty(APInt::getMinValue(W), CR.getUnsignedMax() + 1);
  case CmpInst::ICMP_SLE:
    return getNonEmpty(APInt::getSignedMinValue(W), CR.getSignedMax() + 1);
  case CmpInst::ICMP_UGT: {
    APInt UMin(CR.getUnsignedMin());
    if (UMin.isMaxValue())
      return getEmpty(W);
    return ConstantRange(std::move(UMin) + 1, APInt::getZero(W));
  }
  case CmpInst::ICMP_SGT: {
    APInt SMin(CR.getSignedMin());
    if (SMin.isMaxSignedValue())
      return getEmpty(W);
    return ConstantRange(std::move(SMin) + 1, APInt::getSignedMinValue(W));
  }
  case CmpInst::ICMP_UGE:
    return getNonEmpty(CR.getUnsignedMin(), APInt::getZero(W));
  case CmpInst::ICMP_SGE:
    return getNonEmpty(CR.getSignedMin(), APInt::getSignedMinValue(W));
  }
}

ConstantRange ConstantRange::makeSatisfyingICmpRegion(CmpInst::Predicate Pred,
                                                      const ConstantRange &CR) {
  // Follows from De-Morgan's laws:
  //
  // ~(~A union ~B) == A intersect B.
  //
  return makeAllowedICmpRegion(CmpInst::getInversePredicate(Pred), CR)
      .inverse();
}

ConstantRange ConstantRange::makeExactICmpRegion(CmpInst::Predicate Pred,
                                                 const APInt &C) {
  // Computes the exact range that is equal to both the constant ranges returned
  // by makeAllowedICmpRegion and makeSatisfyingICmpRegion. This is always true
  // when RHS is a singleton such as an APInt and so the assert is valid.
  // However for non-singleton RHS, for example ult [2,5) makeAllowedICmpRegion
  // returns [0,4) but makeSatisfyICmpRegion returns [0,2).
  //
  assert(makeAllowedICmpRegion(Pred, C) == makeSatisfyingICmpRegion(Pred, C));
  return makeAllowedICmpRegion(Pred, C);
}

bool ConstantRange::areInsensitiveToSignednessOfICmpPredicate(
    const ConstantRange &CR1, const ConstantRange &CR2) {
  if (CR1.isEmptySet() || CR2.isEmptySet())
    return true;

  return (CR1.isAllNonNegative() && CR2.isAllNonNegative()) ||
         (CR1.isAllNegative() && CR2.isAllNegative());
}

bool ConstantRange::areInsensitiveToSignednessOfInvertedICmpPredicate(
    const ConstantRange &CR1, const ConstantRange &CR2) {
  if (CR1.isEmptySet() || CR2.isEmptySet())
    return true;

  return (CR1.isAllNonNegative() && CR2.isAllNegative()) ||
         (CR1.isAllNegative() && CR2.isAllNonNegative());
}

CmpInst::Predicate ConstantRange::getEquivalentPredWithFlippedSignedness(
    CmpInst::Predicate Pred, const ConstantRange &CR1,
    const ConstantRange &CR2) {
  assert(CmpInst::isIntPredicate(Pred) && CmpInst::isRelational(Pred) &&
         "Only for relational integer predicates!");

  CmpInst::Predicate FlippedSignednessPred =
      CmpInst::getFlippedSignednessPredicate(Pred);

  if (areInsensitiveToSignednessOfICmpPredicate(CR1, CR2))
    return FlippedSignednessPred;

  if (areInsensitiveToSignednessOfInvertedICmpPredicate(CR1, CR2))
    return CmpInst::getInversePredicate(FlippedSignednessPred);

  return CmpInst::Predicate::BAD_ICMP_PREDICATE;
}

void ConstantRange::getEquivalentICmp(CmpInst::Predicate &Pred,
                                      APInt &RHS, APInt &Offset) const {
  Offset = APInt(getBitWidth(), 0);
  if (isFullSet() || isEmptySet()) {
    Pred = isEmptySet() ? CmpInst::ICMP_ULT : CmpInst::ICMP_UGE;
    RHS = APInt(getBitWidth(), 0);
  } else if (auto *OnlyElt = getSingleElement()) {
    Pred = CmpInst::ICMP_EQ;
    RHS = *OnlyElt;
  } else if (auto *OnlyMissingElt = getSingleMissingElement()) {
    Pred = CmpInst::ICMP_NE;
    RHS = *OnlyMissingElt;
  } else if (getLower().isMinSignedValue() || getLower().isMinValue()) {
    Pred =
        getLower().isMinSignedValue() ? CmpInst::ICMP_SLT : CmpInst::ICMP_ULT;
    RHS = getUpper();
  } else if (getUpper().isMinSignedValue() || getUpper().isMinValue()) {
    Pred =
        getUpper().isMinSignedValue() ? CmpInst::ICMP_SGE : CmpInst::ICMP_UGE;
    RHS = getLower();
  } else {
    Pred = CmpInst::ICMP_ULT;
    RHS = getUpper() - getLower();
    Offset = -getLower();
  }

  assert(ConstantRange::makeExactICmpRegion(Pred, RHS) == add(Offset) &&
         "Bad result!");
}

bool ConstantRange::getEquivalentICmp(CmpInst::Predicate &Pred,
                                      APInt &RHS) const {
  APInt Offset;
  getEquivalentICmp(Pred, RHS, Offset);
  return Offset.isZero();
}

bool ConstantRange::icmp(CmpInst::Predicate Pred,
                         const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return true;

  switch (Pred) {
  case CmpInst::ICMP_EQ:
    if (const APInt *L = getSingleElement())
      if (const APInt *R = Other.getSingleElement())
        return *L == *R;
    return false;
  case CmpInst::ICMP_NE:
    return inverse().contains(Other);
  case CmpInst::ICMP_ULT:
    return getUnsignedMax().ult(Other.getUnsignedMin());
  case CmpInst::ICMP_ULE:
    return getUnsignedMax().ule(Other.getUnsignedMin());
  case CmpInst::ICMP_UGT:
    return getUnsignedMin().ugt(Other.getUnsignedMax());
  case CmpInst::ICMP_UGE:
    return getUnsignedMin().uge(Other.getUnsignedMax());
  case CmpInst::ICMP_SLT:
    return getSignedMax().slt(Other.getSignedMin());
  case CmpInst::ICMP_SLE:
    return getSignedMax().sle(Other.getSignedMin());
  case CmpInst::ICMP_SGT:
    return getSignedMin().sgt(Other.getSignedMax());
  case CmpInst::ICMP_SGE:
    return getSignedMin().sge(Other.getSignedMax());
  default:
    llvm_unreachable("Invalid ICmp predicate");
  }
}

/// Exact mul nuw region for single element RHS.
static ConstantRange makeExactMulNUWRegion(const APInt &V) {
  unsigned BitWidth = V.getBitWidth();
  if (V == 0)
    return ConstantRange::getFull(V.getBitWidth());

  return ConstantRange::getNonEmpty(
      APIntOps::RoundingUDiv(APInt::getMinValue(BitWidth), V,
                             APInt::Rounding::UP),
      APIntOps::RoundingUDiv(APInt::getMaxValue(BitWidth), V,
                             APInt::Rounding::DOWN) + 1);
}

/// Exact mul nsw region for single element RHS.
static ConstantRange makeExactMulNSWRegion(const APInt &V) {
  // Handle 0 and -1 separately to avoid division by zero or overflow.
  unsigned BitWidth = V.getBitWidth();
  if (V == 0)
    return ConstantRange::getFull(BitWidth);

  APInt MinValue = APInt::getSignedMinValue(BitWidth);
  APInt MaxValue = APInt::getSignedMaxValue(BitWidth);
  // e.g. Returning [-127, 127], represented as [-127, -128).
  if (V.isAllOnes())
    return ConstantRange(-MaxValue, MinValue);

  APInt Lower, Upper;
  if (V.isNegative()) {
    Lower = APIntOps::RoundingSDiv(MaxValue, V, APInt::Rounding::UP);
    Upper = APIntOps::RoundingSDiv(MinValue, V, APInt::Rounding::DOWN);
  } else {
    Lower = APIntOps::RoundingSDiv(MinValue, V, APInt::Rounding::UP);
    Upper = APIntOps::RoundingSDiv(MaxValue, V, APInt::Rounding::DOWN);
  }
  return ConstantRange::getNonEmpty(Lower, Upper + 1);
}

ConstantRange
ConstantRange::makeGuaranteedNoWrapRegion(Instruction::BinaryOps BinOp,
                                          const ConstantRange &Other,
                                          unsigned NoWrapKind) {
  using OBO = OverflowingBinaryOperator;

  assert(Instruction::isBinaryOp(BinOp) && "Binary operators only!");

  assert((NoWrapKind == OBO::NoSignedWrap ||
          NoWrapKind == OBO::NoUnsignedWrap) &&
         "NoWrapKind invalid!");

  bool Unsigned = NoWrapKind == OBO::NoUnsignedWrap;
  unsigned BitWidth = Other.getBitWidth();

  switch (BinOp) {
  default:
    llvm_unreachable("Unsupported binary op");

  case Instruction::Add: {
    if (Unsigned)
      return getNonEmpty(APInt::getZero(BitWidth), -Other.getUnsignedMax());

    APInt SignedMinVal = APInt::getSignedMinValue(BitWidth);
    APInt SMin = Other.getSignedMin(), SMax = Other.getSignedMax();
    return getNonEmpty(
        SMin.isNegative() ? SignedMinVal - SMin : SignedMinVal,
        SMax.isStrictlyPositive() ? SignedMinVal - SMax : SignedMinVal);
  }

  case Instruction::Sub: {
    if (Unsigned)
      return getNonEmpty(Other.getUnsignedMax(), APInt::getMinValue(BitWidth));

    APInt SignedMinVal = APInt::getSignedMinValue(BitWidth);
    APInt SMin = Other.getSignedMin(), SMax = Other.getSignedMax();
    return getNonEmpty(
        SMax.isStrictlyPositive() ? SignedMinVal + SMax : SignedMinVal,
        SMin.isNegative() ? SignedMinVal + SMin : SignedMinVal);
  }

  case Instruction::Mul:
    if (Unsigned)
      return makeExactMulNUWRegion(Other.getUnsignedMax());

    // Avoid one makeExactMulNSWRegion() call for the common case of constants.
    if (const APInt *C = Other.getSingleElement())
      return makeExactMulNSWRegion(*C);

    return makeExactMulNSWRegion(Other.getSignedMin())
        .intersectWith(makeExactMulNSWRegion(Other.getSignedMax()));

  case Instruction::Shl: {
    // For given range of shift amounts, if we ignore all illegal shift amounts
    // (that always produce poison), what shift amount range is left?
    ConstantRange ShAmt = Other.intersectWith(
        ConstantRange(APInt(BitWidth, 0), APInt(BitWidth, (BitWidth - 1) + 1)));
    if (ShAmt.isEmptySet()) {
      // If the entire range of shift amounts is already poison-producing,
      // then we can freely add more poison-producing flags ontop of that.
      return getFull(BitWidth);
    }
    // There are some legal shift amounts, we can compute conservatively-correct
    // range of no-wrap inputs. Note that by now we have clamped the ShAmtUMax
    // to be at most bitwidth-1, which results in most conservative range.
    APInt ShAmtUMax = ShAmt.getUnsignedMax();
    if (Unsigned)
      return getNonEmpty(APInt::getZero(BitWidth),
                         APInt::getMaxValue(BitWidth).lshr(ShAmtUMax) + 1);
    return getNonEmpty(APInt::getSignedMinValue(BitWidth).ashr(ShAmtUMax),
                       APInt::getSignedMaxValue(BitWidth).ashr(ShAmtUMax) + 1);
  }
  }
}

ConstantRange ConstantRange::makeExactNoWrapRegion(Instruction::BinaryOps BinOp,
                                                   const APInt &Other,
                                                   unsigned NoWrapKind) {
  // makeGuaranteedNoWrapRegion() is exact for single-element ranges, as
  // "for all" and "for any" coincide in this case.
  return makeGuaranteedNoWrapRegion(BinOp, ConstantRange(Other), NoWrapKind);
}

ConstantRange ConstantRange::makeMaskNotEqualRange(const APInt &Mask,
                                                   const APInt &C) {
  unsigned BitWidth = Mask.getBitWidth();

  if ((Mask & C) != C)
    return getFull(BitWidth);

  if (Mask.isZero())
    return getEmpty(BitWidth);

  // If (Val & Mask) != C, constrained to the non-equality being
  // satisfiable, then the value must be larger than the lowest set bit of
  // Mask, offset by constant C.
  return ConstantRange::getNonEmpty(
      APInt::getOneBitSet(BitWidth, Mask.countr_zero()) + C, C);
}

bool ConstantRange::isFullSet() const {
  return Lower == Upper && Lower.isMaxValue();
}

bool ConstantRange::isEmptySet() const {
  return Lower == Upper && Lower.isMinValue();
}

bool ConstantRange::isWrappedSet() const {
  return Lower.ugt(Upper) && !Upper.isZero();
}

bool ConstantRange::isUpperWrapped() const {
  return Lower.ugt(Upper);
}

bool ConstantRange::isSignWrappedSet() const {
  return Lower.sgt(Upper) && !Upper.isMinSignedValue();
}

bool ConstantRange::isUpperSignWrapped() const {
  return Lower.sgt(Upper);
}

bool
ConstantRange::isSizeStrictlySmallerThan(const ConstantRange &Other) const {
  assert(getBitWidth() == Other.getBitWidth());
  if (isFullSet())
    return false;
  if (Other.isFullSet())
    return true;
  return (Upper - Lower).ult(Other.Upper - Other.Lower);
}

bool
ConstantRange::isSizeLargerThan(uint64_t MaxSize) const {
  // If this a full set, we need special handling to avoid needing an extra bit
  // to represent the size.
  if (isFullSet())
    return MaxSize == 0 || APInt::getMaxValue(getBitWidth()).ugt(MaxSize - 1);

  return (Upper - Lower).ugt(MaxSize);
}

bool ConstantRange::isAllNegative() const {
  // Empty set is all negative, full set is not.
  if (isEmptySet())
    return true;
  if (isFullSet())
    return false;

  return !isUpperSignWrapped() && !Upper.isStrictlyPositive();
}

bool ConstantRange::isAllNonNegative() const {
  // Empty and full set are automatically treated correctly.
  return !isSignWrappedSet() && Lower.isNonNegative();
}

bool ConstantRange::isAllPositive() const {
  // Empty set is all positive, full set is not.
  if (isEmptySet())
    return true;
  if (isFullSet())
    return false;

  return !isSignWrappedSet() && Lower.isStrictlyPositive();
}

APInt ConstantRange::getUnsignedMax() const {
  if (isFullSet() || isUpperWrapped())
    return APInt::getMaxValue(getBitWidth());
  return getUpper() - 1;
}

APInt ConstantRange::getUnsignedMin() const {
  if (isFullSet() || isWrappedSet())
    return APInt::getMinValue(getBitWidth());
  return getLower();
}

APInt ConstantRange::getSignedMax() const {
  if (isFullSet() || isUpperSignWrapped())
    return APInt::getSignedMaxValue(getBitWidth());
  return getUpper() - 1;
}

APInt ConstantRange::getSignedMin() const {
  if (isFullSet() || isSignWrappedSet())
    return APInt::getSignedMinValue(getBitWidth());
  return getLower();
}

bool ConstantRange::contains(const APInt &V) const {
  if (Lower == Upper)
    return isFullSet();

  if (!isUpperWrapped())
    return Lower.ule(V) && V.ult(Upper);
  return Lower.ule(V) || V.ult(Upper);
}

bool ConstantRange::contains(const ConstantRange &Other) const {
  if (isFullSet() || Other.isEmptySet()) return true;
  if (isEmptySet() || Other.isFullSet()) return false;

  if (!isUpperWrapped()) {
    if (Other.isUpperWrapped())
      return false;

    return Lower.ule(Other.getLower()) && Other.getUpper().ule(Upper);
  }

  if (!Other.isUpperWrapped())
    return Other.getUpper().ule(Upper) ||
           Lower.ule(Other.getLower());

  return Other.getUpper().ule(Upper) && Lower.ule(Other.getLower());
}

unsigned ConstantRange::getActiveBits() const {
  if (isEmptySet())
    return 0;

  return getUnsignedMax().getActiveBits();
}

unsigned ConstantRange::getMinSignedBits() const {
  if (isEmptySet())
    return 0;

  return std::max(getSignedMin().getSignificantBits(),
                  getSignedMax().getSignificantBits());
}

ConstantRange ConstantRange::subtract(const APInt &Val) const {
  assert(Val.getBitWidth() == getBitWidth() && "Wrong bit width");
  // If the set is empty or full, don't modify the endpoints.
  if (Lower == Upper)
    return *this;
  return ConstantRange(Lower - Val, Upper - Val);
}

ConstantRange ConstantRange::difference(const ConstantRange &CR) const {
  return intersectWith(CR.inverse());
}

static ConstantRange getPreferredRange(
    const ConstantRange &CR1, const ConstantRange &CR2,
    ConstantRange::PreferredRangeType Type) {
  if (Type == ConstantRange::Unsigned) {
    if (!CR1.isWrappedSet() && CR2.isWrappedSet())
      return CR1;
    if (CR1.isWrappedSet() && !CR2.isWrappedSet())
      return CR2;
  } else if (Type == ConstantRange::Signed) {
    if (!CR1.isSignWrappedSet() && CR2.isSignWrappedSet())
      return CR1;
    if (CR1.isSignWrappedSet() && !CR2.isSignWrappedSet())
      return CR2;
  }

  if (CR1.isSizeStrictlySmallerThan(CR2))
    return CR1;
  return CR2;
}

ConstantRange ConstantRange::intersectWith(const ConstantRange &CR,
                                           PreferredRangeType Type) const {
  assert(getBitWidth() == CR.getBitWidth() &&
         "ConstantRange types don't agree!");

  // Handle common cases.
  if (   isEmptySet() || CR.isFullSet()) return *this;
  if (CR.isEmptySet() ||    isFullSet()) return CR;

  if (!isUpperWrapped() && CR.isUpperWrapped())
    return CR.intersectWith(*this, Type);

  if (!isUpperWrapped() && !CR.isUpperWrapped()) {
    if (Lower.ult(CR.Lower)) {
      // L---U       : this
      //       L---U : CR
      if (Upper.ule(CR.Lower))
        return getEmpty();

      // L---U       : this
      //   L---U     : CR
      if (Upper.ult(CR.Upper))
        return ConstantRange(CR.Lower, Upper);

      // L-------U   : this
      //   L---U     : CR
      return CR;
    }
    //   L---U     : this
    // L-------U   : CR
    if (Upper.ult(CR.Upper))
      return *this;

    //   L-----U   : this
    // L-----U     : CR
    if (Lower.ult(CR.Upper))
      return ConstantRange(Lower, CR.Upper);

    //       L---U : this
    // L---U       : CR
    return getEmpty();
  }

  if (isUpperWrapped() && !CR.isUpperWrapped()) {
    if (CR.Lower.ult(Upper)) {
      // ------U   L--- : this
      //  L--U          : CR
      if (CR.Upper.ult(Upper))
        return CR;

      // ------U   L--- : this
      //  L------U      : CR
      if (CR.Upper.ule(Lower))
        return ConstantRange(CR.Lower, Upper);

      // ------U   L--- : this
      //  L----------U  : CR
      return getPreferredRange(*this, CR, Type);
    }
    if (CR.Lower.ult(Lower)) {
      // --U      L---- : this
      //     L--U       : CR
      if (CR.Upper.ule(Lower))
        return getEmpty();

      // --U      L---- : this
      //     L------U   : CR
      return ConstantRange(Lower, CR.Upper);
    }

    // --U  L------ : this
    //        L--U  : CR
    return CR;
  }

  if (CR.Upper.ult(Upper)) {
    // ------U L-- : this
    // --U L------ : CR
    if (CR.Lower.ult(Upper))
      return getPreferredRange(*this, CR, Type);

    // ----U   L-- : this
    // --U   L---- : CR
    if (CR.Lower.ult(Lower))
      return ConstantRange(Lower, CR.Upper);

    // ----U L---- : this
    // --U     L-- : CR
    return CR;
  }
  if (CR.Upper.ule(Lower)) {
    // --U     L-- : this
    // ----U L---- : CR
    if (CR.Lower.ult(Lower))
      return *this;

    // --U   L---- : this
    // ----U   L-- : CR
    return ConstantRange(CR.Lower, Upper);
  }

  // --U L------ : this
  // ------U L-- : CR
  return getPreferredRange(*this, CR, Type);
}

ConstantRange ConstantRange::unionWith(const ConstantRange &CR,
                                       PreferredRangeType Type) const {
  assert(getBitWidth() == CR.getBitWidth() &&
         "ConstantRange types don't agree!");

  if (   isFullSet() || CR.isEmptySet()) return *this;
  if (CR.isFullSet() ||    isEmptySet()) return CR;

  if (!isUpperWrapped() && CR.isUpperWrapped())
    return CR.unionWith(*this, Type);

  if (!isUpperWrapped() && !CR.isUpperWrapped()) {
    //        L---U  and  L---U        : this
    //  L---U                   L---U  : CR
    // result in one of
    //  L---------U
    // -----U L-----
    if (CR.Upper.ult(Lower) || Upper.ult(CR.Lower))
      return getPreferredRange(
          ConstantRange(Lower, CR.Upper), ConstantRange(CR.Lower, Upper), Type);

    APInt L = CR.Lower.ult(Lower) ? CR.Lower : Lower;
    APInt U = (CR.Upper - 1).ugt(Upper - 1) ? CR.Upper : Upper;

    if (L.isZero() && U.isZero())
      return getFull();

    return ConstantRange(std::move(L), std::move(U));
  }

  if (!CR.isUpperWrapped()) {
    // ------U   L-----  and  ------U   L----- : this
    //   L--U                            L--U  : CR
    if (CR.Upper.ule(Upper) || CR.Lower.uge(Lower))
      return *this;

    // ------U   L----- : this
    //    L---------U   : CR
    if (CR.Lower.ule(Upper) && Lower.ule(CR.Upper))
      return getFull();

    // ----U       L---- : this
    //       L---U       : CR
    // results in one of
    // ----------U L----
    // ----U L----------
    if (Upper.ult(CR.Lower) && CR.Upper.ult(Lower))
      return getPreferredRange(
          ConstantRange(Lower, CR.Upper), ConstantRange(CR.Lower, Upper), Type);

    // ----U     L----- : this
    //        L----U    : CR
    if (Upper.ult(CR.Lower) && Lower.ule(CR.Upper))
      return ConstantRange(CR.Lower, Upper);

    // ------U    L---- : this
    //    L-----U       : CR
    assert(CR.Lower.ule(Upper) && CR.Upper.ult(Lower) &&
           "ConstantRange::unionWith missed a case with one range wrapped");
    return ConstantRange(Lower, CR.Upper);
  }

  // ------U    L----  and  ------U    L---- : this
  // -U  L-----------  and  ------------U  L : CR
  if (CR.Lower.ule(Upper) || Lower.ule(CR.Upper))
    return getFull();

  APInt L = CR.Lower.ult(Lower) ? CR.Lower : Lower;
  APInt U = CR.Upper.ugt(Upper) ? CR.Upper : Upper;

  return ConstantRange(std::move(L), std::move(U));
}

std::optional<ConstantRange>
ConstantRange::exactIntersectWith(const ConstantRange &CR) const {
  // TODO: This can be implemented more efficiently.
  ConstantRange Result = intersectWith(CR);
  if (Result == inverse().unionWith(CR.inverse()).inverse())
    return Result;
  return std::nullopt;
}

std::optional<ConstantRange>
ConstantRange::exactUnionWith(const ConstantRange &CR) const {
  // TODO: This can be implemented more efficiently.
  ConstantRange Result = unionWith(CR);
  if (Result == inverse().intersectWith(CR.inverse()).inverse())
    return Result;
  return std::nullopt;
}

ConstantRange ConstantRange::castOp(Instruction::CastOps CastOp,
                                    uint32_t ResultBitWidth) const {
  switch (CastOp) {
  default:
    llvm_unreachable("unsupported cast type");
  case Instruction::Trunc:
    return truncate(ResultBitWidth);
  case Instruction::SExt:
    return signExtend(ResultBitWidth);
  case Instruction::ZExt:
    return zeroExtend(ResultBitWidth);
  case Instruction::BitCast:
    return *this;
  case Instruction::FPToUI:
  case Instruction::FPToSI:
    if (getBitWidth() == ResultBitWidth)
      return *this;
    else
      return getFull(ResultBitWidth);
  case Instruction::UIToFP: {
    // TODO: use input range if available
    auto BW = getBitWidth();
    APInt Min = APInt::getMinValue(BW);
    APInt Max = APInt::getMaxValue(BW);
    if (ResultBitWidth > BW) {
      Min = Min.zext(ResultBitWidth);
      Max = Max.zext(ResultBitWidth);
    }
    return getNonEmpty(std::move(Min), std::move(Max) + 1);
  }
  case Instruction::SIToFP: {
    // TODO: use input range if available
    auto BW = getBitWidth();
    APInt SMin = APInt::getSignedMinValue(BW);
    APInt SMax = APInt::getSignedMaxValue(BW);
    if (ResultBitWidth > BW) {
      SMin = SMin.sext(ResultBitWidth);
      SMax = SMax.sext(ResultBitWidth);
    }
    return getNonEmpty(std::move(SMin), std::move(SMax) + 1);
  }
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
  case Instruction::AddrSpaceCast:
    // Conservatively return getFull set.
    return getFull(ResultBitWidth);
  };
}

ConstantRange ConstantRange::zeroExtend(uint32_t DstTySize) const {
  if (isEmptySet()) return getEmpty(DstTySize);

  unsigned SrcTySize = getBitWidth();
  assert(SrcTySize < DstTySize && "Not a value extension");
  if (isFullSet() || isUpperWrapped()) {
    // Change into [0, 1 << src bit width)
    APInt LowerExt(DstTySize, 0);
    if (!Upper) // special case: [X, 0) -- not really wrapping around
      LowerExt = Lower.zext(DstTySize);
    return ConstantRange(std::move(LowerExt),
                         APInt::getOneBitSet(DstTySize, SrcTySize));
  }

  return ConstantRange(Lower.zext(DstTySize), Upper.zext(DstTySize));
}

ConstantRange ConstantRange::signExtend(uint32_t DstTySize) const {
  if (isEmptySet()) return getEmpty(DstTySize);

  unsigned SrcTySize = getBitWidth();
  assert(SrcTySize < DstTySize && "Not a value extension");

  // special case: [X, INT_MIN) -- not really wrapping around
  if (Upper.isMinSignedValue())
    return ConstantRange(Lower.sext(DstTySize), Upper.zext(DstTySize));

  if (isFullSet() || isSignWrappedSet()) {
    return ConstantRange(APInt::getHighBitsSet(DstTySize,DstTySize-SrcTySize+1),
                         APInt::getLowBitsSet(DstTySize, SrcTySize-1) + 1);
  }

  return ConstantRange(Lower.sext(DstTySize), Upper.sext(DstTySize));
}

ConstantRange ConstantRange::truncate(uint32_t DstTySize) const {
  assert(getBitWidth() > DstTySize && "Not a value truncation");
  if (isEmptySet())
    return getEmpty(DstTySize);
  if (isFullSet())
    return getFull(DstTySize);

  APInt LowerDiv(Lower), UpperDiv(Upper);
  ConstantRange Union(DstTySize, /*isFullSet=*/false);

  // Analyze wrapped sets in their two parts: [0, Upper) \/ [Lower, MaxValue]
  // We use the non-wrapped set code to analyze the [Lower, MaxValue) part, and
  // then we do the union with [MaxValue, Upper)
  if (isUpperWrapped()) {
    // If Upper is greater than or equal to MaxValue(DstTy), it covers the whole
    // truncated range.
    if (Upper.getActiveBits() > DstTySize || Upper.countr_one() == DstTySize)
      return getFull(DstTySize);

    Union = ConstantRange(APInt::getMaxValue(DstTySize),Upper.trunc(DstTySize));
    UpperDiv.setAllBits();

    // Union covers the MaxValue case, so return if the remaining range is just
    // MaxValue(DstTy).
    if (LowerDiv == UpperDiv)
      return Union;
  }

  // Chop off the most significant bits that are past the destination bitwidth.
  if (LowerDiv.getActiveBits() > DstTySize) {
    // Mask to just the signficant bits and subtract from LowerDiv/UpperDiv.
    APInt Adjust = LowerDiv & APInt::getBitsSetFrom(getBitWidth(), DstTySize);
    LowerDiv -= Adjust;
    UpperDiv -= Adjust;
  }

  unsigned UpperDivWidth = UpperDiv.getActiveBits();
  if (UpperDivWidth <= DstTySize)
    return ConstantRange(LowerDiv.trunc(DstTySize),
                         UpperDiv.trunc(DstTySize)).unionWith(Union);

  // The truncated value wraps around. Check if we can do better than fullset.
  if (UpperDivWidth == DstTySize + 1) {
    // Clear the MSB so that UpperDiv wraps around.
    UpperDiv.clearBit(DstTySize);
    if (UpperDiv.ult(LowerDiv))
      return ConstantRange(LowerDiv.trunc(DstTySize),
                           UpperDiv.trunc(DstTySize)).unionWith(Union);
  }

  return getFull(DstTySize);
}

ConstantRange ConstantRange::zextOrTrunc(uint32_t DstTySize) const {
  unsigned SrcTySize = getBitWidth();
  if (SrcTySize > DstTySize)
    return truncate(DstTySize);
  if (SrcTySize < DstTySize)
    return zeroExtend(DstTySize);
  return *this;
}

ConstantRange ConstantRange::sextOrTrunc(uint32_t DstTySize) const {
  unsigned SrcTySize = getBitWidth();
  if (SrcTySize > DstTySize)
    return truncate(DstTySize);
  if (SrcTySize < DstTySize)
    return signExtend(DstTySize);
  return *this;
}

ConstantRange ConstantRange::binaryOp(Instruction::BinaryOps BinOp,
                                      const ConstantRange &Other) const {
  assert(Instruction::isBinaryOp(BinOp) && "Binary operators only!");

  switch (BinOp) {
  case Instruction::Add:
    return add(Other);
  case Instruction::Sub:
    return sub(Other);
  case Instruction::Mul:
    return multiply(Other);
  case Instruction::UDiv:
    return udiv(Other);
  case Instruction::SDiv:
    return sdiv(Other);
  case Instruction::URem:
    return urem(Other);
  case Instruction::SRem:
    return srem(Other);
  case Instruction::Shl:
    return shl(Other);
  case Instruction::LShr:
    return lshr(Other);
  case Instruction::AShr:
    return ashr(Other);
  case Instruction::And:
    return binaryAnd(Other);
  case Instruction::Or:
    return binaryOr(Other);
  case Instruction::Xor:
    return binaryXor(Other);
  // Note: floating point operations applied to abstract ranges are just
  // ideal integer operations with a lossy representation
  case Instruction::FAdd:
    return add(Other);
  case Instruction::FSub:
    return sub(Other);
  case Instruction::FMul:
    return multiply(Other);
  default:
    // Conservatively return getFull set.
    return getFull();
  }
}

ConstantRange ConstantRange::overflowingBinaryOp(Instruction::BinaryOps BinOp,
                                                 const ConstantRange &Other,
                                                 unsigned NoWrapKind) const {
  assert(Instruction::isBinaryOp(BinOp) && "Binary operators only!");

  switch (BinOp) {
  case Instruction::Add:
    return addWithNoWrap(Other, NoWrapKind);
  case Instruction::Sub:
    return subWithNoWrap(Other, NoWrapKind);
  case Instruction::Mul:
    return multiplyWithNoWrap(Other, NoWrapKind);
  default:
    // Don't know about this Overflowing Binary Operation.
    // Conservatively fallback to plain binop handling.
    return binaryOp(BinOp, Other);
  }
}

bool ConstantRange::isIntrinsicSupported(Intrinsic::ID IntrinsicID) {
  switch (IntrinsicID) {
  case Intrinsic::uadd_sat:
  case Intrinsic::usub_sat:
  case Intrinsic::sadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::umin:
  case Intrinsic::umax:
  case Intrinsic::smin:
  case Intrinsic::smax:
  case Intrinsic::abs:
  case Intrinsic::ctlz:
  case Intrinsic::cttz:
  case Intrinsic::ctpop:
    return true;
  default:
    return false;
  }
}

ConstantRange ConstantRange::intrinsic(Intrinsic::ID IntrinsicID,
                                       ArrayRef<ConstantRange> Ops) {
  switch (IntrinsicID) {
  case Intrinsic::uadd_sat:
    return Ops[0].uadd_sat(Ops[1]);
  case Intrinsic::usub_sat:
    return Ops[0].usub_sat(Ops[1]);
  case Intrinsic::sadd_sat:
    return Ops[0].sadd_sat(Ops[1]);
  case Intrinsic::ssub_sat:
    return Ops[0].ssub_sat(Ops[1]);
  case Intrinsic::umin:
    return Ops[0].umin(Ops[1]);
  case Intrinsic::umax:
    return Ops[0].umax(Ops[1]);
  case Intrinsic::smin:
    return Ops[0].smin(Ops[1]);
  case Intrinsic::smax:
    return Ops[0].smax(Ops[1]);
  case Intrinsic::abs: {
    const APInt *IntMinIsPoison = Ops[1].getSingleElement();
    assert(IntMinIsPoison && "Must be known (immarg)");
    assert(IntMinIsPoison->getBitWidth() == 1 && "Must be boolean");
    return Ops[0].abs(IntMinIsPoison->getBoolValue());
  }
  case Intrinsic::ctlz: {
    const APInt *ZeroIsPoison = Ops[1].getSingleElement();
    assert(ZeroIsPoison && "Must be known (immarg)");
    assert(ZeroIsPoison->getBitWidth() == 1 && "Must be boolean");
    return Ops[0].ctlz(ZeroIsPoison->getBoolValue());
  }
  case Intrinsic::cttz: {
    const APInt *ZeroIsPoison = Ops[1].getSingleElement();
    assert(ZeroIsPoison && "Must be known (immarg)");
    assert(ZeroIsPoison->getBitWidth() == 1 && "Must be boolean");
    return Ops[0].cttz(ZeroIsPoison->getBoolValue());
  }
  case Intrinsic::ctpop:
    return Ops[0].ctpop();
  default:
    assert(!isIntrinsicSupported(IntrinsicID) && "Shouldn't be supported");
    llvm_unreachable("Unsupported intrinsic");
  }
}

ConstantRange
ConstantRange::add(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  if (isFullSet() || Other.isFullSet())
    return getFull();

  APInt NewLower = getLower() + Other.getLower();
  APInt NewUpper = getUpper() + Other.getUpper() - 1;
  if (NewLower == NewUpper)
    return getFull();

  ConstantRange X = ConstantRange(std::move(NewLower), std::move(NewUpper));
  if (X.isSizeStrictlySmallerThan(*this) ||
      X.isSizeStrictlySmallerThan(Other))
    // We've wrapped, therefore, full set.
    return getFull();
  return X;
}

ConstantRange ConstantRange::addWithNoWrap(const ConstantRange &Other,
                                           unsigned NoWrapKind,
                                           PreferredRangeType RangeType) const {
  // Calculate the range for "X + Y" which is guaranteed not to wrap(overflow).
  // (X is from this, and Y is from Other)
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  if (isFullSet() && Other.isFullSet())
    return getFull();

  using OBO = OverflowingBinaryOperator;
  ConstantRange Result = add(Other);

  // If an overflow happens for every value pair in these two constant ranges,
  // we must return Empty set. In this case, we get that for free, because we
  // get lucky that intersection of add() with uadd_sat()/sadd_sat() results
  // in an empty set.

  if (NoWrapKind & OBO::NoSignedWrap)
    Result = Result.intersectWith(sadd_sat(Other), RangeType);

  if (NoWrapKind & OBO::NoUnsignedWrap)
    Result = Result.intersectWith(uadd_sat(Other), RangeType);

  return Result;
}

ConstantRange
ConstantRange::sub(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  if (isFullSet() || Other.isFullSet())
    return getFull();

  APInt NewLower = getLower() - Other.getUpper() + 1;
  APInt NewUpper = getUpper() - Other.getLower();
  if (NewLower == NewUpper)
    return getFull();

  ConstantRange X = ConstantRange(std::move(NewLower), std::move(NewUpper));
  if (X.isSizeStrictlySmallerThan(*this) ||
      X.isSizeStrictlySmallerThan(Other))
    // We've wrapped, therefore, full set.
    return getFull();
  return X;
}

ConstantRange ConstantRange::subWithNoWrap(const ConstantRange &Other,
                                           unsigned NoWrapKind,
                                           PreferredRangeType RangeType) const {
  // Calculate the range for "X - Y" which is guaranteed not to wrap(overflow).
  // (X is from this, and Y is from Other)
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  if (isFullSet() && Other.isFullSet())
    return getFull();

  using OBO = OverflowingBinaryOperator;
  ConstantRange Result = sub(Other);

  // If an overflow happens for every value pair in these two constant ranges,
  // we must return Empty set. In signed case, we get that for free, because we
  // get lucky that intersection of sub() with ssub_sat() results in an
  // empty set. But for unsigned we must perform the overflow check manually.

  if (NoWrapKind & OBO::NoSignedWrap)
    Result = Result.intersectWith(ssub_sat(Other), RangeType);

  if (NoWrapKind & OBO::NoUnsignedWrap) {
    if (getUnsignedMax().ult(Other.getUnsignedMin()))
      return getEmpty(); // Always overflows.
    Result = Result.intersectWith(usub_sat(Other), RangeType);
  }

  return Result;
}

ConstantRange
ConstantRange::multiply(const ConstantRange &Other) const {
  // TODO: If either operand is a single element and the multiply is known to
  // be non-wrapping, round the result min and max value to the appropriate
  // multiple of that element. If wrapping is possible, at least adjust the
  // range according to the greatest power-of-two factor of the single element.

  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  if (const APInt *C = getSingleElement()) {
    if (C->isOne())
      return Other;
    if (C->isAllOnes())
      return ConstantRange(APInt::getZero(getBitWidth())).sub(Other);
  }

  if (const APInt *C = Other.getSingleElement()) {
    if (C->isOne())
      return *this;
    if (C->isAllOnes())
      return ConstantRange(APInt::getZero(getBitWidth())).sub(*this);
  }

  // Multiplication is signedness-independent. However different ranges can be
  // obtained depending on how the input ranges are treated. These different
  // ranges are all conservatively correct, but one might be better than the
  // other. We calculate two ranges; one treating the inputs as unsigned
  // and the other signed, then return the smallest of these ranges.

  // Unsigned range first.
  APInt this_min = getUnsignedMin().zext(getBitWidth() * 2);
  APInt this_max = getUnsignedMax().zext(getBitWidth() * 2);
  APInt Other_min = Other.getUnsignedMin().zext(getBitWidth() * 2);
  APInt Other_max = Other.getUnsignedMax().zext(getBitWidth() * 2);

  ConstantRange Result_zext = ConstantRange(this_min * Other_min,
                                            this_max * Other_max + 1);
  ConstantRange UR = Result_zext.truncate(getBitWidth());

  // If the unsigned range doesn't wrap, and isn't negative then it's a range
  // from one positive number to another which is as good as we can generate.
  // In this case, skip the extra work of generating signed ranges which aren't
  // going to be better than this range.
  if (!UR.isUpperWrapped() &&
      (UR.getUpper().isNonNegative() || UR.getUpper().isMinSignedValue()))
    return UR;

  // Now the signed range. Because we could be dealing with negative numbers
  // here, the lower bound is the smallest of the cartesian product of the
  // lower and upper ranges; for example:
  //   [-1,4) * [-2,3) = min(-1*-2, -1*2, 3*-2, 3*2) = -6.
  // Similarly for the upper bound, swapping min for max.

  this_min = getSignedMin().sext(getBitWidth() * 2);
  this_max = getSignedMax().sext(getBitWidth() * 2);
  Other_min = Other.getSignedMin().sext(getBitWidth() * 2);
  Other_max = Other.getSignedMax().sext(getBitWidth() * 2);

  auto L = {this_min * Other_min, this_min * Other_max,
            this_max * Other_min, this_max * Other_max};
  auto Compare = [](const APInt &A, const APInt &B) { return A.slt(B); };
  ConstantRange Result_sext(std::min(L, Compare), std::max(L, Compare) + 1);
  ConstantRange SR = Result_sext.truncate(getBitWidth());

  return UR.isSizeStrictlySmallerThan(SR) ? UR : SR;
}

ConstantRange
ConstantRange::multiplyWithNoWrap(const ConstantRange &Other,
                                  unsigned NoWrapKind,
                                  PreferredRangeType RangeType) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  if (isFullSet() && Other.isFullSet())
    return getFull();

  ConstantRange Result = multiply(Other);

  if (NoWrapKind & OverflowingBinaryOperator::NoSignedWrap)
    Result = Result.intersectWith(smul_sat(Other), RangeType);

  if (NoWrapKind & OverflowingBinaryOperator::NoUnsignedWrap)
    Result = Result.intersectWith(umul_sat(Other), RangeType);

  return Result;
}

ConstantRange ConstantRange::smul_fast(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt Min = getSignedMin();
  APInt Max = getSignedMax();
  APInt OtherMin = Other.getSignedMin();
  APInt OtherMax = Other.getSignedMax();

  bool O1, O2, O3, O4;
  auto Muls = {Min.smul_ov(OtherMin, O1), Min.smul_ov(OtherMax, O2),
               Max.smul_ov(OtherMin, O3), Max.smul_ov(OtherMax, O4)};
  if (O1 || O2 || O3 || O4)
    return getFull();

  auto Compare = [](const APInt &A, const APInt &B) { return A.slt(B); };
  return getNonEmpty(std::min(Muls, Compare), std::max(Muls, Compare) + 1);
}

ConstantRange
ConstantRange::smax(const ConstantRange &Other) const {
  // X smax Y is: range(smax(X_smin, Y_smin),
  //                    smax(X_smax, Y_smax))
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  APInt NewL = APIntOps::smax(getSignedMin(), Other.getSignedMin());
  APInt NewU = APIntOps::smax(getSignedMax(), Other.getSignedMax()) + 1;
  ConstantRange Res = getNonEmpty(std::move(NewL), std::move(NewU));
  if (isSignWrappedSet() || Other.isSignWrappedSet())
    return Res.intersectWith(unionWith(Other, Signed), Signed);
  return Res;
}

ConstantRange
ConstantRange::umax(const ConstantRange &Other) const {
  // X umax Y is: range(umax(X_umin, Y_umin),
  //                    umax(X_umax, Y_umax))
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  APInt NewL = APIntOps::umax(getUnsignedMin(), Other.getUnsignedMin());
  APInt NewU = APIntOps::umax(getUnsignedMax(), Other.getUnsignedMax()) + 1;
  ConstantRange Res = getNonEmpty(std::move(NewL), std::move(NewU));
  if (isWrappedSet() || Other.isWrappedSet())
    return Res.intersectWith(unionWith(Other, Unsigned), Unsigned);
  return Res;
}

ConstantRange
ConstantRange::smin(const ConstantRange &Other) const {
  // X smin Y is: range(smin(X_smin, Y_smin),
  //                    smin(X_smax, Y_smax))
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  APInt NewL = APIntOps::smin(getSignedMin(), Other.getSignedMin());
  APInt NewU = APIntOps::smin(getSignedMax(), Other.getSignedMax()) + 1;
  ConstantRange Res = getNonEmpty(std::move(NewL), std::move(NewU));
  if (isSignWrappedSet() || Other.isSignWrappedSet())
    return Res.intersectWith(unionWith(Other, Signed), Signed);
  return Res;
}

ConstantRange
ConstantRange::umin(const ConstantRange &Other) const {
  // X umin Y is: range(umin(X_umin, Y_umin),
  //                    umin(X_umax, Y_umax))
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();
  APInt NewL = APIntOps::umin(getUnsignedMin(), Other.getUnsignedMin());
  APInt NewU = APIntOps::umin(getUnsignedMax(), Other.getUnsignedMax()) + 1;
  ConstantRange Res = getNonEmpty(std::move(NewL), std::move(NewU));
  if (isWrappedSet() || Other.isWrappedSet())
    return Res.intersectWith(unionWith(Other, Unsigned), Unsigned);
  return Res;
}

ConstantRange
ConstantRange::udiv(const ConstantRange &RHS) const {
  if (isEmptySet() || RHS.isEmptySet() || RHS.getUnsignedMax().isZero())
    return getEmpty();

  APInt Lower = getUnsignedMin().udiv(RHS.getUnsignedMax());

  APInt RHS_umin = RHS.getUnsignedMin();
  if (RHS_umin.isZero()) {
    // We want the lowest value in RHS excluding zero. Usually that would be 1
    // except for a range in the form of [X, 1) in which case it would be X.
    if (RHS.getUpper() == 1)
      RHS_umin = RHS.getLower();
    else
      RHS_umin = 1;
  }

  APInt Upper = getUnsignedMax().udiv(RHS_umin) + 1;
  return getNonEmpty(std::move(Lower), std::move(Upper));
}

ConstantRange ConstantRange::sdiv(const ConstantRange &RHS) const {
  // We split up the LHS and RHS into positive and negative components
  // and then also compute the positive and negative components of the result
  // separately by combining division results with the appropriate signs.
  APInt Zero = APInt::getZero(getBitWidth());
  APInt SignedMin = APInt::getSignedMinValue(getBitWidth());
  // There are no positive 1-bit values. The 1 would get interpreted as -1.
  ConstantRange PosFilter =
      getBitWidth() == 1 ? getEmpty()
                         : ConstantRange(APInt(getBitWidth(), 1), SignedMin);
  ConstantRange NegFilter(SignedMin, Zero);
  ConstantRange PosL = intersectWith(PosFilter);
  ConstantRange NegL = intersectWith(NegFilter);
  ConstantRange PosR = RHS.intersectWith(PosFilter);
  ConstantRange NegR = RHS.intersectWith(NegFilter);

  ConstantRange PosRes = getEmpty();
  if (!PosL.isEmptySet() && !PosR.isEmptySet())
    // pos / pos = pos.
    PosRes = ConstantRange(PosL.Lower.sdiv(PosR.Upper - 1),
                           (PosL.Upper - 1).sdiv(PosR.Lower) + 1);

  if (!NegL.isEmptySet() && !NegR.isEmptySet()) {
    // neg / neg = pos.
    //
    // We need to deal with one tricky case here: SignedMin / -1 is UB on the
    // IR level, so we'll want to exclude this case when calculating bounds.
    // (For APInts the operation is well-defined and yields SignedMin.) We
    // handle this by dropping either SignedMin from the LHS or -1 from the RHS.
    APInt Lo = (NegL.Upper - 1).sdiv(NegR.Lower);
    if (NegL.Lower.isMinSignedValue() && NegR.Upper.isZero()) {
      // Remove -1 from the LHS. Skip if it's the only element, as this would
      // leave us with an empty set.
      if (!NegR.Lower.isAllOnes()) {
        APInt AdjNegRUpper;
        if (RHS.Lower.isAllOnes())
          // Negative part of [-1, X] without -1 is [SignedMin, X].
          AdjNegRUpper = RHS.Upper;
        else
          // [X, -1] without -1 is [X, -2].
          AdjNegRUpper = NegR.Upper - 1;

        PosRes = PosRes.unionWith(
            ConstantRange(Lo, NegL.Lower.sdiv(AdjNegRUpper - 1) + 1));
      }

      // Remove SignedMin from the RHS. Skip if it's the only element, as this
      // would leave us with an empty set.
      if (NegL.Upper != SignedMin + 1) {
        APInt AdjNegLLower;
        if (Upper == SignedMin + 1)
          // Negative part of [X, SignedMin] without SignedMin is [X, -1].
          AdjNegLLower = Lower;
        else
          // [SignedMin, X] without SignedMin is [SignedMin + 1, X].
          AdjNegLLower = NegL.Lower + 1;

        PosRes = PosRes.unionWith(
            ConstantRange(std::move(Lo),
                          AdjNegLLower.sdiv(NegR.Upper - 1) + 1));
      }
    } else {
      PosRes = PosRes.unionWith(
          ConstantRange(std::move(Lo), NegL.Lower.sdiv(NegR.Upper - 1) + 1));
    }
  }

  ConstantRange NegRes = getEmpty();
  if (!PosL.isEmptySet() && !NegR.isEmptySet())
    // pos / neg = neg.
    NegRes = ConstantRange((PosL.Upper - 1).sdiv(NegR.Upper - 1),
                           PosL.Lower.sdiv(NegR.Lower) + 1);

  if (!NegL.isEmptySet() && !PosR.isEmptySet())
    // neg / pos = neg.
    NegRes = NegRes.unionWith(
        ConstantRange(NegL.Lower.sdiv(PosR.Lower),
                      (NegL.Upper - 1).sdiv(PosR.Upper - 1) + 1));

  // Prefer a non-wrapping signed range here.
  ConstantRange Res = NegRes.unionWith(PosRes, PreferredRangeType::Signed);

  // Preserve the zero that we dropped when splitting the LHS by sign.
  if (contains(Zero) && (!PosR.isEmptySet() || !NegR.isEmptySet()))
    Res = Res.unionWith(ConstantRange(Zero));
  return Res;
}

ConstantRange ConstantRange::urem(const ConstantRange &RHS) const {
  if (isEmptySet() || RHS.isEmptySet() || RHS.getUnsignedMax().isZero())
    return getEmpty();

  if (const APInt *RHSInt = RHS.getSingleElement()) {
    // UREM by null is UB.
    if (RHSInt->isZero())
      return getEmpty();
    // Use APInt's implementation of UREM for single element ranges.
    if (const APInt *LHSInt = getSingleElement())
      return {LHSInt->urem(*RHSInt)};
  }

  // L % R for L < R is L.
  if (getUnsignedMax().ult(RHS.getUnsignedMin()))
    return *this;

  // L % R is <= L and < R.
  APInt Upper = APIntOps::umin(getUnsignedMax(), RHS.getUnsignedMax() - 1) + 1;
  return getNonEmpty(APInt::getZero(getBitWidth()), std::move(Upper));
}

ConstantRange ConstantRange::srem(const ConstantRange &RHS) const {
  if (isEmptySet() || RHS.isEmptySet())
    return getEmpty();

  if (const APInt *RHSInt = RHS.getSingleElement()) {
    // SREM by null is UB.
    if (RHSInt->isZero())
      return getEmpty();
    // Use APInt's implementation of SREM for single element ranges.
    if (const APInt *LHSInt = getSingleElement())
      return {LHSInt->srem(*RHSInt)};
  }

  ConstantRange AbsRHS = RHS.abs();
  APInt MinAbsRHS = AbsRHS.getUnsignedMin();
  APInt MaxAbsRHS = AbsRHS.getUnsignedMax();

  // Modulus by zero is UB.
  if (MaxAbsRHS.isZero())
    return getEmpty();

  if (MinAbsRHS.isZero())
    ++MinAbsRHS;

  APInt MinLHS = getSignedMin(), MaxLHS = getSignedMax();

  if (MinLHS.isNonNegative()) {
    // L % R for L < R is L.
    if (MaxLHS.ult(MinAbsRHS))
      return *this;

    // L % R is <= L and < R.
    APInt Upper = APIntOps::umin(MaxLHS, MaxAbsRHS - 1) + 1;
    return ConstantRange(APInt::getZero(getBitWidth()), std::move(Upper));
  }

  // Same basic logic as above, but the result is negative.
  if (MaxLHS.isNegative()) {
    if (MinLHS.ugt(-MinAbsRHS))
      return *this;

    APInt Lower = APIntOps::umax(MinLHS, -MaxAbsRHS + 1);
    return ConstantRange(std::move(Lower), APInt(getBitWidth(), 1));
  }

  // LHS range crosses zero.
  APInt Lower = APIntOps::umax(MinLHS, -MaxAbsRHS + 1);
  APInt Upper = APIntOps::umin(MaxLHS, MaxAbsRHS - 1) + 1;
  return ConstantRange(std::move(Lower), std::move(Upper));
}

ConstantRange ConstantRange::binaryNot() const {
  return ConstantRange(APInt::getAllOnes(getBitWidth())).sub(*this);
}

ConstantRange ConstantRange::binaryAnd(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  ConstantRange KnownBitsRange =
      fromKnownBits(toKnownBits() & Other.toKnownBits(), false);
  ConstantRange UMinUMaxRange =
      getNonEmpty(APInt::getZero(getBitWidth()),
                  APIntOps::umin(Other.getUnsignedMax(), getUnsignedMax()) + 1);
  return KnownBitsRange.intersectWith(UMinUMaxRange);
}

ConstantRange ConstantRange::binaryOr(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  ConstantRange KnownBitsRange =
      fromKnownBits(toKnownBits() | Other.toKnownBits(), false);
  // Upper wrapped range.
  ConstantRange UMaxUMinRange =
      getNonEmpty(APIntOps::umax(getUnsignedMin(), Other.getUnsignedMin()),
                  APInt::getZero(getBitWidth()));
  return KnownBitsRange.intersectWith(UMaxUMinRange);
}

ConstantRange ConstantRange::binaryXor(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  // Use APInt's implementation of XOR for single element ranges.
  if (isSingleElement() && Other.isSingleElement())
    return {*getSingleElement() ^ *Other.getSingleElement()};

  // Special-case binary complement, since we can give a precise answer.
  if (Other.isSingleElement() && Other.getSingleElement()->isAllOnes())
    return binaryNot();
  if (isSingleElement() && getSingleElement()->isAllOnes())
    return Other.binaryNot();

  KnownBits LHSKnown = toKnownBits();
  KnownBits RHSKnown = Other.toKnownBits();
  KnownBits Known = LHSKnown ^ RHSKnown;
  ConstantRange CR = fromKnownBits(Known, /*IsSigned*/ false);
  // Typically the following code doesn't improve the result if BW = 1.
  if (getBitWidth() == 1)
    return CR;

  // If LHS is known to be the subset of RHS, treat LHS ^ RHS as RHS -nuw/nsw
  // LHS. If RHS is known to be the subset of LHS, treat LHS ^ RHS as LHS
  // -nuw/nsw RHS.
  if ((~LHSKnown.Zero).isSubsetOf(RHSKnown.One))
    CR = CR.intersectWith(Other.sub(*this), PreferredRangeType::Unsigned);
  else if ((~RHSKnown.Zero).isSubsetOf(LHSKnown.One))
    CR = CR.intersectWith(this->sub(Other), PreferredRangeType::Unsigned);
  return CR;
}

ConstantRange
ConstantRange::shl(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt Min = getUnsignedMin();
  APInt Max = getUnsignedMax();
  if (const APInt *RHS = Other.getSingleElement()) {
    unsigned BW = getBitWidth();
    if (RHS->uge(BW))
      return getEmpty();

    unsigned EqualLeadingBits = (Min ^ Max).countl_zero();
    if (RHS->ule(EqualLeadingBits))
      return getNonEmpty(Min << *RHS, (Max << *RHS) + 1);

    return getNonEmpty(APInt::getZero(BW),
                       APInt::getBitsSetFrom(BW, RHS->getZExtValue()) + 1);
  }

  APInt OtherMax = Other.getUnsignedMax();
  if (isAllNegative() && OtherMax.ule(Min.countl_one())) {
    // For negative numbers, if the shift does not overflow in a signed sense,
    // a larger shift will make the number smaller.
    Max <<= Other.getUnsignedMin();
    Min <<= OtherMax;
    return ConstantRange::getNonEmpty(std::move(Min), std::move(Max) + 1);
  }

  // There's overflow!
  if (OtherMax.ugt(Max.countl_zero()))
    return getFull();

  // FIXME: implement the other tricky cases

  Min <<= Other.getUnsignedMin();
  Max <<= OtherMax;

  return ConstantRange::getNonEmpty(std::move(Min), std::move(Max) + 1);
}

ConstantRange
ConstantRange::lshr(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt max = getUnsignedMax().lshr(Other.getUnsignedMin()) + 1;
  APInt min = getUnsignedMin().lshr(Other.getUnsignedMax());
  return getNonEmpty(std::move(min), std::move(max));
}

ConstantRange
ConstantRange::ashr(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  // May straddle zero, so handle both positive and negative cases.
  // 'PosMax' is the upper bound of the result of the ashr
  // operation, when Upper of the LHS of ashr is a non-negative.
  // number. Since ashr of a non-negative number will result in a
  // smaller number, the Upper value of LHS is shifted right with
  // the minimum value of 'Other' instead of the maximum value.
  APInt PosMax = getSignedMax().ashr(Other.getUnsignedMin()) + 1;

  // 'PosMin' is the lower bound of the result of the ashr
  // operation, when Lower of the LHS is a non-negative number.
  // Since ashr of a non-negative number will result in a smaller
  // number, the Lower value of LHS is shifted right with the
  // maximum value of 'Other'.
  APInt PosMin = getSignedMin().ashr(Other.getUnsignedMax());

  // 'NegMax' is the upper bound of the result of the ashr
  // operation, when Upper of the LHS of ashr is a negative number.
  // Since 'ashr' of a negative number will result in a bigger
  // number, the Upper value of LHS is shifted right with the
  // maximum value of 'Other'.
  APInt NegMax = getSignedMax().ashr(Other.getUnsignedMax()) + 1;

  // 'NegMin' is the lower bound of the result of the ashr
  // operation, when Lower of the LHS of ashr is a negative number.
  // Since 'ashr' of a negative number will result in a bigger
  // number, the Lower value of LHS is shifted right with the
  // minimum value of 'Other'.
  APInt NegMin = getSignedMin().ashr(Other.getUnsignedMin());

  APInt max, min;
  if (getSignedMin().isNonNegative()) {
    // Upper and Lower of LHS are non-negative.
    min = PosMin;
    max = PosMax;
  } else if (getSignedMax().isNegative()) {
    // Upper and Lower of LHS are negative.
    min = NegMin;
    max = NegMax;
  } else {
    // Upper is non-negative and Lower is negative.
    min = NegMin;
    max = PosMax;
  }
  return getNonEmpty(std::move(min), std::move(max));
}

ConstantRange ConstantRange::uadd_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt NewL = getUnsignedMin().uadd_sat(Other.getUnsignedMin());
  APInt NewU = getUnsignedMax().uadd_sat(Other.getUnsignedMax()) + 1;
  return getNonEmpty(std::move(NewL), std::move(NewU));
}

ConstantRange ConstantRange::sadd_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt NewL = getSignedMin().sadd_sat(Other.getSignedMin());
  APInt NewU = getSignedMax().sadd_sat(Other.getSignedMax()) + 1;
  return getNonEmpty(std::move(NewL), std::move(NewU));
}

ConstantRange ConstantRange::usub_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt NewL = getUnsignedMin().usub_sat(Other.getUnsignedMax());
  APInt NewU = getUnsignedMax().usub_sat(Other.getUnsignedMin()) + 1;
  return getNonEmpty(std::move(NewL), std::move(NewU));
}

ConstantRange ConstantRange::ssub_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt NewL = getSignedMin().ssub_sat(Other.getSignedMax());
  APInt NewU = getSignedMax().ssub_sat(Other.getSignedMin()) + 1;
  return getNonEmpty(std::move(NewL), std::move(NewU));
}

ConstantRange ConstantRange::umul_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt NewL = getUnsignedMin().umul_sat(Other.getUnsignedMin());
  APInt NewU = getUnsignedMax().umul_sat(Other.getUnsignedMax()) + 1;
  return getNonEmpty(std::move(NewL), std::move(NewU));
}

ConstantRange ConstantRange::smul_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  // Because we could be dealing with negative numbers here, the lower bound is
  // the smallest of the cartesian product of the lower and upper ranges;
  // for example:
  //   [-1,4) * [-2,3) = min(-1*-2, -1*2, 3*-2, 3*2) = -6.
  // Similarly for the upper bound, swapping min for max.

  APInt Min = getSignedMin();
  APInt Max = getSignedMax();
  APInt OtherMin = Other.getSignedMin();
  APInt OtherMax = Other.getSignedMax();

  auto L = {Min.smul_sat(OtherMin), Min.smul_sat(OtherMax),
            Max.smul_sat(OtherMin), Max.smul_sat(OtherMax)};
  auto Compare = [](const APInt &A, const APInt &B) { return A.slt(B); };
  return getNonEmpty(std::min(L, Compare), std::max(L, Compare) + 1);
}

ConstantRange ConstantRange::ushl_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt NewL = getUnsignedMin().ushl_sat(Other.getUnsignedMin());
  APInt NewU = getUnsignedMax().ushl_sat(Other.getUnsignedMax()) + 1;
  return getNonEmpty(std::move(NewL), std::move(NewU));
}

ConstantRange ConstantRange::sshl_sat(const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return getEmpty();

  APInt Min = getSignedMin(), Max = getSignedMax();
  APInt ShAmtMin = Other.getUnsignedMin(), ShAmtMax = Other.getUnsignedMax();
  APInt NewL = Min.sshl_sat(Min.isNonNegative() ? ShAmtMin : ShAmtMax);
  APInt NewU = Max.sshl_sat(Max.isNegative() ? ShAmtMin : ShAmtMax) + 1;
  return getNonEmpty(std::move(NewL), std::move(NewU));
}

ConstantRange ConstantRange::inverse() const {
  if (isFullSet())
    return getEmpty();
  if (isEmptySet())
    return getFull();
  return ConstantRange(Upper, Lower);
}

ConstantRange ConstantRange::abs(bool IntMinIsPoison) const {
  if (isEmptySet())
    return getEmpty();

  if (isSignWrappedSet()) {
    APInt Lo;
    // Check whether the range crosses zero.
    if (Upper.isStrictlyPositive() || !Lower.isStrictlyPositive())
      Lo = APInt::getZero(getBitWidth());
    else
      Lo = APIntOps::umin(Lower, -Upper + 1);

    // If SignedMin is not poison, then it is included in the result range.
    if (IntMinIsPoison)
      return ConstantRange(Lo, APInt::getSignedMinValue(getBitWidth()));
    else
      return ConstantRange(Lo, APInt::getSignedMinValue(getBitWidth()) + 1);
  }

  APInt SMin = getSignedMin(), SMax = getSignedMax();

  // Skip SignedMin if it is poison.
  if (IntMinIsPoison && SMin.isMinSignedValue()) {
    // The range may become empty if it *only* contains SignedMin.
    if (SMax.isMinSignedValue())
      return getEmpty();
    ++SMin;
  }

  // All non-negative.
  if (SMin.isNonNegative())
    return ConstantRange(SMin, SMax + 1);

  // All negative.
  if (SMax.isNegative())
    return ConstantRange(-SMax, -SMin + 1);

  // Range crosses zero.
  return ConstantRange::getNonEmpty(APInt::getZero(getBitWidth()),
                                    APIntOps::umax(-SMin, SMax) + 1);
}

ConstantRange ConstantRange::ctlz(bool ZeroIsPoison) const {
  if (isEmptySet())
    return getEmpty();

  APInt Zero = APInt::getZero(getBitWidth());
  if (ZeroIsPoison && contains(Zero)) {
    // ZeroIsPoison is set, and zero is contained. We discern three cases, in
    // which a zero can appear:
    // 1) Lower is zero, handling cases of kind [0, 1), [0, 2), etc.
    // 2) Upper is zero, wrapped set, handling cases of kind [3, 0], etc.
    // 3) Zero contained in a wrapped set, e.g., [3, 2), [3, 1), etc.

    if (getLower().isZero()) {
      if ((getUpper() - 1).isZero()) {
        // We have in input interval of kind [0, 1). In this case we cannot
        // really help but return empty-set.
        return getEmpty();
      }

      // Compute the resulting range by excluding zero from Lower.
      return ConstantRange(
          APInt(getBitWidth(), (getUpper() - 1).countl_zero()),
          APInt(getBitWidth(), (getLower() + 1).countl_zero() + 1));
    } else if ((getUpper() - 1).isZero()) {
      // Compute the resulting range by excluding zero from Upper.
      return ConstantRange(Zero,
                           APInt(getBitWidth(), getLower().countl_zero() + 1));
    } else {
      return ConstantRange(Zero, APInt(getBitWidth(), getBitWidth()));
    }
  }

  // Zero is either safe or not in the range. The output range is composed by
  // the result of countLeadingZero of the two extremes.
  return getNonEmpty(APInt(getBitWidth(), getUnsignedMax().countl_zero()),
                     APInt(getBitWidth(), getUnsignedMin().countl_zero() + 1));
}

static ConstantRange getUnsignedCountTrailingZerosRange(const APInt &Lower,
                                                        const APInt &Upper) {
  assert(!ConstantRange(Lower, Upper).isWrappedSet() &&
         "Unexpected wrapped set.");
  assert(Lower != Upper && "Unexpected empty set.");
  unsigned BitWidth = Lower.getBitWidth();
  if (Lower + 1 == Upper)
    return ConstantRange(APInt(BitWidth, Lower.countr_zero()));
  if (Lower.isZero())
    return ConstantRange(APInt::getZero(BitWidth),
                         APInt(BitWidth, BitWidth + 1));

  // Calculate longest common prefix.
  unsigned LCPLength = (Lower ^ (Upper - 1)).countl_zero();
  // If Lower is {LCP, 000...}, the maximum is Lower.countr_zero().
  // Otherwise, the maximum is BitWidth - LCPLength - 1 ({LCP, 100...}).
  return ConstantRange(
      APInt::getZero(BitWidth),
      APInt(BitWidth,
            std::max(BitWidth - LCPLength - 1, Lower.countr_zero()) + 1));
}

ConstantRange ConstantRange::cttz(bool ZeroIsPoison) const {
  if (isEmptySet())
    return getEmpty();

  unsigned BitWidth = getBitWidth();
  APInt Zero = APInt::getZero(BitWidth);
  if (ZeroIsPoison && contains(Zero)) {
    // ZeroIsPoison is set, and zero is contained. We discern three cases, in
    // which a zero can appear:
    // 1) Lower is zero, handling cases of kind [0, 1), [0, 2), etc.
    // 2) Upper is zero, wrapped set, handling cases of kind [3, 0], etc.
    // 3) Zero contained in a wrapped set, e.g., [3, 2), [3, 1), etc.

    if (Lower.isZero()) {
      if (Upper == 1) {
        // We have in input interval of kind [0, 1). In this case we cannot
        // really help but return empty-set.
        return getEmpty();
      }

      // Compute the resulting range by excluding zero from Lower.
      return getUnsignedCountTrailingZerosRange(APInt(BitWidth, 1), Upper);
    } else if (Upper == 1) {
      // Compute the resulting range by excluding zero from Upper.
      return getUnsignedCountTrailingZerosRange(Lower, Zero);
    } else {
      ConstantRange CR1 = getUnsignedCountTrailingZerosRange(Lower, Zero);
      ConstantRange CR2 =
          getUnsignedCountTrailingZerosRange(APInt(BitWidth, 1), Upper);
      return CR1.unionWith(CR2);
    }
  }

  if (isFullSet())
    return getNonEmpty(Zero, APInt(BitWidth, BitWidth + 1));
  if (!isWrappedSet())
    return getUnsignedCountTrailingZerosRange(Lower, Upper);
  // The range is wrapped. We decompose it into two ranges, [0, Upper) and
  // [Lower, 0).
  // Handle [Lower, 0)
  ConstantRange CR1 = getUnsignedCountTrailingZerosRange(Lower, Zero);
  // Handle [0, Upper)
  ConstantRange CR2 = getUnsignedCountTrailingZerosRange(Zero, Upper);
  return CR1.unionWith(CR2);
}

static ConstantRange getUnsignedPopCountRange(const APInt &Lower,
                                              const APInt &Upper) {
  assert(!ConstantRange(Lower, Upper).isWrappedSet() &&
         "Unexpected wrapped set.");
  assert(Lower != Upper && "Unexpected empty set.");
  unsigned BitWidth = Lower.getBitWidth();
  if (Lower + 1 == Upper)
    return ConstantRange(APInt(BitWidth, Lower.popcount()));

  APInt Max = Upper - 1;
  // Calculate longest common prefix.
  unsigned LCPLength = (Lower ^ Max).countl_zero();
  unsigned LCPPopCount = Lower.getHiBits(LCPLength).popcount();
  // If Lower is {LCP, 000...}, the minimum is the popcount of LCP.
  // Otherwise, the minimum is the popcount of LCP + 1.
  unsigned MinBits =
      LCPPopCount + (Lower.countr_zero() < BitWidth - LCPLength ? 1 : 0);
  // If Max is {LCP, 111...}, the maximum is the popcount of LCP + (BitWidth -
  // length of LCP).
  // Otherwise, the minimum is the popcount of LCP + (BitWidth -
  // length of LCP - 1).
  unsigned MaxBits = LCPPopCount + (BitWidth - LCPLength) -
                     (Max.countr_one() < BitWidth - LCPLength ? 1 : 0);
  return ConstantRange(APInt(BitWidth, MinBits), APInt(BitWidth, MaxBits + 1));
}

ConstantRange ConstantRange::ctpop() const {
  if (isEmptySet())
    return getEmpty();

  unsigned BitWidth = getBitWidth();
  APInt Zero = APInt::getZero(BitWidth);
  if (isFullSet())
    return getNonEmpty(Zero, APInt(BitWidth, BitWidth + 1));
  if (!isWrappedSet())
    return getUnsignedPopCountRange(Lower, Upper);
  // The range is wrapped. We decompose it into two ranges, [0, Upper) and
  // [Lower, 0).
  // Handle [Lower, 0) == [Lower, Max]
  ConstantRange CR1 = ConstantRange(APInt(BitWidth, Lower.countl_one()),
                                    APInt(BitWidth, BitWidth + 1));
  // Handle [0, Upper)
  ConstantRange CR2 = getUnsignedPopCountRange(Zero, Upper);
  return CR1.unionWith(CR2);
}

ConstantRange::OverflowResult ConstantRange::unsignedAddMayOverflow(
    const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return OverflowResult::MayOverflow;

  APInt Min = getUnsignedMin(), Max = getUnsignedMax();
  APInt OtherMin = Other.getUnsignedMin(), OtherMax = Other.getUnsignedMax();

  // a u+ b overflows high iff a u> ~b.
  if (Min.ugt(~OtherMin))
    return OverflowResult::AlwaysOverflowsHigh;
  if (Max.ugt(~OtherMax))
    return OverflowResult::MayOverflow;
  return OverflowResult::NeverOverflows;
}

ConstantRange::OverflowResult ConstantRange::signedAddMayOverflow(
    const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return OverflowResult::MayOverflow;

  APInt Min = getSignedMin(), Max = getSignedMax();
  APInt OtherMin = Other.getSignedMin(), OtherMax = Other.getSignedMax();

  APInt SignedMin = APInt::getSignedMinValue(getBitWidth());
  APInt SignedMax = APInt::getSignedMaxValue(getBitWidth());

  // a s+ b overflows high iff a s>=0 && b s>= 0 && a s> smax - b.
  // a s+ b overflows low iff a s< 0 && b s< 0 && a s< smin - b.
  if (Min.isNonNegative() && OtherMin.isNonNegative() &&
      Min.sgt(SignedMax - OtherMin))
    return OverflowResult::AlwaysOverflowsHigh;
  if (Max.isNegative() && OtherMax.isNegative() &&
      Max.slt(SignedMin - OtherMax))
    return OverflowResult::AlwaysOverflowsLow;

  if (Max.isNonNegative() && OtherMax.isNonNegative() &&
      Max.sgt(SignedMax - OtherMax))
    return OverflowResult::MayOverflow;
  if (Min.isNegative() && OtherMin.isNegative() &&
      Min.slt(SignedMin - OtherMin))
    return OverflowResult::MayOverflow;

  return OverflowResult::NeverOverflows;
}

ConstantRange::OverflowResult ConstantRange::unsignedSubMayOverflow(
    const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return OverflowResult::MayOverflow;

  APInt Min = getUnsignedMin(), Max = getUnsignedMax();
  APInt OtherMin = Other.getUnsignedMin(), OtherMax = Other.getUnsignedMax();

  // a u- b overflows low iff a u< b.
  if (Max.ult(OtherMin))
    return OverflowResult::AlwaysOverflowsLow;
  if (Min.ult(OtherMax))
    return OverflowResult::MayOverflow;
  return OverflowResult::NeverOverflows;
}

ConstantRange::OverflowResult ConstantRange::signedSubMayOverflow(
    const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return OverflowResult::MayOverflow;

  APInt Min = getSignedMin(), Max = getSignedMax();
  APInt OtherMin = Other.getSignedMin(), OtherMax = Other.getSignedMax();

  APInt SignedMin = APInt::getSignedMinValue(getBitWidth());
  APInt SignedMax = APInt::getSignedMaxValue(getBitWidth());

  // a s- b overflows high iff a s>=0 && b s< 0 && a s> smax + b.
  // a s- b overflows low iff a s< 0 && b s>= 0 && a s< smin + b.
  if (Min.isNonNegative() && OtherMax.isNegative() &&
      Min.sgt(SignedMax + OtherMax))
    return OverflowResult::AlwaysOverflowsHigh;
  if (Max.isNegative() && OtherMin.isNonNegative() &&
      Max.slt(SignedMin + OtherMin))
    return OverflowResult::AlwaysOverflowsLow;

  if (Max.isNonNegative() && OtherMin.isNegative() &&
      Max.sgt(SignedMax + OtherMin))
    return OverflowResult::MayOverflow;
  if (Min.isNegative() && OtherMax.isNonNegative() &&
      Min.slt(SignedMin + OtherMax))
    return OverflowResult::MayOverflow;

  return OverflowResult::NeverOverflows;
}

ConstantRange::OverflowResult ConstantRange::unsignedMulMayOverflow(
    const ConstantRange &Other) const {
  if (isEmptySet() || Other.isEmptySet())
    return OverflowResult::MayOverflow;

  APInt Min = getUnsignedMin(), Max = getUnsignedMax();
  APInt OtherMin = Other.getUnsignedMin(), OtherMax = Other.getUnsignedMax();
  bool Overflow;

  (void) Min.umul_ov(OtherMin, Overflow);
  if (Overflow)
    return OverflowResult::AlwaysOverflowsHigh;

  (void) Max.umul_ov(OtherMax, Overflow);
  if (Overflow)
    return OverflowResult::MayOverflow;

  return OverflowResult::NeverOverflows;
}

void ConstantRange::print(raw_ostream &OS) const {
  if (isFullSet())
    OS << "full-set";
  else if (isEmptySet())
    OS << "empty-set";
  else
    OS << "[" << Lower << "," << Upper << ")";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ConstantRange::dump() const {
  print(dbgs());
}
#endif

ConstantRange llvm::getConstantRangeFromMetadata(const MDNode &Ranges) {
  const unsigned NumRanges = Ranges.getNumOperands() / 2;
  assert(NumRanges >= 1 && "Must have at least one range!");
  assert(Ranges.getNumOperands() % 2 == 0 && "Must be a sequence of pairs");

  auto *FirstLow = mdconst::extract<ConstantInt>(Ranges.getOperand(0));
  auto *FirstHigh = mdconst::extract<ConstantInt>(Ranges.getOperand(1));

  ConstantRange CR(FirstLow->getValue(), FirstHigh->getValue());

  for (unsigned i = 1; i < NumRanges; ++i) {
    auto *Low = mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 0));
    auto *High = mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 1));

    // Note: unionWith will potentially create a range that contains values not
    // contained in any of the original N ranges.
    CR = CR.unionWith(ConstantRange(Low->getValue(), High->getValue()));
  }

  return CR;
}
