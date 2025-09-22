//===- MCGOFFStreamer.h - MCStreamer GOFF Object File Interface--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCGOFFSTREAMER_H
#define LLVM_MC_MCGOFFSTREAMER_H

#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"

namespace llvm {

class MCGOFFStreamer : public MCObjectStreamer {
public:
  MCGOFFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                 std::unique_ptr<MCObjectWriter> OW,
                 std::unique_ptr<MCCodeEmitter> Emitter)
      : MCObjectStreamer(Context, std::move(MAB), std::move(OW),
                         std::move(Emitter)) {}

  ~MCGOFFStreamer() override;

  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override {
    return false;
  }
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override {}
  void emitInstToData(const MCInst &Inst, const MCSubtargetInfo &) override {}
  void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, Align ByteAlignment = Align(1),
                    SMLoc Loc = SMLoc()) override {}
};

} // end namespace llvm

#endif
