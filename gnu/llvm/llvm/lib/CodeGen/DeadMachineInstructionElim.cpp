//===- DeadMachineInstructionElim.cpp - Remove dead machine instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is an extremely simple MachineInstr-level dead-code-elimination pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/DeadMachineInstructionElim.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "dead-mi-elimination"

STATISTIC(NumDeletes,          "Number of dead instructions deleted");

namespace {
class DeadMachineInstructionElimImpl {
  const MachineRegisterInfo *MRI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  LiveRegUnits LivePhysRegs;

public:
  bool runImpl(MachineFunction &MF);

private:
  bool isDead(const MachineInstr *MI) const;
  bool eliminateDeadMI(MachineFunction &MF);
};

class DeadMachineInstructionElim : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  DeadMachineInstructionElim() : MachineFunctionPass(ID) {
    initializeDeadMachineInstructionElimPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;
    return DeadMachineInstructionElimImpl().runImpl(MF);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // namespace

PreservedAnalyses
DeadMachineInstructionElimPass::run(MachineFunction &MF,
                                    MachineFunctionAnalysisManager &) {
  if (!DeadMachineInstructionElimImpl().runImpl(MF))
    return PreservedAnalyses::all();
  PreservedAnalyses PA = getMachineFunctionPassPreservedAnalyses();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

char DeadMachineInstructionElim::ID = 0;
char &llvm::DeadMachineInstructionElimID = DeadMachineInstructionElim::ID;

INITIALIZE_PASS(DeadMachineInstructionElim, DEBUG_TYPE,
                "Remove dead machine instructions", false, false)

bool DeadMachineInstructionElimImpl::isDead(const MachineInstr *MI) const {
  // Technically speaking inline asm without side effects and no defs can still
  // be deleted. But there is so much bad inline asm code out there, we should
  // let them be.
  if (MI->isInlineAsm())
    return false;

  // Don't delete frame allocation labels.
  if (MI->getOpcode() == TargetOpcode::LOCAL_ESCAPE)
    return false;

  // Don't delete instructions with side effects.
  bool SawStore = false;
  if (!MI->isSafeToMove(nullptr, SawStore) && !MI->isPHI())
    return false;

  // Examine each operand.
  for (const MachineOperand &MO : MI->all_defs()) {
    Register Reg = MO.getReg();
    if (Reg.isPhysical()) {
      // Don't delete live physreg defs, or any reserved register defs.
      if (!LivePhysRegs.available(Reg) || MRI->isReserved(Reg))
        return false;
    } else {
      if (MO.isDead()) {
#ifndef NDEBUG
        // Basic check on the register. All of them should be 'undef'.
        for (auto &U : MRI->use_nodbg_operands(Reg))
          assert(U.isUndef() && "'Undef' use on a 'dead' register is found!");
#endif
        continue;
      }
      for (const MachineInstr &Use : MRI->use_nodbg_instructions(Reg)) {
        if (&Use != MI)
          // This def has a non-debug use. Don't delete the instruction!
          return false;
      }
    }
  }

  // If there are no defs with uses, the instruction is dead.
  return true;
}

bool DeadMachineInstructionElimImpl::runImpl(MachineFunction &MF) {
  MRI = &MF.getRegInfo();

  const TargetSubtargetInfo &ST = MF.getSubtarget();
  TII = ST.getInstrInfo();
  LivePhysRegs.init(*ST.getRegisterInfo());

  bool AnyChanges = eliminateDeadMI(MF);
  while (AnyChanges && eliminateDeadMI(MF))
    ;
  return AnyChanges;
}

bool DeadMachineInstructionElimImpl::eliminateDeadMI(MachineFunction &MF) {
  bool AnyChanges = false;

  // Loop over all instructions in all blocks, from bottom to top, so that it's
  // more likely that chains of dependent but ultimately dead instructions will
  // be cleaned up.
  for (MachineBasicBlock *MBB : post_order(&MF)) {
    LivePhysRegs.addLiveOuts(*MBB);

    // Now scan the instructions and delete dead ones, tracking physreg
    // liveness as we go.
    for (MachineInstr &MI : make_early_inc_range(reverse(*MBB))) {
      // If the instruction is dead, delete it!
      if (isDead(&MI)) {
        LLVM_DEBUG(dbgs() << "DeadMachineInstructionElim: DELETING: " << MI);
        // It is possible that some DBG_VALUE instructions refer to this
        // instruction. They will be deleted in the live debug variable
        // analysis.
        MI.eraseFromParent();
        AnyChanges = true;
        ++NumDeletes;
        continue;
      }

      LivePhysRegs.stepBackward(MI);
    }
  }

  LivePhysRegs.clear();
  return AnyChanges;
}
