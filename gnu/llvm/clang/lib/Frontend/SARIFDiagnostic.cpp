//===--------- SARIFDiagnostic.cpp - SARIF Diagnostic Formatting ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/SARIFDiagnostic.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/Sarif.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>

namespace clang {

SARIFDiagnostic::SARIFDiagnostic(raw_ostream &OS, const LangOptions &LangOpts,
                                 DiagnosticOptions *DiagOpts,
                                 SarifDocumentWriter *Writer)
    : DiagnosticRenderer(LangOpts, DiagOpts), Writer(Writer) {}

// FIXME(llvm-project/issues/57323): Refactor Diagnostic classes.
void SARIFDiagnostic::emitDiagnosticMessage(
    FullSourceLoc Loc, PresumedLoc PLoc, DiagnosticsEngine::Level Level,
    StringRef Message, ArrayRef<clang::CharSourceRange> Ranges,
    DiagOrStoredDiag D) {

  const auto *Diag = D.dyn_cast<const Diagnostic *>();

  if (!Diag)
    return;

  SarifRule Rule = SarifRule::create().setRuleId(std::to_string(Diag->getID()));

  Rule = addDiagnosticLevelToRule(Rule, Level);

  unsigned RuleIdx = Writer->createRule(Rule);

  SarifResult Result =
      SarifResult::create(RuleIdx).setDiagnosticMessage(Message);

  if (Loc.isValid())
    Result = addLocationToResult(Result, Loc, PLoc, Ranges, *Diag);

  Writer->appendResult(Result);
}

SarifResult SARIFDiagnostic::addLocationToResult(
    SarifResult Result, FullSourceLoc Loc, PresumedLoc PLoc,
    ArrayRef<CharSourceRange> Ranges, const Diagnostic &Diag) {
  SmallVector<CharSourceRange> Locations = {};

  if (PLoc.isInvalid()) {
    // At least add the file name if available:
    FileID FID = Loc.getFileID();
    if (FID.isValid()) {
      if (OptionalFileEntryRef FE = Loc.getFileEntryRef()) {
        emitFilename(FE->getName(), Loc.getManager());
        // FIXME(llvm-project/issues/57366): File-only locations
      }
    }
    return Result;
  }

  FileID CaretFileID = Loc.getExpansionLoc().getFileID();

  for (const CharSourceRange Range : Ranges) {
    // Ignore invalid ranges.
    if (Range.isInvalid())
      continue;

    auto &SM = Loc.getManager();
    SourceLocation B = SM.getExpansionLoc(Range.getBegin());
    CharSourceRange ERange = SM.getExpansionRange(Range.getEnd());
    SourceLocation E = ERange.getEnd();
    bool IsTokenRange = ERange.isTokenRange();

    std::pair<FileID, unsigned> BInfo = SM.getDecomposedLoc(B);
    std::pair<FileID, unsigned> EInfo = SM.getDecomposedLoc(E);

    // If the start or end of the range is in another file, just discard
    // it.
    if (BInfo.first != CaretFileID || EInfo.first != CaretFileID)
      continue;

    // Add in the length of the token, so that we cover multi-char
    // tokens.
    unsigned TokSize = 0;
    if (IsTokenRange)
      TokSize = Lexer::MeasureTokenLength(E, SM, LangOpts);

    FullSourceLoc BF(B, SM), EF(E, SM);
    SourceLocation BeginLoc = SM.translateLineCol(
        BF.getFileID(), BF.getLineNumber(), BF.getColumnNumber());
    SourceLocation EndLoc = SM.translateLineCol(
        EF.getFileID(), EF.getLineNumber(), EF.getColumnNumber() + TokSize);

    Locations.push_back(
        CharSourceRange{SourceRange{BeginLoc, EndLoc}, /* ITR = */ false});
    // FIXME: Additional ranges should use presumed location in both
    // Text and SARIF diagnostics.
  }

  auto &SM = Loc.getManager();
  auto FID = PLoc.getFileID();
  // Visual Studio 2010 or earlier expects column number to be off by one.
  unsigned int ColNo = (LangOpts.MSCompatibilityVersion &&
                        !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2012))
                           ? PLoc.getColumn() - 1
                           : PLoc.getColumn();
  SourceLocation DiagLoc = SM.translateLineCol(FID, PLoc.getLine(), ColNo);

  // FIXME(llvm-project/issues/57366): Properly process #line directives.
  Locations.push_back(
      CharSourceRange{SourceRange{DiagLoc, DiagLoc}, /* ITR = */ false});

  return Result.setLocations(Locations);
}

SarifRule
SARIFDiagnostic::addDiagnosticLevelToRule(SarifRule Rule,
                                          DiagnosticsEngine::Level Level) {
  auto Config = SarifReportingConfiguration::create();

  switch (Level) {
  case DiagnosticsEngine::Note:
    Config = Config.setLevel(SarifResultLevel::Note);
    break;
  case DiagnosticsEngine::Remark:
    Config = Config.setLevel(SarifResultLevel::None);
    break;
  case DiagnosticsEngine::Warning:
    Config = Config.setLevel(SarifResultLevel::Warning);
    break;
  case DiagnosticsEngine::Error:
    Config = Config.setLevel(SarifResultLevel::Error).setRank(50);
    break;
  case DiagnosticsEngine::Fatal:
    Config = Config.setLevel(SarifResultLevel::Error).setRank(100);
    break;
  case DiagnosticsEngine::Ignored:
    assert(false && "Invalid diagnostic type");
  }

  return Rule.setDefaultConfiguration(Config);
}

llvm::StringRef SARIFDiagnostic::emitFilename(StringRef Filename,
                                              const SourceManager &SM) {
  if (DiagOpts->AbsolutePath) {
    auto File = SM.getFileManager().getOptionalFileRef(Filename);
    if (File) {
      // We want to print a simplified absolute path, i. e. without "dots".
      //
      // The hardest part here are the paths like "<part1>/<link>/../<part2>".
      // On Unix-like systems, we cannot just collapse "<link>/..", because
      // paths are resolved sequentially, and, thereby, the path
      // "<part1>/<part2>" may point to a different location. That is why
      // we use FileManager::getCanonicalName(), which expands all indirections
      // with llvm::sys::fs::real_path() and caches the result.
      //
      // On the other hand, it would be better to preserve as much of the
      // original path as possible, because that helps a user to recognize it.
      // real_path() expands all links, which is sometimes too much. Luckily,
      // on Windows we can just use llvm::sys::path::remove_dots(), because,
      // on that system, both aforementioned paths point to the same place.
#ifdef _WIN32
      SmallString<256> TmpFilename = File->getName();
      llvm::sys::fs::make_absolute(TmpFilename);
      llvm::sys::path::native(TmpFilename);
      llvm::sys::path::remove_dots(TmpFilename, /* remove_dot_dot */ true);
      Filename = StringRef(TmpFilename.data(), TmpFilename.size());
#else
      Filename = SM.getFileManager().getCanonicalName(*File);
#endif
    }
  }

  return Filename;
}

/// Print out the file/line/column information and include trace.
///
/// This method handlen the emission of the diagnostic location information.
/// This includes extracting as much location information as is present for
/// the diagnostic and printing it, as well as any include stack or source
/// ranges necessary.
void SARIFDiagnostic::emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                                        DiagnosticsEngine::Level Level,
                                        ArrayRef<CharSourceRange> Ranges) {
  assert(false && "Not implemented in SARIF mode");
}

void SARIFDiagnostic::emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) {
  assert(false && "Not implemented in SARIF mode");
}

void SARIFDiagnostic::emitImportLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                                         StringRef ModuleName) {
  assert(false && "Not implemented in SARIF mode");
}

void SARIFDiagnostic::emitBuildingModuleLocation(FullSourceLoc Loc,
                                                 PresumedLoc PLoc,
                                                 StringRef ModuleName) {
  assert(false && "Not implemented in SARIF mode");
}
} // namespace clang
