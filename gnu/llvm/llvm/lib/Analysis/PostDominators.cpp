//===- PostDominators.cpp - Post-Dominator Calculation --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the post-dominator construction algorithms.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "postdomtree"

#ifdef EXPENSIVE_CHECKS
static constexpr bool ExpensiveChecksEnabled = true;
#else
static constexpr bool ExpensiveChecksEnabled = false;
#endif

//===----------------------------------------------------------------------===//
//  PostDominatorTree Implementation
//===----------------------------------------------------------------------===//

char PostDominatorTreeWrapperPass::ID = 0;

PostDominatorTreeWrapperPass::PostDominatorTreeWrapperPass()
    : FunctionPass(ID) {
  initializePostDominatorTreeWrapperPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS(PostDominatorTreeWrapperPass, "postdomtree",
                "Post-Dominator Tree Construction", true, true)

bool PostDominatorTree::invalidate(Function &F, const PreservedAnalyses &PA,
                                   FunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on functions, or the function's
  // CFG have been preserved.
  auto PAC = PA.getChecker<PostDominatorTreeAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>() ||
           PAC.preservedSet<CFGAnalyses>());
}

bool PostDominatorTree::dominates(const Instruction *I1,
                                  const Instruction *I2) const {
  assert(I1 && I2 && "Expecting valid I1 and I2");

  const BasicBlock *BB1 = I1->getParent();
  const BasicBlock *BB2 = I2->getParent();

  if (BB1 != BB2)
    return Base::dominates(BB1, BB2);

  // PHINodes in a block are unordered.
  if (isa<PHINode>(I1) && isa<PHINode>(I2))
    return false;

  // Loop through the basic block until we find I1 or I2.
  BasicBlock::const_iterator I = BB1->begin();
  for (; &*I != I1 && &*I != I2; ++I)
    /*empty*/;

  return &*I == I2;
}

bool PostDominatorTreeWrapperPass::runOnFunction(Function &F) {
  DT.recalculate(F);
  return false;
}

void PostDominatorTreeWrapperPass::verifyAnalysis() const {
  if (VerifyDomInfo)
    assert(DT.verify(PostDominatorTree::VerificationLevel::Full));
  else if (ExpensiveChecksEnabled)
    assert(DT.verify(PostDominatorTree::VerificationLevel::Basic));
}

void PostDominatorTreeWrapperPass::print(raw_ostream &OS, const Module *) const {
  DT.print(OS);
}

FunctionPass* llvm::createPostDomTree() {
  return new PostDominatorTreeWrapperPass();
}

AnalysisKey PostDominatorTreeAnalysis::Key;

PostDominatorTree PostDominatorTreeAnalysis::run(Function &F,
                                                 FunctionAnalysisManager &) {
  PostDominatorTree PDT(F);
  return PDT;
}

PostDominatorTreePrinterPass::PostDominatorTreePrinterPass(raw_ostream &OS)
  : OS(OS) {}

PreservedAnalyses
PostDominatorTreePrinterPass::run(Function &F, FunctionAnalysisManager &AM) {
  OS << "PostDominatorTree for function: " << F.getName() << "\n";
  AM.getResult<PostDominatorTreeAnalysis>(F).print(OS);

  return PreservedAnalyses::all();
}
