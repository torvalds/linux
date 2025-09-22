//===- MCXCOFFObjectStreamer.h - MCStreamer XCOFF Object File Interface ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCXCOFFSTREAMER_H
#define LLVM_MC_MCXCOFFSTREAMER_H

#include "llvm/MC/MCObjectStreamer.h"

namespace llvm {

class MCXCOFFStreamer : public MCObjectStreamer {
public:
  MCXCOFFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                  std::unique_ptr<MCObjectWriter> OW,
                  std::unique_ptr<MCCodeEmitter> Emitter);

  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;
  void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, Align ByteAlignment = Align(1),
                    SMLoc Loc = SMLoc()) override;
  void emitInstToData(const MCInst &Inst, const MCSubtargetInfo &) override;
  void emitXCOFFLocalCommonSymbol(MCSymbol *LabelSym, uint64_t Size,
                                  MCSymbol *CsectSym, Align Alignment) override;
  void emitXCOFFSymbolLinkageWithVisibility(MCSymbol *Symbol,
                                            MCSymbolAttr Linkage,
                                            MCSymbolAttr Visibility) override;
  void emitXCOFFRefDirective(const MCSymbol *Symbol) override;
  void emitXCOFFRenameDirective(const MCSymbol *Name,
                                StringRef Rename) override;
  void emitXCOFFExceptDirective(const MCSymbol *Symbol, const MCSymbol *Trap,
                                unsigned Lang, unsigned Reason,
                                unsigned FunctionSize, bool hasDebug) override;
  void emitXCOFFCInfoSym(StringRef Name, StringRef Metadata) override;
};

} // end namespace llvm

#endif // LLVM_MC_MCXCOFFSTREAMER_H
