//===-- Thumb1InstrInfo.h - Thumb-1 Instruction Information -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb-1 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_THUMB1INSTRINFO_H
#define LLVM_LIB_TARGET_ARM_THUMB1INSTRINFO_H

#include "ARMBaseInstrInfo.h"
#include "ThumbRegisterInfo.h"

namespace llvm {
  class ARMSubtarget;

class Thumb1InstrInfo : public ARMBaseInstrInfo {
  ThumbRegisterInfo RI;
public:
  explicit Thumb1InstrInfo(const ARMSubtarget &STI);

  /// Return the noop instruction to use for a noop.
  MCInst getNop() const override;

  // Return the non-pre/post incrementing version of 'Opc'. Return 0
  // if there is not such an opcode.
  unsigned getUnindexedOpcode(unsigned Opc) const override;

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  const ThumbRegisterInfo &getRegisterInfo() const override { return RI; }

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

  bool canCopyGluedNodeDuringSchedule(SDNode *N) const override;
private:
  void expandLoadStackGuard(MachineBasicBlock::iterator MI) const override;
};
}

#endif
