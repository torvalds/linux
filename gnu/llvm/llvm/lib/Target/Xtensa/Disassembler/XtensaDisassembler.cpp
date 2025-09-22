//===-- XtensaDisassembler.cpp - Disassembler for Xtensa ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the XtensaDisassembler class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/XtensaMCTargetDesc.h"
#include "TargetInfo/XtensaTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

#define DEBUG_TYPE "Xtensa-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

class XtensaDisassembler : public MCDisassembler {
  bool IsLittleEndian;

public:
  XtensaDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx, bool isLE)
      : MCDisassembler(STI, Ctx), IsLittleEndian(isLE) {}

  bool hasDensity() const {
    return STI.hasFeature(Xtensa::FeatureDensity);
  }

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
} // end anonymous namespace

static MCDisassembler *createXtensaDisassembler(const Target &T,
                                                const MCSubtargetInfo &STI,
                                                MCContext &Ctx) {
  return new XtensaDisassembler(STI, Ctx, true);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeXtensaDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheXtensaTarget(),
                                         createXtensaDisassembler);
}

static const unsigned ARDecoderTable[] = {
    Xtensa::A0,  Xtensa::SP,  Xtensa::A2,  Xtensa::A3, Xtensa::A4,  Xtensa::A5,
    Xtensa::A6,  Xtensa::A7,  Xtensa::A8,  Xtensa::A9, Xtensa::A10, Xtensa::A11,
    Xtensa::A12, Xtensa::A13, Xtensa::A14, Xtensa::A15};

static DecodeStatus DecodeARRegisterClass(MCInst &Inst, uint64_t RegNo,
                                          uint64_t Address,
                                          const void *Decoder) {
  if (RegNo >= std::size(ARDecoderTable))
    return MCDisassembler::Fail;

  unsigned Reg = ARDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static const unsigned SRDecoderTable[] = {Xtensa::SAR, 3};

static DecodeStatus DecodeSRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                          uint64_t Address,
                                          const void *Decoder) {
  if (RegNo > 255)
    return MCDisassembler::Fail;

  for (unsigned i = 0; i < std::size(SRDecoderTable); i += 2) {
    if (SRDecoderTable[i + 1] == RegNo) {
      unsigned Reg = SRDecoderTable[i];
      Inst.addOperand(MCOperand::createReg(Reg));
      return MCDisassembler::Success;
    }
  }

  return MCDisassembler::Fail;
}

static bool tryAddingSymbolicOperand(int64_t Value, bool isBranch,
                                     uint64_t Address, uint64_t Offset,
                                     uint64_t InstSize, MCInst &MI,
                                     const void *Decoder) {
  const MCDisassembler *Dis = static_cast<const MCDisassembler *>(Decoder);
  return Dis->tryAddingSymbolicOperand(MI, Value, Address, isBranch, Offset, /*OpSize=*/0,
                                       InstSize);
}

static DecodeStatus decodeCallOperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address, const void *Decoder) {
  assert(isUInt<18>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(SignExtend64<20>(Imm << 2)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeJumpOperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address, const void *Decoder) {
  assert(isUInt<18>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(SignExtend64<18>(Imm)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeBranchOperand(MCInst &Inst, uint64_t Imm,
                                        int64_t Address, const void *Decoder) {
  switch (Inst.getOpcode()) {
  case Xtensa::BEQZ:
  case Xtensa::BGEZ:
  case Xtensa::BLTZ:
  case Xtensa::BNEZ:
    assert(isUInt<12>(Imm) && "Invalid immediate");
    if (!tryAddingSymbolicOperand(SignExtend64<12>(Imm) + 4 + Address, true,
                                  Address, 0, 3, Inst, Decoder))
      Inst.addOperand(MCOperand::createImm(SignExtend64<12>(Imm)));
    break;
  default:
    assert(isUInt<8>(Imm) && "Invalid immediate");
    if (!tryAddingSymbolicOperand(SignExtend64<8>(Imm) + 4 + Address, true,
                                  Address, 0, 3, Inst, Decoder))
      Inst.addOperand(MCOperand::createImm(SignExtend64<8>(Imm)));
  }
  return MCDisassembler::Success;
}

static DecodeStatus decodeL32ROperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address, const void *Decoder) {

  assert(isUInt<16>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(
      SignExtend64<17>((Imm << 2) + 0x40000 + (Address & 0x3))));
  return MCDisassembler::Success;
}

static DecodeStatus decodeImm8Operand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address, const void *Decoder) {
  assert(isUInt<8>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(SignExtend64<8>(Imm)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeImm8_sh8Operand(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const void *Decoder) {
  assert(isUInt<8>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(SignExtend64<16>(Imm << 8)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeImm12Operand(MCInst &Inst, uint64_t Imm,
                                       int64_t Address, const void *Decoder) {
  assert(isUInt<12>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(SignExtend64<12>(Imm)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeUimm4Operand(MCInst &Inst, uint64_t Imm,
                                       int64_t Address, const void *Decoder) {
  assert(isUInt<4>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeUimm5Operand(MCInst &Inst, uint64_t Imm,
                                       int64_t Address, const void *Decoder) {
  assert(isUInt<5>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeImm1_16Operand(MCInst &Inst, uint64_t Imm,
                                         int64_t Address, const void *Decoder) {
  assert(isUInt<4>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(Imm + 1));
  return MCDisassembler::Success;
}

static DecodeStatus decodeShimm1_31Operand(MCInst &Inst, uint64_t Imm,
                                           int64_t Address,
                                           const void *Decoder) {
  assert(isUInt<5>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(32 - Imm));
  return MCDisassembler::Success;
}

static int64_t TableB4const[16] = {-1, 1,  2,  3,  4,  5,  6,   7,
                                   8,  10, 12, 16, 32, 64, 128, 256};
static DecodeStatus decodeB4constOperand(MCInst &Inst, uint64_t Imm,
                                         int64_t Address, const void *Decoder) {
  assert(isUInt<4>(Imm) && "Invalid immediate");

  Inst.addOperand(MCOperand::createImm(TableB4const[Imm]));
  return MCDisassembler::Success;
}

static int64_t TableB4constu[16] = {32768, 65536, 2,  3,  4,  5,  6,   7,
                                    8,     10,    12, 16, 32, 64, 128, 256};
static DecodeStatus decodeB4constuOperand(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const void *Decoder) {
  assert(isUInt<4>(Imm) && "Invalid immediate");

  Inst.addOperand(MCOperand::createImm(TableB4constu[Imm]));
  return MCDisassembler::Success;
}

static DecodeStatus decodeMem8Operand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address, const void *Decoder) {
  assert(isUInt<12>(Imm) && "Invalid immediate");
  DecodeARRegisterClass(Inst, Imm & 0xf, Address, Decoder);
  Inst.addOperand(MCOperand::createImm((Imm >> 4) & 0xff));
  return MCDisassembler::Success;
}

static DecodeStatus decodeMem16Operand(MCInst &Inst, uint64_t Imm,
                                       int64_t Address, const void *Decoder) {
  assert(isUInt<12>(Imm) && "Invalid immediate");
  DecodeARRegisterClass(Inst, Imm & 0xf, Address, Decoder);
  Inst.addOperand(MCOperand::createImm((Imm >> 3) & 0x1fe));
  return MCDisassembler::Success;
}

static DecodeStatus decodeMem32Operand(MCInst &Inst, uint64_t Imm,
                                       int64_t Address, const void *Decoder) {
  assert(isUInt<12>(Imm) && "Invalid immediate");
  DecodeARRegisterClass(Inst, Imm & 0xf, Address, Decoder);
  Inst.addOperand(MCOperand::createImm((Imm >> 2) & 0x3fc));
  return MCDisassembler::Success;
}

/// Read three bytes from the ArrayRef and return 24 bit data
static DecodeStatus readInstruction24(ArrayRef<uint8_t> Bytes, uint64_t Address,
                                      uint64_t &Size, uint32_t &Insn,
                                      bool IsLittleEndian) {
  // We want to read exactly 3 Bytes of data.
  if (Bytes.size() < 3) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  if (!IsLittleEndian) {
    report_fatal_error("Big-endian mode currently is not supported!");
  } else {
    Insn = (Bytes[2] << 16) | (Bytes[1] << 8) | (Bytes[0] << 0);
  }

  Size = 3;
  return MCDisassembler::Success;
}

#include "XtensaGenDisassemblerTables.inc"

DecodeStatus XtensaDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address,
                                                raw_ostream &CS) const {
  uint32_t Insn;
  DecodeStatus Result;

  Result = readInstruction24(Bytes, Address, Size, Insn, IsLittleEndian);
  if (Result == MCDisassembler::Fail)
    return MCDisassembler::Fail;
  LLVM_DEBUG(dbgs() << "Trying Xtensa 24-bit instruction table :\n");
  Result = decodeInstruction(DecoderTable24, MI, Insn, Address, this, STI);
  return Result;
}
