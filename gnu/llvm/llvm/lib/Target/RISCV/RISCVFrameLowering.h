//===-- RISCVFrameLowering.h - Define frame lowering for RISC-V -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements RISC-V specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_RISCVFRAMELOWERING_H
#define LLVM_LIB_TARGET_RISCV_RISCVFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {
class RISCVSubtarget;

class RISCVFrameLowering : public TargetFrameLowering {
public:
  explicit RISCVFrameLowering(const RISCVSubtarget &STI);

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  uint64_t getStackSizeWithRVVPadding(const MachineFunction &MF) const;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  bool hasFP(const MachineFunction &MF) const override;

  bool hasBP(const MachineFunction &MF) const;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

  bool assignCalleeSavedSpillSlots(MachineFunction &MF,
                                   const TargetRegisterInfo *TRI,
                                   std::vector<CalleeSavedInfo> &CSI,
                                   unsigned &MinCSFrameIndex,
                                   unsigned &MaxCSFrameIndex) const override;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  // Get the first stack adjustment amount for SplitSPAdjust.
  // Return 0 if we don't want to split the SP adjustment in prologue and
  // epilogue.
  uint64_t getFirstSPAdjustAmount(const MachineFunction &MF) const;

  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;
  bool canUseAsEpilogue(const MachineBasicBlock &MBB) const override;

  bool enableShrinkWrapping(const MachineFunction &MF) const override;

  bool isSupportedStackID(TargetStackID::Value ID) const override;
  TargetStackID::Value getStackIDForScalableVectors() const override;

  bool isStackIdSafeForLocalArea(unsigned StackId) const override {
    // We don't support putting RISC-V Vector objects into the pre-allocated
    // local frame block at the moment.
    return StackId != TargetStackID::ScalableVector;
  }

protected:
  const RISCVSubtarget &STI;

private:
  void determineFrameLayout(MachineFunction &MF) const;
  void adjustStackForRVV(MachineFunction &MF, MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                         int64_t Amount, MachineInstr::MIFlag Flag) const;
  void emitCalleeSavedRVVPrologCFI(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   bool HasFP) const;
  std::pair<int64_t, Align>
  assignRVVStackObjectOffsets(MachineFunction &MF) const;
};
} // namespace llvm
#endif
