//===- APFixedPoint.h - Fixed point constant handling -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the fixed point number interface.
/// This is a class for abstracting various operations performed on fixed point
/// types.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APFIXEDPOINT_H
#define LLVM_ADT_APFIXEDPOINT_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class APFloat;
struct fltSemantics;

/// The fixed point semantics work similarly to fltSemantics. The width
/// specifies the whole bit width of the underlying scaled integer (with padding
/// if any). The scale represents the number of fractional bits in this type.
/// When HasUnsignedPadding is true and this type is unsigned, the first bit
/// in the value this represents is treated as padding.
class FixedPointSemantics {
public:
  static constexpr unsigned WidthBitWidth = 16;
  static constexpr unsigned LsbWeightBitWidth = 13;
  /// Used to differentiate between constructors with Width and Lsb from the
  /// default Width and scale
  struct Lsb {
    int LsbWeight;
  };
  FixedPointSemantics(unsigned Width, unsigned Scale, bool IsSigned,
                      bool IsSaturated, bool HasUnsignedPadding)
      : FixedPointSemantics(Width, Lsb{-static_cast<int>(Scale)}, IsSigned,
                            IsSaturated, HasUnsignedPadding) {}
  FixedPointSemantics(unsigned Width, Lsb Weight, bool IsSigned,
                      bool IsSaturated, bool HasUnsignedPadding)
      : Width(Width), LsbWeight(Weight.LsbWeight), IsSigned(IsSigned),
        IsSaturated(IsSaturated), HasUnsignedPadding(HasUnsignedPadding) {
    assert(isUInt<WidthBitWidth>(Width) && isInt<LsbWeightBitWidth>(Weight.LsbWeight));
    assert(!(IsSigned && HasUnsignedPadding) &&
           "Cannot have unsigned padding on a signed type.");
  }

  /// Check if the Semantic follow the requirements of an older more limited
  /// version of this class
  bool isValidLegacySema() const {
    return LsbWeight <= 0 && static_cast<int>(Width) >= -LsbWeight;
  }
  unsigned getWidth() const { return Width; }
  unsigned getScale() const { assert(isValidLegacySema()); return -LsbWeight; }
  int getLsbWeight() const { return LsbWeight; }
  int getMsbWeight() const {
    return LsbWeight + Width - 1 /*Both lsb and msb are both part of width*/;
  }
  bool isSigned() const { return IsSigned; }
  bool isSaturated() const { return IsSaturated; }
  bool hasUnsignedPadding() const { return HasUnsignedPadding; }

  void setSaturated(bool Saturated) { IsSaturated = Saturated; }

  /// return true if the first bit doesn't have a strictly positive weight
  bool hasSignOrPaddingBit() const { return IsSigned || HasUnsignedPadding; }

  /// Return the number of integral bits represented by these semantics. These
  /// are separate from the fractional bits and do not include the sign or
  /// padding bit.
  unsigned getIntegralBits() const {
    return std::max(getMsbWeight() + 1 - hasSignOrPaddingBit(), 0);
  }

  /// Return the FixedPointSemantics that allows for calculating the full
  /// precision semantic that can precisely represent the precision and ranges
  /// of both input values. This does not compute the resulting semantics for a
  /// given binary operation.
  FixedPointSemantics
  getCommonSemantics(const FixedPointSemantics &Other) const;

  /// Print semantics for debug purposes
  void print(llvm::raw_ostream& OS) const;

  /// Returns true if this fixed-point semantic with its value bits interpreted
  /// as an integer can fit in the given floating point semantic without
  /// overflowing to infinity.
  /// For example, a signed 8-bit fixed-point semantic has a maximum and
  /// minimum integer representation of 127 and -128, respectively. If both of
  /// these values can be represented (possibly inexactly) in the floating
  /// point semantic without overflowing, this returns true.
  bool fitsInFloatSemantics(const fltSemantics &FloatSema) const;

  /// Return the FixedPointSemantics for an integer type.
  static FixedPointSemantics GetIntegerSemantics(unsigned Width,
                                                 bool IsSigned) {
    return FixedPointSemantics(Width, /*Scale=*/0, IsSigned,
                               /*IsSaturated=*/false,
                               /*HasUnsignedPadding=*/false);
  }

  bool operator==(FixedPointSemantics Other) const {
    return Width == Other.Width && LsbWeight == Other.LsbWeight &&
           IsSigned == Other.IsSigned && IsSaturated == Other.IsSaturated &&
           HasUnsignedPadding == Other.HasUnsignedPadding;
  }
  bool operator!=(FixedPointSemantics Other) const { return !(*this == Other); }

private:
  unsigned Width          : WidthBitWidth;
  signed int LsbWeight    : LsbWeightBitWidth;
  unsigned IsSigned       : 1;
  unsigned IsSaturated    : 1;
  unsigned HasUnsignedPadding : 1;
};

static_assert(sizeof(FixedPointSemantics) == 4, "");

inline hash_code hash_value(const FixedPointSemantics &Val) {
  return hash_value(bit_cast<uint32_t>(Val));
}

template <> struct DenseMapInfo<FixedPointSemantics> {
  static inline FixedPointSemantics getEmptyKey() {
    return FixedPointSemantics(0, 0, false, false, false);
  }

  static inline FixedPointSemantics getTombstoneKey() {
    return FixedPointSemantics(0, 1, false, false, false);
  }

  static unsigned getHashValue(const FixedPointSemantics &Val) {
    return hash_value(Val);
  }

  static bool isEqual(const char &LHS, const char &RHS) { return LHS == RHS; }
};

/// The APFixedPoint class works similarly to APInt/APSInt in that it is a
/// functional replacement for a scaled integer. It supports a wide range of
/// semantics including the one used by fixed point types proposed in ISO/IEC
/// JTC1 SC22 WG14 N1169. The class carries the value and semantics of
/// a fixed point, and provides different operations that would normally be
/// performed on fixed point types.
class APFixedPoint {
public:
  APFixedPoint(const APInt &Val, const FixedPointSemantics &Sema)
      : Val(Val, !Sema.isSigned()), Sema(Sema) {
    assert(Val.getBitWidth() == Sema.getWidth() &&
           "The value should have a bit width that matches the Sema width");
  }

  APFixedPoint(uint64_t Val, const FixedPointSemantics &Sema)
      : APFixedPoint(APInt(Sema.getWidth(), Val, Sema.isSigned()), Sema) {}

  // Zero initialization.
  APFixedPoint(const FixedPointSemantics &Sema) : APFixedPoint(0, Sema) {}

  APSInt getValue() const { return APSInt(Val, !Sema.isSigned()); }
  inline unsigned getWidth() const { return Sema.getWidth(); }
  inline unsigned getScale() const { return Sema.getScale(); }
  int getLsbWeight() const { return Sema.getLsbWeight(); }
  int getMsbWeight() const { return Sema.getMsbWeight(); }
  inline bool isSaturated() const { return Sema.isSaturated(); }
  inline bool isSigned() const { return Sema.isSigned(); }
  inline bool hasPadding() const { return Sema.hasUnsignedPadding(); }
  FixedPointSemantics getSemantics() const { return Sema; }

  bool getBoolValue() const { return Val.getBoolValue(); }

  // Convert this number to match the semantics provided. If the overflow
  // parameter is provided, set this value to true or false to indicate if this
  // operation results in an overflow.
  APFixedPoint convert(const FixedPointSemantics &DstSema,
                       bool *Overflow = nullptr) const;

  // Perform binary operations on a fixed point type. The resulting fixed point
  // value will be in the common, full precision semantics that can represent
  // the precision and ranges of both input values. See convert() for an
  // explanation of the Overflow parameter.
  APFixedPoint add(const APFixedPoint &Other, bool *Overflow = nullptr) const;
  APFixedPoint sub(const APFixedPoint &Other, bool *Overflow = nullptr) const;
  APFixedPoint mul(const APFixedPoint &Other, bool *Overflow = nullptr) const;
  APFixedPoint div(const APFixedPoint &Other, bool *Overflow = nullptr) const;

  // Perform shift operations on a fixed point type. Unlike the other binary
  // operations, the resulting fixed point value will be in the original
  // semantic.
  APFixedPoint shl(unsigned Amt, bool *Overflow = nullptr) const;
  APFixedPoint shr(unsigned Amt, bool *Overflow = nullptr) const {
    // Right shift cannot overflow.
    if (Overflow)
      *Overflow = false;
    return APFixedPoint(Val >> Amt, Sema);
  }

  /// Perform a unary negation (-X) on this fixed point type, taking into
  /// account saturation if applicable.
  APFixedPoint negate(bool *Overflow = nullptr) const;

  /// Return the integral part of this fixed point number, rounded towards
  /// zero. (-2.5k -> -2)
  APSInt getIntPart() const {
    if (getMsbWeight() < 0)
      return APSInt(APInt::getZero(getWidth()), Val.isUnsigned());
    APSInt ExtVal =
        (getLsbWeight() > 0) ? Val.extend(getWidth() + getLsbWeight()) : Val;
    if (Val < 0 && Val != -Val) // Cover the case when we have the min val
      return -((-ExtVal).relativeShl(getLsbWeight()));
    return ExtVal.relativeShl(getLsbWeight());
  }

  /// Return the integral part of this fixed point number, rounded towards
  /// zero. The value is stored into an APSInt with the provided width and sign.
  /// If the overflow parameter is provided, and the integral value is not able
  /// to be fully stored in the provided width and sign, the overflow parameter
  /// is set to true.
  APSInt convertToInt(unsigned DstWidth, bool DstSign,
                      bool *Overflow = nullptr) const;

  /// Convert this fixed point number to a floating point value with the
  /// provided semantics.
  APFloat convertToFloat(const fltSemantics &FloatSema) const;

  void toString(SmallVectorImpl<char> &Str) const;
  std::string toString() const {
    SmallString<40> S;
    toString(S);
    return std::string(S);
  }

  void print(raw_ostream &) const;
  void dump() const;

  // If LHS > RHS, return 1. If LHS == RHS, return 0. If LHS < RHS, return -1.
  int compare(const APFixedPoint &Other) const;
  bool operator==(const APFixedPoint &Other) const {
    return compare(Other) == 0;
  }
  bool operator!=(const APFixedPoint &Other) const {
    return compare(Other) != 0;
  }
  bool operator>(const APFixedPoint &Other) const { return compare(Other) > 0; }
  bool operator<(const APFixedPoint &Other) const { return compare(Other) < 0; }
  bool operator>=(const APFixedPoint &Other) const {
    return compare(Other) >= 0;
  }
  bool operator<=(const APFixedPoint &Other) const {
    return compare(Other) <= 0;
  }

  static APFixedPoint getMax(const FixedPointSemantics &Sema);
  static APFixedPoint getMin(const FixedPointSemantics &Sema);
  static APFixedPoint getEpsilon(const FixedPointSemantics &Sema);

  /// Given a floating point semantic, return the next floating point semantic
  /// with a larger exponent and larger or equal mantissa.
  static const fltSemantics *promoteFloatSemantics(const fltSemantics *S);

  /// Create an APFixedPoint with a value equal to that of the provided integer,
  /// and in the same semantics as the provided target semantics. If the value
  /// is not able to fit in the specified fixed point semantics, and the
  /// overflow parameter is provided, it is set to true.
  static APFixedPoint getFromIntValue(const APSInt &Value,
                                      const FixedPointSemantics &DstFXSema,
                                      bool *Overflow = nullptr);

  /// Create an APFixedPoint with a value equal to that of the provided
  /// floating point value, in the provided target semantics. If the value is
  /// not able to fit in the specified fixed point semantics and the overflow
  /// parameter is specified, it is set to true.
  /// For NaN, the Overflow flag is always set. For +inf and -inf, if the
  /// semantic is saturating, the value saturates. Otherwise, the Overflow flag
  /// is set.
  static APFixedPoint getFromFloatValue(const APFloat &Value,
                                        const FixedPointSemantics &DstFXSema,
                                        bool *Overflow = nullptr);

private:
  APSInt Val;
  FixedPointSemantics Sema;
};

inline raw_ostream &operator<<(raw_ostream &OS, const APFixedPoint &FX) {
  OS << FX.toString();
  return OS;
}

inline hash_code hash_value(const APFixedPoint &Val) {
  return hash_combine(Val.getSemantics(), Val.getValue());
}

template <> struct DenseMapInfo<APFixedPoint> {
  static inline APFixedPoint getEmptyKey() {
    return APFixedPoint(DenseMapInfo<FixedPointSemantics>::getEmptyKey());
  }

  static inline APFixedPoint getTombstoneKey() {
    return APFixedPoint(DenseMapInfo<FixedPointSemantics>::getTombstoneKey());
  }

  static unsigned getHashValue(const APFixedPoint &Val) {
    return hash_value(Val);
  }

  static bool isEqual(const APFixedPoint &LHS, const APFixedPoint &RHS) {
    return LHS.getSemantics() == RHS.getSemantics() &&
           LHS.getValue() == RHS.getValue();
  }
};

} // namespace llvm

#endif
