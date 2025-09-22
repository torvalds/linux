//===-- SymbolDumper.h - CodeView symbol info dumper ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPER_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPER_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolDumpDelegate.h"
#include "llvm/Support/Error.h"

#include <memory>
#include <utility>

namespace llvm {
class ScopedPrinter;

namespace codeview {
class TypeCollection;

/// Dumper for CodeView symbol streams found in COFF object files and PDB files.
class CVSymbolDumper {
public:
  CVSymbolDumper(ScopedPrinter &W, TypeCollection &Types,
                 CodeViewContainer Container,
                 std::unique_ptr<SymbolDumpDelegate> ObjDelegate, CPUType CPU,
                 bool PrintRecordBytes)
      : W(W), Types(Types), Container(Container),
        ObjDelegate(std::move(ObjDelegate)), CompilationCPUType(CPU),
        PrintRecordBytes(PrintRecordBytes) {}

  /// Dumps one type record.  Returns false if there was a type parsing error,
  /// and true otherwise.  This should be called in order, since the dumper
  /// maintains state about previous records which are necessary for cross
  /// type references.
  Error dump(CVRecord<SymbolKind> &Record);

  /// Dumps the type records in Data. Returns false if there was a type stream
  /// parse error, and true otherwise.
  Error dump(const CVSymbolArray &Symbols);

  CPUType getCompilationCPUType() const { return CompilationCPUType; }

private:
  ScopedPrinter &W;
  TypeCollection &Types;
  CodeViewContainer Container;
  std::unique_ptr<SymbolDumpDelegate> ObjDelegate;
  CPUType CompilationCPUType;
  bool PrintRecordBytes;
};
} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPER_H
