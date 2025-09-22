//===-- TransformActions.cpp - Migration to ARC mode ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/DenseSet.h"
#include <map>
using namespace clang;
using namespace arcmt;

namespace {

/// Collects transformations and merges them before applying them with
/// with applyRewrites(). E.g. if the same source range
/// is requested to be removed twice, only one rewriter remove will be invoked.
/// Rewrites happen in "transactions"; if one rewrite in the transaction cannot
/// be done (e.g. it resides in a macro) all rewrites in the transaction are
/// aborted.
/// FIXME: "Transactional" rewrites support should be baked in the Rewriter.
class TransformActionsImpl {
  CapturedDiagList &CapturedDiags;
  ASTContext &Ctx;
  Preprocessor &PP;

  bool IsInTransaction;

  enum ActionKind {
    Act_Insert, Act_InsertAfterToken,
    Act_Remove, Act_RemoveStmt,
    Act_Replace, Act_ReplaceText,
    Act_IncreaseIndentation,
    Act_ClearDiagnostic
  };

  struct ActionData {
    ActionKind Kind;
    SourceLocation Loc;
    SourceRange R1, R2;
    StringRef Text1, Text2;
    Stmt *S;
    SmallVector<unsigned, 2> DiagIDs;
  };

  std::vector<ActionData> CachedActions;

  enum RangeComparison {
    Range_Before,
    Range_After,
    Range_Contains,
    Range_Contained,
    Range_ExtendsBegin,
    Range_ExtendsEnd
  };

  /// A range to remove. It is a character range.
  struct CharRange {
    FullSourceLoc Begin, End;

    CharRange(CharSourceRange range, SourceManager &srcMgr, Preprocessor &PP) {
      SourceLocation beginLoc = range.getBegin(), endLoc = range.getEnd();
      assert(beginLoc.isValid() && endLoc.isValid());
      if (range.isTokenRange()) {
        Begin = FullSourceLoc(srcMgr.getExpansionLoc(beginLoc), srcMgr);
        End = FullSourceLoc(getLocForEndOfToken(endLoc, srcMgr, PP), srcMgr);
      } else {
        Begin = FullSourceLoc(srcMgr.getExpansionLoc(beginLoc), srcMgr);
        End = FullSourceLoc(srcMgr.getExpansionLoc(endLoc), srcMgr);
      }
      assert(Begin.isValid() && End.isValid());
    }

    RangeComparison compareWith(const CharRange &RHS) const {
      if (End.isBeforeInTranslationUnitThan(RHS.Begin))
        return Range_Before;
      if (RHS.End.isBeforeInTranslationUnitThan(Begin))
        return Range_After;
      if (!Begin.isBeforeInTranslationUnitThan(RHS.Begin) &&
          !RHS.End.isBeforeInTranslationUnitThan(End))
        return Range_Contained;
      if (Begin.isBeforeInTranslationUnitThan(RHS.Begin) &&
          RHS.End.isBeforeInTranslationUnitThan(End))
        return Range_Contains;
      if (Begin.isBeforeInTranslationUnitThan(RHS.Begin))
        return Range_ExtendsBegin;
      else
        return Range_ExtendsEnd;
    }

    static RangeComparison compare(SourceRange LHS, SourceRange RHS,
                                   SourceManager &SrcMgr, Preprocessor &PP) {
      return CharRange(CharSourceRange::getTokenRange(LHS), SrcMgr, PP)
                  .compareWith(CharRange(CharSourceRange::getTokenRange(RHS),
                                            SrcMgr, PP));
    }
  };

  typedef SmallVector<StringRef, 2> TextsVec;
  typedef std::map<FullSourceLoc, TextsVec, FullSourceLoc::BeforeThanCompare>
      InsertsMap;
  InsertsMap Inserts;
  /// A list of ranges to remove. They are always sorted and they never
  /// intersect with each other.
  std::list<CharRange> Removals;

  llvm::DenseSet<Stmt *> StmtRemovals;

  std::vector<std::pair<CharRange, SourceLocation> > IndentationRanges;

  /// Keeps text passed to transformation methods.
  llvm::StringMap<bool> UniqueText;

public:
  TransformActionsImpl(CapturedDiagList &capturedDiags,
                       ASTContext &ctx, Preprocessor &PP)
    : CapturedDiags(capturedDiags), Ctx(ctx), PP(PP), IsInTransaction(false) { }

  ASTContext &getASTContext() { return Ctx; }

  void startTransaction();
  bool commitTransaction();
  void abortTransaction();

  bool isInTransaction() const { return IsInTransaction; }

  void insert(SourceLocation loc, StringRef text);
  void insertAfterToken(SourceLocation loc, StringRef text);
  void remove(SourceRange range);
  void removeStmt(Stmt *S);
  void replace(SourceRange range, StringRef text);
  void replace(SourceRange range, SourceRange replacementRange);
  void replaceStmt(Stmt *S, StringRef text);
  void replaceText(SourceLocation loc, StringRef text,
                   StringRef replacementText);
  void increaseIndentation(SourceRange range,
                           SourceLocation parentIndent);

  bool clearDiagnostic(ArrayRef<unsigned> IDs, SourceRange range);

  void applyRewrites(TransformActions::RewriteReceiver &receiver);

private:
  bool canInsert(SourceLocation loc);
  bool canInsertAfterToken(SourceLocation loc);
  bool canRemoveRange(SourceRange range);
  bool canReplaceRange(SourceRange range, SourceRange replacementRange);
  bool canReplaceText(SourceLocation loc, StringRef text);

  void commitInsert(SourceLocation loc, StringRef text);
  void commitInsertAfterToken(SourceLocation loc, StringRef text);
  void commitRemove(SourceRange range);
  void commitRemoveStmt(Stmt *S);
  void commitReplace(SourceRange range, SourceRange replacementRange);
  void commitReplaceText(SourceLocation loc, StringRef text,
                         StringRef replacementText);
  void commitIncreaseIndentation(SourceRange range,SourceLocation parentIndent);
  void commitClearDiagnostic(ArrayRef<unsigned> IDs, SourceRange range);

  void addRemoval(CharSourceRange range);
  void addInsertion(SourceLocation loc, StringRef text);

  /// Stores text passed to the transformation methods to keep the string
  /// "alive". Since the vast majority of text will be the same, we also unique
  /// the strings using a StringMap.
  StringRef getUniqueText(StringRef text);

  /// Computes the source location just past the end of the token at
  /// the given source location. If the location points at a macro, the whole
  /// macro expansion is skipped.
  static SourceLocation getLocForEndOfToken(SourceLocation loc,
                                            SourceManager &SM,Preprocessor &PP);
};

} // anonymous namespace

void TransformActionsImpl::startTransaction() {
  assert(!IsInTransaction &&
         "Cannot start a transaction in the middle of another one");
  IsInTransaction = true;
}

bool TransformActionsImpl::commitTransaction() {
  assert(IsInTransaction && "No transaction started");

  if (CachedActions.empty()) {
    IsInTransaction = false;
    return false;
  }

  // Verify that all actions are possible otherwise abort the whole transaction.
  bool AllActionsPossible = true;
  for (unsigned i = 0, e = CachedActions.size(); i != e; ++i) {
    ActionData &act = CachedActions[i];
    switch (act.Kind) {
    case Act_Insert:
      if (!canInsert(act.Loc))
        AllActionsPossible = false;
      break;
    case Act_InsertAfterToken:
      if (!canInsertAfterToken(act.Loc))
        AllActionsPossible = false;
      break;
    case Act_Remove:
      if (!canRemoveRange(act.R1))
        AllActionsPossible = false;
      break;
    case Act_RemoveStmt:
      assert(act.S);
      if (!canRemoveRange(act.S->getSourceRange()))
        AllActionsPossible = false;
      break;
    case Act_Replace:
      if (!canReplaceRange(act.R1, act.R2))
        AllActionsPossible = false;
      break;
    case Act_ReplaceText:
      if (!canReplaceText(act.Loc, act.Text1))
        AllActionsPossible = false;
      break;
    case Act_IncreaseIndentation:
      // This is not important, we don't care if it will fail.
      break;
    case Act_ClearDiagnostic:
      // We are just checking source rewrites.
      break;
    }
    if (!AllActionsPossible)
      break;
  }

  if (!AllActionsPossible) {
    abortTransaction();
    return true;
  }

  for (unsigned i = 0, e = CachedActions.size(); i != e; ++i) {
    ActionData &act = CachedActions[i];
    switch (act.Kind) {
    case Act_Insert:
      commitInsert(act.Loc, act.Text1);
      break;
    case Act_InsertAfterToken:
      commitInsertAfterToken(act.Loc, act.Text1);
      break;
    case Act_Remove:
      commitRemove(act.R1);
      break;
    case Act_RemoveStmt:
      commitRemoveStmt(act.S);
      break;
    case Act_Replace:
      commitReplace(act.R1, act.R2);
      break;
    case Act_ReplaceText:
      commitReplaceText(act.Loc, act.Text1, act.Text2);
      break;
    case Act_IncreaseIndentation:
      commitIncreaseIndentation(act.R1, act.Loc);
      break;
    case Act_ClearDiagnostic:
      commitClearDiagnostic(act.DiagIDs, act.R1);
      break;
    }
  }

  CachedActions.clear();
  IsInTransaction = false;
  return false;
}

void TransformActionsImpl::abortTransaction() {
  assert(IsInTransaction && "No transaction started");
  CachedActions.clear();
  IsInTransaction = false;
}

void TransformActionsImpl::insert(SourceLocation loc, StringRef text) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  text = getUniqueText(text);
  ActionData data;
  data.Kind = Act_Insert;
  data.Loc = loc;
  data.Text1 = text;
  CachedActions.push_back(data);
}

void TransformActionsImpl::insertAfterToken(SourceLocation loc, StringRef text) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  text = getUniqueText(text);
  ActionData data;
  data.Kind = Act_InsertAfterToken;
  data.Loc = loc;
  data.Text1 = text;
  CachedActions.push_back(data);
}

void TransformActionsImpl::remove(SourceRange range) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  ActionData data;
  data.Kind = Act_Remove;
  data.R1 = range;
  CachedActions.push_back(data);
}

void TransformActionsImpl::removeStmt(Stmt *S) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  ActionData data;
  data.Kind = Act_RemoveStmt;
  if (auto *E = dyn_cast<Expr>(S))
    S = E->IgnoreImplicit(); // important for uniquing
  data.S = S;
  CachedActions.push_back(data);
}

void TransformActionsImpl::replace(SourceRange range, StringRef text) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  text = getUniqueText(text);
  remove(range);
  insert(range.getBegin(), text);
}

void TransformActionsImpl::replace(SourceRange range,
                                   SourceRange replacementRange) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  ActionData data;
  data.Kind = Act_Replace;
  data.R1 = range;
  data.R2 = replacementRange;
  CachedActions.push_back(data);
}

void TransformActionsImpl::replaceText(SourceLocation loc, StringRef text,
                                       StringRef replacementText) {
  text = getUniqueText(text);
  replacementText = getUniqueText(replacementText);
  ActionData data;
  data.Kind = Act_ReplaceText;
  data.Loc = loc;
  data.Text1 = text;
  data.Text2 = replacementText;
  CachedActions.push_back(data);
}

void TransformActionsImpl::replaceStmt(Stmt *S, StringRef text) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  text = getUniqueText(text);
  insert(S->getBeginLoc(), text);
  removeStmt(S);
}

void TransformActionsImpl::increaseIndentation(SourceRange range,
                                               SourceLocation parentIndent) {
  if (range.isInvalid()) return;
  assert(IsInTransaction && "Actions only allowed during a transaction");
  ActionData data;
  data.Kind = Act_IncreaseIndentation;
  data.R1 = range;
  data.Loc = parentIndent;
  CachedActions.push_back(data);
}

bool TransformActionsImpl::clearDiagnostic(ArrayRef<unsigned> IDs,
                                           SourceRange range) {
  assert(IsInTransaction && "Actions only allowed during a transaction");
  if (!CapturedDiags.hasDiagnostic(IDs, range))
    return false;

  ActionData data;
  data.Kind = Act_ClearDiagnostic;
  data.R1 = range;
  data.DiagIDs.append(IDs.begin(), IDs.end());
  CachedActions.push_back(data);
  return true;
}

bool TransformActionsImpl::canInsert(SourceLocation loc) {
  if (loc.isInvalid())
    return false;

  SourceManager &SM = Ctx.getSourceManager();
  if (SM.isInSystemHeader(SM.getExpansionLoc(loc)))
    return false;

  if (loc.isFileID())
    return true;
  return PP.isAtStartOfMacroExpansion(loc);
}

bool TransformActionsImpl::canInsertAfterToken(SourceLocation loc) {
  if (loc.isInvalid())
    return false;

  SourceManager &SM = Ctx.getSourceManager();
  if (SM.isInSystemHeader(SM.getExpansionLoc(loc)))
    return false;

  if (loc.isFileID())
    return true;
  return PP.isAtEndOfMacroExpansion(loc);
}

bool TransformActionsImpl::canRemoveRange(SourceRange range) {
  return canInsert(range.getBegin()) && canInsertAfterToken(range.getEnd());
}

bool TransformActionsImpl::canReplaceRange(SourceRange range,
                                           SourceRange replacementRange) {
  return canRemoveRange(range) && canRemoveRange(replacementRange);
}

bool TransformActionsImpl::canReplaceText(SourceLocation loc, StringRef text) {
  if (!canInsert(loc))
    return false;

  SourceManager &SM = Ctx.getSourceManager();
  loc = SM.getExpansionLoc(loc);

  // Break down the source location.
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(loc);

  // Try to load the file buffer.
  bool invalidTemp = false;
  StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp)
    return false;

  return file.substr(locInfo.second).starts_with(text);
}

void TransformActionsImpl::commitInsert(SourceLocation loc, StringRef text) {
  addInsertion(loc, text);
}

void TransformActionsImpl::commitInsertAfterToken(SourceLocation loc,
                                                  StringRef text) {
  addInsertion(getLocForEndOfToken(loc, Ctx.getSourceManager(), PP), text);
}

void TransformActionsImpl::commitRemove(SourceRange range) {
  addRemoval(CharSourceRange::getTokenRange(range));
}

void TransformActionsImpl::commitRemoveStmt(Stmt *S) {
  assert(S);
  if (StmtRemovals.count(S))
    return; // already removed.

  if (Expr *E = dyn_cast<Expr>(S)) {
    commitRemove(E->getSourceRange());
    commitInsert(E->getSourceRange().getBegin(), getARCMTMacroName());
  } else
    commitRemove(S->getSourceRange());

  StmtRemovals.insert(S);
}

void TransformActionsImpl::commitReplace(SourceRange range,
                                         SourceRange replacementRange) {
  RangeComparison comp = CharRange::compare(replacementRange, range,
                                               Ctx.getSourceManager(), PP);
  assert(comp == Range_Contained);
  if (comp != Range_Contained)
    return; // Although we asserted, be extra safe for release build.
  if (range.getBegin() != replacementRange.getBegin())
    addRemoval(CharSourceRange::getCharRange(range.getBegin(),
                                             replacementRange.getBegin()));
  if (replacementRange.getEnd() != range.getEnd())
    addRemoval(CharSourceRange::getTokenRange(
                                  getLocForEndOfToken(replacementRange.getEnd(),
                                                      Ctx.getSourceManager(), PP),
                                  range.getEnd()));
}
void TransformActionsImpl::commitReplaceText(SourceLocation loc,
                                             StringRef text,
                                             StringRef replacementText) {
  SourceManager &SM = Ctx.getSourceManager();
  loc = SM.getExpansionLoc(loc);
  // canReplaceText already checked if loc points at text.
  SourceLocation afterText = loc.getLocWithOffset(text.size());

  addRemoval(CharSourceRange::getCharRange(loc, afterText));
  commitInsert(loc, replacementText);
}

void TransformActionsImpl::commitIncreaseIndentation(SourceRange range,
                                                  SourceLocation parentIndent) {
  SourceManager &SM = Ctx.getSourceManager();
  IndentationRanges.push_back(
                 std::make_pair(CharRange(CharSourceRange::getTokenRange(range),
                                          SM, PP),
                                SM.getExpansionLoc(parentIndent)));
}

void TransformActionsImpl::commitClearDiagnostic(ArrayRef<unsigned> IDs,
                                                 SourceRange range) {
  CapturedDiags.clearDiagnostic(IDs, range);
}

void TransformActionsImpl::addInsertion(SourceLocation loc, StringRef text) {
  SourceManager &SM = Ctx.getSourceManager();
  loc = SM.getExpansionLoc(loc);
  for (const CharRange &I : llvm::reverse(Removals)) {
    if (!SM.isBeforeInTranslationUnit(loc, I.End))
      break;
    if (I.Begin.isBeforeInTranslationUnitThan(loc))
      return;
  }

  Inserts[FullSourceLoc(loc, SM)].push_back(text);
}

void TransformActionsImpl::addRemoval(CharSourceRange range) {
  CharRange newRange(range, Ctx.getSourceManager(), PP);
  if (newRange.Begin == newRange.End)
    return;

  Inserts.erase(Inserts.upper_bound(newRange.Begin),
                Inserts.lower_bound(newRange.End));

  std::list<CharRange>::iterator I = Removals.end();
  while (I != Removals.begin()) {
    std::list<CharRange>::iterator RI = I;
    --RI;
    RangeComparison comp = newRange.compareWith(*RI);
    switch (comp) {
    case Range_Before:
      --I;
      break;
    case Range_After:
      Removals.insert(I, newRange);
      return;
    case Range_Contained:
      return;
    case Range_Contains:
      RI->End = newRange.End;
      [[fallthrough]];
    case Range_ExtendsBegin:
      newRange.End = RI->End;
      Removals.erase(RI);
      break;
    case Range_ExtendsEnd:
      RI->End = newRange.End;
      return;
    }
  }

  Removals.insert(Removals.begin(), newRange);
}

void TransformActionsImpl::applyRewrites(
                                  TransformActions::RewriteReceiver &receiver) {
  for (InsertsMap::iterator I = Inserts.begin(), E = Inserts.end(); I!=E; ++I) {
    SourceLocation loc = I->first;
    for (TextsVec::iterator
           TI = I->second.begin(), TE = I->second.end(); TI != TE; ++TI) {
      receiver.insert(loc, *TI);
    }
  }

  for (std::vector<std::pair<CharRange, SourceLocation> >::iterator
       I = IndentationRanges.begin(), E = IndentationRanges.end(); I!=E; ++I) {
    CharSourceRange range = CharSourceRange::getCharRange(I->first.Begin,
                                                          I->first.End);
    receiver.increaseIndentation(range, I->second);
  }

  for (std::list<CharRange>::iterator
         I = Removals.begin(), E = Removals.end(); I != E; ++I) {
    CharSourceRange range = CharSourceRange::getCharRange(I->Begin, I->End);
    receiver.remove(range);
  }
}

/// Stores text passed to the transformation methods to keep the string
/// "alive". Since the vast majority of text will be the same, we also unique
/// the strings using a StringMap.
StringRef TransformActionsImpl::getUniqueText(StringRef text) {
  return UniqueText.insert(std::make_pair(text, false)).first->first();
}

/// Computes the source location just past the end of the token at
/// the given source location. If the location points at a macro, the whole
/// macro expansion is skipped.
SourceLocation TransformActionsImpl::getLocForEndOfToken(SourceLocation loc,
                                                         SourceManager &SM,
                                                         Preprocessor &PP) {
  if (loc.isMacroID()) {
    CharSourceRange Exp = SM.getExpansionRange(loc);
    if (Exp.isCharRange())
      return Exp.getEnd();
    loc = Exp.getEnd();
  }
  return PP.getLocForEndOfToken(loc);
}

TransformActions::RewriteReceiver::~RewriteReceiver() { }

TransformActions::TransformActions(DiagnosticsEngine &diag,
                                   CapturedDiagList &capturedDiags,
                                   ASTContext &ctx, Preprocessor &PP)
    : Diags(diag), CapturedDiags(capturedDiags) {
  Impl = new TransformActionsImpl(capturedDiags, ctx, PP);
}

TransformActions::~TransformActions() {
  delete static_cast<TransformActionsImpl*>(Impl);
}

void TransformActions::startTransaction() {
  static_cast<TransformActionsImpl*>(Impl)->startTransaction();
}

bool TransformActions::commitTransaction() {
  return static_cast<TransformActionsImpl*>(Impl)->commitTransaction();
}

void TransformActions::abortTransaction() {
  static_cast<TransformActionsImpl*>(Impl)->abortTransaction();
}


void TransformActions::insert(SourceLocation loc, StringRef text) {
  static_cast<TransformActionsImpl*>(Impl)->insert(loc, text);
}

void TransformActions::insertAfterToken(SourceLocation loc,
                                        StringRef text) {
  static_cast<TransformActionsImpl*>(Impl)->insertAfterToken(loc, text);
}

void TransformActions::remove(SourceRange range) {
  static_cast<TransformActionsImpl*>(Impl)->remove(range);
}

void TransformActions::removeStmt(Stmt *S) {
  static_cast<TransformActionsImpl*>(Impl)->removeStmt(S);
}

void TransformActions::replace(SourceRange range, StringRef text) {
  static_cast<TransformActionsImpl*>(Impl)->replace(range, text);
}

void TransformActions::replace(SourceRange range,
                               SourceRange replacementRange) {
  static_cast<TransformActionsImpl*>(Impl)->replace(range, replacementRange);
}

void TransformActions::replaceStmt(Stmt *S, StringRef text) {
  static_cast<TransformActionsImpl*>(Impl)->replaceStmt(S, text);
}

void TransformActions::replaceText(SourceLocation loc, StringRef text,
                                   StringRef replacementText) {
  static_cast<TransformActionsImpl*>(Impl)->replaceText(loc, text,
                                                        replacementText);
}

void TransformActions::increaseIndentation(SourceRange range,
                                           SourceLocation parentIndent) {
  static_cast<TransformActionsImpl*>(Impl)->increaseIndentation(range,
                                                                parentIndent);
}

bool TransformActions::clearDiagnostic(ArrayRef<unsigned> IDs,
                                       SourceRange range) {
  return static_cast<TransformActionsImpl*>(Impl)->clearDiagnostic(IDs, range);
}

void TransformActions::applyRewrites(RewriteReceiver &receiver) {
  static_cast<TransformActionsImpl*>(Impl)->applyRewrites(receiver);
}

DiagnosticBuilder TransformActions::report(SourceLocation loc, unsigned diagId,
                                           SourceRange range) {
  assert(!static_cast<TransformActionsImpl *>(Impl)->isInTransaction() &&
         "Errors should be emitted out of a transaction");
  return Diags.Report(loc, diagId) << range;
}

void TransformActions::reportError(StringRef message, SourceLocation loc,
                                   SourceRange range) {
  report(loc, diag::err_mt_message, range) << message;
}

void TransformActions::reportWarning(StringRef message, SourceLocation loc,
                                     SourceRange range) {
  report(loc, diag::warn_mt_message, range) << message;
}

void TransformActions::reportNote(StringRef message, SourceLocation loc,
                                  SourceRange range) {
  report(loc, diag::note_mt_message, range) << message;
}
