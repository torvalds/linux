//===- SymbolVisitorCallbacks.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORCALLBACKS_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORCALLBACKS_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {

class SymbolVisitorCallbacks {
  friend class CVSymbolVisitor;

public:
  virtual ~SymbolVisitorCallbacks() = default;

  /// Action to take on unknown symbols. By default, they are ignored.
  virtual Error visitUnknownSymbol(CVSymbol &Record) {
    return Error::success();
  }

  /// Paired begin/end actions for all symbols. Receives all record data,
  /// including the fixed-length record prefix.  visitSymbolBegin() should
  /// return the type of the Symbol, or an error if it cannot be determined.
  virtual Error visitSymbolBegin(CVSymbol &Record, uint32_t Offset) {
    return Error::success();
  }
  virtual Error visitSymbolBegin(CVSymbol &Record) { return Error::success(); }
  virtual Error visitSymbolEnd(CVSymbol &Record) { return Error::success(); }

#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  virtual Error visitKnownRecord(CVSymbol &CVR, Name &Record) {                \
    return Error::success();                                                   \
  }
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORCALLBACKS_H
