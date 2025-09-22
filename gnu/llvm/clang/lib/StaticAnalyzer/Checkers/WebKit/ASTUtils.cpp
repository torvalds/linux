//=======- ASTUtils.cpp ------------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTUtils.h"
#include "PtrTypesSemantics.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include <optional>

namespace clang {

bool tryToFindPtrOrigin(
    const Expr *E, bool StopAtFirstRefCountedObj,
    std::function<bool(const clang::Expr *, bool)> callback) {
  while (E) {
    if (auto *tempExpr = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = tempExpr->getSubExpr();
      continue;
    }
    if (auto *tempExpr = dyn_cast<CXXBindTemporaryExpr>(E)) {
      E = tempExpr->getSubExpr();
      continue;
    }
    if (auto *tempExpr = dyn_cast<CXXTemporaryObjectExpr>(E)) {
      if (auto *C = tempExpr->getConstructor()) {
        if (auto *Class = C->getParent(); Class && isRefCounted(Class))
          return callback(E, true);
        break;
      }
    }
    if (auto *tempExpr = dyn_cast<ParenExpr>(E)) {
      E = tempExpr->getSubExpr();
      continue;
    }
    if (auto *Expr = dyn_cast<ConditionalOperator>(E)) {
      return tryToFindPtrOrigin(Expr->getTrueExpr(), StopAtFirstRefCountedObj,
                                callback) &&
             tryToFindPtrOrigin(Expr->getFalseExpr(), StopAtFirstRefCountedObj,
                                callback);
    }
    if (auto *cast = dyn_cast<CastExpr>(E)) {
      if (StopAtFirstRefCountedObj) {
        if (auto *ConversionFunc =
                dyn_cast_or_null<FunctionDecl>(cast->getConversionFunction())) {
          if (isCtorOfRefCounted(ConversionFunc))
            return callback(E, true);
        }
      }
      // FIXME: This can give false "origin" that would lead to false negatives
      // in checkers. See https://reviews.llvm.org/D37023 for reference.
      E = cast->getSubExpr();
      continue;
    }
    if (auto *call = dyn_cast<CallExpr>(E)) {
      if (auto *memberCall = dyn_cast<CXXMemberCallExpr>(call)) {
        if (auto *decl = memberCall->getMethodDecl()) {
          std::optional<bool> IsGetterOfRefCt = isGetterOfRefCounted(decl);
          if (IsGetterOfRefCt && *IsGetterOfRefCt) {
            E = memberCall->getImplicitObjectArgument();
            if (StopAtFirstRefCountedObj) {
              return callback(E, true);
            }
            continue;
          }
        }
      }

      if (auto *operatorCall = dyn_cast<CXXOperatorCallExpr>(E)) {
        if (operatorCall->getNumArgs() == 1) {
          E = operatorCall->getArg(0);
          continue;
        }
      }

      if (auto *callee = call->getDirectCallee()) {
        if (isCtorOfRefCounted(callee)) {
          if (StopAtFirstRefCountedObj)
            return callback(E, true);

          E = call->getArg(0);
          continue;
        }

        if (isReturnValueRefCounted(callee))
          return callback(E, true);

        if (isSingleton(callee))
          return callback(E, true);

        if (isPtrConversion(callee)) {
          E = call->getArg(0);
          continue;
        }
      }
    }
    if (auto *unaryOp = dyn_cast<UnaryOperator>(E)) {
      // FIXME: Currently accepts ANY unary operator. Is it OK?
      E = unaryOp->getSubExpr();
      continue;
    }

    break;
  }
  // Some other expression.
  return callback(E, false);
}

bool isASafeCallArg(const Expr *E) {
  assert(E);
  if (auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    if (auto *D = dyn_cast_or_null<VarDecl>(Ref->getFoundDecl())) {
      if (isa<ParmVarDecl>(D) || D->isLocalVarDecl())
        return true;
    }
  }

  // TODO: checker for method calls on non-refcounted objects
  return isa<CXXThisExpr>(E);
}

} // namespace clang
