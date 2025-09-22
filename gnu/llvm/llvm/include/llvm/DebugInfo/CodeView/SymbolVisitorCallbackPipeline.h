//===- SymbolVisitorCallbackPipeline.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORCALLBACKPIPELINE_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORCALLBACKPIPELINE_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace llvm {
namespace codeview {

class SymbolVisitorCallbackPipeline : public SymbolVisitorCallbacks {
public:
  SymbolVisitorCallbackPipeline() = default;

  Error visitUnknownSymbol(CVSymbol &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitUnknownSymbol(Record))
        return EC;
    }
    return Error::success();
  }

  Error visitSymbolBegin(CVSymbol &Record, uint32_t Offset) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitSymbolBegin(Record, Offset))
        return EC;
    }
    return Error::success();
  }

  Error visitSymbolBegin(CVSymbol &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitSymbolBegin(Record))
        return EC;
    }
    return Error::success();
  }

  Error visitSymbolEnd(CVSymbol &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitSymbolEnd(Record))
        return EC;
    }
    return Error::success();
  }

  void addCallbackToPipeline(SymbolVisitorCallbacks &Callbacks) {
    Pipeline.push_back(&Callbacks);
  }

#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownRecord(CVSymbol &CVR, Name &Record) override {               \
    for (auto Visitor : Pipeline) {                                            \
      if (auto EC = Visitor->visitKnownRecord(CVR, Record))                    \
        return EC;                                                             \
    }                                                                          \
    return Error::success();                                                   \
  }
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"

private:
  std::vector<SymbolVisitorCallbacks *> Pipeline;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORCALLBACKPIPELINE_H
