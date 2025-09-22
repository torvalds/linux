//== DivZeroChecker.cpp - Division by zero checker --------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines DivZeroChecker, a builtin check in ExprEngine that performs
// checks for division by zeros.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace taint;

namespace {
class DivZeroChecker : public Checker< check::PreStmt<BinaryOperator> > {
  const BugType BT{this, "Division by zero"};
  const BugType TaintBT{this, "Division by zero", categories::TaintedData};
  void reportBug(StringRef Msg, ProgramStateRef StateZero,
                 CheckerContext &C) const;
  void reportTaintBug(StringRef Msg, ProgramStateRef StateZero,
                      CheckerContext &C,
                      llvm::ArrayRef<SymbolRef> TaintedSyms) const;

public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
};
} // end anonymous namespace

static const Expr *getDenomExpr(const ExplodedNode *N) {
  const Stmt *S = N->getLocationAs<PreStmt>()->getStmt();
  if (const auto *BE = dyn_cast<BinaryOperator>(S))
    return BE->getRHS();
  return nullptr;
}

void DivZeroChecker::reportBug(StringRef Msg, ProgramStateRef StateZero,
                               CheckerContext &C) const {
  if (ExplodedNode *N = C.generateErrorNode(StateZero)) {
    auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
    bugreporter::trackExpressionValue(N, getDenomExpr(N), *R);
    C.emitReport(std::move(R));
  }
}

void DivZeroChecker::reportTaintBug(
    StringRef Msg, ProgramStateRef StateZero, CheckerContext &C,
    llvm::ArrayRef<SymbolRef> TaintedSyms) const {
  if (ExplodedNode *N = C.generateErrorNode(StateZero)) {
    auto R = std::make_unique<PathSensitiveBugReport>(TaintBT, Msg, N);
    bugreporter::trackExpressionValue(N, getDenomExpr(N), *R);
    for (auto Sym : TaintedSyms)
      R->markInteresting(Sym);
    C.emitReport(std::move(R));
  }
}

void DivZeroChecker::checkPreStmt(const BinaryOperator *B,
                                  CheckerContext &C) const {
  BinaryOperator::Opcode Op = B->getOpcode();
  if (Op != BO_Div &&
      Op != BO_Rem &&
      Op != BO_DivAssign &&
      Op != BO_RemAssign)
    return;

  if (!B->getRHS()->getType()->isScalarType())
    return;

  SVal Denom = C.getSVal(B->getRHS());
  std::optional<DefinedSVal> DV = Denom.getAs<DefinedSVal>();

  // Divide-by-undefined handled in the generic checking for uses of
  // undefined values.
  if (!DV)
    return;

  // Check for divide by zero.
  ConstraintManager &CM = C.getConstraintManager();
  ProgramStateRef stateNotZero, stateZero;
  std::tie(stateNotZero, stateZero) = CM.assumeDual(C.getState(), *DV);

  if (!stateNotZero) {
    assert(stateZero);
    reportBug("Division by zero", stateZero, C);
    return;
  }

  if ((stateNotZero && stateZero)) {
    std::vector<SymbolRef> taintedSyms = getTaintedSymbols(C.getState(), *DV);
    if (!taintedSyms.empty()) {
      reportTaintBug("Division by a tainted value, possibly zero", stateZero, C,
                     taintedSyms);
      return;
    }
  }

  // If we get here, then the denom should not be zero. We abandon the implicit
  // zero denom case for now.
  C.addTransition(stateNotZero);
}

void ento::registerDivZeroChecker(CheckerManager &mgr) {
  mgr.registerChecker<DivZeroChecker>();
}

bool ento::shouldRegisterDivZeroChecker(const CheckerManager &mgr) {
  return true;
}
