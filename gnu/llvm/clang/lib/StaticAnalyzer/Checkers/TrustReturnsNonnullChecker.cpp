//== TrustReturnsNonnullChecker.cpp -- API nullability modeling -*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker adds nullability-related assumptions to methods annotated with
// returns_nonnull attribute.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {

class TrustReturnsNonnullChecker : public Checker<check::PostCall> {

public:
  TrustReturnsNonnullChecker(ASTContext &Ctx) {}

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    ProgramStateRef State = C.getState();

    if (isNonNullPtr(Call))
      if (auto L = Call.getReturnValue().getAs<Loc>())
        State = State->assume(*L, /*assumption=*/true);

    C.addTransition(State);
  }

private:
  /// \returns Whether the method declaration has the attribute returns_nonnull.
  bool isNonNullPtr(const CallEvent &Call) const {
    QualType ExprRetType = Call.getResultType();
    const Decl *CallDeclaration =  Call.getDecl();
    if (!ExprRetType->isAnyPointerType() || !CallDeclaration)
      return false;

    return CallDeclaration->hasAttr<ReturnsNonNullAttr>();
  }
};

} // namespace

void ento::registerTrustReturnsNonnullChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<TrustReturnsNonnullChecker>(Mgr.getASTContext());
}

bool ento::shouldRegisterTrustReturnsNonnullChecker(const CheckerManager &mgr) {
  return true;
}
