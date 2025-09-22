//=- WebAssemblyInstrInfo.h - WebAssembly Instruction Information -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the WebAssembly implementation of the
/// TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYINSTRINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYINSTRINFO_H

#include "WebAssemblyRegisterInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "WebAssemblyGenInstrInfo.inc"

#define GET_INSTRINFO_OPERAND_ENUM
#include "WebAssemblyGenInstrInfo.inc"

namespace llvm {

namespace WebAssembly {

int16_t getNamedOperandIdx(uint16_t Opcode, uint16_t NamedIndex);

}

class WebAssemblySubtarget;

class WebAssemblyInstrInfo final : public WebAssemblyGenInstrInfo {
  const WebAssemblyRegisterInfo RI;

public:
  explicit WebAssemblyInstrInfo(const WebAssemblySubtarget &STI);

  const WebAssemblyRegisterInfo &getRegisterInfo() const { return RI; }

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx1,
                                       unsigned OpIdx2) const override;

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
  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  ArrayRef<std::pair<int, const char *>>
  getSerializableTargetIndices() const override;

  const MachineOperand &getCalleeOperand(const MachineInstr &MI) const override;

  bool isExplicitTargetIndexDef(const MachineInstr &MI, int &Index,
                                int64_t &Offset) const override;
};

} // end namespace llvm

#endif
