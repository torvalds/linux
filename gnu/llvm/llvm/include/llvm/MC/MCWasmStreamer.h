//===- MCWasmStreamer.h - MCStreamer Wasm Object File Interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWASMSTREAMER_H
#define LLVM_MC_MCWASMSTREAMER_H

#include "MCAsmBackend.h"
#include "MCCodeEmitter.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
class MCExpr;
class MCInst;

class MCWasmStreamer : public MCObjectStreamer {
public:
  MCWasmStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                 std::unique_ptr<MCObjectWriter> OW,
                 std::unique_ptr<MCCodeEmitter> Emitter)
      : MCObjectStreamer(Context, std::move(TAB), std::move(OW),
                         std::move(Emitter)),
        SeenIdent(false) {}

  ~MCWasmStreamer() override;

  /// state management
  void reset() override {
    SeenIdent = false;
    MCObjectStreamer::reset();
  }

  /// \name MCStreamer Interface
  /// @{

  void changeSection(MCSection *Section, uint32_t Subsection) override;
  void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  void emitLabelAtPos(MCSymbol *Symbol, SMLoc Loc, MCDataFragment &F,
                      uint64_t Offset) override;
  void emitAssemblerFlag(MCAssemblerFlag Flag) override;
  void emitThumbFunc(MCSymbol *Func) override;
  void emitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) override;
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  void emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) override;
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;

  void emitELFSize(MCSymbol *Symbol, const MCExpr *Value) override;

  void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             Align ByteAlignment) override;

  void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, Align ByteAlignment = Align(1),
                    SMLoc Loc = SMLoc()) override;
  void emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol, uint64_t Size,
                      Align ByteAlignment = Align(1)) override;

  void emitIdent(StringRef IdentString) override;

  void finishImpl() override;

private:
  void emitInstToFragment(const MCInst &Inst, const MCSubtargetInfo &) override;
  void emitInstToData(const MCInst &Inst, const MCSubtargetInfo &) override;

  void fixSymbolsInTLSFixups(const MCExpr *expr);

  bool SeenIdent;
};

} // end namespace llvm

#endif
