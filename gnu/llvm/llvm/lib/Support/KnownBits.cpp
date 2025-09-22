//===-- KnownBits.cpp - Stores known zeros/ones ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class for representing known zeros and ones used by
// computeKnownBits.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/KnownBits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

static KnownBits computeForAddCarry(const KnownBits &LHS, const KnownBits &RHS,
                                    bool CarryZero, bool CarryOne) {

  APInt PossibleSumZero = LHS.getMaxValue() + RHS.getMaxValue() + !CarryZero;
  APInt PossibleSumOne = LHS.getMinValue() + RHS.getMinValue() + CarryOne;

  // Compute known bits of the carry.
  APInt CarryKnownZero = ~(PossibleSumZero ^ LHS.Zero ^ RHS.Zero);
  APInt CarryKnownOne = PossibleSumOne ^ LHS.One ^ RHS.One;

  // Compute set of known bits (where all three relevant bits are known).
  APInt LHSKnownUnion = LHS.Zero | LHS.One;
  APInt RHSKnownUnion = RHS.Zero | RHS.One;
  APInt CarryKnownUnion = std::move(CarryKnownZero) | CarryKnownOne;
  APInt Known = std::move(LHSKnownUnion) & RHSKnownUnion & CarryKnownUnion;

  // Compute known bits of the result.
  KnownBits KnownOut;
  KnownOut.Zero = ~std::move(PossibleSumZero) & Known;
  KnownOut.One = std::move(PossibleSumOne) & Known;
  return KnownOut;
}

KnownBits KnownBits::computeForAddCarry(
    const KnownBits &LHS, const KnownBits &RHS, const KnownBits &Carry) {
  assert(Carry.getBitWidth() == 1 && "Carry must be 1-bit");
  return ::computeForAddCarry(
      LHS, RHS, Carry.Zero.getBoolValue(), Carry.One.getBoolValue());
}

KnownBits KnownBits::computeForAddSub(bool Add, bool NSW, bool NUW,
                                      const KnownBits &LHS,
                                      const KnownBits &RHS) {
  unsigned BitWidth = LHS.getBitWidth();
  KnownBits KnownOut(BitWidth);
  // This can be a relatively expensive helper, so optimistically save some
  // work.
  if (LHS.isUnknown() && RHS.isUnknown())
    return KnownOut;

  if (!LHS.isUnknown() && !RHS.isUnknown()) {
    if (Add) {
      // Sum = LHS + RHS + 0
      KnownOut = ::computeForAddCarry(LHS, RHS, /*CarryZero=*/true,
                                      /*CarryOne=*/false);
    } else {
      // Sum = LHS + ~RHS + 1
      KnownBits NotRHS = RHS;
      std::swap(NotRHS.Zero, NotRHS.One);
      KnownOut = ::computeForAddCarry(LHS, NotRHS, /*CarryZero=*/false,
                                      /*CarryOne=*/true);
    }
  }

  // Handle add/sub given nsw and/or nuw.
  if (NUW) {
    if (Add) {
      // (add nuw X, Y)
      APInt MinVal = LHS.getMinValue().uadd_sat(RHS.getMinValue());
      // None of the adds can end up overflowing, so min consecutive highbits
      // in minimum possible of X + Y must all remain set.
      if (NSW) {
        unsigned NumBits = MinVal.trunc(BitWidth - 1).countl_one();
        // If we have NSW as well, we also know we can't overflow the signbit so
        // can start counting from 1 bit back.
        KnownOut.One.setBits(BitWidth - 1 - NumBits, BitWidth - 1);
      }
      KnownOut.One.setHighBits(MinVal.countl_one());
    } else {
      // (sub nuw X, Y)
      APInt MaxVal = LHS.getMaxValue().usub_sat(RHS.getMinValue());
      // None of the subs can overflow at any point, so any common high bits
      // will subtract away and result in zeros.
      if (NSW) {
        // If we have NSW as well, we also know we can't overflow the signbit so
        // can start counting from 1 bit back.
        unsigned NumBits = MaxVal.trunc(BitWidth - 1).countl_zero();
        KnownOut.Zero.setBits(BitWidth - 1 - NumBits, BitWidth - 1);
      }
      KnownOut.Zero.setHighBits(MaxVal.countl_zero());
    }
  }

  if (NSW) {
    APInt MinVal;
    APInt MaxVal;
    if (Add) {
      // (add nsw X, Y)
      MinVal = LHS.getSignedMinValue().sadd_sat(RHS.getSignedMinValue());
      MaxVal = LHS.getSignedMaxValue().sadd_sat(RHS.getSignedMaxValue());
    } else {
      // (sub nsw X, Y)
      MinVal = LHS.getSignedMinValue().ssub_sat(RHS.getSignedMaxValue());
      MaxVal = LHS.getSignedMaxValue().ssub_sat(RHS.getSignedMinValue());
    }
    if (MinVal.isNonNegative()) {
      // If min is non-negative, result will always be non-neg (can't overflow
      // around).
      unsigned NumBits = MinVal.trunc(BitWidth - 1).countl_one();
      KnownOut.One.setBits(BitWidth - 1 - NumBits, BitWidth - 1);
      KnownOut.Zero.setSignBit();
    }
    if (MaxVal.isNegative()) {
      // If max is negative, result will always be neg (can't overflow around).
      unsigned NumBits = MaxVal.trunc(BitWidth - 1).countl_zero();
      KnownOut.Zero.setBits(BitWidth - 1 - NumBits, BitWidth - 1);
      KnownOut.One.setSignBit();
    }
  }

  // Just return 0 if the nsw/nuw is violated and we have poison.
  if (KnownOut.hasConflict())
    KnownOut.setAllZero();
  return KnownOut;
}

KnownBits KnownBits::computeForSubBorrow(const KnownBits &LHS, KnownBits RHS,
                                         const KnownBits &Borrow) {
  assert(Borrow.getBitWidth() == 1 && "Borrow must be 1-bit");

  // LHS - RHS = LHS + ~RHS + 1
  // Carry 1 - Borrow in ::computeForAddCarry
  std::swap(RHS.Zero, RHS.One);
  return ::computeForAddCarry(LHS, RHS,
                              /*CarryZero=*/Borrow.One.getBoolValue(),
                              /*CarryOne=*/Borrow.Zero.getBoolValue());
}

KnownBits KnownBits::sextInReg(unsigned SrcBitWidth) const {
  unsigned BitWidth = getBitWidth();
  assert(0 < SrcBitWidth && SrcBitWidth <= BitWidth &&
         "Illegal sext-in-register");

  if (SrcBitWidth == BitWidth)
    return *this;

  unsigned ExtBits = BitWidth - SrcBitWidth;
  KnownBits Result;
  Result.One = One << ExtBits;
  Result.Zero = Zero << ExtBits;
  Result.One.ashrInPlace(ExtBits);
  Result.Zero.ashrInPlace(ExtBits);
  return Result;
}

KnownBits KnownBits::makeGE(const APInt &Val) const {
  // Count the number of leading bit positions where our underlying value is
  // known to be less than or equal to Val.
  unsigned N = (Zero | Val).countl_one();

  // For each of those bit positions, if Val has a 1 in that bit then our
  // underlying value must also have a 1.
  APInt MaskedVal(Val);
  MaskedVal.clearLowBits(getBitWidth() - N);
  return KnownBits(Zero, One | MaskedVal);
}

KnownBits KnownBits::umax(const KnownBits &LHS, const KnownBits &RHS) {
  // If we can prove that LHS >= RHS then use LHS as the result. Likewise for
  // RHS. Ideally our caller would already have spotted these cases and
  // optimized away the umax operation, but we handle them here for
  // completeness.
  if (LHS.getMinValue().uge(RHS.getMaxValue()))
    return LHS;
  if (RHS.getMinValue().uge(LHS.getMaxValue()))
    return RHS;

  // If the result of the umax is LHS then it must be greater than or equal to
  // the minimum possible value of RHS. Likewise for RHS. Any known bits that
  // are common to these two values are also known in the result.
  KnownBits L = LHS.makeGE(RHS.getMinValue());
  KnownBits R = RHS.makeGE(LHS.getMinValue());
  return L.intersectWith(R);
}

KnownBits KnownBits::umin(const KnownBits &LHS, const KnownBits &RHS) {
  // Flip the range of values: [0, 0xFFFFFFFF] <-> [0xFFFFFFFF, 0]
  auto Flip = [](const KnownBits &Val) { return KnownBits(Val.One, Val.Zero); };
  return Flip(umax(Flip(LHS), Flip(RHS)));
}

KnownBits KnownBits::smax(const KnownBits &LHS, const KnownBits &RHS) {
  // Flip the range of values: [-0x80000000, 0x7FFFFFFF] <-> [0, 0xFFFFFFFF]
  auto Flip = [](const KnownBits &Val) {
    unsigned SignBitPosition = Val.getBitWidth() - 1;
    APInt Zero = Val.Zero;
    APInt One = Val.One;
    Zero.setBitVal(SignBitPosition, Val.One[SignBitPosition]);
    One.setBitVal(SignBitPosition, Val.Zero[SignBitPosition]);
    return KnownBits(Zero, One);
  };
  return Flip(umax(Flip(LHS), Flip(RHS)));
}

KnownBits KnownBits::smin(const KnownBits &LHS, const KnownBits &RHS) {
  // Flip the range of values: [-0x80000000, 0x7FFFFFFF] <-> [0xFFFFFFFF, 0]
  auto Flip = [](const KnownBits &Val) {
    unsigned SignBitPosition = Val.getBitWidth() - 1;
    APInt Zero = Val.One;
    APInt One = Val.Zero;
    Zero.setBitVal(SignBitPosition, Val.Zero[SignBitPosition]);
    One.setBitVal(SignBitPosition, Val.One[SignBitPosition]);
    return KnownBits(Zero, One);
  };
  return Flip(umax(Flip(LHS), Flip(RHS)));
}

KnownBits KnownBits::abdu(const KnownBits &LHS, const KnownBits &RHS) {
  // If we know which argument is larger, return (sub LHS, RHS) or
  // (sub RHS, LHS) directly.
  if (LHS.getMinValue().uge(RHS.getMaxValue()))
    return computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/false, LHS,
                            RHS);
  if (RHS.getMinValue().uge(LHS.getMaxValue()))
    return computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/false, RHS,
                            LHS);

  // By construction, the subtraction in abdu never has unsigned overflow.
  // Find the common bits between (sub nuw LHS, RHS) and (sub nuw RHS, LHS).
  KnownBits Diff0 =
      computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/true, LHS, RHS);
  KnownBits Diff1 =
      computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/true, RHS, LHS);
  return Diff0.intersectWith(Diff1);
}

KnownBits KnownBits::abds(KnownBits LHS, KnownBits RHS) {
  // If we know which argument is larger, return (sub LHS, RHS) or
  // (sub RHS, LHS) directly.
  if (LHS.getSignedMinValue().sge(RHS.getSignedMaxValue()))
    return computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/false, LHS,
                            RHS);
  if (RHS.getSignedMinValue().sge(LHS.getSignedMaxValue()))
    return computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/false, RHS,
                            LHS);

  // Shift both arguments from the signed range to the unsigned range, e.g. from
  // [-0x80, 0x7F] to [0, 0xFF]. This allows us to use "sub nuw" below just like
  // abdu does.
  // Note that we can't just use "sub nsw" instead because abds has signed
  // inputs but an unsigned result, which makes the overflow conditions
  // different.
  unsigned SignBitPosition = LHS.getBitWidth() - 1;
  for (auto Arg : {&LHS, &RHS}) {
    bool Tmp = Arg->Zero[SignBitPosition];
    Arg->Zero.setBitVal(SignBitPosition, Arg->One[SignBitPosition]);
    Arg->One.setBitVal(SignBitPosition, Tmp);
  }

  // Find the common bits between (sub nuw LHS, RHS) and (sub nuw RHS, LHS).
  KnownBits Diff0 =
      computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/true, LHS, RHS);
  KnownBits Diff1 =
      computeForAddSub(/*Add=*/false, /*NSW=*/false, /*NUW=*/true, RHS, LHS);
  return Diff0.intersectWith(Diff1);
}

static unsigned getMaxShiftAmount(const APInt &MaxValue, unsigned BitWidth) {
  if (isPowerOf2_32(BitWidth))
    return MaxValue.extractBitsAsZExtValue(Log2_32(BitWidth), 0);
  // This is only an approximate upper bound.
  return MaxValue.getLimitedValue(BitWidth - 1);
}

KnownBits KnownBits::shl(const KnownBits &LHS, const KnownBits &RHS, bool NUW,
                         bool NSW, bool ShAmtNonZero) {
  unsigned BitWidth = LHS.getBitWidth();
  auto ShiftByConst = [&](const KnownBits &LHS, unsigned ShiftAmt) {
    KnownBits Known;
    bool ShiftedOutZero, ShiftedOutOne;
    Known.Zero = LHS.Zero.ushl_ov(ShiftAmt, ShiftedOutZero);
    Known.Zero.setLowBits(ShiftAmt);
    Known.One = LHS.One.ushl_ov(ShiftAmt, ShiftedOutOne);

    // All cases returning poison have been handled by MaxShiftAmount already.
    if (NSW) {
      if (NUW && ShiftAmt != 0)
        // NUW means we can assume anything shifted out was a zero.
        ShiftedOutZero = true;

      if (ShiftedOutZero)
        Known.makeNonNegative();
      else if (ShiftedOutOne)
        Known.makeNegative();
    }
    return Known;
  };

  // Fast path for a common case when LHS is completely unknown.
  KnownBits Known(BitWidth);
  unsigned MinShiftAmount = RHS.getMinValue().getLimitedValue(BitWidth);
  if (MinShiftAmount == 0 && ShAmtNonZero)
    MinShiftAmount = 1;
  if (LHS.isUnknown()) {
    Known.Zero.setLowBits(MinShiftAmount);
    if (NUW && NSW && MinShiftAmount != 0)
      Known.makeNonNegative();
    return Known;
  }

  // Determine maximum shift amount, taking NUW/NSW flags into account.
  APInt MaxValue = RHS.getMaxValue();
  unsigned MaxShiftAmount = getMaxShiftAmount(MaxValue, BitWidth);
  if (NUW && NSW)
    MaxShiftAmount = std::min(MaxShiftAmount, LHS.countMaxLeadingZeros() - 1);
  if (NUW)
    MaxShiftAmount = std::min(MaxShiftAmount, LHS.countMaxLeadingZeros());
  if (NSW)
    MaxShiftAmount = std::min(
        MaxShiftAmount,
        std::max(LHS.countMaxLeadingZeros(), LHS.countMaxLeadingOnes()) - 1);

  // Fast path for common case where the shift amount is unknown.
  if (MinShiftAmount == 0 && MaxShiftAmount == BitWidth - 1 &&
      isPowerOf2_32(BitWidth)) {
    Known.Zero.setLowBits(LHS.countMinTrailingZeros());
    if (LHS.isAllOnes())
      Known.One.setSignBit();
    if (NSW) {
      if (LHS.isNonNegative())
        Known.makeNonNegative();
      if (LHS.isNegative())
        Known.makeNegative();
    }
    return Known;
  }

  // Find the common bits from all possible shifts.
  unsigned ShiftAmtZeroMask = RHS.Zero.zextOrTrunc(32).getZExtValue();
  unsigned ShiftAmtOneMask = RHS.One.zextOrTrunc(32).getZExtValue();
  Known.Zero.setAllBits();
  Known.One.setAllBits();
  for (unsigned ShiftAmt = MinShiftAmount; ShiftAmt <= MaxShiftAmount;
       ++ShiftAmt) {
    // Skip if the shift amount is impossible.
    if ((ShiftAmtZeroMask & ShiftAmt) != 0 ||
        (ShiftAmtOneMask | ShiftAmt) != ShiftAmt)
      continue;
    Known = Known.intersectWith(ShiftByConst(LHS, ShiftAmt));
    if (Known.isUnknown())
      break;
  }

  // All shift amounts may result in poison.
  if (Known.hasConflict())
    Known.setAllZero();
  return Known;
}

KnownBits KnownBits::lshr(const KnownBits &LHS, const KnownBits &RHS,
                          bool ShAmtNonZero, bool Exact) {
  unsigned BitWidth = LHS.getBitWidth();
  auto ShiftByConst = [&](const KnownBits &LHS, unsigned ShiftAmt) {
    KnownBits Known = LHS;
    Known.Zero.lshrInPlace(ShiftAmt);
    Known.One.lshrInPlace(ShiftAmt);
    // High bits are known zero.
    Known.Zero.setHighBits(ShiftAmt);
    return Known;
  };

  // Fast path for a common case when LHS is completely unknown.
  KnownBits Known(BitWidth);
  unsigned MinShiftAmount = RHS.getMinValue().getLimitedValue(BitWidth);
  if (MinShiftAmount == 0 && ShAmtNonZero)
    MinShiftAmount = 1;
  if (LHS.isUnknown()) {
    Known.Zero.setHighBits(MinShiftAmount);
    return Known;
  }

  // Find the common bits from all possible shifts.
  APInt MaxValue = RHS.getMaxValue();
  unsigned MaxShiftAmount = getMaxShiftAmount(MaxValue, BitWidth);

  // If exact, bound MaxShiftAmount to first known 1 in LHS.
  if (Exact) {
    unsigned FirstOne = LHS.countMaxTrailingZeros();
    if (FirstOne < MinShiftAmount) {
      // Always poison. Return zero because we don't like returning conflict.
      Known.setAllZero();
      return Known;
    }
    MaxShiftAmount = std::min(MaxShiftAmount, FirstOne);
  }

  unsigned ShiftAmtZeroMask = RHS.Zero.zextOrTrunc(32).getZExtValue();
  unsigned ShiftAmtOneMask = RHS.One.zextOrTrunc(32).getZExtValue();
  Known.Zero.setAllBits();
  Known.One.setAllBits();
  for (unsigned ShiftAmt = MinShiftAmount; ShiftAmt <= MaxShiftAmount;
       ++ShiftAmt) {
    // Skip if the shift amount is impossible.
    if ((ShiftAmtZeroMask & ShiftAmt) != 0 ||
        (ShiftAmtOneMask | ShiftAmt) != ShiftAmt)
      continue;
    Known = Known.intersectWith(ShiftByConst(LHS, ShiftAmt));
    if (Known.isUnknown())
      break;
  }

  // All shift amounts may result in poison.
  if (Known.hasConflict())
    Known.setAllZero();
  return Known;
}

KnownBits KnownBits::ashr(const KnownBits &LHS, const KnownBits &RHS,
                          bool ShAmtNonZero, bool Exact) {
  unsigned BitWidth = LHS.getBitWidth();
  auto ShiftByConst = [&](const KnownBits &LHS, unsigned ShiftAmt) {
    KnownBits Known = LHS;
    Known.Zero.ashrInPlace(ShiftAmt);
    Known.One.ashrInPlace(ShiftAmt);
    return Known;
  };

  // Fast path for a common case when LHS is completely unknown.
  KnownBits Known(BitWidth);
  unsigned MinShiftAmount = RHS.getMinValue().getLimitedValue(BitWidth);
  if (MinShiftAmount == 0 && ShAmtNonZero)
    MinShiftAmount = 1;
  if (LHS.isUnknown()) {
    if (MinShiftAmount == BitWidth) {
      // Always poison. Return zero because we don't like returning conflict.
      Known.setAllZero();
      return Known;
    }
    return Known;
  }

  // Find the common bits from all possible shifts.
  APInt MaxValue = RHS.getMaxValue();
  unsigned MaxShiftAmount = getMaxShiftAmount(MaxValue, BitWidth);

  // If exact, bound MaxShiftAmount to first known 1 in LHS.
  if (Exact) {
    unsigned FirstOne = LHS.countMaxTrailingZeros();
    if (FirstOne < MinShiftAmount) {
      // Always poison. Return zero because we don't like returning conflict.
      Known.setAllZero();
      return Known;
    }
    MaxShiftAmount = std::min(MaxShiftAmount, FirstOne);
  }

  unsigned ShiftAmtZeroMask = RHS.Zero.zextOrTrunc(32).getZExtValue();
  unsigned ShiftAmtOneMask = RHS.One.zextOrTrunc(32).getZExtValue();
  Known.Zero.setAllBits();
  Known.One.setAllBits();
  for (unsigned ShiftAmt = MinShiftAmount; ShiftAmt <= MaxShiftAmount;
      ++ShiftAmt) {
    // Skip if the shift amount is impossible.
    if ((ShiftAmtZeroMask & ShiftAmt) != 0 ||
        (ShiftAmtOneMask | ShiftAmt) != ShiftAmt)
      continue;
    Known = Known.intersectWith(ShiftByConst(LHS, ShiftAmt));
    if (Known.isUnknown())
      break;
  }

  // All shift amounts may result in poison.
  if (Known.hasConflict())
    Known.setAllZero();
  return Known;
}

std::optional<bool> KnownBits::eq(const KnownBits &LHS, const KnownBits &RHS) {
  if (LHS.isConstant() && RHS.isConstant())
    return std::optional<bool>(LHS.getConstant() == RHS.getConstant());
  if (LHS.One.intersects(RHS.Zero) || RHS.One.intersects(LHS.Zero))
    return std::optional<bool>(false);
  return std::nullopt;
}

std::optional<bool> KnownBits::ne(const KnownBits &LHS, const KnownBits &RHS) {
  if (std::optional<bool> KnownEQ = eq(LHS, RHS))
    return std::optional<bool>(!*KnownEQ);
  return std::nullopt;
}

std::optional<bool> KnownBits::ugt(const KnownBits &LHS, const KnownBits &RHS) {
  // LHS >u RHS -> false if umax(LHS) <= umax(RHS)
  if (LHS.getMaxValue().ule(RHS.getMinValue()))
    return std::optional<bool>(false);
  // LHS >u RHS -> true if umin(LHS) > umax(RHS)
  if (LHS.getMinValue().ugt(RHS.getMaxValue()))
    return std::optional<bool>(true);
  return std::nullopt;
}

std::optional<bool> KnownBits::uge(const KnownBits &LHS, const KnownBits &RHS) {
  if (std::optional<bool> IsUGT = ugt(RHS, LHS))
    return std::optional<bool>(!*IsUGT);
  return std::nullopt;
}

std::optional<bool> KnownBits::ult(const KnownBits &LHS, const KnownBits &RHS) {
  return ugt(RHS, LHS);
}

std::optional<bool> KnownBits::ule(const KnownBits &LHS, const KnownBits &RHS) {
  return uge(RHS, LHS);
}

std::optional<bool> KnownBits::sgt(const KnownBits &LHS, const KnownBits &RHS) {
  // LHS >s RHS -> false if smax(LHS) <= smax(RHS)
  if (LHS.getSignedMaxValue().sle(RHS.getSignedMinValue()))
    return std::optional<bool>(false);
  // LHS >s RHS -> true if smin(LHS) > smax(RHS)
  if (LHS.getSignedMinValue().sgt(RHS.getSignedMaxValue()))
    return std::optional<bool>(true);
  return std::nullopt;
}

std::optional<bool> KnownBits::sge(const KnownBits &LHS, const KnownBits &RHS) {
  if (std::optional<bool> KnownSGT = sgt(RHS, LHS))
    return std::optional<bool>(!*KnownSGT);
  return std::nullopt;
}

std::optional<bool> KnownBits::slt(const KnownBits &LHS, const KnownBits &RHS) {
  return sgt(RHS, LHS);
}

std::optional<bool> KnownBits::sle(const KnownBits &LHS, const KnownBits &RHS) {
  return sge(RHS, LHS);
}

KnownBits KnownBits::abs(bool IntMinIsPoison) const {
  // If the source's MSB is zero then we know the rest of the bits already.
  if (isNonNegative())
    return *this;

  // Absolute value preserves trailing zero count.
  KnownBits KnownAbs(getBitWidth());

  // If the input is negative, then abs(x) == -x.
  if (isNegative()) {
    KnownBits Tmp = *this;
    // Special case for IntMinIsPoison. We know the sign bit is set and we know
    // all the rest of the bits except one to be zero. Since we have
    // IntMinIsPoison, that final bit MUST be a one, as otherwise the input is
    // INT_MIN.
    if (IntMinIsPoison && (Zero.popcount() + 2) == getBitWidth())
      Tmp.One.setBit(countMinTrailingZeros());

    KnownAbs = computeForAddSub(
        /*Add*/ false, IntMinIsPoison, /*NUW=*/false,
        KnownBits::makeConstant(APInt(getBitWidth(), 0)), Tmp);

    // One more special case for IntMinIsPoison. If we don't know any ones other
    // than the signbit, we know for certain that all the unknowns can't be
    // zero. So if we know high zero bits, but have unknown low bits, we know
    // for certain those high-zero bits will end up as one. This is because,
    // the low bits can't be all zeros, so the +1 in (~x + 1) cannot carry up
    // to the high bits. If we know a known INT_MIN input skip this. The result
    // is poison anyways.
    if (IntMinIsPoison && Tmp.countMinPopulation() == 1 &&
        Tmp.countMaxPopulation() != 1) {
      Tmp.One.clearSignBit();
      Tmp.Zero.setSignBit();
      KnownAbs.One.setBits(getBitWidth() - Tmp.countMinLeadingZeros(),
                           getBitWidth() - 1);
    }

  } else {
    unsigned MaxTZ = countMaxTrailingZeros();
    unsigned MinTZ = countMinTrailingZeros();

    KnownAbs.Zero.setLowBits(MinTZ);
    // If we know the lowest set 1, then preserve it.
    if (MaxTZ == MinTZ && MaxTZ < getBitWidth())
      KnownAbs.One.setBit(MaxTZ);

    // We only know that the absolute values's MSB will be zero if INT_MIN is
    // poison, or there is a set bit that isn't the sign bit (otherwise it could
    // be INT_MIN).
    if (IntMinIsPoison || (!One.isZero() && !One.isMinSignedValue())) {
      KnownAbs.One.clearSignBit();
      KnownAbs.Zero.setSignBit();
    }
  }

  return KnownAbs;
}

static KnownBits computeForSatAddSub(bool Add, bool Signed,
                                     const KnownBits &LHS,
                                     const KnownBits &RHS) {
  // We don't see NSW even for sadd/ssub as we want to check if the result has
  // signed overflow.
  KnownBits Res =
      KnownBits::computeForAddSub(Add, /*NSW=*/false, /*NUW=*/false, LHS, RHS);
  unsigned BitWidth = Res.getBitWidth();
  auto SignBitKnown = [&](const KnownBits &K) {
    return K.Zero[BitWidth - 1] || K.One[BitWidth - 1];
  };
  std::optional<bool> Overflow;

  if (Signed) {
    // If we can actually detect overflow do so. Otherwise leave Overflow as
    // nullopt (we assume it may have happened).
    if (SignBitKnown(LHS) && SignBitKnown(RHS) && SignBitKnown(Res)) {
      if (Add) {
        // sadd.sat
        Overflow = (LHS.isNonNegative() == RHS.isNonNegative() &&
                    Res.isNonNegative() != LHS.isNonNegative());
      } else {
        // ssub.sat
        Overflow = (LHS.isNonNegative() != RHS.isNonNegative() &&
                    Res.isNonNegative() != LHS.isNonNegative());
      }
    }
  } else if (Add) {
    // uadd.sat
    bool Of;
    (void)LHS.getMaxValue().uadd_ov(RHS.getMaxValue(), Of);
    if (!Of) {
      Overflow = false;
    } else {
      (void)LHS.getMinValue().uadd_ov(RHS.getMinValue(), Of);
      if (Of)
        Overflow = true;
    }
  } else {
    // usub.sat
    bool Of;
    (void)LHS.getMinValue().usub_ov(RHS.getMaxValue(), Of);
    if (!Of) {
      Overflow = false;
    } else {
      (void)LHS.getMaxValue().usub_ov(RHS.getMinValue(), Of);
      if (Of)
        Overflow = true;
    }
  }

  if (Signed) {
    if (Add) {
      if (LHS.isNonNegative() && RHS.isNonNegative()) {
        // Pos + Pos -> Pos
        Res.One.clearSignBit();
        Res.Zero.setSignBit();
      }
      if (LHS.isNegative() && RHS.isNegative()) {
        // Neg + Neg -> Neg
        Res.One.setSignBit();
        Res.Zero.clearSignBit();
      }
    } else {
      if (LHS.isNegative() && RHS.isNonNegative()) {
        // Neg - Pos -> Neg
        Res.One.setSignBit();
        Res.Zero.clearSignBit();
      } else if (LHS.isNonNegative() && RHS.isNegative()) {
        // Pos - Neg -> Pos
        Res.One.clearSignBit();
        Res.Zero.setSignBit();
      }
    }
  } else {
    // Add: Leading ones of either operand are preserved.
    // Sub: Leading zeros of LHS and leading ones of RHS are preserved
    // as leading zeros in the result.
    unsigned LeadingKnown;
    if (Add)
      LeadingKnown =
          std::max(LHS.countMinLeadingOnes(), RHS.countMinLeadingOnes());
    else
      LeadingKnown =
          std::max(LHS.countMinLeadingZeros(), RHS.countMinLeadingOnes());

    // We select between the operation result and all-ones/zero
    // respectively, so we can preserve known ones/zeros.
    APInt Mask = APInt::getHighBitsSet(BitWidth, LeadingKnown);
    if (Add) {
      Res.One |= Mask;
      Res.Zero &= ~Mask;
    } else {
      Res.Zero |= Mask;
      Res.One &= ~Mask;
    }
  }

  if (Overflow) {
    // We know whether or not we overflowed.
    if (!(*Overflow)) {
      // No overflow.
      return Res;
    }

    // We overflowed
    APInt C;
    if (Signed) {
      // sadd.sat / ssub.sat
      assert(SignBitKnown(LHS) &&
             "We somehow know overflow without knowing input sign");
      C = LHS.isNegative() ? APInt::getSignedMinValue(BitWidth)
                           : APInt::getSignedMaxValue(BitWidth);
    } else if (Add) {
      // uadd.sat
      C = APInt::getMaxValue(BitWidth);
    } else {
      // uadd.sat
      C = APInt::getMinValue(BitWidth);
    }

    Res.One = C;
    Res.Zero = ~C;
    return Res;
  }

  // We don't know if we overflowed.
  if (Signed) {
    // sadd.sat/ssub.sat
    // We can keep our information about the sign bits.
    Res.Zero.clearLowBits(BitWidth - 1);
    Res.One.clearLowBits(BitWidth - 1);
  } else if (Add) {
    // uadd.sat
    // We need to clear all the known zeros as we can only use the leading ones.
    Res.Zero.clearAllBits();
  } else {
    // usub.sat
    // We need to clear all the known ones as we can only use the leading zero.
    Res.One.clearAllBits();
  }

  return Res;
}

KnownBits KnownBits::sadd_sat(const KnownBits &LHS, const KnownBits &RHS) {
  return computeForSatAddSub(/*Add*/ true, /*Signed*/ true, LHS, RHS);
}
KnownBits KnownBits::ssub_sat(const KnownBits &LHS, const KnownBits &RHS) {
  return computeForSatAddSub(/*Add*/ false, /*Signed*/ true, LHS, RHS);
}
KnownBits KnownBits::uadd_sat(const KnownBits &LHS, const KnownBits &RHS) {
  return computeForSatAddSub(/*Add*/ true, /*Signed*/ false, LHS, RHS);
}
KnownBits KnownBits::usub_sat(const KnownBits &LHS, const KnownBits &RHS) {
  return computeForSatAddSub(/*Add*/ false, /*Signed*/ false, LHS, RHS);
}

static KnownBits avgCompute(KnownBits LHS, KnownBits RHS, bool IsCeil,
                            bool IsSigned) {
  unsigned BitWidth = LHS.getBitWidth();
  LHS = IsSigned ? LHS.sext(BitWidth + 1) : LHS.zext(BitWidth + 1);
  RHS = IsSigned ? RHS.sext(BitWidth + 1) : RHS.zext(BitWidth + 1);
  LHS =
      computeForAddCarry(LHS, RHS, /*CarryZero*/ !IsCeil, /*CarryOne*/ IsCeil);
  LHS = LHS.extractBits(BitWidth, 1);
  return LHS;
}

KnownBits KnownBits::avgFloorS(const KnownBits &LHS, const KnownBits &RHS) {
  return avgCompute(LHS, RHS, /* IsCeil */ false,
                    /* IsSigned */ true);
}

KnownBits KnownBits::avgFloorU(const KnownBits &LHS, const KnownBits &RHS) {
  return avgCompute(LHS, RHS, /* IsCeil */ false,
                    /* IsSigned */ false);
}

KnownBits KnownBits::avgCeilS(const KnownBits &LHS, const KnownBits &RHS) {
  return avgCompute(LHS, RHS, /* IsCeil */ true,
                    /* IsSigned */ true);
}

KnownBits KnownBits::avgCeilU(const KnownBits &LHS, const KnownBits &RHS) {
  return avgCompute(LHS, RHS, /* IsCeil */ true,
                    /* IsSigned */ false);
}

KnownBits KnownBits::mul(const KnownBits &LHS, const KnownBits &RHS,
                         bool NoUndefSelfMultiply) {
  unsigned BitWidth = LHS.getBitWidth();
  assert(BitWidth == RHS.getBitWidth() && "Operand mismatch");
  assert((!NoUndefSelfMultiply || LHS == RHS) &&
         "Self multiplication knownbits mismatch");

  // Compute the high known-0 bits by multiplying the unsigned max of each side.
  // Conservatively, M active bits * N active bits results in M + N bits in the
  // result. But if we know a value is a power-of-2 for example, then this
  // computes one more leading zero.
  // TODO: This could be generalized to number of sign bits (negative numbers).
  APInt UMaxLHS = LHS.getMaxValue();
  APInt UMaxRHS = RHS.getMaxValue();

  // For leading zeros in the result to be valid, the unsigned max product must
  // fit in the bitwidth (it must not overflow).
  bool HasOverflow;
  APInt UMaxResult = UMaxLHS.umul_ov(UMaxRHS, HasOverflow);
  unsigned LeadZ = HasOverflow ? 0 : UMaxResult.countl_zero();

  // The result of the bottom bits of an integer multiply can be
  // inferred by looking at the bottom bits of both operands and
  // multiplying them together.
  // We can infer at least the minimum number of known trailing bits
  // of both operands. Depending on number of trailing zeros, we can
  // infer more bits, because (a*b) <=> ((a/m) * (b/n)) * (m*n) assuming
  // a and b are divisible by m and n respectively.
  // We then calculate how many of those bits are inferrable and set
  // the output. For example, the i8 mul:
  //  a = XXXX1100 (12)
  //  b = XXXX1110 (14)
  // We know the bottom 3 bits are zero since the first can be divided by
  // 4 and the second by 2, thus having ((12/4) * (14/2)) * (2*4).
  // Applying the multiplication to the trimmed arguments gets:
  //    XX11 (3)
  //    X111 (7)
  // -------
  //    XX11
  //   XX11
  //  XX11
  // XX11
  // -------
  // XXXXX01
  // Which allows us to infer the 2 LSBs. Since we're multiplying the result
  // by 8, the bottom 3 bits will be 0, so we can infer a total of 5 bits.
  // The proof for this can be described as:
  // Pre: (C1 >= 0) && (C1 < (1 << C5)) && (C2 >= 0) && (C2 < (1 << C6)) &&
  //      (C7 == (1 << (umin(countTrailingZeros(C1), C5) +
  //                    umin(countTrailingZeros(C2), C6) +
  //                    umin(C5 - umin(countTrailingZeros(C1), C5),
  //                         C6 - umin(countTrailingZeros(C2), C6)))) - 1)
  // %aa = shl i8 %a, C5
  // %bb = shl i8 %b, C6
  // %aaa = or i8 %aa, C1
  // %bbb = or i8 %bb, C2
  // %mul = mul i8 %aaa, %bbb
  // %mask = and i8 %mul, C7
  //   =>
  // %mask = i8 ((C1*C2)&C7)
  // Where C5, C6 describe the known bits of %a, %b
  // C1, C2 describe the known bottom bits of %a, %b.
  // C7 describes the mask of the known bits of the result.
  const APInt &Bottom0 = LHS.One;
  const APInt &Bottom1 = RHS.One;

  // How many times we'd be able to divide each argument by 2 (shr by 1).
  // This gives us the number of trailing zeros on the multiplication result.
  unsigned TrailBitsKnown0 = (LHS.Zero | LHS.One).countr_one();
  unsigned TrailBitsKnown1 = (RHS.Zero | RHS.One).countr_one();
  unsigned TrailZero0 = LHS.countMinTrailingZeros();
  unsigned TrailZero1 = RHS.countMinTrailingZeros();
  unsigned TrailZ = TrailZero0 + TrailZero1;

  // Figure out the fewest known-bits operand.
  unsigned SmallestOperand =
      std::min(TrailBitsKnown0 - TrailZero0, TrailBitsKnown1 - TrailZero1);
  unsigned ResultBitsKnown = std::min(SmallestOperand + TrailZ, BitWidth);

  APInt BottomKnown =
      Bottom0.getLoBits(TrailBitsKnown0) * Bottom1.getLoBits(TrailBitsKnown1);

  KnownBits Res(BitWidth);
  Res.Zero.setHighBits(LeadZ);
  Res.Zero |= (~BottomKnown).getLoBits(ResultBitsKnown);
  Res.One = BottomKnown.getLoBits(ResultBitsKnown);

  // If we're self-multiplying then bit[1] is guaranteed to be zero.
  if (NoUndefSelfMultiply && BitWidth > 1) {
    assert(Res.One[1] == 0 &&
           "Self-multiplication failed Quadratic Reciprocity!");
    Res.Zero.setBit(1);
  }

  return Res;
}

KnownBits KnownBits::mulhs(const KnownBits &LHS, const KnownBits &RHS) {
  unsigned BitWidth = LHS.getBitWidth();
  assert(BitWidth == RHS.getBitWidth() && "Operand mismatch");
  KnownBits WideLHS = LHS.sext(2 * BitWidth);
  KnownBits WideRHS = RHS.sext(2 * BitWidth);
  return mul(WideLHS, WideRHS).extractBits(BitWidth, BitWidth);
}

KnownBits KnownBits::mulhu(const KnownBits &LHS, const KnownBits &RHS) {
  unsigned BitWidth = LHS.getBitWidth();
  assert(BitWidth == RHS.getBitWidth() && "Operand mismatch");
  KnownBits WideLHS = LHS.zext(2 * BitWidth);
  KnownBits WideRHS = RHS.zext(2 * BitWidth);
  return mul(WideLHS, WideRHS).extractBits(BitWidth, BitWidth);
}

static KnownBits divComputeLowBit(KnownBits Known, const KnownBits &LHS,
                                  const KnownBits &RHS, bool Exact) {

  if (!Exact)
    return Known;

  // If LHS is Odd, the result is Odd no matter what.
  // Odd / Odd -> Odd
  // Odd / Even -> Impossible (because its exact division)
  if (LHS.One[0])
    Known.One.setBit(0);

  int MinTZ =
      (int)LHS.countMinTrailingZeros() - (int)RHS.countMaxTrailingZeros();
  int MaxTZ =
      (int)LHS.countMaxTrailingZeros() - (int)RHS.countMinTrailingZeros();
  if (MinTZ >= 0) {
    // Result has at least MinTZ trailing zeros.
    Known.Zero.setLowBits(MinTZ);
    if (MinTZ == MaxTZ) {
      // Result has exactly MinTZ trailing zeros.
      Known.One.setBit(MinTZ);
    }
  } else if (MaxTZ < 0) {
    // Poison Result
    Known.setAllZero();
  }

  // In the KnownBits exhaustive tests, we have poison inputs for exact values
  // a LOT. If we have a conflict, just return all zeros.
  if (Known.hasConflict())
    Known.setAllZero();

  return Known;
}

KnownBits KnownBits::sdiv(const KnownBits &LHS, const KnownBits &RHS,
                          bool Exact) {
  // Equivalent of `udiv`. We must have caught this before it was folded.
  if (LHS.isNonNegative() && RHS.isNonNegative())
    return udiv(LHS, RHS, Exact);

  unsigned BitWidth = LHS.getBitWidth();
  KnownBits Known(BitWidth);

  if (LHS.isZero() || RHS.isZero()) {
    // Result is either known Zero or UB. Return Zero either way.
    // Checking this earlier saves us a lot of special cases later on.
    Known.setAllZero();
    return Known;
  }

  std::optional<APInt> Res;
  if (LHS.isNegative() && RHS.isNegative()) {
    // Result non-negative.
    APInt Denom = RHS.getSignedMaxValue();
    APInt Num = LHS.getSignedMinValue();
    // INT_MIN/-1 would be a poison result (impossible). Estimate the division
    // as signed max (we will only set sign bit in the result).
    Res = (Num.isMinSignedValue() && Denom.isAllOnes())
              ? APInt::getSignedMaxValue(BitWidth)
              : Num.sdiv(Denom);
  } else if (LHS.isNegative() && RHS.isNonNegative()) {
    // Result is negative if Exact OR -LHS u>= RHS.
    if (Exact || (-LHS.getSignedMaxValue()).uge(RHS.getSignedMaxValue())) {
      APInt Denom = RHS.getSignedMinValue();
      APInt Num = LHS.getSignedMinValue();
      Res = Denom.isZero() ? Num : Num.sdiv(Denom);
    }
  } else if (LHS.isStrictlyPositive() && RHS.isNegative()) {
    // Result is negative if Exact OR LHS u>= -RHS.
    if (Exact || LHS.getSignedMinValue().uge(-RHS.getSignedMinValue())) {
      APInt Denom = RHS.getSignedMaxValue();
      APInt Num = LHS.getSignedMaxValue();
      Res = Num.sdiv(Denom);
    }
  }

  if (Res) {
    if (Res->isNonNegative()) {
      unsigned LeadZ = Res->countLeadingZeros();
      Known.Zero.setHighBits(LeadZ);
    } else {
      unsigned LeadO = Res->countLeadingOnes();
      Known.One.setHighBits(LeadO);
    }
  }

  Known = divComputeLowBit(Known, LHS, RHS, Exact);
  return Known;
}

KnownBits KnownBits::udiv(const KnownBits &LHS, const KnownBits &RHS,
                          bool Exact) {
  unsigned BitWidth = LHS.getBitWidth();
  KnownBits Known(BitWidth);

  if (LHS.isZero() || RHS.isZero()) {
    // Result is either known Zero or UB. Return Zero either way.
    // Checking this earlier saves us a lot of special cases later on.
    Known.setAllZero();
    return Known;
  }

  // We can figure out the minimum number of upper zero bits by doing
  // MaxNumerator / MinDenominator. If the Numerator gets smaller or Denominator
  // gets larger, the number of upper zero bits increases.
  APInt MinDenom = RHS.getMinValue();
  APInt MaxNum = LHS.getMaxValue();
  APInt MaxRes = MinDenom.isZero() ? MaxNum : MaxNum.udiv(MinDenom);

  unsigned LeadZ = MaxRes.countLeadingZeros();

  Known.Zero.setHighBits(LeadZ);
  Known = divComputeLowBit(Known, LHS, RHS, Exact);

  return Known;
}

KnownBits KnownBits::remGetLowBits(const KnownBits &LHS, const KnownBits &RHS) {
  unsigned BitWidth = LHS.getBitWidth();
  if (!RHS.isZero() && RHS.Zero[0]) {
    // rem X, Y where Y[0:N] is zero will preserve X[0:N] in the result.
    unsigned RHSZeros = RHS.countMinTrailingZeros();
    APInt Mask = APInt::getLowBitsSet(BitWidth, RHSZeros);
    APInt OnesMask = LHS.One & Mask;
    APInt ZerosMask = LHS.Zero & Mask;
    return KnownBits(ZerosMask, OnesMask);
  }
  return KnownBits(BitWidth);
}

KnownBits KnownBits::urem(const KnownBits &LHS, const KnownBits &RHS) {
  KnownBits Known = remGetLowBits(LHS, RHS);
  if (RHS.isConstant() && RHS.getConstant().isPowerOf2()) {
    // NB: Low bits set in `remGetLowBits`.
    APInt HighBits = ~(RHS.getConstant() - 1);
    Known.Zero |= HighBits;
    return Known;
  }

  // Since the result is less than or equal to either operand, any leading
  // zero bits in either operand must also exist in the result.
  uint32_t Leaders =
      std::max(LHS.countMinLeadingZeros(), RHS.countMinLeadingZeros());
  Known.Zero.setHighBits(Leaders);
  return Known;
}

KnownBits KnownBits::srem(const KnownBits &LHS, const KnownBits &RHS) {
  KnownBits Known = remGetLowBits(LHS, RHS);
  if (RHS.isConstant() && RHS.getConstant().isPowerOf2()) {
    // NB: Low bits are set in `remGetLowBits`.
    APInt LowBits = RHS.getConstant() - 1;
    // If the first operand is non-negative or has all low bits zero, then
    // the upper bits are all zero.
    if (LHS.isNonNegative() || LowBits.isSubsetOf(LHS.Zero))
      Known.Zero |= ~LowBits;

    // If the first operand is negative and not all low bits are zero, then
    // the upper bits are all one.
    if (LHS.isNegative() && LowBits.intersects(LHS.One))
      Known.One |= ~LowBits;
    return Known;
  }

  // The sign bit is the LHS's sign bit, except when the result of the
  // remainder is zero. The magnitude of the result should be less than or
  // equal to the magnitude of the LHS. Therefore any leading zeros that exist
  // in the left hand side must also exist in the result.
  Known.Zero.setHighBits(LHS.countMinLeadingZeros());
  return Known;
}

KnownBits &KnownBits::operator&=(const KnownBits &RHS) {
  // Result bit is 0 if either operand bit is 0.
  Zero |= RHS.Zero;
  // Result bit is 1 if both operand bits are 1.
  One &= RHS.One;
  return *this;
}

KnownBits &KnownBits::operator|=(const KnownBits &RHS) {
  // Result bit is 0 if both operand bits are 0.
  Zero &= RHS.Zero;
  // Result bit is 1 if either operand bit is 1.
  One |= RHS.One;
  return *this;
}

KnownBits &KnownBits::operator^=(const KnownBits &RHS) {
  // Result bit is 0 if both operand bits are 0 or both are 1.
  APInt Z = (Zero & RHS.Zero) | (One & RHS.One);
  // Result bit is 1 if one operand bit is 0 and the other is 1.
  One = (Zero & RHS.One) | (One & RHS.Zero);
  Zero = std::move(Z);
  return *this;
}

KnownBits KnownBits::blsi() const {
  unsigned BitWidth = getBitWidth();
  KnownBits Known(Zero, APInt(BitWidth, 0));
  unsigned Max = countMaxTrailingZeros();
  Known.Zero.setBitsFrom(std::min(Max + 1, BitWidth));
  unsigned Min = countMinTrailingZeros();
  if (Max == Min && Max < BitWidth)
    Known.One.setBit(Max);
  return Known;
}

KnownBits KnownBits::blsmsk() const {
  unsigned BitWidth = getBitWidth();
  KnownBits Known(BitWidth);
  unsigned Max = countMaxTrailingZeros();
  Known.Zero.setBitsFrom(std::min(Max + 1, BitWidth));
  unsigned Min = countMinTrailingZeros();
  Known.One.setLowBits(std::min(Min + 1, BitWidth));
  return Known;
}

void KnownBits::print(raw_ostream &OS) const {
  unsigned BitWidth = getBitWidth();
  for (unsigned I = 0; I < BitWidth; ++I) {
    unsigned N = BitWidth - I - 1;
    if (Zero[N] && One[N])
      OS << "!";
    else if (Zero[N])
      OS << "0";
    else if (One[N])
      OS << "1";
    else
      OS << "?";
  }
}
void KnownBits::dump() const {
  print(dbgs());
  dbgs() << "\n";
}
