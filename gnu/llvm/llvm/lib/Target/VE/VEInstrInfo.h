//===-- VEInstrInfo.h - VE Instruction Information --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the VE implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_VEINSTRINFO_H
#define LLVM_LIB_TARGET_VE_VEINSTRINFO_H

#include "VERegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "VEGenInstrInfo.inc"

namespace llvm {

class VESubtarget;

/// VEII - This namespace holds all of the Aurora VE target-specific
/// per-instruction flags.  These must match the corresponding definitions in
/// VEInstrFormats.td.
namespace VEII {
enum {
  // Aurora VE Instruction Flags.  These flags describe the characteristics of
  // the Aurora VE instructions for vector handling.

  /// VE_Vector - This instruction is Vector Instruction.
  VE_Vector = 0x1,

  /// VE_VLInUse - This instruction has a vector register in its operands.
  VE_VLInUse = 0x2,

  /// VE_VLMask/Shift - This is a bitmask that selects the index number where
  /// an instruction holds vector length informatio (0 to 6, 7 means undef).n
  VE_VLShift = 2,
  VE_VLMask = 0x07 << VE_VLShift,
};

#define HAS_VLINDEX(TSF) ((TSF)&VEII::VE_VLInUse)
#define GET_VLINDEX(TSF)                                                       \
  (HAS_VLINDEX(TSF) ? (int)(((TSF)&VEII::VE_VLMask) >> VEII::VE_VLShift) : -1)
} // end namespace VEII

class VEInstrInfo : public VEGenInstrInfo {
  const VERegisterInfo RI;
  virtual void anchor();

public:
  explicit VEInstrInfo(VESubtarget &ST);

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  const VERegisterInfo &getRegisterInfo() const { return RI; }

  /// Branch Analysis & Modification {
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
  /// } Branch Analysis & Modification

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  /// Stack Spill & Reload {
  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, Register SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;
  /// } Stack Spill & Reload

  /// Optimization {

  bool foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI, Register Reg,
                     MachineRegisterInfo *MRI) const override;

  /// } Optimization

  Register getGlobalBaseReg(MachineFunction *MF) const;

  // Lower pseudo instructions after register allocation.
  bool expandPostRAPseudo(MachineInstr &MI) const override;

  bool expandExtendStackPseudo(MachineInstr &MI) const;
  bool expandGetStackTopPseudo(MachineInstr &MI) const;
};

} // namespace llvm

#endif
