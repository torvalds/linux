//===-- MCELFObjectTargetWriter.cpp - ELF Target Writer Subclass ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCELFObjectWriter.h"

using namespace llvm;

MCELFObjectTargetWriter::MCELFObjectTargetWriter(bool Is64Bit_, uint8_t OSABI_,
                                                 uint16_t EMachine_,
                                                 bool HasRelocationAddend_,
                                                 uint8_t ABIVersion_)
    : OSABI(OSABI_), ABIVersion(ABIVersion_), EMachine(EMachine_),
      HasRelocationAddend(HasRelocationAddend_), Is64Bit(Is64Bit_) {}

bool MCELFObjectTargetWriter::needsRelocateWithSymbol(const MCValue &,
                                                      const MCSymbol &,
                                                      unsigned Type) const {
  return false;
}

void
MCELFObjectTargetWriter::sortRelocs(const MCAssembler &Asm,
                                    std::vector<ELFRelocationEntry> &Relocs) {
}

void MCELFObjectTargetWriter::addTargetSectionFlags(MCContext &Ctx,
                                                    MCSectionELF &Sec) {}
