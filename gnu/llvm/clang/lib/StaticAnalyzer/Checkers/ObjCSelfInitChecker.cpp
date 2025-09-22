//== ObjCSelfInitChecker.cpp - Checker for 'self' initialization -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines ObjCSelfInitChecker, a builtin check that checks for uses of
// 'self' before proper initialization.
//
//===----------------------------------------------------------------------===//

// This checks initialization methods to verify that they assign 'self' to the
// result of an initialization call (e.g. [super init], or [self initWith..])
// before using 'self' or any instance variable.
//
// To perform the required checking, values are tagged with flags that indicate
// 1) if the object is the one pointed to by 'self', and 2) if the object
// is the result of an initializer (e.g. [super init]).
//
// Uses of an object that is true for 1) but not 2) trigger a diagnostic.
// The uses that are currently checked are:
//  - Using instance variables.
//  - Returning the object.
//
// Note that we don't check for an invalid 'self' that is the receiver of an
// obj-c message expression to cut down false positives where logging functions
// get information from self (like its class) or doing "invalidation" on self
// when the initialization fails.
//
// Because the object that 'self' points to gets invalidated when a call
// receives a reference to 'self', the checker keeps track and passes the flags
// for 1) and 2) to the new object that 'self' points to after the call.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ParentMap.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

static bool shouldRunOnFunctionOrMethod(const NamedDecl *ND);
static bool isInitializationMethod(const ObjCMethodDecl *MD);
static bool isInitMessage(const ObjCMethodCall &Msg);
static bool isSelfVar(SVal location, CheckerContext &C);

namespace {
class ObjCSelfInitChecker : public Checker<  check::PostObjCMessage,
                                             check::PostStmt<ObjCIvarRefExpr>,
                                             check::PreStmt<ReturnStmt>,
                                             check::PreCall,
                                             check::PostCall,
                                             check::Location,
                                             check::Bind > {
  const BugType BT{this, "Missing \"self = [(super or self) init...]\"",
                   categories::CoreFoundationObjectiveC};

  void checkForInvalidSelf(const Expr *E, CheckerContext &C,
                           const char *errorStr) const;

public:
  void checkPostObjCMessage(const ObjCMethodCall &Msg, CheckerContext &C) const;
  void checkPostStmt(const ObjCIvarRefExpr *E, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *S, CheckerContext &C) const;
  void checkLocation(SVal location, bool isLoad, const Stmt *S,
                     CheckerContext &C) const;
  void checkBind(SVal loc, SVal val, const Stmt *S, CheckerContext &C) const;

  void checkPreCall(const CallEvent &CE, CheckerContext &C) const;
  void checkPostCall(const CallEvent &CE, CheckerContext &C) const;

  void printState(raw_ostream &Out, ProgramStateRef State,
                  const char *NL, const char *Sep) const override;
};
} // end anonymous namespace

namespace {
enum SelfFlagEnum {
  /// No flag set.
  SelfFlag_None = 0x0,
  /// Value came from 'self'.
  SelfFlag_Self    = 0x1,
  /// Value came from the result of an initializer (e.g. [super init]).
  SelfFlag_InitRes = 0x2
};
}

REGISTER_MAP_WITH_PROGRAMSTATE(SelfFlag, SymbolRef, SelfFlagEnum)
REGISTER_TRAIT_WITH_PROGRAMSTATE(CalledInit, bool)

/// A call receiving a reference to 'self' invalidates the object that
/// 'self' contains. This keeps the "self flags" assigned to the 'self'
/// object before the call so we can assign them to the new object that 'self'
/// points to after the call.
REGISTER_TRAIT_WITH_PROGRAMSTATE(PreCallSelfFlags, SelfFlagEnum)

static SelfFlagEnum getSelfFlags(SVal val, ProgramStateRef state) {
  if (SymbolRef sym = val.getAsSymbol())
    if (const SelfFlagEnum *attachedFlags = state->get<SelfFlag>(sym))
      return *attachedFlags;
  return SelfFlag_None;
}

static SelfFlagEnum getSelfFlags(SVal val, CheckerContext &C) {
  return getSelfFlags(val, C.getState());
}

static void addSelfFlag(ProgramStateRef state, SVal val,
                        SelfFlagEnum flag, CheckerContext &C) {
  // We tag the symbol that the SVal wraps.
  if (SymbolRef sym = val.getAsSymbol()) {
    state = state->set<SelfFlag>(sym,
                                 SelfFlagEnum(getSelfFlags(val, state) | flag));
    C.addTransition(state);
  }
}

static bool hasSelfFlag(SVal val, SelfFlagEnum flag, CheckerContext &C) {
  return getSelfFlags(val, C) & flag;
}

/// Returns true of the value of the expression is the object that 'self'
/// points to and is an object that did not come from the result of calling
/// an initializer.
static bool isInvalidSelf(const Expr *E, CheckerContext &C) {
  SVal exprVal = C.getSVal(E);
  if (!hasSelfFlag(exprVal, SelfFlag_Self, C))
    return false; // value did not come from 'self'.
  if (hasSelfFlag(exprVal, SelfFlag_InitRes, C))
    return false; // 'self' is properly initialized.

  return true;
}

void ObjCSelfInitChecker::checkForInvalidSelf(const Expr *E, CheckerContext &C,
                                              const char *errorStr) const {
  if (!E)
    return;

  if (!C.getState()->get<CalledInit>())
    return;

  if (!isInvalidSelf(E, C))
    return;

  // Generate an error node.
  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  C.emitReport(std::make_unique<PathSensitiveBugReport>(BT, errorStr, N));
}

void ObjCSelfInitChecker::checkPostObjCMessage(const ObjCMethodCall &Msg,
                                               CheckerContext &C) const {
  // When encountering a message that does initialization (init rule),
  // tag the return value so that we know later on that if self has this value
  // then it is properly initialized.

  // FIXME: A callback should disable checkers at the start of functions.
  if (!shouldRunOnFunctionOrMethod(dyn_cast<NamedDecl>(
                                C.getCurrentAnalysisDeclContext()->getDecl())))
    return;

  if (isInitMessage(Msg)) {
    // Tag the return value as the result of an initializer.
    ProgramStateRef state = C.getState();

    // FIXME this really should be context sensitive, where we record
    // the current stack frame (for IPA).  Also, we need to clean this
    // value out when we return from this method.
    state = state->set<CalledInit>(true);

    SVal V = C.getSVal(Msg.getOriginExpr());
    addSelfFlag(state, V, SelfFlag_InitRes, C);
    return;
  }

  // We don't check for an invalid 'self' in an obj-c message expression to cut
  // down false positives where logging functions get information from self
  // (like its class) or doing "invalidation" on self when the initialization
  // fails.
}

void ObjCSelfInitChecker::checkPostStmt(const ObjCIvarRefExpr *E,
                                        CheckerContext &C) const {
  // FIXME: A callback should disable checkers at the start of functions.
  if (!shouldRunOnFunctionOrMethod(dyn_cast<NamedDecl>(
                                 C.getCurrentAnalysisDeclContext()->getDecl())))
    return;

  checkForInvalidSelf(
      E->getBase(), C,
      "Instance variable used while 'self' is not set to the result of "
      "'[(super or self) init...]'");
}

void ObjCSelfInitChecker::checkPreStmt(const ReturnStmt *S,
                                       CheckerContext &C) const {
  // FIXME: A callback should disable checkers at the start of functions.
  if (!shouldRunOnFunctionOrMethod(dyn_cast<NamedDecl>(
                                 C.getCurrentAnalysisDeclContext()->getDecl())))
    return;

  checkForInvalidSelf(S->getRetValue(), C,
                      "Returning 'self' while it is not set to the result of "
                      "'[(super or self) init...]'");
}

// When a call receives a reference to 'self', [Pre/Post]Call pass
// the SelfFlags from the object 'self' points to before the call to the new
// object after the call. This is to avoid invalidation of 'self' by logging
// functions.
// Another common pattern in classes with multiple initializers is to put the
// subclass's common initialization bits into a static function that receives
// the value of 'self', e.g:
// @code
//   if (!(self = [super init]))
//     return nil;
//   if (!(self = _commonInit(self)))
//     return nil;
// @endcode
// Until we can use inter-procedural analysis, in such a call, transfer the
// SelfFlags to the result of the call.

void ObjCSelfInitChecker::checkPreCall(const CallEvent &CE,
                                       CheckerContext &C) const {
  // FIXME: A callback should disable checkers at the start of functions.
  if (!shouldRunOnFunctionOrMethod(dyn_cast<NamedDecl>(
                                 C.getCurrentAnalysisDeclContext()->getDecl())))
    return;

  ProgramStateRef state = C.getState();
  unsigned NumArgs = CE.getNumArgs();
  // If we passed 'self' as and argument to the call, record it in the state
  // to be propagated after the call.
  // Note, we could have just given up, but try to be more optimistic here and
  // assume that the functions are going to continue initialization or will not
  // modify self.
  for (unsigned i = 0; i < NumArgs; ++i) {
    SVal argV = CE.getArgSVal(i);
    if (isSelfVar(argV, C)) {
      SelfFlagEnum selfFlags =
          getSelfFlags(state->getSVal(argV.castAs<Loc>()), C);
      C.addTransition(state->set<PreCallSelfFlags>(selfFlags));
      return;
    } else if (hasSelfFlag(argV, SelfFlag_Self, C)) {
      SelfFlagEnum selfFlags = getSelfFlags(argV, C);
      C.addTransition(state->set<PreCallSelfFlags>(selfFlags));
      return;
    }
  }
}

void ObjCSelfInitChecker::checkPostCall(const CallEvent &CE,
                                        CheckerContext &C) const {
  // FIXME: A callback should disable checkers at the start of functions.
  if (!shouldRunOnFunctionOrMethod(dyn_cast<NamedDecl>(
                                 C.getCurrentAnalysisDeclContext()->getDecl())))
    return;

  ProgramStateRef state = C.getState();
  SelfFlagEnum prevFlags = state->get<PreCallSelfFlags>();
  if (!prevFlags)
    return;
  state = state->remove<PreCallSelfFlags>();

  unsigned NumArgs = CE.getNumArgs();
  for (unsigned i = 0; i < NumArgs; ++i) {
    SVal argV = CE.getArgSVal(i);
    if (isSelfVar(argV, C)) {
      // If the address of 'self' is being passed to the call, assume that the
      // 'self' after the call will have the same flags.
      // EX: log(&self)
      addSelfFlag(state, state->getSVal(argV.castAs<Loc>()), prevFlags, C);
      return;
    } else if (hasSelfFlag(argV, SelfFlag_Self, C)) {
      // If 'self' is passed to the call by value, assume that the function
      // returns 'self'. So assign the flags, which were set on 'self' to the
      // return value.
      // EX: self = performMoreInitialization(self)
      addSelfFlag(state, CE.getReturnValue(), prevFlags, C);
      return;
    }
  }

  C.addTransition(state);
}

void ObjCSelfInitChecker::checkLocation(SVal location, bool isLoad,
                                        const Stmt *S,
                                        CheckerContext &C) const {
  if (!shouldRunOnFunctionOrMethod(dyn_cast<NamedDecl>(
        C.getCurrentAnalysisDeclContext()->getDecl())))
    return;

  // Tag the result of a load from 'self' so that we can easily know that the
  // value is the object that 'self' points to.
  ProgramStateRef state = C.getState();
  if (isSelfVar(location, C))
    addSelfFlag(state, state->getSVal(location.castAs<Loc>()), SelfFlag_Self,
                C);
}


void ObjCSelfInitChecker::checkBind(SVal loc, SVal val, const Stmt *S,
                                    CheckerContext &C) const {
  // Allow assignment of anything to self. Self is a local variable in the
  // initializer, so it is legal to assign anything to it, like results of
  // static functions/method calls. After self is assigned something we cannot
  // reason about, stop enforcing the rules.
  // (Only continue checking if the assigned value should be treated as self.)
  if ((isSelfVar(loc, C)) &&
      !hasSelfFlag(val, SelfFlag_InitRes, C) &&
      !hasSelfFlag(val, SelfFlag_Self, C) &&
      !isSelfVar(val, C)) {

    // Stop tracking the checker-specific state in the state.
    ProgramStateRef State = C.getState();
    State = State->remove<CalledInit>();
    if (SymbolRef sym = loc.getAsSymbol())
      State = State->remove<SelfFlag>(sym);
    C.addTransition(State);
  }
}

void ObjCSelfInitChecker::printState(raw_ostream &Out, ProgramStateRef State,
                                     const char *NL, const char *Sep) const {
  SelfFlagTy FlagMap = State->get<SelfFlag>();
  bool DidCallInit = State->get<CalledInit>();
  SelfFlagEnum PreCallFlags = State->get<PreCallSelfFlags>();

  if (FlagMap.isEmpty() && !DidCallInit && !PreCallFlags)
    return;

  Out << Sep << NL << *this << " :" << NL;

  if (DidCallInit)
    Out << "  An init method has been called." << NL;

  if (PreCallFlags != SelfFlag_None) {
    if (PreCallFlags & SelfFlag_Self) {
      Out << "  An argument of the current call came from the 'self' variable."
          << NL;
    }
    if (PreCallFlags & SelfFlag_InitRes) {
      Out << "  An argument of the current call came from an init method."
          << NL;
    }
  }

  Out << NL;
  for (auto [Sym, Flag] : FlagMap) {
    Out << Sym << " : ";

    if (Flag == SelfFlag_None)
      Out << "none";

    if (Flag & SelfFlag_Self)
      Out << "self variable";

    if (Flag & SelfFlag_InitRes) {
      if (Flag != SelfFlag_InitRes)
        Out << " | ";
      Out << "result of init method";
    }

    Out << NL;
  }
}


// FIXME: A callback should disable checkers at the start of functions.
static bool shouldRunOnFunctionOrMethod(const NamedDecl *ND) {
  if (!ND)
    return false;

  const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(ND);
  if (!MD)
    return false;
  if (!isInitializationMethod(MD))
    return false;

  // self = [super init] applies only to NSObject subclasses.
  // For instance, NSProxy doesn't implement -init.
  ASTContext &Ctx = MD->getASTContext();
  IdentifierInfo* NSObjectII = &Ctx.Idents.get("NSObject");
  ObjCInterfaceDecl *ID = MD->getClassInterface()->getSuperClass();
  for ( ; ID ; ID = ID->getSuperClass()) {
    IdentifierInfo *II = ID->getIdentifier();

    if (II == NSObjectII)
      break;
  }
  return ID != nullptr;
}

/// Returns true if the location is 'self'.
static bool isSelfVar(SVal location, CheckerContext &C) {
  AnalysisDeclContext *analCtx = C.getCurrentAnalysisDeclContext();
  if (!analCtx->getSelfDecl())
    return false;
  if (!isa<loc::MemRegionVal>(location))
    return false;

  loc::MemRegionVal MRV = location.castAs<loc::MemRegionVal>();
  if (const DeclRegion *DR = dyn_cast<DeclRegion>(MRV.stripCasts()))
    return (DR->getDecl() == analCtx->getSelfDecl());

  return false;
}

static bool isInitializationMethod(const ObjCMethodDecl *MD) {
  return MD->getMethodFamily() == OMF_init;
}

static bool isInitMessage(const ObjCMethodCall &Call) {
  return Call.getMethodFamily() == OMF_init;
}

//===----------------------------------------------------------------------===//
// Registration.
//===----------------------------------------------------------------------===//

void ento::registerObjCSelfInitChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCSelfInitChecker>();
}

bool ento::shouldRegisterObjCSelfInitChecker(const CheckerManager &mgr) {
  return true;
}
