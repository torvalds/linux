//===-- ChrootChecker.cpp - chroot usage checks ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines chroot checker, which checks improper use of chroot.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

using namespace clang;
using namespace ento;

namespace {

// enum value that represent the jail state
enum Kind { NO_CHROOT, ROOT_CHANGED, JAIL_ENTERED };

bool isRootChanged(intptr_t k) { return k == ROOT_CHANGED; }
//bool isJailEntered(intptr_t k) { return k == JAIL_ENTERED; }

// This checker checks improper use of chroot.
// The state transition:
// NO_CHROOT ---chroot(path)--> ROOT_CHANGED ---chdir(/) --> JAIL_ENTERED
//                                  |                               |
//         ROOT_CHANGED<--chdir(..)--      JAIL_ENTERED<--chdir(..)--
//                                  |                               |
//                      bug<--foo()--          JAIL_ENTERED<--foo()--
class ChrootChecker : public Checker<eval::Call, check::PreCall> {
  // This bug refers to possibly break out of a chroot() jail.
  const BugType BT_BreakJail{this, "Break out of jail"};

  const CallDescription Chroot{CDM::CLibrary, {"chroot"}, 1},
      Chdir{CDM::CLibrary, {"chdir"}, 1};

public:
  ChrootChecker() {}

  static void *getTag() {
    static int x;
    return &x;
  }

  bool evalCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

private:
  void evalChroot(const CallEvent &Call, CheckerContext &C) const;
  void evalChdir(const CallEvent &Call, CheckerContext &C) const;
};

} // end anonymous namespace

bool ChrootChecker::evalCall(const CallEvent &Call, CheckerContext &C) const {
  if (Chroot.matches(Call)) {
    evalChroot(Call, C);
    return true;
  }
  if (Chdir.matches(Call)) {
    evalChdir(Call, C);
    return true;
  }

  return false;
}

void ChrootChecker::evalChroot(const CallEvent &Call, CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  ProgramStateManager &Mgr = state->getStateManager();

  // Once encouter a chroot(), set the enum value ROOT_CHANGED directly in
  // the GDM.
  state = Mgr.addGDM(state, ChrootChecker::getTag(), (void*) ROOT_CHANGED);
  C.addTransition(state);
}

void ChrootChecker::evalChdir(const CallEvent &Call, CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  ProgramStateManager &Mgr = state->getStateManager();

  // If there are no jail state in the GDM, just return.
  const void *k = state->FindGDM(ChrootChecker::getTag());
  if (!k)
    return;

  // After chdir("/"), enter the jail, set the enum value JAIL_ENTERED.
  const Expr *ArgExpr = Call.getArgExpr(0);
  SVal ArgVal = C.getSVal(ArgExpr);

  if (const MemRegion *R = ArgVal.getAsRegion()) {
    R = R->StripCasts();
    if (const StringRegion* StrRegion= dyn_cast<StringRegion>(R)) {
      const StringLiteral* Str = StrRegion->getStringLiteral();
      if (Str->getString() == "/")
        state = Mgr.addGDM(state, ChrootChecker::getTag(),
                           (void*) JAIL_ENTERED);
    }
  }

  C.addTransition(state);
}

// Check the jail state before any function call except chroot and chdir().
void ChrootChecker::checkPreCall(const CallEvent &Call,
                                 CheckerContext &C) const {
  // Ignore chroot and chdir.
  if (matchesAny(Call, Chroot, Chdir))
    return;

  // If jail state is ROOT_CHANGED, generate BugReport.
  void *const* k = C.getState()->FindGDM(ChrootChecker::getTag());
  if (k)
    if (isRootChanged((intptr_t) *k))
      if (ExplodedNode *N = C.generateNonFatalErrorNode()) {
        constexpr llvm::StringLiteral Msg =
            "No call of chdir(\"/\") immediately after chroot";
        C.emitReport(
            std::make_unique<PathSensitiveBugReport>(BT_BreakJail, Msg, N));
      }
}

void ento::registerChrootChecker(CheckerManager &mgr) {
  mgr.registerChecker<ChrootChecker>();
}

bool ento::shouldRegisterChrootChecker(const CheckerManager &mgr) {
  return true;
}
