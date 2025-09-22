//===- LanaiRegisterInfo.h - Lanai Register Information Impl ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Lanai implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_LANAIREGISTERINFO_H
#define LLVM_LIB_TARGET_LANAI_LANAIREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "LanaiGenRegisterInfo.inc"

namespace llvm {

struct LanaiRegisterInfo : public LanaiGenRegisterInfo {
  LanaiRegisterInfo();

  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;

  // Code Generation virtual methods.
  const uint16_t *
  getCalleeSavedRegs(const MachineFunction *MF = nullptr) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Debug information queries.
  unsigned getRARegister() const;
  Register getFrameRegister(const MachineFunction &MF) const override;
  Register getBaseRegister() const;
  bool hasBasePointer(const MachineFunction &MF) const;

  int getDwarfRegNum(unsigned RegNum, bool IsEH) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_LANAIREGISTERINFO_H
