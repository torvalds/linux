//===-- MissingFrameInferrer.h -  Missing frame inferrer ---------- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_PROFGEN_MISSINGFRAMEINFERRER_H
#define LLVM_TOOLS_LLVM_PROFGEN_MISSINGFRAMEINFERRER_H

#include "PerfReader.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include <unordered_map>
#include <unordered_set>

namespace llvm {
namespace sampleprof {

class ProfiledBinary;
struct BinaryFunction;

class MissingFrameInferrer {
public:
  MissingFrameInferrer(ProfiledBinary *Binary) : Binary(Binary) {}

  // Defininig a frame transition from a caller function to the callee function.
  using CallerCalleePair = std::pair<BinaryFunction *, BinaryFunction *>;

  void initialize(const ContextSampleCounterMap *SampleCounters);

  // Given an input `Context`, output `NewContext` with inferred missing tail
  // call frames.
  void inferMissingFrames(const SmallVectorImpl<uint64_t> &Context,
                          SmallVectorImpl<uint64_t> &NewContext);

private:
  friend class ProfiledBinary;

  // Compute a unique tail call path for a pair of source frame address and
  // target frame address. Append the unique path prefix (not including `To`) to
  // `UniquePath` if exists. Return the whether this's a unqiue tail call
  // path. The source/dest frame will typically be a pair of adjacent frame
  // entries of call stack samples.
  bool inferMissingFrames(uint64_t From, uint64_t To,
                          SmallVectorImpl<uint64_t> &UniquePath);

  // Compute a unique tail call path from the source frame address to the target
  // function. Output the unique path prefix (not including `To`) in
  // `UniquePath` if exists. Return the number of possibly availabe tail call
  // paths.
  uint64_t computeUniqueTailCallPath(uint64_t From, BinaryFunction *To,
                                     SmallVectorImpl<uint64_t> &UniquePath);

  // Compute a unique tail call path from the source function to the target
  // function. Output the unique path prefix (not including `To`) in
  // `UniquePath` if exists. Return the number of possibly availabe tail call
  // paths.
  uint64_t computeUniqueTailCallPath(BinaryFunction *From, BinaryFunction *To,
                                     SmallVectorImpl<uint64_t> &UniquePath);

  ProfiledBinary *Binary;

  // A map of call instructions to their target addresses. This is first
  // populated with static call edges but then trimmed down to dynamic call
  // edges based on LBR samples.
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>> CallEdges;

  // A map of tail call instructions to their target addresses. This is first
  // populated with static call edges but then trimmed down to dynamic call
  // edges based on LBR samples.
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>> TailCallEdges;

  // Dynamic call targets in terms of BinaryFunction for any calls.
  std::unordered_map<uint64_t, std::unordered_set<BinaryFunction *>> CallEdgesF;

  // Dynamic call targets in terms of BinaryFunction  for tail calls.
  std::unordered_map<uint64_t, std::unordered_set<BinaryFunction *>>
      TailCallEdgesF;

  // Dynamic tail call targets of caller functions.
  std::unordered_map<BinaryFunction *, std::vector<uint64_t>> FuncToTailCallMap;

  // Functions that are reachable via tail calls.
  DenseSet<const BinaryFunction *> TailCallTargetFuncs;

  struct PairHash {
    std::size_t operator()(
        const std::pair<BinaryFunction *, BinaryFunction *> &Pair) const {
      return std::hash<BinaryFunction *>()(Pair.first) ^
             std::hash<BinaryFunction *>()(Pair.second);
    }
  };

  // Cached results from a CallerCalleePair to a unique call path between them.
  std::unordered_map<CallerCalleePair, std::vector<uint64_t>, PairHash>
      UniquePaths;
  // Cached results from CallerCalleePair to the number of available call paths.
  std::unordered_map<CallerCalleePair, uint64_t, PairHash> NonUniquePaths;

  DenseSet<BinaryFunction *> Visiting;

  uint32_t CurSearchingDepth = 0;

#if LLVM_ENABLE_STATS
  DenseSet<std::pair<uint64_t, uint64_t>> ReachableViaUniquePaths;
  DenseSet<std::pair<uint64_t, uint64_t>> Unreachables;
  DenseSet<std::pair<uint64_t, uint64_t>> ReachableViaMultiPaths;
#endif
};
} // end namespace sampleprof
} // end namespace llvm

#endif
