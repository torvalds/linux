//=- LoongArchInstrInfo.h - LoongArch Instruction Information ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the LoongArch implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_LOONGARCHINSTRINFO_H
#define LLVM_LIB_TARGET_LOONGARCH_LOONGARCHINSTRINFO_H

#include "LoongArchRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "LoongArchGenInstrInfo.inc"

namespace llvm {

class LoongArchSubtarget;

class LoongArchInstrInfo : public LoongArchGenInstrInfo {
public:
  explicit LoongArchInstrInfo(LoongArchSubtarget &STI);

  MCInst getNop() const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                   const DebugLoc &DL, MCRegister DstReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, Register SrcReg,
                           bool IsKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, Register DstReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;

  // Materializes the given integer Val into DstReg.
  void movImm(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
              const DebugLoc &DL, Register DstReg, uint64_t Val,
              MachineInstr::MIFlag Flag = MachineInstr::NoFlags) const;

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  bool isAsCheapAsAMove(const MachineInstr &MI) const override;

  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &dl,
                        int *BytesAdded = nullptr) const override;

  void insertIndirectBranch(MachineBasicBlock &MBB,
                            MachineBasicBlock &NewDestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset, RegScavenger *RS) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

protected:
  const LoongArchSubtarget &STI;
};

namespace LoongArch {

// Returns true if this is the sext.w pattern, addi.w rd, rs, 0.
bool isSEXT_W(const MachineInstr &MI);

// Mask assignments for floating-point.
static constexpr unsigned FClassMaskSignalingNaN = 0x001;
static constexpr unsigned FClassMaskQuietNaN = 0x002;
static constexpr unsigned FClassMaskNegativeInfinity = 0x004;
static constexpr unsigned FClassMaskNegativeNormal = 0x008;
static constexpr unsigned FClassMaskNegativeSubnormal = 0x010;
static constexpr unsigned FClassMaskNegativeZero = 0x020;
static constexpr unsigned FClassMaskPositiveInfinity = 0x040;
static constexpr unsigned FClassMaskPositiveNormal = 0x080;
static constexpr unsigned FClassMaskPositiveSubnormal = 0x100;
static constexpr unsigned FClassMaskPositiveZero = 0x200;
} // namespace LoongArch

} // end namespace llvm
#endif // LLVM_LIB_TARGET_LOONGARCH_LOONGARCHINSTRINFO_H
