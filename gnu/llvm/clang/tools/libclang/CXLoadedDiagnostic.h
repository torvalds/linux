/*===-- CXLoadedDiagnostic.h - Handling of persisent diags ------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* Implements handling of persisent diagnostics.                              *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CXLOADEDDIAGNOSTIC_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CXLOADEDDIAGNOSTIC_H

#include "CIndexDiagnostic.h"
#include "llvm/ADT/StringRef.h"
#include "clang/Basic/LLVM.h"
#include <vector>

namespace clang {
class CXLoadedDiagnostic : public CXDiagnosticImpl {
public:
  CXLoadedDiagnostic() : CXDiagnosticImpl(LoadedDiagnosticKind),
    severity(0), category(0) {}

  ~CXLoadedDiagnostic() override;

  /// Return the severity of the diagnostic.
  CXDiagnosticSeverity getSeverity() const override;

  /// Return the location of the diagnostic.
  CXSourceLocation getLocation() const override;

  /// Return the spelling of the diagnostic.
  CXString getSpelling() const override;

  /// Return the text for the diagnostic option.
  CXString getDiagnosticOption(CXString *Disable) const override;

  /// Return the category of the diagnostic.
  unsigned getCategory() const override;

  /// Return the category string of the diagnostic.
  CXString getCategoryText() const override;

  /// Return the number of source ranges for the diagnostic.
  unsigned getNumRanges() const override;

  /// Return the source ranges for the diagnostic.
  CXSourceRange getRange(unsigned Range) const override;

  /// Return the number of FixIts.
  unsigned getNumFixIts() const override;

  /// Return the FixIt information (source range and inserted text).
  CXString getFixIt(unsigned FixIt,
                    CXSourceRange *ReplacementRange) const override;

  static bool classof(const CXDiagnosticImpl *D) {
    return D->getKind() == LoadedDiagnosticKind;
  }
  
  /// Decode the CXSourceLocation into file, line, column, and offset.
  static void decodeLocation(CXSourceLocation location,
                             CXFile *file,
                             unsigned *line,
                             unsigned *column,
                             unsigned *offset);

  struct Location {
    CXFile file;
    unsigned line = 0;
    unsigned column = 0;
    unsigned offset = 0;
    
    Location() = default;
  };
  
  Location DiagLoc;

  std::vector<CXSourceRange> Ranges;
  std::vector<std::pair<CXSourceRange, const char *> > FixIts;
  const char *Spelling;
  llvm::StringRef DiagOption;
  llvm::StringRef CategoryText;
  unsigned severity;
  unsigned category;
};
}

#endif
