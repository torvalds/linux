//===- ModuleInliner.cpp - Code related to module inliner -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the mechanics required to implement inlining without
// missing any calls in the module level. It doesn't need any infromation about
// SCC or call graph, which is different from the SCC inliner.  The decisions of
// which calls are profitable to inline are implemented elsewhere.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ModuleInliner.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/InlineOrder.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ReplayInlineAdvisor.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/CallPromotionUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "module-inline"

STATISTIC(NumInlined, "Number of functions inlined");
STATISTIC(NumDeleted, "Number of functions deleted because all callers found");

/// Return true if the specified inline history ID
/// indicates an inline history that includes the specified function.
static bool inlineHistoryIncludes(
    Function *F, int InlineHistoryID,
    const SmallVectorImpl<std::pair<Function *, int>> &InlineHistory) {
  while (InlineHistoryID != -1) {
    assert(unsigned(InlineHistoryID) < InlineHistory.size() &&
           "Invalid inline history ID");
    if (InlineHistory[InlineHistoryID].first == F)
      return true;
    InlineHistoryID = InlineHistory[InlineHistoryID].second;
  }
  return false;
}

InlineAdvisor &ModuleInlinerPass::getAdvisor(const ModuleAnalysisManager &MAM,
                                             FunctionAnalysisManager &FAM,
                                             Module &M) {
  if (OwnedAdvisor)
    return *OwnedAdvisor;

  auto *IAA = MAM.getCachedResult<InlineAdvisorAnalysis>(M);
  if (!IAA) {
    // It should still be possible to run the inliner as a stand-alone module
    // pass, for test scenarios. In that case, we default to the
    // DefaultInlineAdvisor, which doesn't need to keep state between module
    // pass runs. It also uses just the default InlineParams. In this case, we
    // need to use the provided FAM, which is valid for the duration of the
    // inliner pass, and thus the lifetime of the owned advisor. The one we
    // would get from the MAM can be invalidated as a result of the inliner's
    // activity.
    OwnedAdvisor = std::make_unique<DefaultInlineAdvisor>(
        M, FAM, Params, InlineContext{LTOPhase, InlinePass::ModuleInliner});

    return *OwnedAdvisor;
  }
  assert(IAA->getAdvisor() &&
         "Expected a present InlineAdvisorAnalysis also have an "
         "InlineAdvisor initialized");
  return *IAA->getAdvisor();
}

static bool isKnownLibFunction(Function &F, TargetLibraryInfo &TLI) {
  LibFunc LF;

  // Either this is a normal library function or a "vectorizable"
  // function.  Not using the VFDatabase here because this query
  // is related only to libraries handled via the TLI.
  return TLI.getLibFunc(F, LF) ||
         TLI.isKnownVectorFunctionInLibrary(F.getName());
}

PreservedAnalyses ModuleInlinerPass::run(Module &M,
                                         ModuleAnalysisManager &MAM) {
  LLVM_DEBUG(dbgs() << "---- Module Inliner is Running ---- \n");

  auto &IAA = MAM.getResult<InlineAdvisorAnalysis>(M);
  if (!IAA.tryCreate(Params, Mode, {},
                     InlineContext{LTOPhase, InlinePass::ModuleInliner})) {
    M.getContext().emitError(
        "Could not setup Inlining Advisor for the requested "
        "mode and/or options");
    return PreservedAnalyses::all();
  }

  bool Changed = false;

  ProfileSummaryInfo *PSI = MAM.getCachedResult<ProfileSummaryAnalysis>(M);

  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto GetTLI = [&FAM](Function &F) -> TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };

  InlineAdvisor &Advisor = getAdvisor(MAM, FAM, M);
  Advisor.onPassEntry();

  auto AdvisorOnExit = make_scope_exit([&] { Advisor.onPassExit(); });

  // In the module inliner, a priority-based worklist is used for calls across
  // the entire Module. With this module inliner, the inline order is not
  // limited to bottom-up order. More globally scope inline order is enabled.
  // Also, the inline deferral logic become unnecessary in this module inliner.
  // It is possible to use other priority heuristics, e.g. profile-based
  // heuristic.
  //
  // TODO: Here is a huge amount duplicate code between the module inliner and
  // the SCC inliner, which need some refactoring.
  auto Calls = getInlineOrder(FAM, Params, MAM, M);
  assert(Calls != nullptr && "Expected an initialized InlineOrder");

  // Populate the initial list of calls in this module.
  for (Function &F : M) {
    auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
    for (Instruction &I : instructions(F))
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (Function *Callee = CB->getCalledFunction()) {
          if (!Callee->isDeclaration())
            Calls->push({CB, -1});
          else if (!isa<IntrinsicInst>(I)) {
            using namespace ore;
            setInlineRemark(*CB, "unavailable definition");
            ORE.emit([&]() {
              return OptimizationRemarkMissed(DEBUG_TYPE, "NoDefinition", &I)
                     << NV("Callee", Callee) << " will not be inlined into "
                     << NV("Caller", CB->getCaller())
                     << " because its definition is unavailable"
                     << setIsVerbose();
            });
          }
        }
  }
  if (Calls->empty())
    return PreservedAnalyses::all();

  // When inlining a callee produces new call sites, we want to keep track of
  // the fact that they were inlined from the callee.  This allows us to avoid
  // infinite inlining in some obscure cases.  To represent this, we use an
  // index into the InlineHistory vector.
  SmallVector<std::pair<Function *, int>, 16> InlineHistory;

  // Track the dead functions to delete once finished with inlining calls. We
  // defer deleting these to make it easier to handle the call graph updates.
  SmallVector<Function *, 4> DeadFunctions;

  // Loop forward over all of the calls.
  while (!Calls->empty()) {
    auto P = Calls->pop();
    CallBase *CB = P.first;
    const int InlineHistoryID = P.second;
    Function &F = *CB->getCaller();
    Function &Callee = *CB->getCalledFunction();

    LLVM_DEBUG(dbgs() << "Inlining calls in: " << F.getName() << "\n"
                      << "    Function size: " << F.getInstructionCount()
                      << "\n");
    (void)F;

    auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
      return FAM.getResult<AssumptionAnalysis>(F);
    };

    if (InlineHistoryID != -1 &&
        inlineHistoryIncludes(&Callee, InlineHistoryID, InlineHistory)) {
      setInlineRemark(*CB, "recursive");
      continue;
    }

    auto Advice = Advisor.getAdvice(*CB, /*OnlyMandatory*/ false);
    // Check whether we want to inline this callsite.
    if (!Advice->isInliningRecommended()) {
      Advice->recordUnattemptedInlining();
      continue;
    }

    // Setup the data structure used to plumb customization into the
    // `InlineFunction` routine.
    InlineFunctionInfo IFI(
        GetAssumptionCache, PSI,
        &FAM.getResult<BlockFrequencyAnalysis>(*(CB->getCaller())),
        &FAM.getResult<BlockFrequencyAnalysis>(Callee));

    InlineResult IR =
        InlineFunction(*CB, IFI, /*MergeAttributes=*/true,
                       &FAM.getResult<AAManager>(*CB->getCaller()));
    if (!IR.isSuccess()) {
      Advice->recordUnsuccessfulInlining(IR);
      continue;
    }

    Changed = true;
    ++NumInlined;

    LLVM_DEBUG(dbgs() << "    Size after inlining: " << F.getInstructionCount()
                      << "\n");

    // Add any new callsites to defined functions to the worklist.
    if (!IFI.InlinedCallSites.empty()) {
      int NewHistoryID = InlineHistory.size();
      InlineHistory.push_back({&Callee, InlineHistoryID});

      for (CallBase *ICB : reverse(IFI.InlinedCallSites)) {
        Function *NewCallee = ICB->getCalledFunction();
        if (!NewCallee) {
          // Try to promote an indirect (virtual) call without waiting for
          // the post-inline cleanup and the next DevirtSCCRepeatedPass
          // iteration because the next iteration may not happen and we may
          // miss inlining it.
          if (tryPromoteCall(*ICB))
            NewCallee = ICB->getCalledFunction();
        }
        if (NewCallee)
          if (!NewCallee->isDeclaration())
            Calls->push({ICB, NewHistoryID});
      }
    }

    // For local functions, check whether this makes the callee trivially
    // dead. In that case, we can drop the body of the function eagerly
    // which may reduce the number of callers of other functions to one,
    // changing inline cost thresholds.
    bool CalleeWasDeleted = false;
    if (Callee.hasLocalLinkage()) {
      // To check this we also need to nuke any dead constant uses (perhaps
      // made dead by this operation on other functions).
      Callee.removeDeadConstantUsers();
      // if (Callee.use_empty() && !CG.isLibFunction(Callee)) {
      if (Callee.use_empty() && !isKnownLibFunction(Callee, GetTLI(Callee))) {
        Calls->erase_if([&](const std::pair<CallBase *, int> &Call) {
          return Call.first->getCaller() == &Callee;
        });
        // Clear the body and queue the function itself for deletion when we
        // finish inlining.
        // Note that after this point, it is an error to do anything other
        // than use the callee's address or delete it.
        Callee.dropAllReferences();
        assert(!is_contained(DeadFunctions, &Callee) &&
               "Cannot put cause a function to become dead twice!");
        DeadFunctions.push_back(&Callee);
        CalleeWasDeleted = true;
      }
    }
    if (CalleeWasDeleted)
      Advice->recordInliningWithCalleeDeleted();
    else
      Advice->recordInlining();
  }

  // Now that we've finished inlining all of the calls across this module,
  // delete all of the trivially dead functions.
  //
  // Note that this walks a pointer set which has non-deterministic order but
  // that is OK as all we do is delete things and add pointers to unordered
  // sets.
  for (Function *DeadF : DeadFunctions) {
    // Clear out any cached analyses.
    FAM.clear(*DeadF, DeadF->getName());

    // And delete the actual function from the module.
    M.getFunctionList().erase(DeadF);

    ++NumDeleted;
  }

  if (!Changed)
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}
