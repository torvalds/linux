//===--------- AVRMCELFStreamer.cpp - AVR subclass of MCELFStreamer -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a stub that parses a MCInst bundle and passes the
// instructions on to the real streamer.
//
//===----------------------------------------------------------------------===//
#include "MCTargetDesc/AVRMCELFStreamer.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSymbol.h"

#define DEBUG_TYPE "avrmcelfstreamer"

using namespace llvm;

void AVRMCELFStreamer::emitValueForModiferKind(
    const MCSymbol *Sym, unsigned SizeInBytes, SMLoc Loc,
    AVRMCExpr::VariantKind ModifierKind) {
  MCSymbolRefExpr::VariantKind Kind = MCSymbolRefExpr::VK_AVR_NONE;
  if (ModifierKind == AVRMCExpr::VK_AVR_None) {
    Kind = MCSymbolRefExpr::VK_AVR_DIFF8;
    if (SizeInBytes == SIZE_LONG)
      Kind = MCSymbolRefExpr::VK_AVR_DIFF32;
    else if (SizeInBytes == SIZE_WORD)
      Kind = MCSymbolRefExpr::VK_AVR_DIFF16;
  } else if (ModifierKind == AVRMCExpr::VK_AVR_LO8)
    Kind = MCSymbolRefExpr::VK_AVR_LO8;
  else if (ModifierKind == AVRMCExpr::VK_AVR_HI8)
    Kind = MCSymbolRefExpr::VK_AVR_HI8;
  else if (ModifierKind == AVRMCExpr::VK_AVR_HH8)
    Kind = MCSymbolRefExpr::VK_AVR_HLO8;
  MCELFStreamer::emitValue(MCSymbolRefExpr::create(Sym, Kind, getContext()),
                           SizeInBytes, Loc);
}

namespace llvm {
MCStreamer *createAVRELFStreamer(Triple const &TT, MCContext &Context,
                                 std::unique_ptr<MCAsmBackend> MAB,
                                 std::unique_ptr<MCObjectWriter> OW,
                                 std::unique_ptr<MCCodeEmitter> CE) {
  return new AVRMCELFStreamer(Context, std::move(MAB), std::move(OW),
                              std::move(CE));
}

} // end namespace llvm
