//=== UndefBranchChecker.cpp -----------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines UndefBranchChecker, which checks for undefined branch
// condition.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/StmtObjC.h"
#include "clang/AST/Type.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <optional>
#include <utility>

using namespace clang;
using namespace ento;

namespace {

class UndefBranchChecker : public Checker<check::BranchCondition> {
  const BugType BT{this, "Branch condition evaluates to a garbage value"};

  struct FindUndefExpr {
    ProgramStateRef St;
    const LocationContext *LCtx;

    FindUndefExpr(ProgramStateRef S, const LocationContext *L)
        : St(std::move(S)), LCtx(L) {}

    const Expr *FindExpr(const Expr *Ex) {
      if (!MatchesCriteria(Ex))
        return nullptr;

      for (const Stmt *SubStmt : Ex->children())
        if (const Expr *ExI = dyn_cast_or_null<Expr>(SubStmt))
          if (const Expr *E2 = FindExpr(ExI))
            return E2;

      return Ex;
    }

    bool MatchesCriteria(const Expr *Ex) {
      return St->getSVal(Ex, LCtx).isUndef();
    }
  };

public:
  void checkBranchCondition(const Stmt *Condition, CheckerContext &Ctx) const;
};

} // namespace

void UndefBranchChecker::checkBranchCondition(const Stmt *Condition,
                                              CheckerContext &Ctx) const {
  // ObjCForCollection is a loop, but has no actual condition.
  if (isa<ObjCForCollectionStmt>(Condition))
    return;
  if (!Ctx.getSVal(Condition).isUndef())
    return;

  // Generate a sink node, which implicitly marks both outgoing branches as
  // infeasible.
  ExplodedNode *N = Ctx.generateErrorNode();
  if (!N)
    return;
  // What's going on here: we want to highlight the subexpression of the
  // condition that is the most likely source of the "uninitialized
  // branch condition."  We do a recursive walk of the condition's
  // subexpressions and roughly look for the most nested subexpression
  // that binds to Undefined.  We then highlight that expression's range.

  // Get the predecessor node and check if is a PostStmt with the Stmt
  // being the terminator condition.  We want to inspect the state
  // of that node instead because it will contain main information about
  // the subexpressions.

  // Note: any predecessor will do.  They should have identical state,
  // since all the BlockEdge did was act as an error sink since the value
  // had to already be undefined.
  assert(!N->pred_empty());
  const Expr *Ex = cast<Expr>(Condition);
  ExplodedNode *PrevN = *N->pred_begin();
  ProgramPoint P = PrevN->getLocation();
  ProgramStateRef St = N->getState();

  if (std::optional<PostStmt> PS = P.getAs<PostStmt>())
    if (PS->getStmt() == Ex)
      St = PrevN->getState();

  FindUndefExpr FindIt(St, Ctx.getLocationContext());
  Ex = FindIt.FindExpr(Ex);

  // Emit the bug report.
  auto R = std::make_unique<PathSensitiveBugReport>(BT, BT.getDescription(), N);
  bugreporter::trackExpressionValue(N, Ex, *R);
  R->addRange(Ex->getSourceRange());

  Ctx.emitReport(std::move(R));
}

void ento::registerUndefBranchChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefBranchChecker>();
}

bool ento::shouldRegisterUndefBranchChecker(const CheckerManager &mgr) {
  return true;
}
