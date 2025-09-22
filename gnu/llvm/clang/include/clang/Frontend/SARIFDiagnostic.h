//===--- SARIFDiagnostic.h - SARIF Diagnostic Formatting -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a utility class that provides support for constructing a SARIF object
// containing diagnostics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_SARIFDIAGNOSTIC_H
#define LLVM_CLANG_FRONTEND_SARIFDIAGNOSTIC_H

#include "clang/Basic/Sarif.h"
#include "clang/Frontend/DiagnosticRenderer.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

class SARIFDiagnostic : public DiagnosticRenderer {
public:
  SARIFDiagnostic(raw_ostream &OS, const LangOptions &LangOpts,
                  DiagnosticOptions *DiagOpts, SarifDocumentWriter *Writer);

  ~SARIFDiagnostic() = default;

  SARIFDiagnostic &operator=(const SARIFDiagnostic &&) = delete;
  SARIFDiagnostic(SARIFDiagnostic &&) = delete;
  SARIFDiagnostic &operator=(const SARIFDiagnostic &) = delete;
  SARIFDiagnostic(const SARIFDiagnostic &) = delete;

protected:
  void emitDiagnosticMessage(FullSourceLoc Loc, PresumedLoc PLoc,
                             DiagnosticsEngine::Level Level, StringRef Message,
                             ArrayRef<CharSourceRange> Ranges,
                             DiagOrStoredDiag D) override;

  void emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                         DiagnosticsEngine::Level Level,
                         ArrayRef<CharSourceRange> Ranges) override;

  void emitCodeContext(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                       SmallVectorImpl<CharSourceRange> &Ranges,
                       ArrayRef<FixItHint> Hints) override {}

  void emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) override;

  void emitImportLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                          StringRef ModuleName) override;

  void emitBuildingModuleLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                                  StringRef ModuleName) override;

private:
  // Shared between SARIFDiagnosticPrinter and this renderer.
  SarifDocumentWriter *Writer;

  SarifResult addLocationToResult(SarifResult Result, FullSourceLoc Loc,
                                  PresumedLoc PLoc,
                                  ArrayRef<CharSourceRange> Ranges,
                                  const Diagnostic &Diag);

  SarifRule addDiagnosticLevelToRule(SarifRule Rule,
                                     DiagnosticsEngine::Level Level);

  llvm::StringRef emitFilename(StringRef Filename, const SourceManager &SM);
};

} // end namespace clang

#endif
