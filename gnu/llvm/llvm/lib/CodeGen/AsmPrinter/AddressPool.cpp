//===- llvm/CodeGen/AddressPool.cpp - Dwarf Debug Framework ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AddressPool.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <utility>

using namespace llvm;

unsigned AddressPool::getIndex(const MCSymbol *Sym, bool TLS) {
  resetUsedFlag(true);
  auto IterBool =
      Pool.insert(std::make_pair(Sym, AddressPoolEntry(Pool.size(), TLS)));
  return IterBool.first->second.Number;
}

MCSymbol *AddressPool::emitHeader(AsmPrinter &Asm, MCSection *Section) {
  static const uint8_t AddrSize = Asm.MAI->getCodePointerSize();

  MCSymbol *EndLabel =
      Asm.emitDwarfUnitLength("debug_addr", "Length of contribution");
  Asm.OutStreamer->AddComment("DWARF version number");
  Asm.emitInt16(Asm.getDwarfVersion());
  Asm.OutStreamer->AddComment("Address size");
  Asm.emitInt8(AddrSize);
  Asm.OutStreamer->AddComment("Segment selector size");
  Asm.emitInt8(0); // TODO: Support non-zero segment_selector_size.

  return EndLabel;
}

// Emit addresses into the section given.
void AddressPool::emit(AsmPrinter &Asm, MCSection *AddrSection) {
  if (isEmpty())
    return;

  // Start the dwarf addr section.
  Asm.OutStreamer->switchSection(AddrSection);

  MCSymbol *EndLabel = nullptr;

  if (Asm.getDwarfVersion() >= 5)
    EndLabel = emitHeader(Asm, AddrSection);

  // Define the symbol that marks the start of the contribution.
  // It is referenced via DW_AT_addr_base.
  Asm.OutStreamer->emitLabel(AddressTableBaseSym);

  // Order the address pool entries by ID
  SmallVector<const MCExpr *, 64> Entries(Pool.size());

  for (const auto &I : Pool)
    Entries[I.second.Number] =
        I.second.TLS
            ? Asm.getObjFileLowering().getDebugThreadLocalSymbol(I.first)
            : MCSymbolRefExpr::create(I.first, Asm.OutContext);

  for (const MCExpr *Entry : Entries)
    Asm.OutStreamer->emitValue(Entry, Asm.MAI->getCodePointerSize());

  if (EndLabel)
    Asm.OutStreamer->emitLabel(EndLabel);
}
