//===-- CSPreInliner.cpp - Profile guided preinliner -------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CSPreInliner.h"
#include "ProfiledBinary.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Transforms/IPO/SampleProfile.h"
#include <cstdint>
#include <queue>

#define DEBUG_TYPE "cs-preinliner"

using namespace llvm;
using namespace sampleprof;

STATISTIC(PreInlNumCSInlined,
          "Number of functions inlined with context sensitive profile");
STATISTIC(PreInlNumCSNotInlined,
          "Number of functions not inlined with context sensitive profile");
STATISTIC(PreInlNumCSInlinedHitMinLimit,
          "Number of functions with FDO inline stopped due to min size limit");
STATISTIC(PreInlNumCSInlinedHitMaxLimit,
          "Number of functions with FDO inline stopped due to max size limit");
STATISTIC(
    PreInlNumCSInlinedHitGrowthLimit,
    "Number of functions with FDO inline stopped due to growth size limit");

// The switches specify inline thresholds used in SampleProfileLoader inlining.
// TODO: the actual threshold to be tuned here because the size here is based
// on machine code not LLVM IR.
namespace llvm {
cl::opt<bool> EnableCSPreInliner(
    "csspgo-preinliner", cl::Hidden, cl::init(true),
    cl::desc("Run a global pre-inliner to merge context profile based on "
             "estimated global top-down inline decisions"));

cl::opt<bool> UseContextCostForPreInliner(
    "use-context-cost-for-preinliner", cl::Hidden, cl::init(true),
    cl::desc("Use context-sensitive byte size cost for preinliner decisions"));
} // namespace llvm

static cl::opt<bool> SamplePreInlineReplay(
    "csspgo-replay-preinline", cl::Hidden, cl::init(false),
    cl::desc(
        "Replay previous inlining and adjust context profile accordingly"));

static cl::opt<int> CSPreinlMultiplierForPrevInl(
    "csspgo-preinliner-multiplier-for-previous-inlining", cl::Hidden,
    cl::init(100),
    cl::desc(
        "Multiplier to bump up callsite threshold for previous inlining."));

CSPreInliner::CSPreInliner(SampleContextTracker &Tracker,
                           ProfiledBinary &Binary, ProfileSummary *Summary)
    : UseContextCost(UseContextCostForPreInliner),
      // TODO: Pass in a guid-to-name map in order for
      // ContextTracker.getFuncNameFor to work, if `Profiles` can have md5 codes
      // as their profile context.
      ContextTracker(Tracker), Binary(Binary), Summary(Summary) {
  // Set default preinliner hot/cold call site threshold tuned with CSSPGO.
  // for good performance with reasonable profile size.
  if (!SampleHotCallSiteThreshold.getNumOccurrences())
    SampleHotCallSiteThreshold = 1500;
  if (!SampleColdCallSiteThreshold.getNumOccurrences())
    SampleColdCallSiteThreshold = 0;
  if (!ProfileInlineLimitMax.getNumOccurrences())
    ProfileInlineLimitMax = 50000;
}

std::vector<FunctionId> CSPreInliner::buildTopDownOrder() {
  std::vector<FunctionId> Order;
  // Trim cold edges to get a more stable call graph. This allows for a more
  // stable top-down order which in turns helps the stablity of the generated
  // profile from run to run.
  uint64_t ColdCountThreshold = ProfileSummaryBuilder::getColdCountThreshold(
      (Summary->getDetailedSummary()));
  ProfiledCallGraph ProfiledCG(ContextTracker, ColdCountThreshold);

  // Now that we have a profiled call graph, construct top-down order
  // by building up SCC and reversing SCC order.
  scc_iterator<ProfiledCallGraph *> I = scc_begin(&ProfiledCG);
  while (!I.isAtEnd()) {
    auto Range = *I;
    if (SortProfiledSCC) {
      // Sort nodes in one SCC based on callsite hotness.
      scc_member_iterator<ProfiledCallGraph *> SI(*I);
      Range = *SI;
    }
    for (auto *Node : Range) {
      if (Node != ProfiledCG.getEntryNode())
        Order.push_back(Node->Name);
    }
    ++I;
  }
  std::reverse(Order.begin(), Order.end());

  return Order;
}

bool CSPreInliner::getInlineCandidates(ProfiledCandidateQueue &CQueue,
                                       const FunctionSamples *CallerSamples) {
  assert(CallerSamples && "Expect non-null caller samples");

  // Ideally we want to consider everything a function calls, but as far as
  // context profile is concerned, only those frames that are children of
  // current one in the trie is relavent. So we walk the trie instead of call
  // targets from function profile.
  ContextTrieNode *CallerNode =
      ContextTracker.getContextNodeForProfile(CallerSamples);

  bool HasNewCandidate = false;
  for (auto &Child : CallerNode->getAllChildContext()) {
    ContextTrieNode *CalleeNode = &Child.second;
    FunctionSamples *CalleeSamples = CalleeNode->getFunctionSamples();
    if (!CalleeSamples)
      continue;

    // Call site count is more reliable, so we look up the corresponding call
    // target profile in caller's context profile to retrieve call site count.
    uint64_t CalleeEntryCount = CalleeSamples->getHeadSamplesEstimate();
    uint64_t CallsiteCount = 0;
    LineLocation Callsite = CalleeNode->getCallSiteLoc();
    if (auto CallTargets = CallerSamples->findCallTargetMapAt(Callsite)) {
      auto It = CallTargets->find(CalleeSamples->getFunction());
      if (It != CallTargets->end())
        CallsiteCount = It->second;
    }

    // TODO: call site and callee entry count should be mostly consistent, add
    // check for that.
    HasNewCandidate = true;
    uint32_t CalleeSize = getFuncSize(CalleeNode);
    CQueue.emplace(CalleeSamples, std::max(CallsiteCount, CalleeEntryCount),
                   CalleeSize);
  }

  return HasNewCandidate;
}

uint32_t CSPreInliner::getFuncSize(const ContextTrieNode *ContextNode) {
  if (UseContextCost)
    return Binary.getFuncSizeForContext(ContextNode);

  return ContextNode->getFunctionSamples()->getBodySamples().size();
}

bool CSPreInliner::shouldInline(ProfiledInlineCandidate &Candidate) {
  bool WasInlined =
      Candidate.CalleeSamples->getContext().hasAttribute(ContextWasInlined);
  // If replay inline is requested, simply follow the inline decision of the
  // profiled binary.
  if (SamplePreInlineReplay)
    return WasInlined;

  unsigned int SampleThreshold = SampleColdCallSiteThreshold;
  uint64_t ColdCountThreshold = ProfileSummaryBuilder::getColdCountThreshold(
      (Summary->getDetailedSummary()));

  if (Candidate.CallsiteCount <= ColdCountThreshold)
    SampleThreshold = SampleColdCallSiteThreshold;
  else {
    // Linearly adjust threshold based on normalized hotness, i.e, a value in
    // [0,1]. Use 10% cutoff instead of the max count as the normalization
    // upperbound for stability.
    double NormalizationUpperBound =
        ProfileSummaryBuilder::getEntryForPercentile(
            Summary->getDetailedSummary(), 100000 /* 10% */)
            .MinCount;
    double NormalizationLowerBound = ColdCountThreshold;
    double NormalizedHotness =
        (Candidate.CallsiteCount - NormalizationLowerBound) /
        (NormalizationUpperBound - NormalizationLowerBound);
    if (NormalizedHotness > 1.0)
      NormalizedHotness = 1.0;
    // Add 1 to ensure hot callsites get a non-zero threshold, which could
    // happen when SampleColdCallSiteThreshold is 0. This is when we do not
    // want any inlining for cold callsites.
    SampleThreshold = SampleHotCallSiteThreshold * NormalizedHotness * 100 +
                      SampleColdCallSiteThreshold + 1;
    // Bump up the threshold to favor previous compiler inline decision. The
    // compiler has more insight and knowledge about functions based on their IR
    // and attribures and should be able to make a more reasonable inline
    // decision.
    if (WasInlined)
      SampleThreshold *= CSPreinlMultiplierForPrevInl;
  }

  return (Candidate.SizeCost < SampleThreshold);
}

void CSPreInliner::processFunction(const FunctionId Name) {
  FunctionSamples *FSamples = ContextTracker.getBaseSamplesFor(Name);
  if (!FSamples)
    return;

  unsigned FuncSize =
      getFuncSize(ContextTracker.getContextNodeForProfile(FSamples));
  unsigned FuncFinalSize = FuncSize;
  unsigned SizeLimit = FuncSize * ProfileInlineGrowthLimit;
  SizeLimit = std::min(SizeLimit, (unsigned)ProfileInlineLimitMax);
  SizeLimit = std::max(SizeLimit, (unsigned)ProfileInlineLimitMin);

  LLVM_DEBUG(dbgs() << "Process " << Name
                    << " for context-sensitive pre-inlining (pre-inline size: "
                    << FuncSize << ", size limit: " << SizeLimit << ")\n");

  ProfiledCandidateQueue CQueue;
  getInlineCandidates(CQueue, FSamples);

  while (!CQueue.empty() && FuncFinalSize < SizeLimit) {
    ProfiledInlineCandidate Candidate = CQueue.top();
    CQueue.pop();
    bool ShouldInline = false;
    if ((ShouldInline = shouldInline(Candidate))) {
      // We mark context as inlined as the corresponding context profile
      // won't be merged into that function's base profile.
      ++PreInlNumCSInlined;
      ContextTracker.markContextSamplesInlined(Candidate.CalleeSamples);
      Candidate.CalleeSamples->getContext().setAttribute(
          ContextShouldBeInlined);
      FuncFinalSize += Candidate.SizeCost;
      getInlineCandidates(CQueue, Candidate.CalleeSamples);
    } else {
      ++PreInlNumCSNotInlined;
    }
    LLVM_DEBUG(
        dbgs() << (ShouldInline ? "  Inlined" : "  Outlined")
               << " context profile for: "
               << ContextTracker.getContextString(*Candidate.CalleeSamples)
               << " (callee size: " << Candidate.SizeCost
               << ", call count:" << Candidate.CallsiteCount << ")\n");
  }

  if (!CQueue.empty()) {
    if (SizeLimit == (unsigned)ProfileInlineLimitMax)
      ++PreInlNumCSInlinedHitMaxLimit;
    else if (SizeLimit == (unsigned)ProfileInlineLimitMin)
      ++PreInlNumCSInlinedHitMinLimit;
    else
      ++PreInlNumCSInlinedHitGrowthLimit;
  }

  LLVM_DEBUG({
    if (!CQueue.empty())
      dbgs() << "  Inline candidates ignored due to size limit (inliner "
                "original size: "
             << FuncSize << ", inliner final size: " << FuncFinalSize
             << ", size limit: " << SizeLimit << ")\n";

    while (!CQueue.empty()) {
      ProfiledInlineCandidate Candidate = CQueue.top();
      CQueue.pop();
      bool WasInlined =
          Candidate.CalleeSamples->getContext().hasAttribute(ContextWasInlined);
      dbgs() << "    "
             << ContextTracker.getContextString(*Candidate.CalleeSamples)
             << " (candidate size:" << Candidate.SizeCost
             << ", call count: " << Candidate.CallsiteCount << ", previously "
             << (WasInlined ? "inlined)\n" : "not inlined)\n");
    }
  });
}

void CSPreInliner::run() {
#ifndef NDEBUG
  auto printProfileNames = [](SampleContextTracker &ContextTracker,
                              bool IsInput) {
    uint32_t Size = 0;
    for (auto *Node : ContextTracker) {
      FunctionSamples *FSamples = Node->getFunctionSamples();
      if (FSamples) {
        Size++;
        dbgs() << "  [" << ContextTracker.getContextString(Node) << "] "
               << FSamples->getTotalSamples() << ":"
               << FSamples->getHeadSamples() << "\n";
      }
    }
    dbgs() << (IsInput ? "Input" : "Output") << " context-sensitive profiles ("
           << Size << " total):\n";
  };
#endif

  LLVM_DEBUG(printProfileNames(ContextTracker, true));

  // Execute global pre-inliner to estimate a global top-down inline
  // decision and merge profiles accordingly. This helps with profile
  // merge for ThinLTO otherwise we won't be able to merge profiles back
  // to base profile across module/thin-backend boundaries.
  // It also helps better compress context profile to control profile
  // size, as we now only need context profile for functions going to
  // be inlined.
  for (FunctionId FuncName : buildTopDownOrder()) {
    processFunction(FuncName);
  }

  // Not inlined context profiles are merged into its base, so we can
  // trim out such profiles from the output.
  for (auto *Node : ContextTracker) {
    FunctionSamples *FProfile = Node->getFunctionSamples();
    if (FProfile &&
        (Node->getParentContext() != &ContextTracker.getRootContext() &&
         !FProfile->getContext().hasState(InlinedContext))) {
      Node->setFunctionSamples(nullptr);
    }
  }
  FunctionSamples::ProfileIsPreInlined = true;

  LLVM_DEBUG(printProfileNames(ContextTracker, false));
}
