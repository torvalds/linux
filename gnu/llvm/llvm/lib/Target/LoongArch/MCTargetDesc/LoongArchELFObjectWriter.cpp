//===-- LoongArchELFObjectWriter.cpp - LoongArch ELF Writer ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/LoongArchFixupKinds.h"
#include "MCTargetDesc/LoongArchMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class LoongArchELFObjectWriter : public MCELFObjectTargetWriter {
public:
  LoongArchELFObjectWriter(uint8_t OSABI, bool Is64Bit, bool EnableRelax);

  ~LoongArchELFObjectWriter() override;

  bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                               unsigned Type) const override {
    return EnableRelax;
  }

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
  bool EnableRelax;
};
} // end namespace

LoongArchELFObjectWriter::LoongArchELFObjectWriter(uint8_t OSABI, bool Is64Bit,
                                                   bool EnableRelax)
    : MCELFObjectTargetWriter(Is64Bit, OSABI, ELF::EM_LOONGARCH,
                              /*HasRelocationAddend=*/true),
      EnableRelax(EnableRelax) {}

LoongArchELFObjectWriter::~LoongArchELFObjectWriter() {}

unsigned LoongArchELFObjectWriter::getRelocType(MCContext &Ctx,
                                                const MCValue &Target,
                                                const MCFixup &Fixup,
                                                bool IsPCRel) const {
  // Determine the type of the relocation
  unsigned Kind = Fixup.getTargetKind();

  if (Kind >= FirstLiteralRelocationKind)
    return Kind - FirstLiteralRelocationKind;

  switch (Kind) {
  default:
    Ctx.reportError(Fixup.getLoc(), "Unsupported relocation type");
    return ELF::R_LARCH_NONE;
  case FK_Data_1:
    Ctx.reportError(Fixup.getLoc(), "1-byte data relocations not supported");
    return ELF::R_LARCH_NONE;
  case FK_Data_2:
    Ctx.reportError(Fixup.getLoc(), "2-byte data relocations not supported");
    return ELF::R_LARCH_NONE;
  case FK_Data_4:
    return IsPCRel ? ELF::R_LARCH_32_PCREL : ELF::R_LARCH_32;
  case FK_Data_8:
    return IsPCRel ? ELF::R_LARCH_64_PCREL : ELF::R_LARCH_64;
  case LoongArch::fixup_loongarch_b16:
    return ELF::R_LARCH_B16;
  case LoongArch::fixup_loongarch_b21:
    return ELF::R_LARCH_B21;
  case LoongArch::fixup_loongarch_b26:
    return ELF::R_LARCH_B26;
  case LoongArch::fixup_loongarch_abs_hi20:
    return ELF::R_LARCH_ABS_HI20;
  case LoongArch::fixup_loongarch_abs_lo12:
    return ELF::R_LARCH_ABS_LO12;
  case LoongArch::fixup_loongarch_abs64_lo20:
    return ELF::R_LARCH_ABS64_LO20;
  case LoongArch::fixup_loongarch_abs64_hi12:
    return ELF::R_LARCH_ABS64_HI12;
  case LoongArch::fixup_loongarch_tls_le_hi20:
    return ELF::R_LARCH_TLS_LE_HI20;
  case LoongArch::fixup_loongarch_tls_le_lo12:
    return ELF::R_LARCH_TLS_LE_LO12;
  case LoongArch::fixup_loongarch_tls_le64_lo20:
    return ELF::R_LARCH_TLS_LE64_LO20;
  case LoongArch::fixup_loongarch_tls_le64_hi12:
    return ELF::R_LARCH_TLS_LE64_HI12;
  case LoongArch::fixup_loongarch_call36:
    return ELF::R_LARCH_CALL36;
    // TODO: Handle more fixup-kinds.
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createLoongArchELFObjectWriter(uint8_t OSABI, bool Is64Bit, bool Relax) {
  return std::make_unique<LoongArchELFObjectWriter>(OSABI, Is64Bit, Relax);
}
