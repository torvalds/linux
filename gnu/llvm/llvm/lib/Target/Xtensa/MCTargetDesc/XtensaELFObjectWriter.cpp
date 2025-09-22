//===-- XtensaMCObjectWriter.cpp - Xtensa ELF writer ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/XtensaMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {
class XtensaObjectWriter : public MCELFObjectTargetWriter {
public:
  XtensaObjectWriter(uint8_t OSABI);

  virtual ~XtensaObjectWriter();

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
  bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                               unsigned Type) const override;
};
} // namespace

XtensaObjectWriter::XtensaObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(false, OSABI, ELF::EM_XTENSA,
                              /*HasRelocationAddend=*/true) {}

XtensaObjectWriter::~XtensaObjectWriter() {}

unsigned XtensaObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                          const MCFixup &Fixup,
                                          bool IsPCRel) const {

  switch ((unsigned)Fixup.getKind()) {
  case FK_Data_4:
    return ELF::R_XTENSA_32;
  default:
    return ELF::R_XTENSA_SLOT0_OP;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createXtensaObjectWriter(uint8_t OSABI, bool IsLittleEndian) {
  return std::make_unique<XtensaObjectWriter>(OSABI);
}

bool XtensaObjectWriter::needsRelocateWithSymbol(const MCValue &,
                                                 const MCSymbol &,
                                                 unsigned Type) const {
  return false;
}
