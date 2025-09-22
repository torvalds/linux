//===- XCoreDisassembler.cpp - Disassembler for XCore -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is part of the XCore Disassembler.
///
//===----------------------------------------------------------------------===//

#include "TargetInfo/XCoreTargetInfo.h"
#include "XCore.h"
#include "XCoreRegisterInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "xcore-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for XCore.
class XCoreDisassembler : public MCDisassembler {
public:
  XCoreDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx) :
    MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
}

static bool readInstruction16(ArrayRef<uint8_t> Bytes, uint64_t Address,
                              uint64_t &Size, uint16_t &Insn) {
  // We want to read exactly 2 Bytes of data.
  if (Bytes.size() < 2) {
    Size = 0;
    return false;
  }
  // Encoded as a little-endian 16-bit word in the stream.
  Insn = (Bytes[0] << 0) | (Bytes[1] << 8);
  return true;
}

static bool readInstruction32(ArrayRef<uint8_t> Bytes, uint64_t Address,
                              uint64_t &Size, uint32_t &Insn) {
  // We want to read exactly 4 Bytes of data.
  if (Bytes.size() < 4) {
    Size = 0;
    return false;
  }
  // Encoded as a little-endian 32-bit word in the stream.
  Insn =
      (Bytes[0] << 0) | (Bytes[1] << 8) | (Bytes[2] << 16) | (Bytes[3] << 24);
  return true;
}

static unsigned getReg(const MCDisassembler *D, unsigned RC, unsigned RegNo) {
  const MCRegisterInfo *RegInfo = D->getContext().getRegisterInfo();
  return *(RegInfo->getRegClass(RC).begin() + RegNo);
}

static DecodeStatus DecodeGRRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder);

static DecodeStatus DecodeRRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);

static DecodeStatus DecodeBitpOperand(MCInst &Inst, unsigned Val,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder);

static DecodeStatus DecodeNegImmOperand(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);

static DecodeStatus Decode2RInstruction(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);

static DecodeStatus Decode2RImmInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);

static DecodeStatus DecodeR2RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

static DecodeStatus Decode2RSrcDstInstruction(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder);

static DecodeStatus DecodeRUSInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

static DecodeStatus DecodeRUSBitpInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);

static DecodeStatus
DecodeRUSSrcDstBitpInstruction(MCInst &Inst, unsigned Insn, uint64_t Address,
                               const MCDisassembler *Decoder);

static DecodeStatus DecodeL2RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

static DecodeStatus DecodeLR2RInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);

static DecodeStatus Decode3RInstruction(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);

static DecodeStatus Decode3RImmInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);

static DecodeStatus Decode2RUSInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);

static DecodeStatus Decode2RUSBitpInstruction(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder);

static DecodeStatus DecodeL3RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

static DecodeStatus DecodeL3RSrcDstInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);

static DecodeStatus DecodeL2RUSInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);

static DecodeStatus DecodeL2RUSBitpInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);

static DecodeStatus DecodeL6RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

static DecodeStatus DecodeL5RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

static DecodeStatus DecodeL4RSrcDstInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);

static DecodeStatus
DecodeL4RSrcDstSrcDstInstruction(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);

#include "XCoreGenDisassemblerTables.inc"

static DecodeStatus DecodeGRRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo > 11)
    return MCDisassembler::Fail;
  unsigned Reg = getReg(Decoder, XCore::GRRegsRegClassID, RegNo);
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeRRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;
  unsigned Reg = getReg(Decoder, XCore::RRegsRegClassID, RegNo);
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeBitpOperand(MCInst &Inst, unsigned Val,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  if (Val > 11)
    return MCDisassembler::Fail;
  static const unsigned Values[] = {
    32 /*bpw*/, 1, 2, 3, 4, 5, 6, 7, 8, 16, 24, 32
  };
  Inst.addOperand(MCOperand::createImm(Values[Val]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeNegImmOperand(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(-(int64_t)Val));
  return MCDisassembler::Success;
}

static DecodeStatus
Decode2OpInstruction(unsigned Insn, unsigned &Op1, unsigned &Op2) {
  unsigned Combined = fieldFromInstruction(Insn, 6, 5);
  if (Combined < 27)
    return MCDisassembler::Fail;
  if (fieldFromInstruction(Insn, 5, 1)) {
    if (Combined == 31)
      return MCDisassembler::Fail;
    Combined += 5;
  }
  Combined -= 27;
  unsigned Op1High = Combined % 3;
  unsigned Op2High = Combined / 3;
  Op1 = (Op1High << 2) | fieldFromInstruction(Insn, 2, 2);
  Op2 = (Op2High << 2) | fieldFromInstruction(Insn, 0, 2);
  return MCDisassembler::Success;
}

static DecodeStatus
Decode3OpInstruction(unsigned Insn, unsigned &Op1, unsigned &Op2,
                     unsigned &Op3) {
  unsigned Combined = fieldFromInstruction(Insn, 6, 5);
  if (Combined >= 27)
    return MCDisassembler::Fail;

  unsigned Op1High = Combined % 3;
  unsigned Op2High = (Combined / 3) % 3;
  unsigned Op3High = Combined / 9;
  Op1 = (Op1High << 2) | fieldFromInstruction(Insn, 4, 2);
  Op2 = (Op2High << 2) | fieldFromInstruction(Insn, 2, 2);
  Op3 = (Op3High << 2) | fieldFromInstruction(Insn, 0, 2);
  return MCDisassembler::Success;
}

static DecodeStatus Decode2OpInstructionFail(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  // Try and decode as a 3R instruction.
  unsigned Opcode = fieldFromInstruction(Insn, 11, 5);
  switch (Opcode) {
  case 0x0:
    Inst.setOpcode(XCore::STW_2rus);
    return Decode2RUSInstruction(Inst, Insn, Address, Decoder);
  case 0x1:
    Inst.setOpcode(XCore::LDW_2rus);
    return Decode2RUSInstruction(Inst, Insn, Address, Decoder);
  case 0x2:
    Inst.setOpcode(XCore::ADD_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x3:
    Inst.setOpcode(XCore::SUB_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x4:
    Inst.setOpcode(XCore::SHL_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x5:
    Inst.setOpcode(XCore::SHR_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x6:
    Inst.setOpcode(XCore::EQ_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x7:
    Inst.setOpcode(XCore::AND_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x8:
    Inst.setOpcode(XCore::OR_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x9:
    Inst.setOpcode(XCore::LDW_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x10:
    Inst.setOpcode(XCore::LD16S_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x11:
    Inst.setOpcode(XCore::LD8U_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x12:
    Inst.setOpcode(XCore::ADD_2rus);
    return Decode2RUSInstruction(Inst, Insn, Address, Decoder);
  case 0x13:
    Inst.setOpcode(XCore::SUB_2rus);
    return Decode2RUSInstruction(Inst, Insn, Address, Decoder);
  case 0x14:
    Inst.setOpcode(XCore::SHL_2rus);
    return Decode2RUSBitpInstruction(Inst, Insn, Address, Decoder);
  case 0x15:
    Inst.setOpcode(XCore::SHR_2rus);
    return Decode2RUSBitpInstruction(Inst, Insn, Address, Decoder);
  case 0x16:
    Inst.setOpcode(XCore::EQ_2rus);
    return Decode2RUSInstruction(Inst, Insn, Address, Decoder);
  case 0x17:
    Inst.setOpcode(XCore::TSETR_3r);
    return Decode3RImmInstruction(Inst, Insn, Address, Decoder);
  case 0x18:
    Inst.setOpcode(XCore::LSS_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  case 0x19:
    Inst.setOpcode(XCore::LSU_3r);
    return Decode3RInstruction(Inst, Insn, Address, Decoder);
  }
  return MCDisassembler::Fail;
}

static DecodeStatus Decode2RInstruction(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(Insn, Op1, Op2);
  if (S != MCDisassembler::Success)
    return Decode2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  return S;
}

static DecodeStatus Decode2RImmInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(Insn, Op1, Op2);
  if (S != MCDisassembler::Success)
    return Decode2OpInstructionFail(Inst, Insn, Address, Decoder);

  Inst.addOperand(MCOperand::createImm(Op1));
  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  return S;
}

static DecodeStatus DecodeR2RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(Insn, Op2, Op1);
  if (S != MCDisassembler::Success)
    return Decode2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  return S;
}

static DecodeStatus Decode2RSrcDstInstruction(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(Insn, Op1, Op2);
  if (S != MCDisassembler::Success)
    return Decode2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  return S;
}

static DecodeStatus DecodeRUSInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(Insn, Op1, Op2);
  if (S != MCDisassembler::Success)
    return Decode2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  Inst.addOperand(MCOperand::createImm(Op2));
  return S;
}

static DecodeStatus DecodeRUSBitpInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(Insn, Op1, Op2);
  if (S != MCDisassembler::Success)
    return Decode2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeBitpOperand(Inst, Op2, Address, Decoder);
  return S;
}

static DecodeStatus
DecodeRUSSrcDstBitpInstruction(MCInst &Inst, unsigned Insn, uint64_t Address,
                               const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(Insn, Op1, Op2);
  if (S != MCDisassembler::Success)
    return Decode2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeBitpOperand(Inst, Op2, Address, Decoder);
  return S;
}

static DecodeStatus DecodeL2OpInstructionFail(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  // Try and decode as a L3R / L2RUS instruction.
  unsigned Opcode = fieldFromInstruction(Insn, 16, 4) |
                    fieldFromInstruction(Insn, 27, 5) << 4;
  switch (Opcode) {
  case 0x0c:
    Inst.setOpcode(XCore::STW_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x1c:
    Inst.setOpcode(XCore::XOR_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x2c:
    Inst.setOpcode(XCore::ASHR_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x3c:
    Inst.setOpcode(XCore::LDAWF_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x4c:
    Inst.setOpcode(XCore::LDAWB_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x5c:
    Inst.setOpcode(XCore::LDA16F_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x6c:
    Inst.setOpcode(XCore::LDA16B_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x7c:
    Inst.setOpcode(XCore::MUL_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x8c:
    Inst.setOpcode(XCore::DIVS_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x9c:
    Inst.setOpcode(XCore::DIVU_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x10c:
    Inst.setOpcode(XCore::ST16_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x11c:
    Inst.setOpcode(XCore::ST8_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x12c:
    Inst.setOpcode(XCore::ASHR_l2rus);
    return DecodeL2RUSBitpInstruction(Inst, Insn, Address, Decoder);
  case 0x12d:
    Inst.setOpcode(XCore::OUTPW_l2rus);
    return DecodeL2RUSBitpInstruction(Inst, Insn, Address, Decoder);
  case 0x12e:
    Inst.setOpcode(XCore::INPW_l2rus);
    return DecodeL2RUSBitpInstruction(Inst, Insn, Address, Decoder);
  case 0x13c:
    Inst.setOpcode(XCore::LDAWF_l2rus);
    return DecodeL2RUSInstruction(Inst, Insn, Address, Decoder);
  case 0x14c:
    Inst.setOpcode(XCore::LDAWB_l2rus);
    return DecodeL2RUSInstruction(Inst, Insn, Address, Decoder);
  case 0x15c:
    Inst.setOpcode(XCore::CRC_l3r);
    return DecodeL3RSrcDstInstruction(Inst, Insn, Address, Decoder);
  case 0x18c:
    Inst.setOpcode(XCore::REMS_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  case 0x19c:
    Inst.setOpcode(XCore::REMU_l3r);
    return DecodeL3RInstruction(Inst, Insn, Address, Decoder);
  }
  return MCDisassembler::Fail;
}

static DecodeStatus DecodeL2RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(fieldFromInstruction(Insn, 0, 16),
                                        Op1, Op2);
  if (S != MCDisassembler::Success)
    return DecodeL2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  return S;
}

static DecodeStatus DecodeLR2RInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  unsigned Op1, Op2;
  DecodeStatus S = Decode2OpInstruction(fieldFromInstruction(Insn, 0, 16),
                                        Op1, Op2);
  if (S != MCDisassembler::Success)
    return DecodeL2OpInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  return S;
}

static DecodeStatus Decode3RInstruction(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S = Decode3OpInstruction(Insn, Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  }
  return S;
}

static DecodeStatus Decode3RImmInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S = Decode3OpInstruction(Insn, Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    Inst.addOperand(MCOperand::createImm(Op1));
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  }
  return S;
}

static DecodeStatus Decode2RUSInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S = Decode3OpInstruction(Insn, Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    Inst.addOperand(MCOperand::createImm(Op3));
  }
  return S;
}

static DecodeStatus Decode2RUSBitpInstruction(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S = Decode3OpInstruction(Insn, Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeBitpOperand(Inst, Op3, Address, Decoder);
  }
  return S;
}

static DecodeStatus DecodeL3RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S =
    Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  }
  return S;
}

static DecodeStatus DecodeL3RSrcDstInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S =
  Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  }
  return S;
}

static DecodeStatus DecodeL2RUSInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S =
  Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    Inst.addOperand(MCOperand::createImm(Op3));
  }
  return S;
}

static DecodeStatus DecodeL2RUSBitpInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  DecodeStatus S =
  Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeBitpOperand(Inst, Op3, Address, Decoder);
  }
  return S;
}

static DecodeStatus DecodeL6RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3, Op4, Op5, Op6;
  DecodeStatus S =
    Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S != MCDisassembler::Success)
    return S;
  S = Decode3OpInstruction(fieldFromInstruction(Insn, 16, 16), Op4, Op5, Op6);
  if (S != MCDisassembler::Success)
    return S;
  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op4, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op5, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op6, Address, Decoder);
  return S;
}

static DecodeStatus DecodeL5RInstructionFail(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  // Try and decode as a L6R instruction.
  Inst.clear();
  unsigned Opcode = fieldFromInstruction(Insn, 27, 5);
  switch (Opcode) {
  case 0x00:
    Inst.setOpcode(XCore::LMUL_l6r);
    return DecodeL6RInstruction(Inst, Insn, Address, Decoder);
  }
  return MCDisassembler::Fail;
}

static DecodeStatus DecodeL5RInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3, Op4, Op5;
  DecodeStatus S =
    Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S != MCDisassembler::Success)
    return DecodeL5RInstructionFail(Inst, Insn, Address, Decoder);
  S = Decode2OpInstruction(fieldFromInstruction(Insn, 16, 16), Op4, Op5);
  if (S != MCDisassembler::Success)
    return DecodeL5RInstructionFail(Inst, Insn, Address, Decoder);

  DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op4, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  DecodeGRRegsRegisterClass(Inst, Op5, Address, Decoder);
  return S;
}

static DecodeStatus DecodeL4RSrcDstInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  unsigned Op4 = fieldFromInstruction(Insn, 16, 4);
  DecodeStatus S =
    Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    S = DecodeGRRegsRegisterClass(Inst, Op4, Address, Decoder);
  }
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op4, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  }
  return S;
}

static DecodeStatus
DecodeL4RSrcDstSrcDstInstruction(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  unsigned Op1, Op2, Op3;
  unsigned Op4 = fieldFromInstruction(Insn, 16, 4);
  DecodeStatus S =
  Decode3OpInstruction(fieldFromInstruction(Insn, 0, 16), Op1, Op2, Op3);
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    S = DecodeGRRegsRegisterClass(Inst, Op4, Address, Decoder);
  }
  if (S == MCDisassembler::Success) {
    DecodeGRRegsRegisterClass(Inst, Op1, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op4, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op2, Address, Decoder);
    DecodeGRRegsRegisterClass(Inst, Op3, Address, Decoder);
  }
  return S;
}

MCDisassembler::DecodeStatus
XCoreDisassembler::getInstruction(MCInst &instr, uint64_t &Size,
                                  ArrayRef<uint8_t> Bytes, uint64_t Address,
                                  raw_ostream &cStream) const {
  uint16_t insn16;

  if (!readInstruction16(Bytes, Address, Size, insn16)) {
    return Fail;
  }

  // Calling the auto-generated decoder function.
  DecodeStatus Result = decodeInstruction(DecoderTable16, instr, insn16,
                                          Address, this, STI);
  if (Result != Fail) {
    Size = 2;
    return Result;
  }

  uint32_t insn32;

  if (!readInstruction32(Bytes, Address, Size, insn32)) {
    return Fail;
  }

  // Calling the auto-generated decoder function.
  Result = decodeInstruction(DecoderTable32, instr, insn32, Address, this, STI);
  if (Result != Fail) {
    Size = 4;
    return Result;
  }

  return Fail;
}

static MCDisassembler *createXCoreDisassembler(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx) {
  return new XCoreDisassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeXCoreDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheXCoreTarget(),
                                         createXCoreDisassembler);
}
