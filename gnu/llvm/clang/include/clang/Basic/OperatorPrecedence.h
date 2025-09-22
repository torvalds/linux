//===--- OperatorPrecedence.h - Operator precedence levels ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines and computes precedence levels for binary/ternary operators.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_OPERATORPRECEDENCE_H
#define LLVM_CLANG_BASIC_OPERATORPRECEDENCE_H

#include "clang/Basic/TokenKinds.h"

namespace clang {

/// PrecedenceLevels - These are precedences for the binary/ternary
/// operators in the C99 grammar.  These have been named to relate
/// with the C99 grammar productions.  Low precedences numbers bind
/// more weakly than high numbers.
namespace prec {
  enum Level {
    Unknown         = 0,    // Not binary operator.
    Comma           = 1,    // ,
    Assignment      = 2,    // =, *=, /=, %=, +=, -=, <<=, >>=, &=, ^=, |=
    Conditional     = 3,    // ?
    LogicalOr       = 4,    // ||
    LogicalAnd      = 5,    // &&
    InclusiveOr     = 6,    // |
    ExclusiveOr     = 7,    // ^
    And             = 8,    // &
    Equality        = 9,    // ==, !=
    Relational      = 10,   //  >=, <=, >, <
    Spaceship       = 11,   // <=>
    Shift           = 12,   // <<, >>
    Additive        = 13,   // -, +
    Multiplicative  = 14,   // *, /, %
    PointerToMember = 15    // .*, ->*
  };
}

/// Return the precedence of the specified binary operator token.
prec::Level getBinOpPrecedence(tok::TokenKind Kind, bool GreaterThanIsOperator,
                               bool CPlusPlus11);

}  // end namespace clang

#endif // LLVM_CLANG_BASIC_OPERATORPRECEDENCE_H
