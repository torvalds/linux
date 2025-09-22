//==-- OverflowInstAnalysis.cpp - Utils to fold overflow insts ----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file holds routines to help analyse overflow instructions
// and fold them into constants or other overflow instructions
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/OverflowInstAnalysis.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"

using namespace llvm;
using namespace llvm::PatternMatch;

bool llvm::isCheckForZeroAndMulWithOverflow(Value *Op0, Value *Op1, bool IsAnd,
                                            Use *&Y) {
  ICmpInst::Predicate Pred;
  Value *X, *NotOp1;
  int XIdx;
  IntrinsicInst *II;

  if (!match(Op0, m_ICmp(Pred, m_Value(X), m_Zero())))
    return false;

  ///   %Agg = call { i4, i1 } @llvm.[us]mul.with.overflow.i4(i4 %X, i4 %???)
  ///   %V = extractvalue { i4, i1 } %Agg, 1
  auto matchMulOverflowCheck = [X, &II, &XIdx](Value *V) {
    auto *Extract = dyn_cast<ExtractValueInst>(V);
    // We should only be extracting the overflow bit.
    if (!Extract || !Extract->getIndices().equals(1))
      return false;

    II = dyn_cast<IntrinsicInst>(Extract->getAggregateOperand());
    if (!II ||
        !match(II, m_CombineOr(m_Intrinsic<Intrinsic::umul_with_overflow>(),
                               m_Intrinsic<Intrinsic::smul_with_overflow>())))
      return false;

    if (II->getArgOperand(0) == X)
      XIdx = 0;
    else if (II->getArgOperand(1) == X)
      XIdx = 1;
    else
      return false;
    return true;
  };

  bool Matched =
      (IsAnd && Pred == ICmpInst::Predicate::ICMP_NE &&
       matchMulOverflowCheck(Op1)) ||
      (!IsAnd && Pred == ICmpInst::Predicate::ICMP_EQ &&
       match(Op1, m_Not(m_Value(NotOp1))) && matchMulOverflowCheck(NotOp1));

  if (!Matched)
    return false;

  Y = &II->getArgOperandUse(!XIdx);
  return true;
}

bool llvm::isCheckForZeroAndMulWithOverflow(Value *Op0, Value *Op1,
                                            bool IsAnd) {
  Use *Y;
  return isCheckForZeroAndMulWithOverflow(Op0, Op1, IsAnd, Y);
}
