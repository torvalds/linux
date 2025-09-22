//===-- LanaiELFObjectWriter.cpp - Lanai ELF Writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/LanaiBaseInfo.h"
#include "MCTargetDesc/LanaiFixupKinds.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class LanaiELFObjectWriter : public MCELFObjectTargetWriter {
public:
  explicit LanaiELFObjectWriter(uint8_t OSABI);

  ~LanaiELFObjectWriter() override = default;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
  bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                               unsigned Type) const override;
};

} // end anonymous namespace

LanaiELFObjectWriter::LanaiELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(/*Is64Bit_=*/false, OSABI, ELF::EM_LANAI,
                              /*HasRelocationAddend_=*/true) {}

unsigned LanaiELFObjectWriter::getRelocType(MCContext & /*Ctx*/,
                                            const MCValue & /*Target*/,
                                            const MCFixup &Fixup,
                                            bool /*IsPCRel*/) const {
  unsigned Type;
  unsigned Kind = static_cast<unsigned>(Fixup.getKind());
  switch (Kind) {
  case Lanai::FIXUP_LANAI_21:
    Type = ELF::R_LANAI_21;
    break;
  case Lanai::FIXUP_LANAI_21_F:
    Type = ELF::R_LANAI_21_F;
    break;
  case Lanai::FIXUP_LANAI_25:
    Type = ELF::R_LANAI_25;
    break;
  case Lanai::FIXUP_LANAI_32:
  case FK_Data_4:
    Type = ELF::R_LANAI_32;
    break;
  case Lanai::FIXUP_LANAI_HI16:
    Type = ELF::R_LANAI_HI16;
    break;
  case Lanai::FIXUP_LANAI_LO16:
    Type = ELF::R_LANAI_LO16;
    break;
  case Lanai::FIXUP_LANAI_NONE:
    Type = ELF::R_LANAI_NONE;
    break;

  default:
    llvm_unreachable("Invalid fixup kind!");
  }
  return Type;
}

bool LanaiELFObjectWriter::needsRelocateWithSymbol(const MCValue &,
                                                   const MCSymbol &,
                                                   unsigned Type) const {
  switch (Type) {
  case ELF::R_LANAI_21:
  case ELF::R_LANAI_21_F:
  case ELF::R_LANAI_25:
  case ELF::R_LANAI_32:
  case ELF::R_LANAI_HI16:
    return true;
  default:
    return false;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createLanaiELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<LanaiELFObjectWriter>(OSABI);
}
