//===- SparcDisassembler.cpp - Disassembler for Sparc -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the Sparc Disassembler.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SparcMCTargetDesc.h"
#include "TargetInfo/SparcTargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "sparc-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for Sparc.
class SparcDisassembler : public MCDisassembler {
public:
  SparcDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  virtual ~SparcDisassembler() = default;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
}

static MCDisassembler *createSparcDisassembler(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx) {
  return new SparcDisassembler(STI, Ctx);
}


extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSparcDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheSparcTarget(),
                                         createSparcDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheSparcV9Target(),
                                         createSparcDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheSparcelTarget(),
                                         createSparcDisassembler);
}

static const unsigned IntRegDecoderTable[] = {
  SP::G0,  SP::G1,  SP::G2,  SP::G3,
  SP::G4,  SP::G5,  SP::G6,  SP::G7,
  SP::O0,  SP::O1,  SP::O2,  SP::O3,
  SP::O4,  SP::O5,  SP::O6,  SP::O7,
  SP::L0,  SP::L1,  SP::L2,  SP::L3,
  SP::L4,  SP::L5,  SP::L6,  SP::L7,
  SP::I0,  SP::I1,  SP::I2,  SP::I3,
  SP::I4,  SP::I5,  SP::I6,  SP::I7 };

static const unsigned FPRegDecoderTable[] = {
  SP::F0,   SP::F1,   SP::F2,   SP::F3,
  SP::F4,   SP::F5,   SP::F6,   SP::F7,
  SP::F8,   SP::F9,   SP::F10,  SP::F11,
  SP::F12,  SP::F13,  SP::F14,  SP::F15,
  SP::F16,  SP::F17,  SP::F18,  SP::F19,
  SP::F20,  SP::F21,  SP::F22,  SP::F23,
  SP::F24,  SP::F25,  SP::F26,  SP::F27,
  SP::F28,  SP::F29,  SP::F30,  SP::F31 };

static const unsigned DFPRegDecoderTable[] = {
  SP::D0,   SP::D16,  SP::D1,   SP::D17,
  SP::D2,   SP::D18,  SP::D3,   SP::D19,
  SP::D4,   SP::D20,  SP::D5,   SP::D21,
  SP::D6,   SP::D22,  SP::D7,   SP::D23,
  SP::D8,   SP::D24,  SP::D9,   SP::D25,
  SP::D10,  SP::D26,  SP::D11,  SP::D27,
  SP::D12,  SP::D28,  SP::D13,  SP::D29,
  SP::D14,  SP::D30,  SP::D15,  SP::D31 };

static const unsigned QFPRegDecoderTable[] = {
  SP::Q0,  SP::Q8,   ~0U,  ~0U,
  SP::Q1,  SP::Q9,   ~0U,  ~0U,
  SP::Q2,  SP::Q10,  ~0U,  ~0U,
  SP::Q3,  SP::Q11,  ~0U,  ~0U,
  SP::Q4,  SP::Q12,  ~0U,  ~0U,
  SP::Q5,  SP::Q13,  ~0U,  ~0U,
  SP::Q6,  SP::Q14,  ~0U,  ~0U,
  SP::Q7,  SP::Q15,  ~0U,  ~0U } ;

static const unsigned FCCRegDecoderTable[] = {
  SP::FCC0, SP::FCC1, SP::FCC2, SP::FCC3 };

static const unsigned ASRRegDecoderTable[] = {
    SP::Y,     SP::ASR1,  SP::ASR2,  SP::ASR3,  SP::ASR4,  SP::ASR5,  SP::ASR6,
    SP::ASR7,  SP::ASR8,  SP::ASR9,  SP::ASR10, SP::ASR11, SP::ASR12, SP::ASR13,
    SP::ASR14, SP::ASR15, SP::ASR16, SP::ASR17, SP::ASR18, SP::ASR19, SP::ASR20,
    SP::ASR21, SP::ASR22, SP::ASR23, SP::ASR24, SP::ASR25, SP::ASR26, SP::ASR27,
    SP::ASR28, SP::ASR29, SP::ASR30, SP::ASR31};

static const unsigned PRRegDecoderTable[] = {
    SP::TPC,     SP::TNPC,       SP::TSTATE,   SP::TT,       SP::TICK,
    SP::TBA,     SP::PSTATE,     SP::TL,       SP::PIL,      SP::CWP,
    SP::CANSAVE, SP::CANRESTORE, SP::CLEANWIN, SP::OTHERWIN, SP::WSTATE};

static const uint16_t IntPairDecoderTable[] = {
  SP::G0_G1, SP::G2_G3, SP::G4_G5, SP::G6_G7,
  SP::O0_O1, SP::O2_O3, SP::O4_O5, SP::O6_O7,
  SP::L0_L1, SP::L2_L3, SP::L4_L5, SP::L6_L7,
  SP::I0_I1, SP::I2_I3, SP::I4_I5, SP::I6_I7,
};

static const unsigned CPRegDecoderTable[] = {
  SP::C0,  SP::C1,  SP::C2,  SP::C3,
  SP::C4,  SP::C5,  SP::C6,  SP::C7,
  SP::C8,  SP::C9,  SP::C10, SP::C11,
  SP::C12, SP::C13, SP::C14, SP::C15,
  SP::C16, SP::C17, SP::C18, SP::C19,
  SP::C20, SP::C21, SP::C22, SP::C23,
  SP::C24, SP::C25, SP::C26, SP::C27,
  SP::C28, SP::C29, SP::C30, SP::C31
};


static const uint16_t CPPairDecoderTable[] = {
  SP::C0_C1,   SP::C2_C3,   SP::C4_C5,   SP::C6_C7,
  SP::C8_C9,   SP::C10_C11, SP::C12_C13, SP::C14_C15,
  SP::C16_C17, SP::C18_C19, SP::C20_C21, SP::C22_C23,
  SP::C24_C25, SP::C26_C27, SP::C28_C29, SP::C30_C31
};

static DecodeStatus DecodeIntRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;
  unsigned Reg = IntRegDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeI64RegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return DecodeIntRegsRegisterClass(Inst, RegNo, Address, Decoder);
}

// This is used for the type "ptr_rc", which is either IntRegs or I64Regs
// depending on SparcRegisterInfo::getPointerRegClass.
static DecodeStatus DecodePointerLikeRegClass0(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  return DecodeIntRegsRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeFPRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;
  unsigned Reg = FPRegDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeDFPRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;
  unsigned Reg = DFPRegDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeQFPRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;

  unsigned Reg = QFPRegDecoderTable[RegNo];
  if (Reg == ~0U)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeCoprocRegsRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                              const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;
  unsigned Reg = CPRegDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFCCRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 3)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(FCCRegDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeASRRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(ASRRegDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodePRRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo >= std::size(PRRegDecoderTable))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(PRRegDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeIntPairRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (RegNo > 31)
    return MCDisassembler::Fail;

  if ((RegNo & 1))
    S = MCDisassembler::SoftFail;

  unsigned RegisterPair = IntPairDecoderTable[RegNo/2];
  Inst.addOperand(MCOperand::createReg(RegisterPair));
  return S;
}

static DecodeStatus
DecodeCoprocPairRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                              const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;

  unsigned RegisterPair = CPPairDecoderTable[RegNo/2];
  Inst.addOperand(MCOperand::createReg(RegisterPair));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeCall(MCInst &Inst, unsigned insn, uint64_t Address,
                               const MCDisassembler *Decoder);
static DecodeStatus DecodeSIMM13(MCInst &Inst, unsigned insn, uint64_t Address,
                                 const MCDisassembler *Decoder);

#include "SparcGenDisassemblerTables.inc"

/// Read four bytes from the ArrayRef and return 32 bit word.
static DecodeStatus readInstruction32(ArrayRef<uint8_t> Bytes, uint64_t Address,
                                      uint64_t &Size, uint32_t &Insn,
                                      bool IsLittleEndian) {
  // We want to read exactly 4 Bytes of data.
  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  Insn = IsLittleEndian
             ? (Bytes[0] << 0) | (Bytes[1] << 8) | (Bytes[2] << 16) |
                   (Bytes[3] << 24)
             : (Bytes[3] << 0) | (Bytes[2] << 8) | (Bytes[1] << 16) |
                   (Bytes[0] << 24);

  return MCDisassembler::Success;
}

DecodeStatus SparcDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                               ArrayRef<uint8_t> Bytes,
                                               uint64_t Address,
                                               raw_ostream &CStream) const {
  uint32_t Insn;
  bool isLittleEndian = getContext().getAsmInfo()->isLittleEndian();
  DecodeStatus Result =
      readInstruction32(Bytes, Address, Size, Insn, isLittleEndian);
  if (Result == MCDisassembler::Fail)
    return MCDisassembler::Fail;

  // Calling the auto-generated decoder function.

  if (STI.hasFeature(Sparc::FeatureV9))
  {
    Result = decodeInstruction(DecoderTableSparcV932, Instr, Insn, Address, this, STI);
  }
  else
  {
    Result = decodeInstruction(DecoderTableSparcV832, Instr, Insn, Address, this, STI);
  }
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    return Result;
  }

  Result =
      decodeInstruction(DecoderTableSparc32, Instr, Insn, Address, this, STI);

  if (Result != MCDisassembler::Fail) {
    Size = 4;
    return Result;
  }

  return MCDisassembler::Fail;
}

static bool tryAddingSymbolicOperand(int64_t Value, bool isBranch,
                                     uint64_t Address, uint64_t Offset,
                                     uint64_t Width, MCInst &MI,
                                     const MCDisassembler *Decoder) {
  return Decoder->tryAddingSymbolicOperand(MI, Value, Address, isBranch, Offset,
                                           Width, /*InstSize=*/4);
}

static DecodeStatus DecodeCall(MCInst &MI, unsigned insn, uint64_t Address,
                               const MCDisassembler *Decoder) {
  unsigned tgt = fieldFromInstruction(insn, 0, 30);
  tgt <<= 2;
  if (!tryAddingSymbolicOperand(tgt+Address, false, Address,
                                0, 30, MI, Decoder))
    MI.addOperand(MCOperand::createImm(tgt));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeSIMM13(MCInst &MI, unsigned insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  assert(isUInt<13>(insn));
  MI.addOperand(MCOperand::createImm(SignExtend64<13>(insn)));
  return MCDisassembler::Success;
}
