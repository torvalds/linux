//===--- PPCaching.cpp - Handle caching lexed tokens ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements pieces of the Preprocessor interface that manage the
// caching of lexed tokens.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Preprocessor.h"
using namespace clang;

// EnableBacktrackAtThisPos - From the point that this method is called, and
// until CommitBacktrackedTokens() or Backtrack() is called, the Preprocessor
// keeps track of the lexed tokens so that a subsequent Backtrack() call will
// make the Preprocessor re-lex the same tokens.
//
// Nested backtracks are allowed, meaning that EnableBacktrackAtThisPos can
// be called multiple times and CommitBacktrackedTokens/Backtrack calls will
// be combined with the EnableBacktrackAtThisPos calls in reverse order.
void Preprocessor::EnableBacktrackAtThisPos() {
  assert(LexLevel == 0 && "cannot use lookahead while lexing");
  BacktrackPositions.push_back(CachedLexPos);
  EnterCachingLexMode();
}

// Disable the last EnableBacktrackAtThisPos call.
void Preprocessor::CommitBacktrackedTokens() {
  assert(!BacktrackPositions.empty()
         && "EnableBacktrackAtThisPos was not called!");
  BacktrackPositions.pop_back();
}

// Make Preprocessor re-lex the tokens that were lexed since
// EnableBacktrackAtThisPos() was previously called.
void Preprocessor::Backtrack() {
  assert(!BacktrackPositions.empty()
         && "EnableBacktrackAtThisPos was not called!");
  CachedLexPos = BacktrackPositions.back();
  BacktrackPositions.pop_back();
  recomputeCurLexerKind();
}

void Preprocessor::CachingLex(Token &Result) {
  if (!InCachingLexMode())
    return;

  // The assert in EnterCachingLexMode should prevent this from happening.
  assert(LexLevel == 1 &&
         "should not use token caching within the preprocessor");

  if (CachedLexPos < CachedTokens.size()) {
    Result = CachedTokens[CachedLexPos++];
    Result.setFlag(Token::IsReinjected);
    return;
  }

  ExitCachingLexMode();
  Lex(Result);

  if (isBacktrackEnabled()) {
    // Cache the lexed token.
    EnterCachingLexModeUnchecked();
    CachedTokens.push_back(Result);
    ++CachedLexPos;
    return;
  }

  if (CachedLexPos < CachedTokens.size()) {
    EnterCachingLexModeUnchecked();
  } else {
    // All cached tokens were consumed.
    CachedTokens.clear();
    CachedLexPos = 0;
  }
}

void Preprocessor::EnterCachingLexMode() {
  // The caching layer sits on top of all the other lexers, so it's incorrect
  // to cache tokens while inside a nested lex action. The cached tokens would
  // be retained after returning to the enclosing lex action and, at best,
  // would appear at the wrong position in the token stream.
  assert(LexLevel == 0 &&
         "entered caching lex mode while lexing something else");

  if (InCachingLexMode()) {
    assert(CurLexerCallback == CLK_CachingLexer && "Unexpected lexer kind");
    return;
  }

  EnterCachingLexModeUnchecked();
}

void Preprocessor::EnterCachingLexModeUnchecked() {
  assert(CurLexerCallback != CLK_CachingLexer && "already in caching lex mode");
  PushIncludeMacroStack();
  CurLexerCallback = CLK_CachingLexer;
}


const Token &Preprocessor::PeekAhead(unsigned N) {
  assert(CachedLexPos + N > CachedTokens.size() && "Confused caching.");
  ExitCachingLexMode();
  for (size_t C = CachedLexPos + N - CachedTokens.size(); C > 0; --C) {
    CachedTokens.push_back(Token());
    Lex(CachedTokens.back());
  }
  EnterCachingLexMode();
  return CachedTokens.back();
}

void Preprocessor::AnnotatePreviousCachedTokens(const Token &Tok) {
  assert(Tok.isAnnotation() && "Expected annotation token");
  assert(CachedLexPos != 0 && "Expected to have some cached tokens");
  assert(CachedTokens[CachedLexPos-1].getLastLoc() == Tok.getAnnotationEndLoc()
         && "The annotation should be until the most recent cached token");

  // Start from the end of the cached tokens list and look for the token
  // that is the beginning of the annotation token.
  for (CachedTokensTy::size_type i = CachedLexPos; i != 0; --i) {
    CachedTokensTy::iterator AnnotBegin = CachedTokens.begin() + i-1;
    if (AnnotBegin->getLocation() == Tok.getLocation()) {
      assert((BacktrackPositions.empty() || BacktrackPositions.back() <= i) &&
             "The backtrack pos points inside the annotated tokens!");
      // Replace the cached tokens with the single annotation token.
      if (i < CachedLexPos)
        CachedTokens.erase(AnnotBegin + 1, CachedTokens.begin() + CachedLexPos);
      *AnnotBegin = Tok;
      CachedLexPos = i;
      return;
    }
  }
}

bool Preprocessor::IsPreviousCachedToken(const Token &Tok) const {
  // There's currently no cached token...
  if (!CachedLexPos)
    return false;

  const Token LastCachedTok = CachedTokens[CachedLexPos - 1];
  if (LastCachedTok.getKind() != Tok.getKind())
    return false;

  SourceLocation::IntTy RelOffset = 0;
  if ((!getSourceManager().isInSameSLocAddrSpace(
          Tok.getLocation(), getLastCachedTokenLocation(), &RelOffset)) ||
      RelOffset)
    return false;

  return true;
}

void Preprocessor::ReplacePreviousCachedToken(ArrayRef<Token> NewToks) {
  assert(CachedLexPos != 0 && "Expected to have some cached tokens");
  CachedTokens.insert(CachedTokens.begin() + CachedLexPos - 1, NewToks.begin(),
                      NewToks.end());
  CachedTokens.erase(CachedTokens.begin() + CachedLexPos - 1 + NewToks.size());
  CachedLexPos += NewToks.size() - 1;
}
