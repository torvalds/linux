//===--- ASTLambda.h - Lambda Helper Functions --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides some common utility functions for processing
/// Lambda related AST Constructs.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTLAMBDA_H
#define LLVM_CLANG_AST_ASTLAMBDA_H

#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"

namespace clang {
inline StringRef getLambdaStaticInvokerName() {
  return "__invoke";
}
// This function returns true if M is a specialization, a template,
// or a non-generic lambda call operator.
inline bool isLambdaCallOperator(const CXXMethodDecl *MD) {
  const CXXRecordDecl *LambdaClass = MD->getParent();
  if (!LambdaClass || !LambdaClass->isLambda()) return false;
  return MD->getOverloadedOperator() == OO_Call;
}

inline bool isLambdaCallOperator(const DeclContext *DC) {
  if (!DC || !isa<CXXMethodDecl>(DC)) return false;
  return isLambdaCallOperator(cast<CXXMethodDecl>(DC));
}

inline bool isLambdaCallWithExplicitObjectParameter(const DeclContext *DC) {
  return isLambdaCallOperator(DC) &&
         cast<CXXMethodDecl>(DC)->isExplicitObjectMemberFunction();
}

inline bool isLambdaCallWithImplicitObjectParameter(const DeclContext *DC) {
  return isLambdaCallOperator(DC) &&
         // FIXME: Checking for a null type is not great
         // but lambdas with invalid captures or whose closure parameter list
         // have not fully been parsed may have a call operator whose type is
         // null.
         !cast<CXXMethodDecl>(DC)->getType().isNull() &&
         !cast<CXXMethodDecl>(DC)->isExplicitObjectMemberFunction();
}

inline bool isGenericLambdaCallOperatorSpecialization(const CXXMethodDecl *MD) {
  if (!MD) return false;
  const CXXRecordDecl *LambdaClass = MD->getParent();
  if (LambdaClass && LambdaClass->isGenericLambda())
    return isLambdaCallOperator(MD) &&
                    MD->isFunctionTemplateSpecialization();
  return false;
}

inline bool isLambdaConversionOperator(CXXConversionDecl *C) {
  return C ? C->getParent()->isLambda() : false;
}

inline bool isLambdaConversionOperator(Decl *D) {
  if (!D) return false;
  if (CXXConversionDecl *Conv = dyn_cast<CXXConversionDecl>(D))
    return isLambdaConversionOperator(Conv);
  if (FunctionTemplateDecl *F = dyn_cast<FunctionTemplateDecl>(D))
    if (CXXConversionDecl *Conv =
        dyn_cast_or_null<CXXConversionDecl>(F->getTemplatedDecl()))
      return isLambdaConversionOperator(Conv);
  return false;
}

inline bool isGenericLambdaCallOperatorSpecialization(DeclContext *DC) {
  return isGenericLambdaCallOperatorSpecialization(
                                          dyn_cast<CXXMethodDecl>(DC));
}

inline bool isGenericLambdaCallOperatorOrStaticInvokerSpecialization(
    const DeclContext *DC) {
  const auto *MD = dyn_cast<CXXMethodDecl>(DC);
  if (!MD) return false;
  const CXXRecordDecl *LambdaClass = MD->getParent();
  if (LambdaClass && LambdaClass->isGenericLambda())
    return (isLambdaCallOperator(MD) || MD->isLambdaStaticInvoker()) &&
                    MD->isFunctionTemplateSpecialization();
  return false;
}

// This returns the parent DeclContext ensuring that the correct
// parent DeclContext is returned for Lambdas
inline DeclContext *getLambdaAwareParentOfDeclContext(DeclContext *DC) {
  if (isLambdaCallOperator(DC))
    return DC->getParent()->getParent();
  else
    return DC->getParent();
}

} // clang

#endif
