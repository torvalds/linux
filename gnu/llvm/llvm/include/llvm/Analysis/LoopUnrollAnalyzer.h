//===- llvm/Analysis/LoopUnrollAnalyzer.h - Loop Unroll Analyzer-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements UnrolledInstAnalyzer class. It's used for predicting
// potential effects that loop unrolling might have, such as enabling constant
// propagation and other optimizations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPUNROLLANALYZER_H
#define LLVM_ANALYSIS_LOOPUNROLLANALYZER_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/InstVisitor.h"

// This class is used to get an estimate of the optimization effects that we
// could get from complete loop unrolling. It comes from the fact that some
// loads might be replaced with concrete constant values and that could trigger
// a chain of instruction simplifications.
//
// E.g. we might have:
//   int a[] = {0, 1, 0};
//   v = 0;
//   for (i = 0; i < 3; i ++)
//     v += b[i]*a[i];
// If we completely unroll the loop, we would get:
//   v = b[0]*a[0] + b[1]*a[1] + b[2]*a[2]
// Which then will be simplified to:
//   v = b[0]* 0 + b[1]* 1 + b[2]* 0
// And finally:
//   v = b[1]
namespace llvm {
class Instruction;

class UnrolledInstAnalyzer : private InstVisitor<UnrolledInstAnalyzer, bool> {
  typedef InstVisitor<UnrolledInstAnalyzer, bool> Base;
  friend class InstVisitor<UnrolledInstAnalyzer, bool>;
  struct SimplifiedAddress {
    Value *Base = nullptr;
    ConstantInt *Offset = nullptr;
  };

public:
  UnrolledInstAnalyzer(unsigned Iteration,
                       DenseMap<Value *, Value *> &SimplifiedValues,
                       ScalarEvolution &SE, const Loop *L)
      : SimplifiedValues(SimplifiedValues), SE(SE), L(L) {
      IterationNumber = SE.getConstant(APInt(64, Iteration));
  }

  // Allow access to the initial visit method.
  using Base::visit;

private:
  /// A cache of pointer bases and constant-folded offsets corresponding
  /// to GEP (or derived from GEP) instructions.
  ///
  /// In order to find the base pointer one needs to perform non-trivial
  /// traversal of the corresponding SCEV expression, so it's good to have the
  /// results saved.
  DenseMap<Value *, SimplifiedAddress> SimplifiedAddresses;

  /// SCEV expression corresponding to number of currently simulated
  /// iteration.
  const SCEV *IterationNumber;

  /// While we walk the loop instructions, we build up and maintain a mapping
  /// of simplified values specific to this iteration.  The idea is to propagate
  /// any special information we have about loads that can be replaced with
  /// constants after complete unrolling, and account for likely simplifications
  /// post-unrolling.
  DenseMap<Value *, Value *> &SimplifiedValues;

  ScalarEvolution &SE;
  const Loop *L;

  bool simplifyInstWithSCEV(Instruction *I);

  bool visitInstruction(Instruction &I);
  bool visitBinaryOperator(BinaryOperator &I);
  bool visitLoad(LoadInst &I);
  bool visitCastInst(CastInst &I);
  bool visitCmpInst(CmpInst &I);
  bool visitPHINode(PHINode &PN);
};
}
#endif
