//===---------------------- ProcessImplicitDefs.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "processimpdefs"

namespace {
/// Process IMPLICIT_DEF instructions and make sure there is one implicit_def
/// for each use. Add isUndef marker to implicit_def defs and their uses.
class ProcessImplicitDefs : public MachineFunctionPass {
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MachineRegisterInfo *MRI = nullptr;

  SmallSetVector<MachineInstr*, 16> WorkList;

  void processImplicitDef(MachineInstr *MI);
  bool canTurnIntoImplicitDef(MachineInstr *MI);

public:
  static char ID;

  ProcessImplicitDefs() : MachineFunctionPass(ID) {
    initializeProcessImplicitDefsPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &au) const override;

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }
};
} // end anonymous namespace

char ProcessImplicitDefs::ID = 0;
char &llvm::ProcessImplicitDefsID = ProcessImplicitDefs::ID;

INITIALIZE_PASS(ProcessImplicitDefs, DEBUG_TYPE,
                "Process Implicit Definitions", false, false)

void ProcessImplicitDefs::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addPreserved<AAResultsWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool ProcessImplicitDefs::canTurnIntoImplicitDef(MachineInstr *MI) {
  if (!MI->isCopyLike() &&
      !MI->isInsertSubreg() &&
      !MI->isRegSequence() &&
      !MI->isPHI())
    return false;
  for (const MachineOperand &MO : MI->all_uses())
    if (MO.readsReg())
      return false;
  return true;
}

void ProcessImplicitDefs::processImplicitDef(MachineInstr *MI) {
  LLVM_DEBUG(dbgs() << "Processing " << *MI);
  Register Reg = MI->getOperand(0).getReg();

  if (Reg.isVirtual()) {
    // For virtual registers, mark all uses as <undef>, and convert users to
    // implicit-def when possible.
    for (MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
      MO.setIsUndef();
      MachineInstr *UserMI = MO.getParent();
      if (!canTurnIntoImplicitDef(UserMI))
        continue;
      LLVM_DEBUG(dbgs() << "Converting to IMPLICIT_DEF: " << *UserMI);
      UserMI->setDesc(TII->get(TargetOpcode::IMPLICIT_DEF));
      WorkList.insert(UserMI);
    }
    MI->eraseFromParent();
    return;
  }

  // This is a physreg implicit-def.
  // Look for the first instruction to use or define an alias.
  MachineBasicBlock::instr_iterator UserMI = MI->getIterator();
  MachineBasicBlock::instr_iterator UserE = MI->getParent()->instr_end();
  bool Found = false;
  for (++UserMI; UserMI != UserE; ++UserMI) {
    for (MachineOperand &MO : UserMI->operands()) {
      if (!MO.isReg())
        continue;
      Register UserReg = MO.getReg();
      if (!UserReg.isPhysical() || !TRI->regsOverlap(Reg, UserReg))
        continue;
      // UserMI uses or redefines Reg. Set <undef> flags on all uses.
      Found = true;
      if (MO.isUse())
        MO.setIsUndef();
    }
    if (Found)
      break;
  }

  // If we found the using MI, we can erase the IMPLICIT_DEF.
  if (Found) {
    LLVM_DEBUG(dbgs() << "Physreg user: " << *UserMI);
    MI->eraseFromParent();
    return;
  }

  // Using instr wasn't found, it could be in another block.
  // Leave the physreg IMPLICIT_DEF, but trim any extra operands.
  for (unsigned i = MI->getNumOperands() - 1; i; --i)
    MI->removeOperand(i);
  LLVM_DEBUG(dbgs() << "Keeping physreg: " << *MI);
}

/// processImplicitDefs - Process IMPLICIT_DEF instructions and turn them into
/// <undef> operands.
bool ProcessImplicitDefs::runOnMachineFunction(MachineFunction &MF) {

  LLVM_DEBUG(dbgs() << "********** PROCESS IMPLICIT DEFS **********\n"
                    << "********** Function: " << MF.getName() << '\n');

  bool Changed = false;

  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  assert(WorkList.empty() && "Inconsistent worklist state");

  for (MachineBasicBlock &MBB : MF) {
    // Scan the basic block for implicit defs.
    for (MachineInstr &MI : MBB)
      if (MI.isImplicitDef())
        WorkList.insert(&MI);

    if (WorkList.empty())
      continue;

    LLVM_DEBUG(dbgs() << printMBBReference(MBB) << " has " << WorkList.size()
                      << " implicit defs.\n");
    Changed = true;

    // Drain the WorkList to recursively process any new implicit defs.
    do processImplicitDef(WorkList.pop_back_val());
    while (!WorkList.empty());
  }
  return Changed;
}
