//===-- PPCXCOFFObjectWriter.cpp - PowerPC XCOFF Writer -------------------===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCFixupKinds.h"
#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCXCOFFObjectWriter.h"

using namespace llvm;

namespace {
class PPCXCOFFObjectWriter : public MCXCOFFObjectTargetWriter {
  static constexpr uint8_t SignBitMask = 0x80;

public:
  PPCXCOFFObjectWriter(bool Is64Bit);

  std::pair<uint8_t, uint8_t>
  getRelocTypeAndSignSize(const MCValue &Target, const MCFixup &Fixup,
                          bool IsPCRel) const override;
};
} // end anonymous namespace

PPCXCOFFObjectWriter::PPCXCOFFObjectWriter(bool Is64Bit)
    : MCXCOFFObjectTargetWriter(Is64Bit) {}

std::unique_ptr<MCObjectTargetWriter>
llvm::createPPCXCOFFObjectWriter(bool Is64Bit) {
  return std::make_unique<PPCXCOFFObjectWriter>(Is64Bit);
}

std::pair<uint8_t, uint8_t> PPCXCOFFObjectWriter::getRelocTypeAndSignSize(
    const MCValue &Target, const MCFixup &Fixup, bool IsPCRel) const {
  const MCSymbolRefExpr::VariantKind Modifier =
      Target.isAbsolute() ? MCSymbolRefExpr::VK_None
                          : Target.getSymA()->getKind();
  // People from AIX OS team says AIX link editor does not care about
  // the sign bit in the relocation entry "most" of the time.
  // The system assembler seems to set the sign bit on relocation entry
  // based on similar property of IsPCRel. So we will do the same here.
  // TODO: More investigation on how assembler decides to set the sign
  // bit, and we might want to match that.
  const uint8_t EncodedSignednessIndicator = IsPCRel ? SignBitMask : 0u;

  // The magic number we use in SignAndSize has a strong relationship with
  // the corresponding MCFixupKind. In most cases, it's the MCFixupKind
  // number - 1, because SignAndSize encodes the bit length being
  // relocated minus 1.
  switch ((unsigned)Fixup.getKind()) {
  default:
    report_fatal_error("Unimplemented fixup kind.");
  case PPC::fixup_ppc_half16: {
    const uint8_t SignAndSizeForHalf16 = EncodedSignednessIndicator | 15;
    switch (Modifier) {
    default:
      report_fatal_error("Unsupported modifier for half16 fixup.");
    case MCSymbolRefExpr::VK_None:
      return {XCOFF::RelocationType::R_TOC, SignAndSizeForHalf16};
    case MCSymbolRefExpr::VK_PPC_U:
      return {XCOFF::RelocationType::R_TOCU, SignAndSizeForHalf16};
    case MCSymbolRefExpr::VK_PPC_L:
      return {XCOFF::RelocationType::R_TOCL, SignAndSizeForHalf16};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSLE:
      return {XCOFF::RelocationType::R_TLS_LE, SignAndSizeForHalf16};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSLD:
      return {XCOFF::RelocationType::R_TLS_LD, SignAndSizeForHalf16};
    }
  } break;
  case PPC::fixup_ppc_half16ds:
  case PPC::fixup_ppc_half16dq: {
    if (IsPCRel)
      report_fatal_error("Invalid PC-relative relocation.");
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return {XCOFF::RelocationType::R_TOC, 15};
    case MCSymbolRefExpr::VK_PPC_L:
      return {XCOFF::RelocationType::R_TOCL, 15};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSLE:
      return {XCOFF::RelocationType::R_TLS_LE, 15};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSLD:
      return {XCOFF::RelocationType::R_TLS_LD, 15};
    }
  } break;
  case PPC::fixup_ppc_br24:
    // Branches are 4 byte aligned, so the 24 bits we encode in
    // the instruction actually represents a 26 bit offset.
    return {XCOFF::RelocationType::R_RBR, EncodedSignednessIndicator | 25};
  case PPC::fixup_ppc_br24abs:
    return {XCOFF::RelocationType::R_RBA, EncodedSignednessIndicator | 25};
  case PPC::fixup_ppc_nofixup: {
    if (Modifier == MCSymbolRefExpr::VK_None)
      return {XCOFF::RelocationType::R_REF, 0};
    else
      llvm_unreachable("Unsupported Modifier");
  } break;
  case FK_Data_4:
  case FK_Data_8:
    const uint8_t SignAndSizeForFKData =
        EncodedSignednessIndicator |
        ((unsigned)Fixup.getKind() == FK_Data_4 ? 31 : 63);
    switch (Modifier) {
    default:
      report_fatal_error("Unsupported modifier");
    case MCSymbolRefExpr::VK_PPC_AIX_TLSGD:
      return {XCOFF::RelocationType::R_TLS, SignAndSizeForFKData};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSGDM:
      return {XCOFF::RelocationType::R_TLSM, SignAndSizeForFKData};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSIE:
      return {XCOFF::RelocationType::R_TLS_IE, SignAndSizeForFKData};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSLE:
      return {XCOFF::RelocationType::R_TLS_LE, SignAndSizeForFKData};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSLD:
      return {XCOFF::RelocationType::R_TLS_LD, SignAndSizeForFKData};
    case MCSymbolRefExpr::VK_PPC_AIX_TLSML:
      return {XCOFF::RelocationType::R_TLSML, SignAndSizeForFKData};
    case MCSymbolRefExpr::VK_None:
      return {XCOFF::RelocationType::R_POS, SignAndSizeForFKData};
    }
  }
}
