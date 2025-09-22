//===- SlowDynamicAPInt.cpp - SlowDynamicAPInt Implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SlowDynamicAPInt.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace detail;

SlowDynamicAPInt::SlowDynamicAPInt(int64_t Val)
    : Val(64, Val, /*isSigned=*/true) {}
SlowDynamicAPInt::SlowDynamicAPInt() : SlowDynamicAPInt(0) {}
SlowDynamicAPInt::SlowDynamicAPInt(const APInt &Val) : Val(Val) {}
SlowDynamicAPInt &SlowDynamicAPInt::operator=(int64_t Val) {
  return *this = SlowDynamicAPInt(Val);
}
SlowDynamicAPInt::operator int64_t() const { return Val.getSExtValue(); }

hash_code detail::hash_value(const SlowDynamicAPInt &X) {
  return hash_value(X.Val);
}

/// ---------------------------------------------------------------------------
/// Convenience operator overloads for int64_t.
/// ---------------------------------------------------------------------------
SlowDynamicAPInt &detail::operator+=(SlowDynamicAPInt &A, int64_t B) {
  return A += SlowDynamicAPInt(B);
}
SlowDynamicAPInt &detail::operator-=(SlowDynamicAPInt &A, int64_t B) {
  return A -= SlowDynamicAPInt(B);
}
SlowDynamicAPInt &detail::operator*=(SlowDynamicAPInt &A, int64_t B) {
  return A *= SlowDynamicAPInt(B);
}
SlowDynamicAPInt &detail::operator/=(SlowDynamicAPInt &A, int64_t B) {
  return A /= SlowDynamicAPInt(B);
}
SlowDynamicAPInt &detail::operator%=(SlowDynamicAPInt &A, int64_t B) {
  return A %= SlowDynamicAPInt(B);
}

bool detail::operator==(const SlowDynamicAPInt &A, int64_t B) {
  return A == SlowDynamicAPInt(B);
}
bool detail::operator!=(const SlowDynamicAPInt &A, int64_t B) {
  return A != SlowDynamicAPInt(B);
}
bool detail::operator>(const SlowDynamicAPInt &A, int64_t B) {
  return A > SlowDynamicAPInt(B);
}
bool detail::operator<(const SlowDynamicAPInt &A, int64_t B) {
  return A < SlowDynamicAPInt(B);
}
bool detail::operator<=(const SlowDynamicAPInt &A, int64_t B) {
  return A <= SlowDynamicAPInt(B);
}
bool detail::operator>=(const SlowDynamicAPInt &A, int64_t B) {
  return A >= SlowDynamicAPInt(B);
}
SlowDynamicAPInt detail::operator+(const SlowDynamicAPInt &A, int64_t B) {
  return A + SlowDynamicAPInt(B);
}
SlowDynamicAPInt detail::operator-(const SlowDynamicAPInt &A, int64_t B) {
  return A - SlowDynamicAPInt(B);
}
SlowDynamicAPInt detail::operator*(const SlowDynamicAPInt &A, int64_t B) {
  return A * SlowDynamicAPInt(B);
}
SlowDynamicAPInt detail::operator/(const SlowDynamicAPInt &A, int64_t B) {
  return A / SlowDynamicAPInt(B);
}
SlowDynamicAPInt detail::operator%(const SlowDynamicAPInt &A, int64_t B) {
  return A % SlowDynamicAPInt(B);
}

bool detail::operator==(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) == B;
}
bool detail::operator!=(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) != B;
}
bool detail::operator>(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) > B;
}
bool detail::operator<(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) < B;
}
bool detail::operator<=(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) <= B;
}
bool detail::operator>=(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) >= B;
}
SlowDynamicAPInt detail::operator+(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) + B;
}
SlowDynamicAPInt detail::operator-(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) - B;
}
SlowDynamicAPInt detail::operator*(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) * B;
}
SlowDynamicAPInt detail::operator/(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) / B;
}
SlowDynamicAPInt detail::operator%(int64_t A, const SlowDynamicAPInt &B) {
  return SlowDynamicAPInt(A) % B;
}

static unsigned getMaxWidth(const APInt &A, const APInt &B) {
  return std::max(A.getBitWidth(), B.getBitWidth());
}

/// ---------------------------------------------------------------------------
/// Comparison operators.
/// ---------------------------------------------------------------------------

// TODO: consider instead making APInt::compare available and using that.
bool SlowDynamicAPInt::operator==(const SlowDynamicAPInt &O) const {
  unsigned Width = getMaxWidth(Val, O.Val);
  return Val.sext(Width) == O.Val.sext(Width);
}
bool SlowDynamicAPInt::operator!=(const SlowDynamicAPInt &O) const {
  unsigned Width = getMaxWidth(Val, O.Val);
  return Val.sext(Width) != O.Val.sext(Width);
}
bool SlowDynamicAPInt::operator>(const SlowDynamicAPInt &O) const {
  unsigned Width = getMaxWidth(Val, O.Val);
  return Val.sext(Width).sgt(O.Val.sext(Width));
}
bool SlowDynamicAPInt::operator<(const SlowDynamicAPInt &O) const {
  unsigned Width = getMaxWidth(Val, O.Val);
  return Val.sext(Width).slt(O.Val.sext(Width));
}
bool SlowDynamicAPInt::operator<=(const SlowDynamicAPInt &O) const {
  unsigned Width = getMaxWidth(Val, O.Val);
  return Val.sext(Width).sle(O.Val.sext(Width));
}
bool SlowDynamicAPInt::operator>=(const SlowDynamicAPInt &O) const {
  unsigned Width = getMaxWidth(Val, O.Val);
  return Val.sext(Width).sge(O.Val.sext(Width));
}

/// ---------------------------------------------------------------------------
/// Arithmetic operators.
/// ---------------------------------------------------------------------------

/// Bring a and b to have the same width and then call op(a, b, overflow).
/// If the overflow bit becomes set, resize a and b to double the width and
/// call op(a, b, overflow), returning its result. The operation with double
/// widths should not also overflow.
APInt runOpWithExpandOnOverflow(
    const APInt &A, const APInt &B,
    function_ref<APInt(const APInt &, const APInt &, bool &Overflow)> Op) {
  bool Overflow;
  unsigned Width = getMaxWidth(A, B);
  APInt Ret = Op(A.sext(Width), B.sext(Width), Overflow);
  if (!Overflow)
    return Ret;

  Width *= 2;
  Ret = Op(A.sext(Width), B.sext(Width), Overflow);
  assert(!Overflow && "double width should be sufficient to avoid overflow!");
  return Ret;
}

SlowDynamicAPInt SlowDynamicAPInt::operator+(const SlowDynamicAPInt &O) const {
  return SlowDynamicAPInt(
      runOpWithExpandOnOverflow(Val, O.Val, std::mem_fn(&APInt::sadd_ov)));
}
SlowDynamicAPInt SlowDynamicAPInt::operator-(const SlowDynamicAPInt &O) const {
  return SlowDynamicAPInt(
      runOpWithExpandOnOverflow(Val, O.Val, std::mem_fn(&APInt::ssub_ov)));
}
SlowDynamicAPInt SlowDynamicAPInt::operator*(const SlowDynamicAPInt &O) const {
  return SlowDynamicAPInt(
      runOpWithExpandOnOverflow(Val, O.Val, std::mem_fn(&APInt::smul_ov)));
}
SlowDynamicAPInt SlowDynamicAPInt::operator/(const SlowDynamicAPInt &O) const {
  return SlowDynamicAPInt(
      runOpWithExpandOnOverflow(Val, O.Val, std::mem_fn(&APInt::sdiv_ov)));
}
SlowDynamicAPInt detail::abs(const SlowDynamicAPInt &X) {
  return X >= 0 ? X : -X;
}
SlowDynamicAPInt detail::ceilDiv(const SlowDynamicAPInt &LHS,
                                 const SlowDynamicAPInt &RHS) {
  if (RHS == -1)
    return -LHS;
  unsigned Width = getMaxWidth(LHS.Val, RHS.Val);
  return SlowDynamicAPInt(APIntOps::RoundingSDiv(
      LHS.Val.sext(Width), RHS.Val.sext(Width), APInt::Rounding::UP));
}
SlowDynamicAPInt detail::floorDiv(const SlowDynamicAPInt &LHS,
                                  const SlowDynamicAPInt &RHS) {
  if (RHS == -1)
    return -LHS;
  unsigned Width = getMaxWidth(LHS.Val, RHS.Val);
  return SlowDynamicAPInt(APIntOps::RoundingSDiv(
      LHS.Val.sext(Width), RHS.Val.sext(Width), APInt::Rounding::DOWN));
}
// The RHS is always expected to be positive, and the result
/// is always non-negative.
SlowDynamicAPInt detail::mod(const SlowDynamicAPInt &LHS,
                             const SlowDynamicAPInt &RHS) {
  assert(RHS >= 1 && "mod is only supported for positive divisors!");
  return LHS % RHS < 0 ? LHS % RHS + RHS : LHS % RHS;
}

SlowDynamicAPInt detail::gcd(const SlowDynamicAPInt &A,
                             const SlowDynamicAPInt &B) {
  assert(A >= 0 && B >= 0 && "operands must be non-negative!");
  unsigned Width = getMaxWidth(A.Val, B.Val);
  return SlowDynamicAPInt(
      APIntOps::GreatestCommonDivisor(A.Val.sext(Width), B.Val.sext(Width)));
}

/// Returns the least common multiple of A and B.
SlowDynamicAPInt detail::lcm(const SlowDynamicAPInt &A,
                             const SlowDynamicAPInt &B) {
  SlowDynamicAPInt X = abs(A);
  SlowDynamicAPInt Y = abs(B);
  return (X * Y) / gcd(X, Y);
}

/// This operation cannot overflow.
SlowDynamicAPInt SlowDynamicAPInt::operator%(const SlowDynamicAPInt &O) const {
  unsigned Width = std::max(Val.getBitWidth(), O.Val.getBitWidth());
  return SlowDynamicAPInt(Val.sext(Width).srem(O.Val.sext(Width)));
}

SlowDynamicAPInt SlowDynamicAPInt::operator-() const {
  if (Val.isMinSignedValue()) {
    /// Overflow only occurs when the value is the minimum possible value.
    APInt Ret = Val.sext(2 * Val.getBitWidth());
    return SlowDynamicAPInt(-Ret);
  }
  return SlowDynamicAPInt(-Val);
}

/// ---------------------------------------------------------------------------
/// Assignment operators, preincrement, predecrement.
/// ---------------------------------------------------------------------------
SlowDynamicAPInt &SlowDynamicAPInt::operator+=(const SlowDynamicAPInt &O) {
  *this = *this + O;
  return *this;
}
SlowDynamicAPInt &SlowDynamicAPInt::operator-=(const SlowDynamicAPInt &O) {
  *this = *this - O;
  return *this;
}
SlowDynamicAPInt &SlowDynamicAPInt::operator*=(const SlowDynamicAPInt &O) {
  *this = *this * O;
  return *this;
}
SlowDynamicAPInt &SlowDynamicAPInt::operator/=(const SlowDynamicAPInt &O) {
  *this = *this / O;
  return *this;
}
SlowDynamicAPInt &SlowDynamicAPInt::operator%=(const SlowDynamicAPInt &O) {
  *this = *this % O;
  return *this;
}
SlowDynamicAPInt &SlowDynamicAPInt::operator++() {
  *this += 1;
  return *this;
}

SlowDynamicAPInt &SlowDynamicAPInt::operator--() {
  *this -= 1;
  return *this;
}

/// ---------------------------------------------------------------------------
/// Printing.
/// ---------------------------------------------------------------------------
void SlowDynamicAPInt::print(raw_ostream &OS) const { OS << Val; }

void SlowDynamicAPInt::dump() const { print(dbgs()); }
