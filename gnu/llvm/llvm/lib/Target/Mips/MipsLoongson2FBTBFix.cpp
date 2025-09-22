//===- MipsLoongson2FBTBFix.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Mips.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"

using namespace llvm;

namespace {

class MipsLoongson2FBTBFix : public MachineFunctionPass {
public:
  static char ID;

  MipsLoongson2FBTBFix() : MachineFunctionPass(ID) {
    initializeMipsLoongson2FBTBFixPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Loongson 2F BTB erratum workaround pass";
  }

private:
  bool runOnBasicBlock(MachineFunction &MF, MachineBasicBlock &MBB);
};

} // end of anonymous namespace

char MipsLoongson2FBTBFix::ID = 0;

INITIALIZE_PASS(MipsLoongson2FBTBFix, "loongson2f-btb-fix-pass",
                "Mips Loongson 2F BTB erratum workaround", false, false)

FunctionPass *llvm::createMipsLoongson2FBTBFix() {
  return new MipsLoongson2FBTBFix();
}

bool MipsLoongson2FBTBFix::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;

  for (auto &MBB : MF) {
    Changed |= runOnBasicBlock(MF, MBB);
  }
  return Changed;
}

bool MipsLoongson2FBTBFix::runOnBasicBlock(
    MachineFunction &MF, MachineBasicBlock &MBB) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (auto &MI : MBB) {
    if (!MI.isCall() && !MI.isIndirectBranch() && !MI.isReturn())
      continue;

    // Skip calls that are not through a register.
    if (MI.isCall()) {
      if (MI.getNumOperands() == 0)
        continue;
      const MachineOperand &MO = MI.getOperand(0);
      if (!MO.isReg())
        continue;
    }

    Changed = true;

    DebugLoc MBBDL = MI.getDebugLoc();
    Register TempReg = MRI.createVirtualRegister(&Mips::GPR64RegClass);

    // li $TempReg, COP_0_BTB_CLEAR | COP_0_RAS_DISABLE
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::ORi), TempReg)
        .addReg(Mips::ZERO)
        .addImm(3);
    // dmtc0 $TempReg, COP_0_DIAG
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DMTC0))
        .addReg(Mips::COP022)
        .addReg(TempReg)
        .addImm(0);
  }
  return Changed;
}
