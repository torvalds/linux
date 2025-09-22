//===-- InvalidatedIteratorChecker.cpp ----------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a checker for access of invalidated iterators.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"


#include "Iterator.h"

using namespace clang;
using namespace ento;
using namespace iterator;

namespace {

class InvalidatedIteratorChecker
  : public Checker<check::PreCall, check::PreStmt<UnaryOperator>,
                   check::PreStmt<BinaryOperator>,
                   check::PreStmt<ArraySubscriptExpr>,
                   check::PreStmt<MemberExpr>> {

  const BugType InvalidatedBugType{this, "Iterator invalidated",
                                   "Misuse of STL APIs"};

  void verifyAccess(CheckerContext &C, SVal Val) const;
  void reportBug(StringRef Message, SVal Val, CheckerContext &C,
                 ExplodedNode *ErrNode) const;

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreStmt(const UnaryOperator *UO, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkPreStmt(const ArraySubscriptExpr *ASE, CheckerContext &C) const;
  void checkPreStmt(const MemberExpr *ME, CheckerContext &C) const;

};

} // namespace

void InvalidatedIteratorChecker::checkPreCall(const CallEvent &Call,
                                              CheckerContext &C) const {
  // Check for access of invalidated position
  const auto *Func = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!Func)
    return;

  if (Func->isOverloadedOperator() &&
      isAccessOperator(Func->getOverloadedOperator())) {
    // Check for any kind of access of invalidated iterator positions
    if (const auto *InstCall = dyn_cast<CXXInstanceCall>(&Call)) {
      verifyAccess(C, InstCall->getCXXThisVal());
    } else {
      verifyAccess(C, Call.getArgSVal(0));
    }
  }
}

void InvalidatedIteratorChecker::checkPreStmt(const UnaryOperator *UO,
                                              CheckerContext &C) const {
  if (isa<CXXThisExpr>(UO->getSubExpr()))
    return;

  ProgramStateRef State = C.getState();
  UnaryOperatorKind OK = UO->getOpcode();
  SVal SubVal = State->getSVal(UO->getSubExpr(), C.getLocationContext());

  if (isAccessOperator(OK)) {
    verifyAccess(C, SubVal);
  }
}

void InvalidatedIteratorChecker::checkPreStmt(const BinaryOperator *BO,
                                              CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  BinaryOperatorKind OK = BO->getOpcode();
  SVal LVal = State->getSVal(BO->getLHS(), C.getLocationContext());

  if (isAccessOperator(OK)) {
    verifyAccess(C, LVal);
  }
}

void InvalidatedIteratorChecker::checkPreStmt(const ArraySubscriptExpr *ASE,
                                              CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  SVal LVal = State->getSVal(ASE->getLHS(), C.getLocationContext());
  verifyAccess(C, LVal);
}

void InvalidatedIteratorChecker::checkPreStmt(const MemberExpr *ME,
                                              CheckerContext &C) const {
  if (!ME->isArrow() || ME->isImplicitAccess())
    return;

  ProgramStateRef State = C.getState();
  SVal BaseVal = State->getSVal(ME->getBase(), C.getLocationContext());
  verifyAccess(C, BaseVal);
}

void InvalidatedIteratorChecker::verifyAccess(CheckerContext &C,
                                              SVal Val) const {
  auto State = C.getState();
  const auto *Pos = getIteratorPosition(State, Val);
  if (Pos && !Pos->isValid()) {
    auto *N = C.generateErrorNode(State);
    if (!N) {
      return;
    }
    reportBug("Invalidated iterator accessed.", Val, C, N);
  }
}

void InvalidatedIteratorChecker::reportBug(StringRef Message, SVal Val,
                                           CheckerContext &C,
                                           ExplodedNode *ErrNode) const {
  auto R = std::make_unique<PathSensitiveBugReport>(InvalidatedBugType, Message,
                                                    ErrNode);
  R->markInteresting(Val);
  C.emitReport(std::move(R));
}

void ento::registerInvalidatedIteratorChecker(CheckerManager &mgr) {
  mgr.registerChecker<InvalidatedIteratorChecker>();
}

bool ento::shouldRegisterInvalidatedIteratorChecker(const CheckerManager &mgr) {
  return true;
}
