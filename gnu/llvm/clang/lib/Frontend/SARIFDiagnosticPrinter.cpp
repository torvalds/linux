//===------- SARIFDiagnosticPrinter.cpp - Diagnostic Printer---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This diagnostic client prints out their diagnostic messages in SARIF format.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/SARIFDiagnosticPrinter.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/Sarif.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/DiagnosticRenderer.h"
#include "clang/Frontend/SARIFDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

namespace clang {

SARIFDiagnosticPrinter::SARIFDiagnosticPrinter(raw_ostream &OS,
                                               DiagnosticOptions *Diags)
    : OS(OS), DiagOpts(Diags) {}

void SARIFDiagnosticPrinter::BeginSourceFile(const LangOptions &LO,
                                             const Preprocessor *PP) {
  // Build the SARIFDiagnostic utility.
  assert(hasSarifWriter() && "Writer not set!");
  assert(!SARIFDiag && "SARIFDiagnostic already set.");
  SARIFDiag = std::make_unique<SARIFDiagnostic>(OS, LO, &*DiagOpts, &*Writer);
  // Initialize the SARIF object.
  Writer->createRun("clang", Prefix);
}

void SARIFDiagnosticPrinter::EndSourceFile() {
  assert(SARIFDiag && "SARIFDiagnostic has not been set.");
  Writer->endRun();
  llvm::json::Value Value(Writer->createDocument());
  OS << "\n" << Value << "\n\n";
  OS.flush();
  SARIFDiag.reset();
}

void SARIFDiagnosticPrinter::HandleDiagnostic(DiagnosticsEngine::Level Level,
                                              const Diagnostic &Info) {
  assert(SARIFDiag && "SARIFDiagnostic has not been set.");
  // Default implementation (Warnings/errors count). Keeps track of the
  // number of errors.
  DiagnosticConsumer::HandleDiagnostic(Level, Info);

  // Render the diagnostic message into a temporary buffer eagerly. We'll use
  // this later as we add the diagnostic to the SARIF object.
  SmallString<100> OutStr;
  Info.FormatDiagnostic(OutStr);

  llvm::raw_svector_ostream DiagMessageStream(OutStr);

  // Use a dedicated, simpler path for diagnostics without a valid location.
  // This is important as if the location is missing, we may be emitting
  // diagnostics in a context that lacks language options, a source manager, or
  // other infrastructure necessary when emitting more rich diagnostics.
  if (Info.getLocation().isInvalid()) {
    // FIXME: Enable diagnostics without a source manager
    return;
  }

  // Assert that the rest of our infrastructure is setup properly.
  assert(DiagOpts && "Unexpected diagnostic without options set");
  assert(Info.hasSourceManager() &&
         "Unexpected diagnostic with no source manager");

  SARIFDiag->emitDiagnostic(
      FullSourceLoc(Info.getLocation(), Info.getSourceManager()), Level,
      DiagMessageStream.str(), Info.getRanges(), Info.getFixItHints(), &Info);
}
} // namespace clang
