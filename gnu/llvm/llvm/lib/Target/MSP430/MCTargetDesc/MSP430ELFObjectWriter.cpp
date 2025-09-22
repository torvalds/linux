//===-- MSP430ELFObjectWriter.cpp - MSP430 ELF Writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSP430FixupKinds.h"
#include "MCTargetDesc/MSP430MCTargetDesc.h"

#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class MSP430ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  MSP430ELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(false, OSABI, ELF::EM_MSP430,
                              /*HasRelocationAddend*/ true) {}

  ~MSP430ELFObjectWriter() override = default;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override {
    // Translate fixup kind to ELF relocation type.
    switch (Fixup.getTargetKind()) {
    case FK_Data_1:                   return ELF::R_MSP430_8;
    case FK_Data_2:                   return ELF::R_MSP430_16_BYTE;
    case FK_Data_4:                   return ELF::R_MSP430_32;
    case MSP430::fixup_32:            return ELF::R_MSP430_32;
    case MSP430::fixup_10_pcrel:      return ELF::R_MSP430_10_PCREL;
    case MSP430::fixup_16:            return ELF::R_MSP430_16;
    case MSP430::fixup_16_pcrel:      return ELF::R_MSP430_16_PCREL;
    case MSP430::fixup_16_byte:       return ELF::R_MSP430_16_BYTE;
    case MSP430::fixup_16_pcrel_byte: return ELF::R_MSP430_16_PCREL_BYTE;
    case MSP430::fixup_2x_pcrel:      return ELF::R_MSP430_2X_PCREL;
    case MSP430::fixup_rl_pcrel:      return ELF::R_MSP430_RL_PCREL;
    case MSP430::fixup_8:             return ELF::R_MSP430_8;
    case MSP430::fixup_sym_diff:      return ELF::R_MSP430_SYM_DIFF;
    default:
      llvm_unreachable("Invalid fixup kind");
    }
  }
};
} // end of anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createMSP430ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<MSP430ELFObjectWriter>(OSABI);
}
