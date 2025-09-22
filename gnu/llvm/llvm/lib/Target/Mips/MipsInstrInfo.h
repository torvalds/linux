//===- MipsInstrInfo.h - Mips Instruction Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips implementation of the TargetInstrInfo class.
//
// FIXME: We need to override TargetInstrInfo::getInlineAsmLength method in
// order for MipsLongBranch pass to work correctly when the code has inline
// assembly.  The returned value doesn't have to be the asm instruction's exact
// size in bytes; MipsLongBranch only expects it to be the correct upper bound.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSINSTRINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPSINSTRINFO_H

#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "Mips.h"
#include "MipsRegisterInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <cstdint>

#define GET_INSTRINFO_HEADER
#include "MipsGenInstrInfo.inc"

namespace llvm {

class MachineInstr;
class MachineOperand;
class MipsSubtarget;
class TargetRegisterClass;
class TargetRegisterInfo;

class MipsInstrInfo : public MipsGenInstrInfo {
  virtual void anchor();

protected:
  const MipsSubtarget &Subtarget;
  unsigned UncondBrOpc;

public:
  enum BranchType {
    BT_None,       // Couldn't analyze branch.
    BT_NoBranch,   // No branches found.
    BT_Uncond,     // One unconditional branch.
    BT_Cond,       // One conditional branch.
    BT_CondUncond, // A conditional branch followed by an unconditional branch.
    BT_Indirect    // One indirct branch.
  };

  explicit MipsInstrInfo(const MipsSubtarget &STI, unsigned UncondBrOpc);

  static const MipsInstrInfo *create(MipsSubtarget &STI);

  /// Branch Analysis
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

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  BranchType analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                           MachineBasicBlock *&FBB,
                           SmallVectorImpl<MachineOperand> &Cond,
                           bool AllowModify,
                           SmallVectorImpl<MachineInstr *> &BranchInstrs) const;

  /// Determine the opcode of a non-delay slot form for a branch if one exists.
  unsigned getEquivalentCompactForm(const MachineBasicBlock::iterator I) const;

  /// Determine if the branch target is in range.
  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;

  /// Predicate to determine if an instruction can go in a forbidden slot.
  bool SafeInForbiddenSlot(const MachineInstr &MI) const;

  /// Predicate to determine if an instruction can go in an FPU delay slot.
  bool SafeInFPUDelaySlot(const MachineInstr &MIInSlot,
                          const MachineInstr &FPUMI) const;

  /// Predicate to determine if an instruction can go in a load delay slot.
  bool SafeInLoadDelaySlot(const MachineInstr &MIInSlot,
                           const MachineInstr &LoadMI) const;

  /// Predicate to determine if an instruction has a forbidden slot.
  bool HasForbiddenSlot(const MachineInstr &MI) const;

  /// Predicate to determine if an instruction has an FPU delay slot.
  bool HasFPUDelaySlot(const MachineInstr &MI) const;

  /// Predicate to determine if an instruction has a load delay slot.
  bool HasLoadDelaySlot(const MachineInstr &MI) const;

  /// Insert nop instruction when hazard condition is found
  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  /// Insert an ISA appropriate `nop`.
  // FIXME: Add support for MIPS16e.
  MachineInstrBuilder insertNop(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                DebugLoc DL) const;

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  virtual const MipsRegisterInfo &getRegisterInfo() const = 0;

  virtual unsigned getOppositeBranchOpc(unsigned Opc) const = 0;

  virtual bool isBranchWithImm(unsigned Opc) const {
    return false;
  }

  /// Return the number of bytes of code the specified instruction may be.
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, Register SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override {
    storeRegToStack(MBB, MBBI, SrcReg, isKill, FrameIndex, RC, TRI, 0);
  }

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override {
    loadRegFromStack(MBB, MBBI, DestReg, FrameIndex, RC, TRI, 0);
  }

  virtual void storeRegToStack(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               Register SrcReg, bool isKill, int FrameIndex,
                               const TargetRegisterClass *RC,
                               const TargetRegisterInfo *TRI,
                               int64_t Offset) const = 0;

  virtual void loadRegFromStack(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                Register DestReg, int FrameIndex,
                                const TargetRegisterClass *RC,
                                const TargetRegisterInfo *TRI,
                                int64_t Offset) const = 0;

  virtual void adjustStackPtr(unsigned SP, int64_t Amount,
                              MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const = 0;

  /// Create an instruction which has the same operands and memory operands
  /// as MI but has a new opcode.
  MachineInstrBuilder genInstrWithNewOpc(unsigned NewOpc,
                                         MachineBasicBlock::iterator I) const;

  bool findCommutedOpIndices(const MachineInstr &MI, unsigned &SrcOpIdx1,
                             unsigned &SrcOpIdx2) const override;

  /// Perform target specific instruction verification.
  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  std::optional<RegImmPair> isAddImmediate(const MachineInstr &MI,
                                           Register Reg) const override;

  std::optional<ParamLoadedValue>
  describeLoadedValue(const MachineInstr &MI, Register Reg) const override;

protected:
  bool isZeroImm(const MachineOperand &op) const;

  MachineMemOperand *GetMemOperand(MachineBasicBlock &MBB, int FI,
                                   MachineMemOperand::Flags Flags) const;

private:
  virtual unsigned getAnalyzableBrOpc(unsigned Opc) const = 0;

  void AnalyzeCondBr(const MachineInstr *Inst, unsigned Opc,
                     MachineBasicBlock *&BB,
                     SmallVectorImpl<MachineOperand> &Cond) const;

  void BuildCondBr(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                   const DebugLoc &DL, ArrayRef<MachineOperand> Cond) const;
};

/// Create MipsInstrInfo objects.
const MipsInstrInfo *createMips16InstrInfo(const MipsSubtarget &STI);
const MipsInstrInfo *createMipsSEInstrInfo(const MipsSubtarget &STI);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSINSTRINFO_H
