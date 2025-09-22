//===-- SIFixVGPRCopies.cpp - Fix VGPR Copies after regalloc --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Add implicit use of exec to vector register copies.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

using namespace llvm;

#define DEBUG_TYPE "si-fix-vgpr-copies"

namespace {

class SIFixVGPRCopies : public MachineFunctionPass {
public:
  static char ID;

public:
  SIFixVGPRCopies() : MachineFunctionPass(ID) {
    initializeSIFixVGPRCopiesPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Fix VGPR copies"; }
};

} // End anonymous namespace.

INITIALIZE_PASS(SIFixVGPRCopies, DEBUG_TYPE, "SI Fix VGPR copies", false, false)

char SIFixVGPRCopies::ID = 0;

char &llvm::SIFixVGPRCopiesID = SIFixVGPRCopies::ID;

bool SIFixVGPRCopies::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      switch (MI.getOpcode()) {
      case AMDGPU::COPY:
        if (TII->isVGPRCopy(MI) && !MI.readsRegister(AMDGPU::EXEC, TRI)) {
          MI.addOperand(MF,
                        MachineOperand::CreateReg(AMDGPU::EXEC, false, true));
          LLVM_DEBUG(dbgs() << "Add exec use to " << MI);
          Changed = true;
        }
        break;
      default:
        break;
      }
    }
  }

  return Changed;
}
