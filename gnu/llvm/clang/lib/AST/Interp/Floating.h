//===--- Floating.h - Types for the constexpr VM ----------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_AST_INTERP_FLOATING_H
#define LLVM_CLANG_AST_INTERP_FLOATING_H

#include "Primitives.h"
#include "clang/AST/APValue.h"
#include "llvm/ADT/APFloat.h"

namespace clang {
namespace interp {

using APFloat = llvm::APFloat;
using APSInt = llvm::APSInt;

class Floating final {
private:
  // The underlying value storage.
  APFloat F;

public:
  /// Zero-initializes a Floating.
  Floating() : F(0.0f) {}
  Floating(const APFloat &F) : F(F) {}

  // Static constructors for special floating point values.
  static Floating getInf(const llvm::fltSemantics &Sem) {
    return Floating(APFloat::getInf(Sem));
  }
  const APFloat &getAPFloat() const { return F; }

  bool operator<(Floating RHS) const { return F < RHS.F; }
  bool operator>(Floating RHS) const { return F > RHS.F; }
  bool operator<=(Floating RHS) const { return F <= RHS.F; }
  bool operator>=(Floating RHS) const { return F >= RHS.F; }
  bool operator==(Floating RHS) const { return F == RHS.F; }
  bool operator!=(Floating RHS) const { return F != RHS.F; }
  Floating operator-() const { return Floating(-F); }

  APFloat::opStatus convertToInteger(APSInt &Result) const {
    bool IsExact;
    return F.convertToInteger(Result, llvm::APFloat::rmTowardZero, &IsExact);
  }

  Floating toSemantics(const llvm::fltSemantics *Sem,
                       llvm::RoundingMode RM) const {
    APFloat Copy = F;
    bool LosesInfo;
    Copy.convert(*Sem, RM, &LosesInfo);
    (void)LosesInfo;
    return Floating(Copy);
  }

  /// Convert this Floating to one with the same semantics as \Other.
  Floating toSemantics(const Floating &Other, llvm::RoundingMode RM) const {
    return toSemantics(&Other.F.getSemantics(), RM);
  }

  APSInt toAPSInt(unsigned NumBits = 0) const {
    return APSInt(F.bitcastToAPInt());
  }
  APValue toAPValue(const ASTContext &) const { return APValue(F); }
  void print(llvm::raw_ostream &OS) const {
    // Can't use APFloat::print() since it appends a newline.
    SmallVector<char, 16> Buffer;
    F.toString(Buffer);
    OS << Buffer;
  }
  std::string toDiagnosticString(const ASTContext &Ctx) const {
    std::string NameStr;
    llvm::raw_string_ostream OS(NameStr);
    print(OS);
    return NameStr;
  }

  unsigned bitWidth() const { return F.semanticsSizeInBits(F.getSemantics()); }

  bool isSigned() const { return true; }
  bool isNegative() const { return F.isNegative(); }
  bool isPositive() const { return !F.isNegative(); }
  bool isZero() const { return F.isZero(); }
  bool isNonZero() const { return F.isNonZero(); }
  bool isMin() const { return F.isSmallest(); }
  bool isMinusOne() const { return F.isExactlyValue(-1.0); }
  bool isNan() const { return F.isNaN(); }
  bool isSignaling() const { return F.isSignaling(); }
  bool isInf() const { return F.isInfinity(); }
  bool isFinite() const { return F.isFinite(); }
  bool isNormal() const { return F.isNormal(); }
  bool isDenormal() const { return F.isDenormal(); }
  llvm::FPClassTest classify() const { return F.classify(); }
  APFloat::fltCategory getCategory() const { return F.getCategory(); }

  ComparisonCategoryResult compare(const Floating &RHS) const {
    llvm::APFloatBase::cmpResult CmpRes = F.compare(RHS.F);
    switch (CmpRes) {
    case llvm::APFloatBase::cmpLessThan:
      return ComparisonCategoryResult::Less;
    case llvm::APFloatBase::cmpEqual:
      return ComparisonCategoryResult::Equal;
    case llvm::APFloatBase::cmpGreaterThan:
      return ComparisonCategoryResult::Greater;
    case llvm::APFloatBase::cmpUnordered:
      return ComparisonCategoryResult::Unordered;
    }
    llvm_unreachable("Inavlid cmpResult value");
  }

  static APFloat::opStatus fromIntegral(APSInt Val,
                                        const llvm::fltSemantics &Sem,
                                        llvm::RoundingMode RM,
                                        Floating &Result) {
    APFloat F = APFloat(Sem);
    APFloat::opStatus Status = F.convertFromAPInt(Val, Val.isSigned(), RM);
    Result = Floating(F);
    return Status;
  }

  static Floating bitcastFromMemory(const std::byte *Buff,
                                    const llvm::fltSemantics &Sem) {
    size_t Size = APFloat::semanticsSizeInBits(Sem);
    llvm::APInt API(Size, true);
    llvm::LoadIntFromMemory(API, (const uint8_t *)Buff, Size / 8);

    return Floating(APFloat(Sem, API));
  }

  // === Serialization support ===
  size_t bytesToSerialize() const {
    return sizeof(llvm::fltSemantics *) +
           (APFloat::semanticsSizeInBits(F.getSemantics()) / 8);
  }

  void serialize(std::byte *Buff) const {
    // Semantics followed by an APInt.
    *reinterpret_cast<const llvm::fltSemantics **>(Buff) = &F.getSemantics();

    llvm::APInt API = F.bitcastToAPInt();
    llvm::StoreIntToMemory(API, (uint8_t *)(Buff + sizeof(void *)),
                           bitWidth() / 8);
  }

  static Floating deserialize(const std::byte *Buff) {
    const llvm::fltSemantics *Sem;
    std::memcpy((void *)&Sem, Buff, sizeof(void *));
    return bitcastFromMemory(Buff + sizeof(void *), *Sem);
  }

  static Floating abs(const Floating &F) {
    APFloat V = F.F;
    if (V.isNegative())
      V.changeSign();
    return Floating(V);
  }

  // -------

  static APFloat::opStatus add(const Floating &A, const Floating &B,
                               llvm::RoundingMode RM, Floating *R) {
    *R = Floating(A.F);
    return R->F.add(B.F, RM);
  }

  static APFloat::opStatus increment(const Floating &A, llvm::RoundingMode RM,
                                     Floating *R) {
    APFloat One(A.F.getSemantics(), 1);
    *R = Floating(A.F);
    return R->F.add(One, RM);
  }

  static APFloat::opStatus sub(const Floating &A, const Floating &B,
                               llvm::RoundingMode RM, Floating *R) {
    *R = Floating(A.F);
    return R->F.subtract(B.F, RM);
  }

  static APFloat::opStatus decrement(const Floating &A, llvm::RoundingMode RM,
                                     Floating *R) {
    APFloat One(A.F.getSemantics(), 1);
    *R = Floating(A.F);
    return R->F.subtract(One, RM);
  }

  static APFloat::opStatus mul(const Floating &A, const Floating &B,
                               llvm::RoundingMode RM, Floating *R) {
    *R = Floating(A.F);
    return R->F.multiply(B.F, RM);
  }

  static APFloat::opStatus div(const Floating &A, const Floating &B,
                               llvm::RoundingMode RM, Floating *R) {
    *R = Floating(A.F);
    return R->F.divide(B.F, RM);
  }

  static bool neg(const Floating &A, Floating *R) {
    *R = -A;
    return false;
  }
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, Floating F);
Floating getSwappedBytes(Floating F);

} // namespace interp
} // namespace clang

#endif
