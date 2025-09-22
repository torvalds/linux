//===- TypeSize.h - Wrapper around type sizes -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a struct that can be used to query the size of IR types
// which may be scalable vectors. It provides convenience operators so that
// it can be used in much the same way as a single scalar value.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TYPESIZE_H
#define LLVM_SUPPORT_TYPESIZE_H

#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace llvm {

/// Reports a diagnostic message to indicate an invalid size request has been
/// done on a scalable vector. This function may not return.
void reportInvalidSizeRequest(const char *Msg);

/// StackOffset holds a fixed and a scalable offset in bytes.
class StackOffset {
  int64_t Fixed = 0;
  int64_t Scalable = 0;

  StackOffset(int64_t Fixed, int64_t Scalable)
      : Fixed(Fixed), Scalable(Scalable) {}

public:
  StackOffset() = default;
  static StackOffset getFixed(int64_t Fixed) { return {Fixed, 0}; }
  static StackOffset getScalable(int64_t Scalable) { return {0, Scalable}; }
  static StackOffset get(int64_t Fixed, int64_t Scalable) {
    return {Fixed, Scalable};
  }

  /// Returns the fixed component of the stack.
  int64_t getFixed() const { return Fixed; }

  /// Returns the scalable component of the stack.
  int64_t getScalable() const { return Scalable; }

  // Arithmetic operations.
  StackOffset operator+(const StackOffset &RHS) const {
    return {Fixed + RHS.Fixed, Scalable + RHS.Scalable};
  }
  StackOffset operator-(const StackOffset &RHS) const {
    return {Fixed - RHS.Fixed, Scalable - RHS.Scalable};
  }
  StackOffset &operator+=(const StackOffset &RHS) {
    Fixed += RHS.Fixed;
    Scalable += RHS.Scalable;
    return *this;
  }
  StackOffset &operator-=(const StackOffset &RHS) {
    Fixed -= RHS.Fixed;
    Scalable -= RHS.Scalable;
    return *this;
  }
  StackOffset operator-() const { return {-Fixed, -Scalable}; }

  // Equality comparisons.
  bool operator==(const StackOffset &RHS) const {
    return Fixed == RHS.Fixed && Scalable == RHS.Scalable;
  }
  bool operator!=(const StackOffset &RHS) const {
    return Fixed != RHS.Fixed || Scalable != RHS.Scalable;
  }

  // The bool operator returns true iff any of the components is non zero.
  explicit operator bool() const { return Fixed != 0 || Scalable != 0; }
};

namespace details {

// Base class for ElementCount and TypeSize below.
template <typename LeafTy, typename ValueTy> class FixedOrScalableQuantity {
public:
  using ScalarTy = ValueTy;

protected:
  ScalarTy Quantity = 0;
  bool Scalable = false;

  constexpr FixedOrScalableQuantity() = default;
  constexpr FixedOrScalableQuantity(ScalarTy Quantity, bool Scalable)
      : Quantity(Quantity), Scalable(Scalable) {}

  friend constexpr LeafTy &operator+=(LeafTy &LHS, const LeafTy &RHS) {
    assert((LHS.Quantity == 0 || RHS.Quantity == 0 ||
            LHS.Scalable == RHS.Scalable) &&
           "Incompatible types");
    LHS.Quantity += RHS.Quantity;
    if (!RHS.isZero())
      LHS.Scalable = RHS.Scalable;
    return LHS;
  }

  friend constexpr LeafTy &operator-=(LeafTy &LHS, const LeafTy &RHS) {
    assert((LHS.Quantity == 0 || RHS.Quantity == 0 ||
            LHS.Scalable == RHS.Scalable) &&
           "Incompatible types");
    LHS.Quantity -= RHS.Quantity;
    if (!RHS.isZero())
      LHS.Scalable = RHS.Scalable;
    return LHS;
  }

  friend constexpr LeafTy &operator*=(LeafTy &LHS, ScalarTy RHS) {
    LHS.Quantity *= RHS;
    return LHS;
  }

  friend constexpr LeafTy operator+(const LeafTy &LHS, const LeafTy &RHS) {
    LeafTy Copy = LHS;
    return Copy += RHS;
  }

  friend constexpr LeafTy operator-(const LeafTy &LHS, const LeafTy &RHS) {
    LeafTy Copy = LHS;
    return Copy -= RHS;
  }

  friend constexpr LeafTy operator*(const LeafTy &LHS, ScalarTy RHS) {
    LeafTy Copy = LHS;
    return Copy *= RHS;
  }

  template <typename U = ScalarTy>
  friend constexpr std::enable_if_t<std::is_signed_v<U>, LeafTy>
  operator-(const LeafTy &LHS) {
    LeafTy Copy = LHS;
    return Copy *= -1;
  }

public:
  constexpr bool operator==(const FixedOrScalableQuantity &RHS) const {
    return Quantity == RHS.Quantity && Scalable == RHS.Scalable;
  }

  constexpr bool operator!=(const FixedOrScalableQuantity &RHS) const {
    return Quantity != RHS.Quantity || Scalable != RHS.Scalable;
  }

  constexpr bool isZero() const { return Quantity == 0; }

  constexpr bool isNonZero() const { return Quantity != 0; }

  explicit operator bool() const { return isNonZero(); }

  /// Add \p RHS to the underlying quantity.
  constexpr LeafTy getWithIncrement(ScalarTy RHS) const {
    return LeafTy::get(Quantity + RHS, Scalable);
  }

  /// Returns the minimum value this quantity can represent.
  constexpr ScalarTy getKnownMinValue() const { return Quantity; }

  /// Returns whether the quantity is scaled by a runtime quantity (vscale).
  constexpr bool isScalable() const { return Scalable; }

  /// Returns true if the quantity is not scaled by vscale.
  constexpr bool isFixed() const { return !Scalable; }

  /// A return value of true indicates we know at compile time that the number
  /// of elements (vscale * Min) is definitely even. However, returning false
  /// does not guarantee that the total number of elements is odd.
  constexpr bool isKnownEven() const { return (getKnownMinValue() & 0x1) == 0; }

  /// This function tells the caller whether the element count is known at
  /// compile time to be a multiple of the scalar value RHS.
  constexpr bool isKnownMultipleOf(ScalarTy RHS) const {
    return getKnownMinValue() % RHS == 0;
  }

  /// Returns whether or not the callee is known to be a multiple of RHS.
  constexpr bool isKnownMultipleOf(const FixedOrScalableQuantity &RHS) const {
    // x % y == 0 => x % y == 0
    // x % y == 0 => (vscale * x) % y == 0
    // x % y == 0 => (vscale * x) % (vscale * y) == 0
    // but
    // x % y == 0 !=> x % (vscale * y) == 0
    if (!isScalable() && RHS.isScalable())
      return false;
    return getKnownMinValue() % RHS.getKnownMinValue() == 0;
  }

  // Return the minimum value with the assumption that the count is exact.
  // Use in places where a scalable count doesn't make sense (e.g. non-vector
  // types, or vectors in backends which don't support scalable vectors).
  constexpr ScalarTy getFixedValue() const {
    assert((!isScalable() || isZero()) &&
           "Request for a fixed element count on a scalable object");
    return getKnownMinValue();
  }

  // For some cases, quantity ordering between scalable and fixed quantity types
  // cannot be determined at compile time, so such comparisons aren't allowed.
  //
  // e.g. <vscale x 2 x i16> could be bigger than <4 x i32> with a runtime
  // vscale >= 5, equal sized with a vscale of 4, and smaller with
  // a vscale <= 3.
  //
  // All the functions below make use of the fact vscale is always >= 1, which
  // means that <vscale x 4 x i32> is guaranteed to be >= <4 x i32>, etc.

  static constexpr bool isKnownLT(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (!LHS.isScalable() || RHS.isScalable())
      return LHS.getKnownMinValue() < RHS.getKnownMinValue();
    return false;
  }

  static constexpr bool isKnownGT(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (LHS.isScalable() || !RHS.isScalable())
      return LHS.getKnownMinValue() > RHS.getKnownMinValue();
    return false;
  }

  static constexpr bool isKnownLE(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (!LHS.isScalable() || RHS.isScalable())
      return LHS.getKnownMinValue() <= RHS.getKnownMinValue();
    return false;
  }

  static constexpr bool isKnownGE(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (LHS.isScalable() || !RHS.isScalable())
      return LHS.getKnownMinValue() >= RHS.getKnownMinValue();
    return false;
  }

  /// We do not provide the '/' operator here because division for polynomial
  /// types does not work in the same way as for normal integer types. We can
  /// only divide the minimum value (or coefficient) by RHS, which is not the
  /// same as
  ///   (Min * Vscale) / RHS
  /// The caller is recommended to use this function in combination with
  /// isKnownMultipleOf(RHS), which lets the caller know if it's possible to
  /// perform a lossless divide by RHS.
  constexpr LeafTy divideCoefficientBy(ScalarTy RHS) const {
    return LeafTy::get(getKnownMinValue() / RHS, isScalable());
  }

  constexpr LeafTy multiplyCoefficientBy(ScalarTy RHS) const {
    return LeafTy::get(getKnownMinValue() * RHS, isScalable());
  }

  constexpr LeafTy coefficientNextPowerOf2() const {
    return LeafTy::get(
        static_cast<ScalarTy>(llvm::NextPowerOf2(getKnownMinValue())),
        isScalable());
  }

  /// Returns true if there exists a value X where RHS.multiplyCoefficientBy(X)
  /// will result in a value whose quantity matches our own.
  constexpr bool
  hasKnownScalarFactor(const FixedOrScalableQuantity &RHS) const {
    return isScalable() == RHS.isScalable() &&
           getKnownMinValue() % RHS.getKnownMinValue() == 0;
  }

  /// Returns a value X where RHS.multiplyCoefficientBy(X) will result in a
  /// value whose quantity matches our own.
  constexpr ScalarTy
  getKnownScalarFactor(const FixedOrScalableQuantity &RHS) const {
    assert(hasKnownScalarFactor(RHS) && "Expected RHS to be a known factor!");
    return getKnownMinValue() / RHS.getKnownMinValue();
  }

  /// Printing function.
  void print(raw_ostream &OS) const {
    if (isScalable())
      OS << "vscale x ";
    OS << getKnownMinValue();
  }
};

} // namespace details

// Stores the number of elements for a type and whether this type is fixed
// (N-Elements) or scalable (e.g., SVE).
//  - ElementCount::getFixed(1) : A scalar value.
//  - ElementCount::getFixed(2) : A vector type holding 2 values.
//  - ElementCount::getScalable(4) : A scalable vector type holding 4 values.
class ElementCount
    : public details::FixedOrScalableQuantity<ElementCount, unsigned> {
  constexpr ElementCount(ScalarTy MinVal, bool Scalable)
      : FixedOrScalableQuantity(MinVal, Scalable) {}

  constexpr ElementCount(
      const FixedOrScalableQuantity<ElementCount, unsigned> &V)
      : FixedOrScalableQuantity(V) {}

public:
  constexpr ElementCount() : FixedOrScalableQuantity() {}

  static constexpr ElementCount getFixed(ScalarTy MinVal) {
    return ElementCount(MinVal, false);
  }
  static constexpr ElementCount getScalable(ScalarTy MinVal) {
    return ElementCount(MinVal, true);
  }
  static constexpr ElementCount get(ScalarTy MinVal, bool Scalable) {
    return ElementCount(MinVal, Scalable);
  }

  /// Exactly one element.
  constexpr bool isScalar() const {
    return !isScalable() && getKnownMinValue() == 1;
  }
  /// One or more elements.
  constexpr bool isVector() const {
    return (isScalable() && getKnownMinValue() != 0) || getKnownMinValue() > 1;
  }
};

// Stores the size of a type. If the type is of fixed size, it will represent
// the exact size. If the type is a scalable vector, it will represent the known
// minimum size.
class TypeSize : public details::FixedOrScalableQuantity<TypeSize, uint64_t> {
  TypeSize(const FixedOrScalableQuantity<TypeSize, uint64_t> &V)
      : FixedOrScalableQuantity(V) {}

public:
  constexpr TypeSize(ScalarTy Quantity, bool Scalable)
      : FixedOrScalableQuantity(Quantity, Scalable) {}

  static constexpr TypeSize get(ScalarTy Quantity, bool Scalable) {
    return TypeSize(Quantity, Scalable);
  }
  static constexpr TypeSize getFixed(ScalarTy ExactSize) {
    return TypeSize(ExactSize, false);
  }
  static constexpr TypeSize getScalable(ScalarTy MinimumSize) {
    return TypeSize(MinimumSize, true);
  }
  static constexpr TypeSize getZero() { return TypeSize(0, false); }

  // All code for this class below this point is needed because of the
  // temporary implicit conversion to uint64_t. The operator overloads are
  // needed because otherwise the conversion of the parent class
  // UnivariateLinearPolyBase -> TypeSize is ambiguous.
  // TODO: Remove the implicit conversion.

  // Casts to a uint64_t if this is a fixed-width size.
  //
  // This interface is deprecated and will be removed in a future version
  // of LLVM in favour of upgrading uses that rely on this implicit conversion
  // to uint64_t. Calls to functions that return a TypeSize should use the
  // proper interfaces to TypeSize.
  // In practice this is mostly calls to MVT/EVT::getSizeInBits().
  //
  // To determine how to upgrade the code:
  //
  //   if (<algorithm works for both scalable and fixed-width vectors>)
  //     use getKnownMinValue()
  //   else if (<algorithm works only for fixed-width vectors>) {
  //     if <algorithm can be adapted for both scalable and fixed-width vectors>
  //       update the algorithm and use getKnownMinValue()
  //     else
  //       bail out early for scalable vectors and use getFixedValue()
  //   }
  operator ScalarTy() const;

  // Additional operators needed to avoid ambiguous parses
  // because of the implicit conversion hack.
  friend constexpr TypeSize operator*(const TypeSize &LHS, const int RHS) {
    return LHS * (ScalarTy)RHS;
  }
  friend constexpr TypeSize operator*(const TypeSize &LHS, const unsigned RHS) {
    return LHS * (ScalarTy)RHS;
  }
  friend constexpr TypeSize operator*(const TypeSize &LHS, const int64_t RHS) {
    return LHS * (ScalarTy)RHS;
  }
  friend constexpr TypeSize operator*(const int LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
  friend constexpr TypeSize operator*(const unsigned LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
  friend constexpr TypeSize operator*(const int64_t LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
  friend constexpr TypeSize operator*(const uint64_t LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
};

//===----------------------------------------------------------------------===//
// Utilities
//===----------------------------------------------------------------------===//

/// Returns a TypeSize with a known minimum size that is the next integer
/// (mod 2**64) that is greater than or equal to \p Quantity and is a multiple
/// of \p Align. \p Align must be non-zero.
///
/// Similar to the alignTo functions in MathExtras.h
inline constexpr TypeSize alignTo(TypeSize Size, uint64_t Align) {
  assert(Align != 0u && "Align must be non-zero");
  return {(Size.getKnownMinValue() + Align - 1) / Align * Align,
          Size.isScalable()};
}

/// Stream operator function for `FixedOrScalableQuantity`.
template <typename LeafTy, typename ScalarTy>
inline raw_ostream &
operator<<(raw_ostream &OS,
           const details::FixedOrScalableQuantity<LeafTy, ScalarTy> &PS) {
  PS.print(OS);
  return OS;
}

template <> struct DenseMapInfo<ElementCount, void> {
  static inline ElementCount getEmptyKey() {
    return ElementCount::getScalable(~0U);
  }
  static inline ElementCount getTombstoneKey() {
    return ElementCount::getFixed(~0U - 1);
  }
  static unsigned getHashValue(const ElementCount &EltCnt) {
    unsigned HashVal = EltCnt.getKnownMinValue() * 37U;
    if (EltCnt.isScalable())
      return (HashVal - 1U);

    return HashVal;
  }
  static bool isEqual(const ElementCount &LHS, const ElementCount &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_TYPESIZE_H
