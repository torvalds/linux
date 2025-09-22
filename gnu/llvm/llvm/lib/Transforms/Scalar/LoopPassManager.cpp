//===- LoopPassManager.cpp - Loop pass management -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/TimeProfiler.h"

using namespace llvm;

namespace llvm {

/// Explicitly specialize the pass manager's run method to handle loop nest
/// structure updates.
PreservedAnalyses
PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
            LPMUpdater &>::run(Loop &L, LoopAnalysisManager &AM,
                               LoopStandardAnalysisResults &AR, LPMUpdater &U) {
  // Runs loop-nest passes only when the current loop is a top-level one.
  PreservedAnalyses PA = (L.isOutermost() && !LoopNestPasses.empty())
                             ? runWithLoopNestPasses(L, AM, AR, U)
                             : runWithoutLoopNestPasses(L, AM, AR, U);

  // Invalidation for the current loop should be handled above, and other loop
  // analysis results shouldn't be impacted by runs over this loop. Therefore,
  // the remaining analysis results in the AnalysisManager are preserved. We
  // mark this with a set so that we don't need to inspect each one
  // individually.
  // FIXME: This isn't correct! This loop and all nested loops' analyses should
  // be preserved, but unrolling should invalidate the parent loop's analyses.
  PA.preserveSet<AllAnalysesOn<Loop>>();

  return PA;
}

void PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                 LPMUpdater &>::printPipeline(raw_ostream &OS,
                                              function_ref<StringRef(StringRef)>
                                                  MapClassName2PassName) {
  assert(LoopPasses.size() + LoopNestPasses.size() == IsLoopNestPass.size());

  unsigned IdxLP = 0, IdxLNP = 0;
  for (unsigned Idx = 0, Size = IsLoopNestPass.size(); Idx != Size; ++Idx) {
    if (IsLoopNestPass[Idx]) {
      auto *P = LoopNestPasses[IdxLNP++].get();
      P->printPipeline(OS, MapClassName2PassName);
    } else {
      auto *P = LoopPasses[IdxLP++].get();
      P->printPipeline(OS, MapClassName2PassName);
    }
    if (Idx + 1 < Size)
      OS << ',';
  }
}

// Run both loop passes and loop-nest passes on top-level loop \p L.
PreservedAnalyses
LoopPassManager::runWithLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                                       LoopStandardAnalysisResults &AR,
                                       LPMUpdater &U) {
  assert(L.isOutermost() &&
         "Loop-nest passes should only run on top-level loops.");
  PreservedAnalyses PA = PreservedAnalyses::all();

  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(L, AR);

  unsigned LoopPassIndex = 0, LoopNestPassIndex = 0;

  // `LoopNestPtr` points to the `LoopNest` object for the current top-level
  // loop and `IsLoopNestPtrValid` indicates whether the pointer is still valid.
  // The `LoopNest` object will have to be re-constructed if the pointer is
  // invalid when encountering a loop-nest pass.
  std::unique_ptr<LoopNest> LoopNestPtr;
  bool IsLoopNestPtrValid = false;
  Loop *OuterMostLoop = &L;

  for (size_t I = 0, E = IsLoopNestPass.size(); I != E; ++I) {
    std::optional<PreservedAnalyses> PassPA;
    if (!IsLoopNestPass[I]) {
      // The `I`-th pass is a loop pass.
      auto &Pass = LoopPasses[LoopPassIndex++];
      PassPA = runSinglePass(L, Pass, AM, AR, U, PI);
    } else {
      // The `I`-th pass is a loop-nest pass.
      auto &Pass = LoopNestPasses[LoopNestPassIndex++];

      // If the loop-nest object calculated before is no longer valid,
      // re-calculate it here before running the loop-nest pass.
      //
      // FIXME: PreservedAnalysis should not be abused to tell if the
      // status of loopnest has been changed. We should use and only
      // use LPMUpdater for this purpose.
      if (!IsLoopNestPtrValid || U.isLoopNestChanged()) {
        while (auto *ParentLoop = OuterMostLoop->getParentLoop())
          OuterMostLoop = ParentLoop;
        LoopNestPtr = LoopNest::getLoopNest(*OuterMostLoop, AR.SE);
        IsLoopNestPtrValid = true;
        U.markLoopNestChanged(false);
      }

      PassPA = runSinglePass(*LoopNestPtr, Pass, AM, AR, U, PI);
    }

    // `PassPA` is `None` means that the before-pass callbacks in
    // `PassInstrumentation` return false. The pass does not run in this case,
    // so we can skip the following procedure.
    if (!PassPA)
      continue;

    // If the loop was deleted, abort the run and return to the outer walk.
    if (U.skipCurrentLoop()) {
      PA.intersect(std::move(*PassPA));
      break;
    }

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(IsLoopNestPass[I] ? *OuterMostLoop : L, *PassPA);

    // Finally, we intersect the final preserved analyses to compute the
    // aggregate preserved set for this pass manager.
    PA.intersect(std::move(*PassPA));

    // Check if the current pass preserved the loop-nest object or not.
    IsLoopNestPtrValid &= PassPA->getChecker<LoopNestAnalysis>().preserved();

    // After running the loop pass, the parent loop might change and we need to
    // notify the updater, otherwise U.ParentL might gets outdated and triggers
    // assertion failures in addSiblingLoops and addChildLoops.
    U.setParentLoop((IsLoopNestPass[I] ? *OuterMostLoop : L).getParentLoop());
  }
  return PA;
}

// Run all loop passes on loop \p L. Loop-nest passes don't run either because
// \p L is not a top-level one or simply because there are no loop-nest passes
// in the pass manager at all.
PreservedAnalyses
LoopPassManager::runWithoutLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &U) {
  PreservedAnalyses PA = PreservedAnalyses::all();

  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(L, AR);
  for (auto &Pass : LoopPasses) {
    std::optional<PreservedAnalyses> PassPA =
        runSinglePass(L, Pass, AM, AR, U, PI);

    // `PassPA` is `None` means that the before-pass callbacks in
    // `PassInstrumentation` return false. The pass does not run in this case,
    // so we can skip the following procedure.
    if (!PassPA)
      continue;

    // If the loop was deleted, abort the run and return to the outer walk.
    if (U.skipCurrentLoop()) {
      PA.intersect(std::move(*PassPA));
      break;
    }

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(L, *PassPA);

    // Finally, we intersect the final preserved analyses to compute the
    // aggregate preserved set for this pass manager.
    PA.intersect(std::move(*PassPA));

    // After running the loop pass, the parent loop might change and we need to
    // notify the updater, otherwise U.ParentL might gets outdated and triggers
    // assertion failures in addSiblingLoops and addChildLoops.
    U.setParentLoop(L.getParentLoop());
  }
  return PA;
}
} // namespace llvm

void FunctionToLoopPassAdaptor::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  OS << (UseMemorySSA ? "loop-mssa(" : "loop(");
  Pass->printPipeline(OS, MapClassName2PassName);
  OS << ')';
}
PreservedAnalyses FunctionToLoopPassAdaptor::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  // Before we even compute any loop analyses, first run a miniature function
  // pass pipeline to put loops into their canonical form. Note that we can
  // directly build up function analyses after this as the function pass
  // manager handles all the invalidation at that layer.
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(F);

  PreservedAnalyses PA = PreservedAnalyses::all();
  // Check the PassInstrumentation's BeforePass callbacks before running the
  // canonicalization pipeline.
  if (PI.runBeforePass<Function>(LoopCanonicalizationFPM, F)) {
    PA = LoopCanonicalizationFPM.run(F, AM);
    PI.runAfterPass<Function>(LoopCanonicalizationFPM, F, PA);
  }

  // Get the loop structure for this function
  LoopInfo &LI = AM.getResult<LoopAnalysis>(F);

  // If there are no loops, there is nothing to do here.
  if (LI.empty())
    return PA;

  // Get the analysis results needed by loop passes.
  MemorySSA *MSSA =
      UseMemorySSA ? (&AM.getResult<MemorySSAAnalysis>(F).getMSSA()) : nullptr;
  BlockFrequencyInfo *BFI = UseBlockFrequencyInfo && F.hasProfileData()
                                ? (&AM.getResult<BlockFrequencyAnalysis>(F))
                                : nullptr;
  BranchProbabilityInfo *BPI =
      UseBranchProbabilityInfo && F.hasProfileData()
          ? (&AM.getResult<BranchProbabilityAnalysis>(F))
          : nullptr;
  LoopStandardAnalysisResults LAR = {AM.getResult<AAManager>(F),
                                     AM.getResult<AssumptionAnalysis>(F),
                                     AM.getResult<DominatorTreeAnalysis>(F),
                                     AM.getResult<LoopAnalysis>(F),
                                     AM.getResult<ScalarEvolutionAnalysis>(F),
                                     AM.getResult<TargetLibraryAnalysis>(F),
                                     AM.getResult<TargetIRAnalysis>(F),
                                     BFI,
                                     BPI,
                                     MSSA};

  // Setup the loop analysis manager from its proxy. It is important that
  // this is only done when there are loops to process and we have built the
  // LoopStandardAnalysisResults object. The loop analyses cached in this
  // manager have access to those analysis results and so it must invalidate
  // itself when they go away.
  auto &LAMFP = AM.getResult<LoopAnalysisManagerFunctionProxy>(F);
  if (UseMemorySSA)
    LAMFP.markMSSAUsed();
  LoopAnalysisManager &LAM = LAMFP.getManager();

  // A postorder worklist of loops to process.
  SmallPriorityWorklist<Loop *, 4> Worklist;

  // Register the worklist and loop analysis manager so that loop passes can
  // update them when they mutate the loop nest structure.
  LPMUpdater Updater(Worklist, LAM, LoopNestMode);

  // Add the loop nests in the reverse order of LoopInfo. See method
  // declaration.
  if (!LoopNestMode) {
    appendLoopsToWorklist(LI, Worklist);
  } else {
    for (Loop *L : LI)
      Worklist.insert(L);
  }

#ifndef NDEBUG
  PI.pushBeforeNonSkippedPassCallback([&LAR, &LI](StringRef PassID, Any IR) {
    if (isSpecialPass(PassID, {"PassManager"}))
      return;
    assert(llvm::any_cast<const Loop *>(&IR) ||
           llvm::any_cast<const LoopNest *>(&IR));
    const Loop **LPtr = llvm::any_cast<const Loop *>(&IR);
    const Loop *L = LPtr ? *LPtr : nullptr;
    if (!L)
      L = &llvm::any_cast<const LoopNest *>(IR)->getOutermostLoop();
    assert(L && "Loop should be valid for printing");

    // Verify the loop structure and LCSSA form before visiting the loop.
    L->verifyLoop();
    assert(L->isRecursivelyLCSSAForm(LAR.DT, LI) &&
           "Loops must remain in LCSSA form!");
  });
#endif

  do {
    Loop *L = Worklist.pop_back_val();
    assert(!(LoopNestMode && L->getParentLoop()) &&
           "L should be a top-level loop in loop-nest mode.");

    // Reset the update structure for this loop.
    Updater.CurrentL = L;
    Updater.SkipCurrentLoop = false;

#ifndef NDEBUG
    // Save a parent loop pointer for asserts.
    Updater.ParentL = L->getParentLoop();
#endif
    // Check the PassInstrumentation's BeforePass callbacks before running the
    // pass, skip its execution completely if asked to (callback returns
    // false).
    if (!PI.runBeforePass<Loop>(*Pass, *L))
      continue;

    PreservedAnalyses PassPA = Pass->run(*L, LAM, LAR, Updater);

    // Do not pass deleted Loop into the instrumentation.
    if (Updater.skipCurrentLoop())
      PI.runAfterPassInvalidated<Loop>(*Pass, PassPA);
    else
      PI.runAfterPass<Loop>(*Pass, *L, PassPA);

    if (LAR.MSSA && !PassPA.getChecker<MemorySSAAnalysis>().preserved())
      report_fatal_error("Loop pass manager using MemorySSA contains a pass "
                         "that does not preserve MemorySSA",
                         /*gen_crash_diag*/ false);

#ifndef NDEBUG
    // LoopAnalysisResults should always be valid.
    if (VerifyDomInfo)
      LAR.DT.verify();
    if (VerifyLoopInfo)
      LAR.LI.verify(LAR.DT);
    if (VerifySCEV)
      LAR.SE.verify();
    if (LAR.MSSA && VerifyMemorySSA)
      LAR.MSSA->verifyMemorySSA();
#endif

    // If the loop hasn't been deleted, we need to handle invalidation here.
    if (!Updater.skipCurrentLoop())
      // We know that the loop pass couldn't have invalidated any other
      // loop's analyses (that's the contract of a loop pass), so directly
      // handle the loop analysis manager's invalidation here.
      LAM.invalidate(*L, PassPA);

    // Then intersect the preserved set so that invalidation of module
    // analyses will eventually occur when the module pass completes.
    PA.intersect(std::move(PassPA));
  } while (!Worklist.empty());

#ifndef NDEBUG
  PI.popBeforeNonSkippedPassCallback();
#endif

  // By definition we preserve the proxy. We also preserve all analyses on
  // Loops. This precludes *any* invalidation of loop analyses by the proxy,
  // but that's OK because we've taken care to invalidate analyses in the
  // loop analysis manager incrementally above.
  PA.preserveSet<AllAnalysesOn<Loop>>();
  PA.preserve<LoopAnalysisManagerFunctionProxy>();
  // We also preserve the set of standard analyses.
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  PA.preserve<ScalarEvolutionAnalysis>();
  if (UseBlockFrequencyInfo && F.hasProfileData())
    PA.preserve<BlockFrequencyAnalysis>();
  if (UseBranchProbabilityInfo && F.hasProfileData())
    PA.preserve<BranchProbabilityAnalysis>();
  if (UseMemorySSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}

PrintLoopPass::PrintLoopPass() : OS(dbgs()) {}
PrintLoopPass::PrintLoopPass(raw_ostream &OS, const std::string &Banner)
    : OS(OS), Banner(Banner) {}

PreservedAnalyses PrintLoopPass::run(Loop &L, LoopAnalysisManager &,
                                     LoopStandardAnalysisResults &,
                                     LPMUpdater &) {
  printLoop(L, OS, Banner);
  return PreservedAnalyses::all();
}
