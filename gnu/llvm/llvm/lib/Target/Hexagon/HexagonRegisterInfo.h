//==- HexagonRegisterInfo.h - Hexagon Register Information Impl --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Hexagon implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONREGISTERINFO_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "HexagonGenRegisterInfo.inc"

namespace llvm {

namespace Hexagon {
  // Generic (pseudo) subreg indices for use with getHexagonSubRegIndex.
  enum { ps_sub_lo = 0, ps_sub_hi = 1 };
}

class HexagonRegisterInfo : public HexagonGenRegisterInfo {
public:
  HexagonRegisterInfo(unsigned HwMode);

  /// Code Generation virtual methods...
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF)
        const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
        CallingConv::ID) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
        unsigned FIOperandNum, RegScavenger *RS = nullptr) const override;

  /// Returns true since we may need scavenging for a temporary register
  /// when generating hardware loop instructions.
  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  /// Returns true. Spill code for predicate registers might need an extra
  /// register.
  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override {
    return true;
  }

  /// Returns true if the frame pointer is valid.
  bool useFPForScavengingIndex(const MachineFunction &MF) const override;

  bool shouldCoalesce(MachineInstr *MI, const TargetRegisterClass *SrcRC,
        unsigned SubReg, const TargetRegisterClass *DstRC, unsigned DstSubReg,
        const TargetRegisterClass *NewRC, LiveIntervals &LIS) const override;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;
  Register getFrameRegister() const;
  Register getStackRegister() const;

  unsigned getHexagonSubRegIndex(const TargetRegisterClass &RC,
        unsigned GenIdx) const;

  const MCPhysReg *getCallerSavedRegs(const MachineFunction *MF,
        const TargetRegisterClass *RC) const;

  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;

  bool isEHReturnCalleeSaveReg(Register Reg) const;
};

} // end namespace llvm

#endif
