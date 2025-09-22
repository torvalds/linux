//===- AMDGPURewriteUndefForPHI.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file implements the idea to rewrite undef incoming operand for certain
// PHIs in structurized CFG. This pass only works on IR that has gone through
// StructurizedCFG pass, and this pass has some additional limitation that make
// it can only run after SIAnnotateControlFlow.
//
// To achieve optimal code generation for AMDGPU, we assume that uniformity
// analysis reports the PHI in join block of divergent branch as uniform if
// it has one unique uniform value plus additional undefined/poisoned incoming
// value. That is to say the later compiler pipeline will ensure such PHI always
// return uniform value and ensure it work correctly. Let's take a look at two
// typical patterns in structured CFG that need to be taken care: (In both
// patterns, block %if terminate with divergent branch.)
//
// Pattern A: Block with undefined incoming value dominates defined predecessor
//  %if
//  | \
//  | %then
//  | /
//  %endif: %phi = phi [%undef, %if], [%uniform, %then]
//
//  Pattern B: Block with defined incoming value dominates undefined predecessor
//  %if
//  | \
//  | %then
//  | /
//  %endif: %phi = phi [%uniform, %if], [%undef, %then]
//
// For pattern A, by reporting %phi as uniform, the later pipeline need to make
// sure it be handled correctly. The backend usually allocates a scalar register
// and if any thread in a wave takes %then path, the scalar register will get
// the %uniform value.
//
// For pattern B, we will replace the undef operand with the other defined value
// in this pass. So the scalar register allocated for such PHI will get correct
// liveness. Without this transformation, the scalar register may be overwritten
// in the %then block.
//
// Limitation note:
// If the join block of divergent threads is a loop header, the pass cannot
// handle it correctly right now. For below case, the undef in %phi should also
// be rewritten. Currently we depend on SIAnnotateControlFlow to split %header
// block to get a separate join block, then we can rewrite the undef correctly.
//     %if
//     | \
//     | %then
//     | /
// -> %header: %phi = phi [%uniform, %if], [%undef, %then], [%uniform2, %header]
// |   |
// \---

#include "AMDGPU.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-rewrite-undef-for-phi"

namespace {

class AMDGPURewriteUndefForPHILegacy : public FunctionPass {
public:
  static char ID;
  AMDGPURewriteUndefForPHILegacy() : FunctionPass(ID) {
    initializeAMDGPURewriteUndefForPHILegacyPass(*PassRegistry::getPassRegistry());
  }
  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override {
    return "AMDGPU Rewrite Undef for PHI";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<UniformityInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();

    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.setPreservesCFG();
  }
};

} // end anonymous namespace
char AMDGPURewriteUndefForPHILegacy::ID = 0;

INITIALIZE_PASS_BEGIN(AMDGPURewriteUndefForPHILegacy, DEBUG_TYPE,
                      "Rewrite undef for PHI", false, false)
INITIALIZE_PASS_DEPENDENCY(UniformityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(AMDGPURewriteUndefForPHILegacy, DEBUG_TYPE,
                    "Rewrite undef for PHI", false, false)

bool rewritePHIs(Function &F, UniformityInfo &UA, DominatorTree *DT) {
  bool Changed = false;
  SmallVector<PHINode *> ToBeDeleted;
  for (auto &BB : F) {
    for (auto &PHI : BB.phis()) {
      if (UA.isDivergent(&PHI))
        continue;

      // The unique incoming value except undef/poison for the PHI node.
      Value *UniqueDefinedIncoming = nullptr;
      // The divergent block with defined incoming value that dominates all
      // other block with the same incoming value.
      BasicBlock *DominateBB = nullptr;
      // Predecessors with undefined incoming value (excluding loop backedge).
      SmallVector<BasicBlock *> Undefs;

      for (unsigned i = 0; i < PHI.getNumIncomingValues(); i++) {
        Value *Incoming = PHI.getIncomingValue(i);
        BasicBlock *IncomingBB = PHI.getIncomingBlock(i);

        if (Incoming == &PHI)
          continue;

        if (isa<UndefValue>(Incoming)) {
          // Undef from loop backedge will not be replaced.
          if (!DT->dominates(&BB, IncomingBB))
            Undefs.push_back(IncomingBB);
          continue;
        }

        if (!UniqueDefinedIncoming) {
          UniqueDefinedIncoming = Incoming;
          DominateBB = IncomingBB;
        } else if (Incoming == UniqueDefinedIncoming) {
          // Update DominateBB if necessary.
          if (DT->dominates(IncomingBB, DominateBB))
            DominateBB = IncomingBB;
        } else {
          UniqueDefinedIncoming = nullptr;
          break;
        }
      }
      // We only need to replace the undef for the PHI which is merging
      // defined/undefined values from divergent threads.
      // TODO: We should still be able to replace undef value if the unique
      // value is a Constant.
      if (!UniqueDefinedIncoming || Undefs.empty() ||
          !UA.isDivergent(DominateBB->getTerminator()))
        continue;

      // We only replace the undef when DominateBB truly dominates all the
      // other predecessors with undefined incoming value. Make sure DominateBB
      // dominates BB so that UniqueDefinedIncoming is available in BB and
      // afterwards.
      if (DT->dominates(DominateBB, &BB) && all_of(Undefs, [&](BasicBlock *UD) {
            return DT->dominates(DominateBB, UD);
          })) {
        PHI.replaceAllUsesWith(UniqueDefinedIncoming);
        ToBeDeleted.push_back(&PHI);
        Changed = true;
      }
    }
  }

  for (auto *PHI : ToBeDeleted)
    PHI->eraseFromParent();

  return Changed;
}

bool AMDGPURewriteUndefForPHILegacy::runOnFunction(Function &F) {
  UniformityInfo &UA =
      getAnalysis<UniformityInfoWrapperPass>().getUniformityInfo();
  DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  return rewritePHIs(F, UA, DT);
}

PreservedAnalyses
AMDGPURewriteUndefForPHIPass::run(Function &F, FunctionAnalysisManager &AM) {
  UniformityInfo &UA = AM.getResult<UniformityInfoAnalysis>(F);
  DominatorTree *DT = &AM.getResult<DominatorTreeAnalysis>(F);
  bool Changed = rewritePHIs(F, UA, DT);
  if (Changed) {
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }

  return PreservedAnalyses::all();
}

FunctionPass *llvm::createAMDGPURewriteUndefForPHILegacyPass() {
  return new AMDGPURewriteUndefForPHILegacy();
}
