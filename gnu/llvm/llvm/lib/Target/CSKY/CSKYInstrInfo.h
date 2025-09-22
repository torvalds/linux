//===-- CSKYInstrInfo.h - CSKY Instruction Information --------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the CSKY implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYINSTRINFO_H
#define LLVM_LIB_TARGET_CSKY_CSKYINSTRINFO_H

#include "MCTargetDesc/CSKYMCTargetDesc.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "CSKYGenInstrInfo.inc"

namespace llvm {

class CSKYSubtarget;

class CSKYInstrInfo : public CSKYGenInstrInfo {
  bool v2sf;
  bool v2df;
  bool v3sf;
  bool v3df;

protected:
  const CSKYSubtarget &STI;

public:
  explicit CSKYInstrInfo(CSKYSubtarget &STI);

  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, Register SrcReg,
                           bool IsKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  Register getGlobalBaseReg(MachineFunction &MF) const;

  // Materializes the given integer Val into DstReg.
  Register movImm(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                  const DebugLoc &DL, uint64_t Val,
                  MachineInstr::MIFlag Flag = MachineInstr::NoFlags) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKYINSTRINFO_H
