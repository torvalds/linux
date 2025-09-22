//===-- SPIRVInstrInfo.h - SPIR-V Instruction Information -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the SPIR-V implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVINSTRINFO_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVINSTRINFO_H

#include "SPIRVRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "SPIRVGenInstrInfo.inc"

namespace llvm {

class SPIRVInstrInfo : public SPIRVGenInstrInfo {
  const SPIRVRegisterInfo RI;

public:
  SPIRVInstrInfo();

  const SPIRVRegisterInfo &getRegisterInfo() const { return RI; }
  bool isHeaderInstr(const MachineInstr &MI) const;
  bool isConstantInstr(const MachineInstr &MI) const;
  bool isInlineAsmDefInstr(const MachineInstr &MI) const;
  bool isTypeDeclInstr(const MachineInstr &MI) const;
  bool isDecorationInstr(const MachineInstr &MI) const;
  bool canUseFastMathFlags(const MachineInstr &MI) const;
  bool canUseNSW(const MachineInstr &MI) const;
  bool canUseNUW(const MachineInstr &MI) const;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;
  bool expandPostRAPseudo(MachineInstr &MI) const override;
};

namespace SPIRV {
enum AsmComments {
  // It is a half type
  ASM_PRINTER_WIDTH16 = MachineInstr::TAsmComments
};
} // namespace SPIRV

} // namespace llvm

#endif // LLVM_LIB_TARGET_SPIRV_SPIRVINSTRINFO_H
