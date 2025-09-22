//=== ErrnoChecker.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines an "errno checker" that can detect some invalid use of the
// system-defined value 'errno'. This checker works together with the
// ErrnoModeling checker and other checkers like StdCLibraryFunctions.
//
//===----------------------------------------------------------------------===//

#include "ErrnoModeling.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/STLExtras.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace errno_modeling;

namespace {

class ErrnoChecker
    : public Checker<check::Location, check::PreCall, check::RegionChanges> {
public:
  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  ProgramStateRef
  checkRegionChanges(ProgramStateRef State,
                     const InvalidatedSymbols *Invalidated,
                     ArrayRef<const MemRegion *> ExplicitRegions,
                     ArrayRef<const MemRegion *> Regions,
                     const LocationContext *LCtx, const CallEvent *Call) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &Ctx) const;

  /// Indicates if a read (load) of \c errno is allowed in a non-condition part
  /// of \c if, \c switch, loop and conditional statements when the errno
  /// value may be undefined.
  bool AllowErrnoReadOutsideConditions = true;

private:
  void generateErrnoNotCheckedBug(CheckerContext &C, ProgramStateRef State,
                                  const MemRegion *ErrnoRegion,
                                  const CallEvent *CallMayChangeErrno) const;

  BugType BT_InvalidErrnoRead{this, "Value of 'errno' could be undefined",
                              "Error handling"};
  BugType BT_ErrnoNotChecked{this, "Value of 'errno' was not checked",
                             "Error handling"};
};

} // namespace

static ProgramStateRef setErrnoStateIrrelevant(ProgramStateRef State) {
  return setErrnoState(State, Irrelevant);
}

/// Check if a statement (expression) or an ancestor of it is in a condition
/// part of a (conditional, loop, switch) statement.
static bool isInCondition(const Stmt *S, CheckerContext &C) {
  ParentMapContext &ParentCtx = C.getASTContext().getParentMapContext();
  bool CondFound = false;
  while (S && !CondFound) {
    const DynTypedNodeList Parents = ParentCtx.getParents(*S);
    if (Parents.empty())
      break;
    const auto *ParentS = Parents[0].get<Stmt>();
    if (!ParentS || isa<CallExpr>(ParentS))
      break;
    switch (ParentS->getStmtClass()) {
    case Expr::IfStmtClass:
      CondFound = (S == cast<IfStmt>(ParentS)->getCond());
      break;
    case Expr::ForStmtClass:
      CondFound = (S == cast<ForStmt>(ParentS)->getCond());
      break;
    case Expr::DoStmtClass:
      CondFound = (S == cast<DoStmt>(ParentS)->getCond());
      break;
    case Expr::WhileStmtClass:
      CondFound = (S == cast<WhileStmt>(ParentS)->getCond());
      break;
    case Expr::SwitchStmtClass:
      CondFound = (S == cast<SwitchStmt>(ParentS)->getCond());
      break;
    case Expr::ConditionalOperatorClass:
      CondFound = (S == cast<ConditionalOperator>(ParentS)->getCond());
      break;
    case Expr::BinaryConditionalOperatorClass:
      CondFound = (S == cast<BinaryConditionalOperator>(ParentS)->getCommon());
      break;
    default:
      break;
    }
    S = ParentS;
  }
  return CondFound;
}

void ErrnoChecker::generateErrnoNotCheckedBug(
    CheckerContext &C, ProgramStateRef State, const MemRegion *ErrnoRegion,
    const CallEvent *CallMayChangeErrno) const {
  if (ExplodedNode *N = C.generateNonFatalErrorNode(State)) {
    SmallString<100> StrBuf;
    llvm::raw_svector_ostream OS(StrBuf);
    if (CallMayChangeErrno) {
      OS << "Value of 'errno' was not checked and may be overwritten by "
            "function '";
      const auto *CallD =
          dyn_cast_or_null<FunctionDecl>(CallMayChangeErrno->getDecl());
      assert(CallD && CallD->getIdentifier());
      OS << CallD->getIdentifier()->getName() << "'";
    } else {
      OS << "Value of 'errno' was not checked and is overwritten here";
    }
    auto BR = std::make_unique<PathSensitiveBugReport>(BT_ErrnoNotChecked,
                                                       OS.str(), N);
    BR->markInteresting(ErrnoRegion);
    C.emitReport(std::move(BR));
  }
}

void ErrnoChecker::checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                                 CheckerContext &C) const {
  std::optional<ento::Loc> ErrnoLoc = getErrnoLoc(C.getState());
  if (!ErrnoLoc)
    return;

  auto L = Loc.getAs<ento::Loc>();
  if (!L || *ErrnoLoc != *L)
    return;

  ProgramStateRef State = C.getState();
  ErrnoCheckState EState = getErrnoState(State);

  if (IsLoad) {
    switch (EState) {
    case MustNotBeChecked:
      // Read of 'errno' when it may have undefined value.
      if (!AllowErrnoReadOutsideConditions || isInCondition(S, C)) {
        if (ExplodedNode *N = C.generateErrorNode()) {
          auto BR = std::make_unique<PathSensitiveBugReport>(
              BT_InvalidErrnoRead,
              "An undefined value may be read from 'errno'", N);
          BR->markInteresting(ErrnoLoc->getAsRegion());
          C.emitReport(std::move(BR));
        }
      }
      break;
    case MustBeChecked:
      // 'errno' has to be checked. A load is required for this, with no more
      // information we can assume that it is checked somehow.
      // After this place 'errno' is allowed to be read and written.
      State = setErrnoStateIrrelevant(State);
      C.addTransition(State);
      break;
    default:
      break;
    }
  } else {
    switch (EState) {
    case MustBeChecked:
      // 'errno' is overwritten without a read before but it should have been
      // checked.
      generateErrnoNotCheckedBug(C, setErrnoStateIrrelevant(State),
                                 ErrnoLoc->getAsRegion(), nullptr);
      break;
    case MustNotBeChecked:
      // Write to 'errno' when it is not allowed to be read.
      // After this place 'errno' is allowed to be read and written.
      State = setErrnoStateIrrelevant(State);
      C.addTransition(State);
      break;
    default:
      break;
    }
  }
}

void ErrnoChecker::checkPreCall(const CallEvent &Call,
                                CheckerContext &C) const {
  const auto *CallF = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!CallF)
    return;

  CallF = CallF->getCanonicalDecl();
  // If 'errno' must be checked, it should be done as soon as possible, and
  // before any other call to a system function (something in a system header).
  // To avoid use of a long list of functions that may change 'errno'
  // (which may be different with standard library versions) assume that any
  // function can change it.
  // A list of special functions can be used that are allowed here without
  // generation of diagnostic. For now the only such case is 'errno' itself.
  // Probably 'strerror'?
  if (CallF->isExternC() && CallF->isGlobal() &&
      C.getSourceManager().isInSystemHeader(CallF->getLocation()) &&
      !isErrnoLocationCall(Call)) {
    if (getErrnoState(C.getState()) == MustBeChecked) {
      std::optional<ento::Loc> ErrnoLoc = getErrnoLoc(C.getState());
      assert(ErrnoLoc && "ErrnoLoc should exist if an errno state is set.");
      generateErrnoNotCheckedBug(C, setErrnoStateIrrelevant(C.getState()),
                                 ErrnoLoc->getAsRegion(), &Call);
    }
  }
}

ProgramStateRef ErrnoChecker::checkRegionChanges(
    ProgramStateRef State, const InvalidatedSymbols *Invalidated,
    ArrayRef<const MemRegion *> ExplicitRegions,
    ArrayRef<const MemRegion *> Regions, const LocationContext *LCtx,
    const CallEvent *Call) const {
  std::optional<ento::Loc> ErrnoLoc = getErrnoLoc(State);
  if (!ErrnoLoc)
    return State;
  const MemRegion *ErrnoRegion = ErrnoLoc->getAsRegion();

  // If 'errno' is invalidated we can not know if it is checked or written into,
  // allow read and write without bug reports.
  if (llvm::is_contained(Regions, ErrnoRegion))
    return clearErrnoState(State);

  // Always reset errno state when the system memory space is invalidated.
  // The ErrnoRegion is not always found in the list in this case.
  if (llvm::is_contained(Regions, ErrnoRegion->getMemorySpace()))
    return clearErrnoState(State);

  return State;
}

void ento::registerErrnoChecker(CheckerManager &mgr) {
  const AnalyzerOptions &Opts = mgr.getAnalyzerOptions();
  auto *Checker = mgr.registerChecker<ErrnoChecker>();
  Checker->AllowErrnoReadOutsideConditions = Opts.getCheckerBooleanOption(
      Checker, "AllowErrnoReadOutsideConditionExpressions");
}

bool ento::shouldRegisterErrnoChecker(const CheckerManager &mgr) {
  return true;
}
