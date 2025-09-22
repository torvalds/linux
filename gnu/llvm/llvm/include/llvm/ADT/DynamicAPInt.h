//===- DynamicAPInt.h - DynamicAPInt Class ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a simple class to represent arbitrary precision signed integers.
// Unlike APInt, one does not have to specify a fixed maximum size, and the
// integer can take on any arbitrary values. This is optimized for small-values
// by providing fast-paths for the cases when the value stored fits in 64-bits.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DYNAMICAPINT_H
#define LLVM_ADT_DYNAMICAPINT_H

#include "llvm/ADT/SlowDynamicAPInt.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <numeric>

namespace llvm {
/// This class provides support for dynamic arbitrary-precision arithmetic.
///
/// Unlike APInt, this extends the precision as necessary to prevent overflows
/// and supports operations between objects with differing internal precisions.
///
/// This is optimized for small-values by providing fast-paths for the cases
/// when the value stored fits in 64-bits. We annotate all fastpaths by using
/// the LLVM_LIKELY/LLVM_UNLIKELY annotations. Removing these would result in
/// a 1.2x performance slowdown.
///
/// We always_inline all operations; removing these results in a 1.5x
/// performance slowdown.
///
/// When isLarge returns true, a SlowMPInt is held in the union. If isSmall
/// returns true, the int64_t is held. We don't have a separate field for
/// indicating this, and instead "steal" memory from ValLarge when it is not in
/// use because we know that the memory layout of APInt is such that BitWidth
/// doesn't overlap with ValSmall (see static_assert_layout). Using std::variant
/// instead would lead to significantly worse performance.
class DynamicAPInt {
  union {
    int64_t ValSmall;
    detail::SlowDynamicAPInt ValLarge;
  };

  LLVM_ATTRIBUTE_ALWAYS_INLINE void initSmall(int64_t O) {
    if (LLVM_UNLIKELY(isLarge()))
      ValLarge.detail::SlowDynamicAPInt::~SlowDynamicAPInt();
    ValSmall = O;
    ValLarge.Val.BitWidth = 0;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE void
  initLarge(const detail::SlowDynamicAPInt &O) {
    if (LLVM_LIKELY(isSmall())) {
      // The data in memory could be in an arbitrary state, not necessarily
      // corresponding to any valid state of ValLarge; we cannot call any member
      // functions, e.g. the assignment operator on it, as they may access the
      // invalid internal state. We instead construct a new object using
      // placement new.
      new (&ValLarge) detail::SlowDynamicAPInt(O);
    } else {
      // In this case, we need to use the assignment operator, because if we use
      // placement-new as above we would lose track of allocated memory
      // and leak it.
      ValLarge = O;
    }
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE explicit DynamicAPInt(
      const detail::SlowDynamicAPInt &Val)
      : ValLarge(Val) {}
  LLVM_ATTRIBUTE_ALWAYS_INLINE constexpr bool isSmall() const {
    return ValLarge.Val.BitWidth == 0;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE constexpr bool isLarge() const {
    return !isSmall();
  }
  /// Get the stored value. For getSmall/Large,
  /// the stored value should be small/large.
  LLVM_ATTRIBUTE_ALWAYS_INLINE int64_t getSmall() const {
    assert(isSmall() &&
           "getSmall should only be called when the value stored is small!");
    return ValSmall;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE int64_t &getSmall() {
    assert(isSmall() &&
           "getSmall should only be called when the value stored is small!");
    return ValSmall;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE const detail::SlowDynamicAPInt &
  getLarge() const {
    assert(isLarge() &&
           "getLarge should only be called when the value stored is large!");
    return ValLarge;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE detail::SlowDynamicAPInt &getLarge() {
    assert(isLarge() &&
           "getLarge should only be called when the value stored is large!");
    return ValLarge;
  }
  explicit operator detail::SlowDynamicAPInt() const {
    if (isSmall())
      return detail::SlowDynamicAPInt(getSmall());
    return getLarge();
  }

public:
  LLVM_ATTRIBUTE_ALWAYS_INLINE explicit DynamicAPInt(int64_t Val)
      : ValSmall(Val) {
    ValLarge.Val.BitWidth = 0;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt() : DynamicAPInt(0) {}
  LLVM_ATTRIBUTE_ALWAYS_INLINE ~DynamicAPInt() {
    if (LLVM_UNLIKELY(isLarge()))
      ValLarge.detail::SlowDynamicAPInt::~SlowDynamicAPInt();
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt(const DynamicAPInt &O)
      : ValSmall(O.ValSmall) {
    ValLarge.Val.BitWidth = 0;
    if (LLVM_UNLIKELY(O.isLarge()))
      initLarge(O.ValLarge);
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator=(const DynamicAPInt &O) {
    if (LLVM_LIKELY(O.isSmall())) {
      initSmall(O.ValSmall);
      return *this;
    }
    initLarge(O.ValLarge);
    return *this;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator=(int X) {
    initSmall(X);
    return *this;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE explicit operator int64_t() const {
    if (isSmall())
      return getSmall();
    return static_cast<int64_t>(getLarge());
  }

  bool operator==(const DynamicAPInt &O) const;
  bool operator!=(const DynamicAPInt &O) const;
  bool operator>(const DynamicAPInt &O) const;
  bool operator<(const DynamicAPInt &O) const;
  bool operator<=(const DynamicAPInt &O) const;
  bool operator>=(const DynamicAPInt &O) const;
  DynamicAPInt operator+(const DynamicAPInt &O) const;
  DynamicAPInt operator-(const DynamicAPInt &O) const;
  DynamicAPInt operator*(const DynamicAPInt &O) const;
  DynamicAPInt operator/(const DynamicAPInt &O) const;
  DynamicAPInt operator%(const DynamicAPInt &O) const;
  DynamicAPInt &operator+=(const DynamicAPInt &O);
  DynamicAPInt &operator-=(const DynamicAPInt &O);
  DynamicAPInt &operator*=(const DynamicAPInt &O);
  DynamicAPInt &operator/=(const DynamicAPInt &O);
  DynamicAPInt &operator%=(const DynamicAPInt &O);
  DynamicAPInt operator-() const;
  DynamicAPInt &operator++();
  DynamicAPInt &operator--();

  // Divide by a number that is known to be positive.
  // This is slightly more efficient because it saves an overflow check.
  DynamicAPInt divByPositive(const DynamicAPInt &O) const;
  DynamicAPInt &divByPositiveInPlace(const DynamicAPInt &O);

  friend DynamicAPInt abs(const DynamicAPInt &X);
  friend DynamicAPInt ceilDiv(const DynamicAPInt &LHS, const DynamicAPInt &RHS);
  friend DynamicAPInt floorDiv(const DynamicAPInt &LHS,
                               const DynamicAPInt &RHS);
  // The operands must be non-negative for gcd.
  friend DynamicAPInt gcd(const DynamicAPInt &A, const DynamicAPInt &B);
  friend DynamicAPInt lcm(const DynamicAPInt &A, const DynamicAPInt &B);
  friend DynamicAPInt mod(const DynamicAPInt &LHS, const DynamicAPInt &RHS);

  /// ---------------------------------------------------------------------------
  /// Convenience operator overloads for int64_t.
  /// ---------------------------------------------------------------------------
  friend DynamicAPInt &operator+=(DynamicAPInt &A, int64_t B);
  friend DynamicAPInt &operator-=(DynamicAPInt &A, int64_t B);
  friend DynamicAPInt &operator*=(DynamicAPInt &A, int64_t B);
  friend DynamicAPInt &operator/=(DynamicAPInt &A, int64_t B);
  friend DynamicAPInt &operator%=(DynamicAPInt &A, int64_t B);

  friend bool operator==(const DynamicAPInt &A, int64_t B);
  friend bool operator!=(const DynamicAPInt &A, int64_t B);
  friend bool operator>(const DynamicAPInt &A, int64_t B);
  friend bool operator<(const DynamicAPInt &A, int64_t B);
  friend bool operator<=(const DynamicAPInt &A, int64_t B);
  friend bool operator>=(const DynamicAPInt &A, int64_t B);
  friend DynamicAPInt operator+(const DynamicAPInt &A, int64_t B);
  friend DynamicAPInt operator-(const DynamicAPInt &A, int64_t B);
  friend DynamicAPInt operator*(const DynamicAPInt &A, int64_t B);
  friend DynamicAPInt operator/(const DynamicAPInt &A, int64_t B);
  friend DynamicAPInt operator%(const DynamicAPInt &A, int64_t B);

  friend bool operator==(int64_t A, const DynamicAPInt &B);
  friend bool operator!=(int64_t A, const DynamicAPInt &B);
  friend bool operator>(int64_t A, const DynamicAPInt &B);
  friend bool operator<(int64_t A, const DynamicAPInt &B);
  friend bool operator<=(int64_t A, const DynamicAPInt &B);
  friend bool operator>=(int64_t A, const DynamicAPInt &B);
  friend DynamicAPInt operator+(int64_t A, const DynamicAPInt &B);
  friend DynamicAPInt operator-(int64_t A, const DynamicAPInt &B);
  friend DynamicAPInt operator*(int64_t A, const DynamicAPInt &B);
  friend DynamicAPInt operator/(int64_t A, const DynamicAPInt &B);
  friend DynamicAPInt operator%(int64_t A, const DynamicAPInt &B);

  friend hash_code hash_value(const DynamicAPInt &x); // NOLINT

  void static_assert_layout(); // NOLINT

  raw_ostream &print(raw_ostream &OS) const;
  LLVM_DUMP_METHOD void dump() const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const DynamicAPInt &X) {
  X.print(OS);
  return OS;
}

/// Redeclarations of friend declaration above to
/// make it discoverable by lookups.
hash_code hash_value(const DynamicAPInt &X); // NOLINT

/// This just calls through to the operator int64_t, but it's useful when a
/// function pointer is required. (Although this is marked inline, it is still
/// possible to obtain and use a function pointer to this.)
static inline int64_t int64fromDynamicAPInt(const DynamicAPInt &X) {
  return int64_t(X);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt dynamicAPIntFromInt64(int64_t X) {
  return DynamicAPInt(X);
}

// The RHS is always expected to be positive, and the result
/// is always non-negative.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt mod(const DynamicAPInt &LHS,
                                              const DynamicAPInt &RHS);

/// We define the operations here in the header to facilitate inlining.

/// ---------------------------------------------------------------------------
/// Comparison operators.
/// ---------------------------------------------------------------------------
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator==(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() == O.getSmall();
  return detail::SlowDynamicAPInt(*this) == detail::SlowDynamicAPInt(O);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator!=(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() != O.getSmall();
  return detail::SlowDynamicAPInt(*this) != detail::SlowDynamicAPInt(O);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator>(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() > O.getSmall();
  return detail::SlowDynamicAPInt(*this) > detail::SlowDynamicAPInt(O);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator<(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() < O.getSmall();
  return detail::SlowDynamicAPInt(*this) < detail::SlowDynamicAPInt(O);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator<=(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() <= O.getSmall();
  return detail::SlowDynamicAPInt(*this) <= detail::SlowDynamicAPInt(O);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator>=(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() >= O.getSmall();
  return detail::SlowDynamicAPInt(*this) >= detail::SlowDynamicAPInt(O);
}

/// ---------------------------------------------------------------------------
/// Arithmetic operators.
/// ---------------------------------------------------------------------------

LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator+(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    DynamicAPInt Result;
    bool Overflow = AddOverflow(getSmall(), O.getSmall(), Result.getSmall());
    if (LLVM_LIKELY(!Overflow))
      return Result;
    return DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                        detail::SlowDynamicAPInt(O));
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                      detail::SlowDynamicAPInt(O));
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator-(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    DynamicAPInt Result;
    bool Overflow = SubOverflow(getSmall(), O.getSmall(), Result.getSmall());
    if (LLVM_LIKELY(!Overflow))
      return Result;
    return DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                        detail::SlowDynamicAPInt(O));
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                      detail::SlowDynamicAPInt(O));
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator*(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    DynamicAPInt Result;
    bool Overflow = MulOverflow(getSmall(), O.getSmall(), Result.getSmall());
    if (LLVM_LIKELY(!Overflow))
      return Result;
    return DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                        detail::SlowDynamicAPInt(O));
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                      detail::SlowDynamicAPInt(O));
}

// Division overflows only occur when negating the minimal possible value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::divByPositive(const DynamicAPInt &O) const {
  assert(O > 0);
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return DynamicAPInt(getSmall() / O.getSmall());
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                      detail::SlowDynamicAPInt(O));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator/(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    // Division overflows only occur when negating the minimal possible value.
    if (LLVM_UNLIKELY(divideSignedWouldOverflow(getSmall(), O.getSmall())))
      return -*this;
    return DynamicAPInt(getSmall() / O.getSmall());
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                      detail::SlowDynamicAPInt(O));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt abs(const DynamicAPInt &X) {
  return DynamicAPInt(X >= 0 ? X : -X);
}
// Division overflows only occur when negating the minimal possible value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt ceilDiv(const DynamicAPInt &LHS,
                                                  const DynamicAPInt &RHS) {
  if (LLVM_LIKELY(LHS.isSmall() && RHS.isSmall())) {
    if (LLVM_UNLIKELY(
            divideSignedWouldOverflow(LHS.getSmall(), RHS.getSmall())))
      return -LHS;
    return DynamicAPInt(divideCeilSigned(LHS.getSmall(), RHS.getSmall()));
  }
  return DynamicAPInt(
      ceilDiv(detail::SlowDynamicAPInt(LHS), detail::SlowDynamicAPInt(RHS)));
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt floorDiv(const DynamicAPInt &LHS,
                                                   const DynamicAPInt &RHS) {
  if (LLVM_LIKELY(LHS.isSmall() && RHS.isSmall())) {
    if (LLVM_UNLIKELY(
            divideSignedWouldOverflow(LHS.getSmall(), RHS.getSmall())))
      return -LHS;
    return DynamicAPInt(divideFloorSigned(LHS.getSmall(), RHS.getSmall()));
  }
  return DynamicAPInt(
      floorDiv(detail::SlowDynamicAPInt(LHS), detail::SlowDynamicAPInt(RHS)));
}
// The RHS is always expected to be positive, and the result
/// is always non-negative.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt mod(const DynamicAPInt &LHS,
                                              const DynamicAPInt &RHS) {
  if (LLVM_LIKELY(LHS.isSmall() && RHS.isSmall()))
    return DynamicAPInt(mod(LHS.getSmall(), RHS.getSmall()));
  return DynamicAPInt(
      mod(detail::SlowDynamicAPInt(LHS), detail::SlowDynamicAPInt(RHS)));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt gcd(const DynamicAPInt &A,
                                              const DynamicAPInt &B) {
  assert(A >= 0 && B >= 0 && "operands must be non-negative!");
  if (LLVM_LIKELY(A.isSmall() && B.isSmall()))
    return DynamicAPInt(std::gcd(A.getSmall(), B.getSmall()));
  return DynamicAPInt(
      gcd(detail::SlowDynamicAPInt(A), detail::SlowDynamicAPInt(B)));
}

/// Returns the least common multiple of A and B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt lcm(const DynamicAPInt &A,
                                              const DynamicAPInt &B) {
  DynamicAPInt X = abs(A);
  DynamicAPInt Y = abs(B);
  return (X * Y) / gcd(X, Y);
}

/// This operation cannot overflow.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator%(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return DynamicAPInt(getSmall() % O.getSmall());
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) %
                      detail::SlowDynamicAPInt(O));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt DynamicAPInt::operator-() const {
  if (LLVM_LIKELY(isSmall())) {
    if (LLVM_LIKELY(getSmall() != std::numeric_limits<int64_t>::min()))
      return DynamicAPInt(-getSmall());
    return DynamicAPInt(-detail::SlowDynamicAPInt(*this));
  }
  return DynamicAPInt(-detail::SlowDynamicAPInt(*this));
}

/// ---------------------------------------------------------------------------
/// Assignment operators, preincrement, predecrement.
/// ---------------------------------------------------------------------------
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator+=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    int64_t Result = getSmall();
    bool Overflow = AddOverflow(getSmall(), O.getSmall(), Result);
    if (LLVM_LIKELY(!Overflow)) {
      getSmall() = Result;
      return *this;
    }
    // Note: this return is not strictly required but
    // removing it leads to a performance regression.
    return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                                detail::SlowDynamicAPInt(O));
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                              detail::SlowDynamicAPInt(O));
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator-=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    int64_t Result = getSmall();
    bool Overflow = SubOverflow(getSmall(), O.getSmall(), Result);
    if (LLVM_LIKELY(!Overflow)) {
      getSmall() = Result;
      return *this;
    }
    // Note: this return is not strictly required but
    // removing it leads to a performance regression.
    return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                                detail::SlowDynamicAPInt(O));
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                              detail::SlowDynamicAPInt(O));
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator*=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    int64_t Result = getSmall();
    bool Overflow = MulOverflow(getSmall(), O.getSmall(), Result);
    if (LLVM_LIKELY(!Overflow)) {
      getSmall() = Result;
      return *this;
    }
    // Note: this return is not strictly required but
    // removing it leads to a performance regression.
    return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                                detail::SlowDynamicAPInt(O));
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                              detail::SlowDynamicAPInt(O));
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator/=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    // Division overflows only occur when negating the minimal possible value.
    if (LLVM_UNLIKELY(divideSignedWouldOverflow(getSmall(), O.getSmall())))
      return *this = -*this;
    getSmall() /= O.getSmall();
    return *this;
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                              detail::SlowDynamicAPInt(O));
}

// Division overflows only occur when the divisor is -1.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::divByPositiveInPlace(const DynamicAPInt &O) {
  assert(O > 0);
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    getSmall() /= O.getSmall();
    return *this;
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                              detail::SlowDynamicAPInt(O));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator%=(const DynamicAPInt &O) {
  return *this = *this % O;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &DynamicAPInt::operator++() {
  return *this += 1;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &DynamicAPInt::operator--() {
  return *this -= 1;
}

/// ----------------------------------------------------------------------------
/// Convenience operator overloads for int64_t.
/// ----------------------------------------------------------------------------
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator+=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A + B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator-=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A - B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator*=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A * B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator/=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A / B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator%=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A % B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator+(const DynamicAPInt &A,
                                                    int64_t B) {
  return A + DynamicAPInt(B);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator-(const DynamicAPInt &A,
                                                    int64_t B) {
  return A - DynamicAPInt(B);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator*(const DynamicAPInt &A,
                                                    int64_t B) {
  return A * DynamicAPInt(B);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator/(const DynamicAPInt &A,
                                                    int64_t B) {
  return A / DynamicAPInt(B);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator%(const DynamicAPInt &A,
                                                    int64_t B) {
  return A % DynamicAPInt(B);
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator+(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) + B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator-(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) - B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator*(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) * B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator/(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) / B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator%(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) % B;
}

/// We provide special implementations of the comparison operators rather than
/// calling through as above, as this would result in a 1.2x slowdown.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator==(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() == B;
  return A.getLarge() == B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator!=(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() != B;
  return A.getLarge() != B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() > B;
  return A.getLarge() > B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() < B;
  return A.getLarge() < B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<=(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() <= B;
  return A.getLarge() <= B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>=(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() >= B;
  return A.getLarge() >= B;
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator==(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A == B.getSmall();
  return A == B.getLarge();
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator!=(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A != B.getSmall();
  return A != B.getLarge();
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A > B.getSmall();
  return A > B.getLarge();
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A < B.getSmall();
  return A < B.getLarge();
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<=(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A <= B.getSmall();
  return A <= B.getLarge();
}
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>=(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A >= B.getSmall();
  return A >= B.getLarge();
}
} // namespace llvm

#endif // LLVM_ADT_DYNAMICAPINT_H
