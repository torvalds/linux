//===--- TypeTraits.h - C++ Type Traits Support Enumerations ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines enumerations for the type traits support.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_TYPETRAITS_H
#define LLVM_CLANG_BASIC_TYPETRAITS_H

#include "llvm/Support/Compiler.h"

namespace clang {
/// Names for traits that operate specifically on types.
enum TypeTrait {
#define TYPE_TRAIT_1(Spelling, Name, Key) UTT_##Name,
#include "clang/Basic/TokenKinds.def"
  UTT_Last = -1 // UTT_Last == last UTT_XX in the enum.
#define TYPE_TRAIT_1(Spelling, Name, Key) +1
#include "clang/Basic/TokenKinds.def"
  ,
#define TYPE_TRAIT_2(Spelling, Name, Key) BTT_##Name,
#include "clang/Basic/TokenKinds.def"
  BTT_Last = UTT_Last // BTT_Last == last BTT_XX in the enum.
#define TYPE_TRAIT_2(Spelling, Name, Key) +1
#include "clang/Basic/TokenKinds.def"
  ,
#define TYPE_TRAIT_N(Spelling, Name, Key) TT_##Name,
#include "clang/Basic/TokenKinds.def"
  TT_Last = BTT_Last // TT_Last == last TT_XX in the enum.
#define TYPE_TRAIT_N(Spelling, Name, Key) +1
#include "clang/Basic/TokenKinds.def"
};

/// Names for the array type traits.
enum ArrayTypeTrait {
#define ARRAY_TYPE_TRAIT(Spelling, Name, Key) ATT_##Name,
#include "clang/Basic/TokenKinds.def"
  ATT_Last = -1 // ATT_Last == last ATT_XX in the enum.
#define ARRAY_TYPE_TRAIT(Spelling, Name, Key) +1
#include "clang/Basic/TokenKinds.def"
};

/// Names for the "expression or type" traits.
enum UnaryExprOrTypeTrait {
#define UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) UETT_##Name,
#define CXX11_UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) UETT_##Name,
#include "clang/Basic/TokenKinds.def"
  UETT_Last = -1 // UETT_Last == last UETT_XX in the enum.
#define UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) +1
#define CXX11_UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) +1
#include "clang/Basic/TokenKinds.def"
};

/// Return the internal name of type trait \p T. Never null.
const char *getTraitName(TypeTrait T) LLVM_READONLY;
const char *getTraitName(ArrayTypeTrait T) LLVM_READONLY;
const char *getTraitName(UnaryExprOrTypeTrait T) LLVM_READONLY;

/// Return the spelling of the type trait \p TT. Never null.
const char *getTraitSpelling(TypeTrait T) LLVM_READONLY;
const char *getTraitSpelling(ArrayTypeTrait T) LLVM_READONLY;
const char *getTraitSpelling(UnaryExprOrTypeTrait T) LLVM_READONLY;

/// Return the arity of the type trait \p T.
unsigned getTypeTraitArity(TypeTrait T) LLVM_READONLY;

} // namespace clang

#endif
