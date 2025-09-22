//===- TokenRewriter.h - Token-based Rewriter -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TokenRewriter class, which is used for code
//  transformations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_CORE_TOKENREWRITER_H
#define LLVM_CLANG_REWRITE_CORE_TOKENREWRITER_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Token.h"
#include <cassert>
#include <list>
#include <map>
#include <memory>

namespace clang {

class LangOptions;
class ScratchBuffer;
class SourceManager;

  class TokenRewriter {
    /// TokenList - This is the list of raw tokens that make up this file.  Each
    /// of these tokens has a unique SourceLocation, which is a FileID.
    std::list<Token> TokenList;

    /// TokenRefTy - This is the type used to refer to a token in the TokenList.
    using TokenRefTy = std::list<Token>::iterator;

    /// TokenAtLoc - This map indicates which token exists at a specific
    /// SourceLocation.  Since each token has a unique SourceLocation, this is a
    /// one to one map.  The token can return its own location directly, to map
    /// backwards.
    std::map<SourceLocation, TokenRefTy> TokenAtLoc;

    /// ScratchBuf - This is the buffer that we create scratch tokens from.
    std::unique_ptr<ScratchBuffer> ScratchBuf;

  public:
    /// TokenRewriter - This creates a TokenRewriter for the file with the
    /// specified FileID.
    TokenRewriter(FileID FID, SourceManager &SM, const LangOptions &LO);

    TokenRewriter(const TokenRewriter &) = delete;
    TokenRewriter &operator=(const TokenRewriter &) = delete;
    ~TokenRewriter();

    using token_iterator = std::list<Token>::const_iterator;

    token_iterator token_begin() const { return TokenList.begin(); }
    token_iterator token_end() const { return TokenList.end(); }

    token_iterator AddTokenBefore(token_iterator I, const char *Val);

    token_iterator AddTokenAfter(token_iterator I, const char *Val) {
      assert(I != token_end() && "Cannot insert after token_end()!");
      return AddTokenBefore(++I, Val);
    }

  private:
    /// RemapIterator - Convert from token_iterator (a const iterator) to
    /// TokenRefTy (a non-const iterator).
    TokenRefTy RemapIterator(token_iterator I);

    /// AddToken - Add the specified token into the Rewriter before the other
    /// position.
    TokenRefTy AddToken(const Token &T, TokenRefTy Where);
  };

} // namespace clang

#endif // LLVM_CLANG_REWRITE_CORE_TOKENREWRITER_H
