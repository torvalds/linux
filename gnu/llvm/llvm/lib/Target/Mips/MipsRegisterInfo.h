//===- MipsRegisterInfo.h - Mips Register Information Impl ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSREGISTERINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPSREGISTERINFO_H

#include "Mips.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include <cstdint>

#define GET_REGINFO_HEADER
#include "MipsGenRegisterInfo.inc"

namespace llvm {

class TargetRegisterClass;

class MipsRegisterInfo : public MipsGenRegisterInfo {
public:
  enum class MipsPtrClass {
    /// The default register class for integer values.
    Default = 0,
    /// The subset of registers permitted in certain microMIPS instructions
    /// such as lw16.
    GPR16MM = 1,
    /// The stack pointer only.
    StackPointer = 2,
    /// The global pointer only.
    GlobalPointer = 3,
  };

  MipsRegisterInfo();

  /// Get PIC indirect call register
  static unsigned getPICCallReg();

  /// Code Generation virtual methods...
  const TargetRegisterClass *getPointerRegClass(const MachineFunction &MF,
                                                unsigned Kind) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  static const uint32_t *getMips16RetHelperMask();

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  /// Stack Frame Processing Methods
  bool eliminateFrameIndex(MachineBasicBlock::iterator II,
                           int SPAdj, unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Stack realignment queries.
  bool canRealignStack(const MachineFunction &MF) const override;

  /// Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;

  /// Return GPR register class.
  virtual const TargetRegisterClass *intRegClass(unsigned Size) const = 0;

private:
  virtual void eliminateFI(MachineBasicBlock::iterator II, unsigned OpNo,
                           int FrameIndex, uint64_t StackSize,
                           int64_t SPOffset) const = 0;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSREGISTERINFO_H
