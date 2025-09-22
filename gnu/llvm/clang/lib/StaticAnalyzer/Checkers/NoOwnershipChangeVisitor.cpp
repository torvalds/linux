//===--------------------------------------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NoOwnershipChangeVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "llvm/ADT/SetOperations.h"

using namespace clang;
using namespace ento;
using OwnerSet = NoOwnershipChangeVisitor::OwnerSet;

namespace {
// Collect which entities point to the allocated memory, and could be
// responsible for deallocating it.
class OwnershipBindingsHandler : public StoreManager::BindingsHandler {
  SymbolRef Sym;
  OwnerSet &Owners;

public:
  OwnershipBindingsHandler(SymbolRef Sym, OwnerSet &Owners)
      : Sym(Sym), Owners(Owners) {}

  bool HandleBinding(StoreManager &SMgr, Store Store, const MemRegion *Region,
                     SVal Val) override {
    if (Val.getAsSymbol() == Sym)
      Owners.insert(Region);
    return true;
  }

  LLVM_DUMP_METHOD void dump() const { dumpToStream(llvm::errs()); }
  LLVM_DUMP_METHOD void dumpToStream(llvm::raw_ostream &out) const {
    out << "Owners: {\n";
    for (const MemRegion *Owner : Owners) {
      out << "  ";
      Owner->dumpToStream(out);
      out << ",\n";
    }
    out << "}\n";
  }
};
} // namespace

OwnerSet NoOwnershipChangeVisitor::getOwnersAtNode(const ExplodedNode *N) {
  OwnerSet Ret;

  ProgramStateRef State = N->getState();
  OwnershipBindingsHandler Handler{Sym, Ret};
  State->getStateManager().getStoreManager().iterBindings(State->getStore(),
                                                          Handler);
  return Ret;
}

LLVM_DUMP_METHOD std::string
NoOwnershipChangeVisitor::getFunctionName(const ExplodedNode *CallEnterN) {
  if (const CallExpr *CE = llvm::dyn_cast_or_null<CallExpr>(
          CallEnterN->getLocationAs<CallEnter>()->getCallExpr()))
    if (const FunctionDecl *FD = CE->getDirectCallee())
      return FD->getQualifiedNameAsString();
  return "";
}

bool NoOwnershipChangeVisitor::wasModifiedInFunction(
    const ExplodedNode *CallEnterN, const ExplodedNode *CallExitEndN) {
  const Decl *Callee =
      CallExitEndN->getFirstPred()->getLocationContext()->getDecl();
  const FunctionDecl *FD = dyn_cast<FunctionDecl>(Callee);

  // Given that the stack frame was entered, the body should always be
  // theoretically obtainable. In case of body farms, the synthesized body
  // is not attached to declaration, thus triggering the '!FD->hasBody()'
  // branch. That said, would a synthesized body ever intend to handle
  // ownership? As of today they don't. And if they did, how would we
  // put notes inside it, given that it doesn't match any source locations?
  if (!FD || !FD->hasBody())
    return false;
  if (!doesFnIntendToHandleOwnership(
          Callee,
          CallExitEndN->getState()->getAnalysisManager().getASTContext()))
    return true;

  if (hasResourceStateChanged(CallEnterN->getState(), CallExitEndN->getState()))
    return true;

  OwnerSet CurrOwners = getOwnersAtNode(CallEnterN);
  OwnerSet ExitOwners = getOwnersAtNode(CallExitEndN);

  // Owners in the current set may be purged from the analyzer later on.
  // If a variable is dead (is not referenced directly or indirectly after
  // some point), it will be removed from the Store before the end of its
  // actual lifetime.
  // This means that if the ownership status didn't change, CurrOwners
  // must be a superset of, but not necessarily equal to ExitOwners.
  return !llvm::set_is_subset(ExitOwners, CurrOwners);
}

PathDiagnosticPieceRef NoOwnershipChangeVisitor::maybeEmitNoteForParameters(
    PathSensitiveBugReport &R, const CallEvent &Call, const ExplodedNode *N) {
  // TODO: Factor the logic of "what constitutes as an entity being passed
  // into a function call" out by reusing the code in
  // NoStoreFuncVisitor::maybeEmitNoteForParameters, maybe by incorporating
  // the printing technology in UninitializedObject's FieldChainInfo.
  ArrayRef<ParmVarDecl *> Parameters = Call.parameters();
  for (unsigned I = 0; I < Call.getNumArgs() && I < Parameters.size(); ++I) {
    SVal V = Call.getArgSVal(I);
    if (V.getAsSymbol() == Sym)
      return emitNote(N);
  }
  return nullptr;
}
