//===- FlattenCFGPass.cpp - CFG Flatten Pass ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements flattening of CFG.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/FlattenCFG.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "flatten-cfg"

namespace {
struct FlattenCFGLegacyPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
public:
  FlattenCFGLegacyPass() : FunctionPass(ID) {
    initializeFlattenCFGLegacyPassPass(*PassRegistry::getPassRegistry());
  }
  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AAResultsWrapperPass>();
  }

private:
  AliasAnalysis *AA;
};

/// iterativelyFlattenCFG - Call FlattenCFG on all the blocks in the function,
/// iterating until no more changes are made.
bool iterativelyFlattenCFG(Function &F, AliasAnalysis *AA) {
  bool Changed = false;
  bool LocalChange = true;

  // Use block handles instead of iterating over function blocks directly
  // to avoid using iterators invalidated by erasing blocks.
  std::vector<WeakVH> Blocks;
  Blocks.reserve(F.size());
  for (auto &BB : F)
    Blocks.push_back(&BB);

  while (LocalChange) {
    LocalChange = false;

    // Loop over all of the basic blocks and try to flatten them.
    for (WeakVH &BlockHandle : Blocks) {
      // Skip blocks erased by FlattenCFG.
      if (auto *BB = cast_or_null<BasicBlock>(BlockHandle))
        if (FlattenCFG(BB, AA))
          LocalChange = true;
    }
    Changed |= LocalChange;
  }
  return Changed;
}
} // namespace

char FlattenCFGLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(FlattenCFGLegacyPass, "flattencfg", "Flatten the CFG",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(FlattenCFGLegacyPass, "flattencfg", "Flatten the CFG",
                    false, false)

// Public interface to the FlattenCFG pass
FunctionPass *llvm::createFlattenCFGPass() {
  return new FlattenCFGLegacyPass();
}

bool FlattenCFGLegacyPass::runOnFunction(Function &F) {
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  bool EverChanged = false;
  // iterativelyFlattenCFG can make some blocks dead.
  while (iterativelyFlattenCFG(F, AA)) {
    removeUnreachableBlocks(F);
    EverChanged = true;
  }
  return EverChanged;
}

PreservedAnalyses FlattenCFGPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  bool EverChanged = false;
  AliasAnalysis *AA = &AM.getResult<AAManager>(F);
  // iterativelyFlattenCFG can make some blocks dead.
  while (iterativelyFlattenCFG(F, AA)) {
    removeUnreachableBlocks(F);
    EverChanged = true;
  }
  return EverChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
