//===--- TextDiagnosticPrinter.cpp - Diagnostic Printer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This diagnostic client prints out their diagnostic messages.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/TextDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
using namespace clang;

TextDiagnosticPrinter::TextDiagnosticPrinter(raw_ostream &os,
                                             DiagnosticOptions *diags,
                                             bool _OwnsOutputStream)
  : OS(os), DiagOpts(diags),
    OwnsOutputStream(_OwnsOutputStream) {
}

TextDiagnosticPrinter::~TextDiagnosticPrinter() {
  if (OwnsOutputStream)
    delete &OS;
}

void TextDiagnosticPrinter::BeginSourceFile(const LangOptions &LO,
                                            const Preprocessor *PP) {
  // Build the TextDiagnostic utility.
  TextDiag.reset(new TextDiagnostic(OS, LO, &*DiagOpts, PP));
}

void TextDiagnosticPrinter::EndSourceFile() {
  TextDiag.reset();
}

/// Print any diagnostic option information to a raw_ostream.
///
/// This implements all of the logic for adding diagnostic options to a message
/// (via OS). Each relevant option is comma separated and all are enclosed in
/// the standard bracketing: " [...]".
static void printDiagnosticOptions(raw_ostream &OS,
                                   DiagnosticsEngine::Level Level,
                                   const Diagnostic &Info,
                                   const DiagnosticOptions &DiagOpts) {
  bool Started = false;
  if (DiagOpts.ShowOptionNames) {
    // Handle special cases for non-warnings early.
    if (Info.getID() == diag::fatal_too_many_errors) {
      OS << " [-ferror-limit=]";
      return;
    }

    // The code below is somewhat fragile because we are essentially trying to
    // report to the user what happened by inferring what the diagnostic engine
    // did. Eventually it might make more sense to have the diagnostic engine
    // include some "why" information in the diagnostic.

    // If this is a warning which has been mapped to an error by the user (as
    // inferred by checking whether the default mapping is to an error) then
    // flag it as such. Note that diagnostics could also have been mapped by a
    // pragma, but we don't currently have a way to distinguish this.
    if (Level == DiagnosticsEngine::Error &&
        DiagnosticIDs::isBuiltinWarningOrExtension(Info.getID()) &&
        !DiagnosticIDs::isDefaultMappingAsError(Info.getID())) {
      OS << " [-Werror";
      Started = true;
    }

    StringRef Opt = DiagnosticIDs::getWarningOptionForDiag(Info.getID());
    if (!Opt.empty()) {
      OS << (Started ? "," : " [")
         << (Level == DiagnosticsEngine::Remark ? "-R" : "-W") << Opt;
      StringRef OptValue = Info.getDiags()->getFlagValue();
      if (!OptValue.empty())
        OS << "=" << OptValue;
      Started = true;
    }
  }

  // If the user wants to see category information, include it too.
  if (DiagOpts.ShowCategories) {
    unsigned DiagCategory =
      DiagnosticIDs::getCategoryNumberForDiag(Info.getID());
    if (DiagCategory) {
      OS << (Started ? "," : " [");
      Started = true;
      if (DiagOpts.ShowCategories == 1)
        OS << DiagCategory;
      else {
        assert(DiagOpts.ShowCategories == 2 && "Invalid ShowCategories value");
        OS << DiagnosticIDs::getCategoryNameFromID(DiagCategory);
      }
    }
  }
  if (Started)
    OS << ']';
}

void TextDiagnosticPrinter::HandleDiagnostic(DiagnosticsEngine::Level Level,
                                             const Diagnostic &Info) {
  // Default implementation (Warnings/errors count).
  DiagnosticConsumer::HandleDiagnostic(Level, Info);

  // Render the diagnostic message into a temporary buffer eagerly. We'll use
  // this later as we print out the diagnostic to the terminal.
  SmallString<100> OutStr;
  Info.FormatDiagnostic(OutStr);

  llvm::raw_svector_ostream DiagMessageStream(OutStr);
  printDiagnosticOptions(DiagMessageStream, Level, Info, *DiagOpts);

  // Keeps track of the starting position of the location
  // information (e.g., "foo.c:10:4:") that precedes the error
  // message. We use this information to determine how long the
  // file+line+column number prefix is.
  uint64_t StartOfLocationInfo = OS.tell();

  if (!Prefix.empty())
    OS << Prefix << ": ";

  // Use a dedicated, simpler path for diagnostics without a valid location.
  // This is important as if the location is missing, we may be emitting
  // diagnostics in a context that lacks language options, a source manager, or
  // other infrastructure necessary when emitting more rich diagnostics.
  if (!Info.getLocation().isValid()) {
    TextDiagnostic::printDiagnosticLevel(OS, Level, DiagOpts->ShowColors);
    TextDiagnostic::printDiagnosticMessage(
        OS, /*IsSupplemental=*/Level == DiagnosticsEngine::Note,
        DiagMessageStream.str(), OS.tell() - StartOfLocationInfo,
        DiagOpts->MessageLength, DiagOpts->ShowColors);
    OS.flush();
    return;
  }

  // Assert that the rest of our infrastructure is setup properly.
  assert(DiagOpts && "Unexpected diagnostic without options set");
  assert(Info.hasSourceManager() &&
         "Unexpected diagnostic with no source manager");
  assert(TextDiag && "Unexpected diagnostic outside source file processing");

  TextDiag->emitDiagnostic(
      FullSourceLoc(Info.getLocation(), Info.getSourceManager()), Level,
      DiagMessageStream.str(), Info.getRanges(), Info.getFixItHints());

  OS.flush();
}
