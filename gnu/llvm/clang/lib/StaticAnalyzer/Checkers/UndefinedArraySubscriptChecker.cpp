//===--- UndefinedArraySubscriptChecker.h ----------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines UndefinedArraySubscriptChecker, a builtin check in ExprEngine
// that performs checks for undefined array subscripts.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/DeclCXX.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class UndefinedArraySubscriptChecker
  : public Checker< check::PreStmt<ArraySubscriptExpr> > {
  const BugType BT{this, "Array subscript is undefined"};

public:
  void checkPreStmt(const ArraySubscriptExpr *A, CheckerContext &C) const;
};
} // end anonymous namespace

void
UndefinedArraySubscriptChecker::checkPreStmt(const ArraySubscriptExpr *A,
                                             CheckerContext &C) const {
  const Expr *Index = A->getIdx();
  if (!C.getSVal(Index).isUndef())
    return;

  // Sema generates anonymous array variables for copying array struct fields.
  // Don't warn if we're in an implicitly-generated constructor.
  const Decl *D = C.getLocationContext()->getDecl();
  if (const CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(D))
    if (Ctor->isDefaulted())
      return;

  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;
  // Generate a report for this bug.
  auto R = std::make_unique<PathSensitiveBugReport>(BT, BT.getDescription(), N);
  R->addRange(A->getIdx()->getSourceRange());
  bugreporter::trackExpressionValue(N, A->getIdx(), *R);
  C.emitReport(std::move(R));
}

void ento::registerUndefinedArraySubscriptChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefinedArraySubscriptChecker>();
}

bool ento::shouldRegisterUndefinedArraySubscriptChecker(const CheckerManager &mgr) {
  return true;
}
