//=- SyntheticCountsPropagation.cpp - Propagate function counts --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a transformation that synthesizes entry counts for
// functions and attaches !prof metadata to functions with the synthesized
// counts. The presence of !prof metadata with counter name set to
// 'synthesized_function_entry_count' indicate that the value of the counter is
// an estimation of the likely execution count of the function. This transform
// is applied only in non PGO mode as functions get 'real' profile-based
// function entry counts in the PGO mode.
//
// The transformation works by first assigning some initial values to the entry
// counts of all functions and then doing a top-down traversal of the
// callgraph-scc to propagate the counts. For each function the set of callsites
// and their relative block frequency is gathered. The relative block frequency
// multiplied by the entry count of the caller and added to the callee's entry
// count. For non-trivial SCCs, the new counts are computed from the previous
// counts and updated in one shot.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/SyntheticCountsPropagation.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/SyntheticCountsUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using Scaled64 = ScaledNumber<uint64_t>;
using ProfileCount = Function::ProfileCount;

#define DEBUG_TYPE "synthetic-counts-propagation"

namespace llvm {
cl::opt<int>
    InitialSyntheticCount("initial-synthetic-count", cl::Hidden, cl::init(10),
                          cl::desc("Initial value of synthetic entry count"));
} // namespace llvm

/// Initial synthetic count assigned to inline functions.
static cl::opt<int> InlineSyntheticCount(
    "inline-synthetic-count", cl::Hidden, cl::init(15),
    cl::desc("Initial synthetic entry count for inline functions."));

/// Initial synthetic count assigned to cold functions.
static cl::opt<int> ColdSyntheticCount(
    "cold-synthetic-count", cl::Hidden, cl::init(5),
    cl::desc("Initial synthetic entry count for cold functions."));

// Assign initial synthetic entry counts to functions.
static void
initializeCounts(Module &M, function_ref<void(Function *, uint64_t)> SetCount) {
  auto MayHaveIndirectCalls = [](Function &F) {
    for (auto *U : F.users()) {
      if (!isa<CallInst>(U) && !isa<InvokeInst>(U))
        return true;
    }
    return false;
  };

  for (Function &F : M) {
    uint64_t InitialCount = InitialSyntheticCount;
    if (F.isDeclaration())
      continue;
    if (F.hasFnAttribute(Attribute::AlwaysInline) ||
        F.hasFnAttribute(Attribute::InlineHint)) {
      // Use a higher value for inline functions to account for the fact that
      // these are usually beneficial to inline.
      InitialCount = InlineSyntheticCount;
    } else if (F.hasLocalLinkage() && !MayHaveIndirectCalls(F)) {
      // Local functions without inline hints get counts only through
      // propagation.
      InitialCount = 0;
    } else if (F.hasFnAttribute(Attribute::Cold) ||
               F.hasFnAttribute(Attribute::NoInline)) {
      // Use a lower value for noinline and cold functions.
      InitialCount = ColdSyntheticCount;
    }
    SetCount(&F, InitialCount);
  }
}

PreservedAnalyses SyntheticCountsPropagation::run(Module &M,
                                                  ModuleAnalysisManager &MAM) {
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  DenseMap<Function *, Scaled64> Counts;
  // Set initial entry counts.
  initializeCounts(
      M, [&](Function *F, uint64_t Count) { Counts[F] = Scaled64(Count, 0); });

  // Edge includes information about the source. Hence ignore the first
  // parameter.
  auto GetCallSiteProfCount = [&](const CallGraphNode *,
                                  const CallGraphNode::CallRecord &Edge) {
    std::optional<Scaled64> Res;
    if (!Edge.first)
      return Res;
    CallBase &CB = *cast<CallBase>(*Edge.first);
    Function *Caller = CB.getCaller();
    auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(*Caller);

    // Now compute the callsite count from relative frequency and
    // entry count:
    BasicBlock *CSBB = CB.getParent();
    Scaled64 EntryFreq(BFI.getEntryFreq().getFrequency(), 0);
    Scaled64 BBCount(BFI.getBlockFreq(CSBB).getFrequency(), 0);
    BBCount /= EntryFreq;
    BBCount *= Counts[Caller];
    return std::optional<Scaled64>(BBCount);
  };

  CallGraph CG(M);
  // Propgate the entry counts on the callgraph.
  SyntheticCountsUtils<const CallGraph *>::propagate(
      &CG, GetCallSiteProfCount, [&](const CallGraphNode *N, Scaled64 New) {
        auto F = N->getFunction();
        if (!F || F->isDeclaration())
          return;

        Counts[F] += New;
      });

  // Set the counts as metadata.
  for (auto Entry : Counts) {
    Entry.first->setEntryCount(ProfileCount(
        Entry.second.template toInt<uint64_t>(), Function::PCT_Synthetic));
  }

  return PreservedAnalyses::all();
}
