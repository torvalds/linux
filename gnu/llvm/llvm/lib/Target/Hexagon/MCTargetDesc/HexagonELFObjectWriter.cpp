//===-- HexagonELFObjectWriter.cpp - Hexagon Target Descriptions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonFixupKinds.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "hexagon-elf-writer"

using namespace llvm;
using namespace Hexagon;

namespace {

class HexagonELFObjectWriter : public MCELFObjectTargetWriter {
private:
  StringRef CPU;

public:
  HexagonELFObjectWriter(uint8_t OSABI, StringRef C);

  unsigned getRelocType(MCContext &Ctx, MCValue const &Target,
                        MCFixup const &Fixup, bool IsPCRel) const override;
};
}

HexagonELFObjectWriter::HexagonELFObjectWriter(uint8_t OSABI, StringRef C)
    : MCELFObjectTargetWriter(/*Is64bit*/ false, OSABI, ELF::EM_HEXAGON,
                              /*HasRelocationAddend*/ true),
      CPU(C) {}

unsigned HexagonELFObjectWriter::getRelocType(MCContext &Ctx,
                                              MCValue const &Target,
                                              MCFixup const &Fixup,
                                              bool IsPCRel) const {
  MCSymbolRefExpr::VariantKind Variant = Target.getAccessVariant();
  switch (Fixup.getTargetKind()) {
  default:
    report_fatal_error("Unrecognized relocation type");
    break;
  case FK_Data_4:
    switch(Variant) {
    case MCSymbolRefExpr::VariantKind::VK_DTPREL:
      return ELF::R_HEX_DTPREL_32;
    case MCSymbolRefExpr::VariantKind::VK_GOT:
      return ELF::R_HEX_GOT_32;
    case MCSymbolRefExpr::VariantKind::VK_GOTREL:
      return ELF::R_HEX_GOTREL_32;
    case MCSymbolRefExpr::VariantKind::VK_Hexagon_GD_GOT:
      return ELF::R_HEX_GD_GOT_32;
    case MCSymbolRefExpr::VariantKind::VK_Hexagon_IE:
      return ELF::R_HEX_IE_32;
    case MCSymbolRefExpr::VariantKind::VK_Hexagon_IE_GOT:
      return ELF::R_HEX_IE_GOT_32;
    case MCSymbolRefExpr::VariantKind::VK_Hexagon_LD_GOT:
      return ELF::R_HEX_LD_GOT_32;
    case MCSymbolRefExpr::VariantKind::VK_PCREL:
      return ELF::R_HEX_32_PCREL;
    case MCSymbolRefExpr::VariantKind::VK_TPREL:
      return ELF::R_HEX_TPREL_32;
    case MCSymbolRefExpr::VariantKind::VK_None:
      return IsPCRel ? ELF::R_HEX_32_PCREL : ELF::R_HEX_32;
    default:
      report_fatal_error("Unrecognized variant type");
    };
  case FK_PCRel_4:
    return ELF::R_HEX_32_PCREL;
  case FK_Data_2:
    switch(Variant) {
    case MCSymbolRefExpr::VariantKind::VK_DTPREL:
      return ELF::R_HEX_DTPREL_16;
    case MCSymbolRefExpr::VariantKind::VK_GOT:
      return ELF::R_HEX_GOT_16;
    case MCSymbolRefExpr::VariantKind::VK_Hexagon_GD_GOT:
      return ELF::R_HEX_GD_GOT_16;
    case MCSymbolRefExpr::VariantKind::VK_Hexagon_IE_GOT:
      return ELF::R_HEX_IE_GOT_16;
    case MCSymbolRefExpr::VariantKind::VK_Hexagon_LD_GOT:
      return ELF::R_HEX_LD_GOT_16;
    case MCSymbolRefExpr::VariantKind::VK_TPREL:
      return ELF::R_HEX_TPREL_16;
    case MCSymbolRefExpr::VariantKind::VK_None:
      return ELF::R_HEX_16;
    default:
      report_fatal_error("Unrecognized variant type");
    };
  case FK_Data_1:
    return ELF::R_HEX_8;
  case fixup_Hexagon_B22_PCREL:
    return ELF::R_HEX_B22_PCREL;
  case fixup_Hexagon_B15_PCREL:
    return ELF::R_HEX_B15_PCREL;
  case fixup_Hexagon_B7_PCREL:
    return ELF::R_HEX_B7_PCREL;
  case fixup_Hexagon_LO16:
    return ELF::R_HEX_LO16;
  case fixup_Hexagon_HI16:
    return ELF::R_HEX_HI16;
  case fixup_Hexagon_32:
    return ELF::R_HEX_32;
  case fixup_Hexagon_16:
    return ELF::R_HEX_16;
  case fixup_Hexagon_8:
    return ELF::R_HEX_8;
  case fixup_Hexagon_GPREL16_0:
    return ELF::R_HEX_GPREL16_0;
  case fixup_Hexagon_GPREL16_1:
    return ELF::R_HEX_GPREL16_1;
  case fixup_Hexagon_GPREL16_2:
    return ELF::R_HEX_GPREL16_2;
  case fixup_Hexagon_GPREL16_3:
    return ELF::R_HEX_GPREL16_3;
  case fixup_Hexagon_HL16:
    return ELF::R_HEX_HL16;
  case fixup_Hexagon_B13_PCREL:
    return ELF::R_HEX_B13_PCREL;
  case fixup_Hexagon_B9_PCREL:
    return ELF::R_HEX_B9_PCREL;
  case fixup_Hexagon_B32_PCREL_X:
    return ELF::R_HEX_B32_PCREL_X;
  case fixup_Hexagon_32_6_X:
    return ELF::R_HEX_32_6_X;
  case fixup_Hexagon_B22_PCREL_X:
    return ELF::R_HEX_B22_PCREL_X;
  case fixup_Hexagon_B15_PCREL_X:
    return ELF::R_HEX_B15_PCREL_X;
  case fixup_Hexagon_B13_PCREL_X:
    return ELF::R_HEX_B13_PCREL_X;
  case fixup_Hexagon_B9_PCREL_X:
    return ELF::R_HEX_B9_PCREL_X;
  case fixup_Hexagon_B7_PCREL_X:
    return ELF::R_HEX_B7_PCREL_X;
  case fixup_Hexagon_16_X:
    return ELF::R_HEX_16_X;
  case fixup_Hexagon_12_X:
    return ELF::R_HEX_12_X;
  case fixup_Hexagon_11_X:
    return ELF::R_HEX_11_X;
  case fixup_Hexagon_10_X:
    return ELF::R_HEX_10_X;
  case fixup_Hexagon_9_X:
    return ELF::R_HEX_9_X;
  case fixup_Hexagon_8_X:
    return ELF::R_HEX_8_X;
  case fixup_Hexagon_7_X:
    return ELF::R_HEX_7_X;
  case fixup_Hexagon_6_X:
    return ELF::R_HEX_6_X;
  case fixup_Hexagon_32_PCREL:
    return ELF::R_HEX_32_PCREL;
  case fixup_Hexagon_COPY:
    return ELF::R_HEX_COPY;
  case fixup_Hexagon_GLOB_DAT:
    return ELF::R_HEX_GLOB_DAT;
  case fixup_Hexagon_JMP_SLOT:
    return ELF::R_HEX_JMP_SLOT;
  case fixup_Hexagon_RELATIVE:
    return ELF::R_HEX_RELATIVE;
  case fixup_Hexagon_PLT_B22_PCREL:
    return ELF::R_HEX_PLT_B22_PCREL;
  case fixup_Hexagon_GOTREL_LO16:
    return ELF::R_HEX_GOTREL_LO16;
  case fixup_Hexagon_GOTREL_HI16:
    return ELF::R_HEX_GOTREL_HI16;
  case fixup_Hexagon_GOTREL_32:
    return ELF::R_HEX_GOTREL_32;
  case fixup_Hexagon_GOT_LO16:
    return ELF::R_HEX_GOT_LO16;
  case fixup_Hexagon_GOT_HI16:
    return ELF::R_HEX_GOT_HI16;
  case fixup_Hexagon_GOT_32:
    return ELF::R_HEX_GOT_32;
  case fixup_Hexagon_GOT_16:
    return ELF::R_HEX_GOT_16;
  case fixup_Hexagon_DTPMOD_32:
    return ELF::R_HEX_DTPMOD_32;
  case fixup_Hexagon_DTPREL_LO16:
    return ELF::R_HEX_DTPREL_LO16;
  case fixup_Hexagon_DTPREL_HI16:
    return ELF::R_HEX_DTPREL_HI16;
  case fixup_Hexagon_DTPREL_32:
    return ELF::R_HEX_DTPREL_32;
  case fixup_Hexagon_DTPREL_16:
    return ELF::R_HEX_DTPREL_16;
  case fixup_Hexagon_GD_PLT_B22_PCREL:
    return ELF::R_HEX_GD_PLT_B22_PCREL;
  case fixup_Hexagon_LD_PLT_B22_PCREL:
    return ELF::R_HEX_LD_PLT_B22_PCREL;
  case fixup_Hexagon_GD_GOT_LO16:
    return ELF::R_HEX_GD_GOT_LO16;
  case fixup_Hexagon_GD_GOT_HI16:
    return ELF::R_HEX_GD_GOT_HI16;
  case fixup_Hexagon_GD_GOT_32:
    return ELF::R_HEX_GD_GOT_32;
  case fixup_Hexagon_GD_GOT_16:
    return ELF::R_HEX_GD_GOT_16;
  case fixup_Hexagon_LD_GOT_LO16:
    return ELF::R_HEX_LD_GOT_LO16;
  case fixup_Hexagon_LD_GOT_HI16:
    return ELF::R_HEX_LD_GOT_HI16;
  case fixup_Hexagon_LD_GOT_32:
    return ELF::R_HEX_LD_GOT_32;
  case fixup_Hexagon_LD_GOT_16:
    return ELF::R_HEX_LD_GOT_16;
  case fixup_Hexagon_IE_LO16:
    return ELF::R_HEX_IE_LO16;
  case fixup_Hexagon_IE_HI16:
    return ELF::R_HEX_IE_HI16;
  case fixup_Hexagon_IE_32:
    return ELF::R_HEX_IE_32;
  case fixup_Hexagon_IE_GOT_LO16:
    return ELF::R_HEX_IE_GOT_LO16;
  case fixup_Hexagon_IE_GOT_HI16:
    return ELF::R_HEX_IE_GOT_HI16;
  case fixup_Hexagon_IE_GOT_32:
    return ELF::R_HEX_IE_GOT_32;
  case fixup_Hexagon_IE_GOT_16:
    return ELF::R_HEX_IE_GOT_16;
  case fixup_Hexagon_TPREL_LO16:
    return ELF::R_HEX_TPREL_LO16;
  case fixup_Hexagon_TPREL_HI16:
    return ELF::R_HEX_TPREL_HI16;
  case fixup_Hexagon_TPREL_32:
    return ELF::R_HEX_TPREL_32;
  case fixup_Hexagon_TPREL_16:
    return ELF::R_HEX_TPREL_16;
  case fixup_Hexagon_6_PCREL_X:
    return ELF::R_HEX_6_PCREL_X;
  case fixup_Hexagon_GOTREL_32_6_X:
    return ELF::R_HEX_GOTREL_32_6_X;
  case fixup_Hexagon_GOTREL_16_X:
    return ELF::R_HEX_GOTREL_16_X;
  case fixup_Hexagon_GOTREL_11_X:
    return ELF::R_HEX_GOTREL_11_X;
  case fixup_Hexagon_GOT_32_6_X:
    return ELF::R_HEX_GOT_32_6_X;
  case fixup_Hexagon_GOT_16_X:
    return ELF::R_HEX_GOT_16_X;
  case fixup_Hexagon_GOT_11_X:
    return ELF::R_HEX_GOT_11_X;
  case fixup_Hexagon_DTPREL_32_6_X:
    return ELF::R_HEX_DTPREL_32_6_X;
  case fixup_Hexagon_DTPREL_16_X:
    return ELF::R_HEX_DTPREL_16_X;
  case fixup_Hexagon_DTPREL_11_X:
    return ELF::R_HEX_DTPREL_11_X;
  case fixup_Hexagon_GD_GOT_32_6_X:
    return ELF::R_HEX_GD_GOT_32_6_X;
  case fixup_Hexagon_GD_GOT_16_X:
    return ELF::R_HEX_GD_GOT_16_X;
  case fixup_Hexagon_GD_GOT_11_X:
    return ELF::R_HEX_GD_GOT_11_X;
  case fixup_Hexagon_LD_GOT_32_6_X:
    return ELF::R_HEX_LD_GOT_32_6_X;
  case fixup_Hexagon_LD_GOT_16_X:
    return ELF::R_HEX_LD_GOT_16_X;
  case fixup_Hexagon_LD_GOT_11_X:
    return ELF::R_HEX_LD_GOT_11_X;
  case fixup_Hexagon_IE_32_6_X:
    return ELF::R_HEX_IE_32_6_X;
  case fixup_Hexagon_IE_16_X:
    return ELF::R_HEX_IE_16_X;
  case fixup_Hexagon_IE_GOT_32_6_X:
    return ELF::R_HEX_IE_GOT_32_6_X;
  case fixup_Hexagon_IE_GOT_16_X:
    return ELF::R_HEX_IE_GOT_16_X;
  case fixup_Hexagon_IE_GOT_11_X:
    return ELF::R_HEX_IE_GOT_11_X;
  case fixup_Hexagon_TPREL_32_6_X:
    return ELF::R_HEX_TPREL_32_6_X;
  case fixup_Hexagon_TPREL_16_X:
    return ELF::R_HEX_TPREL_16_X;
  case fixup_Hexagon_TPREL_11_X:
    return ELF::R_HEX_TPREL_11_X;
  case fixup_Hexagon_23_REG:
    return ELF::R_HEX_23_REG;
  case fixup_Hexagon_27_REG:
    return ELF::R_HEX_27_REG;
  case fixup_Hexagon_GD_PLT_B22_PCREL_X:
    return ELF::R_HEX_GD_PLT_B22_PCREL_X;
  case fixup_Hexagon_GD_PLT_B32_PCREL_X:
    return ELF::R_HEX_GD_PLT_B32_PCREL_X;
  case fixup_Hexagon_LD_PLT_B22_PCREL_X:
    return ELF::R_HEX_LD_PLT_B22_PCREL_X;
  case fixup_Hexagon_LD_PLT_B32_PCREL_X:
    return ELF::R_HEX_LD_PLT_B32_PCREL_X;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createHexagonELFObjectWriter(uint8_t OSABI, StringRef CPU) {
  return std::make_unique<HexagonELFObjectWriter>(OSABI, CPU);
}
