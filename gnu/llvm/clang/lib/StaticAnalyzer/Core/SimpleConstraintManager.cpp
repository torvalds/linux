//== SimpleConstraintManager.cpp --------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines SimpleConstraintManager, a class that provides a
//  simplified constraint manager interface, compared to ConstraintManager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/SimpleConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include <optional>

namespace clang {

namespace ento {

SimpleConstraintManager::~SimpleConstraintManager() {}

ProgramStateRef SimpleConstraintManager::assumeInternal(ProgramStateRef State,
                                                        DefinedSVal Cond,
                                                        bool Assumption) {
  // If we have a Loc value, cast it to a bool NonLoc first.
  if (std::optional<Loc> LV = Cond.getAs<Loc>()) {
    SValBuilder &SVB = State->getStateManager().getSValBuilder();
    QualType T;
    const MemRegion *MR = LV->getAsRegion();
    if (const TypedRegion *TR = dyn_cast_or_null<TypedRegion>(MR))
      T = TR->getLocationType();
    else
      T = SVB.getContext().VoidPtrTy;

    Cond = SVB.evalCast(*LV, SVB.getContext().BoolTy, T).castAs<DefinedSVal>();
  }

  return assume(State, Cond.castAs<NonLoc>(), Assumption);
}

ProgramStateRef SimpleConstraintManager::assume(ProgramStateRef State,
                                                NonLoc Cond, bool Assumption) {
  State = assumeAux(State, Cond, Assumption);
  if (EE)
    return EE->processAssume(State, Cond, Assumption);
  return State;
}

ProgramStateRef SimpleConstraintManager::assumeAux(ProgramStateRef State,
                                                   NonLoc Cond,
                                                   bool Assumption) {

  // We cannot reason about SymSymExprs, and can only reason about some
  // SymIntExprs.
  if (!canReasonAbout(Cond)) {
    // Just add the constraint to the expression without trying to simplify.
    SymbolRef Sym = Cond.getAsSymbol();
    assert(Sym);
    return assumeSymUnsupported(State, Sym, Assumption);
  }

  switch (Cond.getKind()) {
  default:
    llvm_unreachable("'Assume' not implemented for this NonLoc");

  case nonloc::SymbolValKind: {
    nonloc::SymbolVal SV = Cond.castAs<nonloc::SymbolVal>();
    SymbolRef Sym = SV.getSymbol();
    assert(Sym);
    return assumeSym(State, Sym, Assumption);
  }

  case nonloc::ConcreteIntKind: {
    bool b = Cond.castAs<nonloc::ConcreteInt>().getValue() != 0;
    bool isFeasible = b ? Assumption : !Assumption;
    return isFeasible ? State : nullptr;
  }

  case nonloc::PointerToMemberKind: {
    bool IsNull = !Cond.castAs<nonloc::PointerToMember>().isNullMemberPointer();
    bool IsFeasible = IsNull ? Assumption : !Assumption;
    return IsFeasible ? State : nullptr;
  }

  case nonloc::LocAsIntegerKind:
    return assumeInternal(State, Cond.castAs<nonloc::LocAsInteger>().getLoc(),
                          Assumption);
  } // end switch
}

ProgramStateRef SimpleConstraintManager::assumeInclusiveRangeInternal(
    ProgramStateRef State, NonLoc Value, const llvm::APSInt &From,
    const llvm::APSInt &To, bool InRange) {

  assert(From.isUnsigned() == To.isUnsigned() &&
         From.getBitWidth() == To.getBitWidth() &&
         "Values should have same types!");

  if (!canReasonAbout(Value)) {
    // Just add the constraint to the expression without trying to simplify.
    SymbolRef Sym = Value.getAsSymbol();
    assert(Sym);
    return assumeSymInclusiveRange(State, Sym, From, To, InRange);
  }

  switch (Value.getKind()) {
  default:
    llvm_unreachable("'assumeInclusiveRange' is not implemented"
                     "for this NonLoc");

  case nonloc::LocAsIntegerKind:
  case nonloc::SymbolValKind: {
    if (SymbolRef Sym = Value.getAsSymbol())
      return assumeSymInclusiveRange(State, Sym, From, To, InRange);
    return State;
  } // end switch

  case nonloc::ConcreteIntKind: {
    const llvm::APSInt &IntVal = Value.castAs<nonloc::ConcreteInt>().getValue();
    bool IsInRange = IntVal >= From && IntVal <= To;
    bool isFeasible = (IsInRange == InRange);
    return isFeasible ? State : nullptr;
  }
  } // end switch
}

} // end of namespace ento

} // end of namespace clang
