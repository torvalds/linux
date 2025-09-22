//===-- VEELFObjectWriter.cpp - VE ELF Writer -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VEFixupKinds.h"
#include "VEMCExpr.h"
#include "VEMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class VEELFObjectWriter : public MCELFObjectTargetWriter {
public:
  VEELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/* Is64Bit */ true, OSABI, ELF::EM_VE,
                                /* HasRelocationAddend */ true) {}

  ~VEELFObjectWriter() override = default;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;

  bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                               unsigned Type) const override;
};
} // namespace

unsigned VEELFObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                         const MCFixup &Fixup,
                                         bool IsPCRel) const {
  if (const VEMCExpr *SExpr = dyn_cast<VEMCExpr>(Fixup.getValue())) {
    if (SExpr->getKind() == VEMCExpr::VK_VE_PC_LO32)
      return ELF::R_VE_PC_LO32;
  }

  if (IsPCRel) {
    switch (Fixup.getTargetKind()) {
    default:
      Ctx.reportError(Fixup.getLoc(), "Unsupported pc-relative fixup kind");
      return ELF::R_VE_NONE;
    case FK_Data_1:
    case FK_PCRel_1:
      Ctx.reportError(Fixup.getLoc(),
                      "1-byte pc-relative data relocation is not supported");
      return ELF::R_VE_NONE;
    case FK_Data_2:
    case FK_PCRel_2:
      Ctx.reportError(Fixup.getLoc(),
                      "2-byte pc-relative data relocation is not supported");
      return ELF::R_VE_NONE;
    case FK_Data_4:
    case FK_PCRel_4:
      return ELF::R_VE_SREL32;
    case FK_Data_8:
    case FK_PCRel_8:
      Ctx.reportError(Fixup.getLoc(),
                      "8-byte pc-relative data relocation is not supported");
      return ELF::R_VE_NONE;
    case VE::fixup_ve_reflong:
    case VE::fixup_ve_srel32:
      return ELF::R_VE_SREL32;
    case VE::fixup_ve_pc_hi32:
      return ELF::R_VE_PC_HI32;
    case VE::fixup_ve_pc_lo32:
      return ELF::R_VE_PC_LO32;
    }
  }

  switch (Fixup.getTargetKind()) {
  default:
    Ctx.reportError(Fixup.getLoc(), "Unknown ELF relocation type");
    return ELF::R_VE_NONE;
  case FK_Data_1:
    Ctx.reportError(Fixup.getLoc(), "1-byte data relocation is not supported");
    return ELF::R_VE_NONE;
  case FK_Data_2:
    Ctx.reportError(Fixup.getLoc(), "2-byte data relocation is not supported");
    return ELF::R_VE_NONE;
  case FK_Data_4:
    return ELF::R_VE_REFLONG;
  case FK_Data_8:
    return ELF::R_VE_REFQUAD;
  case VE::fixup_ve_reflong:
    return ELF::R_VE_REFLONG;
  case VE::fixup_ve_srel32:
    Ctx.reportError(Fixup.getLoc(),
                    "A non pc-relative srel32 relocation is not supported");
    return ELF::R_VE_NONE;
  case VE::fixup_ve_hi32:
    return ELF::R_VE_HI32;
  case VE::fixup_ve_lo32:
    return ELF::R_VE_LO32;
  case VE::fixup_ve_pc_hi32:
    Ctx.reportError(Fixup.getLoc(),
                    "A non pc-relative pc_hi32 relocation is not supported");
    return ELF::R_VE_NONE;
  case VE::fixup_ve_pc_lo32:
    Ctx.reportError(Fixup.getLoc(),
                    "A non pc-relative pc_lo32 relocation is not supported");
    return ELF::R_VE_NONE;
  case VE::fixup_ve_got_hi32:
    return ELF::R_VE_GOT_HI32;
  case VE::fixup_ve_got_lo32:
    return ELF::R_VE_GOT_LO32;
  case VE::fixup_ve_gotoff_hi32:
    return ELF::R_VE_GOTOFF_HI32;
  case VE::fixup_ve_gotoff_lo32:
    return ELF::R_VE_GOTOFF_LO32;
  case VE::fixup_ve_plt_hi32:
    return ELF::R_VE_PLT_HI32;
  case VE::fixup_ve_plt_lo32:
    return ELF::R_VE_PLT_LO32;
  case VE::fixup_ve_tls_gd_hi32:
    return ELF::R_VE_TLS_GD_HI32;
  case VE::fixup_ve_tls_gd_lo32:
    return ELF::R_VE_TLS_GD_LO32;
  case VE::fixup_ve_tpoff_hi32:
    return ELF::R_VE_TPOFF_HI32;
  case VE::fixup_ve_tpoff_lo32:
    return ELF::R_VE_TPOFF_LO32;
  }

  return ELF::R_VE_NONE;
}

bool VEELFObjectWriter::needsRelocateWithSymbol(const MCValue &,
                                                const MCSymbol &,
                                                unsigned Type) const {
  switch (Type) {
  default:
    return false;

  // All relocations that use a GOT need a symbol, not an offset, as
  // the offset of the symbol within the section is irrelevant to
  // where the GOT entry is. Don't need to list all the TLS entries,
  // as they're all marked as requiring a symbol anyways.
  case ELF::R_VE_GOT_HI32:
  case ELF::R_VE_GOT_LO32:
  case ELF::R_VE_GOTOFF_HI32:
  case ELF::R_VE_GOTOFF_LO32:
  case ELF::R_VE_TLS_GD_HI32:
  case ELF::R_VE_TLS_GD_LO32:
    return true;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createVEELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<VEELFObjectWriter>(OSABI);
}
