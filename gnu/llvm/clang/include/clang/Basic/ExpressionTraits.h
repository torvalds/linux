//===- ExpressionTraits.h - C++ Expression Traits Support Enums -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines enumerations for expression traits intrinsics.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_EXPRESSIONTRAITS_H
#define LLVM_CLANG_BASIC_EXPRESSIONTRAITS_H

#include "llvm/Support/Compiler.h"

namespace clang {

enum ExpressionTrait {
#define EXPRESSION_TRAIT(Spelling, Name, Key) ET_##Name,
#include "clang/Basic/TokenKinds.def"
  ET_Last = -1 // ET_Last == last ET_XX in the enum.
#define EXPRESSION_TRAIT(Spelling, Name, Key) +1
#include "clang/Basic/TokenKinds.def"
};

/// Return the internal name of type trait \p T. Never null.
const char *getTraitName(ExpressionTrait T) LLVM_READONLY;

/// Return the spelling of the type trait \p TT. Never null.
const char *getTraitSpelling(ExpressionTrait T) LLVM_READONLY;

} // namespace clang

#endif
