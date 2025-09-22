//===- ReduceInstructionsMIR.cpp - Specialized Delta Pass -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting MachineInstr from the MachineFunction.
//
//===----------------------------------------------------------------------===//

#include "ReduceInstructionsMIR.h"
#include "Delta.h"

#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

static Register getPrevDefOfRCInMBB(MachineBasicBlock &MBB,
                                    MachineBasicBlock::reverse_iterator &RI,
                                    const RegClassOrRegBank &RC, LLT Ty,
                                    SetVector<MachineInstr *> &ExcludeMIs) {
  auto MRI = &MBB.getParent()->getRegInfo();
  for (MachineBasicBlock::reverse_instr_iterator E = MBB.instr_rend(); RI != E;
       ++RI) {
    auto &MI = *RI;
    // All Def operands explicit and implicit.
    for (auto &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isDef() || MO.isDead())
        continue;
      auto Reg = MO.getReg();
      if (Reg.isPhysical())
        continue;

      if (MRI->getRegClassOrRegBank(Reg) == RC && MRI->getType(Reg) == Ty &&
          !ExcludeMIs.count(MO.getParent()))
        return Reg;
    }
  }
  return 0;
}

static bool shouldNotRemoveInstruction(const TargetInstrInfo &TII,
                                       const MachineInstr &MI) {
  if (MI.isTerminator())
    return true;

  // The MIR is almost certainly going to be invalid if frame instructions are
  // deleted individually since they need to come in balanced pairs, so don't
  // try to delete them.
  if (MI.getOpcode() == TII.getCallFrameSetupOpcode() ||
      MI.getOpcode() == TII.getCallFrameDestroyOpcode())
    return true;

  return false;
}

static void extractInstrFromFunction(Oracle &O, MachineFunction &MF) {
  MachineDominatorTree MDT;
  MDT.calculate(MF);

  auto MRI = &MF.getRegInfo();
  SetVector<MachineInstr *> ToDelete;

  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  MachineBasicBlock *EntryMBB = &*MF.begin();
  MachineBasicBlock::iterator EntryInsPt =
      EntryMBB->SkipPHIsLabelsAndDebug(EntryMBB->begin());

  // Mark MIs for deletion according to some criteria.
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (shouldNotRemoveInstruction(*TII, MI))
        continue;
      if (!O.shouldKeep())
        ToDelete.insert(&MI);
    }
  }

  // For each MI to be deleted update users of regs defined by that MI to use
  // some other dominating definition (that is not to be deleted).
  for (auto *MI : ToDelete) {
    for (auto &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef() || MO.isDead())
        continue;
      auto Reg = MO.getReg();
      if (Reg.isPhysical())
        continue;
      auto UI = MRI->use_begin(Reg);
      auto UE = MRI->use_end();

      const auto &RegRC = MRI->getRegClassOrRegBank(Reg);
      LLT RegTy = MRI->getType(Reg);

      Register NewReg = 0;
      // If this is not a physical register and there are some uses.
      if (UI != UE) {
        MachineBasicBlock::reverse_iterator RI(*MI);
        MachineBasicBlock *BB = MI->getParent();
        ++RI;

        if (MDT.isReachableFromEntry(BB)) {
          while (NewReg == 0 && BB) {
            NewReg = getPrevDefOfRCInMBB(*BB, RI, RegRC, RegTy, ToDelete);
            // Prepare for idom(BB).
            if (auto *IDM = MDT.getNode(BB)->getIDom()) {
              BB = IDM->getBlock();
              RI = BB->rbegin();
            } else {
              BB = nullptr;
            }
          }
        }
      }

      // If no dominating definition was found then add an implicit def to the
      // top of the entry block.
      if (!NewReg) {
        NewReg = MRI->cloneVirtualRegister(Reg);
        bool IsGeneric = MRI->getRegClassOrNull(Reg) == nullptr;
        unsigned ImpDef = IsGeneric ? TargetOpcode::G_IMPLICIT_DEF
                                    : TargetOpcode::IMPLICIT_DEF;

        unsigned State = getRegState(MO);
        if (MO.getSubReg())
          State |= RegState::Undef;

        BuildMI(*EntryMBB, EntryInsPt, DebugLoc(), TII->get(ImpDef))
          .addReg(NewReg, State, MO.getSubReg());
      }

      // Update all uses.
      while (UI != UE) {
        auto &UMO = *UI++;
        UMO.setReg(NewReg);
      }
    }
  }

  // Finally delete the MIs.
  for (auto *MI : ToDelete)
    MI->eraseFromParent();
}

static void extractInstrFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (const Function &F : WorkItem.getModule()) {
    if (MachineFunction *MF = WorkItem.MMI->getMachineFunction(F))
      extractInstrFromFunction(O, *MF);
  }
}

void llvm::reduceInstructionsMIRDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractInstrFromModule, "Reducing Instructions");
}
