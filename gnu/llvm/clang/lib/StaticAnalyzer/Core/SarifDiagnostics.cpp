//===--- SarifDiagnostics.cpp - Sarif Diagnostics for Paths -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the SarifDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/MacroExpansionContext.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/Sarif.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace clang;
using namespace ento;

namespace {
class SarifDiagnostics : public PathDiagnosticConsumer {
  std::string OutputFile;
  const LangOptions &LO;
  SarifDocumentWriter SarifWriter;

public:
  SarifDiagnostics(const std::string &Output, const LangOptions &LO,
                   const SourceManager &SM)
      : OutputFile(Output), LO(LO), SarifWriter(SM) {}
  ~SarifDiagnostics() override = default;

  void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                            FilesMade *FM) override;

  StringRef getName() const override { return "SarifDiagnostics"; }
  PathGenerationScheme getGenerationScheme() const override { return Minimal; }
  bool supportsLogicalOpControlFlow() const override { return true; }
  bool supportsCrossFileDiagnostics() const override { return true; }
};
} // end anonymous namespace

void ento::createSarifDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &Output, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {

  // TODO: Emit an error here.
  if (Output.empty())
    return;

  C.push_back(
      new SarifDiagnostics(Output, PP.getLangOpts(), PP.getSourceManager()));
  createTextMinimalPathDiagnosticConsumer(std::move(DiagOpts), C, Output, PP,
                                          CTU, MacroExpansions);
}

static StringRef getRuleDescription(StringRef CheckName) {
  return llvm::StringSwitch<StringRef>(CheckName)
#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI, IS_HIDDEN)                 \
  .Case(FULLNAME, HELPTEXT)
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS
      ;
}

static StringRef getRuleHelpURIStr(StringRef CheckName) {
  return llvm::StringSwitch<StringRef>(CheckName)
#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI, IS_HIDDEN)                 \
  .Case(FULLNAME, DOC_URI)
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS
      ;
}

static ThreadFlowImportance
calculateImportance(const PathDiagnosticPiece &Piece) {
  switch (Piece.getKind()) {
  case PathDiagnosticPiece::Call:
  case PathDiagnosticPiece::Macro:
  case PathDiagnosticPiece::Note:
  case PathDiagnosticPiece::PopUp:
    // FIXME: What should be reported here?
    break;
  case PathDiagnosticPiece::Event:
    return Piece.getTagStr() == "ConditionBRVisitor"
               ? ThreadFlowImportance::Important
               : ThreadFlowImportance::Essential;
  case PathDiagnosticPiece::ControlFlow:
    return ThreadFlowImportance::Unimportant;
  }
  return ThreadFlowImportance::Unimportant;
}

/// Accepts a SourceRange corresponding to a pair of the first and last tokens
/// and converts to a Character granular CharSourceRange.
static CharSourceRange convertTokenRangeToCharRange(const SourceRange &R,
                                                    const SourceManager &SM,
                                                    const LangOptions &LO) {
  // Caret diagnostics have the first and last locations pointed at the same
  // location, return these as-is.
  if (R.getBegin() == R.getEnd())
    return CharSourceRange::getCharRange(R);

  SourceLocation BeginCharLoc = R.getBegin();
  // For token ranges, the raw end SLoc points at the first character of the
  // last token in the range. This must be moved to one past the end of the
  // last character using the lexer.
  SourceLocation EndCharLoc =
      Lexer::getLocForEndOfToken(R.getEnd(), /* Offset = */ 0, SM, LO);
  return CharSourceRange::getCharRange(BeginCharLoc, EndCharLoc);
}

static SmallVector<ThreadFlow, 8> createThreadFlows(const PathDiagnostic *Diag,
                                                    const LangOptions &LO) {
  SmallVector<ThreadFlow, 8> Flows;
  const PathPieces &Pieces = Diag->path.flatten(false);
  for (const auto &Piece : Pieces) {
    auto Range = convertTokenRangeToCharRange(
        Piece->getLocation().asRange(), Piece->getLocation().getManager(), LO);
    auto Flow = ThreadFlow::create()
                    .setImportance(calculateImportance(*Piece))
                    .setRange(Range)
                    .setMessage(Piece->getString());
    Flows.push_back(Flow);
  }
  return Flows;
}

static StringMap<uint32_t>
createRuleMapping(const std::vector<const PathDiagnostic *> &Diags,
                  SarifDocumentWriter &SarifWriter) {
  StringMap<uint32_t> RuleMapping;
  llvm::StringSet<> Seen;

  for (const PathDiagnostic *D : Diags) {
    StringRef CheckName = D->getCheckerName();
    std::pair<llvm::StringSet<>::iterator, bool> P = Seen.insert(CheckName);
    if (P.second) {
      auto Rule = SarifRule::create()
                      .setName(CheckName)
                      .setRuleId(CheckName)
                      .setDescription(getRuleDescription(CheckName))
                      .setHelpURI(getRuleHelpURIStr(CheckName));
      size_t RuleIdx = SarifWriter.createRule(Rule);
      RuleMapping[CheckName] = RuleIdx;
    }
  }
  return RuleMapping;
}

static SarifResult createResult(const PathDiagnostic *Diag,
                                const StringMap<uint32_t> &RuleMapping,
                                const LangOptions &LO) {

  StringRef CheckName = Diag->getCheckerName();
  uint32_t RuleIdx = RuleMapping.lookup(CheckName);
  auto Range = convertTokenRangeToCharRange(
      Diag->getLocation().asRange(), Diag->getLocation().getManager(), LO);

  SmallVector<ThreadFlow, 8> Flows = createThreadFlows(Diag, LO);
  auto Result = SarifResult::create(RuleIdx)
                    .setRuleId(CheckName)
                    .setDiagnosticMessage(Diag->getVerboseDescription())
                    .setDiagnosticLevel(SarifResultLevel::Warning)
                    .setLocations({Range})
                    .setThreadFlows(Flows);
  return Result;
}

void SarifDiagnostics::FlushDiagnosticsImpl(
    std::vector<const PathDiagnostic *> &Diags, FilesMade *) {
  // We currently overwrite the file if it already exists. However, it may be
  // useful to add a feature someday that allows the user to append a run to an
  // existing SARIF file. One danger from that approach is that the size of the
  // file can become large very quickly, so decoding into JSON to append a run
  // may be an expensive operation.
  std::error_code EC;
  llvm::raw_fd_ostream OS(OutputFile, EC, llvm::sys::fs::OF_TextWithCRLF);
  if (EC) {
    llvm::errs() << "warning: could not create file: " << EC.message() << '\n';
    return;
  }

  std::string ToolVersion = getClangFullVersion();
  SarifWriter.createRun("clang", "clang static analyzer", ToolVersion);
  StringMap<uint32_t> RuleMapping = createRuleMapping(Diags, SarifWriter);
  for (const PathDiagnostic *D : Diags) {
    SarifResult Result = createResult(D, RuleMapping, LO);
    SarifWriter.appendResult(Result);
  }
  auto Document = SarifWriter.createDocument();
  OS << llvm::formatv("{0:2}\n", json::Value(std::move(Document)));
}
