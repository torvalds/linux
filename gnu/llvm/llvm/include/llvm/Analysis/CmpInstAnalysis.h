//===-- CmpInstAnalysis.h - Utils to help fold compare insts ----*- C++ -*-===//
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

#ifndef LLVM_ANALYSIS_CMPINSTANALYSIS_H
#define LLVM_ANALYSIS_CMPINSTANALYSIS_H

#include "llvm/IR/InstrTypes.h"

namespace llvm {
  class Type;
  class Value;

  /// Encode a icmp predicate into a three bit mask. These bits are carefully
  /// arranged to allow folding of expressions such as:
  ///
  ///      (A < B) | (A > B) --> (A != B)
  ///
  /// Note that this is only valid if the first and second predicates have the
  /// same sign. It is illegal to do: (A u< B) | (A s> B)
  ///
  /// Three bits are used to represent the condition, as follows:
  ///   0  A > B
  ///   1  A == B
  ///   2  A < B
  ///
  /// <=>  Value  Definition
  /// 000     0   Always false
  /// 001     1   A >  B
  /// 010     2   A == B
  /// 011     3   A >= B
  /// 100     4   A <  B
  /// 101     5   A != B
  /// 110     6   A <= B
  /// 111     7   Always true
  ///
  unsigned getICmpCode(CmpInst::Predicate Pred);

  /// This is the complement of getICmpCode. It turns a predicate code into
  /// either a constant true or false or the predicate for a new ICmp.
  /// The sign is passed in to determine which kind of predicate to use in the
  /// new ICmp instruction.
  /// Non-NULL return value will be a true or false constant.
  /// NULL return means a new ICmp is needed. The predicate is output in Pred.
  Constant *getPredForICmpCode(unsigned Code, bool Sign, Type *OpTy,
                               CmpInst::Predicate &Pred);

  /// Return true if both predicates match sign or if at least one of them is an
  /// equality comparison (which is signless).
  bool predicatesFoldable(CmpInst::Predicate P1, CmpInst::Predicate P2);

  /// Similar to getICmpCode but for FCmpInst. This encodes a fcmp predicate
  /// into a four bit mask.
  inline unsigned getFCmpCode(CmpInst::Predicate CC) {
    assert(CmpInst::FCMP_FALSE <= CC && CC <= CmpInst::FCMP_TRUE &&
           "Unexpected FCmp predicate!");
    // Take advantage of the bit pattern of CmpInst::Predicate here.
    //                                          U L G E
    static_assert(CmpInst::FCMP_FALSE == 0); // 0 0 0 0
    static_assert(CmpInst::FCMP_OEQ == 1);   // 0 0 0 1
    static_assert(CmpInst::FCMP_OGT == 2);   // 0 0 1 0
    static_assert(CmpInst::FCMP_OGE == 3);   // 0 0 1 1
    static_assert(CmpInst::FCMP_OLT == 4);   // 0 1 0 0
    static_assert(CmpInst::FCMP_OLE == 5);   // 0 1 0 1
    static_assert(CmpInst::FCMP_ONE == 6);   // 0 1 1 0
    static_assert(CmpInst::FCMP_ORD == 7);   // 0 1 1 1
    static_assert(CmpInst::FCMP_UNO == 8);   // 1 0 0 0
    static_assert(CmpInst::FCMP_UEQ == 9);   // 1 0 0 1
    static_assert(CmpInst::FCMP_UGT == 10);  // 1 0 1 0
    static_assert(CmpInst::FCMP_UGE == 11);  // 1 0 1 1
    static_assert(CmpInst::FCMP_ULT == 12);  // 1 1 0 0
    static_assert(CmpInst::FCMP_ULE == 13);  // 1 1 0 1
    static_assert(CmpInst::FCMP_UNE == 14);  // 1 1 1 0
    static_assert(CmpInst::FCMP_TRUE == 15); // 1 1 1 1
    return CC;
  }

  /// This is the complement of getFCmpCode. It turns a predicate code into
  /// either a constant true or false or the predicate for a new FCmp.
  /// Non-NULL return value will be a true or false constant.
  /// NULL return means a new ICmp is needed. The predicate is output in Pred.
  Constant *getPredForFCmpCode(unsigned Code, Type *OpTy,
                               CmpInst::Predicate &Pred);

  /// Decompose an icmp into the form ((X & Mask) pred 0) if possible. The
  /// returned predicate is either == or !=. Returns false if decomposition
  /// fails.
  bool decomposeBitTestICmp(Value *LHS, Value *RHS, CmpInst::Predicate &Pred,
                            Value *&X, APInt &Mask,
                            bool LookThroughTrunc = true);

} // end namespace llvm

#endif
