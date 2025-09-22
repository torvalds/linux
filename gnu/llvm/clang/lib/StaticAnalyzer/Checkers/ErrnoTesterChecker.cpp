//=== ErrnoTesterChecker.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines ErrnoTesterChecker, which is used to test functionality of the
// errno_check API.
//
//===----------------------------------------------------------------------===//

#include "ErrnoModeling.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace errno_modeling;

namespace {

class ErrnoTesterChecker : public Checker<eval::Call> {
public:
  bool evalCall(const CallEvent &Call, CheckerContext &C) const;

private:
  /// Evaluate function \code void ErrnoTesterChecker_setErrno(int) \endcode.
  /// Set value of \c errno to the argument.
  static void evalSetErrno(CheckerContext &C, const CallEvent &Call);
  /// Evaluate function \code int ErrnoTesterChecker_getErrno() \endcode.
  /// Return the value of \c errno.
  static void evalGetErrno(CheckerContext &C, const CallEvent &Call);
  /// Evaluate function \code int ErrnoTesterChecker_setErrnoIfError() \endcode.
  /// Simulate a standard library function tha returns 0 on success and 1 on
  /// failure. On the success case \c errno is not allowed to be used (may be
  /// undefined). On the failure case \c errno is set to a fixed value 11 and
  /// is not needed to be checked.
  static void evalSetErrnoIfError(CheckerContext &C, const CallEvent &Call);
  /// Evaluate function \code int ErrnoTesterChecker_setErrnoIfErrorRange()
  /// \endcode. Same as \c ErrnoTesterChecker_setErrnoIfError but \c errno is
  /// set to a range (to be nonzero) at the failure case.
  static void evalSetErrnoIfErrorRange(CheckerContext &C,
                                       const CallEvent &Call);
  /// Evaluate function \code int ErrnoTesterChecker_setErrnoCheckState()
  /// \endcode. This function simulates the following:
  /// - Return 0 and leave \c errno with undefined value.
  ///   This is the case of a successful standard function call.
  ///   For example if \c ftell returns not -1.
  /// - Return 1 and sets \c errno to a specific error code (1).
  ///   This is the case of a failed standard function call.
  ///   The function indicates the failure by a special return value
  ///   that is returned only at failure.
  ///   \c errno can be checked but it is not required.
  ///   For example if \c ftell returns -1.
  /// - Return 2 and may set errno to a value (actually it does not set it).
  ///   This is the case of a standard function call where the failure can only
  ///   be checked by reading from \c errno. The value of \c errno is changed by
  ///   the function only at failure, the user should set \c errno to 0 before
  ///   the call (\c ErrnoChecker does not check for this rule).
  ///   \c strtol is an example of this case, if it returns \c LONG_MIN (or
  ///   \c LONG_MAX). This case applies only if \c LONG_MIN or \c LONG_MAX is
  ///   returned, otherwise the first case in this list applies.
  static void evalSetErrnoCheckState(CheckerContext &C, const CallEvent &Call);

  using EvalFn = std::function<void(CheckerContext &, const CallEvent &)>;
  const CallDescriptionMap<EvalFn> TestCalls{
      {{CDM::SimpleFunc, {"ErrnoTesterChecker_setErrno"}, 1},
       &ErrnoTesterChecker::evalSetErrno},
      {{CDM::SimpleFunc, {"ErrnoTesterChecker_getErrno"}, 0},
       &ErrnoTesterChecker::evalGetErrno},
      {{CDM::SimpleFunc, {"ErrnoTesterChecker_setErrnoIfError"}, 0},
       &ErrnoTesterChecker::evalSetErrnoIfError},
      {{CDM::SimpleFunc, {"ErrnoTesterChecker_setErrnoIfErrorRange"}, 0},
       &ErrnoTesterChecker::evalSetErrnoIfErrorRange},
      {{CDM::SimpleFunc, {"ErrnoTesterChecker_setErrnoCheckState"}, 0},
       &ErrnoTesterChecker::evalSetErrnoCheckState}};
};

} // namespace

void ErrnoTesterChecker::evalSetErrno(CheckerContext &C,
                                      const CallEvent &Call) {
  C.addTransition(setErrnoValue(C.getState(), C.getLocationContext(),
                                Call.getArgSVal(0), Irrelevant));
}

void ErrnoTesterChecker::evalGetErrno(CheckerContext &C,
                                      const CallEvent &Call) {
  ProgramStateRef State = C.getState();

  std::optional<SVal> ErrnoVal = getErrnoValue(State);
  assert(ErrnoVal && "Errno value should be available.");
  State =
      State->BindExpr(Call.getOriginExpr(), C.getLocationContext(), *ErrnoVal);

  C.addTransition(State);
}

void ErrnoTesterChecker::evalSetErrnoIfError(CheckerContext &C,
                                             const CallEvent &Call) {
  ProgramStateRef State = C.getState();
  SValBuilder &SVB = C.getSValBuilder();

  ProgramStateRef StateSuccess = State->BindExpr(
      Call.getOriginExpr(), C.getLocationContext(), SVB.makeIntVal(0, true));
  StateSuccess = setErrnoState(StateSuccess, MustNotBeChecked);

  ProgramStateRef StateFailure = State->BindExpr(
      Call.getOriginExpr(), C.getLocationContext(), SVB.makeIntVal(1, true));
  StateFailure = setErrnoValue(StateFailure, C, 11, Irrelevant);

  C.addTransition(StateSuccess);
  C.addTransition(StateFailure);
}

void ErrnoTesterChecker::evalSetErrnoIfErrorRange(CheckerContext &C,
                                                  const CallEvent &Call) {
  ProgramStateRef State = C.getState();
  SValBuilder &SVB = C.getSValBuilder();

  ProgramStateRef StateSuccess = State->BindExpr(
      Call.getOriginExpr(), C.getLocationContext(), SVB.makeIntVal(0, true));
  StateSuccess = setErrnoState(StateSuccess, MustNotBeChecked);

  ProgramStateRef StateFailure = State->BindExpr(
      Call.getOriginExpr(), C.getLocationContext(), SVB.makeIntVal(1, true));
  DefinedOrUnknownSVal ErrnoVal = SVB.conjureSymbolVal(
      nullptr, Call.getOriginExpr(), C.getLocationContext(), C.blockCount());
  StateFailure = StateFailure->assume(ErrnoVal, true);
  assert(StateFailure && "Failed to assume on an initial value.");
  StateFailure =
      setErrnoValue(StateFailure, C.getLocationContext(), ErrnoVal, Irrelevant);

  C.addTransition(StateSuccess);
  C.addTransition(StateFailure);
}

void ErrnoTesterChecker::evalSetErrnoCheckState(CheckerContext &C,
                                                const CallEvent &Call) {
  ProgramStateRef State = C.getState();
  SValBuilder &SVB = C.getSValBuilder();

  ProgramStateRef StateSuccess = State->BindExpr(
      Call.getOriginExpr(), C.getLocationContext(), SVB.makeIntVal(0, true));
  StateSuccess = setErrnoState(StateSuccess, MustNotBeChecked);

  ProgramStateRef StateFailure1 = State->BindExpr(
      Call.getOriginExpr(), C.getLocationContext(), SVB.makeIntVal(1, true));
  StateFailure1 = setErrnoValue(StateFailure1, C, 1, Irrelevant);

  ProgramStateRef StateFailure2 = State->BindExpr(
      Call.getOriginExpr(), C.getLocationContext(), SVB.makeIntVal(2, true));
  StateFailure2 = setErrnoValue(StateFailure2, C, 2, MustBeChecked);

  C.addTransition(StateSuccess,
                  getErrnoNoteTag(C, "Assuming that this function succeeds but "
                                     "sets 'errno' to an unspecified value."));
  C.addTransition(StateFailure1);
  C.addTransition(
      StateFailure2,
      getErrnoNoteTag(C, "Assuming that this function returns 2. 'errno' "
                         "should be checked to test for failure."));
}

bool ErrnoTesterChecker::evalCall(const CallEvent &Call,
                                  CheckerContext &C) const {
  const EvalFn *Fn = TestCalls.lookup(Call);
  if (Fn) {
    (*Fn)(C, Call);
    return C.isDifferent();
  }
  return false;
}

void ento::registerErrnoTesterChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<ErrnoTesterChecker>();
}

bool ento::shouldRegisterErrnoTesterChecker(const CheckerManager &Mgr) {
  return true;
}
