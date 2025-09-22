//===- SymbolRecordHelpers.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORDHELPERS_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORDHELPERS_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"

namespace llvm {
namespace codeview {
/// Return true if this symbol opens a scope. This implies that the symbol has
/// "parent" and "end" fields, which contain the offset of the S_END or
/// S_INLINESITE_END record.
inline bool symbolOpensScope(SymbolKind Kind) {
  switch (Kind) {
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32:
  case SymbolKind::S_LPROC32_ID:
  case SymbolKind::S_GPROC32_ID:
  case SymbolKind::S_BLOCK32:
  case SymbolKind::S_SEPCODE:
  case SymbolKind::S_THUNK32:
  case SymbolKind::S_INLINESITE:
  case SymbolKind::S_INLINESITE2:
    return true;
  default:
    break;
  }
  return false;
}

/// Return true if this ssymbol ends a scope.
inline bool symbolEndsScope(SymbolKind Kind) {
  switch (Kind) {
  case SymbolKind::S_END:
  case SymbolKind::S_PROC_ID_END:
  case SymbolKind::S_INLINESITE_END:
    return true;
  default:
    break;
  }
  return false;
}

/// Given a symbol P for which symbolOpensScope(P) == true, return the
/// corresponding end offset.
uint32_t getScopeEndOffset(const CVSymbol &Symbol);
uint32_t getScopeParentOffset(const CVSymbol &Symbol);

CVSymbolArray limitSymbolArrayToScope(const CVSymbolArray &Symbols,
                                      uint32_t ScopeBegin);

} // namespace codeview
} // namespace llvm

#endif
