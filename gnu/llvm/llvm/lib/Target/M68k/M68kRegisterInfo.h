//===-- M68kRegisterInfo.h - M68k Register Information Impl -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the M68k implementation of the TargetRegisterInfo
/// class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_M68KREGISTERINFO_H
#define LLVM_LIB_TARGET_M68K_M68KREGISTERINFO_H

#include "M68k.h"

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "M68kGenRegisterInfo.inc"

namespace llvm {
class M68kSubtarget;
class TargetInstrInfo;
class Type;

class M68kRegisterInfo : public M68kGenRegisterInfo {
  virtual void anchor();

  /// Physical register used as stack ptr.
  unsigned StackPtr;

  /// Physical register used as frame ptr.
  unsigned FramePtr;

  /// Physical register used as a base ptr in complex stack frames.  I.e., when
  /// we need a 3rd base, not just SP and FP, due to variable size stack
  /// objects.
  unsigned BasePtr;

  /// Physical register used to store GOT address if needed.
  unsigned GlobalBasePtr;

protected:
  const M68kSubtarget &Subtarget;

public:
  M68kRegisterInfo(const M68kSubtarget &Subtarget);

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;

  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;

  /// Returns a register class with registers that can be used in forming tail
  /// calls.
  const TargetRegisterClass *
  getRegsForTailCall(const MachineFunction &MF) const;

  /// Return a mega-register of the specified register Reg so its sub-register
  /// of index SubIdx is Reg, its super(or mega) Reg. In other words it will
  /// return a register that is not direct super register but still shares
  /// physical register with Reg.
  /// NOTE not sure about the term though.
  unsigned getMatchingMegaReg(unsigned Reg,
                              const TargetRegisterClass *RC) const;

  /// Returns the Register Class of a physical register of the given type,
  /// picking the biggest register class of the right type that contains this
  /// physreg.
  const TargetRegisterClass *getMaximalPhysRegClass(unsigned reg, MVT VT) const;

  /// Return index of a register within a register class, otherwise return -1
  int getRegisterOrder(unsigned Reg, const TargetRegisterClass &TRC) const;

  /// Return spill order index of a register, if there is none then trap
  int getSpillRegisterOrder(unsigned Reg) const;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override;

  bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const override;

  /// FrameIndex represent objects inside a abstract stack. We must replace
  /// FrameIndex with an stack/frame pointer direct reference.
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  bool hasBasePointer(const MachineFunction &MF) const;

  /// True if the stack can be realigned for the target.
  bool canRealignStack(const MachineFunction &MF) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;

  const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const override {
    if (RC == &M68k::CCRCRegClass)
      return &M68k::DR32RegClass;
    return RC;
  }

  unsigned getStackRegister() const { return StackPtr; }
  unsigned getBaseRegister() const { return BasePtr; }
  unsigned getGlobalBaseRegister() const { return GlobalBasePtr; }

  const TargetRegisterClass *intRegClass(unsigned Size) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KREGISTERINFO_H
