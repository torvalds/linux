//===- OptimizationRemarkEmitter.h - Optimization Diagnostic ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Optimization diagnostic interfaces.  It's packaged as an analysis pass so
// that by using this service passes become dependent on BFI as well.  BFI is
// used to compute the "hotness" of the diagnostic message.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OPTIMIZATIONREMARKEMITTER_H
#define LLVM_ANALYSIS_OPTIMIZATIONREMARKEMITTER_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <optional>

namespace llvm {
class Function;
class Value;

/// The optimization diagnostic interface.
///
/// It allows reporting when optimizations are performed and when they are not
/// along with the reasons for it.  Hotness information of the corresponding
/// code region can be included in the remark if DiagnosticsHotnessRequested is
/// enabled in the LLVM context.
class OptimizationRemarkEmitter {
public:
  OptimizationRemarkEmitter(const Function *F, BlockFrequencyInfo *BFI)
      : F(F), BFI(BFI) {}

  /// This variant can be used to generate ORE on demand (without the
  /// analysis pass).
  ///
  /// Note that this ctor has a very different cost depending on whether
  /// F->getContext().getDiagnosticsHotnessRequested() is on or not.  If it's off
  /// the operation is free.
  ///
  /// Whereas if DiagnosticsHotnessRequested is on, it is fairly expensive
  /// operation since BFI and all its required analyses are computed.  This is
  /// for example useful for CGSCC passes that can't use function analyses
  /// passes in the old PM.
  OptimizationRemarkEmitter(const Function *F);

  OptimizationRemarkEmitter(OptimizationRemarkEmitter &&Arg)
      : F(Arg.F), BFI(Arg.BFI) {}

  OptimizationRemarkEmitter &operator=(OptimizationRemarkEmitter &&RHS) {
    F = RHS.F;
    BFI = RHS.BFI;
    return *this;
  }

  /// Handle invalidation events in the new pass manager.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

  /// Return true iff at least *some* remarks are enabled.
  bool enabled() const {
    return F->getContext().getLLVMRemarkStreamer() ||
           F->getContext().getDiagHandlerPtr()->isAnyRemarkEnabled();
  }

  /// Output the remark via the diagnostic handler and to the
  /// optimization record file.
  void emit(DiagnosticInfoOptimizationBase &OptDiag);

  /// Take a lambda that returns a remark which will be emitted.  Second
  /// argument is only used to restrict this to functions.
  template <typename T>
  void emit(T RemarkBuilder, decltype(RemarkBuilder()) * = nullptr) {
    // Avoid building the remark unless we know there are at least *some*
    // remarks enabled. We can't currently check whether remarks are requested
    // for the calling pass since that requires actually building the remark.

    if (enabled()) {
      auto R = RemarkBuilder();
      static_assert(
          std::is_base_of<DiagnosticInfoOptimizationBase, decltype(R)>::value,
          "the lambda passed to emit() must return a remark");
      emit((DiagnosticInfoOptimizationBase &)R);
    }
  }

  /// Whether we allow for extra compile-time budget to perform more
  /// analysis to produce fewer false positives.
  ///
  /// This is useful when reporting missed optimizations.  In this case we can
  /// use the extra analysis (1) to filter trivial false positives or (2) to
  /// provide more context so that non-trivial false positives can be quickly
  /// detected by the user.
  bool allowExtraAnalysis(StringRef PassName) const {
    return OptimizationRemarkEmitter::allowExtraAnalysis(*F, PassName);
  }
  static bool allowExtraAnalysis(const Function &F, StringRef PassName) {
    return allowExtraAnalysis(F.getContext(), PassName);
  }
  static bool allowExtraAnalysis(LLVMContext &Ctx, StringRef PassName) {
    return Ctx.getLLVMRemarkStreamer() ||
           Ctx.getDiagHandlerPtr()->isAnyRemarkEnabled(PassName);
  }

private:
  const Function *F;

  BlockFrequencyInfo *BFI;

  /// If we generate BFI on demand, we need to free it when ORE is freed.
  std::unique_ptr<BlockFrequencyInfo> OwnedBFI;

  /// Compute hotness from IR value (currently assumed to be a block) if PGO is
  /// available.
  std::optional<uint64_t> computeHotness(const Value *V);

  /// Similar but use value from \p OptDiag and update hotness there.
  void computeHotness(DiagnosticInfoIROptimization &OptDiag);

  /// Only allow verbose messages if we know we're filtering by hotness
  /// (BFI is only set in this case).
  bool shouldEmitVerbose() { return BFI != nullptr; }

  OptimizationRemarkEmitter(const OptimizationRemarkEmitter &) = delete;
  void operator=(const OptimizationRemarkEmitter &) = delete;
};

/// Add a small namespace to avoid name clashes with the classes used in
/// the streaming interface.  We want these to be short for better
/// write/readability.
namespace ore {
using NV = DiagnosticInfoOptimizationBase::Argument;
using setIsVerbose = DiagnosticInfoOptimizationBase::setIsVerbose;
using setExtraArgs = DiagnosticInfoOptimizationBase::setExtraArgs;
}

/// OptimizationRemarkEmitter legacy analysis pass
///
/// Note that this pass shouldn't generally be marked as preserved by other
/// passes.  It's holding onto BFI, so if the pass does not preserve BFI, BFI
/// could be freed.
class OptimizationRemarkEmitterWrapperPass : public FunctionPass {
  std::unique_ptr<OptimizationRemarkEmitter> ORE;

public:
  OptimizationRemarkEmitterWrapperPass();

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  OptimizationRemarkEmitter &getORE() {
    assert(ORE && "pass not run yet");
    return *ORE;
  }

  static char ID;
};

class OptimizationRemarkEmitterAnalysis
    : public AnalysisInfoMixin<OptimizationRemarkEmitterAnalysis> {
  friend AnalysisInfoMixin<OptimizationRemarkEmitterAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  typedef OptimizationRemarkEmitter Result;

  /// Run the analysis pass over a function and produce BFI.
  Result run(Function &F, FunctionAnalysisManager &AM);
};
}
#endif // LLVM_ANALYSIS_OPTIMIZATIONREMARKEMITTER_H
