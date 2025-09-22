//== BoolAssignmentChecker.cpp - Boolean assignment checker -----*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines BoolAssignmentChecker, a builtin check in ExprEngine that
// performs checks for assignment of non-Boolean values to Boolean variables.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {
class BoolAssignmentChecker : public Checker<check::Bind> {
  const BugType BT{this, "Assignment of a non-Boolean value"};
  void emitReport(ProgramStateRef State, CheckerContext &C,
                  bool IsTainted = false) const;

public:
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const;
};
} // end anonymous namespace

void BoolAssignmentChecker::emitReport(ProgramStateRef State, CheckerContext &C,
                                       bool IsTainted) const {
  if (ExplodedNode *N = C.generateNonFatalErrorNode(State)) {
    StringRef Msg = IsTainted ? "Might assign a tainted non-Boolean value"
                              : "Assignment of a non-Boolean value";
    C.emitReport(std::make_unique<PathSensitiveBugReport>(BT, Msg, N));
  }
}

static bool isBooleanType(QualType Ty) {
  if (Ty->isBooleanType()) // C++ or C99
    return true;

  if (const TypedefType *TT = Ty->getAs<TypedefType>())
    return TT->getDecl()->getName() == "BOOL" ||  // Objective-C
           TT->getDecl()->getName() == "_Bool" || // stdbool.h < C99
           TT->getDecl()->getName() == "Boolean"; // MacTypes.h

  return false;
}

void BoolAssignmentChecker::checkBind(SVal Loc, SVal Val, const Stmt *S,
                                      CheckerContext &C) const {

  // We are only interested in stores into Booleans.
  const TypedValueRegion *TR =
      dyn_cast_or_null<TypedValueRegion>(Loc.getAsRegion());

  if (!TR)
    return;

  QualType RegTy = TR->getValueType();

  if (!isBooleanType(RegTy))
    return;

  // Get the value of the right-hand side.  We only care about values
  // that are defined (UnknownVals and UndefinedVals are handled by other
  // checkers).
  std::optional<NonLoc> NV = Val.getAs<NonLoc>();
  if (!NV)
    return;

  // Check if the assigned value meets our criteria for correctness.  It must
  // be a value that is either 0 or 1.  One way to check this is to see if
  // the value is possibly < 0 (for a negative value) or greater than 1.
  ProgramStateRef State = C.getState();
  BasicValueFactory &BVF = C.getSValBuilder().getBasicValueFactory();
  ConstraintManager &CM = C.getConstraintManager();

  llvm::APSInt Zero = BVF.getValue(0, RegTy);
  llvm::APSInt One = BVF.getValue(1, RegTy);

  ProgramStateRef StIn, StOut;
  std::tie(StIn, StOut) = CM.assumeInclusiveRangeDual(State, *NV, Zero, One);

  if (!StIn)
    emitReport(StOut, C);
  if (StIn && StOut && taint::isTainted(State, *NV))
    emitReport(StOut, C, /*IsTainted=*/true);
}

void ento::registerBoolAssignmentChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<BoolAssignmentChecker>();
}

bool ento::shouldRegisterBoolAssignmentChecker(const CheckerManager &Mgr) {
  return true;
}
