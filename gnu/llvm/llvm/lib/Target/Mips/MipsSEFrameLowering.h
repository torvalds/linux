//===- MipsSEFrameLowering.h - Mips32/64 frame lowering ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSSEFRAMELOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPSSEFRAMELOWERING_H

#include "MipsFrameLowering.h"

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MipsSubtarget;

class MipsSEFrameLowering : public MipsFrameLowering {
public:
  explicit MipsSEFrameLowering(const MipsSubtarget &STI);

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

private:
  void emitInterruptEpilogueStub(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const;
  void emitInterruptPrologueStub(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSSEFRAMELOWERING_H
