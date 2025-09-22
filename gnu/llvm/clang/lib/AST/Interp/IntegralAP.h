//===--- Integral.h - Wrapper for numeric types for the VM ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the VM types and helpers operating on types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_INTEGRAL_AP_H
#define LLVM_CLANG_AST_INTERP_INTEGRAL_AP_H

#include "clang/AST/APValue.h"
#include "clang/AST/ComparisonCategories.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>

#include "Primitives.h"

namespace clang {
namespace interp {

using APInt = llvm::APInt;
using APSInt = llvm::APSInt;
template <unsigned Bits, bool Signed> class Integral;

template <bool Signed> class IntegralAP final {
private:
  friend IntegralAP<!Signed>;
  APInt V;

  template <typename T, bool InputSigned>
  static T truncateCast(const APInt &V) {
    constexpr unsigned BitSize = sizeof(T) * 8;
    if (BitSize >= V.getBitWidth()) {
      APInt Extended;
      if constexpr (InputSigned)
        Extended = V.sext(BitSize);
      else
        Extended = V.zext(BitSize);
      return std::is_signed_v<T> ? Extended.getSExtValue()
                                 : Extended.getZExtValue();
    }

    return std::is_signed_v<T> ? V.trunc(BitSize).getSExtValue()
                               : V.trunc(BitSize).getZExtValue();
  }

public:
  using AsUnsigned = IntegralAP<false>;

  template <typename T>
  IntegralAP(T Value, unsigned BitWidth)
      : V(APInt(BitWidth, static_cast<uint64_t>(Value), Signed)) {}

  IntegralAP(APInt V) : V(V) {}
  /// Arbitrary value for uninitialized variables.
  IntegralAP() : IntegralAP(-1, 3) {}

  IntegralAP operator-() const { return IntegralAP(-V); }
  IntegralAP operator-(const IntegralAP &Other) const {
    return IntegralAP(V - Other.V);
  }
  bool operator>(const IntegralAP &RHS) const {
    if constexpr (Signed)
      return V.ugt(RHS.V);
    return V.sgt(RHS.V);
  }
  bool operator>=(IntegralAP RHS) const {
    if constexpr (Signed)
      return V.uge(RHS.V);
    return V.sge(RHS.V);
  }
  bool operator<(IntegralAP RHS) const {
    if constexpr (Signed)
      return V.slt(RHS.V);
    return V.slt(RHS.V);
  }
  bool operator<=(IntegralAP RHS) const {
    if constexpr (Signed)
      return V.ult(RHS.V);
    return V.ult(RHS.V);
  }

  template <typename Ty, typename = std::enable_if_t<std::is_integral_v<Ty>>>
  explicit operator Ty() const {
    return truncateCast<Ty, Signed>(V);
  }

  template <typename T> static IntegralAP from(T Value, unsigned NumBits = 0) {
    assert(NumBits > 0);
    APInt Copy = APInt(NumBits, static_cast<uint64_t>(Value), Signed);

    return IntegralAP<Signed>(Copy);
  }

  template <bool InputSigned>
  static IntegralAP from(IntegralAP<InputSigned> V, unsigned NumBits = 0) {
    if (NumBits == 0)
      NumBits = V.bitWidth();

    if constexpr (InputSigned)
      return IntegralAP<Signed>(V.V.sextOrTrunc(NumBits));
    return IntegralAP<Signed>(V.V.zextOrTrunc(NumBits));
  }

  template <unsigned Bits, bool InputSigned>
  static IntegralAP from(Integral<Bits, InputSigned> I, unsigned BitWidth) {
    APInt Copy = APInt(BitWidth, static_cast<uint64_t>(I), InputSigned);

    return IntegralAP<Signed>(Copy);
  }

  static IntegralAP zero(int32_t BitWidth) {
    APInt V = APInt(BitWidth, 0LL, Signed);
    return IntegralAP(V);
  }

  constexpr unsigned bitWidth() const { return V.getBitWidth(); }

  APSInt toAPSInt(unsigned Bits = 0) const {
    if (Bits == 0)
      Bits = bitWidth();

    if constexpr (Signed)
      return APSInt(V.sext(Bits), !Signed);
    else
      return APSInt(V.zext(Bits), !Signed);
  }
  APValue toAPValue(const ASTContext &) const { return APValue(toAPSInt()); }

  bool isZero() const { return V.isZero(); }
  bool isPositive() const { return V.isNonNegative(); }
  bool isNegative() const { return !V.isNonNegative(); }
  bool isMin() const { return V.isMinValue(); }
  bool isMax() const { return V.isMaxValue(); }
  static constexpr bool isSigned() { return Signed; }
  bool isMinusOne() const { return Signed && V == -1; }

  unsigned countLeadingZeros() const { return V.countl_zero(); }

  void print(llvm::raw_ostream &OS) const { OS << V; }
  std::string toDiagnosticString(const ASTContext &Ctx) const {
    std::string NameStr;
    llvm::raw_string_ostream OS(NameStr);
    print(OS);
    return NameStr;
  }

  IntegralAP truncate(unsigned BitWidth) const {
    if constexpr (Signed)
      return IntegralAP(V.trunc(BitWidth).sextOrTrunc(this->bitWidth()));
    else
      return IntegralAP(V.trunc(BitWidth).zextOrTrunc(this->bitWidth()));
  }

  IntegralAP<false> toUnsigned() const {
    APInt Copy = V;
    return IntegralAP<false>(Copy);
  }

  ComparisonCategoryResult compare(const IntegralAP &RHS) const {
    assert(Signed == RHS.isSigned());
    assert(bitWidth() == RHS.bitWidth());
    if constexpr (Signed) {
      if (V.slt(RHS.V))
        return ComparisonCategoryResult::Less;
      if (V.sgt(RHS.V))
        return ComparisonCategoryResult::Greater;
      return ComparisonCategoryResult::Equal;
    }

    assert(!Signed);
    if (V.ult(RHS.V))
      return ComparisonCategoryResult::Less;
    if (V.ugt(RHS.V))
      return ComparisonCategoryResult::Greater;
    return ComparisonCategoryResult::Equal;
  }

  static bool increment(IntegralAP A, IntegralAP *R) {
    IntegralAP<Signed> One(1, A.bitWidth());
    return add(A, One, A.bitWidth() + 1, R);
  }

  static bool decrement(IntegralAP A, IntegralAP *R) {
    IntegralAP<Signed> One(1, A.bitWidth());
    return sub(A, One, A.bitWidth() + 1, R);
  }

  static bool add(IntegralAP A, IntegralAP B, unsigned OpBits, IntegralAP *R) {
    return CheckAddSubMulUB<std::plus>(A, B, OpBits, R);
  }

  static bool sub(IntegralAP A, IntegralAP B, unsigned OpBits, IntegralAP *R) {
    return CheckAddSubMulUB<std::minus>(A, B, OpBits, R);
  }

  static bool mul(IntegralAP A, IntegralAP B, unsigned OpBits, IntegralAP *R) {
    return CheckAddSubMulUB<std::multiplies>(A, B, OpBits, R);
  }

  static bool rem(IntegralAP A, IntegralAP B, unsigned OpBits, IntegralAP *R) {
    if constexpr (Signed)
      *R = IntegralAP(A.V.srem(B.V));
    else
      *R = IntegralAP(A.V.urem(B.V));
    return false;
  }

  static bool div(IntegralAP A, IntegralAP B, unsigned OpBits, IntegralAP *R) {
    if constexpr (Signed)
      *R = IntegralAP(A.V.sdiv(B.V));
    else
      *R = IntegralAP(A.V.udiv(B.V));
    return false;
  }

  static bool bitAnd(IntegralAP A, IntegralAP B, unsigned OpBits,
                     IntegralAP *R) {
    *R = IntegralAP(A.V & B.V);
    return false;
  }

  static bool bitOr(IntegralAP A, IntegralAP B, unsigned OpBits,
                    IntegralAP *R) {
    *R = IntegralAP(A.V | B.V);
    return false;
  }

  static bool bitXor(IntegralAP A, IntegralAP B, unsigned OpBits,
                     IntegralAP *R) {
    *R = IntegralAP(A.V ^ B.V);
    return false;
  }

  static bool neg(const IntegralAP &A, IntegralAP *R) {
    APInt AI = A.V;
    AI.negate();
    *R = IntegralAP(AI);
    return false;
  }

  static bool comp(IntegralAP A, IntegralAP *R) {
    *R = IntegralAP(~A.V);
    return false;
  }

  static void shiftLeft(const IntegralAP A, const IntegralAP B, unsigned OpBits,
                        IntegralAP *R) {
    *R = IntegralAP(A.V.shl(B.V.getZExtValue()));
  }

  static void shiftRight(const IntegralAP A, const IntegralAP B,
                         unsigned OpBits, IntegralAP *R) {
    unsigned ShiftAmount = B.V.getZExtValue();
    if constexpr (Signed)
      *R = IntegralAP(A.V.ashr(ShiftAmount));
    else
      *R = IntegralAP(A.V.lshr(ShiftAmount));
  }

  // === Serialization support ===
  size_t bytesToSerialize() const {
    // 4 bytes for the BitWidth followed by N bytes for the actual APInt.
    return sizeof(uint32_t) + (V.getBitWidth() / CHAR_BIT);
  }

  void serialize(std::byte *Buff) const {
    assert(V.getBitWidth() < std::numeric_limits<uint8_t>::max());
    uint32_t BitWidth = V.getBitWidth();

    std::memcpy(Buff, &BitWidth, sizeof(uint32_t));
    llvm::StoreIntToMemory(V, (uint8_t *)(Buff + sizeof(uint32_t)),
                           BitWidth / CHAR_BIT);
  }

  static IntegralAP<Signed> deserialize(const std::byte *Buff) {
    uint32_t BitWidth;
    std::memcpy(&BitWidth, Buff, sizeof(uint32_t));
    IntegralAP<Signed> Val(APInt(BitWidth, 0ull, !Signed));

    llvm::LoadIntFromMemory(Val.V, (const uint8_t *)Buff + sizeof(uint32_t),
                            BitWidth / CHAR_BIT);
    return Val;
  }

private:
  template <template <typename T> class Op>
  static bool CheckAddSubMulUB(const IntegralAP &A, const IntegralAP &B,
                               unsigned BitWidth, IntegralAP *R) {
    if constexpr (!Signed) {
      R->V = Op<APInt>{}(A.V, B.V);
      return false;
    }

    const APSInt &LHS = A.toAPSInt();
    const APSInt &RHS = B.toAPSInt();
    APSInt Value = Op<APSInt>{}(LHS.extend(BitWidth), RHS.extend(BitWidth));
    APSInt Result = Value.trunc(LHS.getBitWidth());
    R->V = Result;

    return Result.extend(BitWidth) != Value;
  }
};

template <bool Signed>
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     IntegralAP<Signed> I) {
  I.print(OS);
  return OS;
}

template <bool Signed>
IntegralAP<Signed> getSwappedBytes(IntegralAP<Signed> F) {
  return F;
}

} // namespace interp
} // namespace clang

#endif
