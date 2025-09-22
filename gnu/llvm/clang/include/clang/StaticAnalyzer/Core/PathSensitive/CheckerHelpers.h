//== CheckerHelpers.h - Helper functions for checkers ------------*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines CheckerVisitor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CHECKERHELPERS_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CHECKERHELPERS_H

#include "ProgramState_Fwd.h"
#include "SVals.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/OperatorKinds.h"
#include <optional>
#include <tuple>

namespace clang {

class Expr;
class VarDecl;
class QualType;
class Preprocessor;

namespace ento {

bool containsMacro(const Stmt *S);
bool containsEnum(const Stmt *S);
bool containsStaticLocal(const Stmt *S);
bool containsBuiltinOffsetOf(const Stmt *S);
template <class T> bool containsStmt(const Stmt *S) {
  if (isa<T>(S))
      return true;

  for (const Stmt *Child : S->children())
    if (Child && containsStmt<T>(Child))
      return true;

  return false;
}

std::pair<const clang::VarDecl *, const clang::Expr *>
parseAssignment(const Stmt *S);

// Do not reorder! The getMostNullable method relies on the order.
// Optimization: Most pointers expected to be unspecified. When a symbol has an
// unspecified or nonnull type non of the rules would indicate any problem for
// that symbol. For this reason only nullable and contradicted nullability are
// stored for a symbol. When a symbol is already contradicted, it can not be
// casted back to nullable.
enum class Nullability : char {
  Contradicted, // Tracked nullability is contradicted by an explicit cast. Do
                // not report any nullability related issue for this symbol.
                // This nullability is propagated aggressively to avoid false
                // positive results. See the comment on getMostNullable method.
  Nullable,
  Unspecified,
  Nonnull
};

/// Get nullability annotation for a given type.
Nullability getNullabilityAnnotation(QualType Type);

/// Try to parse the value of a defined preprocessor macro. We can only parse
/// simple expressions that consist of an optional minus sign token and then a
/// token for an integer. If we cannot parse the value then std::nullopt is
/// returned.
std::optional<int> tryExpandAsInteger(StringRef Macro, const Preprocessor &PP);

class OperatorKind {
  union {
    BinaryOperatorKind Bin;
    UnaryOperatorKind Un;
  } Op;
  bool IsBinary;

public:
  explicit OperatorKind(BinaryOperatorKind Bin) : Op{Bin}, IsBinary{true} {}
  explicit OperatorKind(UnaryOperatorKind Un) : IsBinary{false} { Op.Un = Un; }
  bool IsBinaryOp() const { return IsBinary; }

  BinaryOperatorKind GetBinaryOpUnsafe() const {
    assert(IsBinary && "cannot get binary operator - we have a unary operator");
    return Op.Bin;
  }

  std::optional<BinaryOperatorKind> GetBinaryOp() const {
    if (IsBinary)
      return Op.Bin;
    return {};
  }

  UnaryOperatorKind GetUnaryOpUnsafe() const {
    assert(!IsBinary &&
           "cannot get unary operator - we have a binary operator");
    return Op.Un;
  }

  std::optional<UnaryOperatorKind> GetUnaryOp() const {
    if (!IsBinary)
      return Op.Un;
    return {};
  }
};

OperatorKind operationKindFromOverloadedOperator(OverloadedOperatorKind OOK,
                                                 bool IsBinary);

std::optional<SVal> getPointeeVal(SVal PtrSVal, ProgramStateRef State);

} // namespace ento

} // namespace clang

#endif
