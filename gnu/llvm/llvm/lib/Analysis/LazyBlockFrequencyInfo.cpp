//===- LazyBlockFrequencyInfo.cpp - Lazy Block Frequency Analysis ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is an alternative analysis pass to BlockFrequencyInfoWrapperPass.  The
// difference is that with this pass the block frequencies are not computed when
// the analysis pass is executed but rather when the BFI result is explicitly
// requested by the analysis client.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/LazyBranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "lazy-block-freq"

INITIALIZE_PASS_BEGIN(LazyBlockFrequencyInfoPass, DEBUG_TYPE,
                      "Lazy Block Frequency Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(LazyBPIPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(LazyBlockFrequencyInfoPass, DEBUG_TYPE,
                    "Lazy Block Frequency Analysis", true, true)

char LazyBlockFrequencyInfoPass::ID = 0;

LazyBlockFrequencyInfoPass::LazyBlockFrequencyInfoPass() : FunctionPass(ID) {
  initializeLazyBlockFrequencyInfoPassPass(*PassRegistry::getPassRegistry());
}

void LazyBlockFrequencyInfoPass::print(raw_ostream &OS, const Module *) const {
  LBFI.getCalculated().print(OS);
}

void LazyBlockFrequencyInfoPass::getAnalysisUsage(AnalysisUsage &AU) const {
  LazyBranchProbabilityInfoPass::getLazyBPIAnalysisUsage(AU);
  // We require DT so it's available when LI is available. The LI updating code
  // asserts that DT is also present so if we don't make sure that we have DT
  // here, that assert will trigger.
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

void LazyBlockFrequencyInfoPass::releaseMemory() { LBFI.releaseMemory(); }

bool LazyBlockFrequencyInfoPass::runOnFunction(Function &F) {
  auto &BPIPass = getAnalysis<LazyBranchProbabilityInfoPass>();
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  LBFI.setAnalysis(&F, &BPIPass, &LI);
  return false;
}

void LazyBlockFrequencyInfoPass::getLazyBFIAnalysisUsage(AnalysisUsage &AU) {
  LazyBranchProbabilityInfoPass::getLazyBPIAnalysisUsage(AU);
  AU.addRequiredTransitive<LazyBlockFrequencyInfoPass>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
}

void llvm::initializeLazyBFIPassPass(PassRegistry &Registry) {
  initializeLazyBPIPassPass(Registry);
  INITIALIZE_PASS_DEPENDENCY(LazyBlockFrequencyInfoPass);
  INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass);
}
