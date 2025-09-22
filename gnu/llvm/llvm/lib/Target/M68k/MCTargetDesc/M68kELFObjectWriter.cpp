//===-- M68kELFObjectWriter.cpp - M68k ELF Writer ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains definitions for M68k ELF Writers
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M68kFixupKinds.h"
#include "MCTargetDesc/M68kMCTargetDesc.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class M68kELFObjectWriter : public MCELFObjectTargetWriter {
public:
  M68kELFObjectWriter(uint8_t OSABI);

  ~M68kELFObjectWriter() override;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};
} // namespace

M68kELFObjectWriter::M68kELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(false, OSABI, ELF::EM_68K, /* RELA */ true) {}

M68kELFObjectWriter::~M68kELFObjectWriter() {}

enum M68kRelType { RT_32, RT_16, RT_8 };

static M68kRelType
getType(unsigned Kind, MCSymbolRefExpr::VariantKind &Modifier, bool &IsPCRel) {
  switch (Kind) {
  case FK_Data_4:
  case FK_PCRel_4:
    return RT_32;
  case FK_PCRel_2:
  case FK_Data_2:
    return RT_16;
  case FK_PCRel_1:
  case FK_Data_1:
    return RT_8;
  }
  llvm_unreachable("Unimplemented");
}

unsigned M68kELFObjectWriter::getRelocType(MCContext &Ctx,
                                           const MCValue &Target,
                                           const MCFixup &Fixup,
                                           bool IsPCRel) const {
  MCSymbolRefExpr::VariantKind Modifier = Target.getAccessVariant();
  unsigned Kind = Fixup.getKind();
  M68kRelType Type = getType(Kind, Modifier, IsPCRel);
  switch (Modifier) {
  default:
    llvm_unreachable("Unimplemented");

  case MCSymbolRefExpr::VK_TLSGD:
    switch (Type) {
    case RT_32:
      return ELF::R_68K_TLS_GD32;
    case RT_16:
      return ELF::R_68K_TLS_GD16;
    case RT_8:
      return ELF::R_68K_TLS_GD8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_TLSLDM:
    switch (Type) {
    case RT_32:
      return ELF::R_68K_TLS_LDM32;
    case RT_16:
      return ELF::R_68K_TLS_LDM16;
    case RT_8:
      return ELF::R_68K_TLS_LDM8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_TLSLD:
    switch (Type) {
    case RT_32:
      return ELF::R_68K_TLS_LDO32;
    case RT_16:
      return ELF::R_68K_TLS_LDO16;
    case RT_8:
      return ELF::R_68K_TLS_LDO8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_GOTTPOFF:
    switch (Type) {
    case RT_32:
      return ELF::R_68K_TLS_IE32;
    case RT_16:
      return ELF::R_68K_TLS_IE16;
    case RT_8:
      return ELF::R_68K_TLS_IE8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_TPOFF:
    switch (Type) {
    case RT_32:
      return ELF::R_68K_TLS_LE32;
    case RT_16:
      return ELF::R_68K_TLS_LE16;
    case RT_8:
      return ELF::R_68K_TLS_LE8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_None:
    switch (Type) {
    case RT_32:
      return IsPCRel ? ELF::R_68K_PC32 : ELF::R_68K_32;
    case RT_16:
      return IsPCRel ? ELF::R_68K_PC16 : ELF::R_68K_16;
    case RT_8:
      return IsPCRel ? ELF::R_68K_PC8 : ELF::R_68K_8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_GOTPCREL:
    switch (Type) {
    case RT_32:
      return ELF::R_68K_GOTPCREL32;
    case RT_16:
      return ELF::R_68K_GOTPCREL16;
    case RT_8:
      return ELF::R_68K_GOTPCREL8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_GOTOFF:
    assert(!IsPCRel);
    switch (Type) {
    case RT_32:
      return ELF::R_68K_GOTOFF32;
    case RT_16:
      return ELF::R_68K_GOTOFF16;
    case RT_8:
      return ELF::R_68K_GOTOFF8;
    }
    llvm_unreachable("Unrecognized size");
  case MCSymbolRefExpr::VK_PLT:
    switch (Type) {
    case RT_32:
      return ELF::R_68K_PLT32;
    case RT_16:
      return ELF::R_68K_PLT16;
    case RT_8:
      return ELF::R_68K_PLT8;
    }
    llvm_unreachable("Unrecognized size");
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createM68kELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<M68kELFObjectWriter>(OSABI);
}
