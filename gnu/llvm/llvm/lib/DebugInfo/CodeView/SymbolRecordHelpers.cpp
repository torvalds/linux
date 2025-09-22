//===- SymbolRecordHelpers.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/SymbolRecordHelpers.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"

using namespace llvm;
using namespace llvm::codeview;

template <typename RecordT> static RecordT createRecord(const CVSymbol &sym) {
  RecordT record(static_cast<SymbolRecordKind>(sym.kind()));
  cantFail(SymbolDeserializer::deserializeAs<RecordT>(sym, record));
  return record;
}

uint32_t llvm::codeview::getScopeEndOffset(const CVSymbol &Sym) {
  assert(symbolOpensScope(Sym.kind()));
  switch (Sym.kind()) {
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32:
  case SymbolKind::S_GPROC32_ID:
  case SymbolKind::S_LPROC32_ID:
  case SymbolKind::S_LPROC32_DPC:
  case SymbolKind::S_LPROC32_DPC_ID: {
    ProcSym Proc = createRecord<ProcSym>(Sym);
    return Proc.End;
  }
  case SymbolKind::S_BLOCK32: {
    BlockSym Block = createRecord<BlockSym>(Sym);
    return Block.End;
  }
  case SymbolKind::S_THUNK32: {
    Thunk32Sym Thunk = createRecord<Thunk32Sym>(Sym);
    return Thunk.End;
  }
  case SymbolKind::S_INLINESITE: {
    InlineSiteSym Site = createRecord<InlineSiteSym>(Sym);
    return Site.End;
  }
  default:
    assert(false && "Unknown record type");
    return 0;
  }
}

uint32_t
llvm::codeview::getScopeParentOffset(const llvm::codeview::CVSymbol &Sym) {
  assert(symbolOpensScope(Sym.kind()));
  switch (Sym.kind()) {
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32:
  case SymbolKind::S_GPROC32_ID:
  case SymbolKind::S_LPROC32_ID:
  case SymbolKind::S_LPROC32_DPC:
  case SymbolKind::S_LPROC32_DPC_ID: {
    ProcSym Proc = createRecord<ProcSym>(Sym);
    return Proc.Parent;
  }
  case SymbolKind::S_BLOCK32: {
    BlockSym Block = createRecord<BlockSym>(Sym);
    return Block.Parent;
  }
  case SymbolKind::S_THUNK32: {
    Thunk32Sym Thunk = createRecord<Thunk32Sym>(Sym);
    return Thunk.Parent;
  }
  case SymbolKind::S_INLINESITE: {
    InlineSiteSym Site = createRecord<InlineSiteSym>(Sym);
    return Site.Parent;
  }
  default:
    assert(false && "Unknown record type");
    return 0;
  }
}

CVSymbolArray
llvm::codeview::limitSymbolArrayToScope(const CVSymbolArray &Symbols,
                                        uint32_t ScopeBegin) {
  CVSymbol Opener = *Symbols.at(ScopeBegin);
  assert(symbolOpensScope(Opener.kind()));
  uint32_t EndOffset = getScopeEndOffset(Opener);
  CVSymbol Closer = *Symbols.at(EndOffset);
  EndOffset += Closer.RecordData.size();
  return Symbols.substream(ScopeBegin, EndOffset);
}
