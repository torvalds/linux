//===------- LoopBoundSplit.h - Split Loop Bound ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPBOUNDSPLIT_H
#define LLVM_TRANSFORMS_SCALAR_LOOPBOUNDSPLIT_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class Loop;

/// This pass transforms loops that contain a conditional branch with induction
/// variable. For example, it transforms left code to right code:
///
///                              newbound = min(n, c)
///  while (iv < n) {            while(iv < newbound) {
///    A                           A
///    if (iv < c)                 B
///      B                         C
///    C                         }
///                              if (iv != n) {
///                                while (iv < n) {
///                                  A
///                                  C
///                                }
///                              }
class LoopBoundSplitPass : public PassInfoMixin<LoopBoundSplitPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPBOUNDSPLIT_H
