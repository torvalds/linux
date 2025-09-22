//===- MachineDominanceFrontier.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"

using namespace llvm;

namespace llvm {
template class DominanceFrontierBase<MachineBasicBlock, false>;
template class DominanceFrontierBase<MachineBasicBlock, true>;
template class ForwardDominanceFrontierBase<MachineBasicBlock>;
}


char MachineDominanceFrontier::ID = 0;

INITIALIZE_PASS_BEGIN(MachineDominanceFrontier, "machine-domfrontier",
                "Machine Dominance Frontier Construction", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(MachineDominanceFrontier, "machine-domfrontier",
                "Machine Dominance Frontier Construction", true, true)

MachineDominanceFrontier::MachineDominanceFrontier() : MachineFunctionPass(ID) {
  initializeMachineDominanceFrontierPass(*PassRegistry::getPassRegistry());
}

char &llvm::MachineDominanceFrontierID = MachineDominanceFrontier::ID;

bool MachineDominanceFrontier::runOnMachineFunction(MachineFunction &) {
  releaseMemory();
  Base.analyze(
      getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree().getBase());
  return false;
}

void MachineDominanceFrontier::releaseMemory() {
  Base.releaseMemory();
}

void MachineDominanceFrontier::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}
