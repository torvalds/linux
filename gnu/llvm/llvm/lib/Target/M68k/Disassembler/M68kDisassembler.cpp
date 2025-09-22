//===-- M68kDisassembler.cpp - Disassembler for M68k ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the M68k Disassembler.
//
//===----------------------------------------------------------------------===//

#include "M68k.h"
#include "M68kRegisterInfo.h"
#include "M68kSubtarget.h"
#include "MCTargetDesc/M68kMCCodeEmitter.h"
#include "MCTargetDesc/M68kMCTargetDesc.h"
#include "TargetInfo/M68kTargetInfo.h"

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "m68k-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

static const unsigned RegisterDecode[] = {
    M68k::D0,    M68k::D1,  M68k::D2,  M68k::D3,  M68k::D4,  M68k::D5,
    M68k::D6,    M68k::D7,  M68k::A0,  M68k::A1,  M68k::A2,  M68k::A3,
    M68k::A4,    M68k::A5,  M68k::A6,  M68k::SP,  M68k::FP0, M68k::FP1,
    M68k::FP2,   M68k::FP3, M68k::FP4, M68k::FP5, M68k::FP6, M68k::FP7,
    M68k::FPIAR, M68k::FPS, M68k::FPC};

static DecodeStatus DecodeRegisterClass(MCInst &Inst, uint64_t RegNo,
                                        uint64_t Address, const void *Decoder) {
  if (RegNo >= 24)
    return DecodeStatus::Fail;
  Inst.addOperand(MCOperand::createReg(RegisterDecode[RegNo]));
  return DecodeStatus::Success;
}

static DecodeStatus DecodeDR32RegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeDR16RegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeDR8RegisterClass(MCInst &Inst, uint64_t RegNo,
                                           uint64_t Address,
                                           const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeAR32RegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo | 8ULL, Address, Decoder);
}

static DecodeStatus DecodeAR16RegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo | 8ULL, Address, Decoder);
}

static DecodeStatus DecodeXR32RegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeXR16RegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeFPDRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  return DecodeRegisterClass(Inst, RegNo | 16ULL, Address, Decoder);
}
#define DecodeFPDR32RegisterClass DecodeFPDRRegisterClass
#define DecodeFPDR64RegisterClass DecodeFPDRRegisterClass
#define DecodeFPDR80RegisterClass DecodeFPDRRegisterClass

static DecodeStatus DecodeFPCSCRegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const void *Decoder) {
  return DecodeRegisterClass(Inst, (RegNo >> 1) + 24, Address, Decoder);
}
#define DecodeFPICRegisterClass DecodeFPCSCRegisterClass

static DecodeStatus DecodeCCRCRegisterClass(MCInst &Inst, APInt &Insn,
                                            uint64_t Address,
                                            const void *Decoder) {
  llvm_unreachable("unimplemented");
}

static DecodeStatus DecodeImm32(MCInst &Inst, uint64_t Imm, uint64_t Address,
                                const void *Decoder) {
  Inst.addOperand(MCOperand::createImm(M68k::swapWord<uint32_t>(Imm)));
  return DecodeStatus::Success;
}

#include "M68kGenDisassemblerTable.inc"

#undef DecodeFPDR32RegisterClass
#undef DecodeFPDR64RegisterClass
#undef DecodeFPDR80RegisterClass
#undef DecodeFPICRegisterClass

/// A disassembler class for M68k.
struct M68kDisassembler : public MCDisassembler {
  M68kDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  virtual ~M68kDisassembler() {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

DecodeStatus M68kDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                              ArrayRef<uint8_t> Bytes,
                                              uint64_t Address,
                                              raw_ostream &CStream) const {
  DecodeStatus Result;
  auto MakeUp = [&](APInt &Insn, unsigned InstrBits) {
    unsigned Idx = Insn.getBitWidth() >> 3;
    unsigned RoundUp = alignTo(InstrBits, Align(16));
    if (RoundUp > Insn.getBitWidth())
      Insn = Insn.zext(RoundUp);
    RoundUp = RoundUp >> 3;
    for (; Idx < RoundUp; Idx += 2) {
      Insn.insertBits(support::endian::read16be(&Bytes[Idx]), Idx * 8, 16);
    }
  };
  APInt Insn(16, support::endian::read16be(Bytes.data()));
  // 2 bytes of data are consumed, so set Size to 2
  // If we don't do this, disassembler may generate result even
  // the encoding is invalid. We need to let it fail correctly.
  Size = 2;
  Result = decodeInstruction(DecoderTable80, Instr, Insn, Address, this, STI,
                             MakeUp);
  if (Result == DecodeStatus::Success)
    Size = InstrLenTable[Instr.getOpcode()] >> 3;
  return Result;
}

static MCDisassembler *createM68kDisassembler(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              MCContext &Ctx) {
  return new M68kDisassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM68kDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheM68kTarget(),
                                         createM68kDisassembler);
}
