//== ReturnUndefChecker.cpp -------------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines ReturnUndefChecker, which is a path-sensitive
// check which looks for undefined or garbage values being returned to the
// caller.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class ReturnUndefChecker : public Checker< check::PreStmt<ReturnStmt> > {
  const BugType BT_Undef{this, "Garbage return value"};
  const BugType BT_NullReference{this, "Returning null reference"};

  void emitUndef(CheckerContext &C, const Expr *RetE) const;
  void checkReference(CheckerContext &C, const Expr *RetE,
                      DefinedOrUnknownSVal RetVal) const;
public:
  void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const;
};
}

void ReturnUndefChecker::checkPreStmt(const ReturnStmt *RS,
                                      CheckerContext &C) const {
  const Expr *RetE = RS->getRetValue();
  if (!RetE)
    return;
  SVal RetVal = C.getSVal(RetE);

  const StackFrameContext *SFC = C.getStackFrame();
  QualType RT = CallEvent::getDeclaredResultType(SFC->getDecl());

  if (RetVal.isUndef()) {
    // "return;" is modeled to evaluate to an UndefinedVal. Allow UndefinedVal
    // to be returned in functions returning void to support this pattern:
    //   void foo() {
    //     return;
    //   }
    //   void test() {
    //     return foo();
    //   }
    if (!RT.isNull() && RT->isVoidType())
      return;

    // Not all blocks have explicitly-specified return types; if the return type
    // is not available, but the return value expression has 'void' type, assume
    // Sema already checked it.
    if (RT.isNull() && isa<BlockDecl>(SFC->getDecl()) &&
        RetE->getType()->isVoidType())
      return;

    emitUndef(C, RetE);
    return;
  }

  if (RT.isNull())
    return;

  if (RT->isReferenceType()) {
    checkReference(C, RetE, RetVal.castAs<DefinedOrUnknownSVal>());
    return;
  }
}

static void emitBug(CheckerContext &C, const BugType &BT, StringRef Msg,
                    const Expr *RetE, const Expr *TrackingE = nullptr) {
  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  auto Report = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);

  Report->addRange(RetE->getSourceRange());
  bugreporter::trackExpressionValue(N, TrackingE ? TrackingE : RetE, *Report);

  C.emitReport(std::move(Report));
}

void ReturnUndefChecker::emitUndef(CheckerContext &C, const Expr *RetE) const {
  emitBug(C, BT_Undef, "Undefined or garbage value returned to caller", RetE);
}

void ReturnUndefChecker::checkReference(CheckerContext &C, const Expr *RetE,
                                        DefinedOrUnknownSVal RetVal) const {
  ProgramStateRef StNonNull, StNull;
  std::tie(StNonNull, StNull) = C.getState()->assume(RetVal);

  if (StNonNull) {
    // Going forward, assume the location is non-null.
    C.addTransition(StNonNull);
    return;
  }

  // The return value is known to be null. Emit a bug report.
  emitBug(C, BT_NullReference, BT_NullReference.getDescription(), RetE,
          bugreporter::getDerefExpr(RetE));
}

void ento::registerReturnUndefChecker(CheckerManager &mgr) {
  mgr.registerChecker<ReturnUndefChecker>();
}

bool ento::shouldRegisterReturnUndefChecker(const CheckerManager &mgr) {
  return true;
}
