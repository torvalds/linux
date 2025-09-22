//===- AMDGPUELFObjectWriter.cpp - AMDGPU ELF Writer ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUFixupKinds.h"
#include "AMDGPUMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

namespace {

class AMDGPUELFObjectWriter : public MCELFObjectTargetWriter {
public:
  AMDGPUELFObjectWriter(bool Is64Bit, uint8_t OSABI, bool HasRelocationAddend);

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};


} // end anonymous namespace

AMDGPUELFObjectWriter::AMDGPUELFObjectWriter(bool Is64Bit, uint8_t OSABI,
                                             bool HasRelocationAddend)
    : MCELFObjectTargetWriter(Is64Bit, OSABI, ELF::EM_AMDGPU,
                              HasRelocationAddend) {}

unsigned AMDGPUELFObjectWriter::getRelocType(MCContext &Ctx,
                                             const MCValue &Target,
                                             const MCFixup &Fixup,
                                             bool IsPCRel) const {
  if (const auto *SymA = Target.getSymA()) {
    // SCRATCH_RSRC_DWORD[01] is a special global variable that represents
    // the scratch buffer.
    if (SymA->getSymbol().getName() == "SCRATCH_RSRC_DWORD0" ||
        SymA->getSymbol().getName() == "SCRATCH_RSRC_DWORD1")
      return ELF::R_AMDGPU_ABS32_LO;
  }

  switch (Target.getAccessVariant()) {
  default:
    break;
  case MCSymbolRefExpr::VK_GOTPCREL:
    return ELF::R_AMDGPU_GOTPCREL;
  case MCSymbolRefExpr::VK_AMDGPU_GOTPCREL32_LO:
    return ELF::R_AMDGPU_GOTPCREL32_LO;
  case MCSymbolRefExpr::VK_AMDGPU_GOTPCREL32_HI:
    return ELF::R_AMDGPU_GOTPCREL32_HI;
  case MCSymbolRefExpr::VK_AMDGPU_REL32_LO:
    return ELF::R_AMDGPU_REL32_LO;
  case MCSymbolRefExpr::VK_AMDGPU_REL32_HI:
    return ELF::R_AMDGPU_REL32_HI;
  case MCSymbolRefExpr::VK_AMDGPU_REL64:
    return ELF::R_AMDGPU_REL64;
  case MCSymbolRefExpr::VK_AMDGPU_ABS32_LO:
    return ELF::R_AMDGPU_ABS32_LO;
  case MCSymbolRefExpr::VK_AMDGPU_ABS32_HI:
    return ELF::R_AMDGPU_ABS32_HI;
  }

  MCFixupKind Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return Kind - FirstLiteralRelocationKind;
  switch (Kind) {
  default: break;
  case FK_PCRel_4:
    return ELF::R_AMDGPU_REL32;
  case FK_Data_4:
  case FK_SecRel_4:
    return IsPCRel ? ELF::R_AMDGPU_REL32 : ELF::R_AMDGPU_ABS32;
  case FK_Data_8:
    return IsPCRel ? ELF::R_AMDGPU_REL64 : ELF::R_AMDGPU_ABS64;
  }

  if (Fixup.getTargetKind() == AMDGPU::fixup_si_sopp_br) {
    const auto *SymA = Target.getSymA();
    assert(SymA);

    if (SymA->getSymbol().isUndefined()) {
      Ctx.reportError(Fixup.getLoc(), Twine("undefined label '") +
                                          SymA->getSymbol().getName() + "'");
      return ELF::R_AMDGPU_NONE;
    }
    return ELF::R_AMDGPU_REL16;
  }

  llvm_unreachable("unhandled relocation type");
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createAMDGPUELFObjectWriter(bool Is64Bit, uint8_t OSABI,
                                  bool HasRelocationAddend) {
  return std::make_unique<AMDGPUELFObjectWriter>(Is64Bit, OSABI,
                                                 HasRelocationAddend);
}
