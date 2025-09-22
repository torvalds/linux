//===-- SARIFDiagnosticPrinter.h - SARIF Diagnostic Client -------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a concrete diagnostic client, which prints the diagnostics to
// standard error in SARIF format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_SARIFDIAGNOSTICPRINTER_H
#define LLVM_CLANG_FRONTEND_SARIFDIAGNOSTICPRINTER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Sarif.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace clang {
class DiagnosticOptions;
class LangOptions;
class SARIFDiagnostic;
class SarifDocumentWriter;

class SARIFDiagnosticPrinter : public DiagnosticConsumer {
public:
  SARIFDiagnosticPrinter(raw_ostream &OS, DiagnosticOptions *Diags);
  ~SARIFDiagnosticPrinter() = default;

  SARIFDiagnosticPrinter &operator=(const SARIFDiagnosticPrinter &&) = delete;
  SARIFDiagnosticPrinter(SARIFDiagnosticPrinter &&) = delete;
  SARIFDiagnosticPrinter &operator=(const SARIFDiagnosticPrinter &) = delete;
  SARIFDiagnosticPrinter(const SARIFDiagnosticPrinter &) = delete;

  /// setPrefix - Set the diagnostic printer prefix string, which will be
  /// printed at the start of any diagnostics. If empty, no prefix string is
  /// used.
  void setPrefix(llvm::StringRef Value) { Prefix = Value; }

  bool hasSarifWriter() const { return Writer != nullptr; }

  SarifDocumentWriter &getSarifWriter() const {
    assert(Writer && "SarifWriter not set!");
    return *Writer;
  }

  void setSarifWriter(std::unique_ptr<SarifDocumentWriter> SarifWriter) {
    Writer = std::move(SarifWriter);
  }

  void BeginSourceFile(const LangOptions &LO, const Preprocessor *PP) override;
  void EndSourceFile() override;
  void HandleDiagnostic(DiagnosticsEngine::Level Level,
                        const Diagnostic &Info) override;

private:
  raw_ostream &OS;
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;

  /// Handle to the currently active SARIF diagnostic emitter.
  std::unique_ptr<SARIFDiagnostic> SARIFDiag;

  /// A string to prefix to error messages.
  std::string Prefix;

  std::unique_ptr<SarifDocumentWriter> Writer;
};

} // end namespace clang

#endif
