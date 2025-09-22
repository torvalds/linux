//===-- WebAssemblyOptimizeReturned.cpp - Optimize "returned" attributes --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Optimize calls with "returned" attributes for WebAssembly.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-optimize-returned"

namespace {
class OptimizeReturned final : public FunctionPass,
                               public InstVisitor<OptimizeReturned> {
  StringRef getPassName() const override {
    return "WebAssembly Optimize Returned";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override;

  DominatorTree *DT = nullptr;

public:
  static char ID;
  OptimizeReturned() : FunctionPass(ID) {}

  void visitCallBase(CallBase &CB);
};
} // End anonymous namespace

char OptimizeReturned::ID = 0;
INITIALIZE_PASS(OptimizeReturned, DEBUG_TYPE,
                "Optimize calls with \"returned\" attributes for WebAssembly",
                false, false)

FunctionPass *llvm::createWebAssemblyOptimizeReturned() {
  return new OptimizeReturned();
}

void OptimizeReturned::visitCallBase(CallBase &CB) {
  for (unsigned I = 0, E = CB.arg_size(); I < E; ++I)
    if (CB.paramHasAttr(I, Attribute::Returned)) {
      Value *Arg = CB.getArgOperand(I);
      // Ignore constants, globals, undef, etc.
      if (isa<Constant>(Arg))
        continue;
      // Like replaceDominatedUsesWith but using Instruction/Use dominance.
      Arg->replaceUsesWithIf(&CB,
                             [&](Use &U) { return DT->dominates(&CB, U); });
    }
}

bool OptimizeReturned::runOnFunction(Function &F) {
  LLVM_DEBUG(dbgs() << "********** Optimize returned Attributes **********\n"
                       "********** Function: "
                    << F.getName() << '\n');

  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  visit(F);
  return true;
}
