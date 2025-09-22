//===- ReduceRegisterUses.cpp - Specialized Delta Pass --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting register uses from the MachineFunction.
//
//===----------------------------------------------------------------------===//

#include "ReduceRegisterUses.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

static void removeUsesFromFunction(Oracle &O, MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      // Generic instructions are not supposed to have undef operands.
      if (isPreISelGenericOpcode(MI.getOpcode()))
        continue;

      int NumOperands = MI.getNumOperands();
      int NumRequiredOps = MI.getNumExplicitOperands() +
                           MI.getDesc().implicit_defs().size() +
                           MI.getDesc().implicit_uses().size();

      for (int I = NumOperands - 1; I >= 0; --I) {
        MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || !MO.readsReg())
          continue;

        Register Reg = MO.getReg();
        if (Reg.isPhysical() && MRI.isReserved(Reg))
          continue;

        if (O.shouldKeep())
          continue;

        // Remove implicit operands. If the register is part of the fixed
        // operand list, set to undef.
        if (I >= NumRequiredOps)
          MI.removeOperand(I);
        else
          MO.setIsUndef();
      }
    }
  }
}

static void removeUsesFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (const Function &F : WorkItem.getModule()) {
    if (auto *MF = WorkItem.MMI->getMachineFunction(F))
      removeUsesFromFunction(O, *MF);
  }
}

void llvm::reduceRegisterUsesMIRDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, removeUsesFromModule, "Reducing register uses");
}
