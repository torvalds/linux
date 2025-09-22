//===- DiagnosticRenderer.cpp - Diagnostic Pretty-Printing ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/DiagnosticRenderer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Edit/Commit.h"
#include "clang/Edit/EditedSource.h"
#include "clang/Edit/EditsReceiver.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

using namespace clang;

DiagnosticRenderer::DiagnosticRenderer(const LangOptions &LangOpts,
                                       DiagnosticOptions *DiagOpts)
    : LangOpts(LangOpts), DiagOpts(DiagOpts), LastLevel() {}

DiagnosticRenderer::~DiagnosticRenderer() = default;

namespace {

class FixitReceiver : public edit::EditsReceiver {
  SmallVectorImpl<FixItHint> &MergedFixits;

public:
  FixitReceiver(SmallVectorImpl<FixItHint> &MergedFixits)
      : MergedFixits(MergedFixits) {}

  void insert(SourceLocation loc, StringRef text) override {
    MergedFixits.push_back(FixItHint::CreateInsertion(loc, text));
  }

  void replace(CharSourceRange range, StringRef text) override {
    MergedFixits.push_back(FixItHint::CreateReplacement(range, text));
  }
};

} // namespace

static void mergeFixits(ArrayRef<FixItHint> FixItHints,
                        const SourceManager &SM, const LangOptions &LangOpts,
                        SmallVectorImpl<FixItHint> &MergedFixits) {
  edit::Commit commit(SM, LangOpts);
  for (const auto &Hint : FixItHints)
    if (Hint.CodeToInsert.empty()) {
      if (Hint.InsertFromRange.isValid())
        commit.insertFromRange(Hint.RemoveRange.getBegin(),
                           Hint.InsertFromRange, /*afterToken=*/false,
                           Hint.BeforePreviousInsertions);
      else
        commit.remove(Hint.RemoveRange);
    } else {
      if (Hint.RemoveRange.isTokenRange() ||
          Hint.RemoveRange.getBegin() != Hint.RemoveRange.getEnd())
        commit.replace(Hint.RemoveRange, Hint.CodeToInsert);
      else
        commit.insert(Hint.RemoveRange.getBegin(), Hint.CodeToInsert,
                    /*afterToken=*/false, Hint.BeforePreviousInsertions);
    }

  edit::EditedSource Editor(SM, LangOpts);
  if (Editor.commit(commit)) {
    FixitReceiver Rec(MergedFixits);
    Editor.applyRewrites(Rec);
  }
}

void DiagnosticRenderer::emitDiagnostic(FullSourceLoc Loc,
                                        DiagnosticsEngine::Level Level,
                                        StringRef Message,
                                        ArrayRef<CharSourceRange> Ranges,
                                        ArrayRef<FixItHint> FixItHints,
                                        DiagOrStoredDiag D) {
  assert(Loc.hasManager() || Loc.isInvalid());

  beginDiagnostic(D, Level);

  if (!Loc.isValid())
    // If we have no source location, just emit the diagnostic message.
    emitDiagnosticMessage(Loc, PresumedLoc(), Level, Message, Ranges, D);
  else {
    // Get the ranges into a local array we can hack on.
    SmallVector<CharSourceRange, 20> MutableRanges(Ranges.begin(),
                                                   Ranges.end());

    SmallVector<FixItHint, 8> MergedFixits;
    if (!FixItHints.empty()) {
      mergeFixits(FixItHints, Loc.getManager(), LangOpts, MergedFixits);
      FixItHints = MergedFixits;
    }

    for (const auto &Hint : FixItHints)
      if (Hint.RemoveRange.isValid())
        MutableRanges.push_back(Hint.RemoveRange);

    FullSourceLoc UnexpandedLoc = Loc;

    // Find the ultimate expansion location for the diagnostic.
    Loc = Loc.getFileLoc();

    PresumedLoc PLoc = Loc.getPresumedLoc(DiagOpts->ShowPresumedLoc);

    // First, if this diagnostic is not in the main file, print out the
    // "included from" lines.
    emitIncludeStack(Loc, PLoc, Level);

    // Next, emit the actual diagnostic message and caret.
    emitDiagnosticMessage(Loc, PLoc, Level, Message, Ranges, D);
    emitCaret(Loc, Level, MutableRanges, FixItHints);

    // If this location is within a macro, walk from UnexpandedLoc up to Loc
    // and produce a macro backtrace.
    if (UnexpandedLoc.isValid() && UnexpandedLoc.isMacroID()) {
      emitMacroExpansions(UnexpandedLoc, Level, MutableRanges, FixItHints);
    }
  }

  LastLoc = Loc;
  LastLevel = Level;

  endDiagnostic(D, Level);
}

void DiagnosticRenderer::emitStoredDiagnostic(StoredDiagnostic &Diag) {
  emitDiagnostic(Diag.getLocation(), Diag.getLevel(), Diag.getMessage(),
                 Diag.getRanges(), Diag.getFixIts(),
                 &Diag);
}

void DiagnosticRenderer::emitBasicNote(StringRef Message) {
  emitDiagnosticMessage(FullSourceLoc(), PresumedLoc(), DiagnosticsEngine::Note,
                        Message, std::nullopt, DiagOrStoredDiag());
}

/// Prints an include stack when appropriate for a particular
/// diagnostic level and location.
///
/// This routine handles all the logic of suppressing particular include
/// stacks (such as those for notes) and duplicate include stacks when
/// repeated warnings occur within the same file. It also handles the logic
/// of customizing the formatting and display of the include stack.
///
/// \param Loc   The diagnostic location.
/// \param PLoc  The presumed location of the diagnostic location.
/// \param Level The diagnostic level of the message this stack pertains to.
void DiagnosticRenderer::emitIncludeStack(FullSourceLoc Loc, PresumedLoc PLoc,
                                          DiagnosticsEngine::Level Level) {
  FullSourceLoc IncludeLoc =
      PLoc.isInvalid() ? FullSourceLoc()
                       : FullSourceLoc(PLoc.getIncludeLoc(), Loc.getManager());

  // Skip redundant include stacks altogether.
  if (LastIncludeLoc == IncludeLoc)
    return;

  LastIncludeLoc = IncludeLoc;

  if (!DiagOpts->ShowNoteIncludeStack && Level == DiagnosticsEngine::Note)
    return;

  if (IncludeLoc.isValid())
    emitIncludeStackRecursively(IncludeLoc);
  else {
    emitModuleBuildStack(Loc.getManager());
    emitImportStack(Loc);
  }
}

/// Helper to recursively walk up the include stack and print each layer
/// on the way back down.
void DiagnosticRenderer::emitIncludeStackRecursively(FullSourceLoc Loc) {
  if (Loc.isInvalid()) {
    emitModuleBuildStack(Loc.getManager());
    return;
  }

  PresumedLoc PLoc = Loc.getPresumedLoc(DiagOpts->ShowPresumedLoc);
  if (PLoc.isInvalid())
    return;

  // If this source location was imported from a module, print the module
  // import stack rather than the
  // FIXME: We want submodule granularity here.
  std::pair<FullSourceLoc, StringRef> Imported = Loc.getModuleImportLoc();
  if (!Imported.second.empty()) {
    // This location was imported by a module. Emit the module import stack.
    emitImportStackRecursively(Imported.first, Imported.second);
    return;
  }

  // Emit the other include frames first.
  emitIncludeStackRecursively(
      FullSourceLoc(PLoc.getIncludeLoc(), Loc.getManager()));

  // Emit the inclusion text/note.
  emitIncludeLocation(Loc, PLoc);
}

/// Emit the module import stack associated with the current location.
void DiagnosticRenderer::emitImportStack(FullSourceLoc Loc) {
  if (Loc.isInvalid()) {
    emitModuleBuildStack(Loc.getManager());
    return;
  }

  std::pair<FullSourceLoc, StringRef> NextImportLoc = Loc.getModuleImportLoc();
  emitImportStackRecursively(NextImportLoc.first, NextImportLoc.second);
}

/// Helper to recursively walk up the import stack and print each layer
/// on the way back down.
void DiagnosticRenderer::emitImportStackRecursively(FullSourceLoc Loc,
                                                    StringRef ModuleName) {
  if (ModuleName.empty()) {
    return;
  }

  PresumedLoc PLoc = Loc.getPresumedLoc(DiagOpts->ShowPresumedLoc);

  // Emit the other import frames first.
  std::pair<FullSourceLoc, StringRef> NextImportLoc = Loc.getModuleImportLoc();
  emitImportStackRecursively(NextImportLoc.first, NextImportLoc.second);

  // Emit the inclusion text/note.
  emitImportLocation(Loc, PLoc, ModuleName);
}

/// Emit the module build stack, for cases where a module is (re-)built
/// on demand.
void DiagnosticRenderer::emitModuleBuildStack(const SourceManager &SM) {
  ModuleBuildStack Stack = SM.getModuleBuildStack();
  for (const auto &I : Stack) {
    emitBuildingModuleLocation(I.second, I.second.getPresumedLoc(
                                              DiagOpts->ShowPresumedLoc),
                               I.first);
  }
}

/// A recursive function to trace all possible backtrace locations
/// to match the \p CaretLocFileID.
static SourceLocation
retrieveMacroLocation(SourceLocation Loc, FileID MacroFileID,
                      FileID CaretFileID,
                      const SmallVectorImpl<FileID> &CommonArgExpansions,
                      bool IsBegin, const SourceManager *SM,
                      bool &IsTokenRange) {
  assert(SM->getFileID(Loc) == MacroFileID);
  if (MacroFileID == CaretFileID)
    return Loc;
  if (!Loc.isMacroID())
    return {};

  CharSourceRange MacroRange, MacroArgRange;

  if (SM->isMacroArgExpansion(Loc)) {
    // Only look at the immediate spelling location of this macro argument if
    // the other location in the source range is also present in that expansion.
    if (std::binary_search(CommonArgExpansions.begin(),
                           CommonArgExpansions.end(), MacroFileID))
      MacroRange =
          CharSourceRange(SM->getImmediateSpellingLoc(Loc), IsTokenRange);
    MacroArgRange = SM->getImmediateExpansionRange(Loc);
  } else {
    MacroRange = SM->getImmediateExpansionRange(Loc);
    MacroArgRange =
        CharSourceRange(SM->getImmediateSpellingLoc(Loc), IsTokenRange);
  }

  SourceLocation MacroLocation =
      IsBegin ? MacroRange.getBegin() : MacroRange.getEnd();
  if (MacroLocation.isValid()) {
    MacroFileID = SM->getFileID(MacroLocation);
    bool TokenRange = IsBegin ? IsTokenRange : MacroRange.isTokenRange();
    MacroLocation =
        retrieveMacroLocation(MacroLocation, MacroFileID, CaretFileID,
                              CommonArgExpansions, IsBegin, SM, TokenRange);
    if (MacroLocation.isValid()) {
      IsTokenRange = TokenRange;
      return MacroLocation;
    }
  }

  // If we moved the end of the range to an expansion location, we now have
  // a range of the same kind as the expansion range.
  if (!IsBegin)
    IsTokenRange = MacroArgRange.isTokenRange();

  SourceLocation MacroArgLocation =
      IsBegin ? MacroArgRange.getBegin() : MacroArgRange.getEnd();
  MacroFileID = SM->getFileID(MacroArgLocation);
  return retrieveMacroLocation(MacroArgLocation, MacroFileID, CaretFileID,
                               CommonArgExpansions, IsBegin, SM, IsTokenRange);
}

/// Walk up the chain of macro expansions and collect the FileIDs identifying the
/// expansions.
static void getMacroArgExpansionFileIDs(SourceLocation Loc,
                                        SmallVectorImpl<FileID> &IDs,
                                        bool IsBegin, const SourceManager *SM) {
  while (Loc.isMacroID()) {
    if (SM->isMacroArgExpansion(Loc)) {
      IDs.push_back(SM->getFileID(Loc));
      Loc = SM->getImmediateSpellingLoc(Loc);
    } else {
      auto ExpRange = SM->getImmediateExpansionRange(Loc);
      Loc = IsBegin ? ExpRange.getBegin() : ExpRange.getEnd();
    }
  }
}

/// Collect the expansions of the begin and end locations and compute the set
/// intersection. Produces a sorted vector of FileIDs in CommonArgExpansions.
static void computeCommonMacroArgExpansionFileIDs(
    SourceLocation Begin, SourceLocation End, const SourceManager *SM,
    SmallVectorImpl<FileID> &CommonArgExpansions) {
  SmallVector<FileID, 4> BeginArgExpansions;
  SmallVector<FileID, 4> EndArgExpansions;
  getMacroArgExpansionFileIDs(Begin, BeginArgExpansions, /*IsBegin=*/true, SM);
  getMacroArgExpansionFileIDs(End, EndArgExpansions, /*IsBegin=*/false, SM);
  llvm::sort(BeginArgExpansions);
  llvm::sort(EndArgExpansions);
  std::set_intersection(BeginArgExpansions.begin(), BeginArgExpansions.end(),
                        EndArgExpansions.begin(), EndArgExpansions.end(),
                        std::back_inserter(CommonArgExpansions));
}

// Helper function to fix up source ranges.  It takes in an array of ranges,
// and outputs an array of ranges where we want to draw the range highlighting
// around the location specified by CaretLoc.
//
// To find locations which correspond to the caret, we crawl the macro caller
// chain for the beginning and end of each range.  If the caret location
// is in a macro expansion, we search each chain for a location
// in the same expansion as the caret; otherwise, we crawl to the top of
// each chain. Two locations are part of the same macro expansion
// iff the FileID is the same.
static void
mapDiagnosticRanges(FullSourceLoc CaretLoc, ArrayRef<CharSourceRange> Ranges,
                    SmallVectorImpl<CharSourceRange> &SpellingRanges) {
  FileID CaretLocFileID = CaretLoc.getFileID();

  const SourceManager *SM = &CaretLoc.getManager();

  for (const auto &Range : Ranges) {
    if (Range.isInvalid())
      continue;

    SourceLocation Begin = Range.getBegin(), End = Range.getEnd();
    bool IsTokenRange = Range.isTokenRange();

    FileID BeginFileID = SM->getFileID(Begin);
    FileID EndFileID = SM->getFileID(End);

    // Find the common parent for the beginning and end of the range.

    // First, crawl the expansion chain for the beginning of the range.
    llvm::SmallDenseMap<FileID, SourceLocation> BeginLocsMap;
    while (Begin.isMacroID() && BeginFileID != EndFileID) {
      BeginLocsMap[BeginFileID] = Begin;
      Begin = SM->getImmediateExpansionRange(Begin).getBegin();
      BeginFileID = SM->getFileID(Begin);
    }

    // Then, crawl the expansion chain for the end of the range.
    if (BeginFileID != EndFileID) {
      while (End.isMacroID() && !BeginLocsMap.count(EndFileID)) {
        auto Exp = SM->getImmediateExpansionRange(End);
        IsTokenRange = Exp.isTokenRange();
        End = Exp.getEnd();
        EndFileID = SM->getFileID(End);
      }
      if (End.isMacroID()) {
        Begin = BeginLocsMap[EndFileID];
        BeginFileID = EndFileID;
      }
    }

    // There is a chance that begin or end is invalid here, for example if
    // specific compile error is reported.
    // It is possible that the FileID's do not match, if one comes from an
    // included file. In this case we can not produce a meaningful source range.
    if (Begin.isInvalid() || End.isInvalid() || BeginFileID != EndFileID)
      continue;

    // Do the backtracking.
    SmallVector<FileID, 4> CommonArgExpansions;
    computeCommonMacroArgExpansionFileIDs(Begin, End, SM, CommonArgExpansions);
    Begin = retrieveMacroLocation(Begin, BeginFileID, CaretLocFileID,
                                  CommonArgExpansions, /*IsBegin=*/true, SM,
                                  IsTokenRange);
    End = retrieveMacroLocation(End, BeginFileID, CaretLocFileID,
                                CommonArgExpansions, /*IsBegin=*/false, SM,
                                IsTokenRange);
    if (Begin.isInvalid() || End.isInvalid()) continue;

    // Return the spelling location of the beginning and end of the range.
    Begin = SM->getSpellingLoc(Begin);
    End = SM->getSpellingLoc(End);

    SpellingRanges.push_back(CharSourceRange(SourceRange(Begin, End),
                                             IsTokenRange));
  }
}

void DiagnosticRenderer::emitCaret(FullSourceLoc Loc,
                                   DiagnosticsEngine::Level Level,
                                   ArrayRef<CharSourceRange> Ranges,
                                   ArrayRef<FixItHint> Hints) {
  SmallVector<CharSourceRange, 4> SpellingRanges;
  mapDiagnosticRanges(Loc, Ranges, SpellingRanges);
  emitCodeContext(Loc, Level, SpellingRanges, Hints);
}

/// A helper function for emitMacroExpansion to print the
/// macro expansion message
void DiagnosticRenderer::emitSingleMacroExpansion(
    FullSourceLoc Loc, DiagnosticsEngine::Level Level,
    ArrayRef<CharSourceRange> Ranges) {
  // Find the spelling location for the macro definition. We must use the
  // spelling location here to avoid emitting a macro backtrace for the note.
  FullSourceLoc SpellingLoc = Loc.getSpellingLoc();

  // Map the ranges into the FileID of the diagnostic location.
  SmallVector<CharSourceRange, 4> SpellingRanges;
  mapDiagnosticRanges(Loc, Ranges, SpellingRanges);

  SmallString<100> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  StringRef MacroName = Lexer::getImmediateMacroNameForDiagnostics(
      Loc, Loc.getManager(), LangOpts);
  if (MacroName.empty())
    Message << "expanded from here";
  else
    Message << "expanded from macro '" << MacroName << "'";

  emitDiagnostic(SpellingLoc, DiagnosticsEngine::Note, Message.str(),
                 SpellingRanges, std::nullopt);
}

/// Check that the macro argument location of Loc starts with ArgumentLoc.
/// The starting location of the macro expansions is used to differeniate
/// different macro expansions.
static bool checkLocForMacroArgExpansion(SourceLocation Loc,
                                         const SourceManager &SM,
                                         SourceLocation ArgumentLoc) {
  SourceLocation MacroLoc;
  if (SM.isMacroArgExpansion(Loc, &MacroLoc)) {
    if (ArgumentLoc == MacroLoc) return true;
  }

  return false;
}

/// Check if all the locations in the range have the same macro argument
/// expansion, and that the expansion starts with ArgumentLoc.
static bool checkRangeForMacroArgExpansion(CharSourceRange Range,
                                           const SourceManager &SM,
                                           SourceLocation ArgumentLoc) {
  SourceLocation BegLoc = Range.getBegin(), EndLoc = Range.getEnd();
  while (BegLoc != EndLoc) {
    if (!checkLocForMacroArgExpansion(BegLoc, SM, ArgumentLoc))
      return false;
    BegLoc.getLocWithOffset(1);
  }

  return checkLocForMacroArgExpansion(BegLoc, SM, ArgumentLoc);
}

/// A helper function to check if the current ranges are all inside the same
/// macro argument expansion as Loc.
static bool checkRangesForMacroArgExpansion(FullSourceLoc Loc,
                                            ArrayRef<CharSourceRange> Ranges) {
  assert(Loc.isMacroID() && "Must be a macro expansion!");

  SmallVector<CharSourceRange, 4> SpellingRanges;
  mapDiagnosticRanges(Loc, Ranges, SpellingRanges);

  // Count all valid ranges.
  unsigned ValidCount =
      llvm::count_if(Ranges, [](const auto &R) { return R.isValid(); });

  if (ValidCount > SpellingRanges.size())
    return false;

  // To store the source location of the argument location.
  FullSourceLoc ArgumentLoc;

  // Set the ArgumentLoc to the beginning location of the expansion of Loc
  // so to check if the ranges expands to the same beginning location.
  if (!Loc.isMacroArgExpansion(&ArgumentLoc))
    return false;

  for (const auto &Range : SpellingRanges)
    if (!checkRangeForMacroArgExpansion(Range, Loc.getManager(), ArgumentLoc))
      return false;

  return true;
}

/// Recursively emit notes for each macro expansion and caret
/// diagnostics where appropriate.
///
/// Walks up the macro expansion stack printing expansion notes, the code
/// snippet, caret, underlines and FixItHint display as appropriate at each
/// level.
///
/// \param Loc The location for this caret.
/// \param Level The diagnostic level currently being emitted.
/// \param Ranges The underlined ranges for this code snippet.
/// \param Hints The FixIt hints active for this diagnostic.
void DiagnosticRenderer::emitMacroExpansions(FullSourceLoc Loc,
                                             DiagnosticsEngine::Level Level,
                                             ArrayRef<CharSourceRange> Ranges,
                                             ArrayRef<FixItHint> Hints) {
  assert(Loc.isValid() && "must have a valid source location here");
  const SourceManager &SM = Loc.getManager();
  SourceLocation L = Loc;

  // Produce a stack of macro backtraces.
  SmallVector<SourceLocation, 8> LocationStack;
  unsigned IgnoredEnd = 0;
  while (L.isMacroID()) {
    // If this is the expansion of a macro argument, point the caret at the
    // use of the argument in the definition of the macro, not the expansion.
    if (SM.isMacroArgExpansion(L))
      LocationStack.push_back(SM.getImmediateExpansionRange(L).getBegin());
    else
      LocationStack.push_back(L);

    if (checkRangesForMacroArgExpansion(FullSourceLoc(L, SM), Ranges))
      IgnoredEnd = LocationStack.size();

    L = SM.getImmediateMacroCallerLoc(L);

    // Once the location no longer points into a macro, try stepping through
    // the last found location.  This sometimes produces additional useful
    // backtraces.
    if (L.isFileID())
      L = SM.getImmediateMacroCallerLoc(LocationStack.back());
    assert(L.isValid() && "must have a valid source location here");
  }

  LocationStack.erase(LocationStack.begin(),
                      LocationStack.begin() + IgnoredEnd);

  unsigned MacroDepth = LocationStack.size();
  unsigned MacroLimit = DiagOpts->MacroBacktraceLimit;
  if (MacroDepth <= MacroLimit || MacroLimit == 0) {
    for (auto I = LocationStack.rbegin(), E = LocationStack.rend();
         I != E; ++I)
      emitSingleMacroExpansion(FullSourceLoc(*I, SM), Level, Ranges);
    return;
  }

  unsigned MacroStartMessages = MacroLimit / 2;
  unsigned MacroEndMessages = MacroLimit / 2 + MacroLimit % 2;

  for (auto I = LocationStack.rbegin(),
            E = LocationStack.rbegin() + MacroStartMessages;
       I != E; ++I)
    emitSingleMacroExpansion(FullSourceLoc(*I, SM), Level, Ranges);

  SmallString<200> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  Message << "(skipping " << (MacroDepth - MacroLimit)
          << " expansions in backtrace; use -fmacro-backtrace-limit=0 to "
             "see all)";
  emitBasicNote(Message.str());

  for (auto I = LocationStack.rend() - MacroEndMessages,
            E = LocationStack.rend();
       I != E; ++I)
    emitSingleMacroExpansion(FullSourceLoc(*I, SM), Level, Ranges);
}

DiagnosticNoteRenderer::~DiagnosticNoteRenderer() = default;

void DiagnosticNoteRenderer::emitIncludeLocation(FullSourceLoc Loc,
                                                 PresumedLoc PLoc) {
  // Generate a note indicating the include location.
  SmallString<200> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  Message << "in file included from " << PLoc.getFilename() << ':'
          << PLoc.getLine() << ":";
  emitNote(Loc, Message.str());
}

void DiagnosticNoteRenderer::emitImportLocation(FullSourceLoc Loc,
                                                PresumedLoc PLoc,
                                                StringRef ModuleName) {
  // Generate a note indicating the include location.
  SmallString<200> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  Message << "in module '" << ModuleName;
  if (PLoc.isValid())
    Message << "' imported from " << PLoc.getFilename() << ':'
            << PLoc.getLine();
  Message << ":";
  emitNote(Loc, Message.str());
}

void DiagnosticNoteRenderer::emitBuildingModuleLocation(FullSourceLoc Loc,
                                                        PresumedLoc PLoc,
                                                        StringRef ModuleName) {
  // Generate a note indicating the include location.
  SmallString<200> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  if (PLoc.isValid())
    Message << "while building module '" << ModuleName << "' imported from "
            << PLoc.getFilename() << ':' << PLoc.getLine() << ":";
  else
    Message << "while building module '" << ModuleName << "':";
  emitNote(Loc, Message.str());
}
