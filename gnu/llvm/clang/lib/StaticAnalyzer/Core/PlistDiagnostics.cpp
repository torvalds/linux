//===--- PlistDiagnostics.cpp - Plist Diagnostics for Paths -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PlistDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/IssueHash.h"
#include "clang/Analysis/MacroExpansionContext.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/PlistSupport.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/CrossTU/CrossTranslationUnit.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/TokenConcatenation.h"
#include "clang/Rewrite/Core/HTMLRewrite.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Casting.h"
#include <memory>
#include <optional>

using namespace clang;
using namespace ento;
using namespace markup;

//===----------------------------------------------------------------------===//
// Declarations of helper classes and functions for emitting bug reports in
// plist format.
//===----------------------------------------------------------------------===//

namespace {
  class PlistDiagnostics : public PathDiagnosticConsumer {
    PathDiagnosticConsumerOptions DiagOpts;
    const std::string OutputFile;
    const Preprocessor &PP;
    const cross_tu::CrossTranslationUnitContext &CTU;
    const MacroExpansionContext &MacroExpansions;
    const bool SupportsCrossFileDiagnostics;

    void printBugPath(llvm::raw_ostream &o, const FIDMap &FM,
                      const PathPieces &Path);

  public:
    PlistDiagnostics(PathDiagnosticConsumerOptions DiagOpts,
                     const std::string &OutputFile, const Preprocessor &PP,
                     const cross_tu::CrossTranslationUnitContext &CTU,
                     const MacroExpansionContext &MacroExpansions,
                     bool supportsMultipleFiles);

    ~PlistDiagnostics() override {}

    void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                              FilesMade *filesMade) override;

    StringRef getName() const override {
      return "PlistDiagnostics";
    }

    PathGenerationScheme getGenerationScheme() const override {
      return Extensive;
    }
    bool supportsLogicalOpControlFlow() const override { return true; }
    bool supportsCrossFileDiagnostics() const override {
      return SupportsCrossFileDiagnostics;
    }
  };
} // end anonymous namespace

namespace {

/// A helper class for emitting a single report.
class PlistPrinter {
  const FIDMap& FM;
  const Preprocessor &PP;
  const cross_tu::CrossTranslationUnitContext &CTU;
  const MacroExpansionContext &MacroExpansions;
  llvm::SmallVector<const PathDiagnosticMacroPiece *, 0> MacroPieces;

public:
  PlistPrinter(const FIDMap &FM, const Preprocessor &PP,
               const cross_tu::CrossTranslationUnitContext &CTU,
               const MacroExpansionContext &MacroExpansions)
      : FM(FM), PP(PP), CTU(CTU), MacroExpansions(MacroExpansions) {}

  void ReportDiag(raw_ostream &o, const PathDiagnosticPiece& P) {
    ReportPiece(o, P, /*indent*/ 4, /*depth*/ 0, /*includeControlFlow*/ true);
  }

  /// Print the expansions of the collected macro pieces.
  ///
  /// Each time ReportDiag is called on a PathDiagnosticMacroPiece (or, if one
  /// is found through a call piece, etc), it's subpieces are reported, and the
  /// piece itself is collected. Call this function after the entire bugpath
  /// was reported.
  void ReportMacroExpansions(raw_ostream &o, unsigned indent);

private:
  void ReportPiece(raw_ostream &o, const PathDiagnosticPiece &P,
                   unsigned indent, unsigned depth, bool includeControlFlow,
                   bool isKeyEvent = false) {
    switch (P.getKind()) {
      case PathDiagnosticPiece::ControlFlow:
        if (includeControlFlow)
          ReportControlFlow(o, cast<PathDiagnosticControlFlowPiece>(P), indent);
        break;
      case PathDiagnosticPiece::Call:
        ReportCall(o, cast<PathDiagnosticCallPiece>(P), indent,
                   depth);
        break;
      case PathDiagnosticPiece::Event:
        ReportEvent(o, cast<PathDiagnosticEventPiece>(P), indent, depth,
                    isKeyEvent);
        break;
      case PathDiagnosticPiece::Macro:
        ReportMacroSubPieces(o, cast<PathDiagnosticMacroPiece>(P), indent,
                             depth);
        break;
      case PathDiagnosticPiece::Note:
        ReportNote(o, cast<PathDiagnosticNotePiece>(P), indent);
        break;
      case PathDiagnosticPiece::PopUp:
        ReportPopUp(o, cast<PathDiagnosticPopUpPiece>(P), indent);
        break;
    }
  }

  void EmitRanges(raw_ostream &o, const ArrayRef<SourceRange> Ranges,
                  unsigned indent);
  void EmitMessage(raw_ostream &o, StringRef Message, unsigned indent);
  void EmitFixits(raw_ostream &o, ArrayRef<FixItHint> fixits, unsigned indent);

  void ReportControlFlow(raw_ostream &o,
                         const PathDiagnosticControlFlowPiece& P,
                         unsigned indent);
  void ReportEvent(raw_ostream &o, const PathDiagnosticEventPiece& P,
                   unsigned indent, unsigned depth, bool isKeyEvent = false);
  void ReportCall(raw_ostream &o, const PathDiagnosticCallPiece &P,
                  unsigned indent, unsigned depth);
  void ReportMacroSubPieces(raw_ostream &o, const PathDiagnosticMacroPiece& P,
                            unsigned indent, unsigned depth);
  void ReportNote(raw_ostream &o, const PathDiagnosticNotePiece& P,
                  unsigned indent);

  void ReportPopUp(raw_ostream &o, const PathDiagnosticPopUpPiece &P,
                   unsigned indent);
};

} // end of anonymous namespace

/// Print coverage information to output stream @c o.
/// May modify the used list of files @c Fids by inserting new ones.
static void printCoverage(const PathDiagnostic *D,
                          unsigned InputIndentLevel,
                          SmallVectorImpl<FileID> &Fids,
                          FIDMap &FM,
                          llvm::raw_fd_ostream &o);

static std::optional<StringRef> getExpandedMacro(
    SourceLocation MacroLoc, const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions, const SourceManager &SM);

//===----------------------------------------------------------------------===//
// Methods of PlistPrinter.
//===----------------------------------------------------------------------===//

void PlistPrinter::EmitRanges(raw_ostream &o,
                              const ArrayRef<SourceRange> Ranges,
                              unsigned indent) {

  if (Ranges.empty())
    return;

  Indent(o, indent) << "<key>ranges</key>\n";
  Indent(o, indent) << "<array>\n";
  ++indent;

  const SourceManager &SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  for (auto &R : Ranges)
    EmitRange(o, SM,
              Lexer::getAsCharRange(SM.getExpansionRange(R), SM, LangOpts),
              FM, indent + 1);
  --indent;
  Indent(o, indent) << "</array>\n";
}

void PlistPrinter::EmitMessage(raw_ostream &o, StringRef Message,
                               unsigned indent) {
  // Output the text.
  assert(!Message.empty());
  Indent(o, indent) << "<key>extended_message</key>\n";
  Indent(o, indent);
  EmitString(o, Message) << '\n';

  // Output the short text.
  // FIXME: Really use a short string.
  Indent(o, indent) << "<key>message</key>\n";
  Indent(o, indent);
  EmitString(o, Message) << '\n';
}

void PlistPrinter::EmitFixits(raw_ostream &o, ArrayRef<FixItHint> fixits,
                              unsigned indent) {
  if (fixits.size() == 0)
    return;

  const SourceManager &SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  Indent(o, indent) << "<key>fixits</key>\n";
  Indent(o, indent) << "<array>\n";
  for (const auto &fixit : fixits) {
    assert(!fixit.isNull());
    // FIXME: Add support for InsertFromRange and BeforePreviousInsertion.
    assert(!fixit.InsertFromRange.isValid() && "Not implemented yet!");
    assert(!fixit.BeforePreviousInsertions && "Not implemented yet!");
    Indent(o, indent) << " <dict>\n";
    Indent(o, indent) << "  <key>remove_range</key>\n";
    EmitRange(o, SM, Lexer::getAsCharRange(fixit.RemoveRange, SM, LangOpts),
              FM, indent + 2);
    Indent(o, indent) << "  <key>insert_string</key>";
    EmitString(o, fixit.CodeToInsert);
    o << "\n";
    Indent(o, indent) << " </dict>\n";
  }
  Indent(o, indent) << "</array>\n";
}

void PlistPrinter::ReportControlFlow(raw_ostream &o,
                                     const PathDiagnosticControlFlowPiece& P,
                                     unsigned indent) {

  const SourceManager &SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  Indent(o, indent) << "<dict>\n";
  ++indent;

  Indent(o, indent) << "<key>kind</key><string>control</string>\n";

  // Emit edges.
  Indent(o, indent) << "<key>edges</key>\n";
  ++indent;
  Indent(o, indent) << "<array>\n";
  ++indent;
  for (PathDiagnosticControlFlowPiece::const_iterator I=P.begin(), E=P.end();
       I!=E; ++I) {
    Indent(o, indent) << "<dict>\n";
    ++indent;

    // Make the ranges of the start and end point self-consistent with adjacent edges
    // by forcing to use only the beginning of the range.  This simplifies the layout
    // logic for clients.
    Indent(o, indent) << "<key>start</key>\n";
    SourceRange StartEdge(
        SM.getExpansionLoc(I->getStart().asRange().getBegin()));
    EmitRange(o, SM, Lexer::getAsCharRange(StartEdge, SM, LangOpts), FM,
              indent + 1);

    Indent(o, indent) << "<key>end</key>\n";
    SourceRange EndEdge(SM.getExpansionLoc(I->getEnd().asRange().getBegin()));
    EmitRange(o, SM, Lexer::getAsCharRange(EndEdge, SM, LangOpts), FM,
              indent + 1);

    --indent;
    Indent(o, indent) << "</dict>\n";
  }
  --indent;
  Indent(o, indent) << "</array>\n";
  --indent;

  // Output any helper text.
  const auto &s = P.getString();
  if (!s.empty()) {
    Indent(o, indent) << "<key>alternate</key>";
    EmitString(o, s) << '\n';
  }

  assert(P.getFixits().size() == 0 &&
         "Fixits on constrol flow pieces are not implemented yet!");

  --indent;
  Indent(o, indent) << "</dict>\n";
}

void PlistPrinter::ReportEvent(raw_ostream &o, const PathDiagnosticEventPiece& P,
                               unsigned indent, unsigned depth,
                               bool isKeyEvent) {

  const SourceManager &SM = PP.getSourceManager();

  Indent(o, indent) << "<dict>\n";
  ++indent;

  Indent(o, indent) << "<key>kind</key><string>event</string>\n";

  if (isKeyEvent) {
    Indent(o, indent) << "<key>key_event</key><true/>\n";
  }

  // Output the location.
  FullSourceLoc L = P.getLocation().asLocation();

  Indent(o, indent) << "<key>location</key>\n";
  EmitLocation(o, SM, L, FM, indent);

  // Output the ranges (if any).
  ArrayRef<SourceRange> Ranges = P.getRanges();
  EmitRanges(o, Ranges, indent);

  // Output the call depth.
  Indent(o, indent) << "<key>depth</key>";
  EmitInteger(o, depth) << '\n';

  // Output the text.
  EmitMessage(o, P.getString(), indent);

  // Output the fixits.
  EmitFixits(o, P.getFixits(), indent);

  // Finish up.
  --indent;
  Indent(o, indent); o << "</dict>\n";
}

void PlistPrinter::ReportCall(raw_ostream &o, const PathDiagnosticCallPiece &P,
                              unsigned indent,
                              unsigned depth) {

  if (auto callEnter = P.getCallEnterEvent())
    ReportPiece(o, *callEnter, indent, depth, /*includeControlFlow*/ true,
                P.isLastInMainSourceFile());


  ++depth;

  if (auto callEnterWithinCaller = P.getCallEnterWithinCallerEvent())
    ReportPiece(o, *callEnterWithinCaller, indent, depth,
                /*includeControlFlow*/ true);

  for (PathPieces::const_iterator I = P.path.begin(), E = P.path.end();I!=E;++I)
    ReportPiece(o, **I, indent, depth, /*includeControlFlow*/ true);

  --depth;

  if (auto callExit = P.getCallExitEvent())
    ReportPiece(o, *callExit, indent, depth, /*includeControlFlow*/ true);

  assert(P.getFixits().size() == 0 &&
         "Fixits on call pieces are not implemented yet!");
}

void PlistPrinter::ReportMacroSubPieces(raw_ostream &o,
                                        const PathDiagnosticMacroPiece& P,
                                        unsigned indent, unsigned depth) {
  MacroPieces.push_back(&P);

  for (const auto &SubPiece : P.subPieces) {
    ReportPiece(o, *SubPiece, indent, depth, /*includeControlFlow*/ false);
  }

  assert(P.getFixits().size() == 0 &&
         "Fixits on constrol flow pieces are not implemented yet!");
}

void PlistPrinter::ReportMacroExpansions(raw_ostream &o, unsigned indent) {

  for (const PathDiagnosticMacroPiece *P : MacroPieces) {
    const SourceManager &SM = PP.getSourceManager();

    SourceLocation MacroExpansionLoc =
        P->getLocation().asLocation().getExpansionLoc();

    const std::optional<StringRef> MacroName =
        MacroExpansions.getOriginalText(MacroExpansionLoc);
    const std::optional<StringRef> ExpansionText =
        getExpandedMacro(MacroExpansionLoc, CTU, MacroExpansions, SM);

    if (!MacroName || !ExpansionText)
      continue;

    Indent(o, indent) << "<dict>\n";
    ++indent;

    // Output the location.
    FullSourceLoc L = P->getLocation().asLocation();

    Indent(o, indent) << "<key>location</key>\n";
    EmitLocation(o, SM, L, FM, indent);

    // Output the ranges (if any).
    ArrayRef<SourceRange> Ranges = P->getRanges();
    EmitRanges(o, Ranges, indent);

    // Output the macro name.
    Indent(o, indent) << "<key>name</key>";
    EmitString(o, *MacroName) << '\n';

    // Output what it expands into.
    Indent(o, indent) << "<key>expansion</key>";
    EmitString(o, *ExpansionText) << '\n';

    // Finish up.
    --indent;
    Indent(o, indent);
    o << "</dict>\n";
  }
}

void PlistPrinter::ReportNote(raw_ostream &o, const PathDiagnosticNotePiece& P,
                              unsigned indent) {

  const SourceManager &SM = PP.getSourceManager();

  Indent(o, indent) << "<dict>\n";
  ++indent;

  // Output the location.
  FullSourceLoc L = P.getLocation().asLocation();

  Indent(o, indent) << "<key>location</key>\n";
  EmitLocation(o, SM, L, FM, indent);

  // Output the ranges (if any).
  ArrayRef<SourceRange> Ranges = P.getRanges();
  EmitRanges(o, Ranges, indent);

  // Output the text.
  EmitMessage(o, P.getString(), indent);

  // Output the fixits.
  EmitFixits(o, P.getFixits(), indent);

  // Finish up.
  --indent;
  Indent(o, indent); o << "</dict>\n";
}

void PlistPrinter::ReportPopUp(raw_ostream &o,
                               const PathDiagnosticPopUpPiece &P,
                               unsigned indent) {
  const SourceManager &SM = PP.getSourceManager();

  Indent(o, indent) << "<dict>\n";
  ++indent;

  Indent(o, indent) << "<key>kind</key><string>pop-up</string>\n";

  // Output the location.
  FullSourceLoc L = P.getLocation().asLocation();

  Indent(o, indent) << "<key>location</key>\n";
  EmitLocation(o, SM, L, FM, indent);

  // Output the ranges (if any).
  ArrayRef<SourceRange> Ranges = P.getRanges();
  EmitRanges(o, Ranges, indent);

  // Output the text.
  EmitMessage(o, P.getString(), indent);

  assert(P.getFixits().size() == 0 &&
         "Fixits on pop-up pieces are not implemented yet!");

  // Finish up.
  --indent;
  Indent(o, indent) << "</dict>\n";
}

//===----------------------------------------------------------------------===//
// Static function definitions.
//===----------------------------------------------------------------------===//

/// Print coverage information to output stream @c o.
/// May modify the used list of files @c Fids by inserting new ones.
static void printCoverage(const PathDiagnostic *D,
                          unsigned InputIndentLevel,
                          SmallVectorImpl<FileID> &Fids,
                          FIDMap &FM,
                          llvm::raw_fd_ostream &o) {
  unsigned IndentLevel = InputIndentLevel;

  Indent(o, IndentLevel) << "<key>ExecutedLines</key>\n";
  Indent(o, IndentLevel) << "<dict>\n";
  IndentLevel++;

  // Mapping from file IDs to executed lines.
  const FilesToLineNumsMap &ExecutedLines = D->getExecutedLines();
  for (const auto &[FID, Lines] : ExecutedLines) {
    unsigned FileKey = AddFID(FM, Fids, FID);
    Indent(o, IndentLevel) << "<key>" << FileKey << "</key>\n";
    Indent(o, IndentLevel) << "<array>\n";
    IndentLevel++;
    for (unsigned LineNo : Lines) {
      Indent(o, IndentLevel);
      EmitInteger(o, LineNo) << "\n";
    }
    IndentLevel--;
    Indent(o, IndentLevel) << "</array>\n";
  }
  IndentLevel--;
  Indent(o, IndentLevel) << "</dict>\n";

  assert(IndentLevel == InputIndentLevel);
}

//===----------------------------------------------------------------------===//
// Methods of PlistDiagnostics.
//===----------------------------------------------------------------------===//

PlistDiagnostics::PlistDiagnostics(
    PathDiagnosticConsumerOptions DiagOpts, const std::string &output,
    const Preprocessor &PP, const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions, bool supportsMultipleFiles)
    : DiagOpts(std::move(DiagOpts)), OutputFile(output), PP(PP), CTU(CTU),
      MacroExpansions(MacroExpansions),
      SupportsCrossFileDiagnostics(supportsMultipleFiles) {
  // FIXME: Will be used by a later planned change.
  (void)this->CTU;
}

void ento::createPlistDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &OutputFile, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {

  // TODO: Emit an error here.
  if (OutputFile.empty())
    return;

  C.push_back(new PlistDiagnostics(DiagOpts, OutputFile, PP, CTU,
                                   MacroExpansions,
                                   /*supportsMultipleFiles=*/false));
  createTextMinimalPathDiagnosticConsumer(std::move(DiagOpts), C, OutputFile,
                                          PP, CTU, MacroExpansions);
}

void ento::createPlistMultiFileDiagnosticConsumer(
    PathDiagnosticConsumerOptions DiagOpts, PathDiagnosticConsumers &C,
    const std::string &OutputFile, const Preprocessor &PP,
    const cross_tu::CrossTranslationUnitContext &CTU,
    const MacroExpansionContext &MacroExpansions) {

  // TODO: Emit an error here.
  if (OutputFile.empty())
    return;

  C.push_back(new PlistDiagnostics(DiagOpts, OutputFile, PP, CTU,
                                   MacroExpansions,
                                   /*supportsMultipleFiles=*/true));
  createTextMinimalPathDiagnosticConsumer(std::move(DiagOpts), C, OutputFile,
                                          PP, CTU, MacroExpansions);
}

void PlistDiagnostics::printBugPath(llvm::raw_ostream &o, const FIDMap &FM,
                                    const PathPieces &Path) {
  PlistPrinter Printer(FM, PP, CTU, MacroExpansions);
  assert(std::is_partitioned(Path.begin(), Path.end(),
                             [](const PathDiagnosticPieceRef &E) {
                               return E->getKind() == PathDiagnosticPiece::Note;
                             }) &&
         "PathDiagnostic is not partitioned so that notes precede the rest");

  PathPieces::const_iterator FirstNonNote = std::partition_point(
      Path.begin(), Path.end(), [](const PathDiagnosticPieceRef &E) {
        return E->getKind() == PathDiagnosticPiece::Note;
      });

  PathPieces::const_iterator I = Path.begin();

  if (FirstNonNote != Path.begin()) {
    o << "   <key>notes</key>\n"
         "   <array>\n";

    for (; I != FirstNonNote; ++I)
      Printer.ReportDiag(o, **I);

    o << "   </array>\n";
  }

  o << "   <key>path</key>\n";

  o << "   <array>\n";

  for (const auto &Piece : llvm::make_range(I, Path.end()))
    Printer.ReportDiag(o, *Piece);

  o << "   </array>\n";

  if (!DiagOpts.ShouldDisplayMacroExpansions)
    return;

  o << "   <key>macro_expansions</key>\n"
       "   <array>\n";
  Printer.ReportMacroExpansions(o, /* indent */ 4);
  o << "   </array>\n";
}

void PlistDiagnostics::FlushDiagnosticsImpl(
                                    std::vector<const PathDiagnostic *> &Diags,
                                    FilesMade *filesMade) {
  // Build up a set of FIDs that we use by scanning the locations and
  // ranges of the diagnostics.
  FIDMap FM;
  SmallVector<FileID, 10> Fids;
  const SourceManager& SM = PP.getSourceManager();
  const LangOptions &LangOpts = PP.getLangOpts();

  auto AddPieceFID = [&FM, &Fids, &SM](const PathDiagnosticPiece &Piece) {
    AddFID(FM, Fids, SM, Piece.getLocation().asLocation());
    ArrayRef<SourceRange> Ranges = Piece.getRanges();
    for (const SourceRange &Range : Ranges) {
      AddFID(FM, Fids, SM, Range.getBegin());
      AddFID(FM, Fids, SM, Range.getEnd());
    }
  };

  for (const PathDiagnostic *D : Diags) {

    SmallVector<const PathPieces *, 5> WorkList;
    WorkList.push_back(&D->path);

    while (!WorkList.empty()) {
      const PathPieces &Path = *WorkList.pop_back_val();

      for (const auto &Iter : Path) {
        const PathDiagnosticPiece &Piece = *Iter;
        AddPieceFID(Piece);

        if (const PathDiagnosticCallPiece *Call =
                dyn_cast<PathDiagnosticCallPiece>(&Piece)) {
          if (auto CallEnterWithin = Call->getCallEnterWithinCallerEvent())
            AddPieceFID(*CallEnterWithin);

          if (auto CallEnterEvent = Call->getCallEnterEvent())
            AddPieceFID(*CallEnterEvent);

          WorkList.push_back(&Call->path);
        } else if (const PathDiagnosticMacroPiece *Macro =
                       dyn_cast<PathDiagnosticMacroPiece>(&Piece)) {
          WorkList.push_back(&Macro->subPieces);
        }
      }
    }
  }

  // Open the file.
  std::error_code EC;
  llvm::raw_fd_ostream o(OutputFile, EC, llvm::sys::fs::OF_TextWithCRLF);
  if (EC) {
    llvm::errs() << "warning: could not create file: " << EC.message() << '\n';
    return;
  }

  EmitPlistHeader(o);

  // Write the root object: a <dict> containing...
  //  - "clang_version", the string representation of clang version
  //  - "files", an <array> mapping from FIDs to file names
  //  - "diagnostics", an <array> containing the path diagnostics
  o << "<dict>\n" <<
       " <key>clang_version</key>\n";
  EmitString(o, getClangFullVersion()) << '\n';
  o << " <key>diagnostics</key>\n"
       " <array>\n";

  for (std::vector<const PathDiagnostic*>::iterator DI=Diags.begin(),
       DE = Diags.end(); DI!=DE; ++DI) {

    o << "  <dict>\n";

    const PathDiagnostic *D = *DI;
    printBugPath(o, FM, D->path);

    // Output the bug type and bug category.
    o << "   <key>description</key>";
    EmitString(o, D->getShortDescription()) << '\n';
    o << "   <key>category</key>";
    EmitString(o, D->getCategory()) << '\n';
    o << "   <key>type</key>";
    EmitString(o, D->getBugType()) << '\n';
    o << "   <key>check_name</key>";
    EmitString(o, D->getCheckerName()) << '\n';

    o << "   <!-- This hash is experimental and going to change! -->\n";
    o << "   <key>issue_hash_content_of_line_in_context</key>";
    PathDiagnosticLocation UPDLoc = D->getUniqueingLoc();
    FullSourceLoc L(SM.getExpansionLoc(UPDLoc.isValid()
                                            ? UPDLoc.asLocation()
                                            : D->getLocation().asLocation()),
                    SM);
    const Decl *DeclWithIssue = D->getDeclWithIssue();
    EmitString(o, getIssueHash(L, D->getCheckerName(), D->getBugType(),
                               DeclWithIssue, LangOpts))
        << '\n';

    // Output information about the semantic context where
    // the issue occurred.
    if (const Decl *DeclWithIssue = D->getDeclWithIssue()) {
      // FIXME: handle blocks, which have no name.
      if (const NamedDecl *ND = dyn_cast<NamedDecl>(DeclWithIssue)) {
        StringRef declKind;
        switch (ND->getKind()) {
          case Decl::CXXRecord:
            declKind = "C++ class";
            break;
          case Decl::CXXMethod:
            declKind = "C++ method";
            break;
          case Decl::ObjCMethod:
            declKind = "Objective-C method";
            break;
          case Decl::Function:
            declKind = "function";
            break;
          default:
            break;
        }
        if (!declKind.empty()) {
          const std::string &declName = ND->getDeclName().getAsString();
          o << "  <key>issue_context_kind</key>";
          EmitString(o, declKind) << '\n';
          o << "  <key>issue_context</key>";
          EmitString(o, declName) << '\n';
        }

        // Output the bug hash for issue unique-ing. Currently, it's just an
        // offset from the beginning of the function.
        if (const Stmt *Body = DeclWithIssue->getBody()) {

          // If the bug uniqueing location exists, use it for the hash.
          // For example, this ensures that two leaks reported on the same line
          // will have different issue_hashes and that the hash will identify
          // the leak location even after code is added between the allocation
          // site and the end of scope (leak report location).
          if (UPDLoc.isValid()) {
            FullSourceLoc UFunL(
                SM.getExpansionLoc(
                    D->getUniqueingDecl()->getBody()->getBeginLoc()),
                SM);
            o << "  <key>issue_hash_function_offset</key><string>"
              << L.getExpansionLineNumber() - UFunL.getExpansionLineNumber()
              << "</string>\n";

          // Otherwise, use the location on which the bug is reported.
          } else {
            FullSourceLoc FunL(SM.getExpansionLoc(Body->getBeginLoc()), SM);
            o << "  <key>issue_hash_function_offset</key><string>"
              << L.getExpansionLineNumber() - FunL.getExpansionLineNumber()
              << "</string>\n";
          }

        }
      }
    }

    // Output the location of the bug.
    o << "  <key>location</key>\n";
    EmitLocation(o, SM, D->getLocation().asLocation(), FM, 2);

    // Output the diagnostic to the sub-diagnostic client, if any.
    if (!filesMade->empty()) {
      StringRef lastName;
      PDFileEntry::ConsumerFiles *files = filesMade->getFiles(*D);
      if (files) {
        for (PDFileEntry::ConsumerFiles::const_iterator CI = files->begin(),
                CE = files->end(); CI != CE; ++CI) {
          StringRef newName = CI->first;
          if (newName != lastName) {
            if (!lastName.empty()) {
              o << "  </array>\n";
            }
            lastName = newName;
            o <<  "  <key>" << lastName << "_files</key>\n";
            o << "  <array>\n";
          }
          o << "   <string>" << CI->second << "</string>\n";
        }
        o << "  </array>\n";
      }
    }

    printCoverage(D, /*IndentLevel=*/2, Fids, FM, o);

    // Close up the entry.
    o << "  </dict>\n";
  }

  o << " </array>\n";

  o << " <key>files</key>\n"
       " <array>\n";
  for (FileID FID : Fids)
    EmitString(o << "  ", SM.getFileEntryRefForID(FID)->getName()) << '\n';
  o << " </array>\n";

  if (llvm::AreStatisticsEnabled() && DiagOpts.ShouldSerializeStats) {
    o << " <key>statistics</key>\n";
    std::string stats;
    llvm::raw_string_ostream os(stats);
    llvm::PrintStatisticsJSON(os);
    os.flush();
    EmitString(o, html::EscapeText(stats)) << '\n';
  }

  // Finish.
  o << "</dict>\n</plist>\n";
}

//===----------------------------------------------------------------------===//
// Definitions of helper functions and methods for expanding macros.
//===----------------------------------------------------------------------===//

static std::optional<StringRef>
getExpandedMacro(SourceLocation MacroExpansionLoc,
                 const cross_tu::CrossTranslationUnitContext &CTU,
                 const MacroExpansionContext &MacroExpansions,
                 const SourceManager &SM) {
  if (auto CTUMacroExpCtx =
          CTU.getMacroExpansionContextForSourceLocation(MacroExpansionLoc)) {
    return CTUMacroExpCtx->getExpandedText(MacroExpansionLoc);
  }
  return MacroExpansions.getExpandedText(MacroExpansionLoc);
}
