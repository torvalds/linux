//===- LoopAnalysisManager.cpp - Loop analysis management -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManagerImpl.h"
#include <optional>

using namespace llvm;

namespace llvm {
// Explicit template instantiations and specialization definitions for core
// template typedefs.
template class AllAnalysesOn<Loop>;
template class AnalysisManager<Loop, LoopStandardAnalysisResults &>;
template class InnerAnalysisManagerProxy<LoopAnalysisManager, Function>;
template class OuterAnalysisManagerProxy<FunctionAnalysisManager, Loop,
                                         LoopStandardAnalysisResults &>;

bool LoopAnalysisManagerFunctionProxy::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  // First compute the sequence of IR units covered by this proxy. We will want
  // to visit this in postorder, but because this is a tree structure we can do
  // this by building a preorder sequence and walking it backwards. We also
  // want siblings in forward program order to match the LoopPassManager so we
  // get the preorder with siblings reversed.
  SmallVector<Loop *, 4> PreOrderLoops = LI->getLoopsInReverseSiblingPreorder();

  // If this proxy or the loop info is going to be invalidated, we also need
  // to clear all the keys coming from that analysis. We also completely blow
  // away the loop analyses if any of the standard analyses provided by the
  // loop pass manager go away so that loop analyses can freely use these
  // without worrying about declaring dependencies on them etc.
  // FIXME: It isn't clear if this is the right tradeoff. We could instead make
  // loop analyses declare any dependencies on these and use the more general
  // invalidation logic below to act on that.
  auto PAC = PA.getChecker<LoopAnalysisManagerFunctionProxy>();
  bool invalidateMemorySSAAnalysis = false;
  if (MSSAUsed)
    invalidateMemorySSAAnalysis = Inv.invalidate<MemorySSAAnalysis>(F, PA);
  if (!(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>()) ||
      Inv.invalidate<AAManager>(F, PA) ||
      Inv.invalidate<AssumptionAnalysis>(F, PA) ||
      Inv.invalidate<DominatorTreeAnalysis>(F, PA) ||
      Inv.invalidate<LoopAnalysis>(F, PA) ||
      Inv.invalidate<ScalarEvolutionAnalysis>(F, PA) ||
      invalidateMemorySSAAnalysis) {
    // Note that the LoopInfo may be stale at this point, however the loop
    // objects themselves remain the only viable keys that could be in the
    // analysis manager's cache. So we just walk the keys and forcibly clear
    // those results. Note that the order doesn't matter here as this will just
    // directly destroy the results without calling methods on them.
    for (Loop *L : PreOrderLoops) {
      // NB! `L` may not be in a good enough state to run Loop::getName.
      InnerAM->clear(*L, "<possibly invalidated loop>");
    }

    // We also need to null out the inner AM so that when the object gets
    // destroyed as invalid we don't try to clear the inner AM again. At that
    // point we won't be able to reliably walk the loops for this function and
    // only clear results associated with those loops the way we do here.
    // FIXME: Making InnerAM null at this point isn't very nice. Most analyses
    // try to remain valid during invalidation. Maybe we should add an
    // `IsClean` flag?
    InnerAM = nullptr;

    // Now return true to indicate this *is* invalid and a fresh proxy result
    // needs to be built. This is especially important given the null InnerAM.
    return true;
  }

  // Directly check if the relevant set is preserved so we can short circuit
  // invalidating loops.
  bool AreLoopAnalysesPreserved =
      PA.allAnalysesInSetPreserved<AllAnalysesOn<Loop>>();

  // Since we have a valid LoopInfo we can actually leave the cached results in
  // the analysis manager associated with the Loop keys, but we need to
  // propagate any necessary invalidation logic into them. We'd like to
  // invalidate things in roughly the same order as they were put into the
  // cache and so we walk the preorder list in reverse to form a valid
  // postorder.
  for (Loop *L : reverse(PreOrderLoops)) {
    std::optional<PreservedAnalyses> InnerPA;

    // Check to see whether the preserved set needs to be adjusted based on
    // function-level analysis invalidation triggering deferred invalidation
    // for this loop.
    if (auto *OuterProxy =
            InnerAM->getCachedResult<FunctionAnalysisManagerLoopProxy>(*L))
      for (const auto &OuterInvalidationPair :
           OuterProxy->getOuterInvalidations()) {
        AnalysisKey *OuterAnalysisID = OuterInvalidationPair.first;
        const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
        if (Inv.invalidate(OuterAnalysisID, F, PA)) {
          if (!InnerPA)
            InnerPA = PA;
          for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
            InnerPA->abandon(InnerAnalysisID);
        }
      }

    // Check if we needed a custom PA set. If so we'll need to run the inner
    // invalidation.
    if (InnerPA) {
      InnerAM->invalidate(*L, *InnerPA);
      continue;
    }

    // Otherwise we only need to do invalidation if the original PA set didn't
    // preserve all Loop analyses.
    if (!AreLoopAnalysesPreserved)
      InnerAM->invalidate(*L, PA);
  }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

template <>
LoopAnalysisManagerFunctionProxy::Result
LoopAnalysisManagerFunctionProxy::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  return Result(*InnerAM, AM.getResult<LoopAnalysis>(F));
}
} // namespace llvm

PreservedAnalyses llvm::getLoopPassPreservedAnalyses() {
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  PA.preserve<LoopAnalysisManagerFunctionProxy>();
  PA.preserve<ScalarEvolutionAnalysis>();
  return PA;
}
