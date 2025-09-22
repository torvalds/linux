//=== FixedAddressChecker.cpp - Fixed address usage checker ----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This files defines FixedAddressChecker, a builtin checker that checks for
// assignment of a fixed address to a pointer.
// This check corresponds to CWE-587.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class FixedAddressChecker
  : public Checker< check::PreStmt<BinaryOperator> > {
  const BugType BT{this, "Use fixed address"};

public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
};
}

void FixedAddressChecker::checkPreStmt(const BinaryOperator *B,
                                       CheckerContext &C) const {
  // Using a fixed address is not portable because that address will probably
  // not be valid in all environments or platforms.

  if (B->getOpcode() != BO_Assign)
    return;

  QualType T = B->getType();
  if (!T->isPointerType())
    return;

  SVal RV = C.getSVal(B->getRHS());

  if (!RV.isConstant() || RV.isZeroConstant())
    return;

  if (ExplodedNode *N = C.generateNonFatalErrorNode()) {
    // FIXME: improve grammar in the following strings:
    constexpr llvm::StringLiteral Msg =
        "Using a fixed address is not portable because that address will "
        "probably not be valid in all environments or platforms.";
    auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
    R->addRange(B->getRHS()->getSourceRange());
    C.emitReport(std::move(R));
  }
}

void ento::registerFixedAddressChecker(CheckerManager &mgr) {
  mgr.registerChecker<FixedAddressChecker>();
}

bool ento::shouldRegisterFixedAddressChecker(const CheckerManager &mgr) {
  return true;
}
