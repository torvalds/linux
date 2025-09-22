//===--- UndefinedNewArraySizeChecker.cpp -----------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines UndefinedNewArraySizeChecker, a builtin check in ExprEngine
// that checks if the size of the array in a new[] expression is undefined.
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
class UndefinedNewArraySizeChecker : public Checker<check::PreCall> {

private:
  BugType BT{this, "Undefined array element count in new[]",
             categories::LogicError};

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void HandleUndefinedArrayElementCount(CheckerContext &C, SVal ArgVal,
                                        const Expr *Init,
                                        SourceRange Range) const;
};
} // namespace

void UndefinedNewArraySizeChecker::checkPreCall(const CallEvent &Call,
                                                CheckerContext &C) const {
  if (const auto *AC = dyn_cast<CXXAllocatorCall>(&Call)) {
    if (!AC->isArray())
      return;

    auto *SizeEx = *AC->getArraySizeExpr();
    auto SizeVal = AC->getArraySizeVal();

    if (SizeVal.isUndef())
      HandleUndefinedArrayElementCount(C, SizeVal, SizeEx,
                                       SizeEx->getSourceRange());
  }
}

void UndefinedNewArraySizeChecker::HandleUndefinedArrayElementCount(
    CheckerContext &C, SVal ArgVal, const Expr *Init, SourceRange Range) const {

  if (ExplodedNode *N = C.generateErrorNode()) {

    SmallString<100> buf;
    llvm::raw_svector_ostream os(buf);

    os << "Element count in new[] is a garbage value";

    auto R = std::make_unique<PathSensitiveBugReport>(BT, os.str(), N);
    R->markInteresting(ArgVal);
    R->addRange(Range);
    bugreporter::trackExpressionValue(N, Init, *R);

    C.emitReport(std::move(R));
  }
}

void ento::registerUndefinedNewArraySizeChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefinedNewArraySizeChecker>();
}

bool ento::shouldRegisterUndefinedNewArraySizeChecker(
    const CheckerManager &mgr) {
  return mgr.getLangOpts().CPlusPlus;
}
