//===-- PPCMCCodeEmitter.cpp - Convert PPC code to machine code -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the PPCMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "PPCMCCodeEmitter.h"
#include "MCTargetDesc/PPCFixupKinds.h"
#include "PPCMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");

MCCodeEmitter *llvm::createPPCMCCodeEmitter(const MCInstrInfo &MCII,
                                            MCContext &Ctx) {
  return new PPCMCCodeEmitter(MCII, Ctx);
}

unsigned PPCMCCodeEmitter::
getDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isReg() || MO.isImm())
    return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (isNoTOCCallInstr(MI)
                                        ? (MCFixupKind)PPC::fixup_ppc_br24_notoc
                                        : (MCFixupKind)PPC::fixup_ppc_br24)));
  return 0;
}

/// Check if Opcode corresponds to a call instruction that should be marked
/// with the NOTOC relocation.
bool PPCMCCodeEmitter::isNoTOCCallInstr(const MCInst &MI) const {
  unsigned Opcode = MI.getOpcode();
  if (!MCII.get(Opcode).isCall())
    return false;

  switch (Opcode) {
  default:
#ifndef NDEBUG
    llvm_unreachable("Unknown call opcode");
#endif
    return false;
  case PPC::BL8_NOTOC:
  case PPC::BL8_NOTOC_TLS:
  case PPC::BL8_NOTOC_RM:
    return true;
#ifndef NDEBUG
  case PPC::BL8:
  case PPC::BL:
  case PPC::BL8_TLS:
  case PPC::BL_TLS:
  case PPC::BLA8:
  case PPC::BLA:
  case PPC::BCCL:
  case PPC::BCCLA:
  case PPC::BCL:
  case PPC::BCLn:
  case PPC::BL8_NOP:
  case PPC::BL_NOP:
  case PPC::BL8_NOP_TLS:
  case PPC::BLA8_NOP:
  case PPC::BCTRL8:
  case PPC::BCTRL:
  case PPC::BCCCTRL8:
  case PPC::BCCCTRL:
  case PPC::BCCTRL8:
  case PPC::BCCTRL:
  case PPC::BCCTRL8n:
  case PPC::BCCTRLn:
  case PPC::BL8_RM:
  case PPC::BLA8_RM:
  case PPC::BL8_NOP_RM:
  case PPC::BLA8_NOP_RM:
  case PPC::BCTRL8_RM:
  case PPC::BCTRL8_LDinto_toc:
  case PPC::BCTRL8_LDinto_toc_RM:
  case PPC::BL8_TLS_:
  case PPC::TCRETURNdi8:
  case PPC::TCRETURNai8:
  case PPC::TCRETURNri8:
  case PPC::TAILBCTR8:
  case PPC::TAILB8:
  case PPC::TAILBA8:
  case PPC::BCLalways:
  case PPC::BLRL:
  case PPC::BCCLRL:
  case PPC::BCLRL:
  case PPC::BCLRLn:
  case PPC::BDZL:
  case PPC::BDNZL:
  case PPC::BDZLA:
  case PPC::BDNZLA:
  case PPC::BDZLp:
  case PPC::BDNZLp:
  case PPC::BDZLAp:
  case PPC::BDNZLAp:
  case PPC::BDZLm:
  case PPC::BDNZLm:
  case PPC::BDZLAm:
  case PPC::BDNZLAm:
  case PPC::BDZLRL:
  case PPC::BDNZLRL:
  case PPC::BDZLRLp:
  case PPC::BDNZLRLp:
  case PPC::BDZLRLm:
  case PPC::BDNZLRLm:
  case PPC::BL_RM:
  case PPC::BLA_RM:
  case PPC::BL_NOP_RM:
  case PPC::BCTRL_RM:
  case PPC::TCRETURNdi:
  case PPC::TCRETURNai:
  case PPC::TCRETURNri:
  case PPC::BCTRL_LWZinto_toc:
  case PPC::BCTRL_LWZinto_toc_RM:
  case PPC::TAILBCTR:
  case PPC::TAILB:
  case PPC::TAILBA:
    return false;
#endif
  }
}

unsigned PPCMCCodeEmitter::getCondBrEncoding(const MCInst &MI, unsigned OpNo,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_brcond14));
  return 0;
}

unsigned PPCMCCodeEmitter::
getAbsDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_br24abs));
  return 0;
}

unsigned PPCMCCodeEmitter::
getAbsCondBrEncoding(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_brcond14abs));
  return 0;
}

unsigned
PPCMCCodeEmitter::getVSRpEvenEncoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  assert(MI.getOperand(OpNo).isReg() && "Operand should be a register");
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI)
                     << 1;
  return RegBits;
}

unsigned PPCMCCodeEmitter::getImm16Encoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the immediate field.
  Fixups.push_back(MCFixup::create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16));
  return 0;
}

uint64_t PPCMCCodeEmitter::getImm34Encoding(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI,
                                            MCFixupKind Fixup) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(!MO.isReg() && "Not expecting a register for this operand.");
  if (MO.isImm())
    return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the immediate field.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(), Fixup));
  return 0;
}

uint64_t
PPCMCCodeEmitter::getImm34EncodingNoPCRel(const MCInst &MI, unsigned OpNo,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  return getImm34Encoding(MI, OpNo, Fixups, STI,
                          (MCFixupKind)PPC::fixup_ppc_imm34);
}

uint64_t
PPCMCCodeEmitter::getImm34EncodingPCRel(const MCInst &MI, unsigned OpNo,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  return getImm34Encoding(MI, OpNo, Fixups, STI,
                          (MCFixupKind)PPC::fixup_ppc_pcrel34);
}

unsigned PPCMCCodeEmitter::getDispRIEncoding(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return getMachineOpValue(MI, MO, Fixups, STI) & 0xFFFF;

  // Add a fixup for the displacement field.
  Fixups.push_back(MCFixup::create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16));
  return 0;
}

unsigned
PPCMCCodeEmitter::getDispRIXEncoding(const MCInst &MI, unsigned OpNo,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return ((getMachineOpValue(MI, MO, Fixups, STI) >> 2) & 0x3FFF);

  // Add a fixup for the displacement field.
  Fixups.push_back(MCFixup::create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16ds));
  return 0;
}

unsigned
PPCMCCodeEmitter::getDispRIX16Encoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    assert(!(MO.getImm() % 16) &&
           "Expecting an immediate that is a multiple of 16");
    return ((getMachineOpValue(MI, MO, Fixups, STI) >> 4) & 0xFFF);
  }

  // Otherwise add a fixup for the displacement field.
  Fixups.push_back(MCFixup::create(IsLittleEndian ? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16dq));
  return 0;
}

unsigned
PPCMCCodeEmitter::getDispRIHashEncoding(const MCInst &MI, unsigned OpNo,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  // Encode imm for the hash load/store to stack for the ROP Protection
  // instructions.
  const MCOperand &MO = MI.getOperand(OpNo);

  assert(MO.isImm() && "Expecting an immediate operand.");
  assert(!(MO.getImm() % 8) && "Expecting offset to be 8 byte aligned.");

  unsigned DX = (MO.getImm() >> 3) & 0x3F;
  return DX;
}

uint64_t
PPCMCCodeEmitter::getDispRI34PCRelEncoding(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  // Encode the displacement part of pc-relative memri34, which is an imm34.
  // The 34 bit immediate can fall into one of three cases:
  // 1) It is a relocation to be filled in by the linker represented as:
  //    (MCExpr::SymbolRef)
  // 2) It is a relocation + SignedOffset represented as:
  //    (MCExpr::Binary(MCExpr::SymbolRef + MCExpr::Constant))
  // 3) It is a known value at compile time.

  // If this is not a MCExpr then we are in case 3) and we are dealing with
  // a value known at compile time, not a relocation.
  const MCOperand &MO = MI.getOperand(OpNo);
  if (!MO.isExpr())
    return (getMachineOpValue(MI, MO, Fixups, STI)) & 0x3FFFFFFFFUL;

  // At this point in the function it is known that MO is of type MCExpr.
  // Therefore we are dealing with either case 1) a symbol ref or
  // case 2) a symbol ref plus a constant.
  const MCExpr *Expr = MO.getExpr();
  switch (Expr->getKind()) {
  default:
    llvm_unreachable("Unsupported MCExpr for getMemRI34PCRelEncoding.");
  case MCExpr::SymbolRef: {
    // Relocation alone.
    const MCSymbolRefExpr *SRE = cast<MCSymbolRefExpr>(Expr);
    (void)SRE;
    // Currently these are the only valid PCRelative Relocations.
    assert((SRE->getKind() == MCSymbolRefExpr::VK_PCREL ||
            SRE->getKind() == MCSymbolRefExpr::VK_PPC_GOT_PCREL ||
            SRE->getKind() == MCSymbolRefExpr::VK_PPC_GOT_TLSGD_PCREL ||
            SRE->getKind() == MCSymbolRefExpr::VK_PPC_GOT_TLSLD_PCREL ||
            SRE->getKind() == MCSymbolRefExpr::VK_PPC_GOT_TPREL_PCREL) &&
           "VariantKind must be VK_PCREL or VK_PPC_GOT_PCREL or "
           "VK_PPC_GOT_TLSGD_PCREL or VK_PPC_GOT_TLSLD_PCREL or "
           "VK_PPC_GOT_TPREL_PCREL.");
    // Generate the fixup for the relocation.
    Fixups.push_back(
        MCFixup::create(0, Expr,
                        static_cast<MCFixupKind>(PPC::fixup_ppc_pcrel34)));
    // Put zero in the location of the immediate. The linker will fill in the
    // correct value based on the relocation.
    return 0;
  }
  case MCExpr::Binary: {
    // Relocation plus some offset.
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(Expr);
    assert(BE->getOpcode() == MCBinaryExpr::Add &&
           "Binary expression opcode must be an add.");

    const MCExpr *LHS = BE->getLHS();
    const MCExpr *RHS = BE->getRHS();

    // Need to check in both directions. Reloc+Offset and Offset+Reloc.
    if (LHS->getKind() != MCExpr::SymbolRef)
      std::swap(LHS, RHS);

    if (LHS->getKind() != MCExpr::SymbolRef ||
        RHS->getKind() != MCExpr::Constant)
      llvm_unreachable("Expecting to have one constant and one relocation.");

    const MCSymbolRefExpr *SRE = cast<MCSymbolRefExpr>(LHS);
    (void)SRE;
    assert(isInt<34>(cast<MCConstantExpr>(RHS)->getValue()) &&
           "Value must fit in 34 bits.");

    // Currently these are the only valid PCRelative Relocations.
    assert((SRE->getKind() == MCSymbolRefExpr::VK_PCREL ||
            SRE->getKind() == MCSymbolRefExpr::VK_PPC_GOT_PCREL) &&
           "VariantKind must be VK_PCREL or VK_PPC_GOT_PCREL");
    // Generate the fixup for the relocation.
    Fixups.push_back(
        MCFixup::create(0, Expr,
                        static_cast<MCFixupKind>(PPC::fixup_ppc_pcrel34)));
    // Put zero in the location of the immediate. The linker will fill in the
    // correct value based on the relocation.
    return 0;
    }
  }
}

uint64_t
PPCMCCodeEmitter::getDispRI34Encoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  // Encode the displacement part of a memri34.
  const MCOperand &MO = MI.getOperand(OpNo);
  return (getMachineOpValue(MI, MO, Fixups, STI)) & 0x3FFFFFFFFUL;
}

unsigned
PPCMCCodeEmitter::getDispSPE8Encoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  // Encode imm as a dispSPE8, which has the low 5-bits of (imm / 8).
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  return getMachineOpValue(MI, MO, Fixups, STI) >> 3;
}

unsigned
PPCMCCodeEmitter::getDispSPE4Encoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  // Encode imm as a dispSPE8, which has the low 5-bits of (imm / 4).
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  return getMachineOpValue(MI, MO, Fixups, STI) >> 2;
}

unsigned
PPCMCCodeEmitter::getDispSPE2Encoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  // Encode imm as a dispSPE8, which has the low 5-bits of (imm / 2).
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  return getMachineOpValue(MI, MO, Fixups, STI) >> 1;
}

unsigned PPCMCCodeEmitter::getTLSRegEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the TLS register, which simply provides a relocation
  // hint to the linker that this statement is part of a relocation sequence.
  // Return the thread-pointer register's encoding. Add a one byte displacement
  // if using PC relative memops.
  const MCExpr *Expr = MO.getExpr();
  const MCSymbolRefExpr *SRE = cast<MCSymbolRefExpr>(Expr);
  bool IsPCRel = SRE->getKind() == MCSymbolRefExpr::VK_PPC_TLS_PCREL;
  Fixups.push_back(MCFixup::create(IsPCRel ? 1 : 0, Expr,
                                   (MCFixupKind)PPC::fixup_ppc_nofixup));
  const Triple &TT = STI.getTargetTriple();
  bool isPPC64 = TT.isPPC64();
  return CTX.getRegisterInfo()->getEncodingValue(isPPC64 ? PPC::X13 : PPC::R2);
}

unsigned PPCMCCodeEmitter::getTLSCallEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  // For special TLS calls, we need two fixups; one for the branch target
  // (__tls_get_addr), which we create via getDirectBrEncoding as usual,
  // and one for the TLSGD or TLSLD symbol, which is emitted here.
  const MCOperand &MO = MI.getOperand(OpNo+1);
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_nofixup));
  return getDirectBrEncoding(MI, OpNo, Fixups, STI);
}

unsigned PPCMCCodeEmitter::
get_crbitm_encoding(const MCInst &MI, unsigned OpNo,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert((MI.getOpcode() == PPC::MTOCRF || MI.getOpcode() == PPC::MTOCRF8 ||
          MI.getOpcode() == PPC::MFOCRF || MI.getOpcode() == PPC::MFOCRF8) &&
         (MO.getReg() >= PPC::CR0 && MO.getReg() <= PPC::CR7));
  return 0x80 >> CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
}

// Get the index for this operand in this instruction. This is needed for
// computing the register number in PPC::getRegNumForOperand() for
// any instructions that use a different numbering scheme for registers in
// different operands.
static unsigned getOpIdxForMO(const MCInst &MI, const MCOperand &MO) {
  for (unsigned i = 0; i < MI.getNumOperands(); i++) {
    const MCOperand &Op = MI.getOperand(i);
    if (&Op == &MO)
      return i;
  }
  llvm_unreachable("This operand is not part of this instruction");
  return ~0U; // Silence any warnings about no return.
}

uint64_t PPCMCCodeEmitter::
getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    // MTOCRF/MFOCRF should go through get_crbitm_encoding for the CR operand.
    // The GPR operand should come through here though.
    assert((MI.getOpcode() != PPC::MTOCRF && MI.getOpcode() != PPC::MTOCRF8 &&
            MI.getOpcode() != PPC::MFOCRF && MI.getOpcode() != PPC::MFOCRF8) ||
           MO.getReg() < PPC::CR0 || MO.getReg() > PPC::CR7);
    unsigned OpNo = getOpIdxForMO(MI, MO);
    unsigned Reg =
        PPC::getRegNumForOperand(MCII.get(MI.getOpcode()), MO.getReg(), OpNo);
    return CTX.getRegisterInfo()->getEncodingValue(Reg);
  }

  assert(MO.isImm() &&
         "Relocation required in an instruction that we cannot encode!");
  return MO.getImm();
}

void PPCMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                         SmallVectorImpl<char> &CB,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);

  // Output the constant in big/little endian byte order.
  unsigned Size = getInstSizeInBytes(MI);
  llvm::endianness E =
      IsLittleEndian ? llvm::endianness::little : llvm::endianness::big;
  switch (Size) {
  case 0:
    break;
  case 4:
    support::endian::write<uint32_t>(CB, Bits, E);
    break;
  case 8:
    // If we emit a pair of instructions, the first one is
    // always in the top 32 bits, even on little-endian.
    support::endian::write<uint32_t>(CB, Bits >> 32, E);
    support::endian::write<uint32_t>(CB, Bits, E);
    break;
  default:
    llvm_unreachable("Invalid instruction size");
  }

  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}

// Get the number of bytes used to encode the given MCInst.
unsigned PPCMCCodeEmitter::getInstSizeInBytes(const MCInst &MI) const {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MCII.get(Opcode);
  return Desc.getSize();
}

bool PPCMCCodeEmitter::isPrefixedInstruction(const MCInst &MI) const {
  return MCII.get(MI.getOpcode()).TSFlags & PPCII::Prefixed;
}

#include "PPCGenMCCodeEmitter.inc"
