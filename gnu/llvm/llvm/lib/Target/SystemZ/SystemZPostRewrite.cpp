//==---- SystemZPostRewrite.cpp - Select pseudos after RegAlloc ---*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that is run immediately after VirtRegRewriter
// but before MachineCopyPropagation. The purpose is to lower pseudos to
// target instructions before any later pass might substitute a register for
// another.
//
//===----------------------------------------------------------------------===//

#include "SystemZ.h"
#include "SystemZInstrInfo.h"
#include "SystemZSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
using namespace llvm;

#define DEBUG_TYPE "systemz-postrewrite"
STATISTIC(MemFoldCopies, "Number of copies inserted before folded mem ops.");
STATISTIC(LOCRMuxJumps, "Number of LOCRMux jump-sequences (lower is better)");

namespace {

class SystemZPostRewrite : public MachineFunctionPass {
public:
  static char ID;
  SystemZPostRewrite() : MachineFunctionPass(ID) {
    initializeSystemZPostRewritePass(*PassRegistry::getPassRegistry());
  }

  const SystemZInstrInfo *TII;

  bool runOnMachineFunction(MachineFunction &Fn) override;

private:
  void selectLOCRMux(MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator MBBI,
                     MachineBasicBlock::iterator &NextMBBI,
                     unsigned LowOpcode,
                     unsigned HighOpcode);
  void selectSELRMux(MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator MBBI,
                     MachineBasicBlock::iterator &NextMBBI,
                     unsigned LowOpcode,
                     unsigned HighOpcode);
  bool expandCondMove(MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator MBBI,
                      MachineBasicBlock::iterator &NextMBBI);
  bool selectMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool selectMBB(MachineBasicBlock &MBB);
};

char SystemZPostRewrite::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(SystemZPostRewrite, "systemz-post-rewrite",
                "SystemZ Post Rewrite pass", false, false)

/// Returns an instance of the Post Rewrite pass.
FunctionPass *llvm::createSystemZPostRewritePass(SystemZTargetMachine &TM) {
  return new SystemZPostRewrite();
}

// MI is a load-register-on-condition pseudo instruction.  Replace it with
// LowOpcode if source and destination are both low GR32s and HighOpcode if
// source and destination are both high GR32s. Otherwise, a branch sequence
// is created.
void SystemZPostRewrite::selectLOCRMux(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       MachineBasicBlock::iterator &NextMBBI,
                                       unsigned LowOpcode,
                                       unsigned HighOpcode) {
  Register DestReg = MBBI->getOperand(0).getReg();
  Register SrcReg = MBBI->getOperand(2).getReg();
  bool DestIsHigh = SystemZ::isHighReg(DestReg);
  bool SrcIsHigh = SystemZ::isHighReg(SrcReg);

  if (!DestIsHigh && !SrcIsHigh)
    MBBI->setDesc(TII->get(LowOpcode));
  else if (DestIsHigh && SrcIsHigh)
    MBBI->setDesc(TII->get(HighOpcode));
  else
    expandCondMove(MBB, MBBI, NextMBBI);
}

// MI is a select pseudo instruction.  Replace it with LowOpcode if source
// and destination are all low GR32s and HighOpcode if source and destination
// are all high GR32s. Otherwise, a branch sequence is created.
void SystemZPostRewrite::selectSELRMux(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       MachineBasicBlock::iterator &NextMBBI,
                                       unsigned LowOpcode,
                                       unsigned HighOpcode) {
  Register DestReg = MBBI->getOperand(0).getReg();
  Register Src1Reg = MBBI->getOperand(1).getReg();
  Register Src2Reg = MBBI->getOperand(2).getReg();
  bool DestIsHigh = SystemZ::isHighReg(DestReg);
  bool Src1IsHigh = SystemZ::isHighReg(Src1Reg);
  bool Src2IsHigh = SystemZ::isHighReg(Src2Reg);

  // If sources and destination aren't all high or all low, we may be able to
  // simplify the operation by moving one of the sources to the destination
  // first.  But only if this doesn't clobber the other source.
  if (DestReg != Src1Reg && DestReg != Src2Reg) {
    if (DestIsHigh != Src1IsHigh) {
      BuildMI(*MBBI->getParent(), MBBI, MBBI->getDebugLoc(),
              TII->get(SystemZ::COPY), DestReg)
        .addReg(MBBI->getOperand(1).getReg(), getRegState(MBBI->getOperand(1)));
      MBBI->getOperand(1).setReg(DestReg);
      Src1Reg = DestReg;
      Src1IsHigh = DestIsHigh;
    } else if (DestIsHigh != Src2IsHigh) {
      BuildMI(*MBBI->getParent(), MBBI, MBBI->getDebugLoc(),
              TII->get(SystemZ::COPY), DestReg)
        .addReg(MBBI->getOperand(2).getReg(), getRegState(MBBI->getOperand(2)));
      MBBI->getOperand(2).setReg(DestReg);
      Src2Reg = DestReg;
      Src2IsHigh = DestIsHigh;
    }
  }

  // If the destination (now) matches one source, prefer this to be first.
  if (DestReg != Src1Reg && DestReg == Src2Reg) {
    TII->commuteInstruction(*MBBI, false, 1, 2);
    std::swap(Src1Reg, Src2Reg);
    std::swap(Src1IsHigh, Src2IsHigh);
  }

  if (!DestIsHigh && !Src1IsHigh && !Src2IsHigh)
    MBBI->setDesc(TII->get(LowOpcode));
  else if (DestIsHigh && Src1IsHigh && Src2IsHigh)
    MBBI->setDesc(TII->get(HighOpcode));
  else
    // Given the simplification above, we must already have a two-operand case.
    expandCondMove(MBB, MBBI, NextMBBI);
}

// Replace MBBI by a branch sequence that performs a conditional move of
// operand 2 to the destination register. Operand 1 is expected to be the
// same register as the destination.
bool SystemZPostRewrite::expandCondMove(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        MachineBasicBlock::iterator &NextMBBI) {
  MachineFunction &MF = *MBB.getParent();
  const BasicBlock *BB = MBB.getBasicBlock();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  Register DestReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(2).getReg();
  unsigned CCValid = MI.getOperand(3).getImm();
  unsigned CCMask = MI.getOperand(4).getImm();
  assert(DestReg == MI.getOperand(1).getReg() &&
         "Expected destination and first source operand to be the same.");

  LivePhysRegs LiveRegs(TII->getRegisterInfo());
  LiveRegs.addLiveOuts(MBB);
  for (auto I = std::prev(MBB.end()); I != MBBI; --I)
    LiveRegs.stepBackward(*I);

  // Splice MBB at MI, moving the rest of the block into RestMBB.
  MachineBasicBlock *RestMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(std::next(MachineFunction::iterator(MBB)), RestMBB);
  RestMBB->splice(RestMBB->begin(), &MBB, MI, MBB.end());
  RestMBB->transferSuccessors(&MBB);
  for (MCPhysReg R : LiveRegs)
    RestMBB->addLiveIn(R);

  // Create a new block MoveMBB to hold the move instruction.
  MachineBasicBlock *MoveMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(std::next(MachineFunction::iterator(MBB)), MoveMBB);
  MoveMBB->addLiveIn(SrcReg);
  for (MCPhysReg R : LiveRegs)
    MoveMBB->addLiveIn(R);

  // At the end of MBB, create a conditional branch to RestMBB if the
  // condition is false, otherwise fall through to MoveMBB.
  BuildMI(&MBB, DL, TII->get(SystemZ::BRC))
    .addImm(CCValid).addImm(CCMask ^ CCValid).addMBB(RestMBB);
  MBB.addSuccessor(RestMBB);
  MBB.addSuccessor(MoveMBB);

  // In MoveMBB, emit an instruction to move SrcReg into DestReg,
  // then fall through to RestMBB.
  BuildMI(*MoveMBB, MoveMBB->end(), DL, TII->get(SystemZ::COPY), DestReg)
      .addReg(MI.getOperand(2).getReg(), getRegState(MI.getOperand(2)));
  MoveMBB->addSuccessor(RestMBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();
  LOCRMuxJumps++;
  return true;
}

/// If MBBI references a pseudo instruction that should be selected here,
/// do it and return true.  Otherwise return false.
bool SystemZPostRewrite::selectMI(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI,
                                  MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();

  // Note: If this could be done during regalloc in foldMemoryOperandImpl()
  // while also updating the LiveIntervals, there would be no need for the
  // MemFoldPseudo to begin with.
  int TargetMemOpcode = SystemZ::getTargetMemOpcode(Opcode);
  if (TargetMemOpcode != -1) {
    MI.setDesc(TII->get(TargetMemOpcode));
    MI.tieOperands(0, 1);
    Register DstReg = MI.getOperand(0).getReg();
    MachineOperand &SrcMO = MI.getOperand(1);
    if (DstReg != SrcMO.getReg()) {
      BuildMI(MBB, &MI, MI.getDebugLoc(), TII->get(SystemZ::COPY), DstReg)
        .addReg(SrcMO.getReg());
      SrcMO.setReg(DstReg);
      MemFoldCopies++;
    }
    return true;
  }

  switch (Opcode) {
  case SystemZ::LOCRMux:
    selectLOCRMux(MBB, MBBI, NextMBBI, SystemZ::LOCR, SystemZ::LOCFHR);
    return true;
  case SystemZ::SELRMux:
    selectSELRMux(MBB, MBBI, NextMBBI, SystemZ::SELR, SystemZ::SELFHR);
    return true;
  }

  return false;
}

/// Iterate over the instructions in basic block MBB and select any
/// pseudo instructions.  Return true if anything was modified.
bool SystemZPostRewrite::selectMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= selectMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool SystemZPostRewrite::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<SystemZSubtarget>().getInstrInfo();

  bool Modified = false;
  for (auto &MBB : MF)
    Modified |= selectMBB(MBB);

  return Modified;
}

