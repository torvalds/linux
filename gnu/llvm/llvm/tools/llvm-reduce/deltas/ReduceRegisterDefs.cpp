//===- ReduceRegisterDefs.cpp - Specialized Delta Pass --------------------===//
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

#include "ReduceRegisterDefs.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

static void removeDefsFromFunction(Oracle &O, MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();

  DenseSet<MachineOperand *> KeepDefs;
  DenseSet<TargetInstrInfo::RegSubRegPair> DeleteDefs;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator It = MBB.begin(),
                                     E = MBB.getFirstTerminator();
         It != E;) {
      MachineBasicBlock::iterator InsPt = It;
      MachineInstr &MI = *It;
      ++It;

      KeepDefs.clear();
      DeleteDefs.clear();

      int NumOperands = MI.getNumOperands();
      int NumRequiredOps = MI.getNumExplicitOperands() +
                           MI.getDesc().implicit_defs().size() +
                           MI.getDesc().implicit_uses().size();

      bool HaveDelete = false;
      // Do an initial scan in case the instruction defines the same register
      // multiple times.
      for (int I = NumOperands - 1; I >= 0; --I) {
        MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || !MO.isDef())
          continue;

        TargetInstrInfo::RegSubRegPair RegPair(MO.getReg(), MO.getSubReg());
        if (!RegPair.Reg.isVirtual())
          continue;

        if (O.shouldKeep())
          KeepDefs.insert(&MO);
        else
          HaveDelete = true;
      }

      if (!HaveDelete)
        continue;

      bool HaveKeptDef = !KeepDefs.empty();
      for (int I = NumOperands - 1; I >= 0; --I) {
        MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || !MO.isDef())
          continue;

        if (KeepDefs.count(&MO))
          continue;

        TargetInstrInfo::RegSubRegPair RegPair(MO.getReg(), MO.getSubReg());
        if (!RegPair.Reg.isVirtual())
          continue;

        if (!DeleteDefs.insert(RegPair).second)
          continue;

        if (MRI.use_empty(RegPair.Reg)) {
          if (I >= NumRequiredOps) {
            // Delete implicit def operands that aren't part of the instruction
            // definition
            MI.removeOperand(I);
          }

          continue;
        }

        // If we aren't going to delete the instruction, replace it with a dead
        // def.
        if (HaveKeptDef)
          MO.setReg(MRI.cloneVirtualRegister(MO.getReg()));

        bool IsGeneric = MRI.getRegClassOrNull(RegPair.Reg) == nullptr;
        unsigned ImpDef = IsGeneric ? TargetOpcode::G_IMPLICIT_DEF
                                    : TargetOpcode::IMPLICIT_DEF;

        unsigned OpFlags = getRegState(MO) & ~RegState::Implicit;
        InsPt = BuildMI(MBB, InsPt, DebugLoc(), TII->get(ImpDef))
          .addReg(RegPair.Reg, OpFlags, RegPair.SubReg);
      }

      if (!HaveKeptDef)
        MI.eraseFromParent();
    }
  }
}

static void removeDefsFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (const Function &F : WorkItem.getModule()) {
    if (auto *MF = WorkItem.MMI->getMachineFunction(F))
      removeDefsFromFunction(O, *MF);
  }
}

void llvm::reduceRegisterDefsMIRDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, removeDefsFromModule, "Reducing register defs");
}
