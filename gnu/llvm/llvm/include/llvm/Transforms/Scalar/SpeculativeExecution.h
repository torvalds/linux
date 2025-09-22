//===- SpeculativeExecution.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass hoists instructions to enable speculative execution on
// targets where branches are expensive. This is aimed at GPUs. It
// currently works on simple if-then and if-then-else
// patterns.
//
// Removing branches is not the only motivation for this
// pass. E.g. consider this code and assume that there is no
// addressing mode for multiplying by sizeof(*a):
//
//   if (b > 0)
//     c = a[i + 1]
//   if (d > 0)
//     e = a[i + 2]
//
// turns into
//
//   p = &a[i + 1];
//   if (b > 0)
//     c = *p;
//   q = &a[i + 2];
//   if (d > 0)
//     e = *q;
//
// which could later be optimized to
//
//   r = &a[i];
//   if (b > 0)
//     c = r[1];
//   if (d > 0)
//     e = r[2];
//
// Later passes sink back much of the speculated code that did not enable
// further optimization.
//
// This pass is more aggressive than the function SpeculativeyExecuteBB in
// SimplifyCFG. SimplifyCFG will not speculate if no selects are introduced and
// it will speculate at most one instruction. It also will not speculate if
// there is a value defined in the if-block that is only used in the then-block.
// These restrictions make sense since the speculation in SimplifyCFG seems
// aimed at introducing cheap selects, while this pass is intended to do more
// aggressive speculation while counting on later passes to either capitalize on
// that or clean it up.
//
// If the pass was created by calling
// createSpeculativeExecutionIfHasBranchDivergencePass or the
// -spec-exec-only-if-divergent-target option is present, this pass only has an
// effect on targets where TargetTransformInfo::hasBranchDivergence() is true;
// on other targets, it is a nop.
//
// This lets you include this pass unconditionally in the IR pass pipeline, but
// only enable it for relevant targets.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_SCALAR_SPECULATIVEEXECUTION_H
#define LLVM_TRANSFORMS_SCALAR_SPECULATIVEEXECUTION_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class BasicBlock;
class TargetTransformInfo;

class SpeculativeExecutionPass
    : public PassInfoMixin<SpeculativeExecutionPass> {
public:
  SpeculativeExecutionPass(bool OnlyIfDivergentTarget = false);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

  // Glue for old PM
  bool runImpl(Function &F, TargetTransformInfo *TTI);

private:
  bool runOnBasicBlock(BasicBlock &B);
  bool considerHoistingFromTo(BasicBlock &FromBlock, BasicBlock &ToBlock);

  // If true, this pass is a nop unless the target architecture has branch
  // divergence.
  const bool OnlyIfDivergentTarget = false;

  TargetTransformInfo *TTI = nullptr;
};
}

#endif // LLVM_TRANSFORMS_SCALAR_SPECULATIVEEXECUTION_H
