//===-- R600RegisterInfo.h - R600 Register Info Interface ------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Interface definition for R600RegisterInfo
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600REGISTERINFO_H
#define LLVM_LIB_TARGET_AMDGPU_R600REGISTERINFO_H

#define GET_REGINFO_HEADER
#include "R600GenRegisterInfo.inc"

namespace llvm {

struct R600RegisterInfo final : public R600GenRegisterInfo {
  R600RegisterInfo() : R600GenRegisterInfo(0) {}

  /// \returns the sub reg enum value for the given \p Channel
  /// (e.g. getSubRegFromChannel(0) -> R600::sub0)
  static unsigned getSubRegFromChannel(unsigned Channel);

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;

  /// get the HW encoding for a register's channel.
  unsigned getHWRegChan(unsigned reg) const;

  unsigned getHWRegIndex(unsigned Reg) const;

  /// get the register class of the specified type to use in the
  /// CFGStructurizer
  const TargetRegisterClass *getCFGStructurizerRegClass(MVT VT) const;

  bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const override {
    return false;
  }

  // \returns true if \p Reg can be defined in one ALU clause and used in
  // another.
  bool isPhysRegLiveAcrossClauses(Register Reg) const;

  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  void reserveRegisterTuples(BitVector &Reserved, unsigned Reg) const;
};

} // End namespace llvm

#endif
