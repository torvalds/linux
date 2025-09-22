//===- LanaiInstrInfo.h - Lanai Instruction Information ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Lanai implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_LANAIINSTRINFO_H
#define LLVM_LIB_TARGET_LANAI_LANAIINSTRINFO_H

#include "LanaiRegisterInfo.h"
#include "MCTargetDesc/LanaiMCTargetDesc.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "LanaiGenInstrInfo.inc"

namespace llvm {

class LanaiInstrInfo : public LanaiGenInstrInfo {
  const LanaiRegisterInfo RegisterInfo;

public:
  LanaiInstrInfo();

  // getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  // such, whenever a client has an instance of instruction info, it should
  // always be able to get register info as well (through this method).
  virtual const LanaiRegisterInfo &getRegisterInfo() const {
    return RegisterInfo;
  }

  bool areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                       const MachineInstr &MIb) const override;

  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;

  Register isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                     int &FrameIndex) const override;

  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator Position,
                   const DebugLoc &DL, MCRegister DestinationRegister,
                   MCRegister SourceRegister, bool KillSource) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator Position,
                           Register SourceRegister, bool IsKill, int FrameIndex,
                           const TargetRegisterClass *RegisterClass,
                           const TargetRegisterInfo *RegisterInfo,
                           Register VReg) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator Position,
                            Register DestinationRegister, int FrameIndex,
                            const TargetRegisterClass *RegisterClass,
                            const TargetRegisterInfo *RegisterInfo,
                            Register VReg) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  bool getMemOperandsWithOffsetWidth(
      const MachineInstr &LdSt,
      SmallVectorImpl<const MachineOperand *> &BaseOps, int64_t &Offset,
      bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const override;

  bool getMemOperandWithOffsetWidth(const MachineInstr &LdSt,
                                    const MachineOperand *&BaseOp,
                                    int64_t &Offset, LocationSize &Width,
                                    const TargetRegisterInfo *TRI) const;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TrueBlock,
                     MachineBasicBlock *&FalseBlock,
                     SmallVectorImpl<MachineOperand> &Condition,
                     bool AllowModify) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  // For a comparison instruction, return the source registers in SrcReg and
  // SrcReg2 if having two register operands, and the value it compares against
  // in CmpValue. Return true if the comparison instruction can be analyzed.
  bool analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                      Register &SrcReg2, int64_t &CmpMask,
                      int64_t &CmpValue) const override;

  // See if the comparison instruction can be converted into something more
  // efficient. E.g., on Lanai register-register instructions can set the flag
  // register, obviating the need for a separate compare.
  bool optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                            Register SrcReg2, int64_t CmpMask, int64_t CmpValue,
                            const MachineRegisterInfo *MRI) const override;

  // Analyze the given select instruction, returning true if it cannot be
  // understood. It is assumed that MI->isSelect() is true.
  //
  // When successful, return the controlling condition and the operands that
  // determine the true and false result values.
  //
  //   Result = SELECT Cond, TrueOp, FalseOp
  //
  // Lanai can optimize certain select instructions, for example by predicating
  // the instruction defining one of the operands and sets Optimizable to true.
  bool analyzeSelect(const MachineInstr &MI,
                     SmallVectorImpl<MachineOperand> &Cond, unsigned &TrueOp,
                     unsigned &FalseOp, bool &Optimizable) const override;

  // Given a select instruction that was understood by analyzeSelect and
  // returned Optimizable = true, attempt to optimize MI by merging it with one
  // of its operands. Returns NULL on failure.
  //
  // When successful, returns the new select instruction. The client is
  // responsible for deleting MI.
  //
  // If both sides of the select can be optimized, the TrueOp is modifed.
  // PreferFalse is not used.
  MachineInstr *optimizeSelect(MachineInstr &MI,
                               SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                               bool PreferFalse) const override;

  bool reverseBranchCondition(
      SmallVectorImpl<MachineOperand> &Condition) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TrueBlock,
                        MachineBasicBlock *FalseBlock,
                        ArrayRef<MachineOperand> Condition,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
};

static inline bool isSPLSOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Lanai::LDBs_RI:
  case Lanai::LDBz_RI:
  case Lanai::LDHs_RI:
  case Lanai::LDHz_RI:
  case Lanai::STB_RI:
  case Lanai::STH_RI:
    return true;
  default:
    return false;
  }
}

static inline bool isRMOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Lanai::LDW_RI:
  case Lanai::SW_RI:
    return true;
  default:
    return false;
  }
}

static inline bool isRRMOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Lanai::LDBs_RR:
  case Lanai::LDBz_RR:
  case Lanai::LDHs_RR:
  case Lanai::LDHz_RR:
  case Lanai::LDWz_RR:
  case Lanai::LDW_RR:
  case Lanai::STB_RR:
  case Lanai::STH_RR:
  case Lanai::SW_RR:
    return true;
  default:
    return false;
  }
}

} // namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_LANAIINSTRINFO_H
