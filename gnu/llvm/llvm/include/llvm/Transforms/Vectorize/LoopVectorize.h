//===- LoopVectorize.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM loop vectorizer. This pass modifies 'vectorizable' loops
// and generates target-independent LLVM-IR.
// The vectorizer uses the TargetTransformInfo analysis to estimate the costs
// of instructions in order to estimate the profitability of vectorization.
//
// The loop vectorizer combines consecutive loop iterations into a single
// 'wide' iteration. After this transformation the index is incremented
// by the SIMD vector width, and not by one.
//
// This pass has four parts:
// 1. The main loop pass that drives the different parts.
// 2. LoopVectorizationLegality - A unit that checks for the legality
//    of the vectorization.
// 3. InnerLoopVectorizer - A unit that performs the actual
//    widening of instructions.
// 4. LoopVectorizationCostModel - A unit that checks for the profitability
//    of vectorization. It decides on the optimal vector width, which
//    can be one, if vectorization is not profitable.
//
// There is a development effort going on to migrate loop vectorizer to the
// VPlan infrastructure and to introduce outer loop vectorization support (see
// docs/VectorizationPlan.rst and
// http://lists.llvm.org/pipermail/llvm-dev/2017-December/119523.html). For this
// purpose, we temporarily introduced the VPlan-native vectorization path: an
// alternative vectorization path that is natively implemented on top of the
// VPlan infrastructure. See EnableVPlanNativePath for enabling.
//
//===----------------------------------------------------------------------===//
//
// The reduction-variable vectorization is based on the paper:
//  D. Nuzman and R. Henderson. Multi-platform Auto-vectorization.
//
// Variable uniformity checks are inspired by:
//  Karrenberg, R. and Hack, S. Whole Function Vectorization.
//
// The interleaved access vectorization is based on the paper:
//  Dorit Nuzman, Ira Rosen and Ayal Zaks.  Auto-Vectorization of Interleaved
//  Data for SIMD
//
// Other ideas/concepts are from:
//  A. Zaks and D. Nuzman. Autovectorization in GCC-two years later.
//
//  S. Maleki, Y. Gao, M. Garzaran, T. Wong and D. Padua.  An Evaluation of
//  Vectorizing Compilers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H
#define LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include <functional>

namespace llvm {

class AssumptionCache;
class BlockFrequencyInfo;
class DemandedBits;
class DominatorTree;
class Function;
class Instruction;
class Loop;
class LoopAccessInfoManager;
class LoopInfo;
class OptimizationRemarkEmitter;
class ProfileSummaryInfo;
class ScalarEvolution;
class TargetLibraryInfo;
class TargetTransformInfo;

extern cl::opt<bool> EnableLoopInterleaving;
extern cl::opt<bool> EnableLoopVectorization;

/// A marker to determine if extra passes after loop vectorization should be
/// run.
struct ShouldRunExtraVectorPasses
    : public AnalysisInfoMixin<ShouldRunExtraVectorPasses> {
  static AnalysisKey Key;
  struct Result {
    bool invalidate(Function &F, const PreservedAnalyses &PA,
                    FunctionAnalysisManager::Invalidator &) {
      // Check whether the analysis has been explicitly invalidated. Otherwise,
      // it remains preserved.
      auto PAC = PA.getChecker<ShouldRunExtraVectorPasses>();
      return !PAC.preservedWhenStateless();
    }
  };

  Result run(Function &F, FunctionAnalysisManager &FAM) { return Result(); }
};

/// A pass manager to run a set of extra function simplification passes after
/// vectorization, if requested. LoopVectorize caches the
/// ShouldRunExtraVectorPasses analysis to request extra simplifications, if
/// they could be beneficial.
struct ExtraVectorPassManager : public FunctionPassManager {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    auto PA = PreservedAnalyses::all();
    if (AM.getCachedResult<ShouldRunExtraVectorPasses>(F))
      PA.intersect(FunctionPassManager::run(F, AM));
    PA.abandon<ShouldRunExtraVectorPasses>();
    return PA;
  }
};

struct LoopVectorizeOptions {
  /// If false, consider all loops for interleaving.
  /// If true, only loops that explicitly request interleaving are considered.
  bool InterleaveOnlyWhenForced;

  /// If false, consider all loops for vectorization.
  /// If true, only loops that explicitly request vectorization are considered.
  bool VectorizeOnlyWhenForced;

  /// The current defaults when creating the pass with no arguments are:
  /// EnableLoopInterleaving = true and EnableLoopVectorization = true. This
  /// means that interleaving default is consistent with the cl::opt flag, while
  /// vectorization is not.
  /// FIXME: The default for EnableLoopVectorization in the cl::opt should be
  /// set to true, and the corresponding change to account for this be made in
  /// opt.cpp. The initializations below will become:
  /// InterleaveOnlyWhenForced(!EnableLoopInterleaving)
  /// VectorizeOnlyWhenForced(!EnableLoopVectorization).
  LoopVectorizeOptions()
      : InterleaveOnlyWhenForced(false), VectorizeOnlyWhenForced(false) {}
  LoopVectorizeOptions(bool InterleaveOnlyWhenForced,
                       bool VectorizeOnlyWhenForced)
      : InterleaveOnlyWhenForced(InterleaveOnlyWhenForced),
        VectorizeOnlyWhenForced(VectorizeOnlyWhenForced) {}

  LoopVectorizeOptions &setInterleaveOnlyWhenForced(bool Value) {
    InterleaveOnlyWhenForced = Value;
    return *this;
  }

  LoopVectorizeOptions &setVectorizeOnlyWhenForced(bool Value) {
    VectorizeOnlyWhenForced = Value;
    return *this;
  }
};

/// Storage for information about made changes.
struct LoopVectorizeResult {
  bool MadeAnyChange;
  bool MadeCFGChange;

  LoopVectorizeResult(bool MadeAnyChange, bool MadeCFGChange)
      : MadeAnyChange(MadeAnyChange), MadeCFGChange(MadeCFGChange) {}
};

/// The LoopVectorize Pass.
struct LoopVectorizePass : public PassInfoMixin<LoopVectorizePass> {
private:
  /// If false, consider all loops for interleaving.
  /// If true, only loops that explicitly request interleaving are considered.
  bool InterleaveOnlyWhenForced;

  /// If false, consider all loops for vectorization.
  /// If true, only loops that explicitly request vectorization are considered.
  bool VectorizeOnlyWhenForced;

public:
  LoopVectorizePass(LoopVectorizeOptions Opts = {});

  ScalarEvolution *SE;
  LoopInfo *LI;
  TargetTransformInfo *TTI;
  DominatorTree *DT;
  BlockFrequencyInfo *BFI;
  TargetLibraryInfo *TLI;
  DemandedBits *DB;
  AssumptionCache *AC;
  LoopAccessInfoManager *LAIs;
  OptimizationRemarkEmitter *ORE;
  ProfileSummaryInfo *PSI;

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

  // Shim for old PM.
  LoopVectorizeResult runImpl(Function &F, ScalarEvolution &SE_, LoopInfo &LI_,
                              TargetTransformInfo &TTI_, DominatorTree &DT_,
                              BlockFrequencyInfo *BFI_, TargetLibraryInfo *TLI_,
                              DemandedBits &DB_, AssumptionCache &AC_,
                              LoopAccessInfoManager &LAIs_,
                              OptimizationRemarkEmitter &ORE_,
                              ProfileSummaryInfo *PSI_);

  bool processLoop(Loop *L);
};

/// Reports a vectorization failure: print \p DebugMsg for debugging
/// purposes along with the corresponding optimization remark \p RemarkName.
/// If \p I is passed, it is an instruction that prevents vectorization.
/// Otherwise, the loop \p TheLoop is used for the location of the remark.
void reportVectorizationFailure(const StringRef DebugMsg,
    const StringRef OREMsg, const StringRef ORETag,
    OptimizationRemarkEmitter *ORE, Loop *TheLoop, Instruction *I = nullptr);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H
