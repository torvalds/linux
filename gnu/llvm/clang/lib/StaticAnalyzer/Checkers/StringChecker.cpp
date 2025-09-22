//=== StringChecker.cpp -------------------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the modeling of the std::basic_string type.
// This involves checking preconditions of the operations and applying the
// effects of the operations, e.g. their post-conditions.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class StringChecker : public Checker<check::PreCall> {
  BugType BT_Null{this, "Dereference of null pointer", categories::LogicError};
  mutable const FunctionDecl *StringConstCharPtrCtor = nullptr;
  mutable CanQualType SizeTypeTy;
  const CallDescription TwoParamStdStringCtor = {
      CDM::CXXMethod, {"std", "basic_string", "basic_string"}, 2, 2};

  bool isCharToStringCtor(const CallEvent &Call, const ASTContext &ACtx) const;

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
};

bool StringChecker::isCharToStringCtor(const CallEvent &Call,
                                       const ASTContext &ACtx) const {
  if (!TwoParamStdStringCtor.matches(Call))
    return false;
  const auto *FD = dyn_cast<FunctionDecl>(Call.getDecl());
  assert(FD);

  // See if we already cached it.
  if (StringConstCharPtrCtor && StringConstCharPtrCtor == FD)
    return true;

  // Verify that the parameters have the expected types:
  // - arg 1: `const CharT *`
  // - arg 2: some allocator - which is definately not `size_t`.
  const QualType Arg1Ty = Call.getArgExpr(0)->getType().getCanonicalType();
  const QualType Arg2Ty = Call.getArgExpr(1)->getType().getCanonicalType();

  if (!Arg1Ty->isPointerType())
    return false;

  // It makes sure that we don't select the `string(const char* p, size_t len)`
  // overload accidentally.
  if (Arg2Ty.getCanonicalType() == ACtx.getSizeType())
    return false;

  StringConstCharPtrCtor = FD; // Cache the decl of the right overload.
  return true;
}

void StringChecker::checkPreCall(const CallEvent &Call,
                                 CheckerContext &C) const {
  if (!isCharToStringCtor(Call, C.getASTContext()))
    return;
  const auto Param = Call.getArgSVal(0).getAs<Loc>();
  if (!Param)
    return;

  // We managed to constrain the parameter to non-null.
  ProgramStateRef NotNull, Null;
  std::tie(NotNull, Null) = C.getState()->assume(*Param);

  if (NotNull) {
    const auto Callback = [Param](PathSensitiveBugReport &BR) -> std::string {
      return BR.isInteresting(*Param) ? "Assuming the pointer is not null."
                                      : "";
    };

    // Emit note only if this operation constrained the pointer to be null.
    C.addTransition(NotNull, Null ? C.getNoteTag(Callback) : nullptr);
    return;
  }

  // We found a path on which the parameter is NULL.
  if (ExplodedNode *N = C.generateErrorNode(C.getState())) {
    auto R = std::make_unique<PathSensitiveBugReport>(
        BT_Null, "The parameter must not be null", N);
    bugreporter::trackExpressionValue(N, Call.getArgExpr(0), *R);
    C.emitReport(std::move(R));
  }
}

} // end anonymous namespace

void ento::registerStringChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<StringChecker>();
}

bool ento::shouldRegisterStringChecker(const CheckerManager &) { return true; }
