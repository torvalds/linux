//===-- CSKYRegisterInfo.h - CSKY Register Information Impl ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the CSKY implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYREGISTERINFO_H
#define LLVM_LIB_TARGET_CSKY_CSKYREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "CSKYGenRegisterInfo.inc"

namespace llvm {
class CSKYInstrInfo;

class CSKYRegisterInfo : public CSKYGenRegisterInfo {
public:
  CSKYRegisterInfo();

  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID id) const override;
  const uint32_t *getNoPreservedMask() const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS) const override;

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool useFPForScavengingIndex(const MachineFunction &MF) const override {
    return false;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKYREGISTERINFO_H
