//===- ARMTargetFrameLowering.h - Define frame lowering for ARM -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMFRAMELOWERING_H
#define LLVM_LIB_TARGET_ARM_ARMFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {

class ARMSubtarget;
class CalleeSavedInfo;
class MachineFunction;

class ARMFrameLowering : public TargetFrameLowering {
protected:
  const ARMSubtarget &STI;

public:
  explicit ARMFrameLowering(const ARMSubtarget &sti);

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  bool keepFramePointer(const MachineFunction &MF) const override;

  bool enableCalleeSaveSkip(const MachineFunction &MF) const override;

  bool hasFP(const MachineFunction &MF) const override;
  bool isFPReserved(const MachineFunction &MF) const;
  bool requiresAAPCSFrameRecord(const MachineFunction &MF) const;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  bool canSimplifyCallFramePseudos(const MachineFunction &MF) const override;
  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;
  int ResolveFrameIndexReference(const MachineFunction &MF, int FI,
                                 Register &FrameReg, int SPAdj) const;

  void getCalleeSaves(const MachineFunction &MF,
                      BitVector &SavedRegs) const override;
  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  /// Update the IsRestored flag on LR if it is spilled, based on the return
  /// instructions.
  static void updateLRRestored(MachineFunction &MF);

  void processFunctionBeforeFrameFinalized(
      MachineFunction &MF, RegScavenger *RS = nullptr) const override;

  void adjustForSegmentedStacks(MachineFunction &MF,
                                MachineBasicBlock &MBB) const override;

  /// Returns true if the target will correctly handle shrink wrapping.
  bool enableShrinkWrapping(const MachineFunction &MF) const override;

  bool isProfitableForNoCSROpt(const Function &F) const override {
    // The no-CSR optimisation is bad for code size on ARM, because we can save
    // many registers with a single PUSH/POP pair.
    return false;
  }

  bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;

  const SpillSlot *
  getCalleeSavedSpillSlots(unsigned &NumEntries) const override;

private:
  void emitPushInst(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                    ArrayRef<CalleeSavedInfo> CSI, unsigned StmOpc,
                    unsigned StrOpc, bool NoGap, bool (*Func)(unsigned, bool),
                    unsigned NumAlignedDPRCS2Regs, unsigned MIFlags = 0) const;
  void emitPopInst(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   MutableArrayRef<CalleeSavedInfo> CSI, unsigned LdmOpc,
                   unsigned LdrOpc, bool isVarArg, bool NoGap,
                   bool (*Func)(unsigned, bool),
                   unsigned NumAlignedDPRCS2Regs) const;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMFRAMELOWERING_H
