//===- MLInlineAdvisor.h - ML - based InlineAdvisor factories ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MLINLINEADVISOR_H
#define LLVM_ANALYSIS_MLINLINEADVISOR_H

#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/PassManager.h"

#include <deque>
#include <map>
#include <memory>
#include <optional>

namespace llvm {
class DiagnosticInfoOptimizationBase;
class Module;
class MLInlineAdvice;

class MLInlineAdvisor : public InlineAdvisor {
public:
  MLInlineAdvisor(Module &M, ModuleAnalysisManager &MAM,
                  std::unique_ptr<MLModelRunner> ModelRunner,
                  std::function<bool(CallBase &)> GetDefaultAdvice);

  virtual ~MLInlineAdvisor() = default;

  void onPassEntry(LazyCallGraph::SCC *SCC) override;
  void onPassExit(LazyCallGraph::SCC *SCC) override;

  int64_t getIRSize(Function &F) const {
    return getCachedFPI(F).TotalInstructionCount;
  }
  void onSuccessfulInlining(const MLInlineAdvice &Advice,
                            bool CalleeWasDeleted);

  bool isForcedToStop() const { return ForceStop; }
  int64_t getLocalCalls(Function &F);
  const MLModelRunner &getModelRunner() const { return *ModelRunner; }
  FunctionPropertiesInfo &getCachedFPI(Function &) const;

protected:
  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

  std::unique_ptr<InlineAdvice> getMandatoryAdvice(CallBase &CB,
                                                   bool Advice) override;

  virtual std::unique_ptr<MLInlineAdvice> getMandatoryAdviceImpl(CallBase &CB);

  virtual std::unique_ptr<MLInlineAdvice>
  getAdviceFromModel(CallBase &CB, OptimizationRemarkEmitter &ORE);

  // Get the initial 'level' of the function, or 0 if the function has been
  // introduced afterwards.
  // TODO: should we keep this updated?
  unsigned getInitialFunctionLevel(const Function &F) const;

  std::unique_ptr<MLModelRunner> ModelRunner;
  std::function<bool(CallBase &)> GetDefaultAdvice;

private:
  int64_t getModuleIRSize() const;
  std::unique_ptr<InlineAdvice>
  getSkipAdviceIfUnreachableCallsite(CallBase &CB);
  void print(raw_ostream &OS) const override;

  // Using std::map to benefit from its iterator / reference non-invalidating
  // semantics, which make it easy to use `getCachedFPI` results from multiple
  // calls without needing to copy to avoid invalidation effects.
  mutable std::map<const Function *, FunctionPropertiesInfo> FPICache;

  LazyCallGraph &CG;

  int64_t NodeCount = 0;
  int64_t EdgeCount = 0;
  int64_t EdgesOfLastSeenNodes = 0;

  std::map<const LazyCallGraph::Node *, unsigned> FunctionLevels;
  const int32_t InitialIRSize = 0;
  int32_t CurrentIRSize = 0;
  llvm::SmallPtrSet<const LazyCallGraph::Node *, 1> NodesInLastSCC;
  DenseSet<const LazyCallGraph::Node *> AllNodes;
  DenseSet<Function *> DeadFunctions;
  bool ForceStop = false;
  ProfileSummaryInfo &PSI;
};

/// InlineAdvice that tracks changes post inlining. For that reason, it only
/// overrides the "successful inlining" extension points.
class MLInlineAdvice : public InlineAdvice {
public:
  MLInlineAdvice(MLInlineAdvisor *Advisor, CallBase &CB,
                 OptimizationRemarkEmitter &ORE, bool Recommendation);
  virtual ~MLInlineAdvice() = default;

  void recordInliningImpl() override;
  void recordInliningWithCalleeDeletedImpl() override;
  void recordUnsuccessfulInliningImpl(const InlineResult &Result) override;
  void recordUnattemptedInliningImpl() override;

  Function *getCaller() const { return Caller; }
  Function *getCallee() const { return Callee; }

  const int64_t CallerIRSize;
  const int64_t CalleeIRSize;
  const int64_t CallerAndCalleeEdges;
  void updateCachedCallerFPI(FunctionAnalysisManager &FAM) const;

private:
  void reportContextForRemark(DiagnosticInfoOptimizationBase &OR);
  MLInlineAdvisor *getAdvisor() const {
    return static_cast<MLInlineAdvisor *>(Advisor);
  };
  // Make a copy of the FPI of the caller right before inlining. If inlining
  // fails, we can just update the cache with that value.
  const FunctionPropertiesInfo PreInlineCallerFPI;
  std::optional<FunctionPropertiesUpdater> FPU;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_MLINLINEADVISOR_H
