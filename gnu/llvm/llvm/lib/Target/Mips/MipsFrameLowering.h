//===-- MipsFrameLowering.h - Define frame lowering for Mips ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSFRAMELOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPSFRAMELOWERING_H

#include "Mips.h"
#include "MipsReturnProtectorLowering.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
  class MipsSubtarget;

class MipsFrameLowering : public TargetFrameLowering {
protected:
  const MipsSubtarget &STI;

public:

  const MipsReturnProtectorLowering RPL;

  explicit MipsFrameLowering(const MipsSubtarget &sti, Align Alignment)
      : TargetFrameLowering(StackGrowsDown, Alignment, 0, Alignment), STI(sti), RPL() {
  }

  static const MipsFrameLowering *create(const MipsSubtarget &ST);

  bool hasFP(const MachineFunction &MF) const override;

  bool hasBP(const MachineFunction &MF) const;

  bool allocateScavengingFrameIndexesNearIncomingSP(
    const MachineFunction &MF) const override {
    return false;
  }

  bool enableShrinkWrapping(const MachineFunction &MF) const override {
    return true;
  }

  const ReturnProtectorLowering *getReturnProtector() const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

protected:
  uint64_t estimateStackSize(const MachineFunction &MF) const;
};

/// Create MipsFrameLowering objects.
const MipsFrameLowering *createMips16FrameLowering(const MipsSubtarget &ST);
const MipsFrameLowering *createMipsSEFrameLowering(const MipsSubtarget &ST);

} // End llvm namespace

#endif
