//===- CGSCCPassManager.cpp - Managing & running CGSCC passes -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassManagerImpl.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <optional>

#define DEBUG_TYPE "cgscc"

using namespace llvm;

// Explicit template instantiations and specialization definitions for core
// template typedefs.
namespace llvm {
static cl::opt<bool> AbortOnMaxDevirtIterationsReached(
    "abort-on-max-devirt-iterations-reached",
    cl::desc("Abort when the max iterations for devirtualization CGSCC repeat "
             "pass is reached"));

AnalysisKey ShouldNotRunFunctionPassesAnalysis::Key;

// Explicit instantiations for the core proxy templates.
template class AllAnalysesOn<LazyCallGraph::SCC>;
template class AnalysisManager<LazyCallGraph::SCC, LazyCallGraph &>;
template class PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager,
                           LazyCallGraph &, CGSCCUpdateResult &>;
template class InnerAnalysisManagerProxy<CGSCCAnalysisManager, Module>;
template class OuterAnalysisManagerProxy<ModuleAnalysisManager,
                                         LazyCallGraph::SCC, LazyCallGraph &>;
template class OuterAnalysisManagerProxy<CGSCCAnalysisManager, Function>;

/// Explicitly specialize the pass manager run method to handle call graph
/// updates.
template <>
PreservedAnalyses
PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,
            CGSCCUpdateResult &>::run(LazyCallGraph::SCC &InitialC,
                                      CGSCCAnalysisManager &AM,
                                      LazyCallGraph &G, CGSCCUpdateResult &UR) {
  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  PassInstrumentation PI =
      AM.getResult<PassInstrumentationAnalysis>(InitialC, G);

  PreservedAnalyses PA = PreservedAnalyses::all();

  // The SCC may be refined while we are running passes over it, so set up
  // a pointer that we can update.
  LazyCallGraph::SCC *C = &InitialC;

  // Get Function analysis manager from its proxy.
  FunctionAnalysisManager &FAM =
      AM.getCachedResult<FunctionAnalysisManagerCGSCCProxy>(*C)->getManager();

  for (auto &Pass : Passes) {
    // Check the PassInstrumentation's BeforePass callbacks before running the
    // pass, skip its execution completely if asked to (callback returns false).
    if (!PI.runBeforePass(*Pass, *C))
      continue;

    PreservedAnalyses PassPA = Pass->run(*C, AM, G, UR);

    // Update the SCC if necessary.
    C = UR.UpdatedC ? UR.UpdatedC : C;
    if (UR.UpdatedC) {
      // If C is updated, also create a proxy and update FAM inside the result.
      auto *ResultFAMCP =
          &AM.getResult<FunctionAnalysisManagerCGSCCProxy>(*C, G);
      ResultFAMCP->updateFAM(FAM);
    }

    // Intersect the final preserved analyses to compute the aggregate
    // preserved set for this pass manager.
    PA.intersect(PassPA);

    // If the CGSCC pass wasn't able to provide a valid updated SCC, the
    // current SCC may simply need to be skipped if invalid.
    if (UR.InvalidatedSCCs.count(C)) {
      PI.runAfterPassInvalidated<LazyCallGraph::SCC>(*Pass, PassPA);
      LLVM_DEBUG(dbgs() << "Skipping invalidated root or island SCC!\n");
      break;
    }

    // Check that we didn't miss any update scenario.
    assert(C->begin() != C->end() && "Cannot have an empty SCC!");

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(*C, PassPA);

    PI.runAfterPass<LazyCallGraph::SCC>(*Pass, *C, PassPA);
  }

  // Before we mark all of *this* SCC's analyses as preserved below, intersect
  // this with the cross-SCC preserved analysis set. This is used to allow
  // CGSCC passes to mutate ancestor SCCs and still trigger proper invalidation
  // for them.
  UR.CrossSCCPA.intersect(PA);

  // Invalidation was handled after each pass in the above loop for the current
  // SCC. Therefore, the remaining analysis results in the AnalysisManager are
  // preserved. We mark this with a set so that we don't need to inspect each
  // one individually.
  PA.preserveSet<AllAnalysesOn<LazyCallGraph::SCC>>();

  return PA;
}

PreservedAnalyses
ModuleToPostOrderCGSCCPassAdaptor::run(Module &M, ModuleAnalysisManager &AM) {
  // Setup the CGSCC analysis manager from its proxy.
  CGSCCAnalysisManager &CGAM =
      AM.getResult<CGSCCAnalysisManagerModuleProxy>(M).getManager();

  // Get the call graph for this module.
  LazyCallGraph &CG = AM.getResult<LazyCallGraphAnalysis>(M);

  // Get Function analysis manager from its proxy.
  FunctionAnalysisManager &FAM =
      AM.getCachedResult<FunctionAnalysisManagerModuleProxy>(M)->getManager();

  // We keep worklists to allow us to push more work onto the pass manager as
  // the passes are run.
  SmallPriorityWorklist<LazyCallGraph::RefSCC *, 1> RCWorklist;
  SmallPriorityWorklist<LazyCallGraph::SCC *, 1> CWorklist;

  // Keep sets for invalidated SCCs that should be skipped when
  // iterating off the worklists.
  SmallPtrSet<LazyCallGraph::SCC *, 4> InvalidSCCSet;

  SmallDenseSet<std::pair<LazyCallGraph::Node *, LazyCallGraph::SCC *>, 4>
      InlinedInternalEdges;

  SmallVector<Function *, 4> DeadFunctions;

  CGSCCUpdateResult UR = {CWorklist,
                          InvalidSCCSet,
                          nullptr,
                          PreservedAnalyses::all(),
                          InlinedInternalEdges,
                          DeadFunctions,
                          {}};

  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(M);

  PreservedAnalyses PA = PreservedAnalyses::all();
  CG.buildRefSCCs();
  for (LazyCallGraph::RefSCC &RC :
       llvm::make_early_inc_range(CG.postorder_ref_sccs())) {
    assert(RCWorklist.empty() &&
           "Should always start with an empty RefSCC worklist");
    // The postorder_ref_sccs range we are walking is lazily constructed, so
    // we only push the first one onto the worklist. The worklist allows us
    // to capture *new* RefSCCs created during transformations.
    //
    // We really want to form RefSCCs lazily because that makes them cheaper
    // to update as the program is simplified and allows us to have greater
    // cache locality as forming a RefSCC touches all the parts of all the
    // functions within that RefSCC.
    //
    // We also eagerly increment the iterator to the next position because
    // the CGSCC passes below may delete the current RefSCC.
    RCWorklist.insert(&RC);

    do {
      LazyCallGraph::RefSCC *RC = RCWorklist.pop_back_val();
      assert(CWorklist.empty() &&
             "Should always start with an empty SCC worklist");

      LLVM_DEBUG(dbgs() << "Running an SCC pass across the RefSCC: " << *RC
                        << "\n");

      // The top of the worklist may *also* be the same SCC we just ran over
      // (and invalidated for). Keep track of that last SCC we processed due
      // to SCC update to avoid redundant processing when an SCC is both just
      // updated itself and at the top of the worklist.
      LazyCallGraph::SCC *LastUpdatedC = nullptr;

      // Push the initial SCCs in reverse post-order as we'll pop off the
      // back and so see this in post-order.
      for (LazyCallGraph::SCC &C : llvm::reverse(*RC))
        CWorklist.insert(&C);

      do {
        LazyCallGraph::SCC *C = CWorklist.pop_back_val();
        // Due to call graph mutations, we may have invalid SCCs or SCCs from
        // other RefSCCs in the worklist. The invalid ones are dead and the
        // other RefSCCs should be queued above, so we just need to skip both
        // scenarios here.
        if (InvalidSCCSet.count(C)) {
          LLVM_DEBUG(dbgs() << "Skipping an invalid SCC...\n");
          continue;
        }
        if (LastUpdatedC == C) {
          LLVM_DEBUG(dbgs() << "Skipping redundant run on SCC: " << *C << "\n");
          continue;
        }
        // We used to also check if the current SCC is part of the current
        // RefSCC and bail if it wasn't, since it should be in RCWorklist.
        // However, this can cause compile time explosions in some cases on
        // modules with a huge RefSCC. If a non-trivial amount of SCCs in the
        // huge RefSCC can become their own child RefSCC, we create one child
        // RefSCC, bail on the current RefSCC, visit the child RefSCC, revisit
        // the huge RefSCC, and repeat. By visiting all SCCs in the original
        // RefSCC we create all the child RefSCCs in one pass of the RefSCC,
        // rather one pass of the RefSCC creating one child RefSCC at a time.

        // Ensure we can proxy analysis updates from the CGSCC analysis manager
        // into the Function analysis manager by getting a proxy here.
        // This also needs to update the FunctionAnalysisManager, as this may be
        // the first time we see this SCC.
        CGAM.getResult<FunctionAnalysisManagerCGSCCProxy>(*C, CG).updateFAM(
            FAM);

        // Each time we visit a new SCC pulled off the worklist,
        // a transformation of a child SCC may have also modified this parent
        // and invalidated analyses. So we invalidate using the update record's
        // cross-SCC preserved set. This preserved set is intersected by any
        // CGSCC pass that handles invalidation (primarily pass managers) prior
        // to marking its SCC as preserved. That lets us track everything that
        // might need invalidation across SCCs without excessive invalidations
        // on a single SCC.
        //
        // This essentially allows SCC passes to freely invalidate analyses
        // of any ancestor SCC. If this becomes detrimental to successfully
        // caching analyses, we could force each SCC pass to manually
        // invalidate the analyses for any SCCs other than themselves which
        // are mutated. However, that seems to lose the robustness of the
        // pass-manager driven invalidation scheme.
        CGAM.invalidate(*C, UR.CrossSCCPA);

        do {
          // Check that we didn't miss any update scenario.
          assert(!InvalidSCCSet.count(C) && "Processing an invalid SCC!");
          assert(C->begin() != C->end() && "Cannot have an empty SCC!");

          LastUpdatedC = UR.UpdatedC;
          UR.UpdatedC = nullptr;

          // Check the PassInstrumentation's BeforePass callbacks before
          // running the pass, skip its execution completely if asked to
          // (callback returns false).
          if (!PI.runBeforePass<LazyCallGraph::SCC>(*Pass, *C))
            continue;

          PreservedAnalyses PassPA = Pass->run(*C, CGAM, CG, UR);

          // Update the SCC and RefSCC if necessary.
          C = UR.UpdatedC ? UR.UpdatedC : C;

          if (UR.UpdatedC) {
            // If we're updating the SCC, also update the FAM inside the proxy's
            // result.
            CGAM.getResult<FunctionAnalysisManagerCGSCCProxy>(*C, CG).updateFAM(
                FAM);
          }

          // Intersect with the cross-SCC preserved set to capture any
          // cross-SCC invalidation.
          UR.CrossSCCPA.intersect(PassPA);
          // Intersect the preserved set so that invalidation of module
          // analyses will eventually occur when the module pass completes.
          PA.intersect(PassPA);

          // If the CGSCC pass wasn't able to provide a valid updated SCC,
          // the current SCC may simply need to be skipped if invalid.
          if (UR.InvalidatedSCCs.count(C)) {
            PI.runAfterPassInvalidated<LazyCallGraph::SCC>(*Pass, PassPA);
            LLVM_DEBUG(dbgs() << "Skipping invalidated root or island SCC!\n");
            break;
          }

          // Check that we didn't miss any update scenario.
          assert(C->begin() != C->end() && "Cannot have an empty SCC!");

          // We handle invalidating the CGSCC analysis manager's information
          // for the (potentially updated) SCC here. Note that any other SCCs
          // whose structure has changed should have been invalidated by
          // whatever was updating the call graph. This SCC gets invalidated
          // late as it contains the nodes that were actively being
          // processed.
          CGAM.invalidate(*C, PassPA);

          PI.runAfterPass<LazyCallGraph::SCC>(*Pass, *C, PassPA);

          // The pass may have restructured the call graph and refined the
          // current SCC and/or RefSCC. We need to update our current SCC and
          // RefSCC pointers to follow these. Also, when the current SCC is
          // refined, re-run the SCC pass over the newly refined SCC in order
          // to observe the most precise SCC model available. This inherently
          // cannot cycle excessively as it only happens when we split SCCs
          // apart, at most converging on a DAG of single nodes.
          // FIXME: If we ever start having RefSCC passes, we'll want to
          // iterate there too.
          if (UR.UpdatedC)
            LLVM_DEBUG(dbgs()
                       << "Re-running SCC passes after a refinement of the "
                          "current SCC: "
                       << *UR.UpdatedC << "\n");

          // Note that both `C` and `RC` may at this point refer to deleted,
          // invalid SCC and RefSCCs respectively. But we will short circuit
          // the processing when we check them in the loop above.
        } while (UR.UpdatedC);
      } while (!CWorklist.empty());

      // We only need to keep internal inlined edge information within
      // a RefSCC, clear it to save on space and let the next time we visit
      // any of these functions have a fresh start.
      InlinedInternalEdges.clear();
    } while (!RCWorklist.empty());
  }

  CG.removeDeadFunctions(DeadFunctions);
  for (Function *DeadF : DeadFunctions)
    DeadF->eraseFromParent();

#if defined(EXPENSIVE_CHECKS)
  // Verify that the call graph is still valid.
  CG.verify();
#endif

  // By definition we preserve the call garph, all SCC analyses, and the
  // analysis proxies by handling them above and in any nested pass managers.
  PA.preserveSet<AllAnalysesOn<LazyCallGraph::SCC>>();
  PA.preserve<LazyCallGraphAnalysis>();
  PA.preserve<CGSCCAnalysisManagerModuleProxy>();
  PA.preserve<FunctionAnalysisManagerModuleProxy>();
  return PA;
}

PreservedAnalyses DevirtSCCRepeatedPass::run(LazyCallGraph::SCC &InitialC,
                                             CGSCCAnalysisManager &AM,
                                             LazyCallGraph &CG,
                                             CGSCCUpdateResult &UR) {
  PreservedAnalyses PA = PreservedAnalyses::all();
  PassInstrumentation PI =
      AM.getResult<PassInstrumentationAnalysis>(InitialC, CG);

  // The SCC may be refined while we are running passes over it, so set up
  // a pointer that we can update.
  LazyCallGraph::SCC *C = &InitialC;

  // Struct to track the counts of direct and indirect calls in each function
  // of the SCC.
  struct CallCount {
    int Direct;
    int Indirect;
  };

  // Put value handles on all of the indirect calls and return the number of
  // direct calls for each function in the SCC.
  auto ScanSCC = [](LazyCallGraph::SCC &C,
                    SmallMapVector<Value *, WeakTrackingVH, 16> &CallHandles) {
    assert(CallHandles.empty() && "Must start with a clear set of handles.");

    SmallDenseMap<Function *, CallCount> CallCounts;
    CallCount CountLocal = {0, 0};
    for (LazyCallGraph::Node &N : C) {
      CallCount &Count =
          CallCounts.insert(std::make_pair(&N.getFunction(), CountLocal))
              .first->second;
      for (Instruction &I : instructions(N.getFunction()))
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (CB->getCalledFunction()) {
            ++Count.Direct;
          } else {
            ++Count.Indirect;
            CallHandles.insert({CB, WeakTrackingVH(CB)});
          }
        }
    }

    return CallCounts;
  };

  UR.IndirectVHs.clear();
  // Populate the initial call handles and get the initial call counts.
  auto CallCounts = ScanSCC(*C, UR.IndirectVHs);

  for (int Iteration = 0;; ++Iteration) {
    if (!PI.runBeforePass<LazyCallGraph::SCC>(*Pass, *C))
      continue;

    PreservedAnalyses PassPA = Pass->run(*C, AM, CG, UR);

    PA.intersect(PassPA);

    // If the CGSCC pass wasn't able to provide a valid updated SCC, the
    // current SCC may simply need to be skipped if invalid.
    if (UR.InvalidatedSCCs.count(C)) {
      PI.runAfterPassInvalidated<LazyCallGraph::SCC>(*Pass, PassPA);
      LLVM_DEBUG(dbgs() << "Skipping invalidated root or island SCC!\n");
      break;
    }

    // Update the analysis manager with each run and intersect the total set
    // of preserved analyses so we're ready to iterate.
    AM.invalidate(*C, PassPA);

    PI.runAfterPass<LazyCallGraph::SCC>(*Pass, *C, PassPA);

    // If the SCC structure has changed, bail immediately and let the outer
    // CGSCC layer handle any iteration to reflect the refined structure.
    if (UR.UpdatedC && UR.UpdatedC != C)
      break;

    assert(C->begin() != C->end() && "Cannot have an empty SCC!");

    // Check whether any of the handles were devirtualized.
    bool Devirt = llvm::any_of(UR.IndirectVHs, [](auto &P) -> bool {
      if (P.second) {
        if (CallBase *CB = dyn_cast<CallBase>(P.second)) {
          if (CB->getCalledFunction()) {
            LLVM_DEBUG(dbgs() << "Found devirtualized call: " << *CB << "\n");
            return true;
          }
        }
      }
      return false;
    });

    // Rescan to build up a new set of handles and count how many direct
    // calls remain. If we decide to iterate, this also sets up the input to
    // the next iteration.
    UR.IndirectVHs.clear();
    auto NewCallCounts = ScanSCC(*C, UR.IndirectVHs);

    // If we haven't found an explicit devirtualization already see if we
    // have decreased the number of indirect calls and increased the number
    // of direct calls for any function in the SCC. This can be fooled by all
    // manner of transformations such as DCE and other things, but seems to
    // work well in practice.
    if (!Devirt)
      // Iterate over the keys in NewCallCounts, if Function also exists in
      // CallCounts, make the check below.
      for (auto &Pair : NewCallCounts) {
        auto &CallCountNew = Pair.second;
        auto CountIt = CallCounts.find(Pair.first);
        if (CountIt != CallCounts.end()) {
          const auto &CallCountOld = CountIt->second;
          if (CallCountOld.Indirect > CallCountNew.Indirect &&
              CallCountOld.Direct < CallCountNew.Direct) {
            Devirt = true;
            break;
          }
        }
      }

    if (!Devirt) {
      break;
    }

    // Otherwise, if we've already hit our max, we're done.
    if (Iteration >= MaxIterations) {
      if (AbortOnMaxDevirtIterationsReached)
        report_fatal_error("Max devirtualization iterations reached");
      LLVM_DEBUG(
          dbgs() << "Found another devirtualization after hitting the max "
                    "number of repetitions ("
                 << MaxIterations << ") on SCC: " << *C << "\n");
      break;
    }

    LLVM_DEBUG(
        dbgs() << "Repeating an SCC pass after finding a devirtualization in: "
               << *C << "\n");

    // Move over the new call counts in preparation for iterating.
    CallCounts = std::move(NewCallCounts);
  }

  // Note that we don't add any preserved entries here unlike a more normal
  // "pass manager" because we only handle invalidation *between* iterations,
  // not after the last iteration.
  return PA;
}

PreservedAnalyses CGSCCToFunctionPassAdaptor::run(LazyCallGraph::SCC &C,
                                                  CGSCCAnalysisManager &AM,
                                                  LazyCallGraph &CG,
                                                  CGSCCUpdateResult &UR) {
  // Setup the function analysis manager from its proxy.
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

  SmallVector<LazyCallGraph::Node *, 4> Nodes;
  for (LazyCallGraph::Node &N : C)
    Nodes.push_back(&N);

  // The SCC may get split while we are optimizing functions due to deleting
  // edges. If this happens, the current SCC can shift, so keep track of
  // a pointer we can overwrite.
  LazyCallGraph::SCC *CurrentC = &C;

  LLVM_DEBUG(dbgs() << "Running function passes across an SCC: " << C << "\n");

  PreservedAnalyses PA = PreservedAnalyses::all();
  for (LazyCallGraph::Node *N : Nodes) {
    // Skip nodes from other SCCs. These may have been split out during
    // processing. We'll eventually visit those SCCs and pick up the nodes
    // there.
    if (CG.lookupSCC(*N) != CurrentC)
      continue;

    Function &F = N->getFunction();

    if (NoRerun && FAM.getCachedResult<ShouldNotRunFunctionPassesAnalysis>(F))
      continue;

    PassInstrumentation PI = FAM.getResult<PassInstrumentationAnalysis>(F);
    if (!PI.runBeforePass<Function>(*Pass, F))
      continue;

    PreservedAnalyses PassPA = Pass->run(F, FAM);

    // We know that the function pass couldn't have invalidated any other
    // function's analyses (that's the contract of a function pass), so
    // directly handle the function analysis manager's invalidation here.
    FAM.invalidate(F, EagerlyInvalidate ? PreservedAnalyses::none() : PassPA);

    PI.runAfterPass<Function>(*Pass, F, PassPA);

    // Then intersect the preserved set so that invalidation of module
    // analyses will eventually occur when the module pass completes.
    PA.intersect(std::move(PassPA));

    // If the call graph hasn't been preserved, update it based on this
    // function pass. This may also update the current SCC to point to
    // a smaller, more refined SCC.
    auto PAC = PA.getChecker<LazyCallGraphAnalysis>();
    if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Module>>()) {
      CurrentC = &updateCGAndAnalysisManagerForFunctionPass(CG, *CurrentC, *N,
                                                            AM, UR, FAM);
      assert(CG.lookupSCC(*N) == CurrentC &&
             "Current SCC not updated to the SCC containing the current node!");
    }
  }

  // By definition we preserve the proxy. And we preserve all analyses on
  // Functions. This precludes *any* invalidation of function analyses by the
  // proxy, but that's OK because we've taken care to invalidate analyses in
  // the function analysis manager incrementally above.
  PA.preserveSet<AllAnalysesOn<Function>>();
  PA.preserve<FunctionAnalysisManagerCGSCCProxy>();

  // We've also ensured that we updated the call graph along the way.
  PA.preserve<LazyCallGraphAnalysis>();

  return PA;
}

bool CGSCCAnalysisManagerModuleProxy::Result::invalidate(
    Module &M, const PreservedAnalyses &PA,
    ModuleAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  // If this proxy or the call graph is going to be invalidated, we also need
  // to clear all the keys coming from that analysis.
  //
  // We also directly invalidate the FAM's module proxy if necessary, and if
  // that proxy isn't preserved we can't preserve this proxy either. We rely on
  // it to handle module -> function analysis invalidation in the face of
  // structural changes and so if it's unavailable we conservatively clear the
  // entire SCC layer as well rather than trying to do invalidation ourselves.
  auto PAC = PA.getChecker<CGSCCAnalysisManagerModuleProxy>();
  if (!(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Module>>()) ||
      Inv.invalidate<LazyCallGraphAnalysis>(M, PA) ||
      Inv.invalidate<FunctionAnalysisManagerModuleProxy>(M, PA)) {
    InnerAM->clear();

    // And the proxy itself should be marked as invalid so that we can observe
    // the new call graph. This isn't strictly necessary because we cheat
    // above, but is still useful.
    return true;
  }

  // Directly check if the relevant set is preserved so we can short circuit
  // invalidating SCCs below.
  bool AreSCCAnalysesPreserved =
      PA.allAnalysesInSetPreserved<AllAnalysesOn<LazyCallGraph::SCC>>();

  // Ok, we have a graph, so we can propagate the invalidation down into it.
  G->buildRefSCCs();
  for (auto &RC : G->postorder_ref_sccs())
    for (auto &C : RC) {
      std::optional<PreservedAnalyses> InnerPA;

      // Check to see whether the preserved set needs to be adjusted based on
      // module-level analysis invalidation triggering deferred invalidation
      // for this SCC.
      if (auto *OuterProxy =
              InnerAM->getCachedResult<ModuleAnalysisManagerCGSCCProxy>(C))
        for (const auto &OuterInvalidationPair :
             OuterProxy->getOuterInvalidations()) {
          AnalysisKey *OuterAnalysisID = OuterInvalidationPair.first;
          const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
          if (Inv.invalidate(OuterAnalysisID, M, PA)) {
            if (!InnerPA)
              InnerPA = PA;
            for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
              InnerPA->abandon(InnerAnalysisID);
          }
        }

      // Check if we needed a custom PA set. If so we'll need to run the inner
      // invalidation.
      if (InnerPA) {
        InnerAM->invalidate(C, *InnerPA);
        continue;
      }

      // Otherwise we only need to do invalidation if the original PA set didn't
      // preserve all SCC analyses.
      if (!AreSCCAnalysesPreserved)
        InnerAM->invalidate(C, PA);
    }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

template <>
CGSCCAnalysisManagerModuleProxy::Result
CGSCCAnalysisManagerModuleProxy::run(Module &M, ModuleAnalysisManager &AM) {
  // Force the Function analysis manager to also be available so that it can
  // be accessed in an SCC analysis and proxied onward to function passes.
  // FIXME: It is pretty awkward to just drop the result here and assert that
  // we can find it again later.
  (void)AM.getResult<FunctionAnalysisManagerModuleProxy>(M);

  return Result(*InnerAM, AM.getResult<LazyCallGraphAnalysis>(M));
}

AnalysisKey FunctionAnalysisManagerCGSCCProxy::Key;

FunctionAnalysisManagerCGSCCProxy::Result
FunctionAnalysisManagerCGSCCProxy::run(LazyCallGraph::SCC &C,
                                       CGSCCAnalysisManager &AM,
                                       LazyCallGraph &CG) {
  // Note: unconditionally getting checking that the proxy exists may get it at
  // this point. There are cases when this is being run unnecessarily, but
  // it is cheap and having the assertion in place is more valuable.
  auto &MAMProxy = AM.getResult<ModuleAnalysisManagerCGSCCProxy>(C, CG);
  Module &M = *C.begin()->getFunction().getParent();
  bool ProxyExists =
      MAMProxy.cachedResultExists<FunctionAnalysisManagerModuleProxy>(M);
  assert(ProxyExists &&
         "The CGSCC pass manager requires that the FAM module proxy is run "
         "on the module prior to entering the CGSCC walk");
  (void)ProxyExists;

  // We just return an empty result. The caller will use the updateFAM interface
  // to correctly register the relevant FunctionAnalysisManager based on the
  // context in which this proxy is run.
  return Result();
}

bool FunctionAnalysisManagerCGSCCProxy::Result::invalidate(
    LazyCallGraph::SCC &C, const PreservedAnalyses &PA,
    CGSCCAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  // All updates to preserve valid results are done below, so we don't need to
  // invalidate this proxy.
  //
  // Note that in order to preserve this proxy, a module pass must ensure that
  // the FAM has been completely updated to handle the deletion of functions.
  // Specifically, any FAM-cached results for those functions need to have been
  // forcibly cleared. When preserved, this proxy will only invalidate results
  // cached on functions *still in the module* at the end of the module pass.
  auto PAC = PA.getChecker<FunctionAnalysisManagerCGSCCProxy>();
  if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<LazyCallGraph::SCC>>()) {
    for (LazyCallGraph::Node &N : C)
      FAM->invalidate(N.getFunction(), PA);

    return false;
  }

  // Directly check if the relevant set is preserved.
  bool AreFunctionAnalysesPreserved =
      PA.allAnalysesInSetPreserved<AllAnalysesOn<Function>>();

  // Now walk all the functions to see if any inner analysis invalidation is
  // necessary.
  for (LazyCallGraph::Node &N : C) {
    Function &F = N.getFunction();
    std::optional<PreservedAnalyses> FunctionPA;

    // Check to see whether the preserved set needs to be pruned based on
    // SCC-level analysis invalidation that triggers deferred invalidation
    // registered with the outer analysis manager proxy for this function.
    if (auto *OuterProxy =
            FAM->getCachedResult<CGSCCAnalysisManagerFunctionProxy>(F))
      for (const auto &OuterInvalidationPair :
           OuterProxy->getOuterInvalidations()) {
        AnalysisKey *OuterAnalysisID = OuterInvalidationPair.first;
        const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
        if (Inv.invalidate(OuterAnalysisID, C, PA)) {
          if (!FunctionPA)
            FunctionPA = PA;
          for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
            FunctionPA->abandon(InnerAnalysisID);
        }
      }

    // Check if we needed a custom PA set, and if so we'll need to run the
    // inner invalidation.
    if (FunctionPA) {
      FAM->invalidate(F, *FunctionPA);
      continue;
    }

    // Otherwise we only need to do invalidation if the original PA set didn't
    // preserve all function analyses.
    if (!AreFunctionAnalysesPreserved)
      FAM->invalidate(F, PA);
  }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

} // end namespace llvm

/// When a new SCC is created for the graph we first update the
/// FunctionAnalysisManager in the Proxy's result.
/// As there might be function analysis results cached for the functions now in
/// that SCC, two forms of  updates are required.
///
/// First, a proxy from the SCC to the FunctionAnalysisManager needs to be
/// created so that any subsequent invalidation events to the SCC are
/// propagated to the function analysis results cached for functions within it.
///
/// Second, if any of the functions within the SCC have analysis results with
/// outer analysis dependencies, then those dependencies would point to the
/// *wrong* SCC's analysis result. We forcibly invalidate the necessary
/// function analyses so that they don't retain stale handles.
static void updateNewSCCFunctionAnalyses(LazyCallGraph::SCC &C,
                                         LazyCallGraph &G,
                                         CGSCCAnalysisManager &AM,
                                         FunctionAnalysisManager &FAM) {
  AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, G).updateFAM(FAM);

  // Now walk the functions in this SCC and invalidate any function analysis
  // results that might have outer dependencies on an SCC analysis.
  for (LazyCallGraph::Node &N : C) {
    Function &F = N.getFunction();

    auto *OuterProxy =
        FAM.getCachedResult<CGSCCAnalysisManagerFunctionProxy>(F);
    if (!OuterProxy)
      // No outer analyses were queried, nothing to do.
      continue;

    // Forcibly abandon all the inner analyses with dependencies, but
    // invalidate nothing else.
    auto PA = PreservedAnalyses::all();
    for (const auto &OuterInvalidationPair :
         OuterProxy->getOuterInvalidations()) {
      const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
      for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
        PA.abandon(InnerAnalysisID);
    }

    // Now invalidate anything we found.
    FAM.invalidate(F, PA);
  }
}

/// Helper function to update both the \c CGSCCAnalysisManager \p AM and the \c
/// CGSCCPassManager's \c CGSCCUpdateResult \p UR based on a range of newly
/// added SCCs.
///
/// The range of new SCCs must be in postorder already. The SCC they were split
/// out of must be provided as \p C. The current node being mutated and
/// triggering updates must be passed as \p N.
///
/// This function returns the SCC containing \p N. This will be either \p C if
/// no new SCCs have been split out, or it will be the new SCC containing \p N.
template <typename SCCRangeT>
static LazyCallGraph::SCC *
incorporateNewSCCRange(const SCCRangeT &NewSCCRange, LazyCallGraph &G,
                       LazyCallGraph::Node &N, LazyCallGraph::SCC *C,
                       CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR) {
  using SCC = LazyCallGraph::SCC;

  if (NewSCCRange.empty())
    return C;

  // Add the current SCC to the worklist as its shape has changed.
  UR.CWorklist.insert(C);
  LLVM_DEBUG(dbgs() << "Enqueuing the existing SCC in the worklist:" << *C
                    << "\n");

  SCC *OldC = C;

  // Update the current SCC. Note that if we have new SCCs, this must actually
  // change the SCC.
  assert(C != &*NewSCCRange.begin() &&
         "Cannot insert new SCCs without changing current SCC!");
  C = &*NewSCCRange.begin();
  assert(G.lookupSCC(N) == C && "Failed to update current SCC!");

  // If we had a cached FAM proxy originally, we will want to create more of
  // them for each SCC that was split off.
  FunctionAnalysisManager *FAM = nullptr;
  if (auto *FAMProxy =
          AM.getCachedResult<FunctionAnalysisManagerCGSCCProxy>(*OldC))
    FAM = &FAMProxy->getManager();

  // We need to propagate an invalidation call to all but the newly current SCC
  // because the outer pass manager won't do that for us after splitting them.
  // FIXME: We should accept a PreservedAnalysis from the CG updater so that if
  // there are preserved analysis we can avoid invalidating them here for
  // split-off SCCs.
  // We know however that this will preserve any FAM proxy so go ahead and mark
  // that.
  auto PA = PreservedAnalyses::allInSet<AllAnalysesOn<Function>>();
  PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
  AM.invalidate(*OldC, PA);

  // Ensure the now-current SCC's function analyses are updated.
  if (FAM)
    updateNewSCCFunctionAnalyses(*C, G, AM, *FAM);

  for (SCC &NewC : llvm::reverse(llvm::drop_begin(NewSCCRange))) {
    assert(C != &NewC && "No need to re-visit the current SCC!");
    assert(OldC != &NewC && "Already handled the original SCC!");
    UR.CWorklist.insert(&NewC);
    LLVM_DEBUG(dbgs() << "Enqueuing a newly formed SCC:" << NewC << "\n");

    // Ensure new SCCs' function analyses are updated.
    if (FAM)
      updateNewSCCFunctionAnalyses(NewC, G, AM, *FAM);

    // Also propagate a normal invalidation to the new SCC as only the current
    // will get one from the pass manager infrastructure.
    AM.invalidate(NewC, PA);
  }
  return C;
}

static LazyCallGraph::SCC &updateCGAndAnalysisManagerForPass(
    LazyCallGraph &G, LazyCallGraph::SCC &InitialC, LazyCallGraph::Node &N,
    CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR,
    FunctionAnalysisManager &FAM, bool FunctionPass) {
  using Node = LazyCallGraph::Node;
  using Edge = LazyCallGraph::Edge;
  using SCC = LazyCallGraph::SCC;
  using RefSCC = LazyCallGraph::RefSCC;

  RefSCC &InitialRC = InitialC.getOuterRefSCC();
  SCC *C = &InitialC;
  RefSCC *RC = &InitialRC;
  Function &F = N.getFunction();

  // Walk the function body and build up the set of retained, promoted, and
  // demoted edges.
  SmallVector<Constant *, 16> Worklist;
  SmallPtrSet<Constant *, 16> Visited;
  SmallPtrSet<Node *, 16> RetainedEdges;
  SmallSetVector<Node *, 4> PromotedRefTargets;
  SmallSetVector<Node *, 4> DemotedCallTargets;
  SmallSetVector<Node *, 4> NewCallEdges;
  SmallSetVector<Node *, 4> NewRefEdges;

  // First walk the function and handle all called functions. We do this first
  // because if there is a single call edge, whether there are ref edges is
  // irrelevant.
  for (Instruction &I : instructions(F)) {
    if (auto *CB = dyn_cast<CallBase>(&I)) {
      if (Function *Callee = CB->getCalledFunction()) {
        if (Visited.insert(Callee).second && !Callee->isDeclaration()) {
          Node *CalleeN = G.lookup(*Callee);
          assert(CalleeN &&
                 "Visited function should already have an associated node");
          Edge *E = N->lookup(*CalleeN);
          assert((E || !FunctionPass) &&
                 "No function transformations should introduce *new* "
                 "call edges! Any new calls should be modeled as "
                 "promoted existing ref edges!");
          bool Inserted = RetainedEdges.insert(CalleeN).second;
          (void)Inserted;
          assert(Inserted && "We should never visit a function twice.");
          if (!E)
            NewCallEdges.insert(CalleeN);
          else if (!E->isCall())
            PromotedRefTargets.insert(CalleeN);
        }
      } else {
        // We can miss devirtualization if an indirect call is created then
        // promoted before updateCGAndAnalysisManagerForPass runs.
        auto *Entry = UR.IndirectVHs.find(CB);
        if (Entry == UR.IndirectVHs.end())
          UR.IndirectVHs.insert({CB, WeakTrackingVH(CB)});
        else if (!Entry->second)
          Entry->second = WeakTrackingVH(CB);
      }
    }
  }

  // Now walk all references.
  for (Instruction &I : instructions(F))
    for (Value *Op : I.operand_values())
      if (auto *OpC = dyn_cast<Constant>(Op))
        if (Visited.insert(OpC).second)
          Worklist.push_back(OpC);

  auto VisitRef = [&](Function &Referee) {
    Node *RefereeN = G.lookup(Referee);
    assert(RefereeN &&
           "Visited function should already have an associated node");
    Edge *E = N->lookup(*RefereeN);
    assert((E || !FunctionPass) &&
           "No function transformations should introduce *new* ref "
           "edges! Any new ref edges would require IPO which "
           "function passes aren't allowed to do!");
    bool Inserted = RetainedEdges.insert(RefereeN).second;
    (void)Inserted;
    assert(Inserted && "We should never visit a function twice.");
    if (!E)
      NewRefEdges.insert(RefereeN);
    else if (E->isCall())
      DemotedCallTargets.insert(RefereeN);
  };
  LazyCallGraph::visitReferences(Worklist, Visited, VisitRef);

  // Handle new ref edges.
  for (Node *RefTarget : NewRefEdges) {
    SCC &TargetC = *G.lookupSCC(*RefTarget);
    RefSCC &TargetRC = TargetC.getOuterRefSCC();
    (void)TargetRC;
    // TODO: This only allows trivial edges to be added for now.
#ifdef EXPENSIVE_CHECKS
    assert((RC == &TargetRC ||
           RC->isAncestorOf(TargetRC)) && "New ref edge is not trivial!");
#endif
    RC->insertTrivialRefEdge(N, *RefTarget);
  }

  // Handle new call edges.
  for (Node *CallTarget : NewCallEdges) {
    SCC &TargetC = *G.lookupSCC(*CallTarget);
    RefSCC &TargetRC = TargetC.getOuterRefSCC();
    (void)TargetRC;
    // TODO: This only allows trivial edges to be added for now.
#ifdef EXPENSIVE_CHECKS
    assert((RC == &TargetRC ||
           RC->isAncestorOf(TargetRC)) && "New call edge is not trivial!");
#endif
    // Add a trivial ref edge to be promoted later on alongside
    // PromotedRefTargets.
    RC->insertTrivialRefEdge(N, *CallTarget);
  }

  // Include synthetic reference edges to known, defined lib functions.
  for (auto *LibFn : G.getLibFunctions())
    // While the list of lib functions doesn't have repeats, don't re-visit
    // anything handled above.
    if (!Visited.count(LibFn))
      VisitRef(*LibFn);

  // First remove all of the edges that are no longer present in this function.
  // The first step makes these edges uniformly ref edges and accumulates them
  // into a separate data structure so removal doesn't invalidate anything.
  SmallVector<Node *, 4> DeadTargets;
  for (Edge &E : *N) {
    if (RetainedEdges.count(&E.getNode()))
      continue;

    SCC &TargetC = *G.lookupSCC(E.getNode());
    RefSCC &TargetRC = TargetC.getOuterRefSCC();
    if (&TargetRC == RC && E.isCall()) {
      if (C != &TargetC) {
        // For separate SCCs this is trivial.
        RC->switchTrivialInternalEdgeToRef(N, E.getNode());
      } else {
        // Now update the call graph.
        C = incorporateNewSCCRange(RC->switchInternalEdgeToRef(N, E.getNode()),
                                   G, N, C, AM, UR);
      }
    }

    // Now that this is ready for actual removal, put it into our list.
    DeadTargets.push_back(&E.getNode());
  }
  // Remove the easy cases quickly and actually pull them out of our list.
  llvm::erase_if(DeadTargets, [&](Node *TargetN) {
    SCC &TargetC = *G.lookupSCC(*TargetN);
    RefSCC &TargetRC = TargetC.getOuterRefSCC();

    // We can't trivially remove internal targets, so skip
    // those.
    if (&TargetRC == RC)
      return false;

    LLVM_DEBUG(dbgs() << "Deleting outgoing edge from '" << N << "' to '"
                      << *TargetN << "'\n");
    RC->removeOutgoingEdge(N, *TargetN);
    return true;
  });

  // Next demote all the call edges that are now ref edges. This helps make
  // the SCCs small which should minimize the work below as we don't want to
  // form cycles that this would break.
  for (Node *RefTarget : DemotedCallTargets) {
    SCC &TargetC = *G.lookupSCC(*RefTarget);
    RefSCC &TargetRC = TargetC.getOuterRefSCC();

    // The easy case is when the target RefSCC is not this RefSCC. This is
    // only supported when the target RefSCC is a child of this RefSCC.
    if (&TargetRC != RC) {
#ifdef EXPENSIVE_CHECKS
      assert(RC->isAncestorOf(TargetRC) &&
             "Cannot potentially form RefSCC cycles here!");
#endif
      RC->switchOutgoingEdgeToRef(N, *RefTarget);
      LLVM_DEBUG(dbgs() << "Switch outgoing call edge to a ref edge from '" << N
                        << "' to '" << *RefTarget << "'\n");
      continue;
    }

    // We are switching an internal call edge to a ref edge. This may split up
    // some SCCs.
    if (C != &TargetC) {
      // For separate SCCs this is trivial.
      RC->switchTrivialInternalEdgeToRef(N, *RefTarget);
      continue;
    }

    // Now update the call graph.
    C = incorporateNewSCCRange(RC->switchInternalEdgeToRef(N, *RefTarget), G, N,
                               C, AM, UR);
  }

  // We added a ref edge earlier for new call edges, promote those to call edges
  // alongside PromotedRefTargets.
  for (Node *E : NewCallEdges)
    PromotedRefTargets.insert(E);

  // Now promote ref edges into call edges.
  for (Node *CallTarget : PromotedRefTargets) {
    SCC &TargetC = *G.lookupSCC(*CallTarget);
    RefSCC &TargetRC = TargetC.getOuterRefSCC();

    // The easy case is when the target RefSCC is not this RefSCC. This is
    // only supported when the target RefSCC is a child of this RefSCC.
    if (&TargetRC != RC) {
#ifdef EXPENSIVE_CHECKS
      assert(RC->isAncestorOf(TargetRC) &&
             "Cannot potentially form RefSCC cycles here!");
#endif
      RC->switchOutgoingEdgeToCall(N, *CallTarget);
      LLVM_DEBUG(dbgs() << "Switch outgoing ref edge to a call edge from '" << N
                        << "' to '" << *CallTarget << "'\n");
      continue;
    }
    LLVM_DEBUG(dbgs() << "Switch an internal ref edge to a call edge from '"
                      << N << "' to '" << *CallTarget << "'\n");

    // Otherwise we are switching an internal ref edge to a call edge. This
    // may merge away some SCCs, and we add those to the UpdateResult. We also
    // need to make sure to update the worklist in the event SCCs have moved
    // before the current one in the post-order sequence
    bool HasFunctionAnalysisProxy = false;
    auto InitialSCCIndex = RC->find(*C) - RC->begin();
    bool FormedCycle = RC->switchInternalEdgeToCall(
        N, *CallTarget, [&](ArrayRef<SCC *> MergedSCCs) {
          for (SCC *MergedC : MergedSCCs) {
            assert(MergedC != &TargetC && "Cannot merge away the target SCC!");

            HasFunctionAnalysisProxy |=
                AM.getCachedResult<FunctionAnalysisManagerCGSCCProxy>(
                    *MergedC) != nullptr;

            // Mark that this SCC will no longer be valid.
            UR.InvalidatedSCCs.insert(MergedC);

            // FIXME: We should really do a 'clear' here to forcibly release
            // memory, but we don't have a good way of doing that and
            // preserving the function analyses.
            auto PA = PreservedAnalyses::allInSet<AllAnalysesOn<Function>>();
            PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
            AM.invalidate(*MergedC, PA);
          }
        });

    // If we formed a cycle by creating this call, we need to update more data
    // structures.
    if (FormedCycle) {
      C = &TargetC;
      assert(G.lookupSCC(N) == C && "Failed to update current SCC!");

      // If one of the invalidated SCCs had a cached proxy to a function
      // analysis manager, we need to create a proxy in the new current SCC as
      // the invalidated SCCs had their functions moved.
      if (HasFunctionAnalysisProxy)
        AM.getResult<FunctionAnalysisManagerCGSCCProxy>(*C, G).updateFAM(FAM);

      // Any analyses cached for this SCC are no longer precise as the shape
      // has changed by introducing this cycle. However, we have taken care to
      // update the proxies so it remains valide.
      auto PA = PreservedAnalyses::allInSet<AllAnalysesOn<Function>>();
      PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
      AM.invalidate(*C, PA);
    }
    auto NewSCCIndex = RC->find(*C) - RC->begin();
    // If we have actually moved an SCC to be topologically "below" the current
    // one due to merging, we will need to revisit the current SCC after
    // visiting those moved SCCs.
    //
    // It is critical that we *do not* revisit the current SCC unless we
    // actually move SCCs in the process of merging because otherwise we may
    // form a cycle where an SCC is split apart, merged, split, merged and so
    // on infinitely.
    if (InitialSCCIndex < NewSCCIndex) {
      // Put our current SCC back onto the worklist as we'll visit other SCCs
      // that are now definitively ordered prior to the current one in the
      // post-order sequence, and may end up observing more precise context to
      // optimize the current SCC.
      UR.CWorklist.insert(C);
      LLVM_DEBUG(dbgs() << "Enqueuing the existing SCC in the worklist: " << *C
                        << "\n");
      // Enqueue in reverse order as we pop off the back of the worklist.
      for (SCC &MovedC : llvm::reverse(make_range(RC->begin() + InitialSCCIndex,
                                                  RC->begin() + NewSCCIndex))) {
        UR.CWorklist.insert(&MovedC);
        LLVM_DEBUG(dbgs() << "Enqueuing a newly earlier in post-order SCC: "
                          << MovedC << "\n");
      }
    }
  }

  assert(!UR.InvalidatedSCCs.count(C) && "Invalidated the current SCC!");
  assert(&C->getOuterRefSCC() == RC && "Current SCC not in current RefSCC!");

  // Record the current SCC for higher layers of the CGSCC pass manager now that
  // all the updates have been applied.
  if (C != &InitialC)
    UR.UpdatedC = C;

  return *C;
}

LazyCallGraph::SCC &llvm::updateCGAndAnalysisManagerForFunctionPass(
    LazyCallGraph &G, LazyCallGraph::SCC &InitialC, LazyCallGraph::Node &N,
    CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR,
    FunctionAnalysisManager &FAM) {
  return updateCGAndAnalysisManagerForPass(G, InitialC, N, AM, UR, FAM,
                                           /* FunctionPass */ true);
}
LazyCallGraph::SCC &llvm::updateCGAndAnalysisManagerForCGSCCPass(
    LazyCallGraph &G, LazyCallGraph::SCC &InitialC, LazyCallGraph::Node &N,
    CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR,
    FunctionAnalysisManager &FAM) {
  return updateCGAndAnalysisManagerForPass(G, InitialC, N, AM, UR, FAM,
                                           /* FunctionPass */ false);
}
