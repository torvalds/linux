//===- ModuleInliner.h - Module level Inliner pass --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_MODULEINLINER_H
#define LLVM_TRANSFORMS_IPO_MODULEINLINER_H

#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// The module inliner pass for the new pass manager.
///
/// This pass wires together the inlining utilities and the inline cost
/// analysis into a module pass. Different from SCC inliner, it considers every
/// call in every function in the whole module and tries to inline if
/// profitable. With this module level inliner, it is possible to evaluate more
/// heuristics in the module level such like PriorityInlineOrder. It can be
/// tuned with a number of parameters to control what cost model is used and
/// what tradeoffs are made when making the decision.
class ModuleInlinerPass : public PassInfoMixin<ModuleInlinerPass> {
public:
  ModuleInlinerPass(InlineParams Params = getInlineParams(),
                    InliningAdvisorMode Mode = InliningAdvisorMode::Default,
                    ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None)
      : Params(Params), Mode(Mode), LTOPhase(LTOPhase){};
  ModuleInlinerPass(ModuleInlinerPass &&Arg) = default;

  PreservedAnalyses run(Module &, ModuleAnalysisManager &);

private:
  InlineAdvisor &getAdvisor(const ModuleAnalysisManager &MAM,
                            FunctionAnalysisManager &FAM, Module &M);
  std::unique_ptr<InlineAdvisor> OwnedAdvisor;
  const InlineParams Params;
  const InliningAdvisorMode Mode;
  const ThinOrFullLTOPhase LTOPhase;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_MODULEINLINER_H
