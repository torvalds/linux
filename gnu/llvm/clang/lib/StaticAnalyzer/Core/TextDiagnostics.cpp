//===--- TextDiagnostics.cpp - Text Diagnostics for Paths -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TextDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/MacroExpansionContext.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/CrossTU/CrossTranslationUnit.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"

using namespace clang;
using namespace ento;
using namespace tooling;

namespace {
/// Emits minimal diagnostics (report message + notes) for the 'none' output
/// type to the standard error, or to complement many others. Emits detailed
/// diagnostics in textual format for the 'text' output type.
class TextDiagnostics : public PathDiagnosticConsumer {
  PathDiagnosticConsumerOptions DiagOpts;
  DiagnosticsEngine &DiagEng;
  const LangOptions &LO;
  bool ShouldDisplayPathNotes;

public:
  TextDiagnostics(PathDiagnosticConsumerOptions DiagOpts,
                  DiagnosticsEngine &DiagEng, const LangOptions &LO,
                  bool ShouldDisplayPathNotes)
      : DiagOpts(std::move(DiagOpts)), DiagEng(DiagEng), LO(LO),
        ShouldDisplayPathNotes(ShouldDisplayPathNotes) {}
  ~TextDiagnostics() override {}

  StringRef getName() const override { return "TextDiagnostics"; }

  bool supportsLogicalOpControlFlow() const override { return true; }
  bool supportsCrossFileDiagnostics() const override { return true; }

  PathGenerationScheme getGenerationScheme() const override {
    return ShouldDisplayPathNotes ? Minimal : None;
  }

  void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                            FilesMade *filesMade) override {
    unsigned WarnID =
        DiagOpts.ShouldDisplayWarningsAsErrors
            ? DiagEng.getCustomDiagID(DiagnosticsEngine::Error, "%0")
            : DiagEng.getCustomDiagID(DiagnosticsEngine::Warning, "%0");
    unsigned NoteID = DiagEng.getCustomDiagID(DiagnosticsEngine::Note, "%0");
    SourceManager &SM = DiagEng.getSourceManager();

    Replacements Repls;
    auto reportPiece = [&](unsigned ID, FullSourceLoc Loc, StringRef String,
                           ArrayRef<SourceRange> Ranges,
                           ArrayRef<FixItHint> Fixits) {
      if (!DiagOpts.ShouldApplyFixIts) {
        DiagEng.Report(Loc, ID) << String << Ranges << Fixits;
        return;
      }

      DiagEng.Report(Loc, ID) << String << Ranges;
      for (const FixItHint &Hint : Fixits) {
        Replacement Repl(SM, Hint.RemoveRange, Hint.CodeToInsert);

        if (llvm::Error Err = Repls.add(Repl)) {
          llvm::errs() << "Error applying replacement " << Repl.toString()
                       << ": " << Err << "\n";
        }
      }
    };

    for (const PathDiagnostic *PD : Diags) {
      std::string WarningMsg = (DiagOpts.ShouldDisplayDiagnosticName
                                    ? " [" + PD->getCheckerName() + "]"
                                    : "")
                                   .str();

      reportPiece(WarnID, PD->getLocation().asLocation(),
                  (PD->getShortDescription() + WarningMsg).str(),
                  PD->path.back()->getRanges(), PD->path.back()->getFixits());

      // First, add extra notes, even if paths should not be included.
      for (const auto &Piece : PD->path) {
        if (!isa<PathDiagnosticNotePiece>(Piece.get()))
          continue;

        reportPiece(NoteID, Piece->getLocation().asLocation(),
                    Piece->getString(), Piece->getRanges(),
                    Piece->getFixits());
      }

      if (!ShouldDisplayPathNotes)
        continue;

      // Then, add the path notes if necessary.
      PathPieces FlatPath = PD->path.flatten(/*ShouldFlattenMacros=*/true);
      for (const auto &Piece : FlatPath) {
        if (isa<PathDiagnosticNotePiece>(Piece.get()))
          continue;

        reportPiece(NoteID, Piece->getLocation().asLocation(),
                    Piece->getString(), Piece->getRanges(),
                    Piece->getFixits());
      }
    }

    if (Repls.empty())
      return;

    Rewriter Rewrite(SM, LO);
    if (!applyAllReplacements(Repls, Rewrite)) {
      llvm::errs() << "An error occurred during applying fix-it.\n";
    }

    Rewrite.overwriteChangedFiles();
  }
};
} // end anonymous namespace

void ento::createTextPathDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &Prefix, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {
  C.emplace_back(new TextDiagnostics(std::move(DiagOpts), PP.getDiagnostics(),
                                     PP.getLangOpts(),
                                     /*ShouldDisplayPathNotes=*/true));
}

void ento::createTextMinimalPathDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &Prefix, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {
  C.emplace_back(new TextDiagnostics(std::move(DiagOpts), PP.getDiagnostics(),
                                     PP.getLangOpts(),
                                     /*ShouldDisplayPathNotes=*/false));
}
