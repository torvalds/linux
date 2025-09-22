//===-- CSKYTargetStreamer.h - CSKY Target Streamer ----------*- C++ -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CSKYTargetStreamer.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

//
// ConstantPool implementation
//
// Emit the contents of the constant pool using the provided streamer.
void CSKYConstantPool::emitAll(MCStreamer &Streamer) {
  if (Entries.empty())
    return;

  if (CurrentSection != nullptr)
    Streamer.switchSection(CurrentSection);

  Streamer.emitDataRegion(MCDR_DataRegion);
  for (const ConstantPoolEntry &Entry : Entries) {
    Streamer.emitCodeAlignment(
        Align(Entry.Size),
        Streamer.getContext().getSubtargetInfo()); // align naturally
    Streamer.emitLabel(Entry.Label);
    Streamer.emitValue(Entry.Value, Entry.Size, Entry.Loc);
  }
  Streamer.emitDataRegion(MCDR_DataRegionEnd);
  Entries.clear();
}

const MCExpr *CSKYConstantPool::addEntry(MCStreamer &Streamer,
                                         const MCExpr *Value, unsigned Size,
                                         SMLoc Loc, const MCExpr *AdjustExpr) {
  if (CurrentSection == nullptr)
    CurrentSection = Streamer.getCurrentSectionOnly();

  auto &Context = Streamer.getContext();

  const MCConstantExpr *C = dyn_cast<MCConstantExpr>(Value);

  // Check if there is existing entry for the same constant. If so, reuse it.
  auto Itr = C ? CachedEntries.find(C->getValue()) : CachedEntries.end();
  if (Itr != CachedEntries.end())
    return Itr->second;

  MCSymbol *CPEntryLabel = Context.createTempSymbol();
  const auto SymRef = MCSymbolRefExpr::create(CPEntryLabel, Context);

  if (AdjustExpr) {
    const CSKYMCExpr *CSKYExpr = cast<CSKYMCExpr>(Value);

    Value = MCBinaryExpr::createSub(AdjustExpr, SymRef, Context);
    Value = MCBinaryExpr::createSub(CSKYExpr->getSubExpr(), Value, Context);
    Value = CSKYMCExpr::create(Value, CSKYExpr->getKind(), Context);
  }

  Entries.push_back(ConstantPoolEntry(CPEntryLabel, Value, Size, Loc));

  if (C)
    CachedEntries[C->getValue()] = SymRef;
  return SymRef;
}

bool CSKYConstantPool::empty() { return Entries.empty(); }

void CSKYConstantPool::clearCache() {
  CurrentSection = nullptr;
  CachedEntries.clear();
}

CSKYTargetStreamer::CSKYTargetStreamer(MCStreamer &S)
    : MCTargetStreamer(S), ConstantPool(new CSKYConstantPool()) {}

const MCExpr *
CSKYTargetStreamer::addConstantPoolEntry(const MCExpr *Expr, SMLoc Loc,
                                         const MCExpr *AdjustExpr) {
  auto ELFRefKind = CSKYMCExpr::VK_CSKY_Invalid;
  ConstantCounter++;

  const MCExpr *OrigExpr = Expr;

  if (const CSKYMCExpr *CE = dyn_cast<CSKYMCExpr>(Expr)) {
    Expr = CE->getSubExpr();
    ELFRefKind = CE->getKind();
  }

  if (const MCSymbolRefExpr *SymExpr = dyn_cast<MCSymbolRefExpr>(Expr)) {
    const MCSymbol *Sym = &SymExpr->getSymbol();

    SymbolIndex Index = {Sym, ELFRefKind};

    if (ConstantMap.find(Index) == ConstantMap.end()) {
      ConstantMap[Index] =
          ConstantPool->addEntry(getStreamer(), OrigExpr, 4, Loc, AdjustExpr);
    }
    return ConstantMap[Index];
  }

  return ConstantPool->addEntry(getStreamer(), Expr, 4, Loc, AdjustExpr);
}

void CSKYTargetStreamer::emitCurrentConstantPool() {
  ConstantPool->emitAll(Streamer);
  ConstantPool->clearCache();
}

// finish() - write out any non-empty assembler constant pools.
void CSKYTargetStreamer::finish() {
  if (ConstantCounter != 0) {
    ConstantPool->emitAll(Streamer);
  }

  finishAttributeSection();
}

void CSKYTargetStreamer::emitTargetAttributes(const MCSubtargetInfo &STI) {}

void CSKYTargetStreamer::emitAttribute(unsigned Attribute, unsigned Value) {}
void CSKYTargetStreamer::emitTextAttribute(unsigned Attribute,
                                           StringRef String) {}
void CSKYTargetStreamer::finishAttributeSection() {}

void CSKYTargetAsmStreamer::emitAttribute(unsigned Attribute, unsigned Value) {
  OS << "\t.csky_attribute\t" << Attribute << ", " << Twine(Value) << "\n";
}

void CSKYTargetAsmStreamer::emitTextAttribute(unsigned Attribute,
                                              StringRef String) {
  OS << "\t.csky_attribute\t" << Attribute << ", \"" << String << "\"\n";
}

void CSKYTargetAsmStreamer::finishAttributeSection() {}
