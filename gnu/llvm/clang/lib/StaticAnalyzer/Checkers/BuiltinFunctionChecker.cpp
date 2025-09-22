//=== BuiltinFunctionChecker.cpp --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker evaluates "standalone" clang builtin functions that are not
// just special-cased variants of well-known non-builtin functions.
// Builtin functions like __builtin_memcpy and __builtin_alloca should be
// evaluated by the same checker that handles their non-builtin variant to
// ensure that the two variants are handled consistently.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Builtins.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"

using namespace clang;
using namespace ento;

namespace {

class BuiltinFunctionChecker : public Checker<eval::Call> {
public:
  bool evalCall(const CallEvent &Call, CheckerContext &C) const;

private:
  // From: clang/include/clang/Basic/Builtins.def
  // C++ standard library builtins in namespace 'std'.
  const CallDescriptionSet BuiltinLikeStdFunctions{
      {CDM::SimpleFunc, {"std", "addressof"}},        //
      {CDM::SimpleFunc, {"std", "__addressof"}},      //
      {CDM::SimpleFunc, {"std", "as_const"}},         //
      {CDM::SimpleFunc, {"std", "forward"}},          //
      {CDM::SimpleFunc, {"std", "forward_like"}},     //
      {CDM::SimpleFunc, {"std", "move"}},             //
      {CDM::SimpleFunc, {"std", "move_if_noexcept"}}, //
  };

  bool isBuiltinLikeFunction(const CallEvent &Call) const;
};

} // namespace

bool BuiltinFunctionChecker::isBuiltinLikeFunction(
    const CallEvent &Call) const {
  const auto *FD = llvm::dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD || FD->getNumParams() != 1)
    return false;

  if (QualType RetTy = FD->getReturnType();
      !RetTy->isPointerType() && !RetTy->isReferenceType())
    return false;

  if (QualType ParmTy = FD->getParamDecl(0)->getType();
      !ParmTy->isPointerType() && !ParmTy->isReferenceType())
    return false;

  return BuiltinLikeStdFunctions.contains(Call);
}

bool BuiltinFunctionChecker::evalCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return false;

  const LocationContext *LCtx = C.getLocationContext();
  const Expr *CE = Call.getOriginExpr();

  if (isBuiltinLikeFunction(Call)) {
    C.addTransition(state->BindExpr(CE, LCtx, Call.getArgSVal(0)));
    return true;
  }

  switch (FD->getBuiltinID()) {
  default:
    return false;

  case Builtin::BI__builtin_assume:
  case Builtin::BI__assume: {
    assert (Call.getNumArgs() > 0);
    SVal Arg = Call.getArgSVal(0);
    if (Arg.isUndef())
      return true; // Return true to model purity.

    state = state->assume(Arg.castAs<DefinedOrUnknownSVal>(), true);
    // FIXME: do we want to warn here? Not right now. The most reports might
    // come from infeasible paths, thus being false positives.
    if (!state) {
      C.generateSink(C.getState(), C.getPredecessor());
      return true;
    }

    C.addTransition(state);
    return true;
  }

  case Builtin::BI__builtin_unpredictable:
  case Builtin::BI__builtin_expect:
  case Builtin::BI__builtin_expect_with_probability:
  case Builtin::BI__builtin_assume_aligned:
  case Builtin::BI__builtin_addressof:
  case Builtin::BI__builtin_function_start: {
    // For __builtin_unpredictable, __builtin_expect,
    // __builtin_expect_with_probability and __builtin_assume_aligned,
    // just return the value of the subexpression.
    // __builtin_addressof is going from a reference to a pointer, but those
    // are represented the same way in the analyzer.
    assert (Call.getNumArgs() > 0);
    SVal Arg = Call.getArgSVal(0);
    C.addTransition(state->BindExpr(CE, LCtx, Arg));
    return true;
  }

  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size:
  case Builtin::BI__builtin_constant_p: {
    // This must be resolvable at compile time, so we defer to the constant
    // evaluator for a value.
    SValBuilder &SVB = C.getSValBuilder();
    SVal V = UnknownVal();
    Expr::EvalResult EVResult;
    if (CE->EvaluateAsInt(EVResult, C.getASTContext(), Expr::SE_NoSideEffects)) {
      // Make sure the result has the correct type.
      llvm::APSInt Result = EVResult.Val.getInt();
      BasicValueFactory &BVF = SVB.getBasicValueFactory();
      BVF.getAPSIntType(CE->getType()).apply(Result);
      V = SVB.makeIntVal(Result);
    }

    if (FD->getBuiltinID() == Builtin::BI__builtin_constant_p) {
      // If we didn't manage to figure out if the value is constant or not,
      // it is safe to assume that it's not constant and unsafe to assume
      // that it's constant.
      if (V.isUnknown())
        V = SVB.makeIntVal(0, CE->getType());
    }

    C.addTransition(state->BindExpr(CE, LCtx, V));
    return true;
  }
  }
}

void ento::registerBuiltinFunctionChecker(CheckerManager &mgr) {
  mgr.registerChecker<BuiltinFunctionChecker>();
}

bool ento::shouldRegisterBuiltinFunctionChecker(const CheckerManager &mgr) {
  return true;
}
