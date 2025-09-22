//===- CmpInstAnalysis.cpp - Utils to help fold compares ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file holds routines to help analyse compare instructions
// and fold them into constants or other compare instructions
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CmpInstAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"

using namespace llvm;

unsigned llvm::getICmpCode(CmpInst::Predicate Pred) {
  switch (Pred) {
      // False -> 0
    case ICmpInst::ICMP_UGT: return 1;  // 001
    case ICmpInst::ICMP_SGT: return 1;  // 001
    case ICmpInst::ICMP_EQ:  return 2;  // 010
    case ICmpInst::ICMP_UGE: return 3;  // 011
    case ICmpInst::ICMP_SGE: return 3;  // 011
    case ICmpInst::ICMP_ULT: return 4;  // 100
    case ICmpInst::ICMP_SLT: return 4;  // 100
    case ICmpInst::ICMP_NE:  return 5;  // 101
    case ICmpInst::ICMP_ULE: return 6;  // 110
    case ICmpInst::ICMP_SLE: return 6;  // 110
      // True -> 7
    default:
      llvm_unreachable("Invalid ICmp predicate!");
  }
}

Constant *llvm::getPredForICmpCode(unsigned Code, bool Sign, Type *OpTy,
                                   CmpInst::Predicate &Pred) {
  switch (Code) {
    default: llvm_unreachable("Illegal ICmp code!");
    case 0: // False.
      return ConstantInt::get(CmpInst::makeCmpResultType(OpTy), 0);
    case 1: Pred = Sign ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT; break;
    case 2: Pred = ICmpInst::ICMP_EQ; break;
    case 3: Pred = Sign ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE; break;
    case 4: Pred = Sign ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT; break;
    case 5: Pred = ICmpInst::ICMP_NE; break;
    case 6: Pred = Sign ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE; break;
    case 7: // True.
      return ConstantInt::get(CmpInst::makeCmpResultType(OpTy), 1);
  }
  return nullptr;
}

bool llvm::predicatesFoldable(ICmpInst::Predicate P1, ICmpInst::Predicate P2) {
  return (CmpInst::isSigned(P1) == CmpInst::isSigned(P2)) ||
         (CmpInst::isSigned(P1) && ICmpInst::isEquality(P2)) ||
         (CmpInst::isSigned(P2) && ICmpInst::isEquality(P1));
}

Constant *llvm::getPredForFCmpCode(unsigned Code, Type *OpTy,
                                   CmpInst::Predicate &Pred) {
  Pred = static_cast<FCmpInst::Predicate>(Code);
  assert(FCmpInst::FCMP_FALSE <= Pred && Pred <= FCmpInst::FCMP_TRUE &&
         "Unexpected FCmp predicate!");
  if (Pred == FCmpInst::FCMP_FALSE)
    return ConstantInt::get(CmpInst::makeCmpResultType(OpTy), 0);
  if (Pred == FCmpInst::FCMP_TRUE)
    return ConstantInt::get(CmpInst::makeCmpResultType(OpTy), 1);
  return nullptr;
}

bool llvm::decomposeBitTestICmp(Value *LHS, Value *RHS,
                                CmpInst::Predicate &Pred,
                                Value *&X, APInt &Mask, bool LookThruTrunc) {
  using namespace PatternMatch;

  const APInt *C;
  if (!match(RHS, m_APIntAllowPoison(C)))
    return false;

  switch (Pred) {
  default:
    return false;
  case ICmpInst::ICMP_SLT:
    // X < 0 is equivalent to (X & SignMask) != 0.
    if (!C->isZero())
      return false;
    Mask = APInt::getSignMask(C->getBitWidth());
    Pred = ICmpInst::ICMP_NE;
    break;
  case ICmpInst::ICMP_SLE:
    // X <= -1 is equivalent to (X & SignMask) != 0.
    if (!C->isAllOnes())
      return false;
    Mask = APInt::getSignMask(C->getBitWidth());
    Pred = ICmpInst::ICMP_NE;
    break;
  case ICmpInst::ICMP_SGT:
    // X > -1 is equivalent to (X & SignMask) == 0.
    if (!C->isAllOnes())
      return false;
    Mask = APInt::getSignMask(C->getBitWidth());
    Pred = ICmpInst::ICMP_EQ;
    break;
  case ICmpInst::ICMP_SGE:
    // X >= 0 is equivalent to (X & SignMask) == 0.
    if (!C->isZero())
      return false;
    Mask = APInt::getSignMask(C->getBitWidth());
    Pred = ICmpInst::ICMP_EQ;
    break;
  case ICmpInst::ICMP_ULT:
    // X <u 2^n is equivalent to (X & ~(2^n-1)) == 0.
    if (!C->isPowerOf2())
      return false;
    Mask = -*C;
    Pred = ICmpInst::ICMP_EQ;
    break;
  case ICmpInst::ICMP_ULE:
    // X <=u 2^n-1 is equivalent to (X & ~(2^n-1)) == 0.
    if (!(*C + 1).isPowerOf2())
      return false;
    Mask = ~*C;
    Pred = ICmpInst::ICMP_EQ;
    break;
  case ICmpInst::ICMP_UGT:
    // X >u 2^n-1 is equivalent to (X & ~(2^n-1)) != 0.
    if (!(*C + 1).isPowerOf2())
      return false;
    Mask = ~*C;
    Pred = ICmpInst::ICMP_NE;
    break;
  case ICmpInst::ICMP_UGE:
    // X >=u 2^n is equivalent to (X & ~(2^n-1)) != 0.
    if (!C->isPowerOf2())
      return false;
    Mask = -*C;
    Pred = ICmpInst::ICMP_NE;
    break;
  }

  if (LookThruTrunc && match(LHS, m_Trunc(m_Value(X)))) {
    Mask = Mask.zext(X->getType()->getScalarSizeInBits());
  } else {
    X = LHS;
  }

  return true;
}
