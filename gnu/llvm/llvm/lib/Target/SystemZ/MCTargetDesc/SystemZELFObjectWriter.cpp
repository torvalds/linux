//===-- SystemZELFObjectWriter.cpp - SystemZ ELF writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SystemZMCFixups.h"
#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <memory>

using namespace llvm;

namespace {

class SystemZELFObjectWriter : public MCELFObjectTargetWriter {
public:
  SystemZELFObjectWriter(uint8_t OSABI);
  ~SystemZELFObjectWriter() override = default;

protected:
  // Override MCELFObjectTargetWriter.
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};

} // end anonymous namespace

SystemZELFObjectWriter::SystemZELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(/*Is64Bit_=*/true, OSABI, ELF::EM_S390,
                              /*HasRelocationAddend_=*/true) {}

// Return the relocation type for an absolute value of MCFixupKind Kind.
static unsigned getAbsoluteReloc(MCContext &Ctx, SMLoc Loc, unsigned Kind) {
  switch (Kind) {
  case FK_Data_1:
  case SystemZ::FK_390_U8Imm:
  case SystemZ::FK_390_S8Imm:
    return ELF::R_390_8;
  case SystemZ::FK_390_U12Imm:
    return ELF::R_390_12;
  case FK_Data_2:
  case SystemZ::FK_390_U16Imm:
  case SystemZ::FK_390_S16Imm:
    return ELF::R_390_16;
  case SystemZ::FK_390_S20Imm:
    return ELF::R_390_20;
  case FK_Data_4:
  case SystemZ::FK_390_U32Imm:
  case SystemZ::FK_390_S32Imm:
    return ELF::R_390_32;
  case FK_Data_8:
    return ELF::R_390_64;
  }
  Ctx.reportError(Loc, "Unsupported absolute address");
  return 0;
}

// Return the relocation type for a PC-relative value of MCFixupKind Kind.
static unsigned getPCRelReloc(MCContext &Ctx, SMLoc Loc, unsigned Kind) {
  switch (Kind) {
  case FK_Data_2:
  case SystemZ::FK_390_U16Imm:
  case SystemZ::FK_390_S16Imm:
    return ELF::R_390_PC16;
  case FK_Data_4:
  case SystemZ::FK_390_U32Imm:
  case SystemZ::FK_390_S32Imm:
    return ELF::R_390_PC32;
  case FK_Data_8:
    return ELF::R_390_PC64;
  case SystemZ::FK_390_PC12DBL:
    return ELF::R_390_PC12DBL;
  case SystemZ::FK_390_PC16DBL:
    return ELF::R_390_PC16DBL;
  case SystemZ::FK_390_PC24DBL:
    return ELF::R_390_PC24DBL;
  case SystemZ::FK_390_PC32DBL:
    return ELF::R_390_PC32DBL;
  }
  Ctx.reportError(Loc, "Unsupported PC-relative address");
  return 0;
}

// Return the R_390_TLS_LE* relocation type for MCFixupKind Kind.
static unsigned getTLSLEReloc(MCContext &Ctx, SMLoc Loc, unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_LE32;
  case FK_Data_8: return ELF::R_390_TLS_LE64;
  }
  Ctx.reportError(Loc, "Unsupported thread-local address (local-exec)");
  return 0;
}

// Return the R_390_TLS_LDO* relocation type for MCFixupKind Kind.
static unsigned getTLSLDOReloc(MCContext &Ctx, SMLoc Loc, unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_LDO32;
  case FK_Data_8: return ELF::R_390_TLS_LDO64;
  }
  Ctx.reportError(Loc, "Unsupported thread-local address (local-dynamic)");
  return 0;
}

// Return the R_390_TLS_LDM* relocation type for MCFixupKind Kind.
static unsigned getTLSLDMReloc(MCContext &Ctx, SMLoc Loc, unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_LDM32;
  case FK_Data_8: return ELF::R_390_TLS_LDM64;
  case SystemZ::FK_390_TLS_CALL: return ELF::R_390_TLS_LDCALL;
  }
  Ctx.reportError(Loc, "Unsupported thread-local address (local-dynamic)");
  return 0;
}

// Return the R_390_TLS_GD* relocation type for MCFixupKind Kind.
static unsigned getTLSGDReloc(MCContext &Ctx, SMLoc Loc, unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_GD32;
  case FK_Data_8: return ELF::R_390_TLS_GD64;
  case SystemZ::FK_390_TLS_CALL: return ELF::R_390_TLS_GDCALL;
  }
  Ctx.reportError(Loc, "Unsupported thread-local address (general-dynamic)");
  return 0;
}

// Return the PLT relocation counterpart of MCFixupKind Kind.
static unsigned getPLTReloc(MCContext &Ctx, SMLoc Loc, unsigned Kind) {
  switch (Kind) {
  case SystemZ::FK_390_PC12DBL: return ELF::R_390_PLT12DBL;
  case SystemZ::FK_390_PC16DBL: return ELF::R_390_PLT16DBL;
  case SystemZ::FK_390_PC24DBL: return ELF::R_390_PLT24DBL;
  case SystemZ::FK_390_PC32DBL: return ELF::R_390_PLT32DBL;
  }
  Ctx.reportError(Loc, "Unsupported PC-relative PLT address");
  return 0;
}

unsigned SystemZELFObjectWriter::getRelocType(MCContext &Ctx,
                                              const MCValue &Target,
                                              const MCFixup &Fixup,
                                              bool IsPCRel) const {
  SMLoc Loc = Fixup.getLoc();
  unsigned Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return Kind - FirstLiteralRelocationKind;
  MCSymbolRefExpr::VariantKind Modifier = Target.getAccessVariant();
  switch (Modifier) {
  case MCSymbolRefExpr::VK_None:
    if (IsPCRel)
      return getPCRelReloc(Ctx, Loc, Kind);
    return getAbsoluteReloc(Ctx, Loc, Kind);

  case MCSymbolRefExpr::VK_NTPOFF:
    assert(!IsPCRel && "NTPOFF shouldn't be PC-relative");
    return getTLSLEReloc(Ctx, Loc, Kind);

  case MCSymbolRefExpr::VK_INDNTPOFF:
    if (IsPCRel && Kind == SystemZ::FK_390_PC32DBL)
      return ELF::R_390_TLS_IEENT;
    Ctx.reportError(Loc, "Only PC-relative INDNTPOFF accesses are supported for now");
    return 0;

  case MCSymbolRefExpr::VK_DTPOFF:
    assert(!IsPCRel && "DTPOFF shouldn't be PC-relative");
    return getTLSLDOReloc(Ctx, Loc, Kind);

  case MCSymbolRefExpr::VK_TLSLDM:
    assert(!IsPCRel && "TLSLDM shouldn't be PC-relative");
    return getTLSLDMReloc(Ctx, Loc, Kind);

  case MCSymbolRefExpr::VK_TLSGD:
    assert(!IsPCRel && "TLSGD shouldn't be PC-relative");
    return getTLSGDReloc(Ctx, Loc, Kind);

  case MCSymbolRefExpr::VK_GOT:
    if (IsPCRel && Kind == SystemZ::FK_390_PC32DBL)
      return ELF::R_390_GOTENT;
    Ctx.reportError(Loc, "Only PC-relative GOT accesses are supported for now");
    return 0;

  case MCSymbolRefExpr::VK_PLT:
    assert(IsPCRel && "@PLT shouldn't be PC-relative");
    return getPLTReloc(Ctx, Loc, Kind);

  default:
    llvm_unreachable("Modifier not supported");
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createSystemZELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<SystemZELFObjectWriter>(OSABI);
}
