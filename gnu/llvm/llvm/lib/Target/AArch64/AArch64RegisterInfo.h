//==- AArch64RegisterInfo.h - AArch64 Register Information Impl --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 implementation of the MRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64REGISTERINFO_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64REGISTERINFO_H

#define GET_REGINFO_HEADER
#include "AArch64GenRegisterInfo.inc"

namespace llvm {

class MachineFunction;
class RegScavenger;
class TargetRegisterClass;
class Triple;

class AArch64RegisterInfo final : public AArch64GenRegisterInfo {
  const Triple &TT;

public:
  AArch64RegisterInfo(const Triple &TT);

  // FIXME: This should be tablegen'd like getDwarfRegNum is
  int getSEHRegNum(unsigned i) const {
    return getEncodingValue(i);
  }

  bool isReservedReg(const MachineFunction &MF, MCRegister Reg) const;
  bool isStrictlyReservedReg(const MachineFunction &MF, MCRegister Reg) const;
  bool isAnyArgRegReserved(const MachineFunction &MF) const;
  void emitReservedArgRegCallError(const MachineFunction &MF) const;

  void UpdateCustomCalleeSavedRegs(MachineFunction &MF) const;
  void UpdateCustomCallPreservedMask(MachineFunction &MF,
                                     const uint32_t **Mask) const;

  /// Code Generation virtual methods...
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const MCPhysReg *getDarwinCalleeSavedRegs(const MachineFunction *MF) const;
  const MCPhysReg *
  getCalleeSavedRegsViaCopy(const MachineFunction *MF) const;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  const uint32_t *getDarwinCallPreservedMask(const MachineFunction &MF,
                                             CallingConv::ID) const;

  unsigned getCSRFirstUseCost() const override {
    // The cost will be compared against BlockFrequency where entry has the
    // value of 1 << 14. A value of 5 will choose to spill or split really
    // cold path instead of using a callee-saved register.
    return 5;
  }

  const TargetRegisterClass *
  getSubClassWithSubReg(const TargetRegisterClass *RC,
                        unsigned Idx) const override;

  // Calls involved in thread-local variable lookup save more registers than
  // normal calls, so they need a different mask to represent this.
  const uint32_t *getTLSCallPreservedMask() const;

  const uint32_t *getSMStartStopCallPreservedMask() const;
  const uint32_t *SMEABISupportRoutinesCallPreservedMaskFromX0() const;

  // Funclets on ARM64 Windows don't preserve any registers.
  const uint32_t *getNoPreservedMask() const override;

  // Unwinders may not preserve all Neon and SVE registers.
  const uint32_t *
  getCustomEHPadPreservedMask(const MachineFunction &MF) const override;

  /// getThisReturnPreservedMask - Returns a call preserved mask specific to the
  /// case that 'returned' is on an i64 first argument if the calling convention
  /// is one that can (partially) model this attribute with a preserved mask
  /// (i.e. it is a calling convention that uses the same register for the first
  /// i64 argument and an i64 return value)
  ///
  /// Should return NULL in the case that the calling convention does not have
  /// this property
  const uint32_t *getThisReturnPreservedMask(const MachineFunction &MF,
                                             CallingConv::ID) const;

  /// Stack probing calls preserve different CSRs to the normal CC.
  const uint32_t *getWindowsStackProbePreservedMask() const;

  BitVector getStrictlyReservedRegs(const MachineFunction &MF) const;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  std::optional<std::string>
  explainReservedReg(const MachineFunction &MF,
                     MCRegister PhysReg) const override;
  bool isAsmClobberable(const MachineFunction &MF,
                       MCRegister PhysReg) const override;
  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;
  const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override;
  bool useFPForScavengingIndex(const MachineFunction &MF) const override;
  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override;

  bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const override;
  bool isFrameOffsetLegal(const MachineInstr *MI, Register BaseReg,
                          int64_t Offset) const override;
  Register materializeFrameBaseRegister(MachineBasicBlock *MBB, int FrameIdx,
                                        int64_t Offset) const override;
  void resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                         int64_t Offset) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  bool cannotEliminateFrame(const MachineFunction &MF) const;

  bool requiresVirtualBaseRegisters(const MachineFunction &MF) const override;
  bool hasBasePointer(const MachineFunction &MF) const;
  unsigned getBaseRegister() const;

  bool isArgumentRegister(const MachineFunction &MF,
                          MCRegister Reg) const override;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  unsigned getLocalAddressRegister(const MachineFunction &MF) const;
  bool regNeedsCFI(unsigned Reg, unsigned &RegToUseForCFI) const;

  /// SrcRC and DstRC will be morphed into NewRC if this returns true
  bool shouldCoalesce(MachineInstr *MI, const TargetRegisterClass *SrcRC,
                      unsigned SubReg, const TargetRegisterClass *DstRC,
                      unsigned DstSubReg, const TargetRegisterClass *NewRC,
                      LiveIntervals &LIS) const override;

  void getOffsetOpcodes(const StackOffset &Offset,
                        SmallVectorImpl<uint64_t> &Ops) const override;

  bool shouldAnalyzePhysregInMachineLoopInfo(MCRegister R) const override;
};

} // end namespace llvm

#endif
