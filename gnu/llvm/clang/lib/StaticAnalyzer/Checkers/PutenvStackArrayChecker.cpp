//== PutenvStackArrayChecker.cpp ------------------------------- -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines PutenvStackArrayChecker which finds calls of ``putenv``
// function with automatic array variable as the argument.
// https://wiki.sei.cmu.edu/confluence/x/6NYxBQ
//
//===----------------------------------------------------------------------===//

#include "AllocationState.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"

using namespace clang;
using namespace ento;

namespace {
class PutenvStackArrayChecker : public Checker<check::PostCall> {
private:
  BugType BT{this, "'putenv' called with stack-allocated string",
             categories::SecurityError};
  const CallDescription Putenv{CDM::CLibrary, {"putenv"}, 1};

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
};
} // namespace

void PutenvStackArrayChecker::checkPostCall(const CallEvent &Call,
                                            CheckerContext &C) const {
  if (!Putenv.matches(Call))
    return;

  SVal ArgV = Call.getArgSVal(0);
  const Expr *ArgExpr = Call.getArgExpr(0);

  const auto *SSR =
      dyn_cast<StackSpaceRegion>(ArgV.getAsRegion()->getMemorySpace());
  if (!SSR)
    return;
  const auto *StackFrameFuncD =
      dyn_cast_or_null<FunctionDecl>(SSR->getStackFrame()->getDecl());
  if (StackFrameFuncD && StackFrameFuncD->isMain())
    return;

  StringRef ErrorMsg = "The 'putenv' function should not be called with "
                       "arrays that have automatic storage";
  ExplodedNode *N = C.generateErrorNode();
  auto Report = std::make_unique<PathSensitiveBugReport>(BT, ErrorMsg, N);

  // Track the argument.
  bugreporter::trackExpressionValue(Report->getErrorNode(), ArgExpr, *Report);

  C.emitReport(std::move(Report));
}

void ento::registerPutenvStackArray(CheckerManager &Mgr) {
  Mgr.registerChecker<PutenvStackArrayChecker>();
}

bool ento::shouldRegisterPutenvStackArray(const CheckerManager &) {
  return true;
}
