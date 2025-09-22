//===- llvm/Transforms/Utils/UnrollLoop.h - Unrolling utilities -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some loop unrolling utilities. It does not define any
// actual pass or policy, but provides a single function to perform loop
// unrolling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_UNROLLLOOP_H
#define LLVM_TRANSFORMS_UTILS_UNROLLLOOP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/InstructionCost.h"

namespace llvm {

class AssumptionCache;
class AAResults;
class BasicBlock;
class BlockFrequencyInfo;
class DependenceInfo;
class DominatorTree;
class Loop;
class LoopInfo;
class MDNode;
class ProfileSummaryInfo;
class OptimizationRemarkEmitter;
class ScalarEvolution;
class StringRef;
class Value;

using NewLoopsMap = SmallDenseMap<const Loop *, Loop *, 4>;

/// @{
/// Metadata attribute names
const char *const LLVMLoopUnrollFollowupAll = "llvm.loop.unroll.followup_all";
const char *const LLVMLoopUnrollFollowupUnrolled =
    "llvm.loop.unroll.followup_unrolled";
const char *const LLVMLoopUnrollFollowupRemainder =
    "llvm.loop.unroll.followup_remainder";
/// @}

const Loop* addClonedBlockToLoopInfo(BasicBlock *OriginalBB,
                                     BasicBlock *ClonedBB, LoopInfo *LI,
                                     NewLoopsMap &NewLoops);

/// Represents the result of a \c UnrollLoop invocation.
enum class LoopUnrollResult {
  /// The loop was not modified.
  Unmodified,

  /// The loop was partially unrolled -- we still have a loop, but with a
  /// smaller trip count.  We may also have emitted epilogue loop if the loop
  /// had a non-constant trip count.
  PartiallyUnrolled,

  /// The loop was fully unrolled into straight-line code.  We no longer have
  /// any back-edges.
  FullyUnrolled
};

struct UnrollLoopOptions {
  unsigned Count;
  bool Force;
  bool Runtime;
  bool AllowExpensiveTripCount;
  bool UnrollRemainder;
  bool ForgetAllSCEV;
  const Instruction *Heart = nullptr;
};

LoopUnrollResult UnrollLoop(Loop *L, UnrollLoopOptions ULO, LoopInfo *LI,
                            ScalarEvolution *SE, DominatorTree *DT,
                            AssumptionCache *AC,
                            const llvm::TargetTransformInfo *TTI,
                            OptimizationRemarkEmitter *ORE, bool PreserveLCSSA,
                            Loop **RemainderLoop = nullptr,
                            AAResults *AA = nullptr);

bool UnrollRuntimeLoopRemainder(
    Loop *L, unsigned Count, bool AllowExpensiveTripCount,
    bool UseEpilogRemainder, bool UnrollRemainder, bool ForgetAllSCEV,
    LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT, AssumptionCache *AC,
    const TargetTransformInfo *TTI, bool PreserveLCSSA,
    Loop **ResultLoop = nullptr);

LoopUnrollResult UnrollAndJamLoop(Loop *L, unsigned Count, unsigned TripCount,
                                  unsigned TripMultiple, bool UnrollRemainder,
                                  LoopInfo *LI, ScalarEvolution *SE,
                                  DominatorTree *DT, AssumptionCache *AC,
                                  const TargetTransformInfo *TTI,
                                  OptimizationRemarkEmitter *ORE,
                                  Loop **EpilogueLoop = nullptr);

bool isSafeToUnrollAndJam(Loop *L, ScalarEvolution &SE, DominatorTree &DT,
                          DependenceInfo &DI, LoopInfo &LI);

void simplifyLoopAfterUnroll(Loop *L, bool SimplifyIVs, LoopInfo *LI,
                             ScalarEvolution *SE, DominatorTree *DT,
                             AssumptionCache *AC,
                             const TargetTransformInfo *TTI,
                             AAResults *AA = nullptr);

MDNode *GetUnrollMetadata(MDNode *LoopID, StringRef Name);

TargetTransformInfo::UnrollingPreferences gatherUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, const TargetTransformInfo &TTI,
    BlockFrequencyInfo *BFI, ProfileSummaryInfo *PSI,
    llvm::OptimizationRemarkEmitter &ORE, int OptLevel,
    std::optional<unsigned> UserThreshold, std::optional<unsigned> UserCount,
    std::optional<bool> UserAllowPartial, std::optional<bool> UserRuntime,
    std::optional<bool> UserUpperBound,
    std::optional<unsigned> UserFullUnrollMaxCount);

/// Produce an estimate of the unrolled cost of the specified loop.  This
/// is used to a) produce a cost estimate for partial unrolling and b) to
/// cheaply estimate cost for full unrolling when we don't want to symbolically
/// evaluate all iterations.
class UnrollCostEstimator {
  InstructionCost LoopSize;
  bool NotDuplicatable;

public:
  unsigned NumInlineCandidates;
  ConvergenceKind Convergence;
  bool ConvergenceAllowsRuntime;

  UnrollCostEstimator(const Loop *L, const TargetTransformInfo &TTI,
                      const SmallPtrSetImpl<const Value *> &EphValues,
                      unsigned BEInsns);

  /// Whether it is legal to unroll this loop.
  bool canUnroll() const;

  uint64_t getRolledLoopSize() const { return *LoopSize.getValue(); }

  /// Returns loop size estimation for unrolled loop, given the unrolling
  /// configuration specified by UP.
  uint64_t
  getUnrolledLoopSize(const TargetTransformInfo::UnrollingPreferences &UP,
                      unsigned CountOverwrite = 0) const;
};

bool computeUnrollCount(Loop *L, const TargetTransformInfo &TTI,
                        DominatorTree &DT, LoopInfo *LI, AssumptionCache *AC,
                        ScalarEvolution &SE,
                        const SmallPtrSetImpl<const Value *> &EphValues,
                        OptimizationRemarkEmitter *ORE, unsigned TripCount,
                        unsigned MaxTripCount, bool MaxOrZero,
                        unsigned TripMultiple, const UnrollCostEstimator &UCE,
                        TargetTransformInfo::UnrollingPreferences &UP,
                        TargetTransformInfo::PeelingPreferences &PP,
                        bool &UseUpperBound);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_UNROLLLOOP_H
