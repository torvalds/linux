//===-- VEFrameLowering.h - Define frame lowering for VE --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements VE-specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_VEFRAMELOWERING_H
#define LLVM_LIB_TARGET_VE_VEFRAMELOWERING_H

#include "VE.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {

class VESubtarget;
class VEFrameLowering : public TargetFrameLowering {
public:
  explicit VEFrameLowering(const VESubtarget &ST);

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitPrologueInsns(MachineFunction &MF, MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, uint64_t NumBytes,
                         bool RequireFPUpdate) const;
  void emitEpilogueInsns(MachineFunction &MF, MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, uint64_t NumBytes,
                         bool RequireFPUpdate) const;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  bool hasFP(const MachineFunction &MF) const override;
  bool hasBP(const MachineFunction &MF) const;
  bool hasGOT(const MachineFunction &MF) const;

  // VE reserves argument space always for call sites in the function
  // immediately on entry of the current function.
  bool hasReservedCallFrame(const MachineFunction &MF) const override {
    return true;
  }
  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  const SpillSlot *
  getCalleeSavedSpillSlots(unsigned &NumEntries) const override {
    static const SpillSlot Offsets[] = {
        {VE::SX17, 40},  {VE::SX18, 48},  {VE::SX19, 56},  {VE::SX20, 64},
        {VE::SX21, 72},  {VE::SX22, 80},  {VE::SX23, 88},  {VE::SX24, 96},
        {VE::SX25, 104}, {VE::SX26, 112}, {VE::SX27, 120}, {VE::SX28, 128},
        {VE::SX29, 136}, {VE::SX30, 144}, {VE::SX31, 152}, {VE::SX32, 160},
        {VE::SX33, 168}};
    NumEntries = std::size(Offsets);
    return Offsets;
  }

protected:
  const VESubtarget &STI;

private:
  // Returns true if MF is a leaf procedure.
  bool isLeafProc(MachineFunction &MF) const;

  // Emits code for adjusting SP in function prologue/epilogue.
  void emitSPAdjustment(MachineFunction &MF, MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MBBI, int64_t NumBytes,
                        MaybeAlign MayAlign = MaybeAlign()) const;

  // Emits code for extending SP in function prologue/epilogue.
  void emitSPExtend(MachineFunction &MF, MachineBasicBlock &MBB,
                    MachineBasicBlock::iterator MBBI) const;
};

} // namespace llvm

#endif
