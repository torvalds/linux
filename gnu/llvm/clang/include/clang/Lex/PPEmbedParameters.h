//===--- PPEmbedParameters.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines all of the preprocessor directive parmeters for #embed
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PPEMBEDPARAMETERS_H
#define LLVM_CLANG_LEX_PPEMBEDPARAMETERS_H

#include "clang/Lex/PPDirectiveParameter.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

/// Preprocessor extension embed parameter "clang::offset"
/// `clang::offset( constant-expression )`
class PPEmbedParameterOffset : public PPDirectiveParameter {
public:
  size_t Offset;

  PPEmbedParameterOffset(size_t Offset, SourceRange R)
      : PPDirectiveParameter(R), Offset(Offset) {}
};

/// Preprocessor standard embed parameter "limit"
/// `limit( constant-expression )`
class PPEmbedParameterLimit : public PPDirectiveParameter {
public:
  size_t Limit;

  PPEmbedParameterLimit(size_t Limit, SourceRange R)
      : PPDirectiveParameter(R), Limit(Limit) {}
};

/// Preprocessor standard embed parameter "prefix"
/// `prefix( balanced-token-seq )`
class PPEmbedParameterPrefix : public PPDirectiveParameter {
public:
  SmallVector<Token, 2> Tokens;

  PPEmbedParameterPrefix(SmallVectorImpl<Token> &&Tokens, SourceRange R)
      : PPDirectiveParameter(R), Tokens(std::move(Tokens)) {}
};

/// Preprocessor standard embed parameter "suffix"
/// `suffix( balanced-token-seq )`
class PPEmbedParameterSuffix : public PPDirectiveParameter {
public:
  SmallVector<Token, 2> Tokens;

  PPEmbedParameterSuffix(SmallVectorImpl<Token> &&Tokens, SourceRange R)
      : PPDirectiveParameter(R), Tokens(std::move(Tokens)) {}
};

/// Preprocessor standard embed parameter "if_empty"
/// `if_empty( balanced-token-seq )`
class PPEmbedParameterIfEmpty : public PPDirectiveParameter {
public:
  SmallVector<Token, 2> Tokens;

  PPEmbedParameterIfEmpty(SmallVectorImpl<Token> &&Tokens, SourceRange R)
      : PPDirectiveParameter(R), Tokens(std::move(Tokens)) {}
};

struct LexEmbedParametersResult {
  std::optional<PPEmbedParameterLimit> MaybeLimitParam;
  std::optional<PPEmbedParameterOffset> MaybeOffsetParam;
  std::optional<PPEmbedParameterIfEmpty> MaybeIfEmptyParam;
  std::optional<PPEmbedParameterPrefix> MaybePrefixParam;
  std::optional<PPEmbedParameterSuffix> MaybeSuffixParam;
  int UnrecognizedParams;

  size_t PrefixTokenCount() const {
    if (MaybePrefixParam)
      return MaybePrefixParam->Tokens.size();
    return 0;
  }
  size_t SuffixTokenCount() const {
    if (MaybeSuffixParam)
      return MaybeSuffixParam->Tokens.size();
    return 0;
  }
};
} // end namespace clang

#endif
