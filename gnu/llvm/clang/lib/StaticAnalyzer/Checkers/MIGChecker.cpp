//== MIGChecker.cpp - MIG calling convention checker ------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines MIGChecker, a Mach Interface Generator calling convention
// checker. Namely, in MIG callback implementation the following rules apply:
// - When a server routine returns an error code that represents success, it
//   must take ownership of resources passed to it (and eventually release
//   them).
// - Additionally, when returning success, all out-parameters must be
//   initialized.
// - When it returns any other error code, it must not take ownership,
//   because the message and its out-of-line parameters will be destroyed
//   by the client that called the function.
// For now we only check the last rule, as its violations lead to dangerous
// use-after-free exploits.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/Analysis/AnyCall.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {
class MIGChecker : public Checker<check::PostCall, check::PreStmt<ReturnStmt>,
                                  check::EndFunction> {
  BugType BT{this, "Use-after-free (MIG calling convention violation)",
             categories::MemoryError};

  // The checker knows that an out-of-line object is deallocated if it is
  // passed as an argument to one of these functions. If this object is
  // additionally an argument of a MIG routine, the checker keeps track of that
  // information and issues a warning when an error is returned from the
  // respective routine.
  CallDescriptionMap<unsigned> Deallocators = {
#define CALL(required_args, deallocated_arg, ...)                              \
  {{CDM::SimpleFunc, {__VA_ARGS__}, required_args}, deallocated_arg}
      // E.g., if the checker sees a C function 'vm_deallocate' that has
      // exactly 3 parameters, it knows that argument #1 (starting from 0, i.e.
      // the second argument) is going to be consumed in the sense of the MIG
      // consume-on-success convention.
      CALL(3, 1, "vm_deallocate"),
      CALL(3, 1, "mach_vm_deallocate"),
      CALL(2, 0, "mig_deallocate"),
      CALL(2, 1, "mach_port_deallocate"),
      CALL(1, 0, "device_deallocate"),
      CALL(1, 0, "iokit_remove_connect_reference"),
      CALL(1, 0, "iokit_remove_reference"),
      CALL(1, 0, "iokit_release_port"),
      CALL(1, 0, "ipc_port_release"),
      CALL(1, 0, "ipc_port_release_sonce"),
      CALL(1, 0, "ipc_voucher_attr_control_release"),
      CALL(1, 0, "ipc_voucher_release"),
      CALL(1, 0, "lock_set_dereference"),
      CALL(1, 0, "memory_object_control_deallocate"),
      CALL(1, 0, "pset_deallocate"),
      CALL(1, 0, "semaphore_dereference"),
      CALL(1, 0, "space_deallocate"),
      CALL(1, 0, "space_inspect_deallocate"),
      CALL(1, 0, "task_deallocate"),
      CALL(1, 0, "task_inspect_deallocate"),
      CALL(1, 0, "task_name_deallocate"),
      CALL(1, 0, "thread_deallocate"),
      CALL(1, 0, "thread_inspect_deallocate"),
      CALL(1, 0, "upl_deallocate"),
      CALL(1, 0, "vm_map_deallocate"),
#undef CALL
#define CALL(required_args, deallocated_arg, ...)                              \
  {{CDM::CXXMethod, {__VA_ARGS__}, required_args}, deallocated_arg}
      // E.g., if the checker sees a method 'releaseAsyncReference64()' that is
      // defined on class 'IOUserClient' that takes exactly 1 argument, it knows
      // that the argument is going to be consumed in the sense of the MIG
      // consume-on-success convention.
      CALL(1, 0, "IOUserClient", "releaseAsyncReference64"),
      CALL(1, 0, "IOUserClient", "releaseNotificationPort"),
#undef CALL
  };

  CallDescription OsRefRetain{CDM::SimpleFunc, {"os_ref_retain"}, 1};

  void checkReturnAux(const ReturnStmt *RS, CheckerContext &C) const;

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;

  // HACK: We're making two attempts to find the bug: checkEndFunction
  // should normally be enough but it fails when the return value is a literal
  // that never gets put into the Environment and ends of function with multiple
  // returns get agglutinated across returns, preventing us from obtaining
  // the return value. The problem is similar to https://reviews.llvm.org/D25326
  // but now we step into it in the top-level function.
  void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const {
    checkReturnAux(RS, C);
  }
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const {
    checkReturnAux(RS, C);
  }

};
} // end anonymous namespace

// A flag that says that the programmer has called a MIG destructor
// for at least one parameter.
REGISTER_TRAIT_WITH_PROGRAMSTATE(ReleasedParameter, bool)
// A set of parameters for which the check is suppressed because
// reference counting is being performed.
REGISTER_SET_WITH_PROGRAMSTATE(RefCountedParameters, const ParmVarDecl *)

static const ParmVarDecl *getOriginParam(SVal V, CheckerContext &C,
                                         bool IncludeBaseRegions = false) {
  // TODO: We should most likely always include base regions here.
  SymbolRef Sym = V.getAsSymbol(IncludeBaseRegions);
  if (!Sym)
    return nullptr;

  // If we optimistically assume that the MIG routine never re-uses the storage
  // that was passed to it as arguments when it invalidates it (but at most when
  // it assigns to parameter variables directly), this procedure correctly
  // determines if the value was loaded from the transitive closure of MIG
  // routine arguments in the heap.
  while (const MemRegion *MR = Sym->getOriginRegion()) {
    const auto *VR = dyn_cast<VarRegion>(MR);
    if (VR && VR->hasStackParametersStorage() &&
           VR->getStackFrame()->inTopFrame())
      return cast<ParmVarDecl>(VR->getDecl());

    const SymbolicRegion *SR = MR->getSymbolicBase();
    if (!SR)
      return nullptr;

    Sym = SR->getSymbol();
  }

  return nullptr;
}

static bool isInMIGCall(CheckerContext &C) {
  const LocationContext *LC = C.getLocationContext();
  assert(LC && "Unknown location context");

  const StackFrameContext *SFC;
  // Find the top frame.
  while (LC) {
    SFC = LC->getStackFrame();
    LC = SFC->getParent();
  }

  const Decl *D = SFC->getDecl();

  if (std::optional<AnyCall> AC = AnyCall::forDecl(D)) {
    // Even though there's a Sema warning when the return type of an annotated
    // function is not a kern_return_t, this warning isn't an error, so we need
    // an extra check here.
    // FIXME: AnyCall doesn't support blocks yet, so they remain unchecked
    // for now.
    if (!AC->getReturnType(C.getASTContext())
             .getCanonicalType()->isSignedIntegerType())
      return false;
  }

  if (D->hasAttr<MIGServerRoutineAttr>())
    return true;

  // See if there's an annotated method in the superclass.
  if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
    for (const auto *OMD: MD->overridden_methods())
      if (OMD->hasAttr<MIGServerRoutineAttr>())
        return true;

  return false;
}

void MIGChecker::checkPostCall(const CallEvent &Call, CheckerContext &C) const {
  if (OsRefRetain.matches(Call)) {
    // If the code is doing reference counting over the parameter,
    // it opens up an opportunity for safely calling a destructor function.
    // TODO: We should still check for over-releases.
    if (const ParmVarDecl *PVD =
            getOriginParam(Call.getArgSVal(0), C, /*IncludeBaseRegions=*/true)) {
      // We never need to clean up the program state because these are
      // top-level parameters anyway, so they're always live.
      C.addTransition(C.getState()->add<RefCountedParameters>(PVD));
    }
    return;
  }

  if (!isInMIGCall(C))
    return;

  const unsigned *ArgIdxPtr = Deallocators.lookup(Call);
  if (!ArgIdxPtr)
    return;

  ProgramStateRef State = C.getState();
  unsigned ArgIdx = *ArgIdxPtr;
  SVal Arg = Call.getArgSVal(ArgIdx);
  const ParmVarDecl *PVD = getOriginParam(Arg, C);
  if (!PVD || State->contains<RefCountedParameters>(PVD))
    return;

  const NoteTag *T =
    C.getNoteTag([this, PVD](PathSensitiveBugReport &BR) -> std::string {
        if (&BR.getBugType() != &BT)
          return "";
        SmallString<64> Str;
        llvm::raw_svector_ostream OS(Str);
        OS << "Value passed through parameter '" << PVD->getName()
           << "\' is deallocated";
        return std::string(OS.str());
      });
  C.addTransition(State->set<ReleasedParameter>(true), T);
}

// Returns true if V can potentially represent a "successful" kern_return_t.
static bool mayBeSuccess(SVal V, CheckerContext &C) {
  ProgramStateRef State = C.getState();

  // Can V represent KERN_SUCCESS?
  if (!State->isNull(V).isConstrainedFalse())
    return true;

  SValBuilder &SVB = C.getSValBuilder();
  ASTContext &ACtx = C.getASTContext();

  // Can V represent MIG_NO_REPLY?
  static const int MigNoReply = -305;
  V = SVB.evalEQ(C.getState(), V, SVB.makeIntVal(MigNoReply, ACtx.IntTy));
  if (!State->isNull(V).isConstrainedTrue())
    return true;

  // If none of the above, it's definitely an error.
  return false;
}

void MIGChecker::checkReturnAux(const ReturnStmt *RS, CheckerContext &C) const {
  // It is very unlikely that a MIG callback will be called from anywhere
  // within the project under analysis and the caller isn't itself a routine
  // that follows the MIG calling convention. Therefore we're safe to believe
  // that it's always the top frame that is of interest. There's a slight chance
  // that the user would want to enforce the MIG calling convention upon
  // a random routine in the middle of nowhere, but given that the convention is
  // fairly weird and hard to follow in the first place, there's relatively
  // little motivation to spread it this way.
  if (!C.inTopFrame())
    return;

  if (!isInMIGCall(C))
    return;

  // We know that the function is non-void, but what if the return statement
  // is not there in the code? It's not a compile error, we should not crash.
  if (!RS)
    return;

  ProgramStateRef State = C.getState();
  if (!State->get<ReleasedParameter>())
    return;

  SVal V = C.getSVal(RS);
  if (mayBeSuccess(V, C))
    return;

  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(
      BT,
      "MIG callback fails with error after deallocating argument value. "
      "This is a use-after-free vulnerability because the caller will try to "
      "deallocate it again",
      N);

  R->addRange(RS->getSourceRange());
  bugreporter::trackExpressionValue(
      N, RS->getRetValue(), *R,
      {bugreporter::TrackingKind::Thorough, /*EnableNullFPSuppression=*/false});
  C.emitReport(std::move(R));
}

void ento::registerMIGChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<MIGChecker>();
}

bool ento::shouldRegisterMIGChecker(const CheckerManager &mgr) {
  return true;
}
