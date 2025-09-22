//===------ SemaMSP430.cpp ----- MSP430 target-specific routines ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to NVPTX.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaMSP430.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/ParsedAttr.h"

namespace clang {

SemaMSP430::SemaMSP430(Sema &S) : SemaBase(S) {}

void SemaMSP430::handleInterruptAttr(Decl *D, const ParsedAttr &AL) {
  // MSP430 'interrupt' attribute is applied to
  // a function with no parameters and void return type.
  if (!isFuncOrMethodForAttrSubject(D)) {
    Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedFunctionOrMethod;
    return;
  }

  if (hasFunctionProto(D) && getFunctionOrMethodNumParams(D) != 0) {
    Diag(D->getLocation(), diag::warn_interrupt_attribute_invalid)
        << /*MSP430*/ 1 << 0;
    return;
  }

  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    Diag(D->getLocation(), diag::warn_interrupt_attribute_invalid)
        << /*MSP430*/ 1 << 1;
    return;
  }

  // The attribute takes one integer argument.
  if (!AL.checkExactlyNumArgs(SemaRef, 1))
    return;

  if (!AL.isArgExpr(0)) {
    Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIntegerConstant;
    return;
  }

  Expr *NumParamsExpr = static_cast<Expr *>(AL.getArgAsExpr(0));
  std::optional<llvm::APSInt> NumParams = llvm::APSInt(32);
  if (!(NumParams = NumParamsExpr->getIntegerConstantExpr(getASTContext()))) {
    Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIntegerConstant
        << NumParamsExpr->getSourceRange();
    return;
  }
  // The argument should be in range 0..63.
  unsigned Num = NumParams->getLimitedValue(255);
  if (Num > 63) {
    Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
        << AL << (int)NumParams->getSExtValue()
        << NumParamsExpr->getSourceRange();
    return;
  }

  D->addAttr(::new (getASTContext())
                 MSP430InterruptAttr(getASTContext(), AL, Num));
  D->addAttr(UsedAttr::CreateImplicit(getASTContext()));
}

} // namespace clang
