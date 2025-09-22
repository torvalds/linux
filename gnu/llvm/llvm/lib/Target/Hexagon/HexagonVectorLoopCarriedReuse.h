//===- HexagonVectorLoopCarriedReuse.h ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass removes the computation of provably redundant expressions that have
// been computed earlier in a previous iteration. It relies on the use of PHIs
// to identify loop carried dependences. This is scalar replacement for vector
// types.
//
//-----------------------------------------------------------------------------
// Motivation: Consider the case where we have the following loop structure.
//
// Loop:
//  t0 = a[i];
//  t1 = f(t0);
//  t2 = g(t1);
//  ...
//  t3 = a[i+1];
//  t4 = f(t3);
//  t5 = g(t4);
//  t6 = op(t2, t5)
//  cond_branch <Loop>
//
// This can be converted to
//  t00 = a[0];
//  t10 = f(t00);
//  t20 = g(t10);
// Loop:
//  t2 = t20;
//  t3 = a[i+1];
//  t4 = f(t3);
//  t5 = g(t4);
//  t6 = op(t2, t5)
//  t20 = t5
//  cond_branch <Loop>
//
// SROA does a good job of reusing a[i+1] as a[i] in the next iteration.
// Such a loop comes to this pass in the following form.
//
// LoopPreheader:
//  X0 = a[0];
// Loop:
//  X2 = PHI<(X0, LoopPreheader), (X1, Loop)>
//  t1 = f(X2)   <-- I1
//  t2 = g(t1)
//  ...
//  X1 = a[i+1]
//  t4 = f(X1)   <-- I2
//  t5 = g(t4)
//  t6 = op(t2, t5)
//  cond_branch <Loop>
//
// In this pass, we look for PHIs such as X2 whose incoming values come only
// from the Loop Preheader and over the backedge and additionaly, both these
// values are the results of the same operation in terms of opcode. We call such
// a PHI node a dependence chain or DepChain. In this case, the dependence of X2
// over X1 is carried over only one iteration and so the DepChain is only one
// PHI node long.
//
// Then, we traverse the uses of the PHI (X2) and the uses of the value of the
// PHI coming  over the backedge (X1). We stop at the first pair of such users
// I1 (of X2) and I2 (of X1) that meet the following conditions.
// 1. I1 and I2 are the same operation, but with different operands.
// 2. X2 and X1 are used at the same operand number in the two instructions.
// 3. All other operands Op1 of I1 and Op2 of I2 are also such that there is a
//    a DepChain from Op1 to Op2 of the same length as that between X2 and X1.
//
// We then make the following transformation
// LoopPreheader:
//  X0 = a[0];
//  Y0 = f(X0);
// Loop:
//  X2 = PHI<(X0, LoopPreheader), (X1, Loop)>
//  Y2 = PHI<(Y0, LoopPreheader), (t4, Loop)>
//  t1 = f(X2)   <-- Will be removed by DCE.
//  t2 = g(Y2)
//  ...
//  X1 = a[i+1]
//  t4 = f(X1)
//  t5 = g(t4)
//  t6 = op(t2, t5)
//  cond_branch <Loop>
//
// We proceed until we cannot find any more such instructions I1 and I2.
//
// --- DepChains & Loop carried dependences ---
// Consider a single basic block loop such as
//
// LoopPreheader:
//  X0 = ...
//  Y0 = ...
// Loop:
//  X2 = PHI<(X0, LoopPreheader), (X1, Loop)>
//  Y2 = PHI<(Y0, LoopPreheader), (X2, Loop)>
//  ...
//  X1 = ...
//  ...
//  cond_branch <Loop>
//
// Then there is a dependence between X2 and X1 that goes back one iteration,
// i.e. X1 is used as X2 in the very next iteration. We represent this as a
// DepChain from X2 to X1 (X2->X1).
// Similarly, there is a dependence between Y2 and X1 that goes back two
// iterations. X1 is used as Y2 two iterations after it is computed. This is
// represented by a DepChain as (Y2->X2->X1).
//
// A DepChain has the following properties.
// 1. Num of edges in DepChain = Number of Instructions in DepChain = Number of
//    iterations of carried dependence + 1.
// 2. All instructions in the DepChain except the last are PHIs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONVLCR_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONVLCR_H

#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

class Loop;

/// Hexagon Vector Loop Carried Reuse Pass
struct HexagonVectorLoopCarriedReusePass
    : public PassInfoMixin<HexagonVectorLoopCarriedReusePass> {
  HexagonVectorLoopCarriedReusePass() = default;

  /// Run pass over the Loop.
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &LAM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONVLCR_H
