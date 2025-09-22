//=== CXXSelfAssignmentChecker.cpp -----------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines CXXSelfAssignmentChecker, which tests all custom defined
// copy and move assignment operators for the case of self assignment, thus
// where the parameter refers to the same location where the this pointer
// points to. The checker itself does not do any checks at all, but it
// causes the analyzer to check every copy and move assignment operator twice:
// once for when 'this' aliases with the parameter and once for when it may not.
// It is the task of the other enabled checkers to find the bugs in these two
// different cases.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {

class CXXSelfAssignmentChecker : public Checker<check::BeginFunction> {
public:
  CXXSelfAssignmentChecker();
  void checkBeginFunction(CheckerContext &C) const;
};
}

CXXSelfAssignmentChecker::CXXSelfAssignmentChecker() {}

void CXXSelfAssignmentChecker::checkBeginFunction(CheckerContext &C) const {
  if (!C.inTopFrame())
    return;
  const auto *LCtx = C.getLocationContext();
  const auto *MD = dyn_cast<CXXMethodDecl>(LCtx->getDecl());
  if (!MD)
    return;
  if (!MD->isCopyAssignmentOperator() && !MD->isMoveAssignmentOperator())
    return;
  auto &State = C.getState();
  auto &SVB = C.getSValBuilder();
  auto ThisVal =
      State->getSVal(SVB.getCXXThis(MD, LCtx->getStackFrame()));
  auto Param = SVB.makeLoc(State->getRegion(MD->getParamDecl(0), LCtx));
  auto ParamVal = State->getSVal(Param);

  ProgramStateRef SelfAssignState = State->bindLoc(Param, ThisVal, LCtx);
  const NoteTag *SelfAssignTag =
    C.getNoteTag([MD](PathSensitiveBugReport &BR) -> std::string {
        SmallString<256> Msg;
        llvm::raw_svector_ostream Out(Msg);
        Out << "Assuming " << MD->getParamDecl(0)->getName() << " == *this";
        return std::string(Out.str());
      });
  C.addTransition(SelfAssignState, SelfAssignTag);

  ProgramStateRef NonSelfAssignState = State->bindLoc(Param, ParamVal, LCtx);
  const NoteTag *NonSelfAssignTag =
    C.getNoteTag([MD](PathSensitiveBugReport &BR) -> std::string {
        SmallString<256> Msg;
        llvm::raw_svector_ostream Out(Msg);
        Out << "Assuming " << MD->getParamDecl(0)->getName() << " != *this";
        return std::string(Out.str());
      });
  C.addTransition(NonSelfAssignState, NonSelfAssignTag);
}

void ento::registerCXXSelfAssignmentChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<CXXSelfAssignmentChecker>();
}

bool ento::shouldRegisterCXXSelfAssignmentChecker(const CheckerManager &mgr) {
  return true;
}
