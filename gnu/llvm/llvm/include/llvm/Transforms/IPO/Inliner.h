//===- Inliner.h - Inliner pass and infrastructure --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INLINER_H
#define LLVM_TRANSFORMS_IPO_INLINER_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/Utils/ImportedFunctionsInliningStatistics.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// The inliner pass for the new pass manager.
///
/// This pass wires together the inlining utilities and the inline cost
/// analysis into a CGSCC pass. It considers every call in every function in
/// the SCC and tries to inline if profitable. It can be tuned with a number of
/// parameters to control what cost model is used and what tradeoffs are made
/// when making the decision.
///
/// It should be noted that the legacy inliners do considerably more than this
/// inliner pass does. They provide logic for manually merging allocas, and
/// doing considerable DCE including the DCE of dead functions. This pass makes
/// every attempt to be simpler. DCE of functions requires complex reasoning
/// about comdat groups, etc. Instead, it is expected that other more focused
/// passes be composed to achieve the same end result.
class InlinerPass : public PassInfoMixin<InlinerPass> {
public:
  InlinerPass(bool OnlyMandatory = false,
              ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None)
      : OnlyMandatory(OnlyMandatory), LTOPhase(LTOPhase) {}
  InlinerPass(InlinerPass &&Arg) = default;

  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  InlineAdvisor &getAdvisor(const ModuleAnalysisManagerCGSCCProxy::Result &MAM,
                            FunctionAnalysisManager &FAM, Module &M);
  std::unique_ptr<InlineAdvisor> OwnedAdvisor;
  const bool OnlyMandatory;
  const ThinOrFullLTOPhase LTOPhase;
};

/// Module pass, wrapping the inliner pass. This works in conjunction with the
/// InlineAdvisorAnalysis to facilitate inlining decisions taking into account
/// module-wide state, that need to keep track of inter-inliner pass runs, for
/// a given module. An InlineAdvisor is configured and kept alive for the
/// duration of the ModuleInlinerWrapperPass::run.
class ModuleInlinerWrapperPass
    : public PassInfoMixin<ModuleInlinerWrapperPass> {
public:
  ModuleInlinerWrapperPass(
      InlineParams Params = getInlineParams(), bool MandatoryFirst = true,
      InlineContext IC = {},
      InliningAdvisorMode Mode = InliningAdvisorMode::Default,
      unsigned MaxDevirtIterations = 0);
  ModuleInlinerWrapperPass(ModuleInlinerWrapperPass &&Arg) = default;

  PreservedAnalyses run(Module &, ModuleAnalysisManager &);

  /// Allow adding more CGSCC passes, besides inlining. This should be called
  /// before run is called, as part of pass pipeline building.
  CGSCCPassManager &getPM() { return PM; }

  /// Add a module pass that runs before the CGSCC passes.
  template <class T> void addModulePass(T Pass) {
    MPM.addPass(std::move(Pass));
  }

  /// Add a module pass that runs after the CGSCC passes.
  template <class T> void addLateModulePass(T Pass) {
    AfterCGMPM.addPass(std::move(Pass));
  }

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  const InlineParams Params;
  const InlineContext IC;
  const InliningAdvisorMode Mode;
  const unsigned MaxDevirtIterations;
  // TODO: Clean this up so we only have one ModulePassManager.
  CGSCCPassManager PM;
  ModulePassManager MPM;
  ModulePassManager AfterCGMPM;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_INLINER_H
