//=- NSAutoreleasePoolChecker.cpp --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a NSAutoreleasePoolChecker, a small checker that warns
//  about subpar uses of NSAutoreleasePool.  Note that while the check itself
//  (in its current form) could be written as a flow-insensitive check, in
//  can be potentially enhanced in the future with flow-sensitive information.
//  It is also a good example of the CheckerVisitor interface.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"

using namespace clang;
using namespace ento;

namespace {
class NSAutoreleasePoolChecker
  : public Checker<check::PreObjCMessage> {
  const BugType BT{this, "Use -drain instead of -release",
                   "API Upgrade (Apple)"};
  mutable Selector releaseS;

public:
  void checkPreObjCMessage(const ObjCMethodCall &msg, CheckerContext &C) const;
};

} // end anonymous namespace

void NSAutoreleasePoolChecker::checkPreObjCMessage(const ObjCMethodCall &msg,
                                                   CheckerContext &C) const {
  if (!msg.isInstanceMessage())
    return;

  const ObjCInterfaceDecl *OD = msg.getReceiverInterface();
  if (!OD)
    return;
  if (!OD->getIdentifier()->isStr("NSAutoreleasePool"))
    return;

  if (releaseS.isNull())
    releaseS = GetNullarySelector("release", C.getASTContext());
  // Sending 'release' message?
  if (msg.getSelector() != releaseS)
    return;

  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N) {
    assert(0);
    return;
  }

  auto Report = std::make_unique<PathSensitiveBugReport>(
      BT,
      "Use -drain instead of -release when using NSAutoreleasePool and "
      "garbage collection",
      N);
  Report->addRange(msg.getSourceRange());
  C.emitReport(std::move(Report));
}

void ento::registerNSAutoreleasePoolChecker(CheckerManager &mgr) {
  mgr.registerChecker<NSAutoreleasePoolChecker>();
}

bool ento::shouldRegisterNSAutoreleasePoolChecker(const CheckerManager &mgr) {
  const LangOptions &LO = mgr.getLangOpts();
  return LO.getGC() != LangOptions::NonGC;
}
