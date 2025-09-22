//===- LICM.h - Loop Invariant Code Motion Pass -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs loop invariant code motion, attempting to remove as much
// code from the body of a loop as possible.  It does this by either hoisting
// code into the preheader block, or by sinking code to the exit blocks if it is
// safe.  This pass also promotes must-aliased memory locations in the loop to
// live in registers, thus hoisting and sinking "invariant" loads and stores.
//
// This pass uses alias analysis for two purposes:
//
//  1. Moving loop invariant loads and calls out of loops.  If we can determine
//     that a load or call inside of a loop never aliases anything stored to,
//     we can hoist it or sink it like any other instruction.
//  2. Scalar Promotion of Memory - If there is a store instruction inside of
//     the loop, we try to move the store to happen AFTER the loop instead of
//     inside of the loop.  This can only happen if a few conditions are true:
//       A. The pointer stored through is loop invariant
//       B. There are no stores or loads in the loop which _may_ alias the
//          pointer.  There are no calls in the loop which mod/ref the pointer.
//     If these conditions are true, we can promote the loads and stores in the
//     loop of the pointer to use a temporary alloca'd variable.  We then use
//     the SSAUpdater to construct the appropriate SSA form for the value.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LICM_H
#define LLVM_TRANSFORMS_SCALAR_LICM_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

class LPMUpdater;
class Loop;
class LoopNest;

extern cl::opt<unsigned> SetLicmMssaOptCap;
extern cl::opt<unsigned> SetLicmMssaNoAccForPromotionCap;

struct LICMOptions {
  unsigned MssaOptCap;
  unsigned MssaNoAccForPromotionCap;
  bool AllowSpeculation;

  LICMOptions()
      : MssaOptCap(SetLicmMssaOptCap),
        MssaNoAccForPromotionCap(SetLicmMssaNoAccForPromotionCap),
        AllowSpeculation(true) {}

  LICMOptions(unsigned MssaOptCap, unsigned MssaNoAccForPromotionCap,
              bool AllowSpeculation)
      : MssaOptCap(MssaOptCap),
        MssaNoAccForPromotionCap(MssaNoAccForPromotionCap),
        AllowSpeculation(AllowSpeculation) {}
};

/// Performs Loop Invariant Code Motion Pass.
class LICMPass : public PassInfoMixin<LICMPass> {
  LICMOptions Opts;

public:
  LICMPass(unsigned MssaOptCap, unsigned MssaNoAccForPromotionCap,
           bool AllowSpeculation)
      : LICMPass(LICMOptions(MssaOptCap, MssaNoAccForPromotionCap,
                             AllowSpeculation)) {}
  LICMPass(LICMOptions Opts) : Opts(Opts) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};

/// Performs LoopNest Invariant Code Motion Pass.
class LNICMPass : public PassInfoMixin<LNICMPass> {
  LICMOptions Opts;

public:
  LNICMPass(unsigned MssaOptCap, unsigned MssaNoAccForPromotionCap,
            bool AllowSpeculation)
      : LNICMPass(LICMOptions(MssaOptCap, MssaNoAccForPromotionCap,
                              AllowSpeculation)) {}
  LNICMPass(LICMOptions Opts) : Opts(Opts) {}

  PreservedAnalyses run(LoopNest &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LICM_H
