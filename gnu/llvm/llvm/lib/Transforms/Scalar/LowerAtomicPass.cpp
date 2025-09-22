//===- LowerAtomic.cpp - Lower atomic intrinsics --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LowerAtomicPass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/LowerAtomic.h"
using namespace llvm;

#define DEBUG_TYPE "lower-atomic"

static bool LowerFenceInst(FenceInst *FI) {
  FI->eraseFromParent();
  return true;
}

static bool LowerLoadInst(LoadInst *LI) {
  LI->setAtomic(AtomicOrdering::NotAtomic);
  return true;
}

static bool LowerStoreInst(StoreInst *SI) {
  SI->setAtomic(AtomicOrdering::NotAtomic);
  return true;
}

static bool runOnBasicBlock(BasicBlock &BB) {
  bool Changed = false;
  for (Instruction &Inst : make_early_inc_range(BB)) {
    if (FenceInst *FI = dyn_cast<FenceInst>(&Inst))
      Changed |= LowerFenceInst(FI);
    else if (AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(&Inst))
      Changed |= lowerAtomicCmpXchgInst(CXI);
    else if (AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(&Inst))
      Changed |= lowerAtomicRMWInst(RMWI);
    else if (LoadInst *LI = dyn_cast<LoadInst>(&Inst)) {
      if (LI->isAtomic())
        LowerLoadInst(LI);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&Inst)) {
      if (SI->isAtomic())
        LowerStoreInst(SI);
    }
  }
  return Changed;
}

static bool lowerAtomics(Function &F) {
  bool Changed = false;
  for (BasicBlock &BB : F) {
    Changed |= runOnBasicBlock(BB);
  }
  return Changed;
}

PreservedAnalyses LowerAtomicPass::run(Function &F, FunctionAnalysisManager &) {
  if (lowerAtomics(F))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

namespace {
class LowerAtomicLegacyPass : public FunctionPass {
public:
  static char ID;

  LowerAtomicLegacyPass() : FunctionPass(ID) {
    initializeLowerAtomicLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    // Don't skip optnone functions; atomics still need to be lowered.
    FunctionAnalysisManager DummyFAM;
    auto PA = Impl.run(F, DummyFAM);
    return !PA.areAllPreserved();
  }

private:
  LowerAtomicPass Impl;
  };
}

char LowerAtomicLegacyPass::ID = 0;
INITIALIZE_PASS(LowerAtomicLegacyPass, "loweratomic",
                "Lower atomic intrinsics to non-atomic form", false, false)

Pass *llvm::createLowerAtomicPass() { return new LowerAtomicLegacyPass(); }
