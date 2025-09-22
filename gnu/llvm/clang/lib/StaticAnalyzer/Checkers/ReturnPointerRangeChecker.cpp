//== ReturnPointerRangeChecker.cpp ------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines ReturnPointerRangeChecker, which is a path-sensitive check
// which looks for an out-of-bound pointer being returned to callers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"

using namespace clang;
using namespace ento;

namespace {
class ReturnPointerRangeChecker :
    public Checker< check::PreStmt<ReturnStmt> > {
  // FIXME: This bug correspond to CWE-466.  Eventually we should have bug
  // types explicitly reference such exploit categories (when applicable).
  const BugType BT{this, "Buffer overflow"};

public:
    void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const;
};
}

void ReturnPointerRangeChecker::checkPreStmt(const ReturnStmt *RS,
                                             CheckerContext &C) const {
  ProgramStateRef state = C.getState();

  const Expr *RetE = RS->getRetValue();
  if (!RetE)
    return;

  // Skip "body farmed" functions.
  if (RetE->getSourceRange().isInvalid())
    return;

  SVal V = C.getSVal(RetE);
  const MemRegion *R = V.getAsRegion();

  const ElementRegion *ER = dyn_cast_or_null<ElementRegion>(R);
  if (!ER)
    return;

  DefinedOrUnknownSVal Idx = ER->getIndex().castAs<DefinedOrUnknownSVal>();
  // Zero index is always in bound, this also passes ElementRegions created for
  // pointer casts.
  if (Idx.isZeroConstant())
    return;

  // FIXME: All of this out-of-bounds checking should eventually be refactored
  // into a common place.
  DefinedOrUnknownSVal ElementCount = getDynamicElementCount(
      state, ER->getSuperRegion(), C.getSValBuilder(), ER->getValueType());

  // We assume that the location after the last element in the array is used as
  // end() iterator. Reporting on these would return too many false positives.
  if (Idx == ElementCount)
    return;

  ProgramStateRef StInBound, StOutBound;
  std::tie(StInBound, StOutBound) = state->assumeInBoundDual(Idx, ElementCount);
  if (StOutBound && !StInBound) {
    ExplodedNode *N = C.generateErrorNode(StOutBound);

    if (!N)
      return;

    constexpr llvm::StringLiteral Msg =
        "Returned pointer value points outside the original object "
        "(potential buffer overflow)";

    // Generate a report for this bug.
    auto Report = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
    Report->addRange(RetE->getSourceRange());

    const auto ConcreteElementCount = ElementCount.getAs<nonloc::ConcreteInt>();
    const auto ConcreteIdx = Idx.getAs<nonloc::ConcreteInt>();

    const auto *DeclR = ER->getSuperRegion()->getAs<DeclRegion>();

    if (DeclR)
      Report->addNote("Original object declared here",
                      {DeclR->getDecl(), C.getSourceManager()});

    if (ConcreteElementCount) {
      SmallString<128> SBuf;
      llvm::raw_svector_ostream OS(SBuf);
      OS << "Original object ";
      if (DeclR) {
        OS << "'";
        DeclR->getDecl()->printName(OS);
        OS << "' ";
      }
      OS << "is an array of " << ConcreteElementCount->getValue() << " '";
      ER->getValueType().print(OS,
                               PrintingPolicy(C.getASTContext().getLangOpts()));
      OS << "' objects";
      if (ConcreteIdx) {
        OS << ", returned pointer points at index " << ConcreteIdx->getValue();
      }

      Report->addNote(SBuf,
                      {RetE, C.getSourceManager(), C.getLocationContext()});
    }

    bugreporter::trackExpressionValue(N, RetE, *Report);

    C.emitReport(std::move(Report));
  }
}

void ento::registerReturnPointerRangeChecker(CheckerManager &mgr) {
  mgr.registerChecker<ReturnPointerRangeChecker>();
}

bool ento::shouldRegisterReturnPointerRangeChecker(const CheckerManager &mgr) {
  return true;
}
