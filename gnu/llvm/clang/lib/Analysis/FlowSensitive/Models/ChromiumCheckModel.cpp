//===-- ChromiumCheckModel.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/Models/ChromiumCheckModel.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "llvm/ADT/DenseSet.h"

namespace clang {
namespace dataflow {

/// Determines whether `D` is one of the methods used to implement Chromium's
/// `CHECK` macros. Populates `CheckDecls`, if empty.
bool isCheckLikeMethod(llvm::SmallDenseSet<const CXXMethodDecl *> &CheckDecls,
                       const CXXMethodDecl &D) {
  // All of the methods of interest are static, so avoid any lookup for
  // non-static methods (the common case).
  if (!D.isStatic())
    return false;

  if (CheckDecls.empty()) {
    // Attempt to initialize `CheckDecls` with the methods in class
    // `CheckError`.
    const CXXRecordDecl *ParentClass = D.getParent();
    if (ParentClass == nullptr || !ParentClass->getDeclName().isIdentifier() ||
        ParentClass->getName() != "CheckError")
      return false;

    // Check whether namespace is "logging".
    const auto *N =
        dyn_cast_or_null<NamespaceDecl>(ParentClass->getDeclContext());
    if (N == nullptr || !N->getDeclName().isIdentifier() ||
        N->getName() != "logging")
      return false;

    // Check whether "logging" is a top-level namespace.
    if (N->getParent() == nullptr || !N->getParent()->isTranslationUnit())
      return false;

    for (const CXXMethodDecl *M : ParentClass->methods())
      if (M->getDeclName().isIdentifier() && M->getName().ends_with("Check"))
        CheckDecls.insert(M);
  }

  return CheckDecls.contains(&D);
}

bool ChromiumCheckModel::transfer(const CFGElement &Element, Environment &Env) {
  auto CS = Element.getAs<CFGStmt>();
  if (!CS)
    return false;
  auto Stmt = CS->getStmt();
  if (const auto *Call = dyn_cast<CallExpr>(Stmt)) {
    if (const auto *M = dyn_cast<CXXMethodDecl>(Call->getDirectCallee())) {
      if (isCheckLikeMethod(CheckDecls, *M)) {
        // Mark this branch as unreachable.
        Env.assume(Env.arena().makeLiteral(false));
        return true;
      }
    }
  }
  return false;
}

} // namespace dataflow
} // namespace clang
