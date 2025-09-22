//===----- SemaPseudoObject.h --- Semantic Analysis for Pseudo-Objects ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis for expressions involving
//  pseudo-object references.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAPSEUDOOBJECT_H
#define LLVM_CLANG_SEMA_SEMAPSEUDOOBJECT_H

#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaBase.h"

namespace clang {

class SemaPseudoObject : public SemaBase {
public:
  SemaPseudoObject(Sema &S);

  ExprResult checkIncDec(Scope *S, SourceLocation OpLoc,
                         UnaryOperatorKind Opcode, Expr *Op);
  ExprResult checkAssignment(Scope *S, SourceLocation OpLoc,
                             BinaryOperatorKind Opcode, Expr *LHS, Expr *RHS);
  ExprResult checkRValue(Expr *E);
  Expr *recreateSyntacticForm(PseudoObjectExpr *E);
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAPSEUDOOBJECT_H