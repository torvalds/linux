//===-- RISCVMCCodeEmitter.cpp - Convert RISC-V code to machine code ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RISCVMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVBaseInfo.h"
#include "MCTargetDesc/RISCVFixupKinds.h"
#include "MCTargetDesc/RISCVMCExpr.h"
#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");
STATISTIC(MCNumFixups, "Number of MC fixups created");

namespace {
class RISCVMCCodeEmitter : public MCCodeEmitter {
  RISCVMCCodeEmitter(const RISCVMCCodeEmitter &) = delete;
  void operator=(const RISCVMCCodeEmitter &) = delete;
  MCContext &Ctx;
  MCInstrInfo const &MCII;

public:
  RISCVMCCodeEmitter(MCContext &ctx, MCInstrInfo const &MCII)
      : Ctx(ctx), MCII(MCII) {}

  ~RISCVMCCodeEmitter() override = default;

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  void expandFunctionCall(const MCInst &MI, SmallVectorImpl<char> &CB,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  void expandTLSDESCCall(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  void expandAddTPRel(const MCInst &MI, SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups,
                      const MCSubtargetInfo &STI) const;

  void expandLongCondBr(const MCInst &MI, SmallVectorImpl<char> &CB,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// Return binary encoding of operand. If the machine operand requires
  /// relocation, record the relocation and return zero.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getImmOpValue(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  unsigned getVMaskReg(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const;

  unsigned getRlistOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  unsigned getRegReg(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const;
};
} // end anonymous namespace

MCCodeEmitter *llvm::createRISCVMCCodeEmitter(const MCInstrInfo &MCII,
                                              MCContext &Ctx) {
  return new RISCVMCCodeEmitter(Ctx, MCII);
}

// Expand PseudoCALL(Reg), PseudoTAIL and PseudoJump to AUIPC and JALR with
// relocation types. We expand those pseudo-instructions while encoding them,
// meaning AUIPC and JALR won't go through RISC-V MC to MC compressed
// instruction transformation. This is acceptable because AUIPC has no 16-bit
// form and C_JALR has no immediate operand field.  We let linker relaxation
// deal with it. When linker relaxation is enabled, AUIPC and JALR have a
// chance to relax to JAL.
// If the C extension is enabled, JAL has a chance relax to C_JAL.
void RISCVMCCodeEmitter::expandFunctionCall(const MCInst &MI,
                                            SmallVectorImpl<char> &CB,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  MCInst TmpInst;
  MCOperand Func;
  MCRegister Ra;
  if (MI.getOpcode() == RISCV::PseudoTAIL) {
    Func = MI.getOperand(0);
    Ra = RISCV::X6;
    // For Zicfilp, PseudoTAIL should be expanded to a software guarded branch.
    // It means to use t2(x7) as rs1 of JALR to expand PseudoTAIL.
    if (STI.hasFeature(RISCV::FeatureStdExtZicfilp))
      Ra = RISCV::X7;
  } else if (MI.getOpcode() == RISCV::PseudoCALLReg) {
    Func = MI.getOperand(1);
    Ra = MI.getOperand(0).getReg();
  } else if (MI.getOpcode() == RISCV::PseudoCALL) {
    Func = MI.getOperand(0);
    Ra = RISCV::X1;
  } else if (MI.getOpcode() == RISCV::PseudoJump) {
    Func = MI.getOperand(1);
    Ra = MI.getOperand(0).getReg();
  }
  uint32_t Binary;

  assert(Func.isExpr() && "Expected expression");

  const MCExpr *CallExpr = Func.getExpr();

  // Emit AUIPC Ra, Func with R_RISCV_CALL relocation type.
  TmpInst = MCInstBuilder(RISCV::AUIPC).addReg(Ra).addExpr(CallExpr);
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);

  if (MI.getOpcode() == RISCV::PseudoTAIL ||
      MI.getOpcode() == RISCV::PseudoJump)
    // Emit JALR X0, Ra, 0
    TmpInst = MCInstBuilder(RISCV::JALR).addReg(RISCV::X0).addReg(Ra).addImm(0);
  else
    // Emit JALR Ra, Ra, 0
    TmpInst = MCInstBuilder(RISCV::JALR).addReg(Ra).addReg(Ra).addImm(0);
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);
}

void RISCVMCCodeEmitter::expandTLSDESCCall(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  MCOperand SrcSymbol = MI.getOperand(3);
  assert(SrcSymbol.isExpr() &&
         "Expected expression as first input to TLSDESCCALL");
  const RISCVMCExpr *Expr = dyn_cast<RISCVMCExpr>(SrcSymbol.getExpr());
  MCRegister Link = MI.getOperand(0).getReg();
  MCRegister Dest = MI.getOperand(1).getReg();
  MCRegister Imm = MI.getOperand(2).getImm();
  Fixups.push_back(MCFixup::create(
      0, Expr, MCFixupKind(RISCV::fixup_riscv_tlsdesc_call), MI.getLoc()));
  MCInst Call =
      MCInstBuilder(RISCV::JALR).addReg(Link).addReg(Dest).addImm(Imm);

  uint32_t Binary = getBinaryCodeForInstr(Call, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);
}

// Expand PseudoAddTPRel to a simple ADD with the correct relocation.
void RISCVMCCodeEmitter::expandAddTPRel(const MCInst &MI,
                                        SmallVectorImpl<char> &CB,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  MCOperand DestReg = MI.getOperand(0);
  MCOperand SrcReg = MI.getOperand(1);
  MCOperand TPReg = MI.getOperand(2);
  assert(TPReg.isReg() && TPReg.getReg() == RISCV::X4 &&
         "Expected thread pointer as second input to TP-relative add");

  MCOperand SrcSymbol = MI.getOperand(3);
  assert(SrcSymbol.isExpr() &&
         "Expected expression as third input to TP-relative add");

  const RISCVMCExpr *Expr = dyn_cast<RISCVMCExpr>(SrcSymbol.getExpr());
  assert(Expr && Expr->getKind() == RISCVMCExpr::VK_RISCV_TPREL_ADD &&
         "Expected tprel_add relocation on TP-relative symbol");

  // Emit the correct tprel_add relocation for the symbol.
  Fixups.push_back(MCFixup::create(
      0, Expr, MCFixupKind(RISCV::fixup_riscv_tprel_add), MI.getLoc()));

  // Emit fixup_riscv_relax for tprel_add where the relax feature is enabled.
  if (STI.hasFeature(RISCV::FeatureRelax)) {
    const MCConstantExpr *Dummy = MCConstantExpr::create(0, Ctx);
    Fixups.push_back(MCFixup::create(
        0, Dummy, MCFixupKind(RISCV::fixup_riscv_relax), MI.getLoc()));
  }

  // Emit a normal ADD instruction with the given operands.
  MCInst TmpInst = MCInstBuilder(RISCV::ADD)
                       .addOperand(DestReg)
                       .addOperand(SrcReg)
                       .addOperand(TPReg);
  uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);
}

static unsigned getInvertedBranchOp(unsigned BrOp) {
  switch (BrOp) {
  default:
    llvm_unreachable("Unexpected branch opcode!");
  case RISCV::PseudoLongBEQ:
    return RISCV::BNE;
  case RISCV::PseudoLongBNE:
    return RISCV::BEQ;
  case RISCV::PseudoLongBLT:
    return RISCV::BGE;
  case RISCV::PseudoLongBGE:
    return RISCV::BLT;
  case RISCV::PseudoLongBLTU:
    return RISCV::BGEU;
  case RISCV::PseudoLongBGEU:
    return RISCV::BLTU;
  }
}

// Expand PseudoLongBxx to an inverted conditional branch and an unconditional
// jump.
void RISCVMCCodeEmitter::expandLongCondBr(const MCInst &MI,
                                          SmallVectorImpl<char> &CB,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  MCRegister SrcReg1 = MI.getOperand(0).getReg();
  MCRegister SrcReg2 = MI.getOperand(1).getReg();
  MCOperand SrcSymbol = MI.getOperand(2);
  unsigned Opcode = MI.getOpcode();
  bool IsEqTest =
      Opcode == RISCV::PseudoLongBNE || Opcode == RISCV::PseudoLongBEQ;

  bool UseCompressedBr = false;
  if (IsEqTest && (STI.hasFeature(RISCV::FeatureStdExtC) ||
                   STI.hasFeature(RISCV::FeatureStdExtZca))) {
    if (RISCV::X8 <= SrcReg1.id() && SrcReg1.id() <= RISCV::X15 &&
        SrcReg2.id() == RISCV::X0) {
      UseCompressedBr = true;
    } else if (RISCV::X8 <= SrcReg2.id() && SrcReg2.id() <= RISCV::X15 &&
               SrcReg1.id() == RISCV::X0) {
      std::swap(SrcReg1, SrcReg2);
      UseCompressedBr = true;
    }
  }

  uint32_t Offset;
  if (UseCompressedBr) {
    unsigned InvOpc =
        Opcode == RISCV::PseudoLongBNE ? RISCV::C_BEQZ : RISCV::C_BNEZ;
    MCInst TmpInst = MCInstBuilder(InvOpc).addReg(SrcReg1).addImm(6);
    uint16_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write<uint16_t>(CB, Binary, llvm::endianness::little);
    Offset = 2;
  } else {
    unsigned InvOpc = getInvertedBranchOp(Opcode);
    MCInst TmpInst =
        MCInstBuilder(InvOpc).addReg(SrcReg1).addReg(SrcReg2).addImm(8);
    uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write(CB, Binary, llvm::endianness::little);
    Offset = 4;
  }

  // Save the number fixups.
  size_t FixupStartIndex = Fixups.size();

  // Emit an unconditional jump to the destination.
  MCInst TmpInst =
      MCInstBuilder(RISCV::JAL).addReg(RISCV::X0).addOperand(SrcSymbol);
  uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);

  // Drop any fixup added so we can add the correct one.
  Fixups.resize(FixupStartIndex);

  if (SrcSymbol.isExpr()) {
    Fixups.push_back(MCFixup::create(Offset, SrcSymbol.getExpr(),
                                     MCFixupKind(RISCV::fixup_riscv_jal),
                                     MI.getLoc()));
  }
}

void RISCVMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  // Get byte count of instruction.
  unsigned Size = Desc.getSize();

  // RISCVInstrInfo::getInstSizeInBytes expects that the total size of the
  // expanded instructions for each pseudo is correct in the Size field of the
  // tablegen definition for the pseudo.
  switch (MI.getOpcode()) {
  default:
    break;
  case RISCV::PseudoCALLReg:
  case RISCV::PseudoCALL:
  case RISCV::PseudoTAIL:
  case RISCV::PseudoJump:
    expandFunctionCall(MI, CB, Fixups, STI);
    MCNumEmitted += 2;
    return;
  case RISCV::PseudoAddTPRel:
    expandAddTPRel(MI, CB, Fixups, STI);
    MCNumEmitted += 1;
    return;
  case RISCV::PseudoLongBEQ:
  case RISCV::PseudoLongBNE:
  case RISCV::PseudoLongBLT:
  case RISCV::PseudoLongBGE:
  case RISCV::PseudoLongBLTU:
  case RISCV::PseudoLongBGEU:
    expandLongCondBr(MI, CB, Fixups, STI);
    MCNumEmitted += 2;
    return;
  case RISCV::PseudoTLSDESCCall:
    expandTLSDESCCall(MI, CB, Fixups, STI);
    MCNumEmitted += 1;
    return;
  }

  switch (Size) {
  default:
    llvm_unreachable("Unhandled encodeInstruction length!");
  case 2: {
    uint16_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write<uint16_t>(CB, Bits, llvm::endianness::little);
    break;
  }
  case 4: {
    uint32_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write(CB, Bits, llvm::endianness::little);
    break;
  }
  }

  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}

unsigned
RISCVMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {

  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());

  llvm_unreachable("Unhandled expression!");
  return 0;
}

unsigned
RISCVMCCodeEmitter::getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) {
    unsigned Res = MO.getImm();
    assert((Res & 1) == 0 && "LSB is non-zero");
    return Res >> 1;
  }

  return getImmOpValue(MI, OpNo, Fixups, STI);
}

unsigned RISCVMCCodeEmitter::getImmOpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  bool EnableRelax = STI.hasFeature(RISCV::FeatureRelax);
  const MCOperand &MO = MI.getOperand(OpNo);

  MCInstrDesc const &Desc = MCII.get(MI.getOpcode());
  unsigned MIFrm = RISCVII::getFormat(Desc.TSFlags);

  // If the destination is an immediate, there is nothing to do.
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() &&
         "getImmOpValue expects only expressions or immediates");
  const MCExpr *Expr = MO.getExpr();
  MCExpr::ExprKind Kind = Expr->getKind();
  RISCV::Fixups FixupKind = RISCV::fixup_riscv_invalid;
  bool RelaxCandidate = false;
  if (Kind == MCExpr::Target) {
    const RISCVMCExpr *RVExpr = cast<RISCVMCExpr>(Expr);

    switch (RVExpr->getKind()) {
    case RISCVMCExpr::VK_RISCV_None:
    case RISCVMCExpr::VK_RISCV_Invalid:
    case RISCVMCExpr::VK_RISCV_32_PCREL:
      llvm_unreachable("Unhandled fixup kind!");
    case RISCVMCExpr::VK_RISCV_TPREL_ADD:
      // tprel_add is only used to indicate that a relocation should be emitted
      // for an add instruction used in TP-relative addressing. It should not be
      // expanded as if representing an actual instruction operand and so to
      // encounter it here is an error.
      llvm_unreachable(
          "VK_RISCV_TPREL_ADD should not represent an instruction operand");
    case RISCVMCExpr::VK_RISCV_LO:
      if (MIFrm == RISCVII::InstFormatI)
        FixupKind = RISCV::fixup_riscv_lo12_i;
      else if (MIFrm == RISCVII::InstFormatS)
        FixupKind = RISCV::fixup_riscv_lo12_s;
      else
        llvm_unreachable("VK_RISCV_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_HI:
      FixupKind = RISCV::fixup_riscv_hi20;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_PCREL_LO:
      if (MIFrm == RISCVII::InstFormatI)
        FixupKind = RISCV::fixup_riscv_pcrel_lo12_i;
      else if (MIFrm == RISCVII::InstFormatS)
        FixupKind = RISCV::fixup_riscv_pcrel_lo12_s;
      else
        llvm_unreachable(
            "VK_RISCV_PCREL_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_PCREL_HI:
      FixupKind = RISCV::fixup_riscv_pcrel_hi20;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_GOT_HI:
      FixupKind = RISCV::fixup_riscv_got_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_TPREL_LO:
      if (MIFrm == RISCVII::InstFormatI)
        FixupKind = RISCV::fixup_riscv_tprel_lo12_i;
      else if (MIFrm == RISCVII::InstFormatS)
        FixupKind = RISCV::fixup_riscv_tprel_lo12_s;
      else
        llvm_unreachable(
            "VK_RISCV_TPREL_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_TPREL_HI:
      FixupKind = RISCV::fixup_riscv_tprel_hi20;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_TLS_GOT_HI:
      FixupKind = RISCV::fixup_riscv_tls_got_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_TLS_GD_HI:
      FixupKind = RISCV::fixup_riscv_tls_gd_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_CALL:
      FixupKind = RISCV::fixup_riscv_call;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_CALL_PLT:
      FixupKind = RISCV::fixup_riscv_call_plt;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_TLSDESC_HI:
      FixupKind = RISCV::fixup_riscv_tlsdesc_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_TLSDESC_LOAD_LO:
      FixupKind = RISCV::fixup_riscv_tlsdesc_load_lo12;
      break;
    case RISCVMCExpr::VK_RISCV_TLSDESC_ADD_LO:
      FixupKind = RISCV::fixup_riscv_tlsdesc_add_lo12;
      break;
    case RISCVMCExpr::VK_RISCV_TLSDESC_CALL:
      FixupKind = RISCV::fixup_riscv_tlsdesc_call;
      break;
    }
  } else if ((Kind == MCExpr::SymbolRef &&
                 cast<MCSymbolRefExpr>(Expr)->getKind() ==
                     MCSymbolRefExpr::VK_None) ||
             Kind == MCExpr::Binary) {
    // FIXME: Sub kind binary exprs have chance of underflow.
    if (MIFrm == RISCVII::InstFormatJ) {
      FixupKind = RISCV::fixup_riscv_jal;
    } else if (MIFrm == RISCVII::InstFormatB) {
      FixupKind = RISCV::fixup_riscv_branch;
    } else if (MIFrm == RISCVII::InstFormatCJ) {
      FixupKind = RISCV::fixup_riscv_rvc_jump;
    } else if (MIFrm == RISCVII::InstFormatCB) {
      FixupKind = RISCV::fixup_riscv_rvc_branch;
    } else if (MIFrm == RISCVII::InstFormatI) {
      FixupKind = RISCV::fixup_riscv_12_i;
    }
  }

  assert(FixupKind != RISCV::fixup_riscv_invalid && "Unhandled expression!");

  Fixups.push_back(
      MCFixup::create(0, Expr, MCFixupKind(FixupKind), MI.getLoc()));
  ++MCNumFixups;

  // Ensure an R_RISCV_RELAX relocation will be emitted if linker relaxation is
  // enabled and the current fixup will result in a relocation that may be
  // relaxed.
  if (EnableRelax && RelaxCandidate) {
    const MCConstantExpr *Dummy = MCConstantExpr::create(0, Ctx);
    Fixups.push_back(
    MCFixup::create(0, Dummy, MCFixupKind(RISCV::fixup_riscv_relax),
                    MI.getLoc()));
    ++MCNumFixups;
  }

  return 0;
}

unsigned RISCVMCCodeEmitter::getVMaskReg(const MCInst &MI, unsigned OpNo,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  MCOperand MO = MI.getOperand(OpNo);
  assert(MO.isReg() && "Expected a register.");

  switch (MO.getReg()) {
  default:
    llvm_unreachable("Invalid mask register.");
  case RISCV::V0:
    return 0;
  case RISCV::NoRegister:
    return 1;
  }
}

unsigned RISCVMCCodeEmitter::getRlistOpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm() && "Rlist operand must be immediate");
  auto Imm = MO.getImm();
  assert(Imm >= 4 && "EABI is currently not implemented");
  return Imm;
}

unsigned RISCVMCCodeEmitter::getRegReg(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  const MCOperand &MO1 = MI.getOperand(OpNo + 1);
  assert(MO.isReg() && MO1.isReg() && "Expected registers.");

  unsigned Op = Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
  unsigned Op1 = Ctx.getRegisterInfo()->getEncodingValue(MO1.getReg());

  return Op | Op1 << 5;
}

#include "RISCVGenMCCodeEmitter.inc"
