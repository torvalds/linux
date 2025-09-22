//===- MCDXContainerStreamer.h - MCDXContainerStreamer Interface ---*- C++ ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Overrides MCObjectStreamer to disable all unnecessary features with stubs.
// The DXContainer format isn't a fully featured object format. It doesn't
// support symbols, and initially it will not support instruction data since it
// is used as a bitcode container for DXIL.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDXCONTAINERSTREAMER_H
#define LLVM_MC_MCDXCONTAINERSTREAMER_H

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"

namespace llvm {
class MCInst;
class raw_ostream;

class MCDXContainerStreamer : public MCObjectStreamer {
public:
  MCDXContainerStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                        std::unique_ptr<MCObjectWriter> OW,
                        std::unique_ptr<MCCodeEmitter> Emitter)
      : MCObjectStreamer(Context, std::move(TAB), std::move(OW),
                         std::move(Emitter)) {}

  bool emitSymbolAttribute(MCSymbol *, MCSymbolAttr) override { return false; }
  void emitCommonSymbol(MCSymbol *, uint64_t, Align) override {}
  void emitZerofill(MCSection *, MCSymbol *Symbol = nullptr, uint64_t Size = 0,
                    Align ByteAlignment = Align(1),
                    SMLoc Loc = SMLoc()) override {}

private:
  void emitInstToData(const MCInst &, const MCSubtargetInfo &) override;
};

} // end namespace llvm

#endif // LLVM_MC_MCDXCONTAINERSTREAMER_H
