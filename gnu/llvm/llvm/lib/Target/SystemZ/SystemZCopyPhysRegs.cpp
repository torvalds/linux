//===---------- SystemZPhysRegCopy.cpp - Handle phys reg copies -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass makes sure that a COPY of a physical register will be
// implementable after register allocation in copyPhysReg() (this could be
// done in EmitInstrWithCustomInserter() instead if COPY instructions would
// be passed to it).
//
//===----------------------------------------------------------------------===//

#include "SystemZMachineFunctionInfo.h"
#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

class SystemZCopyPhysRegs : public MachineFunctionPass {
public:
  static char ID;
  SystemZCopyPhysRegs()
    : MachineFunctionPass(ID), TII(nullptr), MRI(nullptr) {
    initializeSystemZCopyPhysRegsPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:

  bool visitMBB(MachineBasicBlock &MBB);

  const SystemZInstrInfo *TII;
  MachineRegisterInfo *MRI;
};

char SystemZCopyPhysRegs::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(SystemZCopyPhysRegs, "systemz-copy-physregs",
                "SystemZ Copy Physregs", false, false)

FunctionPass *llvm::createSystemZCopyPhysRegsPass(SystemZTargetMachine &TM) {
  return new SystemZCopyPhysRegs();
}

void SystemZCopyPhysRegs::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool SystemZCopyPhysRegs::visitMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  // Certain special registers can only be copied from a subset of the
  // default register class of the type. It is therefore necessary to create
  // the target copy instructions before regalloc instead of in copyPhysReg().
  for (MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
       MBBI != E; ) {
    MachineInstr *MI = &*MBBI++;
    if (!MI->isCopy())
      continue;

    DebugLoc DL = MI->getDebugLoc();
    Register SrcReg = MI->getOperand(1).getReg();
    Register DstReg = MI->getOperand(0).getReg();
    if (DstReg.isVirtual() &&
        (SrcReg == SystemZ::CC || SystemZ::AR32BitRegClass.contains(SrcReg))) {
      Register Tmp = MRI->createVirtualRegister(&SystemZ::GR32BitRegClass);
      if (SrcReg == SystemZ::CC)
        BuildMI(MBB, MI, DL, TII->get(SystemZ::IPM), Tmp);
      else
        BuildMI(MBB, MI, DL, TII->get(SystemZ::EAR), Tmp).addReg(SrcReg);
      MI->getOperand(1).setReg(Tmp);
      Modified = true;
    }
    else if (SrcReg.isVirtual() &&
             SystemZ::AR32BitRegClass.contains(DstReg)) {
      Register Tmp = MRI->createVirtualRegister(&SystemZ::GR32BitRegClass);
      MI->getOperand(0).setReg(Tmp);
      BuildMI(MBB, MBBI, DL, TII->get(SystemZ::SAR), DstReg).addReg(Tmp);
      Modified = true;
    }
  }

  return Modified;
}

bool SystemZCopyPhysRegs::runOnMachineFunction(MachineFunction &F) {
  TII = F.getSubtarget<SystemZSubtarget>().getInstrInfo();
  MRI = &F.getRegInfo();

  bool Modified = false;
  for (auto &MBB : F)
    Modified |= visitMBB(MBB);

  return Modified;
}

