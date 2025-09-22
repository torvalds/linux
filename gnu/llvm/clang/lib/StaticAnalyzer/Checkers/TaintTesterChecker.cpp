//== TaintTesterChecker.cpp ----------------------------------- -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker can be used for testing how taint data is propagated.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;
using namespace taint;

namespace {
class TaintTesterChecker : public Checker<check::PostStmt<Expr>> {
  const BugType BT{this, "Tainted data", "General"};

public:
  void checkPostStmt(const Expr *E, CheckerContext &C) const;
};
}

void TaintTesterChecker::checkPostStmt(const Expr *E,
                                       CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  if (!State)
    return;

  if (isTainted(State, E, C.getLocationContext())) {
    if (ExplodedNode *N = C.generateNonFatalErrorNode()) {
      auto report = std::make_unique<PathSensitiveBugReport>(BT, "tainted", N);
      report->addRange(E->getSourceRange());
      C.emitReport(std::move(report));
    }
  }
}

void ento::registerTaintTesterChecker(CheckerManager &mgr) {
  mgr.registerChecker<TaintTesterChecker>();
}

bool ento::shouldRegisterTaintTesterChecker(const CheckerManager &mgr) {
  return true;
}
