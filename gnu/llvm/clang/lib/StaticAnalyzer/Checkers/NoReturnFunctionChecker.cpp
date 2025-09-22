//=== NoReturnFunctionChecker.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines NoReturnFunctionChecker, which evaluates functions that do not
// return to the caller.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Attr.h"
#include "clang/Analysis/SelectorExtras.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/StringSwitch.h"
#include <cstdarg>

using namespace clang;
using namespace ento;

namespace {

class NoReturnFunctionChecker : public Checker< check::PostCall,
                                                check::PostObjCMessage > {
  mutable Selector HandleFailureInFunctionSel;
  mutable Selector HandleFailureInMethodSel;
public:
  void checkPostCall(const CallEvent &CE, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &msg, CheckerContext &C) const;
};

}

void NoReturnFunctionChecker::checkPostCall(const CallEvent &CE,
                                            CheckerContext &C) const {
  bool BuildSinks = false;

  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(CE.getDecl()))
    BuildSinks = FD->hasAttr<AnalyzerNoReturnAttr>() || FD->isNoReturn();

  if (const CallExpr *CExpr = dyn_cast_or_null<CallExpr>(CE.getOriginExpr());
      CExpr && !BuildSinks) {
    if (const Expr *C = CExpr->getCallee())
      BuildSinks = getFunctionExtInfo(C->getType()).getNoReturn();
  }

  if (!BuildSinks && CE.isGlobalCFunction()) {
    if (const IdentifierInfo *II = CE.getCalleeIdentifier()) {
      // HACK: Some functions are not marked noreturn, and don't return.
      //  Here are a few hardwired ones.  If this takes too long, we can
      //  potentially cache these results.
      BuildSinks
        = llvm::StringSwitch<bool>(StringRef(II->getName()))
            .Case("exit", true)
            .Case("panic", true)
            .Case("error", true)
            .Case("Assert", true)
            // FIXME: This is just a wrapper around throwing an exception.
            //  Eventually inter-procedural analysis should handle this easily.
            .Case("ziperr", true)
            .Case("assfail", true)
            .Case("db_error", true)
            .Case("__assert", true)
            .Case("__assert2", true)
            // For the purpose of static analysis, we do not care that
            //  this MSVC function will return if the user decides to continue.
            .Case("_wassert", true)
            .Case("__assert_rtn", true)
            .Case("__assert_fail", true)
            .Case("dtrace_assfail", true)
            .Case("yy_fatal_error", true)
            .Case("_XCAssertionFailureHandler", true)
            .Case("_DTAssertionFailureHandler", true)
            .Case("_TSAssertionFailureHandler", true)
            .Default(false);
    }
  }

  if (BuildSinks)
    C.generateSink(C.getState(), C.getPredecessor());
}

void NoReturnFunctionChecker::checkPostObjCMessage(const ObjCMethodCall &Msg,
                                                   CheckerContext &C) const {
  // Check if the method is annotated with analyzer_noreturn.
  if (const ObjCMethodDecl *MD = Msg.getDecl()) {
    MD = MD->getCanonicalDecl();
    if (MD->hasAttr<AnalyzerNoReturnAttr>()) {
      C.generateSink(C.getState(), C.getPredecessor());
      return;
    }
  }

  // HACK: This entire check is to handle two messages in the Cocoa frameworks:
  // -[NSAssertionHandler
  //    handleFailureInMethod:object:file:lineNumber:description:]
  // -[NSAssertionHandler
  //    handleFailureInFunction:file:lineNumber:description:]
  // Eventually these should be annotated with __attribute__((noreturn)).
  // Because ObjC messages use dynamic dispatch, it is not generally safe to
  // assume certain methods can't return. In cases where it is definitely valid,
  // see if you can mark the methods noreturn or analyzer_noreturn instead of
  // adding more explicit checks to this method.

  if (!Msg.isInstanceMessage())
    return;

  const ObjCInterfaceDecl *Receiver = Msg.getReceiverInterface();
  if (!Receiver)
    return;
  if (!Receiver->getIdentifier()->isStr("NSAssertionHandler"))
    return;

  Selector Sel = Msg.getSelector();
  switch (Sel.getNumArgs()) {
  default:
    return;
  case 4:
    lazyInitKeywordSelector(HandleFailureInFunctionSel, C.getASTContext(),
                            "handleFailureInFunction", "file", "lineNumber",
                            "description");
    if (Sel != HandleFailureInFunctionSel)
      return;
    break;
  case 5:
    lazyInitKeywordSelector(HandleFailureInMethodSel, C.getASTContext(),
                            "handleFailureInMethod", "object", "file",
                            "lineNumber", "description");
    if (Sel != HandleFailureInMethodSel)
      return;
    break;
  }

  // If we got here, it's one of the messages we care about.
  C.generateSink(C.getState(), C.getPredecessor());
}

void ento::registerNoReturnFunctionChecker(CheckerManager &mgr) {
  mgr.registerChecker<NoReturnFunctionChecker>();
}

bool ento::shouldRegisterNoReturnFunctionChecker(const CheckerManager &mgr) {
  return true;
}
