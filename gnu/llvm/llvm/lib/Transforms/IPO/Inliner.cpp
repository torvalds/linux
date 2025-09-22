//===- Inliner.cpp - Code common to all inliners --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the mechanics required to implement inlining without
// missing any calls and updating the call graph.  The decisions of which calls
// are profitable to inline are implemented elsewhere.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ReplayInlineAdvisor.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/Utils/ImportedFunctionsInliningStatistics.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/CallPromotionUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "inline"

STATISTIC(NumInlined, "Number of functions inlined");
STATISTIC(NumDeleted, "Number of functions deleted because all callers found");

static cl::opt<int> IntraSCCCostMultiplier(
    "intra-scc-cost-multiplier", cl::init(2), cl::Hidden,
    cl::desc(
        "Cost multiplier to multiply onto inlined call sites where the "
        "new call was previously an intra-SCC call (not relevant when the "
        "original call was already intra-SCC). This can accumulate over "
        "multiple inlinings (e.g. if a call site already had a cost "
        "multiplier and one of its inlined calls was also subject to "
        "this, the inlined call would have the original multiplier "
        "multiplied by intra-scc-cost-multiplier). This is to prevent tons of "
        "inlining through a child SCC which can cause terrible compile times"));

/// A flag for test, so we can print the content of the advisor when running it
/// as part of the default (e.g. -O3) pipeline.
static cl::opt<bool> KeepAdvisorForPrinting("keep-inline-advisor-for-printing",
                                            cl::init(false), cl::Hidden);

/// Allows printing the contents of the advisor after each SCC inliner pass.
static cl::opt<bool>
    EnablePostSCCAdvisorPrinting("enable-scc-inline-advisor-printing",
                                 cl::init(false), cl::Hidden);


static cl::opt<std::string> CGSCCInlineReplayFile(
    "cgscc-inline-replay", cl::init(""), cl::value_desc("filename"),
    cl::desc(
        "Optimization remarks file containing inline remarks to be replayed "
        "by cgscc inlining."),
    cl::Hidden);

static cl::opt<ReplayInlinerSettings::Scope> CGSCCInlineReplayScope(
    "cgscc-inline-replay-scope",
    cl::init(ReplayInlinerSettings::Scope::Function),
    cl::values(clEnumValN(ReplayInlinerSettings::Scope::Function, "Function",
                          "Replay on functions that have remarks associated "
                          "with them (default)"),
               clEnumValN(ReplayInlinerSettings::Scope::Module, "Module",
                          "Replay on the entire module")),
    cl::desc("Whether inline replay should be applied to the entire "
             "Module or just the Functions (default) that are present as "
             "callers in remarks during cgscc inlining."),
    cl::Hidden);

static cl::opt<ReplayInlinerSettings::Fallback> CGSCCInlineReplayFallback(
    "cgscc-inline-replay-fallback",
    cl::init(ReplayInlinerSettings::Fallback::Original),
    cl::values(
        clEnumValN(
            ReplayInlinerSettings::Fallback::Original, "Original",
            "All decisions not in replay send to original advisor (default)"),
        clEnumValN(ReplayInlinerSettings::Fallback::AlwaysInline,
                   "AlwaysInline", "All decisions not in replay are inlined"),
        clEnumValN(ReplayInlinerSettings::Fallback::NeverInline, "NeverInline",
                   "All decisions not in replay are not inlined")),
    cl::desc(
        "How cgscc inline replay treats sites that don't come from the replay. "
        "Original: defers to original advisor, AlwaysInline: inline all sites "
        "not in replay, NeverInline: inline no sites not in replay"),
    cl::Hidden);

static cl::opt<CallSiteFormat::Format> CGSCCInlineReplayFormat(
    "cgscc-inline-replay-format",
    cl::init(CallSiteFormat::Format::LineColumnDiscriminator),
    cl::values(
        clEnumValN(CallSiteFormat::Format::Line, "Line", "<Line Number>"),
        clEnumValN(CallSiteFormat::Format::LineColumn, "LineColumn",
                   "<Line Number>:<Column Number>"),
        clEnumValN(CallSiteFormat::Format::LineDiscriminator,
                   "LineDiscriminator", "<Line Number>.<Discriminator>"),
        clEnumValN(CallSiteFormat::Format::LineColumnDiscriminator,
                   "LineColumnDiscriminator",
                   "<Line Number>:<Column Number>.<Discriminator> (default)")),
    cl::desc("How cgscc inline replay file is formatted"), cl::Hidden);

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

InlineAdvisor &
InlinerPass::getAdvisor(const ModuleAnalysisManagerCGSCCProxy::Result &MAM,
                        FunctionAnalysisManager &FAM, Module &M) {
  if (OwnedAdvisor)
    return *OwnedAdvisor;

  auto *IAA = MAM.getCachedResult<InlineAdvisorAnalysis>(M);
  if (!IAA) {
    // It should still be possible to run the inliner as a stand-alone SCC pass,
    // for test scenarios. In that case, we default to the
    // DefaultInlineAdvisor, which doesn't need to keep state between SCC pass
    // runs. It also uses just the default InlineParams.
    // In this case, we need to use the provided FAM, which is valid for the
    // duration of the inliner pass, and thus the lifetime of the owned advisor.
    // The one we would get from the MAM can be invalidated as a result of the
    // inliner's activity.
    OwnedAdvisor = std::make_unique<DefaultInlineAdvisor>(
        M, FAM, getInlineParams(),
        InlineContext{LTOPhase, InlinePass::CGSCCInliner});

    if (!CGSCCInlineReplayFile.empty())
      OwnedAdvisor = getReplayInlineAdvisor(
          M, FAM, M.getContext(), std::move(OwnedAdvisor),
          ReplayInlinerSettings{CGSCCInlineReplayFile,
                                CGSCCInlineReplayScope,
                                CGSCCInlineReplayFallback,
                                {CGSCCInlineReplayFormat}},
          /*EmitRemarks=*/true,
          InlineContext{LTOPhase, InlinePass::ReplayCGSCCInliner});

    return *OwnedAdvisor;
  }
  assert(IAA->getAdvisor() &&
         "Expected a present InlineAdvisorAnalysis also have an "
         "InlineAdvisor initialized");
  return *IAA->getAdvisor();
}

void makeFunctionBodyUnreachable(Function &F) {
  F.dropAllReferences();
  for (BasicBlock &BB : make_early_inc_range(F))
    BB.eraseFromParent();
  BasicBlock *BB = BasicBlock::Create(F.getContext(), "", &F);
  new UnreachableInst(F.getContext(), BB);
}

PreservedAnalyses InlinerPass::run(LazyCallGraph::SCC &InitialC,
                                   CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                   CGSCCUpdateResult &UR) {
  const auto &MAMProxy =
      AM.getResult<ModuleAnalysisManagerCGSCCProxy>(InitialC, CG);
  bool Changed = false;

  assert(InitialC.size() > 0 && "Cannot handle an empty SCC!");
  Module &M = *InitialC.begin()->getFunction().getParent();
  ProfileSummaryInfo *PSI = MAMProxy.getCachedResult<ProfileSummaryAnalysis>(M);

  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(InitialC, CG)
          .getManager();

  InlineAdvisor &Advisor = getAdvisor(MAMProxy, FAM, M);
  Advisor.onPassEntry(&InitialC);

  // We use a single common worklist for calls across the entire SCC. We
  // process these in-order and append new calls introduced during inlining to
  // the end. The PriorityInlineOrder is optional here, in which the smaller
  // callee would have a higher priority to inline.
  //
  // Note that this particular order of processing is actually critical to
  // avoid very bad behaviors. Consider *highly connected* call graphs where
  // each function contains a small amount of code and a couple of calls to
  // other functions. Because the LLVM inliner is fundamentally a bottom-up
  // inliner, it can handle gracefully the fact that these all appear to be
  // reasonable inlining candidates as it will flatten things until they become
  // too big to inline, and then move on and flatten another batch.
  //
  // However, when processing call edges *within* an SCC we cannot rely on this
  // bottom-up behavior. As a consequence, with heavily connected *SCCs* of
  // functions we can end up incrementally inlining N calls into each of
  // N functions because each incremental inlining decision looks good and we
  // don't have a topological ordering to prevent explosions.
  //
  // To compensate for this, we don't process transitive edges made immediate
  // by inlining until we've done one pass of inlining across the entire SCC.
  // Large, highly connected SCCs still lead to some amount of code bloat in
  // this model, but it is uniformly spread across all the functions in the SCC
  // and eventually they all become too large to inline, rather than
  // incrementally maknig a single function grow in a super linear fashion.
  SmallVector<std::pair<CallBase *, int>, 16> Calls;

  // Populate the initial list of calls in this SCC.
  for (auto &N : InitialC) {
    auto &ORE =
        FAM.getResult<OptimizationRemarkEmitterAnalysis>(N.getFunction());
    // We want to generally process call sites top-down in order for
    // simplifications stemming from replacing the call with the returned value
    // after inlining to be visible to subsequent inlining decisions.
    // FIXME: Using instructions sequence is a really bad way to do this.
    // Instead we should do an actual RPO walk of the function body.
    for (Instruction &I : instructions(N.getFunction()))
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (Function *Callee = CB->getCalledFunction()) {
          if (!Callee->isDeclaration())
            Calls.push_back({CB, -1});
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

  // Capture updatable variable for the current SCC.
  auto *C = &InitialC;

  auto AdvisorOnExit = make_scope_exit([&] { Advisor.onPassExit(C); });

  if (Calls.empty())
    return PreservedAnalyses::all();

  // When inlining a callee produces new call sites, we want to keep track of
  // the fact that they were inlined from the callee.  This allows us to avoid
  // infinite inlining in some obscure cases.  To represent this, we use an
  // index into the InlineHistory vector.
  SmallVector<std::pair<Function *, int>, 16> InlineHistory;

  // Track a set vector of inlined callees so that we can augment the caller
  // with all of their edges in the call graph before pruning out the ones that
  // got simplified away.
  SmallSetVector<Function *, 4> InlinedCallees;

  // Track the dead functions to delete once finished with inlining calls. We
  // defer deleting these to make it easier to handle the call graph updates.
  SmallVector<Function *, 4> DeadFunctions;

  // Track potentially dead non-local functions with comdats to see if they can
  // be deleted as a batch after inlining.
  SmallVector<Function *, 4> DeadFunctionsInComdats;

  // Loop forward over all of the calls. Note that we cannot cache the size as
  // inlining can introduce new calls that need to be processed.
  for (int I = 0; I < (int)Calls.size(); ++I) {
    // We expect the calls to typically be batched with sequences of calls that
    // have the same caller, so we first set up some shared infrastructure for
    // this caller. We also do any pruning we can at this layer on the caller
    // alone.
    Function &F = *Calls[I].first->getCaller();
    LazyCallGraph::Node &N = *CG.lookup(F);
    if (CG.lookupSCC(N) != C)
      continue;

    LLVM_DEBUG(dbgs() << "Inlining calls in: " << F.getName() << "\n"
                      << "    Function size: " << F.getInstructionCount()
                      << "\n");

    auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
      return FAM.getResult<AssumptionAnalysis>(F);
    };

    // Now process as many calls as we have within this caller in the sequence.
    // We bail out as soon as the caller has to change so we can update the
    // call graph and prepare the context of that new caller.
    bool DidInline = false;
    for (; I < (int)Calls.size() && Calls[I].first->getCaller() == &F; ++I) {
      auto &P = Calls[I];
      CallBase *CB = P.first;
      const int InlineHistoryID = P.second;
      Function &Callee = *CB->getCalledFunction();

      if (InlineHistoryID != -1 &&
          inlineHistoryIncludes(&Callee, InlineHistoryID, InlineHistory)) {
        LLVM_DEBUG(dbgs() << "Skipping inlining due to history: " << F.getName()
                          << " -> " << Callee.getName() << "\n");
        setInlineRemark(*CB, "recursive");
        // Set noinline so that we don't forget this decision across CGSCC
        // iterations.
        CB->setIsNoInline();
        continue;
      }

      // Check if this inlining may repeat breaking an SCC apart that has
      // already been split once before. In that case, inlining here may
      // trigger infinite inlining, much like is prevented within the inliner
      // itself by the InlineHistory above, but spread across CGSCC iterations
      // and thus hidden from the full inline history.
      LazyCallGraph::SCC *CalleeSCC = CG.lookupSCC(*CG.lookup(Callee));
      if (CalleeSCC == C && UR.InlinedInternalEdges.count({&N, C})) {
        LLVM_DEBUG(dbgs() << "Skipping inlining internal SCC edge from a node "
                             "previously split out of this SCC by inlining: "
                          << F.getName() << " -> " << Callee.getName() << "\n");
        setInlineRemark(*CB, "recursive SCC split");
        continue;
      }

      std::unique_ptr<InlineAdvice> Advice =
          Advisor.getAdvice(*CB, OnlyMandatory);

      // Check whether we want to inline this callsite.
      if (!Advice)
        continue;

      if (!Advice->isInliningRecommended()) {
        Advice->recordUnattemptedInlining();
        continue;
      }

      int CBCostMult =
          getStringFnAttrAsInt(
              *CB, InlineConstants::FunctionInlineCostMultiplierAttributeName)
              .value_or(1);

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

      DidInline = true;
      InlinedCallees.insert(&Callee);
      ++NumInlined;

      LLVM_DEBUG(dbgs() << "    Size after inlining: "
                        << F.getInstructionCount() << "\n");

      // Add any new callsites to defined functions to the worklist.
      if (!IFI.InlinedCallSites.empty()) {
        int NewHistoryID = InlineHistory.size();
        InlineHistory.push_back({&Callee, InlineHistoryID});

        for (CallBase *ICB : reverse(IFI.InlinedCallSites)) {
          Function *NewCallee = ICB->getCalledFunction();
          assert(!(NewCallee && NewCallee->isIntrinsic()) &&
                 "Intrinsic calls should not be tracked.");
          if (!NewCallee) {
            // Try to promote an indirect (virtual) call without waiting for
            // the post-inline cleanup and the next DevirtSCCRepeatedPass
            // iteration because the next iteration may not happen and we may
            // miss inlining it.
            if (tryPromoteCall(*ICB))
              NewCallee = ICB->getCalledFunction();
          }
          if (NewCallee) {
            if (!NewCallee->isDeclaration()) {
              Calls.push_back({ICB, NewHistoryID});
              // Continually inlining through an SCC can result in huge compile
              // times and bloated code since we arbitrarily stop at some point
              // when the inliner decides it's not profitable to inline anymore.
              // We attempt to mitigate this by making these calls exponentially
              // more expensive.
              // This doesn't apply to calls in the same SCC since if we do
              // inline through the SCC the function will end up being
              // self-recursive which the inliner bails out on, and inlining
              // within an SCC is necessary for performance.
              if (CalleeSCC != C &&
                  CalleeSCC == CG.lookupSCC(CG.get(*NewCallee))) {
                Attribute NewCBCostMult = Attribute::get(
                    M.getContext(),
                    InlineConstants::FunctionInlineCostMultiplierAttributeName,
                    itostr(CBCostMult * IntraSCCCostMultiplier));
                ICB->addFnAttr(NewCBCostMult);
              }
            }
          }
        }
      }

      // For local functions or discardable functions without comdats, check
      // whether this makes the callee trivially dead. In that case, we can drop
      // the body of the function eagerly which may reduce the number of callers
      // of other functions to one, changing inline cost thresholds. Non-local
      // discardable functions with comdats are checked later on.
      bool CalleeWasDeleted = false;
      if (Callee.isDiscardableIfUnused() && Callee.hasZeroLiveUses() &&
          !CG.isLibFunction(Callee)) {
        if (Callee.hasLocalLinkage() || !Callee.hasComdat()) {
          Calls.erase(
              std::remove_if(Calls.begin() + I + 1, Calls.end(),
                             [&](const std::pair<CallBase *, int> &Call) {
                               return Call.first->getCaller() == &Callee;
                             }),
              Calls.end());

          // Clear the body and queue the function itself for call graph
          // updating when we finish inlining.
          makeFunctionBodyUnreachable(Callee);
          assert(!is_contained(DeadFunctions, &Callee) &&
                 "Cannot put cause a function to become dead twice!");
          DeadFunctions.push_back(&Callee);
          CalleeWasDeleted = true;
        } else {
          DeadFunctionsInComdats.push_back(&Callee);
        }
      }
      if (CalleeWasDeleted)
        Advice->recordInliningWithCalleeDeleted();
      else
        Advice->recordInlining();
    }

    // Back the call index up by one to put us in a good position to go around
    // the outer loop.
    --I;

    if (!DidInline)
      continue;
    Changed = true;

    // At this point, since we have made changes we have at least removed
    // a call instruction. However, in the process we do some incremental
    // simplification of the surrounding code. This simplification can
    // essentially do all of the same things as a function pass and we can
    // re-use the exact same logic for updating the call graph to reflect the
    // change.

    // Inside the update, we also update the FunctionAnalysisManager in the
    // proxy for this particular SCC. We do this as the SCC may have changed and
    // as we're going to mutate this particular function we want to make sure
    // the proxy is in place to forward any invalidation events.
    LazyCallGraph::SCC *OldC = C;
    C = &updateCGAndAnalysisManagerForCGSCCPass(CG, *C, N, AM, UR, FAM);
    LLVM_DEBUG(dbgs() << "Updated inlining SCC: " << *C << "\n");

    // If this causes an SCC to split apart into multiple smaller SCCs, there
    // is a subtle risk we need to prepare for. Other transformations may
    // expose an "infinite inlining" opportunity later, and because of the SCC
    // mutation, we will revisit this function and potentially re-inline. If we
    // do, and that re-inlining also has the potentially to mutate the SCC
    // structure, the infinite inlining problem can manifest through infinite
    // SCC splits and merges. To avoid this, we capture the originating caller
    // node and the SCC containing the call edge. This is a slight over
    // approximation of the possible inlining decisions that must be avoided,
    // but is relatively efficient to store. We use C != OldC to know when
    // a new SCC is generated and the original SCC may be generated via merge
    // in later iterations.
    //
    // It is also possible that even if no new SCC is generated
    // (i.e., C == OldC), the original SCC could be split and then merged
    // into the same one as itself. and the original SCC will be added into
    // UR.CWorklist again, we want to catch such cases too.
    //
    // FIXME: This seems like a very heavyweight way of retaining the inline
    // history, we should look for a more efficient way of tracking it.
    if ((C != OldC || UR.CWorklist.count(OldC)) &&
        llvm::any_of(InlinedCallees, [&](Function *Callee) {
          return CG.lookupSCC(*CG.lookup(*Callee)) == OldC;
        })) {
      LLVM_DEBUG(dbgs() << "Inlined an internal call edge and split an SCC, "
                           "retaining this to avoid infinite inlining.\n");
      UR.InlinedInternalEdges.insert({&N, OldC});
    }
    InlinedCallees.clear();

    // Invalidate analyses for this function now so that we don't have to
    // invalidate analyses for all functions in this SCC later.
    FAM.invalidate(F, PreservedAnalyses::none());
  }

  // We must ensure that we only delete functions with comdats if every function
  // in the comdat is going to be deleted.
  if (!DeadFunctionsInComdats.empty()) {
    filterDeadComdatFunctions(DeadFunctionsInComdats);
    for (auto *Callee : DeadFunctionsInComdats)
      makeFunctionBodyUnreachable(*Callee);
    DeadFunctions.append(DeadFunctionsInComdats);
  }

  // Now that we've finished inlining all of the calls across this SCC, delete
  // all of the trivially dead functions, updating the call graph and the CGSCC
  // pass manager in the process.
  //
  // Note that this walks a pointer set which has non-deterministic order but
  // that is OK as all we do is delete things and add pointers to unordered
  // sets.
  for (Function *DeadF : DeadFunctions) {
    CG.markDeadFunction(*DeadF);
    // Get the necessary information out of the call graph and nuke the
    // function there. Also, clear out any cached analyses.
    auto &DeadC = *CG.lookupSCC(*CG.lookup(*DeadF));
    FAM.clear(*DeadF, DeadF->getName());
    AM.clear(DeadC, DeadC.getName());

    // Mark the relevant parts of the call graph as invalid so we don't visit
    // them.
    UR.InvalidatedSCCs.insert(&DeadC);

    UR.DeadFunctions.push_back(DeadF);

    ++NumDeleted;
  }

  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  // Even if we change the IR, we update the core CGSCC data structures and so
  // can preserve the proxy to the function analysis manager.
  PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
  // We have already invalidated all analyses on modified functions.
  PA.preserveSet<AllAnalysesOn<Function>>();
  return PA;
}

ModuleInlinerWrapperPass::ModuleInlinerWrapperPass(InlineParams Params,
                                                   bool MandatoryFirst,
                                                   InlineContext IC,
                                                   InliningAdvisorMode Mode,
                                                   unsigned MaxDevirtIterations)
    : Params(Params), IC(IC), Mode(Mode),
      MaxDevirtIterations(MaxDevirtIterations) {
  // Run the inliner first. The theory is that we are walking bottom-up and so
  // the callees have already been fully optimized, and we want to inline them
  // into the callers so that our optimizations can reflect that.
  // For PreLinkThinLTO pass, we disable hot-caller heuristic for sample PGO
  // because it makes profile annotation in the backend inaccurate.
  if (MandatoryFirst) {
    PM.addPass(InlinerPass(/*OnlyMandatory*/ true));
    if (EnablePostSCCAdvisorPrinting)
      PM.addPass(InlineAdvisorAnalysisPrinterPass(dbgs()));
  }
  PM.addPass(InlinerPass());
  if (EnablePostSCCAdvisorPrinting)
    PM.addPass(InlineAdvisorAnalysisPrinterPass(dbgs()));
}

PreservedAnalyses ModuleInlinerWrapperPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  auto &IAA = MAM.getResult<InlineAdvisorAnalysis>(M);
  if (!IAA.tryCreate(Params, Mode,
                     {CGSCCInlineReplayFile,
                      CGSCCInlineReplayScope,
                      CGSCCInlineReplayFallback,
                      {CGSCCInlineReplayFormat}},
                     IC)) {
    M.getContext().emitError(
        "Could not setup Inlining Advisor for the requested "
        "mode and/or options");
    return PreservedAnalyses::all();
  }

  // We wrap the CGSCC pipeline in a devirtualization repeater. This will try
  // to detect when we devirtualize indirect calls and iterate the SCC passes
  // in that case to try and catch knock-on inlining or function attrs
  // opportunities. Then we add it to the module pipeline by walking the SCCs
  // in postorder (or bottom-up).
  // If MaxDevirtIterations is 0, we just don't use the devirtualization
  // wrapper.
  if (MaxDevirtIterations == 0)
    MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(PM)));
  else
    MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(
        createDevirtSCCRepeatedPass(std::move(PM), MaxDevirtIterations)));

  MPM.addPass(std::move(AfterCGMPM));
  MPM.run(M, MAM);

  // Discard the InlineAdvisor, a subsequent inlining session should construct
  // its own.
  auto PA = PreservedAnalyses::all();
  if (!KeepAdvisorForPrinting)
    PA.abandon<InlineAdvisorAnalysis>();
  return PA;
}

void InlinerPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<InlinerPass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  if (OnlyMandatory)
    OS << "<only-mandatory>";
}

void ModuleInlinerWrapperPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  // Print some info about passes added to the wrapper. This is however
  // incomplete as InlineAdvisorAnalysis part isn't included (which also depends
  // on Params and Mode).
  if (!MPM.isEmpty()) {
    MPM.printPipeline(OS, MapClassName2PassName);
    OS << ',';
  }
  OS << "cgscc(";
  if (MaxDevirtIterations != 0)
    OS << "devirt<" << MaxDevirtIterations << ">(";
  PM.printPipeline(OS, MapClassName2PassName);
  if (MaxDevirtIterations != 0)
    OS << ')';
  OS << ')';
}
