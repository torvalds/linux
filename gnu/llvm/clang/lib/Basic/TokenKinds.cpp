//===--- TokenKinds.cpp - Token Kinds Support -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the TokenKind enum and support functions.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/TokenKinds.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;

static const char * const TokNames[] = {
#define TOK(X) #X,
#define KEYWORD(X,Y) #X,
#include "clang/Basic/TokenKinds.def"
  nullptr
};

const char *tok::getTokenName(TokenKind Kind) {
  if (Kind < tok::NUM_TOKENS)
    return TokNames[Kind];
  llvm_unreachable("unknown TokenKind");
  return nullptr;
}

const char *tok::getPunctuatorSpelling(TokenKind Kind) {
  switch (Kind) {
#define PUNCTUATOR(X,Y) case X: return Y;
#include "clang/Basic/TokenKinds.def"
  default: break;
  }
  return nullptr;
}

const char *tok::getKeywordSpelling(TokenKind Kind) {
  switch (Kind) {
#define KEYWORD(X,Y) case kw_ ## X: return #X;
#include "clang/Basic/TokenKinds.def"
    default: break;
  }
  return nullptr;
}

const char *tok::getPPKeywordSpelling(tok::PPKeywordKind Kind) {
  switch (Kind) {
#define PPKEYWORD(x) case tok::pp_##x: return #x;
#include "clang/Basic/TokenKinds.def"
  default: break;
  }
  return nullptr;
}

bool tok::isAnnotation(TokenKind Kind) {
  switch (Kind) {
#define ANNOTATION(X) case annot_ ## X: return true;
#include "clang/Basic/TokenKinds.def"
  default:
    break;
  }
  return false;
}

bool tok::isPragmaAnnotation(TokenKind Kind) {
  switch (Kind) {
#define PRAGMA_ANNOTATION(X) case annot_ ## X: return true;
#include "clang/Basic/TokenKinds.def"
  default:
    break;
  }
  return false;
}
