//===- LoopAnalysisManager.h - Loop analysis management ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header provides classes for managing per-loop analyses. These are
/// typically used as part of a loop pass pipeline over the loop nests of
/// a function.
///
/// Loop analyses are allowed to make some simplifying assumptions:
/// 1) Loops are, where possible, in simplified form.
/// 2) Loops are *always* in LCSSA form.
/// 3) A collection of analysis results are available:
///    - LoopInfo
///    - DominatorTree
///    - ScalarEvolution
///    - AAManager
///
/// The primary mechanism to provide these invariants is the loop pass manager,
/// but they can also be manually provided in order to reason about a loop from
/// outside of a dedicated pass manager.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPANALYSISMANAGER_H
#define LLVM_ANALYSIS_LOOPANALYSISMANAGER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class AAResults;
class AssumptionCache;
class BlockFrequencyInfo;
class BranchProbabilityInfo;
class DominatorTree;
class Function;
class Loop;
class LoopInfo;
class MemorySSA;
class ScalarEvolution;
class TargetLibraryInfo;
class TargetTransformInfo;

/// The adaptor from a function pass to a loop pass computes these analyses and
/// makes them available to the loop passes "for free". Each loop pass is
/// expected to update these analyses if necessary to ensure they're
/// valid after it runs.
struct LoopStandardAnalysisResults {
  AAResults &AA;
  AssumptionCache &AC;
  DominatorTree &DT;
  LoopInfo &LI;
  ScalarEvolution &SE;
  TargetLibraryInfo &TLI;
  TargetTransformInfo &TTI;
  BlockFrequencyInfo *BFI;
  BranchProbabilityInfo *BPI;
  MemorySSA *MSSA;
};

/// Extern template declaration for the analysis set for this IR unit.
extern template class AllAnalysesOn<Loop>;

extern template class AnalysisManager<Loop, LoopStandardAnalysisResults &>;
/// The loop analysis manager.
///
/// See the documentation for the AnalysisManager template for detail
/// documentation. This typedef serves as a convenient way to refer to this
/// construct in the adaptors and proxies used to integrate this into the larger
/// pass manager infrastructure.
typedef AnalysisManager<Loop, LoopStandardAnalysisResults &>
    LoopAnalysisManager;

/// A proxy from a \c LoopAnalysisManager to a \c Function.
typedef InnerAnalysisManagerProxy<LoopAnalysisManager, Function>
    LoopAnalysisManagerFunctionProxy;

/// A specialized result for the \c LoopAnalysisManagerFunctionProxy which
/// retains a \c LoopInfo reference.
///
/// This allows it to collect loop objects for which analysis results may be
/// cached in the \c LoopAnalysisManager.
template <> class LoopAnalysisManagerFunctionProxy::Result {
public:
  explicit Result(LoopAnalysisManager &InnerAM, LoopInfo &LI)
      : InnerAM(&InnerAM), LI(&LI) {}
  Result(Result &&Arg)
      : InnerAM(std::move(Arg.InnerAM)), LI(Arg.LI), MSSAUsed(Arg.MSSAUsed) {
    // We have to null out the analysis manager in the moved-from state
    // because we are taking ownership of the responsibilty to clear the
    // analysis state.
    Arg.InnerAM = nullptr;
  }
  Result &operator=(Result &&RHS) {
    InnerAM = RHS.InnerAM;
    LI = RHS.LI;
    MSSAUsed = RHS.MSSAUsed;
    // We have to null out the analysis manager in the moved-from state
    // because we are taking ownership of the responsibilty to clear the
    // analysis state.
    RHS.InnerAM = nullptr;
    return *this;
  }
  ~Result() {
    // InnerAM is cleared in a moved from state where there is nothing to do.
    if (!InnerAM)
      return;

    // Clear out the analysis manager if we're being destroyed -- it means we
    // didn't even see an invalidate call when we got invalidated.
    InnerAM->clear();
  }

  /// Mark MemorySSA as used so we can invalidate self if MSSA is invalidated.
  void markMSSAUsed() { MSSAUsed = true; }

  /// Accessor for the analysis manager.
  LoopAnalysisManager &getManager() { return *InnerAM; }

  /// Handler for invalidation of the proxy for a particular function.
  ///
  /// If the proxy, \c LoopInfo, and associated analyses are preserved, this
  /// will merely forward the invalidation event to any cached loop analysis
  /// results for loops within this function.
  ///
  /// If the necessary loop infrastructure is not preserved, this will forcibly
  /// clear all of the cached analysis results that are keyed on the \c
  /// LoopInfo for this function.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

private:
  LoopAnalysisManager *InnerAM;
  LoopInfo *LI;
  bool MSSAUsed = false;
};

/// Provide a specialized run method for the \c LoopAnalysisManagerFunctionProxy
/// so it can pass the \c LoopInfo to the result.
template <>
LoopAnalysisManagerFunctionProxy::Result
LoopAnalysisManagerFunctionProxy::run(Function &F, FunctionAnalysisManager &AM);

// Ensure the \c LoopAnalysisManagerFunctionProxy is provided as an extern
// template.
extern template class InnerAnalysisManagerProxy<LoopAnalysisManager, Function>;

extern template class OuterAnalysisManagerProxy<FunctionAnalysisManager, Loop,
                                                LoopStandardAnalysisResults &>;
/// A proxy from a \c FunctionAnalysisManager to a \c Loop.
typedef OuterAnalysisManagerProxy<FunctionAnalysisManager, Loop,
                                  LoopStandardAnalysisResults &>
    FunctionAnalysisManagerLoopProxy;

/// Returns the minimum set of Analyses that all loop passes must preserve.
PreservedAnalyses getLoopPassPreservedAnalyses();
}

#endif // LLVM_ANALYSIS_LOOPANALYSISMANAGER_H
