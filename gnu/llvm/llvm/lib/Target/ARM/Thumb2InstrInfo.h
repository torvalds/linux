//===-- Thumb2InstrInfo.h - Thumb-2 Instruction Information -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb-2 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_THUMB2INSTRINFO_H
#define LLVM_LIB_TARGET_ARM_THUMB2INSTRINFO_H

#include "ARMBaseInstrInfo.h"
#include "ThumbRegisterInfo.h"

namespace llvm {
class ARMSubtarget;

class Thumb2InstrInfo : public ARMBaseInstrInfo {
  ThumbRegisterInfo RI;
public:
  explicit Thumb2InstrInfo(const ARMSubtarget &STI);

  /// Return the noop instruction to use for a noop.
  MCInst getNop() const override;

  // Return the non-pre/post incrementing version of 'Opc'. Return 0
  // if there is not such an opcode.
  unsigned getUnindexedOpcode(unsigned Opc) const override;

  void ReplaceTailWithBranchTo(MachineBasicBlock::iterator Tail,
                               MachineBasicBlock *NewDest) const override;

  bool isLegalToSplitMBBAt(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, Register SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  const ThumbRegisterInfo &getRegisterInfo() const override { return RI; }

  MachineInstr *optimizeSelect(MachineInstr &MI,
                               SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                               bool) const override;

  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx1,
                                       unsigned OpIdx2) const override;

  bool isSchedulingBoundary(const MachineInstr &MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

private:
  void expandLoadStackGuard(MachineBasicBlock::iterator MI) const override;
};

/// getITInstrPredicate - Valid only in Thumb2 mode. This function is identical
/// to llvm::getInstrPredicate except it returns AL for conditional branch
/// instructions which are "predicated", but are not in IT blocks.
ARMCC::CondCodes getITInstrPredicate(const MachineInstr &MI, Register &PredReg);

// getVPTInstrPredicate: VPT analogue of that, plus a helper function
// corresponding to MachineInstr::findFirstPredOperandIdx.
int findFirstVPTPredOperandIdx(const MachineInstr &MI);
ARMVCC::VPTCodes getVPTInstrPredicate(const MachineInstr &MI,
                                      Register &PredReg);
inline ARMVCC::VPTCodes getVPTInstrPredicate(const MachineInstr &MI) {
  Register PredReg;
  return getVPTInstrPredicate(MI, PredReg);
}

// Recomputes the Block Mask of Instr, a VPT or VPST instruction.
// This rebuilds the block mask of the instruction depending on the predicates
// of the instructions following it. This should only be used after the
// MVEVPTBlockInsertion pass has run, and should be used whenever a predicated
// instruction is added to/removed from the block.
void recomputeVPTBlockMask(MachineInstr &Instr);
} // namespace llvm

#endif
