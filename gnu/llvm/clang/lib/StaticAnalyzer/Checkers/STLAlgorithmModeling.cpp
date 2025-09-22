//===-- STLAlgorithmModeling.cpp ----------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Models STL algorithms.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

#include "Iterator.h"

using namespace clang;
using namespace ento;
using namespace iterator;

namespace {

class STLAlgorithmModeling : public Checker<eval::Call> {
  bool evalFind(CheckerContext &C, const CallExpr *CE) const;

  void Find(CheckerContext &C, const CallExpr *CE, unsigned paramNum) const;

  using FnCheck = bool (STLAlgorithmModeling::*)(CheckerContext &,
                                                const CallExpr *) const;

  const CallDescriptionMap<FnCheck> Callbacks = {
      {{CDM::SimpleFunc, {"std", "find"}, 3}, &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find"}, 4}, &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_if"}, 3},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_if"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_if_not"}, 3},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_if_not"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_first_of"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_first_of"}, 5},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_first_of"}, 6},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_end"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_end"}, 5},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "find_end"}, 6},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "lower_bound"}, 3},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "lower_bound"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "upper_bound"}, 3},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "upper_bound"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "search"}, 3},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "search"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "search"}, 5},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "search"}, 6},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "search_n"}, 4},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "search_n"}, 5},
       &STLAlgorithmModeling::evalFind},
      {{CDM::SimpleFunc, {"std", "search_n"}, 6},
       &STLAlgorithmModeling::evalFind},
  };

public:
  STLAlgorithmModeling() = default;

  bool AggressiveStdFindModeling = false;

  bool evalCall(const CallEvent &Call, CheckerContext &C) const;
}; //

bool STLAlgorithmModeling::evalCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  const auto *CE = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());
  if (!CE)
    return false;

  const FnCheck *Handler = Callbacks.lookup(Call);
  if (!Handler)
    return false;

  return (this->**Handler)(C, CE);
}

bool STLAlgorithmModeling::evalFind(CheckerContext &C,
                                    const CallExpr *CE) const {
  // std::find()-like functions either take their primary range in the first
  // two parameters, or if the first parameter is "execution policy" then in
  // the second and third. This means that the second parameter must always be
  // an iterator.
  if (!isIteratorType(CE->getArg(1)->getType()))
    return false;

  // If no "execution policy" parameter is used then the first argument is the
  // beginning of the range.
  if (isIteratorType(CE->getArg(0)->getType())) {
    Find(C, CE, 0);
    return true;
  }

  // If "execution policy" parameter is used then the second argument is the
  // beginning of the range.
  if (isIteratorType(CE->getArg(2)->getType())) {
    Find(C, CE, 1);
    return true;
  }

  return false;
}

void STLAlgorithmModeling::Find(CheckerContext &C, const CallExpr *CE,
                                unsigned paramNum) const {
  auto State = C.getState();
  auto &SVB = C.getSValBuilder();
  const auto *LCtx = C.getLocationContext();

  SVal RetVal = SVB.conjureSymbolVal(nullptr, CE, LCtx, C.blockCount());
  SVal Param = State->getSVal(CE->getArg(paramNum), LCtx);

  auto StateFound = State->BindExpr(CE, LCtx, RetVal);

  // If we have an iterator position for the range-begin argument then we can
  // assume that in case of successful search the position of the found element
  // is not ahead of it.
  // FIXME: Reverse iterators
  const auto *Pos = getIteratorPosition(State, Param);
  if (Pos) {
    StateFound = createIteratorPosition(StateFound, RetVal, Pos->getContainer(),
                                        CE, LCtx, C.blockCount());
    const auto *NewPos = getIteratorPosition(StateFound, RetVal);
    assert(NewPos && "Failed to create new iterator position.");

    SVal GreaterOrEqual = SVB.evalBinOp(StateFound, BO_GE,
                                        nonloc::SymbolVal(NewPos->getOffset()),
                                        nonloc::SymbolVal(Pos->getOffset()),
                                        SVB.getConditionType());
    assert(isa<DefinedSVal>(GreaterOrEqual) &&
           "Symbol comparison must be a `DefinedSVal`");
    StateFound = StateFound->assume(GreaterOrEqual.castAs<DefinedSVal>(), true);
  }

  Param = State->getSVal(CE->getArg(paramNum + 1), LCtx);

  // If we have an iterator position for the range-end argument then we can
  // assume that in case of successful search the position of the found element
  // is ahead of it.
  // FIXME: Reverse iterators
  Pos = getIteratorPosition(State, Param);
  if (Pos) {
    StateFound = createIteratorPosition(StateFound, RetVal, Pos->getContainer(),
                                        CE, LCtx, C.blockCount());
    const auto *NewPos = getIteratorPosition(StateFound, RetVal);
    assert(NewPos && "Failed to create new iterator position.");

    SVal Less = SVB.evalBinOp(StateFound, BO_LT,
                              nonloc::SymbolVal(NewPos->getOffset()),
                              nonloc::SymbolVal(Pos->getOffset()),
                              SVB.getConditionType());
    assert(isa<DefinedSVal>(Less) &&
           "Symbol comparison must be a `DefinedSVal`");
    StateFound = StateFound->assume(Less.castAs<DefinedSVal>(), true);
  }

  C.addTransition(StateFound);

  if (AggressiveStdFindModeling) {
    auto StateNotFound = State->BindExpr(CE, LCtx, Param);
    C.addTransition(StateNotFound);
  }
}

} // namespace

void ento::registerSTLAlgorithmModeling(CheckerManager &Mgr) {
  auto *Checker = Mgr.registerChecker<STLAlgorithmModeling>();
  Checker->AggressiveStdFindModeling =
      Mgr.getAnalyzerOptions().getCheckerBooleanOption(Checker,
                                                  "AggressiveStdFindModeling");
}

bool ento::shouldRegisterSTLAlgorithmModeling(const CheckerManager &mgr) {
  return true;
}

