//===- ConstantPools.cpp - ConstantPool class -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ConstantPool and  AssemblerConstantPools classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/ConstantPools.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

//
// ConstantPool implementation
//
// Emit the contents of the constant pool using the provided streamer.
void ConstantPool::emitEntries(MCStreamer &Streamer) {
  if (Entries.empty())
    return;
  Streamer.emitDataRegion(MCDR_DataRegion);
  for (const ConstantPoolEntry &Entry : Entries) {
    Streamer.emitValueToAlignment(Align(Entry.Size)); // align naturally
    Streamer.emitLabel(Entry.Label);
    Streamer.emitValue(Entry.Value, Entry.Size, Entry.Loc);
  }
  Streamer.emitDataRegion(MCDR_DataRegionEnd);
  Entries.clear();
}

const MCExpr *ConstantPool::addEntry(const MCExpr *Value, MCContext &Context,
                                     unsigned Size, SMLoc Loc) {
  const MCConstantExpr *C = dyn_cast<MCConstantExpr>(Value);
  const MCSymbolRefExpr *S = dyn_cast<MCSymbolRefExpr>(Value);

  // Check if there is existing entry for the same constant. If so, reuse it.
  if (C) {
    auto CItr = CachedConstantEntries.find(std::make_pair(C->getValue(), Size));
    if (CItr != CachedConstantEntries.end())
      return CItr->second;
  }

  // Check if there is existing entry for the same symbol. If so, reuse it.
  if (S) {
    auto SItr =
        CachedSymbolEntries.find(std::make_pair(&(S->getSymbol()), Size));
    if (SItr != CachedSymbolEntries.end())
      return SItr->second;
  }

  MCSymbol *CPEntryLabel = Context.createTempSymbol();

  Entries.push_back(ConstantPoolEntry(CPEntryLabel, Value, Size, Loc));
  const auto SymRef = MCSymbolRefExpr::create(CPEntryLabel, Context);
  if (C)
    CachedConstantEntries[std::make_pair(C->getValue(), Size)] = SymRef;
  if (S)
    CachedSymbolEntries[std::make_pair(&(S->getSymbol()), Size)] = SymRef;
  return SymRef;
}

bool ConstantPool::empty() { return Entries.empty(); }

void ConstantPool::clearCache() {
  CachedConstantEntries.clear();
  CachedSymbolEntries.clear();
}

//
// AssemblerConstantPools implementation
//
ConstantPool *AssemblerConstantPools::getConstantPool(MCSection *Section) {
  ConstantPoolMapTy::iterator CP = ConstantPools.find(Section);
  if (CP == ConstantPools.end())
    return nullptr;

  return &CP->second;
}

ConstantPool &
AssemblerConstantPools::getOrCreateConstantPool(MCSection *Section) {
  return ConstantPools[Section];
}

static void emitConstantPool(MCStreamer &Streamer, MCSection *Section,
                             ConstantPool &CP) {
  if (!CP.empty()) {
    Streamer.switchSection(Section);
    CP.emitEntries(Streamer);
  }
}

void AssemblerConstantPools::emitAll(MCStreamer &Streamer) {
  // Dump contents of assembler constant pools.
  for (auto &CPI : ConstantPools) {
    MCSection *Section = CPI.first;
    ConstantPool &CP = CPI.second;

    emitConstantPool(Streamer, Section, CP);
  }
}

void AssemblerConstantPools::emitForCurrentSection(MCStreamer &Streamer) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  if (ConstantPool *CP = getConstantPool(Section))
    emitConstantPool(Streamer, Section, *CP);
}

void AssemblerConstantPools::clearCacheForCurrentSection(MCStreamer &Streamer) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  if (ConstantPool *CP = getConstantPool(Section))
    CP->clearCache();
}

const MCExpr *AssemblerConstantPools::addEntry(MCStreamer &Streamer,
                                               const MCExpr *Expr,
                                               unsigned Size, SMLoc Loc) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  return getOrCreateConstantPool(Section).addEntry(Expr, Streamer.getContext(),
                                                   Size, Loc);
}
