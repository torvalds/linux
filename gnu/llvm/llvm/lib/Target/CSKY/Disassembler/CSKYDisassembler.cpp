//===-- CSKYDisassembler.cpp - Disassembler for CSKY ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CSKYDisassembler class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/CSKYBaseInfo.h"
#include "MCTargetDesc/CSKYMCTargetDesc.h"
#include "TargetInfo/CSKYTargetInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

#define DEBUG_TYPE "csky-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class CSKYDisassembler : public MCDisassembler {
  std::unique_ptr<MCInstrInfo const> const MCII;
  mutable StringRef symbolName;

  DecodeStatus handleCROperand(MCInst &Instr) const;

public:
  CSKYDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                   MCInstrInfo const *MCII);

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
} // end anonymous namespace

CSKYDisassembler::CSKYDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                                   MCInstrInfo const *MCII)
    : MCDisassembler(STI, Ctx), MCII(MCII) {}

static MCDisassembler *createCSKYDisassembler(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              MCContext &Ctx) {
  return new CSKYDisassembler(STI, Ctx, T.createMCInstrInfo());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeCSKYDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheCSKYTarget(),
                                         createCSKYDisassembler);
}

static const uint16_t GPRDecoderTable[] = {
    CSKY::R0,  CSKY::R1,  CSKY::R2,  CSKY::R3,  CSKY::R4,  CSKY::R5,  CSKY::R6,
    CSKY::R7,  CSKY::R8,  CSKY::R9,  CSKY::R10, CSKY::R11, CSKY::R12, CSKY::R13,
    CSKY::R14, CSKY::R15, CSKY::R16, CSKY::R17, CSKY::R18, CSKY::R19, CSKY::R20,
    CSKY::R21, CSKY::R22, CSKY::R23, CSKY::R24, CSKY::R25, CSKY::R26, CSKY::R27,
    CSKY::R28, CSKY::R29, CSKY::R30, CSKY::R31};

static const uint16_t GPRPairDecoderTable[] = {
    CSKY::R0_R1,   CSKY::R1_R2,   CSKY::R2_R3,   CSKY::R3_R4,   CSKY::R4_R5,
    CSKY::R5_R6,   CSKY::R6_R7,   CSKY::R7_R8,   CSKY::R8_R9,   CSKY::R9_R10,
    CSKY::R10_R11, CSKY::R11_R12, CSKY::R12_R13, CSKY::R13_R14, CSKY::R14_R15,
    CSKY::R15_R16, CSKY::R16_R17, CSKY::R17_R18, CSKY::R18_R19, CSKY::R19_R20,
    CSKY::R20_R21, CSKY::R21_R22, CSKY::R22_R23, CSKY::R23_R24, CSKY::R24_R25,
    CSKY::R25_R26, CSKY::R26_R27, CSKY::R27_R28, CSKY::R28_R29, CSKY::R29_R30,
    CSKY::R30_R31, CSKY::R31_R32};

static const uint16_t FPR32DecoderTable[] = {
    CSKY::F0_32,  CSKY::F1_32,  CSKY::F2_32,  CSKY::F3_32,  CSKY::F4_32,
    CSKY::F5_32,  CSKY::F6_32,  CSKY::F7_32,  CSKY::F8_32,  CSKY::F9_32,
    CSKY::F10_32, CSKY::F11_32, CSKY::F12_32, CSKY::F13_32, CSKY::F14_32,
    CSKY::F15_32, CSKY::F16_32, CSKY::F17_32, CSKY::F18_32, CSKY::F19_32,
    CSKY::F20_32, CSKY::F21_32, CSKY::F22_32, CSKY::F23_32, CSKY::F24_32,
    CSKY::F25_32, CSKY::F26_32, CSKY::F27_32, CSKY::F28_32, CSKY::F29_32,
    CSKY::F30_32, CSKY::F31_32};

static const uint16_t FPR64DecoderTable[] = {
    CSKY::F0_64,  CSKY::F1_64,  CSKY::F2_64,  CSKY::F3_64,  CSKY::F4_64,
    CSKY::F5_64,  CSKY::F6_64,  CSKY::F7_64,  CSKY::F8_64,  CSKY::F9_64,
    CSKY::F10_64, CSKY::F11_64, CSKY::F12_64, CSKY::F13_64, CSKY::F14_64,
    CSKY::F15_64, CSKY::F16_64, CSKY::F17_64, CSKY::F18_64, CSKY::F19_64,
    CSKY::F20_64, CSKY::F21_64, CSKY::F22_64, CSKY::F23_64, CSKY::F24_64,
    CSKY::F25_64, CSKY::F26_64, CSKY::F27_64, CSKY::F28_64, CSKY::F29_64,
    CSKY::F30_64, CSKY::F31_64};

static const uint16_t FPR128DecoderTable[] = {
    CSKY::F0_128,  CSKY::F1_128,  CSKY::F2_128,  CSKY::F3_128,  CSKY::F4_128,
    CSKY::F5_128,  CSKY::F6_128,  CSKY::F7_128,  CSKY::F8_128,  CSKY::F9_128,
    CSKY::F10_128, CSKY::F11_128, CSKY::F12_128, CSKY::F13_128, CSKY::F14_128,
    CSKY::F15_128, CSKY::F16_128, CSKY::F17_128, CSKY::F18_128, CSKY::F19_128,
    CSKY::F20_128, CSKY::F21_128, CSKY::F22_128, CSKY::F23_128, CSKY::F24_128,
    CSKY::F25_128, CSKY::F26_128, CSKY::F27_128, CSKY::F28_128, CSKY::F29_128,
    CSKY::F30_128, CSKY::F31_128};

static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo >= 32)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(GPRDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR32RegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo >= 32)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR32DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodesFPR32RegisterClass(MCInst &Inst, uint64_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR32DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodesFPR64RegisterClass(MCInst &Inst, uint64_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR64DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodesFPR64_VRegisterClass(MCInst &Inst, uint64_t RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR64DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR64RegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo >= 32)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR64DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

// TODO
LLVM_ATTRIBUTE_UNUSED
static DecodeStatus DecodesFPR128RegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR128DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodesGPRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(GPRDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodemGPRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo >= 8)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(GPRDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

// TODO
LLVM_ATTRIBUTE_UNUSED
static DecodeStatus DecodeGPRSPRegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo != 14)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(GPRDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRPairRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  const FeatureBitset &FeatureBits =
      Decoder->getSubtargetInfo().getFeatureBits();
  bool hasHighReg = FeatureBits[CSKY::FeatureHighreg];

  if (RegNo >= 32 || (!hasHighReg && RegNo >= 16))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(GPRPairDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

template <unsigned N, unsigned S>
static DecodeStatus decodeUImmOperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address,
                                      const MCDisassembler *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(Imm << S));
  return MCDisassembler::Success;
}

template <unsigned N>
static DecodeStatus decodeOImmOperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address,
                                      const MCDisassembler *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(Imm + 1));
  return MCDisassembler::Success;
}

static DecodeStatus decodeLRW16Imm8(MCInst &Inst, uint64_t Imm, int64_t Address,
                                    const MCDisassembler *Decoder) {
  assert(isUInt<8>(Imm) && "Invalid immediate");
  if ((Imm >> 7) & 0x1) {
    Inst.addOperand(MCOperand::createImm((Imm & 0x7F) << 2));
  } else {
    uint64_t V = ((Imm ^ 0xFFFFFFFF) & 0xFF);
    Inst.addOperand(MCOperand::createImm(V << 2));
  }

  return MCDisassembler::Success;
}

static DecodeStatus decodeJMPIXImmOperand(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const MCDisassembler *Decoder) {
  assert(isUInt<2>(Imm) && "Invalid immediate");

  if (Imm == 0)
    Inst.addOperand(MCOperand::createImm(16));
  else if (Imm == 1)
    Inst.addOperand(MCOperand::createImm(24));
  else if (Imm == 2)
    Inst.addOperand(MCOperand::createImm(32));
  else if (Imm == 3)
    Inst.addOperand(MCOperand::createImm(40));
  else
    return MCDisassembler::Fail;

  return MCDisassembler::Success;
}

static DecodeStatus DecodeRegSeqOperand(MCInst &Inst, uint64_t Imm,
                                        int64_t Address,
                                        const MCDisassembler *Decoder) {
  assert(isUInt<10>(Imm) && "Invalid immediate");

  auto Imm5 = Imm & 0x1f;
  auto Ry = (Imm >> 5) & 0x1f;

  if (DecodeGPRRegisterClass(Inst, Ry, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(GPRDecoderTable[Ry + Imm5]));

  return MCDisassembler::Success;
}

static DecodeStatus DecodeRegSeqOperandF1(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const MCDisassembler *Decoder) {
  assert(isUInt<10>(Imm) && "Invalid immediate");

  auto Imm5 = Imm & 0x1f;
  auto Ry = (Imm >> 5) & 0x1f;

  if (DecodesFPR32RegisterClass(Inst, Ry, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR32DecoderTable[Ry + Imm5]));

  return MCDisassembler::Success;
}

static DecodeStatus DecodeRegSeqOperandD1(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const MCDisassembler *Decoder) {
  assert(isUInt<10>(Imm) && "Invalid immediate");

  auto Imm5 = Imm & 0x1f;
  auto Ry = (Imm >> 5) & 0x1f;

  if (DecodesFPR64RegisterClass(Inst, Ry, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR64DecoderTable[Ry + Imm5]));

  return MCDisassembler::Success;
}

static DecodeStatus DecodeRegSeqOperandF2(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const MCDisassembler *Decoder) {
  assert(isUInt<10>(Imm) && "Invalid immediate");

  auto Imm5 = Imm & 0x1f;
  auto Ry = (Imm >> 5) & 0x1f;

  if (DecodeFPR32RegisterClass(Inst, Ry, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR32DecoderTable[Ry + Imm5]));

  return MCDisassembler::Success;
}

static DecodeStatus DecodeRegSeqOperandD2(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const MCDisassembler *Decoder) {
  assert(isUInt<10>(Imm) && "Invalid immediate");

  auto Imm5 = Imm & 0x1f;
  auto Ry = (Imm >> 5) & 0x1f;

  if (DecodeFPR64RegisterClass(Inst, Ry, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(FPR64DecoderTable[Ry + Imm5]));

  return MCDisassembler::Success;
}

static DecodeStatus decodeImmShiftOpValue(MCInst &Inst, uint64_t Imm,
                                          int64_t Address,
                                          const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(Log2_64(Imm)));
  return MCDisassembler::Success;
}

template <unsigned N, unsigned S>
static DecodeStatus decodeSImmOperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address,
                                      const MCDisassembler *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  // Sign-extend the number in the bottom N bits of Imm
  Inst.addOperand(MCOperand::createImm(SignExtend64<N>(Imm) << S));
  return MCDisassembler::Success;
}

#include "CSKYGenDisassemblerTables.inc"

DecodeStatus CSKYDisassembler::handleCROperand(MCInst &MI) const {

  // FIXME: To query instruction info from td file or a table inc file
  switch (MI.getOpcode()) {
  default:
    return MCDisassembler::Success;
  case CSKY::LD16WSP:
  case CSKY::ST16WSP:
  case CSKY::ADDI16ZSP:
    MI.insert(std::next(MI.begin()), MCOperand::createReg(CSKY::R14));
    return MCDisassembler::Success;
  case CSKY::ADDI16SPSP:
  case CSKY::SUBI16SPSP:
    MI.insert(MI.begin(), MCOperand::createReg(CSKY::R14));
    MI.insert(MI.begin(), MCOperand::createReg(CSKY::R14));
    return MCDisassembler::Success;
  case CSKY::FCMPHS_S:
  case CSKY::FCMPHS_D:
  case CSKY::FCMPLT_S:
  case CSKY::FCMPLT_D:
  case CSKY::FCMPNE_S:
  case CSKY::FCMPNE_D:
  case CSKY::FCMPUO_S:
  case CSKY::FCMPUO_D:
  case CSKY::FCMPZHS_S:
  case CSKY::FCMPZHS_D:
  case CSKY::FCMPZLS_S:
  case CSKY::FCMPZLS_D:
  case CSKY::FCMPZNE_S:
  case CSKY::FCMPZNE_D:
  case CSKY::FCMPZUO_S:
  case CSKY::FCMPZUO_D:
  case CSKY::f2FCMPHS_S:
  case CSKY::f2FCMPHS_D:
  case CSKY::f2FCMPLT_S:
  case CSKY::f2FCMPLT_D:
  case CSKY::f2FCMPNE_S:
  case CSKY::f2FCMPNE_D:
  case CSKY::f2FCMPUO_S:
  case CSKY::f2FCMPUO_D:
  case CSKY::f2FCMPHSZ_S:
  case CSKY::f2FCMPHSZ_D:
  case CSKY::f2FCMPHZ_S:
  case CSKY::f2FCMPHZ_D:
  case CSKY::f2FCMPLSZ_S:
  case CSKY::f2FCMPLSZ_D:
  case CSKY::f2FCMPLTZ_S:
  case CSKY::f2FCMPLTZ_D:
  case CSKY::f2FCMPNEZ_S:
  case CSKY::f2FCMPNEZ_D:
  case CSKY::f2FCMPUOZ_S:
  case CSKY::f2FCMPUOZ_D:

  case CSKY::BT32:
  case CSKY::BF32:
  case CSKY::BT16:
  case CSKY::BF16:
  case CSKY::CMPNEI32:
  case CSKY::CMPNEI16:
  case CSKY::CMPNE32:
  case CSKY::CMPNE16:
  case CSKY::CMPHSI32:
  case CSKY::CMPHSI16:
  case CSKY::CMPHS32:
  case CSKY::CMPHS16:
  case CSKY::CMPLTI32:
  case CSKY::CMPLTI16:
  case CSKY::CMPLT32:
  case CSKY::CMPLT16:
  case CSKY::BTSTI32:
  case CSKY::BTSTI16:
  case CSKY::TSTNBZ32:
  case CSKY::TSTNBZ16:
  case CSKY::TST32:
  case CSKY::TST16:
    MI.insert(MI.begin(), MCOperand::createReg(CSKY::C));
    return MCDisassembler::Success;
  case CSKY::LSLC32:
  case CSKY::LSRC32:
  case CSKY::ASRC32:
    MI.insert(std::next(MI.begin()), MCOperand::createReg(CSKY::C));
    return MCDisassembler::Success;
  case CSKY::MOVF32:
  case CSKY::MOVT32:
  case CSKY::MVC32:
  case CSKY::MVCV32:
  case CSKY::MVCV16:
  case CSKY::INCT32:
  case CSKY::INCF32:
  case CSKY::DECT32:
  case CSKY::DECF32:
  case CSKY::DECGT32:
  case CSKY::DECLT32:
  case CSKY::DECNE32:
  case CSKY::CLRF32:
  case CSKY::CLRT32:
  case CSKY::f2FSEL_S:
  case CSKY::f2FSEL_D:
    MI.insert(std::next(MI.begin()), MCOperand::createReg(CSKY::C));
    return MCDisassembler::Success;
  case CSKY::ADDC32:
  case CSKY::ADDC16:
  case CSKY::SUBC32:
  case CSKY::SUBC16:
  case CSKY::XSR32:
    MI.insert(std::next(MI.begin()), MCOperand::createReg(CSKY::C));
    MI.insert(MI.end(), MCOperand::createReg(CSKY::C));
    return MCDisassembler::Success;
  case CSKY::INS32:
    MI.getOperand(3).setImm(MI.getOperand(3).getImm() +
                            MI.getOperand(4).getImm());
    return MCDisassembler::Success;
  }
}

static bool decodeFPUV3Instruction(MCInst &MI, uint32_t insn, uint64_t Address,
                                   const MCDisassembler *DisAsm,
                                   const MCSubtargetInfo &STI) {
  LLVM_DEBUG(dbgs() << "Trying CSKY 32-bit fpuv3 table :\n");
  if (!STI.hasFeature(CSKY::FeatureFPUV3_HF) &&
      !STI.hasFeature(CSKY::FeatureFPUV3_SF) &&
      !STI.hasFeature(CSKY::FeatureFPUV3_DF))
    return false;

  DecodeStatus Result =
      decodeInstruction(DecoderTableFPUV332, MI, insn, Address, DisAsm, STI);

  if (Result == MCDisassembler::Fail) {
    MI.clear();
    return false;
  }

  return true;
}

DecodeStatus CSKYDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                              ArrayRef<uint8_t> Bytes,
                                              uint64_t Address,
                                              raw_ostream &CS) const {

  uint32_t Insn;
  DecodeStatus Result = MCDisassembler::Fail;

  Insn = support::endian::read16le(Bytes.data());

  if ((Insn >> 14) == 0x3) {
    if (Bytes.size() < 4) {
      Size = 0;
      return MCDisassembler::Fail;
    }
    Insn = (Insn << 16) | support::endian::read16le(&Bytes[2]);

    if (decodeFPUV3Instruction(MI, Insn, Address, this, STI))
      Result = MCDisassembler::Success;
    else {
      LLVM_DEBUG(dbgs() << "Trying CSKY 32-bit table :\n");
      Result = decodeInstruction(DecoderTable32, MI, Insn, Address, this, STI);
    }

    Size = 4;
  } else {
    if (Bytes.size() < 2) {
      Size = 0;
      return MCDisassembler::Fail;
    }
    LLVM_DEBUG(dbgs() << "Trying CSKY 16-bit table :\n");
    Result = decodeInstruction(DecoderTable16, MI, Insn, Address, this, STI);
    Size = 2;
  }

  handleCROperand(MI);

  return Result;
}
