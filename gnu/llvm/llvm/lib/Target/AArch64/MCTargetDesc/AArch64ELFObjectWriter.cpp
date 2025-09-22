//===-- AArch64ELFObjectWriter.cpp - AArch64 ELF Writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file handles ELF-specific object emission, converting LLVM's internal
// fixups into the appropriate relocations.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/AArch64FixupKinds.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {

class AArch64ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  AArch64ELFObjectWriter(uint8_t OSABI, bool IsILP32);

  ~AArch64ELFObjectWriter() override = default;

  MCSectionELF *getMemtagRelocsSection(MCContext &Ctx) const override;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
  bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                               unsigned Type) const override;
  bool IsILP32;
};

} // end anonymous namespace

AArch64ELFObjectWriter::AArch64ELFObjectWriter(uint8_t OSABI, bool IsILP32)
    : MCELFObjectTargetWriter(/*Is64Bit*/ !IsILP32, OSABI, ELF::EM_AARCH64,
                              /*HasRelocationAddend*/ true),
      IsILP32(IsILP32) {}

#define R_CLS(rtype)                                                           \
  IsILP32 ? ELF::R_AARCH64_P32_##rtype : ELF::R_AARCH64_##rtype
#define BAD_ILP32_MOV(lp64rtype)                                               \
  "ILP32 absolute MOV relocation not "                                         \
  "supported (LP64 eqv: " #lp64rtype ")"

// assumes IsILP32 is true
static bool isNonILP32reloc(const MCFixup &Fixup,
                            AArch64MCExpr::VariantKind RefKind,
                            MCContext &Ctx) {
  if (Fixup.getTargetKind() != AArch64::fixup_aarch64_movw)
    return false;
  switch (RefKind) {
  case AArch64MCExpr::VK_ABS_G3:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(MOVW_UABS_G3));
    return true;
  case AArch64MCExpr::VK_ABS_G2:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(MOVW_UABS_G2));
    return true;
  case AArch64MCExpr::VK_ABS_G2_S:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(MOVW_SABS_G2));
    return true;
  case AArch64MCExpr::VK_ABS_G2_NC:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(MOVW_UABS_G2_NC));
    return true;
  case AArch64MCExpr::VK_ABS_G1_S:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(MOVW_SABS_G1));
    return true;
  case AArch64MCExpr::VK_ABS_G1_NC:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(MOVW_UABS_G1_NC));
    return true;
  case AArch64MCExpr::VK_DTPREL_G2:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(TLSLD_MOVW_DTPREL_G2));
    return true;
  case AArch64MCExpr::VK_DTPREL_G1_NC:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(TLSLD_MOVW_DTPREL_G1_NC));
    return true;
  case AArch64MCExpr::VK_TPREL_G2:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(TLSLE_MOVW_TPREL_G2));
    return true;
  case AArch64MCExpr::VK_TPREL_G1_NC:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(TLSLE_MOVW_TPREL_G1_NC));
    return true;
  case AArch64MCExpr::VK_GOTTPREL_G1:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(TLSIE_MOVW_GOTTPREL_G1));
    return true;
  case AArch64MCExpr::VK_GOTTPREL_G0_NC:
    Ctx.reportError(Fixup.getLoc(), BAD_ILP32_MOV(TLSIE_MOVW_GOTTPREL_G0_NC));
    return true;
  default:
    return false;
  }
  return false;
}

unsigned AArch64ELFObjectWriter::getRelocType(MCContext &Ctx,
                                              const MCValue &Target,
                                              const MCFixup &Fixup,
                                              bool IsPCRel) const {
  unsigned Kind = Fixup.getTargetKind();
  if (Kind >= FirstLiteralRelocationKind)
    return Kind - FirstLiteralRelocationKind;
  AArch64MCExpr::VariantKind RefKind =
      static_cast<AArch64MCExpr::VariantKind>(Target.getRefKind());
  AArch64MCExpr::VariantKind SymLoc = AArch64MCExpr::getSymbolLoc(RefKind);
  bool IsNC = AArch64MCExpr::isNotChecked(RefKind);

  assert((!Target.getSymA() ||
          Target.getSymA()->getKind() == MCSymbolRefExpr::VK_None ||
          Target.getSymA()->getKind() == MCSymbolRefExpr::VK_PLT ||
          Target.getSymA()->getKind() == MCSymbolRefExpr::VK_GOTPCREL) &&
         "Should only be expression-level modifiers here");

  assert((!Target.getSymB() ||
          Target.getSymB()->getKind() == MCSymbolRefExpr::VK_None) &&
         "Should only be expression-level modifiers here");

  if (IsPCRel) {
    switch (Kind) {
    case FK_Data_1:
      Ctx.reportError(Fixup.getLoc(), "1-byte data relocations not supported");
      return ELF::R_AARCH64_NONE;
    case FK_Data_2:
      return R_CLS(PREL16);
    case FK_Data_4: {
      return Target.getAccessVariant() == MCSymbolRefExpr::VK_PLT
                 ? R_CLS(PLT32)
                 : R_CLS(PREL32);
    }
    case FK_Data_8:
      if (IsILP32) {
        Ctx.reportError(Fixup.getLoc(),
                        "ILP32 8 byte PC relative data "
                        "relocation not supported (LP64 eqv: PREL64)");
        return ELF::R_AARCH64_NONE;
      }
      return ELF::R_AARCH64_PREL64;
    case AArch64::fixup_aarch64_pcrel_adr_imm21:
      if (SymLoc != AArch64MCExpr::VK_ABS)
        Ctx.reportError(Fixup.getLoc(),
                        "invalid symbol kind for ADR relocation");
      return R_CLS(ADR_PREL_LO21);
    case AArch64::fixup_aarch64_pcrel_adrp_imm21:
      if (SymLoc == AArch64MCExpr::VK_ABS && !IsNC)
        return R_CLS(ADR_PREL_PG_HI21);
      if (SymLoc == AArch64MCExpr::VK_ABS && IsNC) {
        if (IsILP32) {
          Ctx.reportError(Fixup.getLoc(),
                          "invalid fixup for 32-bit pcrel ADRP instruction "
                          "VK_ABS VK_NC");
          return ELF::R_AARCH64_NONE;
        }
        return ELF::R_AARCH64_ADR_PREL_PG_HI21_NC;
      }
      if (SymLoc == AArch64MCExpr::VK_GOT && !IsNC)
        return R_CLS(ADR_GOT_PAGE);
      if (SymLoc == AArch64MCExpr::VK_GOTTPREL && !IsNC)
        return R_CLS(TLSIE_ADR_GOTTPREL_PAGE21);
      if (SymLoc == AArch64MCExpr::VK_TLSDESC && !IsNC)
        return R_CLS(TLSDESC_ADR_PAGE21);
      Ctx.reportError(Fixup.getLoc(),
                      "invalid symbol kind for ADRP relocation");
      return ELF::R_AARCH64_NONE;
    case AArch64::fixup_aarch64_pcrel_branch26:
      return R_CLS(JUMP26);
    case AArch64::fixup_aarch64_pcrel_call26:
      return R_CLS(CALL26);
    case AArch64::fixup_aarch64_ldr_pcrel_imm19:
      if (SymLoc == AArch64MCExpr::VK_GOTTPREL)
        return R_CLS(TLSIE_LD_GOTTPREL_PREL19);
      if (SymLoc == AArch64MCExpr::VK_GOT)
        return R_CLS(GOT_LD_PREL19);
      return R_CLS(LD_PREL_LO19);
    case AArch64::fixup_aarch64_pcrel_branch14:
      return R_CLS(TSTBR14);
    case AArch64::fixup_aarch64_pcrel_branch16:
      Ctx.reportError(Fixup.getLoc(),
                      "relocation of PAC/AUT instructions is not supported");
      return ELF::R_AARCH64_NONE;
    case AArch64::fixup_aarch64_pcrel_branch19:
      return R_CLS(CONDBR19);
    default:
      Ctx.reportError(Fixup.getLoc(), "Unsupported pc-relative fixup kind");
      return ELF::R_AARCH64_NONE;
    }
  } else {
    if (IsILP32 && isNonILP32reloc(Fixup, RefKind, Ctx))
      return ELF::R_AARCH64_NONE;
    switch (Fixup.getTargetKind()) {
    case FK_Data_1:
      Ctx.reportError(Fixup.getLoc(), "1-byte data relocations not supported");
      return ELF::R_AARCH64_NONE;
    case FK_Data_2:
      return R_CLS(ABS16);
    case FK_Data_4:
      return (!IsILP32 &&
              Target.getAccessVariant() == MCSymbolRefExpr::VK_GOTPCREL)
                 ? ELF::R_AARCH64_GOTPCREL32
                 : R_CLS(ABS32);
    case FK_Data_8: {
      bool IsAuth = (RefKind == AArch64MCExpr::VK_AUTH ||
                     RefKind == AArch64MCExpr::VK_AUTHADDR);
      if (IsILP32) {
        Ctx.reportError(Fixup.getLoc(),
                        Twine("ILP32 8 byte absolute data "
                              "relocation not supported (LP64 eqv: ") +
                            (IsAuth ? "AUTH_ABS64" : "ABS64") + Twine(')'));
        return ELF::R_AARCH64_NONE;
      }
      return (IsAuth ? ELF::R_AARCH64_AUTH_ABS64 : ELF::R_AARCH64_ABS64);
    }
    case AArch64::fixup_aarch64_add_imm12:
      if (RefKind == AArch64MCExpr::VK_DTPREL_HI12)
        return R_CLS(TLSLD_ADD_DTPREL_HI12);
      if (RefKind == AArch64MCExpr::VK_TPREL_HI12)
        return R_CLS(TLSLE_ADD_TPREL_HI12);
      if (RefKind == AArch64MCExpr::VK_DTPREL_LO12_NC)
        return R_CLS(TLSLD_ADD_DTPREL_LO12_NC);
      if (RefKind == AArch64MCExpr::VK_DTPREL_LO12)
        return R_CLS(TLSLD_ADD_DTPREL_LO12);
      if (RefKind == AArch64MCExpr::VK_TPREL_LO12_NC)
        return R_CLS(TLSLE_ADD_TPREL_LO12_NC);
      if (RefKind == AArch64MCExpr::VK_TPREL_LO12)
        return R_CLS(TLSLE_ADD_TPREL_LO12);
      if (RefKind == AArch64MCExpr::VK_TLSDESC_LO12)
        return R_CLS(TLSDESC_ADD_LO12);
      if (SymLoc == AArch64MCExpr::VK_ABS && IsNC)
        return R_CLS(ADD_ABS_LO12_NC);

      Ctx.reportError(Fixup.getLoc(),
                      "invalid fixup for add (uimm12) instruction");
      return ELF::R_AARCH64_NONE;
    case AArch64::fixup_aarch64_ldst_imm12_scale1:
      if (SymLoc == AArch64MCExpr::VK_ABS && IsNC)
        return R_CLS(LDST8_ABS_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && !IsNC)
        return R_CLS(TLSLD_LDST8_DTPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && IsNC)
        return R_CLS(TLSLD_LDST8_DTPREL_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_TPREL && !IsNC)
        return R_CLS(TLSLE_LDST8_TPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_TPREL && IsNC)
        return R_CLS(TLSLE_LDST8_TPREL_LO12_NC);

      Ctx.reportError(Fixup.getLoc(),
                      "invalid fixup for 8-bit load/store instruction");
      return ELF::R_AARCH64_NONE;
    case AArch64::fixup_aarch64_ldst_imm12_scale2:
      if (SymLoc == AArch64MCExpr::VK_ABS && IsNC)
        return R_CLS(LDST16_ABS_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && !IsNC)
        return R_CLS(TLSLD_LDST16_DTPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && IsNC)
        return R_CLS(TLSLD_LDST16_DTPREL_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_TPREL && !IsNC)
        return R_CLS(TLSLE_LDST16_TPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_TPREL && IsNC)
        return R_CLS(TLSLE_LDST16_TPREL_LO12_NC);

      Ctx.reportError(Fixup.getLoc(),
                      "invalid fixup for 16-bit load/store instruction");
      return ELF::R_AARCH64_NONE;
    case AArch64::fixup_aarch64_ldst_imm12_scale4:
      if (SymLoc == AArch64MCExpr::VK_ABS && IsNC)
        return R_CLS(LDST32_ABS_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && !IsNC)
        return R_CLS(TLSLD_LDST32_DTPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && IsNC)
        return R_CLS(TLSLD_LDST32_DTPREL_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_TPREL && !IsNC)
        return R_CLS(TLSLE_LDST32_TPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_TPREL && IsNC)
        return R_CLS(TLSLE_LDST32_TPREL_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_GOT && IsNC) {
        if (IsILP32)
          return ELF::R_AARCH64_P32_LD32_GOT_LO12_NC;
        Ctx.reportError(Fixup.getLoc(),
                        "LP64 4 byte unchecked GOT load/store relocation "
                        "not supported (ILP32 eqv: LD32_GOT_LO12_NC");
        return ELF::R_AARCH64_NONE;
      }
      if (SymLoc == AArch64MCExpr::VK_GOT && !IsNC) {
        if (IsILP32) {
          Ctx.reportError(Fixup.getLoc(),
                          "ILP32 4 byte checked GOT load/store relocation "
                          "not supported (unchecked eqv: LD32_GOT_LO12_NC)");
        } else {
          Ctx.reportError(Fixup.getLoc(),
                          "LP64 4 byte checked GOT load/store relocation "
                          "not supported (unchecked/ILP32 eqv: "
                          "LD32_GOT_LO12_NC)");
        }
        return ELF::R_AARCH64_NONE;
      }
      if (SymLoc == AArch64MCExpr::VK_GOTTPREL && IsNC) {
        if (IsILP32)
          return ELF::R_AARCH64_P32_TLSIE_LD32_GOTTPREL_LO12_NC;
        Ctx.reportError(Fixup.getLoc(), "LP64 32-bit load/store "
                                        "relocation not supported (ILP32 eqv: "
                                        "TLSIE_LD32_GOTTPREL_LO12_NC)");
        return ELF::R_AARCH64_NONE;
      }
      if (SymLoc == AArch64MCExpr::VK_TLSDESC && !IsNC) {
        if (IsILP32)
          return ELF::R_AARCH64_P32_TLSDESC_LD32_LO12;
        Ctx.reportError(Fixup.getLoc(),
                        "LP64 4 byte TLSDESC load/store relocation "
                        "not supported (ILP32 eqv: TLSDESC_LD64_LO12)");
        return ELF::R_AARCH64_NONE;
      }

      Ctx.reportError(Fixup.getLoc(),
                      "invalid fixup for 32-bit load/store instruction "
                      "fixup_aarch64_ldst_imm12_scale4");
      return ELF::R_AARCH64_NONE;
    case AArch64::fixup_aarch64_ldst_imm12_scale8:
      if (SymLoc == AArch64MCExpr::VK_ABS && IsNC)
        return R_CLS(LDST64_ABS_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_GOT && IsNC) {
        AArch64MCExpr::VariantKind AddressLoc =
            AArch64MCExpr::getAddressFrag(RefKind);
        if (!IsILP32) {
          if (AddressLoc == AArch64MCExpr::VK_LO15)
            return ELF::R_AARCH64_LD64_GOTPAGE_LO15;
          return ELF::R_AARCH64_LD64_GOT_LO12_NC;
        }
        Ctx.reportError(Fixup.getLoc(), "ILP32 64-bit load/store "
                                        "relocation not supported (LP64 eqv: "
                                        "LD64_GOT_LO12_NC)");
        return ELF::R_AARCH64_NONE;
      }
      if (SymLoc == AArch64MCExpr::VK_DTPREL && !IsNC)
        return R_CLS(TLSLD_LDST64_DTPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && IsNC)
        return R_CLS(TLSLD_LDST64_DTPREL_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_TPREL && !IsNC)
        return R_CLS(TLSLE_LDST64_TPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_TPREL && IsNC)
        return R_CLS(TLSLE_LDST64_TPREL_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_GOTTPREL && IsNC) {
        if (!IsILP32)
          return ELF::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC;
        Ctx.reportError(Fixup.getLoc(), "ILP32 64-bit load/store "
                                        "relocation not supported (LP64 eqv: "
                                        "TLSIE_LD64_GOTTPREL_LO12_NC)");
        return ELF::R_AARCH64_NONE;
      }
      if (SymLoc == AArch64MCExpr::VK_TLSDESC) {
        if (!IsILP32)
          return ELF::R_AARCH64_TLSDESC_LD64_LO12;
        Ctx.reportError(Fixup.getLoc(), "ILP32 64-bit load/store "
                                        "relocation not supported (LP64 eqv: "
                                        "TLSDESC_LD64_LO12)");
        return ELF::R_AARCH64_NONE;
      }
      Ctx.reportError(Fixup.getLoc(),
                      "invalid fixup for 64-bit load/store instruction");
      return ELF::R_AARCH64_NONE;
    case AArch64::fixup_aarch64_ldst_imm12_scale16:
      if (SymLoc == AArch64MCExpr::VK_ABS && IsNC)
        return R_CLS(LDST128_ABS_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && !IsNC)
        return R_CLS(TLSLD_LDST128_DTPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_DTPREL && IsNC)
        return R_CLS(TLSLD_LDST128_DTPREL_LO12_NC);
      if (SymLoc == AArch64MCExpr::VK_TPREL && !IsNC)
        return R_CLS(TLSLE_LDST128_TPREL_LO12);
      if (SymLoc == AArch64MCExpr::VK_TPREL && IsNC)
        return R_CLS(TLSLE_LDST128_TPREL_LO12_NC);

      Ctx.reportError(Fixup.getLoc(),
                      "invalid fixup for 128-bit load/store instruction");
      return ELF::R_AARCH64_NONE;
    // ILP32 case not reached here, tested with isNonILP32reloc
    case AArch64::fixup_aarch64_movw:
      if (RefKind == AArch64MCExpr::VK_ABS_G3)
        return ELF::R_AARCH64_MOVW_UABS_G3;
      if (RefKind == AArch64MCExpr::VK_ABS_G2)
        return ELF::R_AARCH64_MOVW_UABS_G2;
      if (RefKind == AArch64MCExpr::VK_ABS_G2_S)
        return ELF::R_AARCH64_MOVW_SABS_G2;
      if (RefKind == AArch64MCExpr::VK_ABS_G2_NC)
        return ELF::R_AARCH64_MOVW_UABS_G2_NC;
      if (RefKind == AArch64MCExpr::VK_ABS_G1)
        return R_CLS(MOVW_UABS_G1);
      if (RefKind == AArch64MCExpr::VK_ABS_G1_S)
        return ELF::R_AARCH64_MOVW_SABS_G1;
      if (RefKind == AArch64MCExpr::VK_ABS_G1_NC)
        return ELF::R_AARCH64_MOVW_UABS_G1_NC;
      if (RefKind == AArch64MCExpr::VK_ABS_G0)
        return R_CLS(MOVW_UABS_G0);
      if (RefKind == AArch64MCExpr::VK_ABS_G0_S)
        return R_CLS(MOVW_SABS_G0);
      if (RefKind == AArch64MCExpr::VK_ABS_G0_NC)
        return R_CLS(MOVW_UABS_G0_NC);
      if (RefKind == AArch64MCExpr::VK_PREL_G3)
        return ELF::R_AARCH64_MOVW_PREL_G3;
      if (RefKind == AArch64MCExpr::VK_PREL_G2)
        return ELF::R_AARCH64_MOVW_PREL_G2;
      if (RefKind == AArch64MCExpr::VK_PREL_G2_NC)
        return ELF::R_AARCH64_MOVW_PREL_G2_NC;
      if (RefKind == AArch64MCExpr::VK_PREL_G1)
        return R_CLS(MOVW_PREL_G1);
      if (RefKind == AArch64MCExpr::VK_PREL_G1_NC)
        return ELF::R_AARCH64_MOVW_PREL_G1_NC;
      if (RefKind == AArch64MCExpr::VK_PREL_G0)
        return R_CLS(MOVW_PREL_G0);
      if (RefKind == AArch64MCExpr::VK_PREL_G0_NC)
        return R_CLS(MOVW_PREL_G0_NC);
      if (RefKind == AArch64MCExpr::VK_DTPREL_G2)
        return ELF::R_AARCH64_TLSLD_MOVW_DTPREL_G2;
      if (RefKind == AArch64MCExpr::VK_DTPREL_G1)
        return R_CLS(TLSLD_MOVW_DTPREL_G1);
      if (RefKind == AArch64MCExpr::VK_DTPREL_G1_NC)
        return ELF::R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC;
      if (RefKind == AArch64MCExpr::VK_DTPREL_G0)
        return R_CLS(TLSLD_MOVW_DTPREL_G0);
      if (RefKind == AArch64MCExpr::VK_DTPREL_G0_NC)
        return R_CLS(TLSLD_MOVW_DTPREL_G0_NC);
      if (RefKind == AArch64MCExpr::VK_TPREL_G2)
        return ELF::R_AARCH64_TLSLE_MOVW_TPREL_G2;
      if (RefKind == AArch64MCExpr::VK_TPREL_G1)
        return R_CLS(TLSLE_MOVW_TPREL_G1);
      if (RefKind == AArch64MCExpr::VK_TPREL_G1_NC)
        return ELF::R_AARCH64_TLSLE_MOVW_TPREL_G1_NC;
      if (RefKind == AArch64MCExpr::VK_TPREL_G0)
        return R_CLS(TLSLE_MOVW_TPREL_G0);
      if (RefKind == AArch64MCExpr::VK_TPREL_G0_NC)
        return R_CLS(TLSLE_MOVW_TPREL_G0_NC);
      if (RefKind == AArch64MCExpr::VK_GOTTPREL_G1)
        return ELF::R_AARCH64_TLSIE_MOVW_GOTTPREL_G1;
      if (RefKind == AArch64MCExpr::VK_GOTTPREL_G0_NC)
        return ELF::R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC;
      Ctx.reportError(Fixup.getLoc(),
                      "invalid fixup for movz/movk instruction");
      return ELF::R_AARCH64_NONE;
    default:
      Ctx.reportError(Fixup.getLoc(), "Unknown ELF relocation type");
      return ELF::R_AARCH64_NONE;
    }
  }

  llvm_unreachable("Unimplemented fixup -> relocation");
}

bool AArch64ELFObjectWriter::needsRelocateWithSymbol(const MCValue &Val,
                                                     const MCSymbol &,
                                                     unsigned) const {
  return (Val.getRefKind() & AArch64MCExpr::VK_GOT) == AArch64MCExpr::VK_GOT;
}

MCSectionELF *
AArch64ELFObjectWriter::getMemtagRelocsSection(MCContext &Ctx) const {
  return Ctx.getELFSection(".memtag.globals.static",
                           ELF::SHT_AARCH64_MEMTAG_GLOBALS_STATIC, 0);
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createAArch64ELFObjectWriter(uint8_t OSABI, bool IsILP32) {
  return std::make_unique<AArch64ELFObjectWriter>(OSABI, IsILP32);
}
