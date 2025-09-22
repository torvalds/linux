//===- llvm/Support/KnownBits.h - Stores known zeros/ones -------*- C++ -*-===//
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

#ifndef LLVM_SUPPORT_KNOWNBITS_H
#define LLVM_SUPPORT_KNOWNBITS_H

#include "llvm/ADT/APInt.h"
#include <optional>

namespace llvm {

// Struct for tracking the known zeros and ones of a value.
struct KnownBits {
  APInt Zero;
  APInt One;

private:
  // Internal constructor for creating a KnownBits from two APInts.
  KnownBits(APInt Zero, APInt One)
      : Zero(std::move(Zero)), One(std::move(One)) {}

public:
  // Default construct Zero and One.
  KnownBits() = default;

  /// Create a known bits object of BitWidth bits initialized to unknown.
  KnownBits(unsigned BitWidth) : Zero(BitWidth, 0), One(BitWidth, 0) {}

  /// Get the bit width of this value.
  unsigned getBitWidth() const {
    assert(Zero.getBitWidth() == One.getBitWidth() &&
           "Zero and One should have the same width!");
    return Zero.getBitWidth();
  }

  /// Returns true if there is conflicting information.
  bool hasConflict() const { return Zero.intersects(One); }

  /// Returns true if we know the value of all bits.
  bool isConstant() const {
    return Zero.popcount() + One.popcount() == getBitWidth();
  }

  /// Returns the value when all bits have a known value. This just returns One
  /// with a protective assertion.
  const APInt &getConstant() const {
    assert(isConstant() && "Can only get value when all bits are known");
    return One;
  }

  /// Returns true if we don't know any bits.
  bool isUnknown() const { return Zero.isZero() && One.isZero(); }

  /// Returns true if we don't know the sign bit.
  bool isSignUnknown() const {
    return !Zero.isSignBitSet() && !One.isSignBitSet();
  }

  /// Resets the known state of all bits.
  void resetAll() {
    Zero.clearAllBits();
    One.clearAllBits();
  }

  /// Returns true if value is all zero.
  bool isZero() const { return Zero.isAllOnes(); }

  /// Returns true if value is all one bits.
  bool isAllOnes() const { return One.isAllOnes(); }

  /// Make all bits known to be zero and discard any previous information.
  void setAllZero() {
    Zero.setAllBits();
    One.clearAllBits();
  }

  /// Make all bits known to be one and discard any previous information.
  void setAllOnes() {
    Zero.clearAllBits();
    One.setAllBits();
  }

  /// Returns true if this value is known to be negative.
  bool isNegative() const { return One.isSignBitSet(); }

  /// Returns true if this value is known to be non-negative.
  bool isNonNegative() const { return Zero.isSignBitSet(); }

  /// Returns true if this value is known to be non-zero.
  bool isNonZero() const { return !One.isZero(); }

  /// Returns true if this value is known to be positive.
  bool isStrictlyPositive() const {
    return Zero.isSignBitSet() && !One.isZero();
  }

  /// Make this value negative.
  void makeNegative() {
    One.setSignBit();
  }

  /// Make this value non-negative.
  void makeNonNegative() {
    Zero.setSignBit();
  }

  /// Return the minimal unsigned value possible given these KnownBits.
  APInt getMinValue() const {
    // Assume that all bits that aren't known-ones are zeros.
    return One;
  }

  /// Return the minimal signed value possible given these KnownBits.
  APInt getSignedMinValue() const {
    // Assume that all bits that aren't known-ones are zeros.
    APInt Min = One;
    // Sign bit is unknown.
    if (Zero.isSignBitClear())
      Min.setSignBit();
    return Min;
  }

  /// Return the maximal unsigned value possible given these KnownBits.
  APInt getMaxValue() const {
    // Assume that all bits that aren't known-zeros are ones.
    return ~Zero;
  }

  /// Return the maximal signed value possible given these KnownBits.
  APInt getSignedMaxValue() const {
    // Assume that all bits that aren't known-zeros are ones.
    APInt Max = ~Zero;
    // Sign bit is unknown.
    if (One.isSignBitClear())
      Max.clearSignBit();
    return Max;
  }

  /// Return known bits for a truncation of the value we're tracking.
  KnownBits trunc(unsigned BitWidth) const {
    return KnownBits(Zero.trunc(BitWidth), One.trunc(BitWidth));
  }

  /// Return known bits for an "any" extension of the value we're tracking,
  /// where we don't know anything about the extended bits.
  KnownBits anyext(unsigned BitWidth) const {
    return KnownBits(Zero.zext(BitWidth), One.zext(BitWidth));
  }

  /// Return known bits for a zero extension of the value we're tracking.
  KnownBits zext(unsigned BitWidth) const {
    unsigned OldBitWidth = getBitWidth();
    APInt NewZero = Zero.zext(BitWidth);
    NewZero.setBitsFrom(OldBitWidth);
    return KnownBits(NewZero, One.zext(BitWidth));
  }

  /// Return known bits for a sign extension of the value we're tracking.
  KnownBits sext(unsigned BitWidth) const {
    return KnownBits(Zero.sext(BitWidth), One.sext(BitWidth));
  }

  /// Return known bits for an "any" extension or truncation of the value we're
  /// tracking.
  KnownBits anyextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return anyext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a zero extension or truncation of the value we're
  /// tracking.
  KnownBits zextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return zext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a sign extension or truncation of the value we're
  /// tracking.
  KnownBits sextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return sext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a in-register sign extension of the value we're
  /// tracking.
  KnownBits sextInReg(unsigned SrcBitWidth) const;

  /// Insert the bits from a smaller known bits starting at bitPosition.
  void insertBits(const KnownBits &SubBits, unsigned BitPosition) {
    Zero.insertBits(SubBits.Zero, BitPosition);
    One.insertBits(SubBits.One, BitPosition);
  }

  /// Return a subset of the known bits from [bitPosition,bitPosition+numBits).
  KnownBits extractBits(unsigned NumBits, unsigned BitPosition) const {
    return KnownBits(Zero.extractBits(NumBits, BitPosition),
                     One.extractBits(NumBits, BitPosition));
  }

  /// Concatenate the bits from \p Lo onto the bottom of *this.  This is
  /// equivalent to:
  ///   (this->zext(NewWidth) << Lo.getBitWidth()) | Lo.zext(NewWidth)
  KnownBits concat(const KnownBits &Lo) const {
    return KnownBits(Zero.concat(Lo.Zero), One.concat(Lo.One));
  }

  /// Return KnownBits based on this, but updated given that the underlying
  /// value is known to be greater than or equal to Val.
  KnownBits makeGE(const APInt &Val) const;

  /// Returns the minimum number of trailing zero bits.
  unsigned countMinTrailingZeros() const { return Zero.countr_one(); }

  /// Returns the minimum number of trailing one bits.
  unsigned countMinTrailingOnes() const { return One.countr_one(); }

  /// Returns the minimum number of leading zero bits.
  unsigned countMinLeadingZeros() const { return Zero.countl_one(); }

  /// Returns the minimum number of leading one bits.
  unsigned countMinLeadingOnes() const { return One.countl_one(); }

  /// Returns the number of times the sign bit is replicated into the other
  /// bits.
  unsigned countMinSignBits() const {
    if (isNonNegative())
      return countMinLeadingZeros();
    if (isNegative())
      return countMinLeadingOnes();
    // Every value has at least 1 sign bit.
    return 1;
  }

  /// Returns the maximum number of bits needed to represent all possible
  /// signed values with these known bits. This is the inverse of the minimum
  /// number of known sign bits. Examples for bitwidth 5:
  /// 110?? --> 4
  /// 0000? --> 2
  unsigned countMaxSignificantBits() const {
    return getBitWidth() - countMinSignBits() + 1;
  }

  /// Returns the maximum number of trailing zero bits possible.
  unsigned countMaxTrailingZeros() const { return One.countr_zero(); }

  /// Returns the maximum number of trailing one bits possible.
  unsigned countMaxTrailingOnes() const { return Zero.countr_zero(); }

  /// Returns the maximum number of leading zero bits possible.
  unsigned countMaxLeadingZeros() const { return One.countl_zero(); }

  /// Returns the maximum number of leading one bits possible.
  unsigned countMaxLeadingOnes() const { return Zero.countl_zero(); }

  /// Returns the number of bits known to be one.
  unsigned countMinPopulation() const { return One.popcount(); }

  /// Returns the maximum number of bits that could be one.
  unsigned countMaxPopulation() const {
    return getBitWidth() - Zero.popcount();
  }

  /// Returns the maximum number of bits needed to represent all possible
  /// unsigned values with these known bits. This is the inverse of the
  /// minimum number of leading zeros.
  unsigned countMaxActiveBits() const {
    return getBitWidth() - countMinLeadingZeros();
  }

  /// Create known bits from a known constant.
  static KnownBits makeConstant(const APInt &C) {
    return KnownBits(~C, C);
  }

  /// Returns KnownBits information that is known to be true for both this and
  /// RHS.
  ///
  /// When an operation is known to return one of its operands, this can be used
  /// to combine information about the known bits of the operands to get the
  /// information that must be true about the result.
  KnownBits intersectWith(const KnownBits &RHS) const {
    return KnownBits(Zero & RHS.Zero, One & RHS.One);
  }

  /// Returns KnownBits information that is known to be true for either this or
  /// RHS or both.
  ///
  /// This can be used to combine different sources of information about the
  /// known bits of a single value, e.g. information about the low bits and the
  /// high bits of the result of a multiplication.
  KnownBits unionWith(const KnownBits &RHS) const {
    return KnownBits(Zero | RHS.Zero, One | RHS.One);
  }

  /// Return true if LHS and RHS have no common bits set.
  static bool haveNoCommonBitsSet(const KnownBits &LHS, const KnownBits &RHS) {
    return (LHS.Zero | RHS.Zero).isAllOnes();
  }

  /// Compute known bits resulting from adding LHS, RHS and a 1-bit Carry.
  static KnownBits computeForAddCarry(
      const KnownBits &LHS, const KnownBits &RHS, const KnownBits &Carry);

  /// Compute known bits resulting from adding LHS and RHS.
  static KnownBits computeForAddSub(bool Add, bool NSW, bool NUW,
                                    const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits results from subtracting RHS from LHS with 1-bit
  /// Borrow.
  static KnownBits computeForSubBorrow(const KnownBits &LHS, KnownBits RHS,
                                       const KnownBits &Borrow);

  /// Compute knownbits resulting from llvm.sadd.sat(LHS, RHS)
  static KnownBits sadd_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.uadd.sat(LHS, RHS)
  static KnownBits uadd_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.ssub.sat(LHS, RHS)
  static KnownBits ssub_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.usub.sat(LHS, RHS)
  static KnownBits usub_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgFloorS
  static KnownBits avgFloorS(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgFloorU
  static KnownBits avgFloorU(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgCeilS
  static KnownBits avgCeilS(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgCeilU
  static KnownBits avgCeilU(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits resulting from multiplying LHS and RHS.
  static KnownBits mul(const KnownBits &LHS, const KnownBits &RHS,
                       bool NoUndefSelfMultiply = false);

  /// Compute known bits from sign-extended multiply-hi.
  static KnownBits mulhs(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits from zero-extended multiply-hi.
  static KnownBits mulhu(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for sdiv(LHS, RHS).
  static KnownBits sdiv(const KnownBits &LHS, const KnownBits &RHS,
                        bool Exact = false);

  /// Compute known bits for udiv(LHS, RHS).
  static KnownBits udiv(const KnownBits &LHS, const KnownBits &RHS,
                        bool Exact = false);

  /// Compute known bits for urem(LHS, RHS).
  static KnownBits urem(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for srem(LHS, RHS).
  static KnownBits srem(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for umax(LHS, RHS).
  static KnownBits umax(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for umin(LHS, RHS).
  static KnownBits umin(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for smax(LHS, RHS).
  static KnownBits smax(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for smin(LHS, RHS).
  static KnownBits smin(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for abdu(LHS, RHS).
  static KnownBits abdu(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for abds(LHS, RHS).
  static KnownBits abds(KnownBits LHS, KnownBits RHS);

  /// Compute known bits for shl(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  static KnownBits shl(const KnownBits &LHS, const KnownBits &RHS,
                       bool NUW = false, bool NSW = false,
                       bool ShAmtNonZero = false);

  /// Compute known bits for lshr(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  static KnownBits lshr(const KnownBits &LHS, const KnownBits &RHS,
                        bool ShAmtNonZero = false, bool Exact = false);

  /// Compute known bits for ashr(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  static KnownBits ashr(const KnownBits &LHS, const KnownBits &RHS,
                        bool ShAmtNonZero = false, bool Exact = false);

  /// Determine if these known bits always give the same ICMP_EQ result.
  static std::optional<bool> eq(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_NE result.
  static std::optional<bool> ne(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_UGT result.
  static std::optional<bool> ugt(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_UGE result.
  static std::optional<bool> uge(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_ULT result.
  static std::optional<bool> ult(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_ULE result.
  static std::optional<bool> ule(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SGT result.
  static std::optional<bool> sgt(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SGE result.
  static std::optional<bool> sge(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SLT result.
  static std::optional<bool> slt(const KnownBits &LHS, const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SLE result.
  static std::optional<bool> sle(const KnownBits &LHS, const KnownBits &RHS);

  /// Update known bits based on ANDing with RHS.
  KnownBits &operator&=(const KnownBits &RHS);

  /// Update known bits based on ORing with RHS.
  KnownBits &operator|=(const KnownBits &RHS);

  /// Update known bits based on XORing with RHS.
  KnownBits &operator^=(const KnownBits &RHS);

  /// Compute known bits for the absolute value.
  KnownBits abs(bool IntMinIsPoison = false) const;

  KnownBits byteSwap() const {
    return KnownBits(Zero.byteSwap(), One.byteSwap());
  }

  KnownBits reverseBits() const {
    return KnownBits(Zero.reverseBits(), One.reverseBits());
  }

  /// Compute known bits for X & -X, which has only the lowest bit set of X set.
  /// The name comes from the X86 BMI instruction
  KnownBits blsi() const;

  /// Compute known bits for X ^ (X - 1), which has all bits up to and including
  /// the lowest set bit of X set. The name comes from the X86 BMI instruction.
  KnownBits blsmsk() const;

  bool operator==(const KnownBits &Other) const {
    return Zero == Other.Zero && One == Other.One;
  }

  bool operator!=(const KnownBits &Other) const { return !(*this == Other); }

  void print(raw_ostream &OS) const;
  void dump() const;

private:
  // Internal helper for getting the initial KnownBits for an `srem` or `urem`
  // operation with the low-bits set.
  static KnownBits remGetLowBits(const KnownBits &LHS, const KnownBits &RHS);
};

inline KnownBits operator&(KnownBits LHS, const KnownBits &RHS) {
  LHS &= RHS;
  return LHS;
}

inline KnownBits operator&(const KnownBits &LHS, KnownBits &&RHS) {
  RHS &= LHS;
  return std::move(RHS);
}

inline KnownBits operator|(KnownBits LHS, const KnownBits &RHS) {
  LHS |= RHS;
  return LHS;
}

inline KnownBits operator|(const KnownBits &LHS, KnownBits &&RHS) {
  RHS |= LHS;
  return std::move(RHS);
}

inline KnownBits operator^(KnownBits LHS, const KnownBits &RHS) {
  LHS ^= RHS;
  return LHS;
}

inline KnownBits operator^(const KnownBits &LHS, KnownBits &&RHS) {
  RHS ^= LHS;
  return std::move(RHS);
}

inline raw_ostream &operator<<(raw_ostream &OS, const KnownBits &Known) {
  Known.print(OS);
  return OS;
}

} // end namespace llvm

#endif
