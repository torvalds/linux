//===- ARCRegisterInfo.h - ARC Register Information Impl --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARC implementation of the MRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCREGISTERINFO_H
#define LLVM_LIB_TARGET_ARC_ARCREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "ARCGenRegisterInfo.inc"

namespace llvm {

class TargetInstrInfo;
class ARCSubtarget;

struct ARCRegisterInfo : public ARCGenRegisterInfo {
  const ARCSubtarget &ST;

public:
  ARCRegisterInfo(const ARCSubtarget &);

  /// Code Generation virtual methods...

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override;

  bool useFPForScavengingIndex(const MachineFunction &MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;

  //! Return whether to emit frame moves
  static bool needsFrameMoves(const MachineFunction &MF);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCREGISTERINFO_H
