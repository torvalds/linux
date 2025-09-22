//===-- CSKYELFObjectWriter.cpp - CSKY ELF Writer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CSKYFixupKinds.h"
#include "CSKYMCExpr.h"
#include "CSKYMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"

#define DEBUG_TYPE "csky-elf-object-writer"

using namespace llvm;

namespace {

class CSKYELFObjectWriter : public MCELFObjectTargetWriter {
public:
  CSKYELFObjectWriter(uint8_t OSABI = 0)
      : MCELFObjectTargetWriter(false, OSABI, ELF::EM_CSKY, true){};
  ~CSKYELFObjectWriter() {}

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};

} // namespace

unsigned CSKYELFObjectWriter::getRelocType(MCContext &Ctx,
                                           const MCValue &Target,
                                           const MCFixup &Fixup,
                                           bool IsPCRel) const {
  const MCExpr *Expr = Fixup.getValue();
  // Determine the type of the relocation
  unsigned Kind = Fixup.getTargetKind();
  MCSymbolRefExpr::VariantKind Modifier = Target.getAccessVariant();

  if (IsPCRel) {
    switch (Kind) {
    default:
      LLVM_DEBUG(dbgs() << "Unknown Kind1  = " << Kind);
      Ctx.reportError(Fixup.getLoc(), "Unsupported relocation type");
      return ELF::R_CKCORE_NONE;
    case FK_Data_4:
    case FK_PCRel_4:
      return ELF::R_CKCORE_PCREL32;
    case CSKY::fixup_csky_pcrel_uimm16_scale4:
      return ELF::R_CKCORE_PCREL_IMM16_4;
    case CSKY::fixup_csky_pcrel_uimm8_scale4:
      return ELF::R_CKCORE_PCREL_IMM8_4;
    case CSKY::fixup_csky_pcrel_imm26_scale2:
      return ELF::R_CKCORE_PCREL_IMM26_2;
    case CSKY::fixup_csky_pcrel_imm18_scale2:
      return ELF::R_CKCORE_PCREL_IMM18_2;
    case CSKY::fixup_csky_pcrel_imm16_scale2:
      return ELF::R_CKCORE_PCREL_IMM16_2;
    case CSKY::fixup_csky_pcrel_imm10_scale2:
      return ELF::R_CKCORE_PCREL_IMM10_2;
    case CSKY::fixup_csky_pcrel_uimm7_scale4:
      return ELF::R_CKCORE_PCREL_IMM7_4;
    }
  }

  switch (Kind) {
  default:
    LLVM_DEBUG(dbgs() << "Unknown Kind2  = " << Kind);
    Ctx.reportError(Fixup.getLoc(), "Unsupported relocation type");
    return ELF::R_CKCORE_NONE;
  case FK_Data_1:
    Ctx.reportError(Fixup.getLoc(), "1-byte data relocations not supported");
    return ELF::R_CKCORE_NONE;
  case FK_Data_2:
    Ctx.reportError(Fixup.getLoc(), "2-byte data relocations not supported");
    return ELF::R_CKCORE_NONE;
  case FK_Data_4:
    if (Expr->getKind() == MCExpr::Target) {
      auto TK = cast<CSKYMCExpr>(Expr)->getKind();
      if (TK == CSKYMCExpr::VK_CSKY_ADDR)
        return ELF::R_CKCORE_ADDR32;
      if (TK == CSKYMCExpr::VK_CSKY_GOT)
        return ELF::R_CKCORE_GOT32;
      if (TK == CSKYMCExpr::VK_CSKY_GOTOFF)
        return ELF::R_CKCORE_GOTOFF;
      if (TK == CSKYMCExpr::VK_CSKY_PLT)
        return ELF::R_CKCORE_PLT32;
      if (TK == CSKYMCExpr::VK_CSKY_TLSIE)
        return ELF::R_CKCORE_TLS_IE32;
      if (TK == CSKYMCExpr::VK_CSKY_TLSLE)
        return ELF::R_CKCORE_TLS_LE32;
      if (TK == CSKYMCExpr::VK_CSKY_TLSGD)
        return ELF::R_CKCORE_TLS_GD32;
      if (TK == CSKYMCExpr::VK_CSKY_TLSLDM)
        return ELF::R_CKCORE_TLS_LDM32;
      if (TK == CSKYMCExpr::VK_CSKY_TLSLDO)
        return ELF::R_CKCORE_TLS_LDO32;
      if (TK == CSKYMCExpr::VK_CSKY_GOTPC)
        return ELF::R_CKCORE_GOTPC;
      if (TK == CSKYMCExpr::VK_CSKY_None)
        return ELF::R_CKCORE_ADDR32;

      LLVM_DEBUG(dbgs() << "Unknown FK_Data_4 TK  = " << TK);
      Ctx.reportError(Fixup.getLoc(), "unknown target FK_Data_4");
    } else {
      switch (Modifier) {
      default:
        Ctx.reportError(Fixup.getLoc(),
                        "invalid fixup for 4-byte data relocation");
        return ELF::R_CKCORE_NONE;
      case MCSymbolRefExpr::VK_GOT:
        return ELF::R_CKCORE_GOT32;
      case MCSymbolRefExpr::VK_GOTOFF:
        return ELF::R_CKCORE_GOTOFF;
      case MCSymbolRefExpr::VK_PLT:
        return ELF::R_CKCORE_PLT32;
      case MCSymbolRefExpr::VK_TLSGD:
        return ELF::R_CKCORE_TLS_GD32;
      case MCSymbolRefExpr::VK_TLSLDM:
        return ELF::R_CKCORE_TLS_LDM32;
      case MCSymbolRefExpr::VK_TPOFF:
        return ELF::R_CKCORE_TLS_LE32;
      case MCSymbolRefExpr::VK_None:
        return ELF::R_CKCORE_ADDR32;
      }
    }
    return ELF::R_CKCORE_NONE;
  case FK_Data_8:
    Ctx.reportError(Fixup.getLoc(), "8-byte data relocations not supported");
    return ELF::R_CKCORE_NONE;
  case CSKY::fixup_csky_addr32:
    return ELF::R_CKCORE_ADDR32;
  case CSKY::fixup_csky_addr_hi16:
    return ELF::R_CKCORE_ADDR_HI16;
  case CSKY::fixup_csky_addr_lo16:
    return ELF::R_CKCORE_ADDR_LO16;
  case CSKY::fixup_csky_doffset_imm18:
    return ELF::R_CKCORE_DOFFSET_IMM18;
  case CSKY::fixup_csky_doffset_imm18_scale2:
    return ELF::R_CKCORE_DOFFSET_IMM18_2;
  case CSKY::fixup_csky_doffset_imm18_scale4:
    return ELF::R_CKCORE_DOFFSET_IMM18_4;
  case CSKY::fixup_csky_got_imm18_scale4:
    return ELF::R_CKCORE_GOT_IMM18_4;
  case CSKY::fixup_csky_plt_imm18_scale4:
    return ELF::R_CKCORE_PLT_IMM18_4;
  }
}

std::unique_ptr<MCObjectTargetWriter> llvm::createCSKYELFObjectWriter() {
  return std::make_unique<CSKYELFObjectWriter>();
}
