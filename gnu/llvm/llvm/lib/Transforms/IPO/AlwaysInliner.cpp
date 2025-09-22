//===- AlwaysInliner.cpp - Code to inline always_inline functions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a custom inliner that handles only functions that
// are marked as "always inline".
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

#define DEBUG_TYPE "inline"

namespace {

bool AlwaysInlineImpl(
    Module &M, bool InsertLifetime, ProfileSummaryInfo &PSI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<AAResults &(Function &)> GetAAR,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI) {
  SmallSetVector<CallBase *, 16> Calls;
  bool Changed = false;
  SmallVector<Function *, 16> InlinedComdatFunctions;

  for (Function &F : make_early_inc_range(M)) {
    if (F.isPresplitCoroutine())
      continue;

    if (F.isDeclaration() || !isInlineViable(F).isSuccess())
      continue;

    Calls.clear();

    for (User *U : F.users())
      if (auto *CB = dyn_cast<CallBase>(U))
        if (CB->getCalledFunction() == &F &&
            CB->hasFnAttr(Attribute::AlwaysInline) &&
            !CB->getAttributes().hasFnAttr(Attribute::NoInline))
          Calls.insert(CB);

    for (CallBase *CB : Calls) {
      Function *Caller = CB->getCaller();
      OptimizationRemarkEmitter ORE(Caller);
      DebugLoc DLoc = CB->getDebugLoc();
      BasicBlock *Block = CB->getParent();

      InlineFunctionInfo IFI(GetAssumptionCache, &PSI,
                             GetBFI ? &GetBFI(*Caller) : nullptr,
                             GetBFI ? &GetBFI(F) : nullptr);

      InlineResult Res = InlineFunction(*CB, IFI, /*MergeAttributes=*/true,
                                        &GetAAR(F), InsertLifetime);
      if (!Res.isSuccess()) {
        ORE.emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "NotInlined", DLoc, Block)
                 << "'" << ore::NV("Callee", &F) << "' is not inlined into '"
                 << ore::NV("Caller", Caller)
                 << "': " << ore::NV("Reason", Res.getFailureReason());
        });
        continue;
      }

      emitInlinedIntoBasedOnCost(
          ORE, DLoc, Block, F, *Caller,
          InlineCost::getAlways("always inline attribute"),
          /*ForProfileContext=*/false, DEBUG_TYPE);

      Changed = true;
    }

    F.removeDeadConstantUsers();
    if (F.hasFnAttribute(Attribute::AlwaysInline) && F.isDefTriviallyDead()) {
      // Remember to try and delete this function afterward. This allows to call
      // filterDeadComdatFunctions() only once.
      if (F.hasComdat()) {
        InlinedComdatFunctions.push_back(&F);
      } else {
        M.getFunctionList().erase(F);
        Changed = true;
      }
    }
  }

  if (!InlinedComdatFunctions.empty()) {
    // Now we just have the comdat functions. Filter out the ones whose comdats
    // are not actually dead.
    filterDeadComdatFunctions(InlinedComdatFunctions);
    // The remaining functions are actually dead.
    for (Function *F : InlinedComdatFunctions) {
      M.getFunctionList().erase(F);
      Changed = true;
    }
  }

  return Changed;
}

struct AlwaysInlinerLegacyPass : public ModulePass {
  bool InsertLifetime;

  AlwaysInlinerLegacyPass()
      : AlwaysInlinerLegacyPass(/*InsertLifetime*/ true) {}

  AlwaysInlinerLegacyPass(bool InsertLifetime)
      : ModulePass(ID), InsertLifetime(InsertLifetime) {
    initializeAlwaysInlinerLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  /// Main run interface method.  We override here to avoid calling skipSCC().
  bool runOnModule(Module &M) override {

    auto &PSI = getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
    auto GetAAR = [&](Function &F) -> AAResults & {
      return getAnalysis<AAResultsWrapperPass>(F).getAAResults();
    };
    auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
      return getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    };

    return AlwaysInlineImpl(M, InsertLifetime, PSI, GetAssumptionCache, GetAAR,
                            /*GetBFI*/ nullptr);
  }

  static char ID; // Pass identification, replacement for typeid

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
  }
};

} // namespace

char AlwaysInlinerLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(AlwaysInlinerLegacyPass, "always-inline",
                      "Inliner for always_inline functions", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_END(AlwaysInlinerLegacyPass, "always-inline",
                    "Inliner for always_inline functions", false, false)

Pass *llvm::createAlwaysInlinerLegacyPass(bool InsertLifetime) {
  return new AlwaysInlinerLegacyPass(InsertLifetime);
}

PreservedAnalyses AlwaysInlinerPass::run(Module &M,
                                         ModuleAnalysisManager &MAM) {
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
    return FAM.getResult<AssumptionAnalysis>(F);
  };
  auto GetBFI = [&](Function &F) -> BlockFrequencyInfo & {
    return FAM.getResult<BlockFrequencyAnalysis>(F);
  };
  auto GetAAR = [&](Function &F) -> AAResults & {
    return FAM.getResult<AAManager>(F);
  };
  auto &PSI = MAM.getResult<ProfileSummaryAnalysis>(M);

  bool Changed = AlwaysInlineImpl(M, InsertLifetime, PSI, GetAssumptionCache,
                                  GetAAR, GetBFI);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
