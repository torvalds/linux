//===- Mips16InstrInfo.cpp - Mips16 Instruction Information ---------------===//
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

#include "Mips16InstrInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "mips16-instrinfo"

Mips16InstrInfo::Mips16InstrInfo(const MipsSubtarget &STI)
    : MipsInstrInfo(STI, Mips::Bimm16) {}

const MipsRegisterInfo &Mips16InstrInfo::getRegisterInfo() const {
  return RI;
}

/// isLoadFromStackSlot - If the specified machine instruction is a direct
/// load from a stack slot, return the virtual or physical register number of
/// the destination along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than loading from the stack slot.
Register Mips16InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                              int &FrameIndex) const {
  return 0;
}

/// isStoreToStackSlot - If the specified machine instruction is a direct
/// store to a stack slot, return the virtual or physical register number of
/// the source reg along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than storing to the stack slot.
Register Mips16InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                             int &FrameIndex) const {
  return 0;
}

void Mips16InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, MCRegister DestReg,
                                  MCRegister SrcReg, bool KillSrc) const {
  unsigned Opc = 0;

  if (Mips::CPU16RegsRegClass.contains(DestReg) &&
      Mips::GPR32RegClass.contains(SrcReg))
    Opc = Mips::MoveR3216;
  else if (Mips::GPR32RegClass.contains(DestReg) &&
           Mips::CPU16RegsRegClass.contains(SrcReg))
    Opc = Mips::Move32R16;
  else if ((SrcReg == Mips::HI0) &&
           (Mips::CPU16RegsRegClass.contains(DestReg)))
    Opc = Mips::Mfhi16, SrcReg = 0;
  else if ((SrcReg == Mips::LO0) &&
           (Mips::CPU16RegsRegClass.contains(DestReg)))
    Opc = Mips::Mflo16, SrcReg = 0;

  assert(Opc && "Cannot copy registers");

  MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opc));

  if (DestReg)
    MIB.addReg(DestReg, RegState::Define);

  if (SrcReg)
    MIB.addReg(SrcReg, getKillRegState(KillSrc));
}

std::optional<DestSourcePair>
Mips16InstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {
  if (MI.isMoveReg())
    return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};
  return std::nullopt;
}

void Mips16InstrInfo::storeRegToStack(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator I,
                                      Register SrcReg, bool isKill, int FI,
                                      const TargetRegisterClass *RC,
                                      const TargetRegisterInfo *TRI,
                                      int64_t Offset) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOStore);
  unsigned Opc = 0;
  if (Mips::CPU16RegsRegClass.hasSubClassEq(RC))
    Opc = Mips::SwRxSpImmX16;
  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc)).addReg(SrcReg, getKillRegState(isKill)).
      addFrameIndex(FI).addImm(Offset)
      .addMemOperand(MMO);
}

void Mips16InstrInfo::loadRegFromStack(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       Register DestReg, int FI,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI,
                                       int64_t Offset) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOLoad);
  unsigned Opc = 0;

  if (Mips::CPU16RegsRegClass.hasSubClassEq(RC))
    Opc = Mips::LwRxSpImmX16;
  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc), DestReg).addFrameIndex(FI).addImm(Offset)
    .addMemOperand(MMO);
}

bool Mips16InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  switch (MI.getDesc().getOpcode()) {
  default:
    return false;
  case Mips::RetRA16:
    ExpandRetRA16(MBB, MI, Mips::JrcRa16);
    break;
  }

  MBB.erase(MI.getIterator());
  return true;
}

/// GetOppositeBranchOpc - Return the inverse of the specified
/// opcode, e.g. turning BEQ to BNE.
unsigned Mips16InstrInfo::getOppositeBranchOpc(unsigned Opc) const {
  switch (Opc) {
  case Mips::BeqzRxImmX16: return Mips::BnezRxImmX16;
  case Mips::BnezRxImmX16: return Mips::BeqzRxImmX16;
  case Mips::BeqzRxImm16: return Mips::BnezRxImm16;
  case Mips::BnezRxImm16: return Mips::BeqzRxImm16;
  case Mips::BteqzT8CmpX16: return Mips::BtnezT8CmpX16;
  case Mips::BteqzT8SltX16: return Mips::BtnezT8SltX16;
  case Mips::BteqzT8SltiX16: return Mips::BtnezT8SltiX16;
  case Mips::Btnez16: return Mips::Bteqz16;
  case Mips::BtnezX16: return Mips::BteqzX16;
  case Mips::BtnezT8CmpiX16: return Mips::BteqzT8CmpiX16;
  case Mips::BtnezT8SltuX16: return Mips::BteqzT8SltuX16;
  case Mips::BtnezT8SltiuX16: return Mips::BteqzT8SltiuX16;
  case Mips::Bteqz16: return Mips::Btnez16;
  case Mips::BteqzX16: return Mips::BtnezX16;
  case Mips::BteqzT8CmpiX16: return Mips::BtnezT8CmpiX16;
  case Mips::BteqzT8SltuX16: return Mips::BtnezT8SltuX16;
  case Mips::BteqzT8SltiuX16: return Mips::BtnezT8SltiuX16;
  case Mips::BtnezT8CmpX16: return Mips::BteqzT8CmpX16;
  case Mips::BtnezT8SltX16: return Mips::BteqzT8SltX16;
  case Mips::BtnezT8SltiX16: return Mips::BteqzT8SltiX16;
  }
  llvm_unreachable("Illegal opcode!");
}

static void addSaveRestoreRegs(MachineInstrBuilder &MIB,
                               ArrayRef<CalleeSavedInfo> CSI,
                               unsigned Flags = 0) {
  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    // Add the callee-saved register as live-in. Do not add if the register is
    // RA and return address is taken, because it has already been added in
    // method MipsTargetLowering::lowerRETURNADDR.
    // It's killed at the spill, unless the register is RA and return address
    // is taken.
    Register Reg = CSI[e-i-1].getReg();
    switch (Reg) {
    case Mips::RA:
    case Mips::S0:
    case Mips::S1:
      MIB.addReg(Reg, Flags);
      break;
    case Mips::S2:
      break;
    default:
      llvm_unreachable("unexpected mips16 callee saved register");

    }
  }
}

// Adjust SP by FrameSize bytes. Save RA, S0, S1
void Mips16InstrInfo::makeFrame(unsigned SP, int64_t FrameSize,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const {
  DebugLoc DL;
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI    = MF.getFrameInfo();
  const BitVector Reserved = RI.getReservedRegs(MF);
  bool SaveS2 = Reserved[Mips::S2];
  MachineInstrBuilder MIB;
  unsigned Opc = ((FrameSize <= 128) && !SaveS2)? Mips::Save16:Mips::SaveX16;
  MIB = BuildMI(MBB, I, DL, get(Opc));
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  addSaveRestoreRegs(MIB, CSI);
  if (SaveS2)
    MIB.addReg(Mips::S2);
  if (isUInt<11>(FrameSize))
    MIB.addImm(FrameSize);
  else {
    int Base = 2040; // should create template function like isUInt that
                     // returns largest possible n bit unsigned integer
    int64_t Remainder = FrameSize - Base;
    MIB.addImm(Base);
    if (isInt<16>(-Remainder))
      BuildAddiuSpImm(MBB, I, -Remainder);
    else
      adjustStackPtrBig(SP, -Remainder, MBB, I, Mips::V0, Mips::V1);
  }
}

// Adjust SP by FrameSize bytes. Restore RA, S0, S1
void Mips16InstrInfo::restoreFrame(unsigned SP, int64_t FrameSize,
                                   MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  MachineFunction *MF = MBB.getParent();
  MachineFrameInfo &MFI    = MF->getFrameInfo();
  const BitVector Reserved = RI.getReservedRegs(*MF);
  bool SaveS2 = Reserved[Mips::S2];
  MachineInstrBuilder MIB;
  unsigned Opc = ((FrameSize <= 128) && !SaveS2)?
    Mips::Restore16:Mips::RestoreX16;

  if (!isUInt<11>(FrameSize)) {
    unsigned Base = 2040;
    int64_t Remainder = FrameSize - Base;
    FrameSize = Base; // should create template function like isUInt that
                     // returns largest possible n bit unsigned integer

    if (isInt<16>(Remainder))
      BuildAddiuSpImm(MBB, I, Remainder);
    else
      adjustStackPtrBig(SP, Remainder, MBB, I, Mips::A0, Mips::A1);
  }
  MIB = BuildMI(MBB, I, DL, get(Opc));
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  addSaveRestoreRegs(MIB, CSI, RegState::Define);
  if (SaveS2)
    MIB.addReg(Mips::S2, RegState::Define);
  MIB.addImm(FrameSize);
}

// Adjust SP by Amount bytes where bytes can be up to 32bit number.
// This can only be called at times that we know that there is at least one free
// register.
// This is clearly safe at prologue and epilogue.
void Mips16InstrInfo::adjustStackPtrBig(unsigned SP, int64_t Amount,
                                        MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        unsigned Reg1, unsigned Reg2) const {
  DebugLoc DL;
  //
  // li reg1, constant
  // move reg2, sp
  // add reg1, reg1, reg2
  // move sp, reg1
  //
  //
  MachineInstrBuilder MIB1 = BuildMI(MBB, I, DL, get(Mips::LwConstant32), Reg1);
  MIB1.addImm(Amount).addImm(-1);
  MachineInstrBuilder MIB2 = BuildMI(MBB, I, DL, get(Mips::MoveR3216), Reg2);
  MIB2.addReg(Mips::SP, RegState::Kill);
  MachineInstrBuilder MIB3 = BuildMI(MBB, I, DL, get(Mips::AdduRxRyRz16), Reg1);
  MIB3.addReg(Reg1);
  MIB3.addReg(Reg2, RegState::Kill);
  MachineInstrBuilder MIB4 = BuildMI(MBB, I, DL, get(Mips::Move32R16),
                                                     Mips::SP);
  MIB4.addReg(Reg1, RegState::Kill);
}

void Mips16InstrInfo::adjustStackPtrBigUnrestricted(
    unsigned SP, int64_t Amount, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
   llvm_unreachable("adjust stack pointer amount exceeded");
}

/// Adjust SP by Amount bytes.
void Mips16InstrInfo::adjustStackPtr(unsigned SP, int64_t Amount,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I) const {
  if (Amount == 0)
    return;

  if (isInt<16>(Amount))  // need to change to addiu sp, ....and isInt<16>
    BuildAddiuSpImm(MBB, I, Amount);
  else
    adjustStackPtrBigUnrestricted(SP, Amount, MBB, I);
}

/// This function generates the sequence of instructions needed to get the
/// result of adding register REG and immediate IMM.
unsigned Mips16InstrInfo::loadImmediate(unsigned FrameReg, int64_t Imm,
                                        MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator II,
                                        const DebugLoc &DL,
                                        unsigned &NewImm) const {
  //
  // given original instruction is:
  // Instr rx, T[offset] where offset is too big.
  //
  // lo = offset & 0xFFFF
  // hi = ((offset >> 16) + (lo >> 15)) & 0xFFFF;
  //
  // let T = temporary register
  // li T, hi
  // shl T, 16
  // add T, Rx, T
  //
  RegScavenger rs;
  int32_t lo = Imm & 0xFFFF;
  NewImm = lo;
  int Reg =0;
  int SpReg = 0;

  rs.enterBasicBlockEnd(MBB);
  rs.backward(std::next(II));
  //
  // We need to know which registers can be used, in the case where there
  // are not enough free registers. We exclude all registers that
  // are used in the instruction that we are helping.
  //  // Consider all allocatable registers in the register class initially
  BitVector Candidates =
      RI.getAllocatableSet
      (*II->getParent()->getParent(), &Mips::CPU16RegsRegClass);
  // Exclude all the registers being used by the instruction.
  for (MachineOperand &MO : II->operands()) {
    if (MO.isReg() && MO.getReg() != 0 && !MO.isDef() &&
        !MO.getReg().isVirtual())
      Candidates.reset(MO.getReg());
  }

  // If the same register was used and defined in an instruction, then
  // it will not be in the list of candidates.
  //
  // we need to analyze the instruction that we are helping.
  // we need to know if it defines register x but register x is not
  // present as an operand of the instruction. this tells
  // whether the register is live before the instruction. if it's not
  // then we don't need to save it in case there are no free registers.
  int DefReg = 0;
  for (MachineOperand &MO : II->operands()) {
    if (MO.isReg() && MO.isDef()) {
      DefReg = MO.getReg();
      break;
    }
  }

  BitVector Available = rs.getRegsAvailable(&Mips::CPU16RegsRegClass);
  Available &= Candidates;
  //
  // we use T0 for the first register, if we need to save something away.
  // we use T1 for the second register, if we need to save something away.
  //
  unsigned FirstRegSaved =0, SecondRegSaved=0;
  unsigned FirstRegSavedTo = 0, SecondRegSavedTo = 0;

  Reg = Available.find_first();

  if (Reg == -1) {
    Reg = Candidates.find_first();
    Candidates.reset(Reg);
    if (DefReg != Reg) {
      FirstRegSaved = Reg;
      FirstRegSavedTo = Mips::T0;
      copyPhysReg(MBB, II, DL, FirstRegSavedTo, FirstRegSaved, true);
    }
  }
  else
    Available.reset(Reg);
  BuildMI(MBB, II, DL, get(Mips::LwConstant32), Reg).addImm(Imm).addImm(-1);
  NewImm = 0;
  if (FrameReg == Mips::SP) {
    SpReg = Available.find_first();
    if (SpReg == -1) {
      SpReg = Candidates.find_first();
      // Candidates.reset(SpReg); // not really needed
      if (DefReg!= SpReg) {
        SecondRegSaved = SpReg;
        SecondRegSavedTo = Mips::T1;
      }
      if (SecondRegSaved)
        copyPhysReg(MBB, II, DL, SecondRegSavedTo, SecondRegSaved, true);
    }
   else
     Available.reset(SpReg);
    copyPhysReg(MBB, II, DL, SpReg, Mips::SP, false);
    BuildMI(MBB, II, DL, get(Mips::AdduRxRyRz16), Reg)
        .addReg(SpReg, RegState::Kill)
        .addReg(Reg);
  }
  else
    BuildMI(MBB, II, DL, get(Mips::  AdduRxRyRz16), Reg).addReg(FrameReg)
      .addReg(Reg, RegState::Kill);
  if (FirstRegSaved || SecondRegSaved) {
    II = std::next(II);
    if (FirstRegSaved)
      copyPhysReg(MBB, II, DL, FirstRegSaved, FirstRegSavedTo, true);
    if (SecondRegSaved)
      copyPhysReg(MBB, II, DL, SecondRegSaved, SecondRegSavedTo, true);
  }
  return Reg;
}

unsigned Mips16InstrInfo::getAnalyzableBrOpc(unsigned Opc) const {
  return (Opc == Mips::BeqzRxImmX16   || Opc == Mips::BimmX16  ||
          Opc == Mips::Bimm16  ||
          Opc == Mips::Bteqz16        || Opc == Mips::Btnez16 ||
          Opc == Mips::BeqzRxImm16    || Opc == Mips::BnezRxImm16   ||
          Opc == Mips::BnezRxImmX16   || Opc == Mips::BteqzX16 ||
          Opc == Mips::BteqzT8CmpX16  || Opc == Mips::BteqzT8CmpiX16 ||
          Opc == Mips::BteqzT8SltX16  || Opc == Mips::BteqzT8SltuX16  ||
          Opc == Mips::BteqzT8SltiX16 || Opc == Mips::BteqzT8SltiuX16 ||
          Opc == Mips::BtnezX16       || Opc == Mips::BtnezT8CmpX16 ||
          Opc == Mips::BtnezT8CmpiX16 || Opc == Mips::BtnezT8SltX16 ||
          Opc == Mips::BtnezT8SltuX16 || Opc == Mips::BtnezT8SltiX16 ||
          Opc == Mips::BtnezT8SltiuX16 ) ? Opc : 0;
}

void Mips16InstrInfo::ExpandRetRA16(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  unsigned Opc) const {
  BuildMI(MBB, I, I->getDebugLoc(), get(Opc));
}

const MCInstrDesc &Mips16InstrInfo::AddiuSpImm(int64_t Imm) const {
  if (validSpImm8(Imm))
    return get(Mips::AddiuSpImm16);
  else
    return get(Mips::AddiuSpImmX16);
}

void Mips16InstrInfo::BuildAddiuSpImm
  (MachineBasicBlock &MBB, MachineBasicBlock::iterator I, int64_t Imm) const {
  DebugLoc DL;
  BuildMI(MBB, I, DL, AddiuSpImm(Imm)).addImm(Imm);
}

const MipsInstrInfo *llvm::createMips16InstrInfo(const MipsSubtarget &STI) {
  return new Mips16InstrInfo(STI);
}

bool Mips16InstrInfo::validImmediate(unsigned Opcode, unsigned Reg,
                                     int64_t Amount) {
  switch (Opcode) {
  case Mips::LbRxRyOffMemX16:
  case Mips::LbuRxRyOffMemX16:
  case Mips::LhRxRyOffMemX16:
  case Mips::LhuRxRyOffMemX16:
  case Mips::SbRxRyOffMemX16:
  case Mips::ShRxRyOffMemX16:
  case Mips::LwRxRyOffMemX16:
  case Mips::SwRxRyOffMemX16:
  case Mips::SwRxSpImmX16:
  case Mips::LwRxSpImmX16:
    return isInt<16>(Amount);
  case Mips::AddiuRxRyOffMemX16:
    if ((Reg == Mips::PC) || (Reg == Mips::SP))
      return isInt<16>(Amount);
    return isInt<15>(Amount);
  }
  llvm_unreachable("unexpected Opcode in validImmediate");
}
