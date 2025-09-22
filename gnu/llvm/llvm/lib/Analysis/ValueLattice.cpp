//===- ValueLattice.cpp - Value constraint analysis -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ValueLattice.h"
#include "llvm/Analysis/ConstantFolding.h"

namespace llvm {
Constant *
ValueLatticeElement::getCompare(CmpInst::Predicate Pred, Type *Ty,
                                const ValueLatticeElement &Other,
                                const DataLayout &DL) const {
  // Not yet resolved.
  if (isUnknown() || Other.isUnknown())
    return nullptr;

  // TODO: Can be made more precise, but always returning undef would be
  // incorrect.
  if (isUndef() || Other.isUndef())
    return nullptr;

  if (isConstant() && Other.isConstant())
    return ConstantFoldCompareInstOperands(Pred, getConstant(),
                                           Other.getConstant(), DL);

  if (ICmpInst::isEquality(Pred)) {
    // not(C) != C => true, not(C) == C => false.
    if ((isNotConstant() && Other.isConstant() &&
         getNotConstant() == Other.getConstant()) ||
        (isConstant() && Other.isNotConstant() &&
         getConstant() == Other.getNotConstant()))
      return Pred == ICmpInst::ICMP_NE ? ConstantInt::getTrue(Ty)
                                       : ConstantInt::getFalse(Ty);
  }

  // Integer constants are represented as ConstantRanges with single
  // elements.
  if (!isConstantRange() || !Other.isConstantRange())
    return nullptr;

  const auto &CR = getConstantRange();
  const auto &OtherCR = Other.getConstantRange();
  if (CR.icmp(Pred, OtherCR))
    return ConstantInt::getTrue(Ty);
  if (CR.icmp(CmpInst::getInversePredicate(Pred), OtherCR))
    return ConstantInt::getFalse(Ty);

  return nullptr;
}

raw_ostream &operator<<(raw_ostream &OS, const ValueLatticeElement &Val) {
  if (Val.isUnknown())
    return OS << "unknown";
  if (Val.isUndef())
    return OS << "undef";
  if (Val.isOverdefined())
    return OS << "overdefined";

  if (Val.isNotConstant())
    return OS << "notconstant<" << *Val.getNotConstant() << ">";

  if (Val.isConstantRangeIncludingUndef())
    return OS << "constantrange incl. undef <"
              << Val.getConstantRange(true).getLower() << ", "
              << Val.getConstantRange(true).getUpper() << ">";

  if (Val.isConstantRange())
    return OS << "constantrange<" << Val.getConstantRange().getLower() << ", "
              << Val.getConstantRange().getUpper() << ">";
  return OS << "constant<" << *Val.getConstant() << ">";
}
} // end namespace llvm
