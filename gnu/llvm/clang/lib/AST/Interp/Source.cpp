//===--- Source.cpp - Source expression tracking ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Source.h"
#include "clang/AST/Expr.h"

using namespace clang;
using namespace clang::interp;

SourceLocation SourceInfo::getLoc() const {
  if (const Expr *E = asExpr())
    return E->getExprLoc();
  if (const Stmt *S = asStmt())
    return S->getBeginLoc();
  if (const Decl *D = asDecl())
    return D->getBeginLoc();
  return SourceLocation();
}

SourceRange SourceInfo::getRange() const {
  if (const Expr *E = asExpr())
    return E->getSourceRange();
  if (const Stmt *S = asStmt())
    return S->getSourceRange();
  if (const Decl *D = asDecl())
    return D->getSourceRange();
  return SourceRange();
}

const Expr *SourceInfo::asExpr() const {
  if (const auto *S = Source.dyn_cast<const Stmt *>())
    return dyn_cast<Expr>(S);
  return nullptr;
}

const Expr *SourceMapper::getExpr(const Function *F, CodePtr PC) const {
  if (const Expr *E = getSource(F, PC).asExpr())
    return E;
  llvm::report_fatal_error("missing source expression");
}

SourceLocation SourceMapper::getLocation(const Function *F, CodePtr PC) const {
  return getSource(F, PC).getLoc();
}

SourceRange SourceMapper::getRange(const Function *F, CodePtr PC) const {
  return getSource(F, PC).getRange();
}
