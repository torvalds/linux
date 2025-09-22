//===- AMDGPURegBankSelect.cpp -----------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Use MachineUniformityAnalysis as the primary basis for making SGPR vs. VGPR
// register bank selection. Use/def analysis as in the default RegBankSelect can
// be useful in narrower circumstances (e.g. choosing AGPR vs. VGPR for gfx908).
//
//===----------------------------------------------------------------------===//

#include "AMDGPURegBankSelect.h"
#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "llvm/CodeGen/MachineUniformityAnalysis.h"
#include "llvm/InitializePasses.h"

#define DEBUG_TYPE "regbankselect"

using namespace llvm;

AMDGPURegBankSelect::AMDGPURegBankSelect(Mode RunningMode)
    : RegBankSelect(AMDGPURegBankSelect::ID, RunningMode) {}

char AMDGPURegBankSelect::ID = 0;

StringRef AMDGPURegBankSelect::getPassName() const {
  return "AMDGPURegBankSelect";
}

void AMDGPURegBankSelect::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineCycleInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  // TODO: Preserve DomTree
  RegBankSelect::getAnalysisUsage(AU);
}

INITIALIZE_PASS_BEGIN(AMDGPURegBankSelect, "amdgpu-" DEBUG_TYPE,
                      "AMDGPU Register Bank Select", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineCycleInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(AMDGPURegBankSelect, "amdgpu-" DEBUG_TYPE,
                    "AMDGPU Register Bank Select", false, false)

bool AMDGPURegBankSelect::runOnMachineFunction(MachineFunction &MF) {
  // If the ISel pipeline failed, do not bother running that pass.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;

  LLVM_DEBUG(dbgs() << "Assign register banks for: " << MF.getName() << '\n');
  const Function &F = MF.getFunction();
  Mode SaveOptMode = OptMode;
  if (F.hasOptNone())
    OptMode = Mode::Fast;
  init(MF);

  assert(checkFunctionIsLegal(MF));

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  MachineCycleInfo &CycleInfo =
      getAnalysis<MachineCycleInfoWrapperPass>().getCycleInfo();
  MachineDominatorTree &DomTree =
      getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();

  MachineUniformityInfo Uniformity =
      computeMachineUniformityInfo(MF, CycleInfo, DomTree.getBase(),
                                   !ST.isSingleLaneExecution(F));
  (void)Uniformity; // TODO: Use this

  assignRegisterBanks(MF);

  OptMode = SaveOptMode;
  return false;
}
