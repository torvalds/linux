//===------ SemaM68k.cpp -------- M68k target-specific routines -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to M68k.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaM68k.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Sema/ParsedAttr.h"

namespace clang {
SemaM68k::SemaM68k(Sema &S) : SemaBase(S) {}

void SemaM68k::handleInterruptAttr(Decl *D, const ParsedAttr &AL) {
  if (!AL.checkExactlyNumArgs(SemaRef, 1))
    return;

  if (!AL.isArgExpr(0)) {
    Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIntegerConstant;
    return;
  }

  // FIXME: Check for decl - it should be void ()(void).

  Expr *NumParamsExpr = static_cast<Expr *>(AL.getArgAsExpr(0));
  auto MaybeNumParams = NumParamsExpr->getIntegerConstantExpr(getASTContext());
  if (!MaybeNumParams) {
    Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIntegerConstant
        << NumParamsExpr->getSourceRange();
    return;
  }

  unsigned Num = MaybeNumParams->getLimitedValue(255);
  if ((Num & 1) || Num > 30) {
    Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
        << AL << (int)MaybeNumParams->getSExtValue()
        << NumParamsExpr->getSourceRange();
    return;
  }

  D->addAttr(::new (getASTContext())
                 M68kInterruptAttr(getASTContext(), AL, Num));
  D->addAttr(UsedAttr::CreateImplicit(getASTContext()));
}
} // namespace clang
