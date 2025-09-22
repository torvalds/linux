//==-SummaryBasedOptimizations.cpp - Optimizations based on ThinLTO summary-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements optimizations that are based on the module summaries.
// These optimizations are performed during the thinlink phase of the
// compilation.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/SummaryBasedOptimizations.h"
#include "llvm/Analysis/SyntheticCountsUtils.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> ThinLTOSynthesizeEntryCounts(
    "thinlto-synthesize-entry-counts", cl::init(false), cl::Hidden,
    cl::desc("Synthesize entry counts based on the summary"));

namespace llvm {
extern cl::opt<int> InitialSyntheticCount;
}

static void initializeCounts(ModuleSummaryIndex &Index) {
  auto Root = Index.calculateCallGraphRoot();
  // Root is a fake node. All its successors are the actual roots of the
  // callgraph.
  // FIXME: This initializes the entry counts of only the root nodes. This makes
  // sense when compiling a binary with ThinLTO, but for libraries any of the
  // non-root nodes could be called from outside.
  for (auto &C : Root.calls()) {
    auto &V = C.first;
    for (auto &GVS : V.getSummaryList()) {
      auto S = GVS.get()->getBaseObject();
      auto *F = cast<FunctionSummary>(S);
      F->setEntryCount(InitialSyntheticCount);
    }
  }
}

void llvm::computeSyntheticCounts(ModuleSummaryIndex &Index) {
  if (!ThinLTOSynthesizeEntryCounts)
    return;

  using Scaled64 = ScaledNumber<uint64_t>;
  initializeCounts(Index);
  auto GetCallSiteRelFreq = [](FunctionSummary::EdgeTy &Edge) {
    return Scaled64(Edge.second.RelBlockFreq, -CalleeInfo::ScaleShift);
  };
  auto GetEntryCount = [](ValueInfo V) {
    if (V.getSummaryList().size()) {
      auto S = V.getSummaryList().front()->getBaseObject();
      auto *F = cast<FunctionSummary>(S);
      return F->entryCount();
    } else {
      return UINT64_C(0);
    }
  };
  auto AddToEntryCount = [](ValueInfo V, Scaled64 New) {
    if (!V.getSummaryList().size())
      return;
    for (auto &GVS : V.getSummaryList()) {
      auto S = GVS.get()->getBaseObject();
      auto *F = cast<FunctionSummary>(S);
      F->setEntryCount(
          SaturatingAdd(F->entryCount(), New.template toInt<uint64_t>()));
    }
  };

  auto GetProfileCount = [&](ValueInfo V, FunctionSummary::EdgeTy &Edge) {
    auto RelFreq = GetCallSiteRelFreq(Edge);
    Scaled64 EC(GetEntryCount(V), 0);
    return RelFreq * EC;
  };
  // After initializing the counts in initializeCounts above, the counts have to
  // be propagated across the combined callgraph.
  // SyntheticCountsUtils::propagate takes care of this propagation on any
  // callgraph that specialized GraphTraits.
  SyntheticCountsUtils<ModuleSummaryIndex *>::propagate(&Index, GetProfileCount,
                                                        AddToEntryCount);
  Index.setHasSyntheticEntryCounts();
}
