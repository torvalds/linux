//===-- RISCVDisassembler.cpp - Disassembler for RISC-V -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RISCVDisassembler class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVBaseInfo.h"
#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "TargetInfo/RISCVTargetInfo.h"
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

#define DEBUG_TYPE "riscv-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class RISCVDisassembler : public MCDisassembler {
  std::unique_ptr<MCInstrInfo const> const MCII;

public:
  RISCVDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                    MCInstrInfo const *MCII)
      : MCDisassembler(STI, Ctx), MCII(MCII) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;

private:
  void addSPOperands(MCInst &MI) const;

  DecodeStatus getInstruction32(MCInst &Instr, uint64_t &Size,
                                ArrayRef<uint8_t> Bytes, uint64_t Address,
                                raw_ostream &CStream) const;
  DecodeStatus getInstruction16(MCInst &Instr, uint64_t &Size,
                                ArrayRef<uint8_t> Bytes, uint64_t Address,
                                raw_ostream &CStream) const;
};
} // end anonymous namespace

static MCDisassembler *createRISCVDisassembler(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx) {
  return new RISCVDisassembler(STI, Ctx, T.createMCInstrInfo());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCVDisassembler() {
  // Register the disassembler for each target.
  TargetRegistry::RegisterMCDisassembler(getTheRISCV32Target(),
                                         createRISCVDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheRISCV64Target(),
                                         createRISCVDisassembler);
}

static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, uint32_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  bool IsRVE = Decoder->getSubtargetInfo().hasFeature(RISCV::FeatureStdExtE);

  if (RegNo >= 32 || (IsRVE && RegNo >= 16))
    return MCDisassembler::Fail;

  MCRegister Reg = RISCV::X0 + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRX1X5RegisterClass(MCInst &Inst, uint32_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  MCRegister Reg = RISCV::X0 + RegNo;
  if (Reg != RISCV::X1 && Reg != RISCV::X5)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR16RegisterClass(MCInst &Inst, uint32_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo >= 32)
    return MCDisassembler::Fail;

  MCRegister Reg = RISCV::F0_H + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR32RegisterClass(MCInst &Inst, uint32_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo >= 32)
    return MCDisassembler::Fail;

  MCRegister Reg = RISCV::F0_F + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR32CRegisterClass(MCInst &Inst, uint32_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo >= 8) {
    return MCDisassembler::Fail;
  }
  MCRegister Reg = RISCV::F8_F + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR64RegisterClass(MCInst &Inst, uint32_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo >= 32)
    return MCDisassembler::Fail;

  MCRegister Reg = RISCV::F0_D + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR64CRegisterClass(MCInst &Inst, uint32_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo >= 8) {
    return MCDisassembler::Fail;
  }
  MCRegister Reg = RISCV::F8_D + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRNoX0RegisterClass(MCInst &Inst, uint32_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo == 0) {
    return MCDisassembler::Fail;
  }

  return DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus
DecodeGPRNoX0X2RegisterClass(MCInst &Inst, uint64_t RegNo, uint32_t Address,
                             const MCDisassembler *Decoder) {
  if (RegNo == 2) {
    return MCDisassembler::Fail;
  }

  return DecodeGPRNoX0RegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeGPRCRegisterClass(MCInst &Inst, uint32_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo >= 8)
    return MCDisassembler::Fail;

  MCRegister Reg = RISCV::X8 + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRPairRegisterClass(MCInst &Inst, uint32_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo >= 32 || RegNo & 1)
    return MCDisassembler::Fail;

  MCRegister Reg = RISCV::X0 + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeSR07RegisterClass(MCInst &Inst, uint32_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  if (RegNo >= 8)
    return MCDisassembler::Fail;

  MCRegister Reg = (RegNo < 2) ? (RegNo + RISCV::X8) : (RegNo - 2 + RISCV::X18);
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeVRRegisterClass(MCInst &Inst, uint32_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  if (RegNo >= 32)
    return MCDisassembler::Fail;

  MCRegister Reg = RISCV::V0 + RegNo;
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeVRM2RegisterClass(MCInst &Inst, uint32_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo >= 32 || RegNo % 2)
    return MCDisassembler::Fail;

  const RISCVDisassembler *Dis =
      static_cast<const RISCVDisassembler *>(Decoder);
  const MCRegisterInfo *RI = Dis->getContext().getRegisterInfo();
  MCRegister Reg =
      RI->getMatchingSuperReg(RISCV::V0 + RegNo, RISCV::sub_vrm1_0,
                              &RISCVMCRegisterClasses[RISCV::VRM2RegClassID]);

  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeVRM4RegisterClass(MCInst &Inst, uint32_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo >= 32 || RegNo % 4)
    return MCDisassembler::Fail;

  const RISCVDisassembler *Dis =
      static_cast<const RISCVDisassembler *>(Decoder);
  const MCRegisterInfo *RI = Dis->getContext().getRegisterInfo();
  MCRegister Reg =
      RI->getMatchingSuperReg(RISCV::V0 + RegNo, RISCV::sub_vrm1_0,
                              &RISCVMCRegisterClasses[RISCV::VRM4RegClassID]);

  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeVRM8RegisterClass(MCInst &Inst, uint32_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo >= 32 || RegNo % 8)
    return MCDisassembler::Fail;

  const RISCVDisassembler *Dis =
      static_cast<const RISCVDisassembler *>(Decoder);
  const MCRegisterInfo *RI = Dis->getContext().getRegisterInfo();
  MCRegister Reg =
      RI->getMatchingSuperReg(RISCV::V0 + RegNo, RISCV::sub_vrm1_0,
                              &RISCVMCRegisterClasses[RISCV::VRM8RegClassID]);

  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus decodeVMaskReg(MCInst &Inst, uint32_t RegNo,
                                   uint64_t Address,
                                   const MCDisassembler *Decoder) {
  if (RegNo >= 2)
    return MCDisassembler::Fail;

  MCRegister Reg = (RegNo == 0) ? RISCV::V0 : RISCV::NoRegister;

  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

template <unsigned N>
static DecodeStatus decodeUImmOperand(MCInst &Inst, uint32_t Imm,
                                      int64_t Address,
                                      const MCDisassembler *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

template <unsigned N>
static DecodeStatus decodeUImmNonZeroOperand(MCInst &Inst, uint32_t Imm,
                                             int64_t Address,
                                             const MCDisassembler *Decoder) {
  if (Imm == 0)
    return MCDisassembler::Fail;
  return decodeUImmOperand<N>(Inst, Imm, Address, Decoder);
}

template <unsigned N>
static DecodeStatus decodeSImmOperand(MCInst &Inst, uint32_t Imm,
                                      int64_t Address,
                                      const MCDisassembler *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  // Sign-extend the number in the bottom N bits of Imm
  Inst.addOperand(MCOperand::createImm(SignExtend64<N>(Imm)));
  return MCDisassembler::Success;
}

template <unsigned N>
static DecodeStatus decodeSImmNonZeroOperand(MCInst &Inst, uint32_t Imm,
                                             int64_t Address,
                                             const MCDisassembler *Decoder) {
  if (Imm == 0)
    return MCDisassembler::Fail;
  return decodeSImmOperand<N>(Inst, Imm, Address, Decoder);
}

template <unsigned N>
static DecodeStatus decodeSImmOperandAndLsl1(MCInst &Inst, uint32_t Imm,
                                             int64_t Address,
                                             const MCDisassembler *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  // Sign-extend the number in the bottom N bits of Imm after accounting for
  // the fact that the N bit immediate is stored in N-1 bits (the LSB is
  // always zero)
  Inst.addOperand(MCOperand::createImm(SignExtend64<N>(Imm << 1)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeCLUIImmOperand(MCInst &Inst, uint32_t Imm,
                                         int64_t Address,
                                         const MCDisassembler *Decoder) {
  assert(isUInt<6>(Imm) && "Invalid immediate");
  if (Imm > 31) {
    Imm = (SignExtend64<6>(Imm) & 0xfffff);
  }
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeFRMArg(MCInst &Inst, uint32_t Imm, int64_t Address,
                                 const MCDisassembler *Decoder) {
  assert(isUInt<3>(Imm) && "Invalid immediate");
  if (!llvm::RISCVFPRndMode::isValidRoundingMode(Imm))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeRVCInstrRdRs1ImmZero(MCInst &Inst, uint32_t Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);

static DecodeStatus decodeRVCInstrRdSImm(MCInst &Inst, uint32_t Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

static DecodeStatus decodeRVCInstrRdRs1UImm(MCInst &Inst, uint32_t Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);

static DecodeStatus decodeRVCInstrRdRs2(MCInst &Inst, uint32_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);

static DecodeStatus decodeRVCInstrRdRs1Rs2(MCInst &Inst, uint32_t Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);

static DecodeStatus decodeXTHeadMemPair(MCInst &Inst, uint32_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);

static DecodeStatus decodeZcmpRlist(MCInst &Inst, uint32_t Imm,
                                    uint64_t Address, const void *Decoder);

static DecodeStatus decodeRegReg(MCInst &Inst, uint32_t Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);

static DecodeStatus decodeZcmpSpimm(MCInst &Inst, uint32_t Imm,
                                    uint64_t Address, const void *Decoder);

static DecodeStatus decodeCSSPushPopchk(MCInst &Inst, uint32_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);

#include "RISCVGenDisassemblerTables.inc"

static DecodeStatus decodeRVCInstrRdRs1ImmZero(MCInst &Inst, uint32_t Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  uint32_t Rd = fieldFromInstruction(Insn, 7, 5);
  [[maybe_unused]] DecodeStatus Result =
      DecodeGPRNoX0RegisterClass(Inst, Rd, Address, Decoder);
  assert(Result == MCDisassembler::Success && "Invalid register");
  Inst.addOperand(Inst.getOperand(0));
  Inst.addOperand(MCOperand::createImm(0));
  return MCDisassembler::Success;
}

static DecodeStatus decodeCSSPushPopchk(MCInst &Inst, uint32_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  uint32_t Rs1 = fieldFromInstruction(Insn, 7, 5);
  [[maybe_unused]] DecodeStatus Result =
      DecodeGPRX1X5RegisterClass(Inst, Rs1, Address, Decoder);
  assert(Result == MCDisassembler::Success && "Invalid register");
  return MCDisassembler::Success;
}

static DecodeStatus decodeRVCInstrRdSImm(MCInst &Inst, uint32_t Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createReg(RISCV::X0));
  uint32_t SImm6 =
      fieldFromInstruction(Insn, 12, 1) << 5 | fieldFromInstruction(Insn, 2, 5);
  [[maybe_unused]] DecodeStatus Result =
      decodeSImmOperand<6>(Inst, SImm6, Address, Decoder);
  assert(Result == MCDisassembler::Success && "Invalid immediate");
  return MCDisassembler::Success;
}

static DecodeStatus decodeRVCInstrRdRs1UImm(MCInst &Inst, uint32_t Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createReg(RISCV::X0));
  Inst.addOperand(Inst.getOperand(0));
  uint32_t UImm6 =
      fieldFromInstruction(Insn, 12, 1) << 5 | fieldFromInstruction(Insn, 2, 5);
  [[maybe_unused]] DecodeStatus Result =
      decodeUImmOperand<6>(Inst, UImm6, Address, Decoder);
  assert(Result == MCDisassembler::Success && "Invalid immediate");
  return MCDisassembler::Success;
}

static DecodeStatus decodeRVCInstrRdRs2(MCInst &Inst, uint32_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  uint32_t Rd = fieldFromInstruction(Insn, 7, 5);
  uint32_t Rs2 = fieldFromInstruction(Insn, 2, 5);
  DecodeGPRRegisterClass(Inst, Rd, Address, Decoder);
  DecodeGPRRegisterClass(Inst, Rs2, Address, Decoder);
  return MCDisassembler::Success;
}

static DecodeStatus decodeRVCInstrRdRs1Rs2(MCInst &Inst, uint32_t Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  uint32_t Rd = fieldFromInstruction(Insn, 7, 5);
  uint32_t Rs2 = fieldFromInstruction(Insn, 2, 5);
  DecodeGPRRegisterClass(Inst, Rd, Address, Decoder);
  Inst.addOperand(Inst.getOperand(0));
  DecodeGPRRegisterClass(Inst, Rs2, Address, Decoder);
  return MCDisassembler::Success;
}

static DecodeStatus decodeXTHeadMemPair(MCInst &Inst, uint32_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  uint32_t Rd1 = fieldFromInstruction(Insn, 7, 5);
  uint32_t Rs1 = fieldFromInstruction(Insn, 15, 5);
  uint32_t Rd2 = fieldFromInstruction(Insn, 20, 5);
  uint32_t UImm2 = fieldFromInstruction(Insn, 25, 2);
  DecodeGPRRegisterClass(Inst, Rd1, Address, Decoder);
  DecodeGPRRegisterClass(Inst, Rd2, Address, Decoder);
  DecodeGPRRegisterClass(Inst, Rs1, Address, Decoder);
  [[maybe_unused]] DecodeStatus Result =
      decodeUImmOperand<2>(Inst, UImm2, Address, Decoder);
  assert(Result == MCDisassembler::Success && "Invalid immediate");

  // Disassemble the final operand which is implicit.
  unsigned Opcode = Inst.getOpcode();
  bool IsWordOp = (Opcode == RISCV::TH_LWD || Opcode == RISCV::TH_LWUD ||
                   Opcode == RISCV::TH_SWD);
  if (IsWordOp)
    Inst.addOperand(MCOperand::createImm(3));
  else
    Inst.addOperand(MCOperand::createImm(4));

  return MCDisassembler::Success;
}

static DecodeStatus decodeZcmpRlist(MCInst &Inst, uint32_t Imm,
                                    uint64_t Address, const void *Decoder) {
  if (Imm <= 3)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeRegReg(MCInst &Inst, uint32_t Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  uint32_t Rs1 = fieldFromInstruction(Insn, 0, 5);
  uint32_t Rs2 = fieldFromInstruction(Insn, 5, 5);
  DecodeGPRRegisterClass(Inst, Rs1, Address, Decoder);
  DecodeGPRRegisterClass(Inst, Rs2, Address, Decoder);
  return MCDisassembler::Success;
}

static DecodeStatus decodeZcmpSpimm(MCInst &Inst, uint32_t Imm,
                                    uint64_t Address, const void *Decoder) {
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

// Add implied SP operand for C.*SP compressed instructions. The SP operand
// isn't explicitly encoded in the instruction.
void RISCVDisassembler::addSPOperands(MCInst &MI) const {
  const MCInstrDesc &MCID = MCII->get(MI.getOpcode());
  for (unsigned i = 0; i < MCID.getNumOperands(); i++)
    if (MCID.operands()[i].RegClass == RISCV::SPRegClassID)
      MI.insert(MI.begin() + i, MCOperand::createReg(RISCV::X2));
}

#define TRY_TO_DECODE_WITH_ADDITIONAL_OPERATION(FEATURE_CHECKS, DECODER_TABLE, \
                                                DESC, ADDITIONAL_OPERATION)    \
  do {                                                                         \
    if (FEATURE_CHECKS) {                                                      \
      LLVM_DEBUG(dbgs() << "Trying " DESC ":\n");                              \
      DecodeStatus Result =                                                    \
          decodeInstruction(DECODER_TABLE, MI, Insn, Address, this, STI);      \
      if (Result != MCDisassembler::Fail) {                                    \
        ADDITIONAL_OPERATION;                                                  \
        return Result;                                                         \
      }                                                                        \
    }                                                                          \
  } while (false)
#define TRY_TO_DECODE_AND_ADD_SP(FEATURE_CHECKS, DECODER_TABLE, DESC)          \
  TRY_TO_DECODE_WITH_ADDITIONAL_OPERATION(FEATURE_CHECKS, DECODER_TABLE, DESC, \
                                          addSPOperands(MI))
#define TRY_TO_DECODE(FEATURE_CHECKS, DECODER_TABLE, DESC)                     \
  TRY_TO_DECODE_WITH_ADDITIONAL_OPERATION(FEATURE_CHECKS, DECODER_TABLE, DESC, \
                                          (void)nullptr)
#define TRY_TO_DECODE_FEATURE(FEATURE, DECODER_TABLE, DESC)                    \
  TRY_TO_DECODE(STI.hasFeature(FEATURE), DECODER_TABLE, DESC)

DecodeStatus RISCVDisassembler::getInstruction32(MCInst &MI, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CS) const {
  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }
  Size = 4;

  uint32_t Insn = support::endian::read32le(Bytes.data());

  TRY_TO_DECODE(STI.hasFeature(RISCV::FeatureStdExtZdinx) &&
                    !STI.hasFeature(RISCV::Feature64Bit),
                DecoderTableRV32Zdinx32,
                "RV32Zdinx table (Double in Integer and rv32)");
  TRY_TO_DECODE(STI.hasFeature(RISCV::FeatureStdExtZacas) &&
                    !STI.hasFeature(RISCV::Feature64Bit),
                DecoderTableRV32Zacas32,
                "RV32Zacas table (Compare-And-Swap and rv32)");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureStdExtZfinx, DecoderTableRVZfinx32,
                        "RVZfinx table (Float in Integer)");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXVentanaCondOps,
                        DecoderTableXVentana32, "Ventana custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadBa, DecoderTableXTHeadBa32,
                        "XTHeadBa custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadBb, DecoderTableXTHeadBb32,
                        "XTHeadBb custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadBs, DecoderTableXTHeadBs32,
                        "XTHeadBs custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadCondMov,
                        DecoderTableXTHeadCondMov32,
                        "XTHeadCondMov custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadCmo, DecoderTableXTHeadCmo32,
                        "XTHeadCmo custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadFMemIdx,
                        DecoderTableXTHeadFMemIdx32,
                        "XTHeadFMemIdx custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadMac, DecoderTableXTHeadMac32,
                        "XTHeadMac custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadMemIdx,
                        DecoderTableXTHeadMemIdx32,
                        "XTHeadMemIdx custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadMemPair,
                        DecoderTableXTHeadMemPair32,
                        "XTHeadMemPair custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadSync,
                        DecoderTableXTHeadSync32,
                        "XTHeadSync custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXTHeadVdot,
                        DecoderTableXTHeadVdot32,
                        "XTHeadVdot custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXSfvcp, DecoderTableXSfvcp32,
                        "SiFive VCIX custom opcode table");
  TRY_TO_DECODE_FEATURE(
      RISCV::FeatureVendorXSfvqmaccdod, DecoderTableXSfvqmaccdod32,
      "SiFive Matrix Multiplication (2x8 and 8x2) Instruction opcode table");
  TRY_TO_DECODE_FEATURE(
      RISCV::FeatureVendorXSfvqmaccqoq, DecoderTableXSfvqmaccqoq32,
      "SiFive Matrix Multiplication (4x8 and 8x4) Instruction opcode table");
  TRY_TO_DECODE_FEATURE(
      RISCV::FeatureVendorXSfvfwmaccqqq, DecoderTableXSfvfwmaccqqq32,
      "SiFive Matrix Multiplication Instruction opcode table");
  TRY_TO_DECODE_FEATURE(
      RISCV::FeatureVendorXSfvfnrclipxfqf, DecoderTableXSfvfnrclipxfqf32,
      "SiFive FP32-to-int8 Ranged Clip Instructions opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXSiFivecdiscarddlone,
                        DecoderTableXSiFivecdiscarddlone32,
                        "SiFive sf.cdiscard.d.l1 custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXSiFivecflushdlone,
                        DecoderTableXSiFivecflushdlone32,
                        "SiFive sf.cflush.d.l1 custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXSfcease, DecoderTableXSfcease32,
                        "SiFive sf.cease custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXCVbitmanip,
                        DecoderTableXCVbitmanip32,
                        "CORE-V Bit Manipulation custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXCVelw, DecoderTableXCVelw32,
                        "CORE-V Event load custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXCVmac, DecoderTableXCVmac32,
                        "CORE-V MAC custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXCVmem, DecoderTableXCVmem32,
                        "CORE-V MEM custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXCValu, DecoderTableXCValu32,
                        "CORE-V ALU custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXCVsimd, DecoderTableXCVsimd32,
                        "CORE-V SIMD extensions custom opcode table");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureVendorXCVbi, DecoderTableXCVbi32,
                        "CORE-V Immediate Branching custom opcode table");
  TRY_TO_DECODE(true, DecoderTable32, "RISCV32 table");

  return MCDisassembler::Fail;
}

DecodeStatus RISCVDisassembler::getInstruction16(MCInst &MI, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CS) const {
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }
  Size = 2;

  uint32_t Insn = support::endian::read16le(Bytes.data());
  TRY_TO_DECODE_AND_ADD_SP(!STI.hasFeature(RISCV::Feature64Bit),
                           DecoderTableRISCV32Only_16,
                           "RISCV32Only_16 table (16-bit Instruction)");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureStdExtZicfiss, DecoderTableZicfiss16,
                        "RVZicfiss table (Shadow Stack)");
  TRY_TO_DECODE_FEATURE(RISCV::FeatureStdExtZcmt, DecoderTableRVZcmt16,
                        "Zcmt table (16-bit Table Jump Instructions)");
  TRY_TO_DECODE_FEATURE(
      RISCV::FeatureStdExtZcmp, DecoderTableRVZcmp16,
      "Zcmp table (16-bit Push/Pop & Double Move Instructions)");
  TRY_TO_DECODE_AND_ADD_SP(STI.hasFeature(RISCV::FeatureVendorXwchc),
                           DecoderTableXwchc16,
                           "WCH QingKe XW custom opcode table");
  TRY_TO_DECODE_AND_ADD_SP(true, DecoderTable16,
                           "RISCV_C table (16-bit Instruction)");

  return MCDisassembler::Fail;
}

DecodeStatus RISCVDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                               ArrayRef<uint8_t> Bytes,
                                               uint64_t Address,
                                               raw_ostream &CS) const {
  // It's a 16 bit instruction if bit 0 and 1 are not 0b11.
  if ((Bytes[0] & 0b11) != 0b11)
    return getInstruction16(MI, Size, Bytes, Address, CS);

  // It's a 32 bit instruction if bit 1:0 are 0b11(checked above) and bits 4:2
  // are not 0b111.
  if ((Bytes[0] & 0b1'1100) != 0b1'1100)
    return getInstruction32(MI, Size, Bytes, Address, CS);

  // 48-bit instructions are encoded as 0bxx011111.
  if ((Bytes[0] & 0b11'1111) == 0b01'1111) {
    Size = Bytes.size() >= 6 ? 6 : 0;
    return MCDisassembler::Fail;
  }

  // 64-bit instructions are encoded as 0x0111111.
  if ((Bytes[0] & 0b111'1111) == 0b011'1111) {
    Size = Bytes.size() >= 8 ? 8 : 0;
    return MCDisassembler::Fail;
  }

  // Remaining cases need to check a second byte.
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  // 80-bit through 176-bit instructions are encoded as 0bxnnnxxxx_x1111111.
  // Where the number of bits is (80 + (nnn * 16)) for nnn != 0b111.
  unsigned nnn = (Bytes[1] >> 4) & 0b111;
  if (nnn != 0b111) {
    Size = 10 + (nnn * 2);
    if (Bytes.size() < Size)
      Size = 0;
    return MCDisassembler::Fail;
  }

  // Remaining encodings are reserved for > 176-bit instructions.
  Size = 0;
  return MCDisassembler::Fail;
}
