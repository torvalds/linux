//===- NVPTXInstrInfo.h - NVPTX Instruction Information----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the NVPTX implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXINSTRINFO_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXINSTRINFO_H

#include "NVPTX.h"
#include "NVPTXRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "NVPTXGenInstrInfo.inc"

namespace llvm {

class NVPTXInstrInfo : public NVPTXGenInstrInfo {
  const NVPTXRegisterInfo RegInfo;
  virtual void anchor();
public:
  explicit NVPTXInstrInfo();

  const NVPTXRegisterInfo &getRegisterInfo() const { return RegInfo; }

  /* The following virtual functions are used in register allocation.
   * They are not implemented because the existing interface and the logic
   * at the caller side do not work for the elementized vector load and store.
   *
   * virtual Register isLoadFromStackSlot(const MachineInstr *MI,
   *                                  int &FrameIndex) const;
   * virtual Register isStoreToStackSlot(const MachineInstr *MI,
   *                                 int &FrameIndex) const;
   * virtual void storeRegToStackSlot(MachineBasicBlock &MBB,
   *                              MachineBasicBlock::iterator MBBI,
   *                             unsigned SrcReg, bool isKill, int FrameIndex,
   *                              const TargetRegisterClass *RC,
   *                              Register VReg) const;
   * virtual void loadRegFromStackSlot(MachineBasicBlock &MBB,
   *                               MachineBasicBlock::iterator MBBI,
   *                               unsigned DestReg, int FrameIndex,
   *                               const TargetRegisterClass *RC,
   *                               const TargetRegisterInfo *TRI,
   *                               Register VReg) const;
   */

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  // Branch analysis.
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
};

} // namespace llvm

#endif
