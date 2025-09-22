//===-- AVRELFObjectWriter.cpp - AVR ELF Writer ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/AVRFixupKinds.h"
#include "MCTargetDesc/AVRMCExpr.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {

/// Writes AVR machine code into an ELF32 object file.
class AVRELFObjectWriter : public MCELFObjectTargetWriter {
public:
  AVRELFObjectWriter(uint8_t OSABI);

  virtual ~AVRELFObjectWriter() = default;

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};

AVRELFObjectWriter::AVRELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(false, OSABI, ELF::EM_AVR, true) {}

unsigned AVRELFObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                          const MCFixup &Fixup,
                                          bool IsPCRel) const {
  const unsigned Kind = Fixup.getTargetKind();
  if (Kind >= FirstLiteralRelocationKind)
    return Kind - FirstLiteralRelocationKind;
  MCSymbolRefExpr::VariantKind Modifier = Target.getAccessVariant();
  switch ((unsigned)Fixup.getKind()) {
  case FK_Data_1:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_AVR_8;
    case MCSymbolRefExpr::VK_AVR_DIFF8:
      return ELF::R_AVR_DIFF8;
    case MCSymbolRefExpr::VK_AVR_LO8:
      return ELF::R_AVR_8_LO8;
    case MCSymbolRefExpr::VK_AVR_HI8:
      return ELF::R_AVR_8_HI8;
    case MCSymbolRefExpr::VK_AVR_HLO8:
      return ELF::R_AVR_8_HLO8;
    }
  case FK_Data_4:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_AVR_32;
    case MCSymbolRefExpr::VK_AVR_DIFF32:
      return ELF::R_AVR_DIFF32;
    }
  case FK_Data_2:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_AVR_16;
    case MCSymbolRefExpr::VK_AVR_NONE:
    case MCSymbolRefExpr::VK_AVR_PM:
      return ELF::R_AVR_16_PM;
    case MCSymbolRefExpr::VK_AVR_DIFF16:
      return ELF::R_AVR_DIFF16;
    }
  case AVR::fixup_32:
    return ELF::R_AVR_32;
  case AVR::fixup_7_pcrel:
    return ELF::R_AVR_7_PCREL;
  case AVR::fixup_13_pcrel:
    return ELF::R_AVR_13_PCREL;
  case AVR::fixup_16:
    return ELF::R_AVR_16;
  case AVR::fixup_16_pm:
    return ELF::R_AVR_16_PM;
  case AVR::fixup_lo8_ldi:
    return ELF::R_AVR_LO8_LDI;
  case AVR::fixup_hi8_ldi:
    return ELF::R_AVR_HI8_LDI;
  case AVR::fixup_hh8_ldi:
    return ELF::R_AVR_HH8_LDI;
  case AVR::fixup_lo8_ldi_neg:
    return ELF::R_AVR_LO8_LDI_NEG;
  case AVR::fixup_hi8_ldi_neg:
    return ELF::R_AVR_HI8_LDI_NEG;
  case AVR::fixup_hh8_ldi_neg:
    return ELF::R_AVR_HH8_LDI_NEG;
  case AVR::fixup_lo8_ldi_pm:
    return ELF::R_AVR_LO8_LDI_PM;
  case AVR::fixup_hi8_ldi_pm:
    return ELF::R_AVR_HI8_LDI_PM;
  case AVR::fixup_hh8_ldi_pm:
    return ELF::R_AVR_HH8_LDI_PM;
  case AVR::fixup_lo8_ldi_pm_neg:
    return ELF::R_AVR_LO8_LDI_PM_NEG;
  case AVR::fixup_hi8_ldi_pm_neg:
    return ELF::R_AVR_HI8_LDI_PM_NEG;
  case AVR::fixup_hh8_ldi_pm_neg:
    return ELF::R_AVR_HH8_LDI_PM_NEG;
  case AVR::fixup_call:
    return ELF::R_AVR_CALL;
  case AVR::fixup_ldi:
    return ELF::R_AVR_LDI;
  case AVR::fixup_6:
    return ELF::R_AVR_6;
  case AVR::fixup_6_adiw:
    return ELF::R_AVR_6_ADIW;
  case AVR::fixup_ms8_ldi:
    return ELF::R_AVR_MS8_LDI;
  case AVR::fixup_ms8_ldi_neg:
    return ELF::R_AVR_MS8_LDI_NEG;
  case AVR::fixup_lo8_ldi_gs:
    return ELF::R_AVR_LO8_LDI_GS;
  case AVR::fixup_hi8_ldi_gs:
    return ELF::R_AVR_HI8_LDI_GS;
  case AVR::fixup_8:
    return ELF::R_AVR_8;
  case AVR::fixup_8_lo8:
    return ELF::R_AVR_8_LO8;
  case AVR::fixup_8_hi8:
    return ELF::R_AVR_8_HI8;
  case AVR::fixup_8_hlo8:
    return ELF::R_AVR_8_HLO8;
  case AVR::fixup_diff8:
    return ELF::R_AVR_DIFF8;
  case AVR::fixup_diff16:
    return ELF::R_AVR_DIFF16;
  case AVR::fixup_diff32:
    return ELF::R_AVR_DIFF32;
  case AVR::fixup_lds_sts_16:
    return ELF::R_AVR_LDS_STS_16;
  case AVR::fixup_port6:
    return ELF::R_AVR_PORT6;
  case AVR::fixup_port5:
    return ELF::R_AVR_PORT5;
  default:
    llvm_unreachable("invalid fixup kind!");
  }
}

std::unique_ptr<MCObjectTargetWriter> createAVRELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<AVRELFObjectWriter>(OSABI);
}

} // end of namespace llvm
