//===- HexagonDisassembler.cpp - Disassembler for Hexagon ISA -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCChecker.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "TargetInfo/HexagonTargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

#define DEBUG_TYPE "hexagon-disassembler"

using namespace llvm;
using namespace Hexagon;

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

/// Hexagon disassembler for all Hexagon platforms.
class HexagonDisassembler : public MCDisassembler {
public:
  std::unique_ptr<MCInstrInfo const> const MCII;
  std::unique_ptr<MCInst *> CurrentBundle;
  mutable MCInst const *CurrentExtender;

  HexagonDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                      MCInstrInfo const *MCII)
      : MCDisassembler(STI, Ctx), MCII(MCII), CurrentBundle(new MCInst *),
        CurrentExtender(nullptr) {}

  DecodeStatus getSingleInstruction(MCInst &Instr, MCInst &MCB,
                                    ArrayRef<uint8_t> Bytes, uint64_t Address,
                                    raw_ostream &CStream, bool &Complete) const;
  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
  void remapInstruction(MCInst &Instr) const;
};

static uint64_t fullValue(HexagonDisassembler const &Disassembler, MCInst &MI,
                          int64_t Value) {
  MCInstrInfo MCII = *Disassembler.MCII;
  if (!Disassembler.CurrentExtender ||
      MI.size() != HexagonMCInstrInfo::getExtendableOp(MCII, MI))
    return Value;
  unsigned Alignment = HexagonMCInstrInfo::getExtentAlignment(MCII, MI);
  uint32_t Lower6 = static_cast<uint32_t>(Value >> Alignment) & 0x3f;
  int64_t Bits;
  bool Success =
      Disassembler.CurrentExtender->getOperand(0).getExpr()->evaluateAsAbsolute(
          Bits);
  assert(Success);
  (void)Success;
  uint64_t Upper26 = static_cast<uint64_t>(Bits);
  uint64_t Operand = Upper26 | Lower6;
  return Operand;
}
static HexagonDisassembler const &disassembler(const MCDisassembler *Decoder) {
  return *static_cast<HexagonDisassembler const *>(Decoder);
}
template <size_t T>
static void signedDecoder(MCInst &MI, unsigned tmp,
                          const MCDisassembler *Decoder) {
  HexagonDisassembler const &Disassembler = disassembler(Decoder);
  int64_t FullValue = fullValue(Disassembler, MI, SignExtend64<T>(tmp));
  int64_t Extended = SignExtend64<32>(FullValue);
  HexagonMCInstrInfo::addConstant(MI, Extended, Disassembler.getContext());
}
}

// Forward declare these because the auto-generated code will reference them.
// Definitions are further down.

static DecodeStatus DecodeIntRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus
DecodeGeneralSubRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                  uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus
DecodeIntRegsLow8RegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder);
static DecodeStatus DecodeHvxVRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus
DecodeDoubleRegsRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                              const MCDisassembler *Decoder);
static DecodeStatus
DecodeGeneralDoubleLow8RegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeHvxWRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeHvxVQRRegisterClass(MCInst &Inst, unsigned RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder);
static DecodeStatus DecodePredRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder);
static DecodeStatus DecodeHvxQRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeCtrRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeGuestRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                                 uint64_t Address,
                                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeSysRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeModRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeCtrRegs64RegisterClass(MCInst &Inst, unsigned RegNo,
                                                 uint64_t Address,
                                                 const MCDisassembler *Decoder);
static DecodeStatus
DecodeGuestRegs64RegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder);
static DecodeStatus DecodeSysRegs64RegisterClass(MCInst &Inst, unsigned RegNo,
                                                 uint64_t Address,
                                                 const MCDisassembler *Decoder);

static DecodeStatus unsignedImmDecoder(MCInst &MI, unsigned tmp,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder);
static DecodeStatus s32_0ImmDecoder(MCInst &MI, unsigned tmp,
                                    uint64_t /*Address*/,
                                    const MCDisassembler *Decoder);
static DecodeStatus brtargetDecoder(MCInst &MI, unsigned tmp, uint64_t Address,
                                    const MCDisassembler *Decoder);
#include "HexagonDepDecoders.inc"
#include "HexagonGenDisassemblerTables.inc"

static MCDisassembler *createHexagonDisassembler(const Target &T,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  return new HexagonDisassembler(STI, Ctx, T.createMCInstrInfo());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHexagonDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheHexagonTarget(),
                                         createHexagonDisassembler);
}

DecodeStatus HexagonDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &cs) const {
  DecodeStatus Result = DecodeStatus::Success;
  bool Complete = false;
  Size = 0;

  *CurrentBundle = &MI;
  MI.setOpcode(Hexagon::BUNDLE);
  MI.addOperand(MCOperand::createImm(0));
  while (Result == Success && !Complete) {
    if (Bytes.size() < HEXAGON_INSTR_SIZE)
      return MCDisassembler::Fail;
    MCInst *Inst = getContext().createMCInst();
    Result = getSingleInstruction(*Inst, MI, Bytes, Address, cs, Complete);
    MI.addOperand(MCOperand::createInst(Inst));
    Size += HEXAGON_INSTR_SIZE;
    Bytes = Bytes.slice(HEXAGON_INSTR_SIZE);
  }
  if (Result == MCDisassembler::Fail)
    return Result;
  if (Size > HEXAGON_MAX_PACKET_SIZE)
    return MCDisassembler::Fail;

  const auto ArchSTI = Hexagon_MC::getArchSubtarget(&STI);
  const auto STI_ = (ArchSTI != nullptr) ? *ArchSTI : STI;
  HexagonMCChecker Checker(getContext(), *MCII, STI_, MI,
                           *getContext().getRegisterInfo(), false);
  if (!Checker.check())
    return MCDisassembler::Fail;
  remapInstruction(MI);
  return MCDisassembler::Success;
}

void HexagonDisassembler::remapInstruction(MCInst &Instr) const {
  for (auto I: HexagonMCInstrInfo::bundleInstructions(Instr)) {
    auto &MI = const_cast<MCInst &>(*I.getInst());
    switch (MI.getOpcode()) {
    case Hexagon::S2_allocframe:
      if (MI.getOperand(0).getReg() == Hexagon::R29) {
        MI.setOpcode(Hexagon::S6_allocframe_to_raw);
        MI.erase(MI.begin () + 1);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L2_deallocframe:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(1).getReg() == Hexagon::R30) {
        MI.setOpcode(L6_deallocframe_map_to_raw);
        MI.erase(MI.begin () + 1);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L4_return:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(1).getReg() == Hexagon::R30) {
        MI.setOpcode(L6_return_map_to_raw);
        MI.erase(MI.begin () + 1);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L4_return_t:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(2).getReg() == Hexagon::R30) {
        MI.setOpcode(L4_return_map_to_raw_t);
        MI.erase(MI.begin () + 2);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L4_return_f:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(2).getReg() == Hexagon::R30) {
        MI.setOpcode(L4_return_map_to_raw_f);
        MI.erase(MI.begin () + 2);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L4_return_tnew_pt:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(2).getReg() == Hexagon::R30) {
        MI.setOpcode(L4_return_map_to_raw_tnew_pt);
        MI.erase(MI.begin () + 2);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L4_return_fnew_pt:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(2).getReg() == Hexagon::R30) {
        MI.setOpcode(L4_return_map_to_raw_fnew_pt);
        MI.erase(MI.begin () + 2);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L4_return_tnew_pnt:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(2).getReg() == Hexagon::R30) {
        MI.setOpcode(L4_return_map_to_raw_tnew_pnt);
        MI.erase(MI.begin () + 2);
        MI.erase(MI.begin ());
      }
      break;
    case Hexagon::L4_return_fnew_pnt:
      if (MI.getOperand(0).getReg() == Hexagon::D15 &&
          MI.getOperand(2).getReg() == Hexagon::R30) {
        MI.setOpcode(L4_return_map_to_raw_fnew_pnt);
        MI.erase(MI.begin () + 2);
        MI.erase(MI.begin ());
      }
      break;
    }
  }
}

static void adjustDuplex(MCInst &MI, MCContext &Context) {
  switch (MI.getOpcode()) {
  case Hexagon::SA1_setin1:
    MI.insert(MI.begin() + 1,
              MCOperand::createExpr(MCConstantExpr::create(-1, Context)));
    break;
  case Hexagon::SA1_dec:
    MI.insert(MI.begin() + 2,
              MCOperand::createExpr(MCConstantExpr::create(-1, Context)));
    break;
  default:
    break;
  }
}

DecodeStatus HexagonDisassembler::getSingleInstruction(MCInst &MI, MCInst &MCB,
                                                       ArrayRef<uint8_t> Bytes,
                                                       uint64_t Address,
                                                       raw_ostream &cs,
                                                       bool &Complete) const {
  assert(Bytes.size() >= HEXAGON_INSTR_SIZE);

  uint32_t Instruction = support::endian::read32le(Bytes.data());

  auto BundleSize = HexagonMCInstrInfo::bundleSize(MCB);
  if ((Instruction & HexagonII::INST_PARSE_MASK) ==
      HexagonII::INST_PARSE_LOOP_END) {
    if (BundleSize == 0)
      HexagonMCInstrInfo::setInnerLoop(MCB);
    else if (BundleSize == 1)
      HexagonMCInstrInfo::setOuterLoop(MCB);
    else
      return DecodeStatus::Fail;
  }

  CurrentExtender = HexagonMCInstrInfo::extenderForIndex(
      MCB, HexagonMCInstrInfo::bundleSize(MCB));

  DecodeStatus Result = DecodeStatus::Fail;
  if ((Instruction & HexagonII::INST_PARSE_MASK) ==
      HexagonII::INST_PARSE_DUPLEX) {
    unsigned duplexIClass;
    uint8_t const *DecodeLow, *DecodeHigh;
    duplexIClass = ((Instruction >> 28) & 0xe) | ((Instruction >> 13) & 0x1);
    switch (duplexIClass) {
    default:
      return MCDisassembler::Fail;
    case 0:
      DecodeLow = DecoderTableSUBINSN_L132;
      DecodeHigh = DecoderTableSUBINSN_L132;
      break;
    case 1:
      DecodeLow = DecoderTableSUBINSN_L232;
      DecodeHigh = DecoderTableSUBINSN_L132;
      break;
    case 2:
      DecodeLow = DecoderTableSUBINSN_L232;
      DecodeHigh = DecoderTableSUBINSN_L232;
      break;
    case 3:
      DecodeLow = DecoderTableSUBINSN_A32;
      DecodeHigh = DecoderTableSUBINSN_A32;
      break;
    case 4:
      DecodeLow = DecoderTableSUBINSN_L132;
      DecodeHigh = DecoderTableSUBINSN_A32;
      break;
    case 5:
      DecodeLow = DecoderTableSUBINSN_L232;
      DecodeHigh = DecoderTableSUBINSN_A32;
      break;
    case 6:
      DecodeLow = DecoderTableSUBINSN_S132;
      DecodeHigh = DecoderTableSUBINSN_A32;
      break;
    case 7:
      DecodeLow = DecoderTableSUBINSN_S232;
      DecodeHigh = DecoderTableSUBINSN_A32;
      break;
    case 8:
      DecodeLow = DecoderTableSUBINSN_S132;
      DecodeHigh = DecoderTableSUBINSN_L132;
      break;
    case 9:
      DecodeLow = DecoderTableSUBINSN_S132;
      DecodeHigh = DecoderTableSUBINSN_L232;
      break;
    case 10:
      DecodeLow = DecoderTableSUBINSN_S132;
      DecodeHigh = DecoderTableSUBINSN_S132;
      break;
    case 11:
      DecodeLow = DecoderTableSUBINSN_S232;
      DecodeHigh = DecoderTableSUBINSN_S132;
      break;
    case 12:
      DecodeLow = DecoderTableSUBINSN_S232;
      DecodeHigh = DecoderTableSUBINSN_L132;
      break;
    case 13:
      DecodeLow = DecoderTableSUBINSN_S232;
      DecodeHigh = DecoderTableSUBINSN_L232;
      break;
    case 14:
      DecodeLow = DecoderTableSUBINSN_S232;
      DecodeHigh = DecoderTableSUBINSN_S232;
      break;
    }
    MI.setOpcode(Hexagon::DuplexIClass0 + duplexIClass);
    MCInst *MILow = getContext().createMCInst();
    MCInst *MIHigh = getContext().createMCInst();
    auto TmpExtender = CurrentExtender;
    CurrentExtender =
        nullptr; // constant extenders in duplex must always be in slot 1
    Result = decodeInstruction(DecodeLow, *MILow, Instruction & 0x1fff, Address,
                               this, STI);
    CurrentExtender = TmpExtender;
    if (Result != DecodeStatus::Success)
      return DecodeStatus::Fail;
    adjustDuplex(*MILow, getContext());
    Result = decodeInstruction(
        DecodeHigh, *MIHigh, (Instruction >> 16) & 0x1fff, Address, this, STI);
    if (Result != DecodeStatus::Success)
      return DecodeStatus::Fail;
    adjustDuplex(*MIHigh, getContext());
    MCOperand OPLow = MCOperand::createInst(MILow);
    MCOperand OPHigh = MCOperand::createInst(MIHigh);
    MI.addOperand(OPLow);
    MI.addOperand(OPHigh);
    Complete = true;
  } else {
    if ((Instruction & HexagonII::INST_PARSE_MASK) ==
        HexagonII::INST_PARSE_PACKET_END)
      Complete = true;

    if (CurrentExtender != nullptr)
      Result = decodeInstruction(DecoderTableMustExtend32, MI, Instruction,
                                 Address, this, STI);

    if (Result != MCDisassembler::Success)
      Result = decodeInstruction(DecoderTable32, MI, Instruction, Address, this,
                                 STI);

    if (Result != MCDisassembler::Success &&
        STI.hasFeature(Hexagon::ExtensionHVX))
      Result = decodeInstruction(DecoderTableEXT_mmvec32, MI, Instruction,
                                 Address, this, STI);

  }

  switch (MI.getOpcode()) {
  case Hexagon::J4_cmpeqn1_f_jumpnv_nt:
  case Hexagon::J4_cmpeqn1_f_jumpnv_t:
  case Hexagon::J4_cmpeqn1_fp0_jump_nt:
  case Hexagon::J4_cmpeqn1_fp0_jump_t:
  case Hexagon::J4_cmpeqn1_fp1_jump_nt:
  case Hexagon::J4_cmpeqn1_fp1_jump_t:
  case Hexagon::J4_cmpeqn1_t_jumpnv_nt:
  case Hexagon::J4_cmpeqn1_t_jumpnv_t:
  case Hexagon::J4_cmpeqn1_tp0_jump_nt:
  case Hexagon::J4_cmpeqn1_tp0_jump_t:
  case Hexagon::J4_cmpeqn1_tp1_jump_nt:
  case Hexagon::J4_cmpeqn1_tp1_jump_t:
  case Hexagon::J4_cmpgtn1_f_jumpnv_nt:
  case Hexagon::J4_cmpgtn1_f_jumpnv_t:
  case Hexagon::J4_cmpgtn1_fp0_jump_nt:
  case Hexagon::J4_cmpgtn1_fp0_jump_t:
  case Hexagon::J4_cmpgtn1_fp1_jump_nt:
  case Hexagon::J4_cmpgtn1_fp1_jump_t:
  case Hexagon::J4_cmpgtn1_t_jumpnv_nt:
  case Hexagon::J4_cmpgtn1_t_jumpnv_t:
  case Hexagon::J4_cmpgtn1_tp0_jump_nt:
  case Hexagon::J4_cmpgtn1_tp0_jump_t:
  case Hexagon::J4_cmpgtn1_tp1_jump_nt:
  case Hexagon::J4_cmpgtn1_tp1_jump_t:
    MI.insert(MI.begin() + 1,
              MCOperand::createExpr(MCConstantExpr::create(-1, getContext())));
    break;
  default:
    break;
  }

  if (HexagonMCInstrInfo::isNewValue(*MCII, MI)) {
    unsigned OpIndex = HexagonMCInstrInfo::getNewValueOp(*MCII, MI);
    MCOperand &MCO = MI.getOperand(OpIndex);
    assert(MCO.isReg() && "New value consumers must be registers");
    unsigned Register =
        getContext().getRegisterInfo()->getEncodingValue(MCO.getReg());
    if ((Register & 0x6) == 0)
      // HexagonPRM 10.11 Bit 1-2 == 0 is reserved
      return MCDisassembler::Fail;
    unsigned Lookback = (Register & 0x6) >> 1;
    unsigned Offset = 1;
    bool Vector = HexagonMCInstrInfo::isVector(*MCII, MI);
    bool PrevVector = false;
    auto Instructions = HexagonMCInstrInfo::bundleInstructions(**CurrentBundle);
    auto i = Instructions.end() - 1;
    for (auto n = Instructions.begin() - 1;; --i, ++Offset) {
      if (i == n)
        // Couldn't find producer
        return MCDisassembler::Fail;
      bool CurrentVector = HexagonMCInstrInfo::isVector(*MCII, *i->getInst());
      if (Vector && !CurrentVector)
        // Skip scalars when calculating distances for vectors
        ++Lookback;
      if (HexagonMCInstrInfo::isImmext(*i->getInst()) && (Vector == PrevVector))
        ++Lookback;
      PrevVector = CurrentVector;
      if (Offset == Lookback)
        break;
    }
    auto const &Inst = *i->getInst();
    bool SubregBit = (Register & 0x1) != 0;
    if (HexagonMCInstrInfo::hasNewValue2(*MCII, Inst)) {
      // If subreg bit is set we're selecting the second produced newvalue
      unsigned Producer = SubregBit ?
          HexagonMCInstrInfo::getNewValueOperand(*MCII, Inst).getReg() :
          HexagonMCInstrInfo::getNewValueOperand2(*MCII, Inst).getReg();
      assert(Producer != Hexagon::NoRegister);
      MCO.setReg(Producer);
    } else if (HexagonMCInstrInfo::hasNewValue(*MCII, Inst)) {
      unsigned Producer =
          HexagonMCInstrInfo::getNewValueOperand(*MCII, Inst).getReg();

      if (HexagonMCInstrInfo::IsVecRegPair(Producer)) {
        const bool Rev = HexagonMCInstrInfo::IsReverseVecRegPair(Producer);
        const unsigned ProdPairIndex =
            Rev ? Producer - Hexagon::WR0 : Producer - Hexagon::W0;
        if (Rev)
          SubregBit = !SubregBit;
        Producer = (ProdPairIndex << 1) + SubregBit + Hexagon::V0;
      } else if (SubregBit)
        // Hexagon PRM 10.11 New-value operands
        // Nt[0] is reserved and should always be encoded as zero.
        return MCDisassembler::Fail;
      assert(Producer != Hexagon::NoRegister);
      MCO.setReg(Producer);
    } else
      return MCDisassembler::Fail;
  }

  if (CurrentExtender != nullptr) {
    MCInst const &Inst = HexagonMCInstrInfo::isDuplex(*MCII, MI)
                             ? *MI.getOperand(1).getInst()
                             : MI;
    if (!HexagonMCInstrInfo::isExtendable(*MCII, Inst) &&
        !HexagonMCInstrInfo::isExtended(*MCII, Inst))
      return MCDisassembler::Fail;
  }
  return Result;
}

static DecodeStatus DecodeRegisterClass(MCInst &Inst, unsigned RegNo,
                                        ArrayRef<MCPhysReg> Table) {
  if (RegNo < Table.size()) {
    Inst.addOperand(MCOperand::createReg(Table[RegNo]));
    return MCDisassembler::Success;
  }

  return MCDisassembler::Fail;
}

static DecodeStatus
DecodeIntRegsLow8RegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder) {
  return DecodeIntRegsRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeIntRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  static const MCPhysReg IntRegDecoderTable[] = {
      Hexagon::R0,  Hexagon::R1,  Hexagon::R2,  Hexagon::R3,  Hexagon::R4,
      Hexagon::R5,  Hexagon::R6,  Hexagon::R7,  Hexagon::R8,  Hexagon::R9,
      Hexagon::R10, Hexagon::R11, Hexagon::R12, Hexagon::R13, Hexagon::R14,
      Hexagon::R15, Hexagon::R16, Hexagon::R17, Hexagon::R18, Hexagon::R19,
      Hexagon::R20, Hexagon::R21, Hexagon::R22, Hexagon::R23, Hexagon::R24,
      Hexagon::R25, Hexagon::R26, Hexagon::R27, Hexagon::R28, Hexagon::R29,
      Hexagon::R30, Hexagon::R31};

  return DecodeRegisterClass(Inst, RegNo, IntRegDecoderTable);
}

static DecodeStatus
DecodeGeneralSubRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                  uint64_t Address,
                                  const MCDisassembler *Decoder) {
  static const MCPhysReg GeneralSubRegDecoderTable[] = {
      Hexagon::R0,  Hexagon::R1,  Hexagon::R2,  Hexagon::R3,
      Hexagon::R4,  Hexagon::R5,  Hexagon::R6,  Hexagon::R7,
      Hexagon::R16, Hexagon::R17, Hexagon::R18, Hexagon::R19,
      Hexagon::R20, Hexagon::R21, Hexagon::R22, Hexagon::R23,
  };

  return DecodeRegisterClass(Inst, RegNo, GeneralSubRegDecoderTable);
}

static DecodeStatus DecodeHvxVRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t /*Address*/,
                                             const MCDisassembler *Decoder) {
  static const MCPhysReg HvxVRDecoderTable[] = {
      Hexagon::V0,  Hexagon::V1,  Hexagon::V2,  Hexagon::V3,  Hexagon::V4,
      Hexagon::V5,  Hexagon::V6,  Hexagon::V7,  Hexagon::V8,  Hexagon::V9,
      Hexagon::V10, Hexagon::V11, Hexagon::V12, Hexagon::V13, Hexagon::V14,
      Hexagon::V15, Hexagon::V16, Hexagon::V17, Hexagon::V18, Hexagon::V19,
      Hexagon::V20, Hexagon::V21, Hexagon::V22, Hexagon::V23, Hexagon::V24,
      Hexagon::V25, Hexagon::V26, Hexagon::V27, Hexagon::V28, Hexagon::V29,
      Hexagon::V30, Hexagon::V31};

  return DecodeRegisterClass(Inst, RegNo, HvxVRDecoderTable);
}

static DecodeStatus
DecodeDoubleRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                              uint64_t /*Address*/,
                              const MCDisassembler *Decoder) {
  static const MCPhysReg DoubleRegDecoderTable[] = {
      Hexagon::D0,  Hexagon::D1,  Hexagon::D2,  Hexagon::D3,
      Hexagon::D4,  Hexagon::D5,  Hexagon::D6,  Hexagon::D7,
      Hexagon::D8,  Hexagon::D9,  Hexagon::D10, Hexagon::D11,
      Hexagon::D12, Hexagon::D13, Hexagon::D14, Hexagon::D15};

  return DecodeRegisterClass(Inst, RegNo >> 1, DoubleRegDecoderTable);
}

static DecodeStatus
DecodeGeneralDoubleLow8RegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                         uint64_t /*Address*/,
                                         const MCDisassembler *Decoder) {
  static const MCPhysReg GeneralDoubleLow8RegDecoderTable[] = {
      Hexagon::D0, Hexagon::D1, Hexagon::D2,  Hexagon::D3,
      Hexagon::D8, Hexagon::D9, Hexagon::D10, Hexagon::D11};

  return DecodeRegisterClass(Inst, RegNo, GeneralDoubleLow8RegDecoderTable);
}

static DecodeStatus DecodeHvxWRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t /*Address*/,
                                             const MCDisassembler *Decoder) {
  static const MCPhysReg HvxWRDecoderTable[] = {
      Hexagon::W0,   Hexagon::WR0,  Hexagon::W1,   Hexagon::WR1,  Hexagon::W2,
      Hexagon::WR2,  Hexagon::W3,   Hexagon::WR3,  Hexagon::W4,   Hexagon::WR4,
      Hexagon::W5,   Hexagon::WR5,  Hexagon::W6,   Hexagon::WR6,  Hexagon::W7,
      Hexagon::WR7,  Hexagon::W8,   Hexagon::WR8,  Hexagon::W9,   Hexagon::WR9,
      Hexagon::W10,  Hexagon::WR10, Hexagon::W11,  Hexagon::WR11, Hexagon::W12,
      Hexagon::WR12, Hexagon::W13,  Hexagon::WR13, Hexagon::W14,  Hexagon::WR14,
      Hexagon::W15,  Hexagon::WR15,
  };

  return DecodeRegisterClass(Inst, RegNo, HvxWRDecoderTable);
}

LLVM_ATTRIBUTE_UNUSED // Suppress warning temporarily.
    static DecodeStatus
    DecodeHvxVQRRegisterClass(MCInst &Inst, unsigned RegNo,
                              uint64_t /*Address*/,
                              const MCDisassembler *Decoder) {
  static const MCPhysReg HvxVQRDecoderTable[] = {
      Hexagon::VQ0,  Hexagon::VQ1,  Hexagon::VQ2,  Hexagon::VQ3,
      Hexagon::VQ4,  Hexagon::VQ5,  Hexagon::VQ6,  Hexagon::VQ7};

  return DecodeRegisterClass(Inst, RegNo >> 2, HvxVQRDecoderTable);
}

static DecodeStatus DecodePredRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                                uint64_t /*Address*/,
                                                const MCDisassembler *Decoder) {
  static const MCPhysReg PredRegDecoderTable[] = {Hexagon::P0, Hexagon::P1,
                                                  Hexagon::P2, Hexagon::P3};

  return DecodeRegisterClass(Inst, RegNo, PredRegDecoderTable);
}

static DecodeStatus DecodeHvxQRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t /*Address*/,
                                             const MCDisassembler *Decoder) {
  static const MCPhysReg HvxQRDecoderTable[] = {Hexagon::Q0, Hexagon::Q1,
                                                Hexagon::Q2, Hexagon::Q3};

  return DecodeRegisterClass(Inst, RegNo, HvxQRDecoderTable);
}

static DecodeStatus DecodeCtrRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t /*Address*/,
                                               const MCDisassembler *Decoder) {
  using namespace Hexagon;

  static const MCPhysReg CtrlRegDecoderTable[] = {
    /*  0 */  SA0,        LC0,        SA1,        LC1,
    /*  4 */  P3_0,       C5,         M0,         M1,
    /*  8 */  USR,        PC,         UGP,        GP,
    /* 12 */  CS0,        CS1,        UPCYCLELO,  UPCYCLEHI,
    /* 16 */  FRAMELIMIT, FRAMEKEY,   PKTCOUNTLO, PKTCOUNTHI,
    /* 20 */  0,          0,          0,          0,
    /* 24 */  0,          0,          0,          0,
    /* 28 */  0,          0,          UTIMERLO,   UTIMERHI
  };

  if (RegNo >= std::size(CtrlRegDecoderTable))
    return MCDisassembler::Fail;

  static_assert(NoRegister == 0, "Expecting NoRegister to be 0");
  if (CtrlRegDecoderTable[RegNo] == NoRegister)
    return MCDisassembler::Fail;

  unsigned Register = CtrlRegDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeCtrRegs64RegisterClass(MCInst &Inst, unsigned RegNo, uint64_t /*Address*/,
                             const MCDisassembler *Decoder) {
  using namespace Hexagon;

  static const MCPhysReg CtrlReg64DecoderTable[] = {
    /*  0 */  C1_0,       0,          C3_2,       0,
    /*  4 */  C5_4,       0,          C7_6,       0,
    /*  8 */  C9_8,       0,          C11_10,     0,
    /* 12 */  CS,         0,          UPCYCLE,    0,
    /* 16 */  C17_16,     0,          PKTCOUNT,   0,
    /* 20 */  0,          0,          0,          0,
    /* 24 */  0,          0,          0,          0,
    /* 28 */  0,          0,          UTIMER,     0
  };

  if (RegNo >= std::size(CtrlReg64DecoderTable))
    return MCDisassembler::Fail;

  static_assert(NoRegister == 0, "Expecting NoRegister to be 0");
  if (CtrlReg64DecoderTable[RegNo] == NoRegister)
    return MCDisassembler::Fail;

  unsigned Register = CtrlReg64DecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeModRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t /*Address*/,
                                               const MCDisassembler *Decoder) {
  unsigned Register = 0;
  switch (RegNo) {
  case 0:
    Register = Hexagon::M0;
    break;
  case 1:
    Register = Hexagon::M1;
    break;
  default:
    return MCDisassembler::Fail;
  }
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus unsignedImmDecoder(MCInst &MI, unsigned tmp,
                                       uint64_t /*Address*/,
                                       const MCDisassembler *Decoder) {
  HexagonDisassembler const &Disassembler = disassembler(Decoder);
  int64_t FullValue = fullValue(Disassembler, MI, tmp);
  assert(FullValue >= 0 && "Negative in unsigned decoder");
  HexagonMCInstrInfo::addConstant(MI, FullValue, Disassembler.getContext());
  return MCDisassembler::Success;
}

static DecodeStatus s32_0ImmDecoder(MCInst &MI, unsigned tmp,
                                    uint64_t /*Address*/,
                                    const MCDisassembler *Decoder) {
  HexagonDisassembler const &Disassembler = disassembler(Decoder);
  unsigned Bits = HexagonMCInstrInfo::getExtentBits(*Disassembler.MCII, MI);
  tmp = SignExtend64(tmp, Bits);
  signedDecoder<32>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}

// custom decoder for various jump/call immediates
static DecodeStatus brtargetDecoder(MCInst &MI, unsigned tmp, uint64_t Address,
                                    const MCDisassembler *Decoder) {
  HexagonDisassembler const &Disassembler = disassembler(Decoder);
  unsigned Bits = HexagonMCInstrInfo::getExtentBits(*Disassembler.MCII, MI);
  // r13_2 is not extendable, so if there are no extent bits, it's r13_2
  if (Bits == 0)
    Bits = 15;
  uint64_t FullValue = fullValue(Disassembler, MI, SignExtend64(tmp, Bits));
  uint32_t Extended = FullValue + Address;
  if (!Disassembler.tryAddingSymbolicOperand(MI, Extended, Address, true, 0, 0,
                                             4))
    HexagonMCInstrInfo::addConstant(MI, Extended, Disassembler.getContext());
  return MCDisassembler::Success;
}

static const uint16_t SysRegDecoderTable[] = {
    Hexagon::SGP0,       Hexagon::SGP1,      Hexagon::STID,
    Hexagon::ELR,        Hexagon::BADVA0,    Hexagon::BADVA1,
    Hexagon::SSR,        Hexagon::CCR,       Hexagon::HTID,
    Hexagon::BADVA,      Hexagon::IMASK,     Hexagon::S11,
    Hexagon::S12,        Hexagon::S13,       Hexagon::S14,
    Hexagon::S15,        Hexagon::EVB,       Hexagon::MODECTL,
    Hexagon::SYSCFG,     Hexagon::S19,       Hexagon::S20,
    Hexagon::VID,        Hexagon::S22,       Hexagon::S23,
    Hexagon::S24,        Hexagon::S25,       Hexagon::S26,
    Hexagon::CFGBASE,    Hexagon::DIAG,      Hexagon::REV,
    Hexagon::PCYCLELO,   Hexagon::PCYCLEHI,  Hexagon::ISDBST,
    Hexagon::ISDBCFG0,   Hexagon::ISDBCFG1,  Hexagon::S35,
    Hexagon::BRKPTPC0,   Hexagon::BRKPTCFG0, Hexagon::BRKPTPC1,
    Hexagon::BRKPTCFG1,  Hexagon::ISDBMBXIN, Hexagon::ISDBMBXOUT,
    Hexagon::ISDBEN,     Hexagon::ISDBGPR,   Hexagon::S44,
    Hexagon::S45,        Hexagon::S46,       Hexagon::S47,
    Hexagon::PMUCNT0,    Hexagon::PMUCNT1,   Hexagon::PMUCNT2,
    Hexagon::PMUCNT3,    Hexagon::PMUEVTCFG, Hexagon::PMUCFG,
    Hexagon::S54,        Hexagon::S55,       Hexagon::S56,
    Hexagon::S57,        Hexagon::S58,       Hexagon::S59,
    Hexagon::S60,        Hexagon::S61,       Hexagon::S62,
    Hexagon::S63,        Hexagon::S64,       Hexagon::S65,
    Hexagon::S66,        Hexagon::S67,       Hexagon::S68,
    Hexagon::S69,        Hexagon::S70,       Hexagon::S71,
    Hexagon::S72,        Hexagon::S73,       Hexagon::S74,
    Hexagon::S75,        Hexagon::S76,       Hexagon::S77,
    Hexagon::S78,        Hexagon::S79,       Hexagon::S80,
};

static DecodeStatus DecodeSysRegsRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t /*Address*/,
                                               const MCDisassembler *Decoder) {
  if (RegNo >= std::size(SysRegDecoderTable))
    return MCDisassembler::Fail;

  if (SysRegDecoderTable[RegNo] == Hexagon::NoRegister)
    return MCDisassembler::Fail;

  unsigned Register = SysRegDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static const uint16_t SysReg64DecoderTable[] = {
    Hexagon::SGP1_0, Hexagon::S3_2,   Hexagon::S5_4,   Hexagon::S7_6,
    Hexagon::S9_8,   Hexagon::S11_10, Hexagon::S13_12, Hexagon::S15_14,
    Hexagon::S17_16, Hexagon::S19_18, Hexagon::S21_20, Hexagon::S23_22,
    Hexagon::S25_24, Hexagon::S27_26, Hexagon::S29_28, Hexagon::S31_30,
    Hexagon::S33_32, Hexagon::S35_34, Hexagon::S37_36, Hexagon::S39_38,
    Hexagon::S41_40, Hexagon::S43_42, Hexagon::S45_44, Hexagon::S47_46,
    Hexagon::S49_48, Hexagon::S51_50, Hexagon::S53_52, Hexagon::S55_54,
    Hexagon::S57_56, Hexagon::S59_58, Hexagon::S61_60, Hexagon::S63_62,
    Hexagon::S65_64, Hexagon::S67_66, Hexagon::S69_68, Hexagon::S71_70,
    Hexagon::S73_72, Hexagon::S75_74, Hexagon::S77_76, Hexagon::S79_78,
};

static DecodeStatus
DecodeSysRegs64RegisterClass(MCInst &Inst, unsigned RegNo, uint64_t /*Address*/,
                             const MCDisassembler *Decoder) {
  RegNo = RegNo >> 1;
  if (RegNo >= std::size(SysReg64DecoderTable))
    return MCDisassembler::Fail;

  if (SysReg64DecoderTable[RegNo] == Hexagon::NoRegister)
    return MCDisassembler::Fail;

  unsigned Register = SysReg64DecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeGuestRegsRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t /*Address*/,
                             const MCDisassembler *Decoder) {
  using namespace Hexagon;

  static const MCPhysReg GuestRegDecoderTable[] = {
    /*  0 */ GELR,      GSR,        GOSP,       G3,
    /*  4 */ G4,        G5,         G6,         G7,
    /*  8 */ G8,        G9,         G10,        G11,
    /* 12 */ G12,       G13,        G14,        G15,
    /* 16 */ GPMUCNT4,  GPMUCNT5,   GPMUCNT6,   GPMUCNT7,
    /* 20 */ G20,       G21,        G22,        G23,
    /* 24 */ GPCYCLELO, GPCYCLEHI,  GPMUCNT0,   GPMUCNT1,
    /* 28 */ GPMUCNT2,  GPMUCNT3,   G30,        G31
  };

  if (RegNo >= std::size(GuestRegDecoderTable))
    return MCDisassembler::Fail;
  if (GuestRegDecoderTable[RegNo] == Hexagon::NoRegister)
    return MCDisassembler::Fail;

  unsigned Register = GuestRegDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeGuestRegs64RegisterClass(MCInst &Inst, unsigned RegNo,
                               uint64_t /*Address*/,
                               const MCDisassembler *Decoder) {
  using namespace Hexagon;

  static const MCPhysReg GuestReg64DecoderTable[] = {
    /*  0 */ G1_0,      0,          G3_2,       0,
    /*  4 */ G5_4,      0,          G7_6,       0,
    /*  8 */ G9_8,      0,          G11_10,     0,
    /* 12 */ G13_12,    0,          G15_14,     0,
    /* 16 */ G17_16,    0,          G19_18,     0,
    /* 20 */ G21_20,    0,          G23_22,     0,
    /* 24 */ G25_24,    0,          G27_26,     0,
    /* 28 */ G29_28,    0,          G31_30,     0
  };

  if (RegNo >= std::size(GuestReg64DecoderTable))
    return MCDisassembler::Fail;
  if (GuestReg64DecoderTable[RegNo] == Hexagon::NoRegister)
    return MCDisassembler::Fail;

  unsigned Register = GuestReg64DecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}
