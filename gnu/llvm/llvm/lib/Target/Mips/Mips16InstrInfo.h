//===- Mips16InstrInfo.h - Mips16 Instruction Information -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips16 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPS16INSTRINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPS16INSTRINFO_H

#include "Mips16RegisterInfo.h"
#include "MipsInstrInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>

namespace llvm {

class MCInstrDesc;
class MipsSubtarget;

class Mips16InstrInfo : public MipsInstrInfo {
  const Mips16RegisterInfo RI;

public:
  explicit Mips16InstrInfo(const MipsSubtarget &STI);

  const MipsRegisterInfo &getRegisterInfo() const override;

  /// isLoadFromStackSlot - If the specified machine instruction is a direct
  /// load from a stack slot, return the virtual or physical register number of
  /// the destination along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than loading from the stack slot.
  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;

  /// isStoreToStackSlot - If the specified machine instruction is a direct
  /// store to a stack slot, return the virtual or physical register number of
  /// the source reg along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than storing to the stack slot.
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  void storeRegToStack(MachineBasicBlock &MBB,
                       MachineBasicBlock::iterator MBBI,
                       Register SrcReg, bool isKill, int FrameIndex,
                       const TargetRegisterClass *RC,
                       const TargetRegisterInfo *TRI,
                       int64_t Offset) const override;

  void loadRegFromStack(MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MBBI,
                        Register DestReg, int FrameIndex,
                        const TargetRegisterClass *RC,
                        const TargetRegisterInfo *TRI,
                        int64_t Offset) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  unsigned getOppositeBranchOpc(unsigned Opc) const override;

  // Adjust SP by FrameSize bytes. Save RA, S0, S1
  void makeFrame(unsigned SP, int64_t FrameSize, MachineBasicBlock &MBB,
                 MachineBasicBlock::iterator I) const;

  // Adjust SP by FrameSize bytes. Restore RA, S0, S1
  void restoreFrame(unsigned SP, int64_t FrameSize, MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator I) const;

  /// Adjust SP by Amount bytes.
  void adjustStackPtr(unsigned SP, int64_t Amount, MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator I) const override;

  /// Emit a series of instructions to load an immediate.
  // This is to adjust some FrameReg. We return the new register to be used
  // in place of FrameReg and the adjusted immediate field (&NewImm)
  unsigned loadImmediate(unsigned FrameReg, int64_t Imm, MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator II, const DebugLoc &DL,
                         unsigned &NewImm) const;

  static bool validImmediate(unsigned Opcode, unsigned Reg, int64_t Amount);

  static bool validSpImm8(int offset) {
    return ((offset & 7) == 0) && isInt<11>(offset);
  }

  // build the proper one based on the Imm field

  const MCInstrDesc& AddiuSpImm(int64_t Imm) const;

  void BuildAddiuSpImm
    (MachineBasicBlock &MBB, MachineBasicBlock::iterator I, int64_t Imm) const;

protected:
  /// If the specific machine instruction is a instruction that moves/copies
  /// value from one register to another register return destination and source
  /// registers as machine operands.
  std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &MI) const override;

private:
  unsigned getAnalyzableBrOpc(unsigned Opc) const override;

  void ExpandRetRA16(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   unsigned Opc) const;

  // Adjust SP by Amount bytes where bytes can be up to 32bit number.
  void adjustStackPtrBig(unsigned SP, int64_t Amount, MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator I,
                         unsigned Reg1, unsigned Reg2) const;

  // Adjust SP by Amount bytes where bytes can be up to 32bit number.
  void adjustStackPtrBigUnrestricted(unsigned SP, int64_t Amount,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPS16INSTRINFO_H
