//===-- ExpandPostRAPseudos.cpp - Pseudo instruction expansion pass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that expands COPY and SUBREG_TO_REG pseudo
// instructions after register allocation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "postrapseudos"

namespace {
struct ExpandPostRA : public MachineFunctionPass {
private:
  const TargetRegisterInfo *TRI = nullptr;
  const TargetInstrInfo *TII = nullptr;

public:
  static char ID; // Pass identification, replacement for typeid
  ExpandPostRA() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// runOnMachineFunction - pass entry point
  bool runOnMachineFunction(MachineFunction&) override;

private:
  bool LowerSubregToReg(MachineInstr *MI);
};
} // end anonymous namespace

char ExpandPostRA::ID = 0;
char &llvm::ExpandPostRAPseudosID = ExpandPostRA::ID;

INITIALIZE_PASS(ExpandPostRA, DEBUG_TYPE,
                "Post-RA pseudo instruction expansion pass", false, false)

bool ExpandPostRA::LowerSubregToReg(MachineInstr *MI) {
  MachineBasicBlock *MBB = MI->getParent();
  assert((MI->getOperand(0).isReg() && MI->getOperand(0).isDef()) &&
         MI->getOperand(1).isImm() &&
         (MI->getOperand(2).isReg() && MI->getOperand(2).isUse()) &&
          MI->getOperand(3).isImm() && "Invalid subreg_to_reg");

  Register DstReg = MI->getOperand(0).getReg();
  Register InsReg = MI->getOperand(2).getReg();
  assert(!MI->getOperand(2).getSubReg() && "SubIdx on physreg?");
  unsigned SubIdx  = MI->getOperand(3).getImm();

  assert(SubIdx != 0 && "Invalid index for insert_subreg");
  Register DstSubReg = TRI->getSubReg(DstReg, SubIdx);

  assert(DstReg.isPhysical() &&
         "Insert destination must be in a physical register");
  assert(InsReg.isPhysical() &&
         "Inserted value must be in a physical register");

  LLVM_DEBUG(dbgs() << "subreg: CONVERTING: " << *MI);

  if (MI->allDefsAreDead()) {
    MI->setDesc(TII->get(TargetOpcode::KILL));
    MI->removeOperand(3); // SubIdx
    MI->removeOperand(1); // Imm
    LLVM_DEBUG(dbgs() << "subreg: replaced by: " << *MI);
    return true;
  }

  if (DstSubReg == InsReg) {
    // No need to insert an identity copy instruction.
    // Watch out for case like this:
    // %rax = SUBREG_TO_REG 0, killed %eax, 3
    // We must leave %rax live.
    if (DstReg != InsReg) {
      MI->setDesc(TII->get(TargetOpcode::KILL));
      MI->removeOperand(3);     // SubIdx
      MI->removeOperand(1);     // Imm
      LLVM_DEBUG(dbgs() << "subreg: replace by: " << *MI);
      return true;
    }
    LLVM_DEBUG(dbgs() << "subreg: eliminated!");
  } else {
    TII->copyPhysReg(*MBB, MI, MI->getDebugLoc(), DstSubReg, InsReg,
                     MI->getOperand(2).isKill());

    // Implicitly define DstReg for subsequent uses.
    MachineBasicBlock::iterator CopyMI = MI;
    --CopyMI;
    CopyMI->addRegisterDefined(DstReg);
    LLVM_DEBUG(dbgs() << "subreg: " << *CopyMI);
  }

  LLVM_DEBUG(dbgs() << '\n');
  MBB->erase(MI);
  return true;
}

/// runOnMachineFunction - Reduce subregister inserts and extracts to register
/// copies.
///
bool ExpandPostRA::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "Machine Function\n"
                    << "********** EXPANDING POST-RA PSEUDO INSTRS **********\n"
                    << "********** Function: " << MF.getName() << '\n');
  TRI = MF.getSubtarget().getRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();

  bool MadeChange = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      // Only expand pseudos.
      if (!MI.isPseudo())
        continue;

      // Give targets a chance to expand even standard pseudos.
      if (TII->expandPostRAPseudo(MI)) {
        MadeChange = true;
        continue;
      }

      // Expand standard pseudos.
      switch (MI.getOpcode()) {
      case TargetOpcode::SUBREG_TO_REG:
        MadeChange |= LowerSubregToReg(&MI);
        break;
      case TargetOpcode::COPY:
        TII->lowerCopy(&MI, TRI);
        MadeChange = true;
        break;
      case TargetOpcode::DBG_VALUE:
        continue;
      case TargetOpcode::INSERT_SUBREG:
      case TargetOpcode::EXTRACT_SUBREG:
        llvm_unreachable("Sub-register pseudos should have been eliminated.");
      }
    }
  }

  return MadeChange;
}
