//===- SlowDynamicAPInt.h - SlowDynamicAPInt Class --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a simple class to represent arbitrary precision signed integers.
// Unlike APInt, one does not have to specify a fixed maximum size, and the
// integer can take on any arbitrary values.
//
// This class is to be used as a fallback slow path for the DynamicAPInt class,
// and is not intended to be used directly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SLOWDYNAMICAPINT_H
#define LLVM_ADT_SLOWDYNAMICAPINT_H

#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class DynamicAPInt;
} // namespace llvm

namespace llvm::detail {
/// A simple class providing dynamic arbitrary-precision arithmetic. Internally,
/// it stores an APInt, whose width is doubled whenever an overflow occurs at a
/// certain width. The default constructor sets the initial width to 64.
/// SlowDynamicAPInt is primarily intended to be used as a slow fallback path
/// for the upcoming DynamicAPInt class.
class SlowDynamicAPInt {
  APInt Val;

public:
  explicit SlowDynamicAPInt(int64_t Val);
  SlowDynamicAPInt();
  explicit SlowDynamicAPInt(const APInt &Val);
  SlowDynamicAPInt &operator=(int64_t Val);
  explicit operator int64_t() const;
  SlowDynamicAPInt operator-() const;
  bool operator==(const SlowDynamicAPInt &O) const;
  bool operator!=(const SlowDynamicAPInt &O) const;
  bool operator>(const SlowDynamicAPInt &O) const;
  bool operator<(const SlowDynamicAPInt &O) const;
  bool operator<=(const SlowDynamicAPInt &O) const;
  bool operator>=(const SlowDynamicAPInt &O) const;
  SlowDynamicAPInt operator+(const SlowDynamicAPInt &O) const;
  SlowDynamicAPInt operator-(const SlowDynamicAPInt &O) const;
  SlowDynamicAPInt operator*(const SlowDynamicAPInt &O) const;
  SlowDynamicAPInt operator/(const SlowDynamicAPInt &O) const;
  SlowDynamicAPInt operator%(const SlowDynamicAPInt &O) const;
  SlowDynamicAPInt &operator+=(const SlowDynamicAPInt &O);
  SlowDynamicAPInt &operator-=(const SlowDynamicAPInt &O);
  SlowDynamicAPInt &operator*=(const SlowDynamicAPInt &O);
  SlowDynamicAPInt &operator/=(const SlowDynamicAPInt &O);
  SlowDynamicAPInt &operator%=(const SlowDynamicAPInt &O);

  SlowDynamicAPInt &operator++();
  SlowDynamicAPInt &operator--();

  friend SlowDynamicAPInt abs(const SlowDynamicAPInt &X);
  friend SlowDynamicAPInt ceilDiv(const SlowDynamicAPInt &LHS,
                                  const SlowDynamicAPInt &RHS);
  friend SlowDynamicAPInt floorDiv(const SlowDynamicAPInt &LHS,
                                   const SlowDynamicAPInt &RHS);
  /// The operands must be non-negative for gcd.
  friend SlowDynamicAPInt gcd(const SlowDynamicAPInt &A,
                              const SlowDynamicAPInt &B);

  /// Overload to compute a hash_code for a SlowDynamicAPInt value.
  friend hash_code hash_value(const SlowDynamicAPInt &X); // NOLINT

  // Make DynamicAPInt a friend so it can access Val directly.
  friend DynamicAPInt;

  unsigned getBitWidth() const { return Val.getBitWidth(); }

  void print(raw_ostream &OS) const;
  LLVM_DUMP_METHOD void dump() const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const SlowDynamicAPInt &X) {
  X.print(OS);
  return OS;
}

/// Returns the remainder of dividing LHS by RHS.
///
/// The RHS is always expected to be positive, and the result
/// is always non-negative.
SlowDynamicAPInt mod(const SlowDynamicAPInt &LHS, const SlowDynamicAPInt &RHS);

/// Returns the least common multiple of A and B.
SlowDynamicAPInt lcm(const SlowDynamicAPInt &A, const SlowDynamicAPInt &B);

/// Redeclarations of friend declarations above to
/// make it discoverable by lookups.
SlowDynamicAPInt abs(const SlowDynamicAPInt &X);
SlowDynamicAPInt ceilDiv(const SlowDynamicAPInt &LHS,
                         const SlowDynamicAPInt &RHS);
SlowDynamicAPInt floorDiv(const SlowDynamicAPInt &LHS,
                          const SlowDynamicAPInt &RHS);
SlowDynamicAPInt gcd(const SlowDynamicAPInt &A, const SlowDynamicAPInt &B);
hash_code hash_value(const SlowDynamicAPInt &X); // NOLINT

/// ---------------------------------------------------------------------------
/// Convenience operator overloads for int64_t.
/// ---------------------------------------------------------------------------
SlowDynamicAPInt &operator+=(SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt &operator-=(SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt &operator*=(SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt &operator/=(SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt &operator%=(SlowDynamicAPInt &A, int64_t B);

bool operator==(const SlowDynamicAPInt &A, int64_t B);
bool operator!=(const SlowDynamicAPInt &A, int64_t B);
bool operator>(const SlowDynamicAPInt &A, int64_t B);
bool operator<(const SlowDynamicAPInt &A, int64_t B);
bool operator<=(const SlowDynamicAPInt &A, int64_t B);
bool operator>=(const SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt operator+(const SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt operator-(const SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt operator*(const SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt operator/(const SlowDynamicAPInt &A, int64_t B);
SlowDynamicAPInt operator%(const SlowDynamicAPInt &A, int64_t B);

bool operator==(int64_t A, const SlowDynamicAPInt &B);
bool operator!=(int64_t A, const SlowDynamicAPInt &B);
bool operator>(int64_t A, const SlowDynamicAPInt &B);
bool operator<(int64_t A, const SlowDynamicAPInt &B);
bool operator<=(int64_t A, const SlowDynamicAPInt &B);
bool operator>=(int64_t A, const SlowDynamicAPInt &B);
SlowDynamicAPInt operator+(int64_t A, const SlowDynamicAPInt &B);
SlowDynamicAPInt operator-(int64_t A, const SlowDynamicAPInt &B);
SlowDynamicAPInt operator*(int64_t A, const SlowDynamicAPInt &B);
SlowDynamicAPInt operator/(int64_t A, const SlowDynamicAPInt &B);
SlowDynamicAPInt operator%(int64_t A, const SlowDynamicAPInt &B);
} // namespace llvm::detail

#endif // LLVM_ADT_SLOWDYNAMICAPINT_H
