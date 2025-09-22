//===---------- MachinePassManager.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the pass management machinery for machine functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/PassManagerImpl.h"

using namespace llvm;

AnalysisKey FunctionAnalysisManagerMachineFunctionProxy::Key;

namespace llvm {
template class AnalysisManager<MachineFunction>;
template class PassManager<MachineFunction>;
template class InnerAnalysisManagerProxy<MachineFunctionAnalysisManager,
                                         Module>;
template class InnerAnalysisManagerProxy<MachineFunctionAnalysisManager,
                                         Function>;
template class OuterAnalysisManagerProxy<ModuleAnalysisManager,
                                         MachineFunction>;
} // namespace llvm

bool FunctionAnalysisManagerMachineFunctionProxy::Result::invalidate(
    MachineFunction &IR, const PreservedAnalyses &PA,
    MachineFunctionAnalysisManager::Invalidator &Inv) {
  // MachineFunction passes should not invalidate Function analyses.
  // TODO: verify that PA doesn't invalidate Function analyses.
  return false;
}

template <>
bool MachineFunctionAnalysisManagerModuleProxy::Result::invalidate(
    Module &M, const PreservedAnalyses &PA,
    ModuleAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  // If this proxy isn't marked as preserved, then even if the result remains
  // valid, the key itself may no longer be valid, so we clear everything.
  //
  // Note that in order to preserve this proxy, a module pass must ensure that
  // the MFAM has been completely updated to handle the deletion of functions.
  // Specifically, any MFAM-cached results for those functions need to have been
  // forcibly cleared. When preserved, this proxy will only invalidate results
  // cached on functions *still in the module* at the end of the module pass.
  auto PAC = PA.getChecker<MachineFunctionAnalysisManagerModuleProxy>();
  if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Module>>()) {
    InnerAM->clear();
    return true;
  }

  // FIXME: be more precise, see
  // FunctionAnalysisManagerModuleProxy::Result::invalidate.
  if (!PA.allAnalysesInSetPreserved<AllAnalysesOn<MachineFunction>>()) {
    InnerAM->clear();
    return true;
  }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

template <>
bool MachineFunctionAnalysisManagerFunctionProxy::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  // If this proxy isn't marked as preserved, then even if the result remains
  // valid, the key itself may no longer be valid, so we clear everything.
  //
  // Note that in order to preserve this proxy, a module pass must ensure that
  // the MFAM has been completely updated to handle the deletion of functions.
  // Specifically, any MFAM-cached results for those functions need to have been
  // forcibly cleared. When preserved, this proxy will only invalidate results
  // cached on functions *still in the module* at the end of the module pass.
  auto PAC = PA.getChecker<MachineFunctionAnalysisManagerFunctionProxy>();
  if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Function>>()) {
    InnerAM->clear();
    return true;
  }

  // FIXME: be more precise, see
  // FunctionAnalysisManagerModuleProxy::Result::invalidate.
  if (!PA.allAnalysesInSetPreserved<AllAnalysesOn<MachineFunction>>()) {
    InnerAM->clear();
    return true;
  }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

PreservedAnalyses
FunctionToMachineFunctionPassAdaptor::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  MachineFunctionAnalysisManager &MFAM =
      FAM.getResult<MachineFunctionAnalysisManagerFunctionProxy>(F)
          .getManager();
  PassInstrumentation PI = FAM.getResult<PassInstrumentationAnalysis>(F);
  PreservedAnalyses PA = PreservedAnalyses::all();
  // Do not codegen any 'available_externally' functions at all, they have
  // definitions outside the translation unit.
  if (F.isDeclaration() || F.hasAvailableExternallyLinkage())
    return PreservedAnalyses::all();

  MachineFunction &MF = FAM.getResult<MachineFunctionAnalysis>(F).getMF();

  if (!PI.runBeforePass<MachineFunction>(*Pass, MF))
    return PreservedAnalyses::all();
  PreservedAnalyses PassPA = Pass->run(MF, MFAM);
  MFAM.invalidate(MF, PassPA);
  PI.runAfterPass(*Pass, MF, PassPA);
  PA.intersect(std::move(PassPA));

  return PA;
}

void FunctionToMachineFunctionPassAdaptor::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  OS << "machine-function(";
  Pass->printPipeline(OS, MapClassName2PassName);
  OS << ')';
}

template <>
PreservedAnalyses
PassManager<MachineFunction>::run(MachineFunction &MF,
                                  AnalysisManager<MachineFunction> &MFAM) {
  PassInstrumentation PI = MFAM.getResult<PassInstrumentationAnalysis>(MF);
  PreservedAnalyses PA = PreservedAnalyses::all();
  for (auto &Pass : Passes) {
    if (!PI.runBeforePass<MachineFunction>(*Pass, MF))
      continue;

    PreservedAnalyses PassPA = Pass->run(MF, MFAM);
    MFAM.invalidate(MF, PassPA);
    PI.runAfterPass(*Pass, MF, PassPA);
    PA.intersect(std::move(PassPA));
  }
  return PA;
}

PreservedAnalyses llvm::getMachineFunctionPassPreservedAnalyses() {
  PreservedAnalyses PA;
  // Machine function passes are not allowed to modify the LLVM
  // representation, therefore we should preserve all IR analyses.
  PA.template preserveSet<AllAnalysesOn<Module>>();
  PA.template preserveSet<AllAnalysesOn<Function>>();
  return PA;
}
