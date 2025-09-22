//===-- MissingFrameInferrer.cpp - Missing frame inferrer --------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MissingFrameInferrer.h"
#include "PerfReader.h"
#include "ProfiledBinary.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <queue>
#include <sys/types.h>

#define DEBUG_TYPE "missing-frame-inferrer"

using namespace llvm;
using namespace sampleprof;

STATISTIC(TailCallUniReachable,
          "Number of frame pairs reachable via a unique tail call path");
STATISTIC(TailCallMultiReachable,
          "Number of frame pairs reachable via a multiple tail call paths");
STATISTIC(TailCallUnreachable,
          "Number of frame pairs unreachable via any tail call path");
STATISTIC(TailCallFuncSingleTailCalls,
          "Number of functions with single tail call site");
STATISTIC(TailCallFuncMultipleTailCalls,
          "Number of functions with multiple tail call sites");
STATISTIC(TailCallMaxTailCallPath, "Length of the longest tail call path");

static cl::opt<uint32_t>
    MaximumSearchDepth("max-search-depth", cl::init(UINT32_MAX - 1),
                       cl::desc("The maximum levels the DFS-based missing "
                                "frame search should go with"));

void MissingFrameInferrer::initialize(
    const ContextSampleCounterMap *SampleCounters) {
  // Refine call edges based on LBR samples.
  if (SampleCounters) {
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> SampledCalls;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> SampledTailCalls;

    // Populate SampledCalls based on static call sites. Similarly to
    // SampledTailCalls.
    for (const auto &CI : *SampleCounters) {
      for (auto Item : CI.second.BranchCounter) {
        auto From = Item.first.first;
        auto To = Item.first.second;
        if (CallEdges.count(From)) {
          assert(CallEdges[From].size() == 1 &&
                 "A callsite should only appear once with either a known or a "
                 "zero (unknown) target value at this point");
          SampledCalls[From].insert(To);
        }
        if (TailCallEdges.count(From)) {
          assert(TailCallEdges[From].size() == 1 &&
                 "A callsite should only appear once with either a known or a "
                 "zero (unknown) target value at this point");
          FuncRange *FromFRange = Binary->findFuncRange(From);
          FuncRange *ToFRange = Binary->findFuncRange(To);
          if (FromFRange != ToFRange)
            SampledTailCalls[From].insert(To);
        }
      }
    }

    // Replace static edges with dynamic edges.
    CallEdges = SampledCalls;
    TailCallEdges = SampledTailCalls;
  }

  // Populate function-based edges. This is to speed up address to function
  // translation.
  for (auto Call : CallEdges)
    for (auto Target : Call.second)
      if (FuncRange *ToFRange = Binary->findFuncRange(Target))
        CallEdgesF[Call.first].insert(ToFRange->Func);

  for (auto Call : TailCallEdges) {
    for (auto Target : Call.second) {
      if (FuncRange *ToFRange = Binary->findFuncRange(Target)) {
        TailCallEdgesF[Call.first].insert(ToFRange->Func);
        TailCallTargetFuncs.insert(ToFRange->Func);
      }
    }
    if (FuncRange *FromFRange = Binary->findFuncRange(Call.first))
      FuncToTailCallMap[FromFRange->Func].push_back(Call.first);
  }

#if LLVM_ENABLE_STATS
  for (auto F : FuncToTailCallMap) {
    assert(F.second.size() > 0 && "");
    if (F.second.size() > 1)
      TailCallFuncMultipleTailCalls++;
    else
      TailCallFuncSingleTailCalls++;
  }
#endif

#ifndef NDEBUG
  auto PrintCallTargets =
      [&](const std::unordered_map<uint64_t, std::unordered_set<uint64_t>>
              &CallTargets,
          bool IsTailCall) {
        for (const auto &Targets : CallTargets) {
          for (const auto &Target : Targets.second) {
            dbgs() << (IsTailCall ? "TailCall" : "Call");
            dbgs() << " From " << format("%8" PRIx64, Targets.first) << " to "
                   << format("%8" PRIx64, Target) << "\n";
          }
        }
      };

  LLVM_DEBUG(dbgs() << "============================\n ";
             dbgs() << "Call targets:\n";
             PrintCallTargets(CallEdges, false);
             dbgs() << "\nTail call targets:\n";
             PrintCallTargets(CallEdges, true);
             dbgs() << "============================\n";);
#endif
}

uint64_t MissingFrameInferrer::computeUniqueTailCallPath(
    BinaryFunction *From, BinaryFunction *To, SmallVectorImpl<uint64_t> &Path) {
  // Search for a unique path comprised of only tail call edges for a given
  // source and target frame address on the a tail call graph that consists of
  // only tail call edges. Note that only a unique path counts. Multiple paths
  // are treated unreachable.
  if (From == To)
    return 1;

  // Ignore cyclic paths. Since we are doing a recursive DFS walk, if the source
  // frame being visited is already in the stack, it means we are seeing a
  // cycle. This is done before querying the cached result because the cached
  // result may be computed based on the same path. Consider the following case:
  //     A -> B, B -> A, A -> D
  // When computing unique reachablity from A to D, the cached result for (B,D)
  // should not be counted since the unique path B->A->D is basically the same
  // path as A->D. Counting that with invalidate the uniqueness from A to D.
  if (Visiting.contains(From))
    return 0;

  // If already computed, return the cached result.
  auto I = UniquePaths.find({From, To});
  if (I != UniquePaths.end()) {
    Path.append(I->second.begin(), I->second.end());
    return 1;
  }

  auto J = NonUniquePaths.find({From, To});
  if (J != NonUniquePaths.end()) {
    return J->second;
  }

  uint64_t Pos = Path.size();

  // DFS walk each outgoing tail call edges.
  // Bail out if we are already at the the maximum searching depth.
  if (CurSearchingDepth == MaximumSearchDepth)
    return 0;


  if (!FuncToTailCallMap.count(From))
    return 0;

  CurSearchingDepth++;
  Visiting.insert(From);
  uint64_t NumPaths = 0;
  for (auto TailCall : FuncToTailCallMap[From]) {
    NumPaths += computeUniqueTailCallPath(TailCall, To, Path);
    // Stop analyzing the remaining if we are already seeing more than one
    // reachable paths.
    if (NumPaths > 1)
      break;
  }
  CurSearchingDepth--;
  Visiting.erase(From);

  // Undo already-computed path if it is not unique.
  if (NumPaths != 1) {
    Path.pop_back_n(Path.size() - Pos);
  }

  // Cache the result.
  if (NumPaths == 1) {
    UniquePaths[{From, To}].assign(Path.begin() + Pos, Path.end());
#if LLVM_ENABLE_STATS
    auto &LocalPath = UniquePaths[{From, To}];
    assert((LocalPath.size() <= MaximumSearchDepth + 1) &&
           "Path should not be longer than the maximum searching depth");
    TailCallMaxTailCallPath = std::max(uint64_t(LocalPath.size()),
                                       TailCallMaxTailCallPath.getValue());
#endif
  } else {
    NonUniquePaths[{From, To}] = NumPaths;
  }

  return NumPaths;
}

uint64_t MissingFrameInferrer::computeUniqueTailCallPath(
    uint64_t From, BinaryFunction *To, SmallVectorImpl<uint64_t> &Path) {
  if (!TailCallEdgesF.count(From))
    return 0;
  Path.push_back(From);
  uint64_t NumPaths = 0;
  for (auto Target : TailCallEdgesF[From]) {
    NumPaths += computeUniqueTailCallPath(Target, To, Path);
    // Stop analyzing the remaining if we are already seeing more than one
    // reachable paths.
    if (NumPaths > 1)
      break;
  }

  // Undo already-computed path if it is not unique.
  if (NumPaths != 1)
    Path.pop_back();
  return NumPaths;
}

bool MissingFrameInferrer::inferMissingFrames(
    uint64_t From, uint64_t To, SmallVectorImpl<uint64_t> &UniquePath) {
  assert(!TailCallEdgesF.count(From) &&
         "transition between From and To cannot be via a tailcall otherwise "
         "they would not show up at the same time");
  UniquePath.push_back(From);
  uint64_t Pos = UniquePath.size();

  FuncRange *ToFRange = Binary->findFuncRange(To);
  if (!ToFRange)
    return false;

  // Bail out if caller has no known outgoing call edges.
  if (!CallEdgesF.count(From))
    return false;

  // Done with the inference if the calle is reachable via a single callsite.
  // This may not be accurate but it improves the search throughput.
  if (llvm::is_contained(CallEdgesF[From], ToFRange->Func))
    return true;

  // Bail out if callee is not tailcall reachable at all.
  if (!TailCallTargetFuncs.contains(ToFRange->Func))
    return false;

  Visiting.clear();
  CurSearchingDepth = 0;
  uint64_t NumPaths = 0;
  for (auto Target : CallEdgesF[From]) {
    NumPaths +=
        computeUniqueTailCallPath(Target, ToFRange->Func, UniquePath);
    // Stop analyzing the remaining if we are already seeing more than one
    // reachable paths.
    if (NumPaths > 1)
      break;
  }

  // Undo already-computed path if it is not unique.
  if (NumPaths != 1) {
    UniquePath.pop_back_n(UniquePath.size() - Pos);
    assert(UniquePath.back() == From && "broken path");
  }

#if LLVM_ENABLE_STATS
  if (NumPaths == 1) {
    if (ReachableViaUniquePaths.insert({From, ToFRange->StartAddress}).second)
      TailCallUniReachable++;
  } else if (NumPaths == 0) {
    if (Unreachables.insert({From, ToFRange->StartAddress}).second) {
      TailCallUnreachable++;
      LLVM_DEBUG(dbgs() << "No path found from "
                        << format("%8" PRIx64 ":", From) << " to "
                        << format("%8" PRIx64 ":", ToFRange->StartAddress)
                        << "\n");
    }
  } else if (NumPaths > 1) {
    if (ReachableViaMultiPaths.insert({From, ToFRange->StartAddress})
            .second) {
      TailCallMultiReachable++;
      LLVM_DEBUG(dbgs() << "Multiple paths found from "
                        << format("%8" PRIx64 ":", From) << " to "
                        << format("%8" PRIx64 ":", ToFRange->StartAddress)
                        << "\n");
    }
  }
#endif

  return NumPaths == 1;
}

void MissingFrameInferrer::inferMissingFrames(
    const SmallVectorImpl<uint64_t> &Context,
    SmallVectorImpl<uint64_t> &NewContext) {
  if (Context.size() == 1) {
    NewContext = Context;
    return;
  }

  NewContext.clear();
  for (uint64_t I = 1; I < Context.size(); I++) {
    inferMissingFrames(Context[I - 1], Context[I], NewContext);
  }
  NewContext.push_back(Context.back());

  assert((NewContext.size() >= Context.size()) &&
         "Inferred context should include all frames in the original context");
  assert((NewContext.size() > Context.size() || NewContext == Context) &&
         "Inferred context should be exactly the same "
         "with the original context");
}
