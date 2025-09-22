//===- lib/MC/MCNullStreamer.cpp - Dummy Streamer Implementation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/SMLoc.h"
namespace llvm {
class MCContext;
class MCExpr;
class MCSection;
class MCSymbol;
} // namespace llvm

using namespace llvm;

namespace {

  class MCNullStreamer : public MCStreamer {
  public:
    MCNullStreamer(MCContext &Context) : MCStreamer(Context) {}

    /// @name MCStreamer Interface
    /// @{

    bool hasRawTextSupport() const override { return true; }
    void emitRawTextImpl(StringRef String) override {}

    bool emitSymbolAttribute(MCSymbol *Symbol,
                             MCSymbolAttr Attribute) override {
      return true;
    }

    void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                          Align ByteAlignment) override {}
    void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                      uint64_t Size = 0, Align ByteAlignment = Align(1),
                      SMLoc Loc = SMLoc()) override {}
    void emitGPRel32Value(const MCExpr *Value) override {}
    void beginCOFFSymbolDef(const MCSymbol *Symbol) override {}
    void emitCOFFSymbolStorageClass(int StorageClass) override {}
    void emitCOFFSymbolType(int Type) override {}
    void endCOFFSymbolDef() override {}
    void
    emitXCOFFSymbolLinkageWithVisibility(MCSymbol *Symbol, MCSymbolAttr Linkage,
                                         MCSymbolAttr Visibility) override {}
  };

}

MCStreamer *llvm::createNullStreamer(MCContext &Context) {
  return new MCNullStreamer(Context);
}
