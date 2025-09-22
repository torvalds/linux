//===--- PartiallyInlineLibCalls.cpp - Partially inline libcalls ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass tries to partially inline the fast path of well-known library
// functions, such as using square-root instructions for cases where sqrt()
// does not need to set errno.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/PartiallyInlineLibCalls.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "partially-inline-libcalls"

DEBUG_COUNTER(PILCounter, "partially-inline-libcalls-transform",
              "Controls transformations in partially-inline-libcalls");

static bool optimizeSQRT(CallInst *Call, Function *CalledFunc,
                         BasicBlock &CurrBB, Function::iterator &BB,
                         const TargetTransformInfo *TTI, DomTreeUpdater *DTU) {
  // There is no need to change the IR, since backend will emit sqrt
  // instruction if the call has already been marked read-only.
  if (Call->onlyReadsMemory())
    return false;

  if (!DebugCounter::shouldExecute(PILCounter))
    return false;

  // Do the following transformation:
  //
  // (before)
  // dst = sqrt(src)
  //
  // (after)
  // v0 = sqrt_noreadmem(src) # native sqrt instruction.
  // [if (v0 is a NaN) || if (src < 0)]
  //   v1 = sqrt(src)         # library call.
  // dst = phi(v0, v1)
  //

  Type *Ty = Call->getType();
  IRBuilder<> Builder(Call->getNextNode());

  // Split CurrBB right after the call, create a 'then' block (that branches
  // back to split-off tail of CurrBB) into which we'll insert a libcall.
  Instruction *LibCallTerm = SplitBlockAndInsertIfThen(
      Builder.getTrue(), Call->getNextNode(), /*Unreachable=*/false,
      /*BranchWeights*/ nullptr, DTU);

  auto *CurrBBTerm = cast<BranchInst>(CurrBB.getTerminator());
  // We want an 'else' block though, not a 'then' block.
  cast<BranchInst>(CurrBBTerm)->swapSuccessors();

  // Create phi that will merge results of either sqrt and replace all uses.
  BasicBlock *JoinBB = LibCallTerm->getSuccessor(0);
  JoinBB->setName(CurrBB.getName() + ".split");
  Builder.SetInsertPoint(JoinBB, JoinBB->begin());
  PHINode *Phi = Builder.CreatePHI(Ty, 2);
  Call->replaceAllUsesWith(Phi);

  // Finally, insert the libcall into 'else' block.
  BasicBlock *LibCallBB = LibCallTerm->getParent();
  LibCallBB->setName("call.sqrt");
  Builder.SetInsertPoint(LibCallTerm);
  Instruction *LibCall = Call->clone();
  Builder.Insert(LibCall);

  // Add memory(none) attribute, so that the backend can use a native sqrt
  // instruction for this call.
  Call->setDoesNotAccessMemory();

  // Insert a FP compare instruction and use it as the CurrBB branch condition.
  Builder.SetInsertPoint(CurrBBTerm);
  Value *FCmp = TTI->isFCmpOrdCheaperThanFCmpZero(Ty)
                    ? Builder.CreateFCmpORD(Call, Call)
                    : Builder.CreateFCmpOGE(Call->getOperand(0),
                                            ConstantFP::get(Ty, 0.0));
  CurrBBTerm->setCondition(FCmp);

  // Add phi operands.
  Phi->addIncoming(Call, &CurrBB);
  Phi->addIncoming(LibCall, LibCallBB);

  BB = JoinBB->getIterator();
  return true;
}

static bool runPartiallyInlineLibCalls(Function &F, TargetLibraryInfo *TLI,
                                       const TargetTransformInfo *TTI,
                                       DominatorTree *DT) {
  std::optional<DomTreeUpdater> DTU;
  if (DT)
    DTU.emplace(DT, DomTreeUpdater::UpdateStrategy::Lazy);

  bool Changed = false;

  Function::iterator CurrBB;
  for (Function::iterator BB = F.begin(), BE = F.end(); BB != BE;) {
    CurrBB = BB++;

    for (BasicBlock::iterator II = CurrBB->begin(), IE = CurrBB->end();
         II != IE; ++II) {
      CallInst *Call = dyn_cast<CallInst>(&*II);
      Function *CalledFunc;

      if (!Call || !(CalledFunc = Call->getCalledFunction()))
        continue;

      if (Call->isNoBuiltin() || Call->isStrictFP())
        continue;

      if (Call->isMustTailCall())
        continue;

      // Skip if function either has local linkage or is not a known library
      // function.
      LibFunc LF;
      if (CalledFunc->hasLocalLinkage() ||
          !TLI->getLibFunc(*CalledFunc, LF) || !TLI->has(LF))
        continue;

      switch (LF) {
      case LibFunc_sqrtf:
      case LibFunc_sqrt:
        if (TTI->haveFastSqrt(Call->getType()) &&
            optimizeSQRT(Call, CalledFunc, *CurrBB, BB, TTI,
                         DTU ? &*DTU : nullptr))
          break;
        continue;
      default:
        continue;
      }

      Changed = true;
      break;
    }
  }

  return Changed;
}

PreservedAnalyses
PartiallyInlineLibCallsPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto *DT = AM.getCachedResult<DominatorTreeAnalysis>(F);
  if (!runPartiallyInlineLibCalls(F, &TLI, &TTI, DT))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

namespace {
class PartiallyInlineLibCallsLegacyPass : public FunctionPass {
public:
  static char ID;

  PartiallyInlineLibCallsLegacyPass() : FunctionPass(ID) {
    initializePartiallyInlineLibCallsLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    TargetLibraryInfo *TLI =
        &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
    const TargetTransformInfo *TTI =
        &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    DominatorTree *DT = nullptr;
    if (auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>())
      DT = &DTWP->getDomTree();
    return runPartiallyInlineLibCalls(F, TLI, TTI, DT);
  }
};
}

char PartiallyInlineLibCallsLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(PartiallyInlineLibCallsLegacyPass,
                      "partially-inline-libcalls",
                      "Partially inline calls to library functions", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(PartiallyInlineLibCallsLegacyPass,
                    "partially-inline-libcalls",
                    "Partially inline calls to library functions", false, false)

FunctionPass *llvm::createPartiallyInlineLibCallsPass() {
  return new PartiallyInlineLibCallsLegacyPass();
}
