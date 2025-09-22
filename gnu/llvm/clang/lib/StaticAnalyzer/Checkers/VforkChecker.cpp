//===- VforkChecker.cpp -------- Vfork usage checks --------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines vfork checker which checks for dangerous uses of vfork.
//  Vforked process shares memory (including stack) with parent so it's
//  range of actions is significantly limited: can't write variables,
//  can't call functions not in the allowed list, etc. For more details, see
//  http://man7.org/linux/man-pages/man2/vfork.2.html
//
//  This checker checks for prohibited constructs in vforked process.
//  The state transition diagram:
//  PARENT ---(vfork() == 0)--> CHILD
//                                   |
//                                   --(*p = ...)--> bug
//                                   |
//                                   --foo()--> bug
//                                   |
//                                   --return--> bug
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/AST/ParentMap.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {

class VforkChecker : public Checker<check::PreCall, check::PostCall,
                                    check::Bind, check::PreStmt<ReturnStmt>> {
  const BugType BT{this, "Dangerous construct in a vforked process"};
  mutable llvm::SmallSet<const IdentifierInfo *, 10> VforkAllowlist;
  mutable const IdentifierInfo *II_vfork = nullptr;

  static bool isChildProcess(const ProgramStateRef State);

  bool isVforkCall(const Decl *D, CheckerContext &C) const;
  bool isCallExplicitelyAllowed(const IdentifierInfo *II,
                                CheckerContext &C) const;

  void reportBug(const char *What, CheckerContext &C,
                 const char *Details = nullptr) const;

public:
  VforkChecker() = default;

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkBind(SVal L, SVal V, const Stmt *S, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const;
};

} // end anonymous namespace

// This trait holds region of variable that is assigned with vfork's
// return value (this is the only region child is allowed to write).
// VFORK_RESULT_INVALID means that we are in parent process.
// VFORK_RESULT_NONE means that vfork's return value hasn't been assigned.
// Other values point to valid regions.
REGISTER_TRAIT_WITH_PROGRAMSTATE(VforkResultRegion, const void *)
#define VFORK_RESULT_INVALID 0
#define VFORK_RESULT_NONE ((void *)(uintptr_t)1)

bool VforkChecker::isChildProcess(const ProgramStateRef State) {
  return State->get<VforkResultRegion>() != VFORK_RESULT_INVALID;
}

bool VforkChecker::isVforkCall(const Decl *D, CheckerContext &C) const {
  auto FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD || !C.isCLibraryFunction(FD))
    return false;

  if (!II_vfork) {
    ASTContext &AC = C.getASTContext();
    II_vfork = &AC.Idents.get("vfork");
  }

  return FD->getIdentifier() == II_vfork;
}

// Returns true iff ok to call function after successful vfork.
bool VforkChecker::isCallExplicitelyAllowed(const IdentifierInfo *II,
                                            CheckerContext &C) const {
  if (VforkAllowlist.empty()) {
    // According to manpage.
    const char *ids[] = {
      "_Exit",
      "_exit",
      "execl",
      "execle",
      "execlp",
      "execv",
      "execve",
      "execvp",
      "execvpe",
      nullptr
    };

    ASTContext &AC = C.getASTContext();
    for (const char **id = ids; *id; ++id)
      VforkAllowlist.insert(&AC.Idents.get(*id));
  }

  return VforkAllowlist.count(II);
}

void VforkChecker::reportBug(const char *What, CheckerContext &C,
                             const char *Details) const {
  if (ExplodedNode *N = C.generateErrorNode(C.getState())) {
    SmallString<256> buf;
    llvm::raw_svector_ostream os(buf);

    os << What << " is prohibited after a successful vfork";

    if (Details)
      os << "; " << Details;

    auto Report = std::make_unique<PathSensitiveBugReport>(BT, os.str(), N);
    // TODO: mark vfork call in BugReportVisitor
    C.emitReport(std::move(Report));
  }
}

// Detect calls to vfork and split execution appropriately.
void VforkChecker::checkPostCall(const CallEvent &Call,
                                 CheckerContext &C) const {
  // We can't call vfork in child so don't bother
  // (corresponding warning has already been emitted in checkPreCall).
  ProgramStateRef State = C.getState();
  if (isChildProcess(State))
    return;

  if (!isVforkCall(Call.getDecl(), C))
    return;

  // Get return value of vfork.
  SVal VforkRetVal = Call.getReturnValue();
  std::optional<DefinedOrUnknownSVal> DVal =
      VforkRetVal.getAs<DefinedOrUnknownSVal>();
  if (!DVal)
    return;

  // Get assigned variable.
  const ParentMap &PM = C.getLocationContext()->getParentMap();
  const Stmt *P = PM.getParentIgnoreParenCasts(Call.getOriginExpr());
  const VarDecl *LhsDecl;
  std::tie(LhsDecl, std::ignore) = parseAssignment(P);

  // Get assigned memory region.
  MemRegionManager &M = C.getStoreManager().getRegionManager();
  const MemRegion *LhsDeclReg =
    LhsDecl
      ? M.getVarRegion(LhsDecl, C.getLocationContext())
      : (const MemRegion *)VFORK_RESULT_NONE;

  // Parent branch gets nonzero return value (according to manpage).
  ProgramStateRef ParentState, ChildState;
  std::tie(ParentState, ChildState) = C.getState()->assume(*DVal);
  C.addTransition(ParentState);
  ChildState = ChildState->set<VforkResultRegion>(LhsDeclReg);
  C.addTransition(ChildState);
}

// Prohibit calls to functions in child process which are not explicitly
// allowed.
void VforkChecker::checkPreCall(const CallEvent &Call,
                                CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  if (isChildProcess(State) &&
      !isCallExplicitelyAllowed(Call.getCalleeIdentifier(), C))
    reportBug("This function call", C);
}

// Prohibit writes in child process (except for vfork's lhs).
void VforkChecker::checkBind(SVal L, SVal V, const Stmt *S,
                             CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  if (!isChildProcess(State))
    return;

  const MemRegion *VforkLhs =
    static_cast<const MemRegion *>(State->get<VforkResultRegion>());
  const MemRegion *MR = L.getAsRegion();

  // Child is allowed to modify only vfork's lhs.
  if (!MR || MR == VforkLhs)
    return;

  reportBug("This assignment", C);
}

// Prohibit return from function in child process.
void VforkChecker::checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  if (isChildProcess(State))
    reportBug("Return", C, "call _exit() instead");
}

void ento::registerVforkChecker(CheckerManager &mgr) {
  mgr.registerChecker<VforkChecker>();
}

bool ento::shouldRegisterVforkChecker(const CheckerManager &mgr) {
  return true;
}
