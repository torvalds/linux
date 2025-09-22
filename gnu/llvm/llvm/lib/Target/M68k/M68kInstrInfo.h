//===-- M68kInstrInfo.h - M68k Instruction Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the M68k implementation of the TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_M68KINSTRINFO_H
#define LLVM_LIB_TARGET_M68K_M68KINSTRINFO_H

#include "M68k.h"
#include "M68kRegisterInfo.h"

#include "MCTargetDesc/M68kBaseInfo.h"

#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "M68kGenInstrInfo.inc"

namespace llvm {

class M68kSubtarget;

namespace M68k {
// These MUST be kept in sync with codes definitions in M68kInstrInfo.td
enum CondCode {
  COND_T = 0,   // True
  COND_F = 1,   // False
  COND_HI = 2,  // High
  COND_LS = 3,  // Less or Same
  COND_CC = 4,  // Carry Clear
  COND_CS = 5,  // Carry Set
  COND_NE = 6,  // Not Equal
  COND_EQ = 7,  // Equal
  COND_VC = 8,  // Overflow Clear
  COND_VS = 9,  // Overflow Set
  COND_PL = 10, // Plus
  COND_MI = 11, // Minus
  COND_GE = 12, // Greater or Equal
  COND_LT = 13, // Less Than
  COND_GT = 14, // Greater Than
  COND_LE = 15, // Less or Equal
  LAST_VALID_COND = COND_LE,
  COND_INVALID
};

// FIXME would be nice tablegen to generate these predicates and converters
// mb tag based

static inline M68k::CondCode GetOppositeBranchCondition(M68k::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Illegal condition code!");
  case M68k::COND_T:
    return M68k::COND_F;
  case M68k::COND_F:
    return M68k::COND_T;
  case M68k::COND_HI:
    return M68k::COND_LS;
  case M68k::COND_LS:
    return M68k::COND_HI;
  case M68k::COND_CC:
    return M68k::COND_CS;
  case M68k::COND_CS:
    return M68k::COND_CC;
  case M68k::COND_NE:
    return M68k::COND_EQ;
  case M68k::COND_EQ:
    return M68k::COND_NE;
  case M68k::COND_VC:
    return M68k::COND_VS;
  case M68k::COND_VS:
    return M68k::COND_VC;
  case M68k::COND_PL:
    return M68k::COND_MI;
  case M68k::COND_MI:
    return M68k::COND_PL;
  case M68k::COND_GE:
    return M68k::COND_LT;
  case M68k::COND_LT:
    return M68k::COND_GE;
  case M68k::COND_GT:
    return M68k::COND_LE;
  case M68k::COND_LE:
    return M68k::COND_GT;
  }
}

static inline unsigned GetCondBranchFromCond(M68k::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Illegal condition code!");
  case M68k::COND_EQ:
    return M68k::Beq8;
  case M68k::COND_NE:
    return M68k::Bne8;
  case M68k::COND_LT:
    return M68k::Blt8;
  case M68k::COND_LE:
    return M68k::Ble8;
  case M68k::COND_GT:
    return M68k::Bgt8;
  case M68k::COND_GE:
    return M68k::Bge8;
  case M68k::COND_CS:
    return M68k::Bcs8;
  case M68k::COND_LS:
    return M68k::Bls8;
  case M68k::COND_HI:
    return M68k::Bhi8;
  case M68k::COND_CC:
    return M68k::Bcc8;
  case M68k::COND_MI:
    return M68k::Bmi8;
  case M68k::COND_PL:
    return M68k::Bpl8;
  case M68k::COND_VS:
    return M68k::Bvs8;
  case M68k::COND_VC:
    return M68k::Bvc8;
  }
}

static inline M68k::CondCode GetCondFromBranchOpc(unsigned Opcode) {
  switch (Opcode) {
  default:
    return M68k::COND_INVALID;
  case M68k::Beq8:
    return M68k::COND_EQ;
  case M68k::Bne8:
    return M68k::COND_NE;
  case M68k::Blt8:
    return M68k::COND_LT;
  case M68k::Ble8:
    return M68k::COND_LE;
  case M68k::Bgt8:
    return M68k::COND_GT;
  case M68k::Bge8:
    return M68k::COND_GE;
  case M68k::Bcs8:
    return M68k::COND_CS;
  case M68k::Bls8:
    return M68k::COND_LS;
  case M68k::Bhi8:
    return M68k::COND_HI;
  case M68k::Bcc8:
    return M68k::COND_CC;
  case M68k::Bmi8:
    return M68k::COND_MI;
  case M68k::Bpl8:
    return M68k::COND_PL;
  case M68k::Bvs8:
    return M68k::COND_VS;
  case M68k::Bvc8:
    return M68k::COND_VC;
  }
}

static inline unsigned IsCMP(unsigned Op) {
  switch (Op) {
  default:
    return false;
  case M68k::CMP8dd:
  case M68k::CMP8df:
  case M68k::CMP8di:
  case M68k::CMP8dj:
  case M68k::CMP8dp:
  case M68k::CMP16dr:
  case M68k::CMP16df:
  case M68k::CMP16di:
  case M68k::CMP16dj:
  case M68k::CMP16dp:
    return true;
  }
}

static inline bool IsSETCC(unsigned SETCC) {
  switch (SETCC) {
  default:
    return false;
  case M68k::SETd8eq:
  case M68k::SETd8ne:
  case M68k::SETd8lt:
  case M68k::SETd8ge:
  case M68k::SETd8le:
  case M68k::SETd8gt:
  case M68k::SETd8cs:
  case M68k::SETd8cc:
  case M68k::SETd8ls:
  case M68k::SETd8hi:
  case M68k::SETd8pl:
  case M68k::SETd8mi:
  case M68k::SETd8vc:
  case M68k::SETd8vs:
  case M68k::SETj8eq:
  case M68k::SETj8ne:
  case M68k::SETj8lt:
  case M68k::SETj8ge:
  case M68k::SETj8le:
  case M68k::SETj8gt:
  case M68k::SETj8cs:
  case M68k::SETj8cc:
  case M68k::SETj8ls:
  case M68k::SETj8hi:
  case M68k::SETj8pl:
  case M68k::SETj8mi:
  case M68k::SETj8vc:
  case M68k::SETj8vs:
  case M68k::SETp8eq:
  case M68k::SETp8ne:
  case M68k::SETp8lt:
  case M68k::SETp8ge:
  case M68k::SETp8le:
  case M68k::SETp8gt:
  case M68k::SETp8cs:
  case M68k::SETp8cc:
  case M68k::SETp8ls:
  case M68k::SETp8hi:
  case M68k::SETp8pl:
  case M68k::SETp8mi:
  case M68k::SETp8vc:
  case M68k::SETp8vs:
    return true;
  }
}

} // namespace M68k

class M68kInstrInfo : public M68kGenInstrInfo {
  virtual void anchor();

protected:
  const M68kSubtarget &Subtarget;
  const M68kRegisterInfo RI;

public:
  explicit M68kInstrInfo(const M68kSubtarget &STI);

  static const M68kInstrInfo *create(M68kSubtarget &STI);

  /// TargetInstrInfo is a superset of MRegister info. As such, whenever a
  /// client has an instance of instruction info, it should always be able to
  /// get register info as well (through this method).
  const M68kRegisterInfo &getRegisterInfo() const { return RI; };

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  bool AnalyzeBranchImpl(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                         MachineBasicBlock *&FBB,
                         SmallVectorImpl<MachineOperand> &Cond,
                         bool AllowModify) const;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  bool getStackSlotRange(const TargetRegisterClass *RC, unsigned SubIdx,
                         unsigned &Size, unsigned &Offset,
                         const MachineFunction &MF) const override;

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

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  bool isPCRelRegisterOperandLegal(const MachineOperand &MO) const override;

  /// Add appropriate SExt nodes
  void AddSExt(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
               DebugLoc DL, unsigned Reg, MVT From, MVT To) const;

  /// Add appropriate ZExt nodes
  void AddZExt(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
               DebugLoc DL, unsigned Reg, MVT From, MVT To) const;

  /// Move immediate to register
  bool ExpandMOVI(MachineInstrBuilder &MIB, MVT MVTSize) const;

  /// Move across register classes without extension
  bool ExpandMOVX_RR(MachineInstrBuilder &MIB, MVT MVTDst, MVT MVTSrc) const;

  /// Move from register and extend
  bool ExpandMOVSZX_RR(MachineInstrBuilder &MIB, bool IsSigned, MVT MVTDst,
                       MVT MVTSrc) const;

  /// Move from memory and extend
  bool ExpandMOVSZX_RM(MachineInstrBuilder &MIB, bool IsSigned,
                       const MCInstrDesc &Desc, MVT MVTDst, MVT MVTSrc) const;

  /// Push/Pop to/from stack
  bool ExpandPUSH_POP(MachineInstrBuilder &MIB, const MCInstrDesc &Desc,
                      bool IsPush) const;

  /// Moves to/from CCR
  bool ExpandCCR(MachineInstrBuilder &MIB, bool IsToCCR) const;

  /// Expand all MOVEM pseudos into real MOVEMs
  bool ExpandMOVEM(MachineInstrBuilder &MIB, const MCInstrDesc &Desc,
                   bool IsRM) const;

  /// Return a virtual register initialized with the global base register
  /// value. Output instructions required to initialize the register in the
  /// function entry block, if necessary.
  unsigned getGlobalBaseReg(MachineFunction *MF) const;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KINSTRINFO_H
