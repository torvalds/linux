//=== UndefResultChecker.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines UndefResultChecker, a builtin check in ExprEngine that
// performs checks for undefined results of non-assignment binary operators.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class UndefResultChecker
  : public Checker< check::PostStmt<BinaryOperator> > {

  const BugType BT{this, "Result of operation is garbage or undefined"};

public:
  void checkPostStmt(const BinaryOperator *B, CheckerContext &C) const;
};
} // end anonymous namespace

static bool isArrayIndexOutOfBounds(CheckerContext &C, const Expr *Ex) {
  ProgramStateRef state = C.getState();

  if (!isa<ArraySubscriptExpr>(Ex))
    return false;

  SVal Loc = C.getSVal(Ex);
  if (!Loc.isValid())
    return false;

  const MemRegion *MR = Loc.castAs<loc::MemRegionVal>().getRegion();
  const ElementRegion *ER = dyn_cast<ElementRegion>(MR);
  if (!ER)
    return false;

  DefinedOrUnknownSVal Idx = ER->getIndex().castAs<DefinedOrUnknownSVal>();
  DefinedOrUnknownSVal ElementCount = getDynamicElementCount(
      state, ER->getSuperRegion(), C.getSValBuilder(), ER->getValueType());
  ProgramStateRef StInBound, StOutBound;
  std::tie(StInBound, StOutBound) = state->assumeInBoundDual(Idx, ElementCount);
  return StOutBound && !StInBound;
}

void UndefResultChecker::checkPostStmt(const BinaryOperator *B,
                                       CheckerContext &C) const {
  if (C.getSVal(B).isUndef()) {

    // Do not report assignments of uninitialized values inside swap functions.
    // This should allow to swap partially uninitialized structs
    if (const FunctionDecl *EnclosingFunctionDecl =
        dyn_cast<FunctionDecl>(C.getStackFrame()->getDecl()))
      if (C.getCalleeName(EnclosingFunctionDecl) == "swap")
        return;

    // Generate an error node.
    ExplodedNode *N = C.generateErrorNode();
    if (!N)
      return;

    SmallString<256> sbuf;
    llvm::raw_svector_ostream OS(sbuf);
    const Expr *Ex = nullptr;
    bool isLeft = true;

    if (C.getSVal(B->getLHS()).isUndef()) {
      Ex = B->getLHS()->IgnoreParenCasts();
      isLeft = true;
    }
    else if (C.getSVal(B->getRHS()).isUndef()) {
      Ex = B->getRHS()->IgnoreParenCasts();
      isLeft = false;
    }

    if (Ex) {
      OS << "The " << (isLeft ? "left" : "right") << " operand of '"
         << BinaryOperator::getOpcodeStr(B->getOpcode())
         << "' is a garbage value";
      if (isArrayIndexOutOfBounds(C, Ex))
        OS << " due to array index out of bounds";
    } else {
      // Neither operand was undefined, but the result is undefined.
      OS << "The result of the '"
         << BinaryOperator::getOpcodeStr(B->getOpcode())
         << "' expression is undefined";
    }
    auto report = std::make_unique<PathSensitiveBugReport>(BT, OS.str(), N);
    if (Ex) {
      report->addRange(Ex->getSourceRange());
      bugreporter::trackExpressionValue(N, Ex, *report);
    }
    else
      bugreporter::trackExpressionValue(N, B, *report);

    C.emitReport(std::move(report));
  }
}

void ento::registerUndefResultChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefResultChecker>();
}

bool ento::shouldRegisterUndefResultChecker(const CheckerManager &mgr) {
  return true;
}
