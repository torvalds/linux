//===- NVPTXInstrInfo.cpp - NVPTX Instruction Information -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the NVPTX implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "NVPTXInstrInfo.h"
#include "NVPTX.h"
#include "NVPTXTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "NVPTXGenInstrInfo.inc"

// Pin the vtable to this file.
void NVPTXInstrInfo::anchor() {}

NVPTXInstrInfo::NVPTXInstrInfo() : RegInfo() {}

void NVPTXInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, MCRegister DestReg,
                                 MCRegister SrcReg, bool KillSrc) const {
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *DestRC = MRI.getRegClass(DestReg);
  const TargetRegisterClass *SrcRC = MRI.getRegClass(SrcReg);

  if (RegInfo.getRegSizeInBits(*DestRC) != RegInfo.getRegSizeInBits(*SrcRC))
    report_fatal_error("Copy one register into another with a different width");

  unsigned Op;
  if (DestRC == &NVPTX::Int1RegsRegClass) {
    Op = NVPTX::IMOV1rr;
  } else if (DestRC == &NVPTX::Int16RegsRegClass) {
    Op = NVPTX::IMOV16rr;
  } else if (DestRC == &NVPTX::Int32RegsRegClass) {
    Op = (SrcRC == &NVPTX::Int32RegsRegClass ? NVPTX::IMOV32rr
                                             : NVPTX::BITCONVERT_32_F2I);
  } else if (DestRC == &NVPTX::Int64RegsRegClass) {
    Op = (SrcRC == &NVPTX::Int64RegsRegClass ? NVPTX::IMOV64rr
                                             : NVPTX::BITCONVERT_64_F2I);
  } else if (DestRC == &NVPTX::Int128RegsRegClass) {
    Op = NVPTX::IMOV128rr;
  } else if (DestRC == &NVPTX::Float32RegsRegClass) {
    Op = (SrcRC == &NVPTX::Float32RegsRegClass ? NVPTX::FMOV32rr
                                               : NVPTX::BITCONVERT_32_I2F);
  } else if (DestRC == &NVPTX::Float64RegsRegClass) {
    Op = (SrcRC == &NVPTX::Float64RegsRegClass ? NVPTX::FMOV64rr
                                               : NVPTX::BITCONVERT_64_I2F);
  } else {
    llvm_unreachable("Bad register copy");
  }
  BuildMI(MBB, I, DL, get(Op), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

/// analyzeBranch - Analyze the branching code at the end of MBB, returning
/// true if it cannot be understood (e.g. it's a switch dispatch or isn't
/// implemented for a target).  Upon success, this returns false and returns
/// with the following information in various cases:
///
/// 1. If this block ends with no branches (it just falls through to its succ)
///    just return false, leaving TBB/FBB null.
/// 2. If this block ends with only an unconditional branch, it sets TBB to be
///    the destination block.
/// 3. If this block ends with an conditional branch and it falls through to
///    an successor block, it sets TBB to be the branch destination block and a
///    list of operands that evaluate the condition. These
///    operands can be passed to other TargetInstrInfo methods to create new
///    branches.
/// 4. If this block ends with an conditional branch and an unconditional
///    block, it returns the 'true' destination in TBB, the 'false' destination
///    in FBB, and a list of operands that evaluate the condition. These
///    operands can be passed to other TargetInstrInfo methods to create new
///    branches.
///
/// Note that removeBranch and insertBranch must be implemented to support
/// cases where this method returns success.
///
bool NVPTXInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::iterator I = MBB.end();
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I))
    return false;

  // Get the last instruction in the block.
  MachineInstr &LastInst = *I;

  // If there is only one terminator instruction, process it.
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (LastInst.getOpcode() == NVPTX::GOTO) {
      TBB = LastInst.getOperand(0).getMBB();
      return false;
    } else if (LastInst.getOpcode() == NVPTX::CBranch) {
      // Block ends with fall-through condbranch.
      TBB = LastInst.getOperand(1).getMBB();
      Cond.push_back(LastInst.getOperand(0));
      return false;
    }
    // Otherwise, don't know what this is.
    return true;
  }

  // Get the instruction before it if it's a terminator.
  MachineInstr &SecondLastInst = *I;

  // If there are three terminators, we don't know what sort of block this is.
  if (I != MBB.begin() && isUnpredicatedTerminator(*--I))
    return true;

  // If the block ends with NVPTX::GOTO and NVPTX:CBranch, handle it.
  if (SecondLastInst.getOpcode() == NVPTX::CBranch &&
      LastInst.getOpcode() == NVPTX::GOTO) {
    TBB = SecondLastInst.getOperand(1).getMBB();
    Cond.push_back(SecondLastInst.getOperand(0));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  }

  // If the block ends with two NVPTX:GOTOs, handle it.  The second one is not
  // executed, so remove it.
  if (SecondLastInst.getOpcode() == NVPTX::GOTO &&
      LastInst.getOpcode() == NVPTX::GOTO) {
    TBB = SecondLastInst.getOperand(0).getMBB();
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return false;
  }

  // Otherwise, can't handle this.
  return true;
}

unsigned NVPTXInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");
  MachineBasicBlock::iterator I = MBB.end();
  if (I == MBB.begin())
    return 0;
  --I;
  if (I->getOpcode() != NVPTX::GOTO && I->getOpcode() != NVPTX::CBranch)
    return 0;

  // Remove the branch.
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin())
    return 1;
  --I;
  if (I->getOpcode() != NVPTX::CBranch)
    return 1;

  // Remove the branch.
  I->eraseFromParent();
  return 2;
}

unsigned NVPTXInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.size() == 0) &&
         "NVPTX branch conditions have two components!");

  // One-way branch.
  if (!FBB) {
    if (Cond.empty()) // Unconditional branch
      BuildMI(&MBB, DL, get(NVPTX::GOTO)).addMBB(TBB);
    else // Conditional branch
      BuildMI(&MBB, DL, get(NVPTX::CBranch)).add(Cond[0]).addMBB(TBB);
    return 1;
  }

  // Two-way Conditional Branch.
  BuildMI(&MBB, DL, get(NVPTX::CBranch)).add(Cond[0]).addMBB(TBB);
  BuildMI(&MBB, DL, get(NVPTX::GOTO)).addMBB(FBB);
  return 2;
}
