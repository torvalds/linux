//=======- VirtualCallChecker.cpp --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a checker that checks virtual method calls during
//  construction or destruction of C++ objects.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"

using namespace clang;
using namespace ento;

namespace {
enum class ObjectState : bool { CtorCalled, DtorCalled };
} // end namespace
  // FIXME: Ascending over StackFrameContext maybe another method.

namespace llvm {
template <> struct FoldingSetTrait<ObjectState> {
  static inline void Profile(ObjectState X, FoldingSetNodeID &ID) {
    ID.AddInteger(static_cast<int>(X));
  }
};
} // end namespace llvm

namespace {
class VirtualCallChecker
    : public Checker<check::BeginFunction, check::EndFunction, check::PreCall> {
public:
  // These are going to be null if the respective check is disabled.
  mutable std::unique_ptr<BugType> BT_Pure, BT_Impure;
  bool ShowFixIts = false;

  void checkBeginFunction(CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

private:
  void registerCtorDtorCallInState(bool IsBeginFunction,
                                   CheckerContext &C) const;
};
} // end namespace

// GDM (generic data map) to the memregion of this for the ctor and dtor.
REGISTER_MAP_WITH_PROGRAMSTATE(CtorDtorMap, const MemRegion *, ObjectState)

// The function to check if a callexpr is a virtual method call.
static bool isVirtualCall(const CallExpr *CE) {
  bool CallIsNonVirtual = false;

  if (const MemberExpr *CME = dyn_cast<MemberExpr>(CE->getCallee())) {
    // The member access is fully qualified (i.e., X::F).
    // Treat this as a non-virtual call and do not warn.
    if (CME->getQualifier())
      CallIsNonVirtual = true;

    if (const Expr *Base = CME->getBase()) {
      // The most derived class is marked final.
      if (Base->getBestDynamicClassType()->hasAttr<FinalAttr>())
        CallIsNonVirtual = true;
    }
  }

  const CXXMethodDecl *MD =
      dyn_cast_or_null<CXXMethodDecl>(CE->getDirectCallee());
  if (MD && MD->isVirtual() && !CallIsNonVirtual && !MD->hasAttr<FinalAttr>() &&
      !MD->getParent()->hasAttr<FinalAttr>())
    return true;
  return false;
}

// The BeginFunction callback when enter a constructor or a destructor.
void VirtualCallChecker::checkBeginFunction(CheckerContext &C) const {
  registerCtorDtorCallInState(true, C);
}

// The EndFunction callback when leave a constructor or a destructor.
void VirtualCallChecker::checkEndFunction(const ReturnStmt *RS,
                                          CheckerContext &C) const {
  registerCtorDtorCallInState(false, C);
}

void VirtualCallChecker::checkPreCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  const auto MC = dyn_cast<CXXMemberCall>(&Call);
  if (!MC)
    return;

  const CXXMethodDecl *MD = dyn_cast_or_null<CXXMethodDecl>(Call.getDecl());
  if (!MD)
    return;

  ProgramStateRef State = C.getState();
  // Member calls are always represented by a call-expression.
  const auto *CE = cast<CallExpr>(Call.getOriginExpr());
  if (!isVirtualCall(CE))
    return;

  const MemRegion *Reg = MC->getCXXThisVal().getAsRegion();
  const ObjectState *ObState = State->get<CtorDtorMap>(Reg);
  if (!ObState)
    return;

  bool IsPure = MD->isPureVirtual();

  // At this point we're sure that we're calling a virtual method
  // during construction or destruction, so we'll emit a report.
  SmallString<128> Msg;
  llvm::raw_svector_ostream OS(Msg);
  OS << "Call to ";
  if (IsPure)
    OS << "pure ";
  OS << "virtual method '" << MD->getParent()->getDeclName()
     << "::" << MD->getDeclName() << "' during ";
  if (*ObState == ObjectState::CtorCalled)
    OS << "construction ";
  else
    OS << "destruction ";
  if (IsPure)
    OS << "has undefined behavior";
  else
    OS << "bypasses virtual dispatch";

  ExplodedNode *N =
      IsPure ? C.generateErrorNode() : C.generateNonFatalErrorNode();
  if (!N)
    return;

  const std::unique_ptr<BugType> &BT = IsPure ? BT_Pure : BT_Impure;
  if (!BT) {
    // The respective check is disabled.
    return;
  }

  auto Report = std::make_unique<PathSensitiveBugReport>(*BT, OS.str(), N);

  if (ShowFixIts && !IsPure) {
    // FIXME: These hints are valid only when the virtual call is made
    // directly from the constructor/destructor. Otherwise the dispatch
    // will work just fine from other callees, and the fix may break
    // the otherwise correct program.
    FixItHint Fixit = FixItHint::CreateInsertion(
        CE->getBeginLoc(), MD->getParent()->getNameAsString() + "::");
    Report->addFixItHint(Fixit);
  }

  C.emitReport(std::move(Report));
}

void VirtualCallChecker::registerCtorDtorCallInState(bool IsBeginFunction,
                                                     CheckerContext &C) const {
  const auto *LCtx = C.getLocationContext();
  const auto *MD = dyn_cast_or_null<CXXMethodDecl>(LCtx->getDecl());
  if (!MD)
    return;

  ProgramStateRef State = C.getState();
  auto &SVB = C.getSValBuilder();

  // Enter a constructor, set the corresponding memregion be true.
  if (isa<CXXConstructorDecl>(MD)) {
    auto ThiSVal =
        State->getSVal(SVB.getCXXThis(MD, LCtx->getStackFrame()));
    const MemRegion *Reg = ThiSVal.getAsRegion();
    if (IsBeginFunction)
      State = State->set<CtorDtorMap>(Reg, ObjectState::CtorCalled);
    else
      State = State->remove<CtorDtorMap>(Reg);

    C.addTransition(State);
    return;
  }

  // Enter a Destructor, set the corresponding memregion be true.
  if (isa<CXXDestructorDecl>(MD)) {
    auto ThiSVal =
        State->getSVal(SVB.getCXXThis(MD, LCtx->getStackFrame()));
    const MemRegion *Reg = ThiSVal.getAsRegion();
    if (IsBeginFunction)
      State = State->set<CtorDtorMap>(Reg, ObjectState::DtorCalled);
    else
      State = State->remove<CtorDtorMap>(Reg);

    C.addTransition(State);
    return;
  }
}

void ento::registerVirtualCallModeling(CheckerManager &Mgr) {
  Mgr.registerChecker<VirtualCallChecker>();
}

void ento::registerPureVirtualCallChecker(CheckerManager &Mgr) {
  auto *Chk = Mgr.getChecker<VirtualCallChecker>();
  Chk->BT_Pure = std::make_unique<BugType>(Mgr.getCurrentCheckerName(),
                                           "Pure virtual method call",
                                           categories::CXXObjectLifecycle);
}

void ento::registerVirtualCallChecker(CheckerManager &Mgr) {
  auto *Chk = Mgr.getChecker<VirtualCallChecker>();
  if (!Mgr.getAnalyzerOptions().getCheckerBooleanOption(
          Mgr.getCurrentCheckerName(), "PureOnly")) {
    Chk->BT_Impure = std::make_unique<BugType>(
        Mgr.getCurrentCheckerName(), "Unexpected loss of virtual dispatch",
        categories::CXXObjectLifecycle);
    Chk->ShowFixIts = Mgr.getAnalyzerOptions().getCheckerBooleanOption(
        Mgr.getCurrentCheckerName(), "ShowFixIts");
  }
}

bool ento::shouldRegisterVirtualCallModeling(const CheckerManager &mgr) {
  const LangOptions &LO = mgr.getLangOpts();
  return LO.CPlusPlus;
}

bool ento::shouldRegisterPureVirtualCallChecker(const CheckerManager &mgr) {
  const LangOptions &LO = mgr.getLangOpts();
  return LO.CPlusPlus;
}

bool ento::shouldRegisterVirtualCallChecker(const CheckerManager &mgr) {
  const LangOptions &LO = mgr.getLangOpts();
  return LO.CPlusPlus;
}
