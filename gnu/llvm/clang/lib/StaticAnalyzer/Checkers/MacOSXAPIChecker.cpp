// MacOSXAPIChecker.h - Checks proper use of various MacOS X APIs --*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines MacOSXAPIChecker, which is an assortment of checks on calls
// to various, widely used Apple APIs.
//
// FIXME: What's currently in BasicObjCFoundationChecks.cpp should be migrated
// to here, using the new Checker interface.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class MacOSXAPIChecker : public Checker< check::PreStmt<CallExpr> > {
  const BugType BT_dispatchOnce{this, "Improper use of 'dispatch_once'",
                                categories::AppleAPIMisuse};

  static const ObjCIvarRegion *getParentIvarRegion(const MemRegion *R);

public:
  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;

  void CheckDispatchOnce(CheckerContext &C, const CallExpr *CE,
                         StringRef FName) const;

  typedef void (MacOSXAPIChecker::*SubChecker)(CheckerContext &,
                                               const CallExpr *,
                                               StringRef FName) const;
};
} //end anonymous namespace

//===----------------------------------------------------------------------===//
// dispatch_once and dispatch_once_f
//===----------------------------------------------------------------------===//

const ObjCIvarRegion *
MacOSXAPIChecker::getParentIvarRegion(const MemRegion *R) {
  const SubRegion *SR = dyn_cast<SubRegion>(R);
  while (SR) {
    if (const ObjCIvarRegion *IR = dyn_cast<ObjCIvarRegion>(SR))
      return IR;
    SR = dyn_cast<SubRegion>(SR->getSuperRegion());
  }
  return nullptr;
}

void MacOSXAPIChecker::CheckDispatchOnce(CheckerContext &C, const CallExpr *CE,
                                         StringRef FName) const {
  if (CE->getNumArgs() < 1)
    return;

  // Check if the first argument is improperly allocated.  If so, issue a
  // warning because that's likely to be bad news.
  const MemRegion *R = C.getSVal(CE->getArg(0)).getAsRegion();
  if (!R)
    return;

  // Global variables are fine.
  const MemRegion *RB = R->getBaseRegion();
  const MemSpaceRegion *RS = RB->getMemorySpace();
  if (isa<GlobalsSpaceRegion>(RS))
    return;

  // Handle _dispatch_once.  In some versions of the OS X SDK we have the case
  // that dispatch_once is a macro that wraps a call to _dispatch_once.
  // _dispatch_once is then a function which then calls the real dispatch_once.
  // Users do not care; they just want the warning at the top-level call.
  if (CE->getBeginLoc().isMacroID()) {
    StringRef TrimmedFName = FName.ltrim('_');
    if (TrimmedFName != FName)
      FName = TrimmedFName;
  }

  SmallString<256> S;
  llvm::raw_svector_ostream os(S);
  bool SuggestStatic = false;
  os << "Call to '" << FName << "' uses";
  if (const VarRegion *VR = dyn_cast<VarRegion>(RB)) {
    const VarDecl *VD = VR->getDecl();
    // FIXME: These should have correct memory space and thus should be filtered
    // out earlier. This branch only fires when we're looking from a block,
    // which we analyze as a top-level declaration, onto a static local
    // in a function that contains the block.
    if (VD->isStaticLocal())
      return;
    // We filtered out globals earlier, so it must be a local variable
    // or a block variable which is under UnknownSpaceRegion.
    if (VR != R)
      os << " memory within";
    if (VD->hasAttr<BlocksAttr>())
      os << " the block variable '";
    else
      os << " the local variable '";
    os << VR->getDecl()->getName() << '\'';
    SuggestStatic = true;
  } else if (const ObjCIvarRegion *IVR = getParentIvarRegion(R)) {
    if (IVR != R)
      os << " memory within";
    os << " the instance variable '" << IVR->getDecl()->getName() << '\'';
  } else if (isa<HeapSpaceRegion>(RS)) {
    os << " heap-allocated memory";
  } else if (isa<UnknownSpaceRegion>(RS)) {
    // Presence of an IVar superregion has priority over this branch, because
    // ObjC objects are on the heap even if the core doesn't realize this.
    // Presence of a block variable base region has priority over this branch,
    // because block variables are known to be either on stack or on heap
    // (might actually move between the two, hence UnknownSpace).
    return;
  } else {
    os << " stack allocated memory";
  }
  os << " for the predicate value.  Using such transient memory for "
        "the predicate is potentially dangerous.";
  if (SuggestStatic)
    os << "  Perhaps you intended to declare the variable as 'static'?";

  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  auto report =
      std::make_unique<PathSensitiveBugReport>(BT_dispatchOnce, os.str(), N);
  report->addRange(CE->getArg(0)->getSourceRange());
  C.emitReport(std::move(report));
}

//===----------------------------------------------------------------------===//
// Central dispatch function.
//===----------------------------------------------------------------------===//

void MacOSXAPIChecker::checkPreStmt(const CallExpr *CE,
                                    CheckerContext &C) const {
  StringRef Name = C.getCalleeName(CE);
  if (Name.empty())
    return;

  SubChecker SC =
    llvm::StringSwitch<SubChecker>(Name)
      .Cases("dispatch_once",
             "_dispatch_once",
             "dispatch_once_f",
             &MacOSXAPIChecker::CheckDispatchOnce)
      .Default(nullptr);

  if (SC)
    (this->*SC)(C, CE, Name);
}

//===----------------------------------------------------------------------===//
// Registration.
//===----------------------------------------------------------------------===//

void ento::registerMacOSXAPIChecker(CheckerManager &mgr) {
  mgr.registerChecker<MacOSXAPIChecker>();
}

bool ento::shouldRegisterMacOSXAPIChecker(const CheckerManager &mgr) {
  return true;
}
