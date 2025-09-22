//===--- ExpressionTraits.cpp - Expression Traits Support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the expression traits support functions.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/ExpressionTraits.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
using namespace clang;

static constexpr const char *ExpressionTraitNames[] = {
#define EXPRESSION_TRAIT(Spelling, Name, Key) #Name,
#include "clang/Basic/TokenKinds.def"
};

static constexpr const char *ExpressionTraitSpellings[] = {
#define EXPRESSION_TRAIT(Spelling, Name, Key) #Spelling,
#include "clang/Basic/TokenKinds.def"
};

const char *clang::getTraitName(ExpressionTrait T) {
  assert(T <= ET_Last && "invalid enum value!");
  return ExpressionTraitNames[T];
}

const char *clang::getTraitSpelling(ExpressionTrait T) {
  assert(T <= ET_Last && "invalid enum value!");
  return ExpressionTraitSpellings[T];
}
