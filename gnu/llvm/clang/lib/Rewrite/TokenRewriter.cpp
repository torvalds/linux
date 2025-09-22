//===- TokenRewriter.cpp - Token-based code rewriting interface -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the TokenRewriter class, which is used for code
//  transformations.
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Core/TokenRewriter.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/ScratchBuffer.h"
#include "clang/Lex/Token.h"
#include <cassert>
#include <cstring>
#include <map>
#include <utility>

using namespace clang;

TokenRewriter::TokenRewriter(FileID FID, SourceManager &SM,
                             const LangOptions &LangOpts) {
  ScratchBuf.reset(new ScratchBuffer(SM));

  // Create a lexer to lex all the tokens of the main file in raw mode.
  llvm::MemoryBufferRef FromFile = SM.getBufferOrFake(FID);
  Lexer RawLex(FID, FromFile, SM, LangOpts);

  // Return all comments and whitespace as tokens.
  RawLex.SetKeepWhitespaceMode(true);

  // Lex the file, populating our datastructures.
  Token RawTok;
  RawLex.LexFromRawLexer(RawTok);
  while (RawTok.isNot(tok::eof)) {
#if 0
    if (Tok.is(tok::raw_identifier)) {
      // Look up the identifier info for the token.  This should use
      // IdentifierTable directly instead of PP.
      PP.LookUpIdentifierInfo(Tok);
    }
#endif

    AddToken(RawTok, TokenList.end());
    RawLex.LexFromRawLexer(RawTok);
  }
}

TokenRewriter::~TokenRewriter() = default;

/// RemapIterator - Convert from token_iterator (a const iterator) to
/// TokenRefTy (a non-const iterator).
TokenRewriter::TokenRefTy TokenRewriter::RemapIterator(token_iterator I) {
  if (I == token_end()) return TokenList.end();

  // FIXME: This is horrible, we should use our own list or something to avoid
  // this.
  std::map<SourceLocation, TokenRefTy>::iterator MapIt =
    TokenAtLoc.find(I->getLocation());
  assert(MapIt != TokenAtLoc.end() && "iterator not in rewriter?");
  return MapIt->second;
}

/// AddToken - Add the specified token into the Rewriter before the other
/// position.
TokenRewriter::TokenRefTy
TokenRewriter::AddToken(const Token &T, TokenRefTy Where) {
  Where = TokenList.insert(Where, T);

  bool InsertSuccess = TokenAtLoc.insert(std::make_pair(T.getLocation(),
                                                        Where)).second;
  assert(InsertSuccess && "Token location already in rewriter!");
  (void)InsertSuccess;
  return Where;
}

TokenRewriter::token_iterator
TokenRewriter::AddTokenBefore(token_iterator I, const char *Val) {
  unsigned Len = strlen(Val);

  // Plop the string into the scratch buffer, then create a token for this
  // string.
  Token Tok;
  Tok.startToken();
  const char *Spelling;
  Tok.setLocation(ScratchBuf->getToken(Val, Len, Spelling));
  Tok.setLength(Len);

  // TODO: Form a whole lexer around this and relex the token!  For now, just
  // set kind to tok::unknown.
  Tok.setKind(tok::unknown);

  return AddToken(Tok, RemapIterator(I));
}
