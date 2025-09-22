//===- Scope.cpp - Lexical scope information --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Scope class, which is used for recording
// information about a lexical scope.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/Scope.h"
#include "clang/AST/Decl.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

void Scope::setFlags(Scope *parent, unsigned flags) {
  AnyParent = parent;
  Flags = flags;

  if (parent && !(flags & FnScope)) {
    BreakParent    = parent->BreakParent;
    ContinueParent = parent->ContinueParent;
  } else {
    // Control scopes do not contain the contents of nested function scopes for
    // control flow purposes.
    BreakParent = ContinueParent = nullptr;
  }

  if (parent) {
    Depth = parent->Depth + 1;
    PrototypeDepth = parent->PrototypeDepth;
    PrototypeIndex = 0;
    FnParent       = parent->FnParent;
    BlockParent    = parent->BlockParent;
    TemplateParamParent = parent->TemplateParamParent;
    DeclParent = parent->DeclParent;
    MSLastManglingParent = parent->MSLastManglingParent;
    MSCurManglingNumber = getMSLastManglingNumber();
    if ((Flags & (FnScope | ClassScope | BlockScope | TemplateParamScope |
                  FunctionPrototypeScope | AtCatchScope | ObjCMethodScope)) ==
        0)
      Flags |= parent->getFlags() & OpenMPSimdDirectiveScope;
    // transmit the parent's 'order' flag, if exists
    if (parent->getFlags() & OpenMPOrderClauseScope)
      Flags |= OpenMPOrderClauseScope;
  } else {
    Depth = 0;
    PrototypeDepth = 0;
    PrototypeIndex = 0;
    MSLastManglingParent = FnParent = BlockParent = nullptr;
    TemplateParamParent = nullptr;
    DeclParent = nullptr;
    MSLastManglingNumber = 1;
    MSCurManglingNumber = 1;
  }

  // If this scope is a function or contains breaks/continues, remember it.
  if (flags & FnScope)            FnParent = this;
  // The MS mangler uses the number of scopes that can hold declarations as
  // part of an external name.
  if (Flags & (ClassScope | FnScope)) {
    MSLastManglingNumber = getMSLastManglingNumber();
    MSLastManglingParent = this;
    MSCurManglingNumber = 1;
  }
  if (flags & BreakScope)         BreakParent = this;
  if (flags & ContinueScope)      ContinueParent = this;
  if (flags & BlockScope)         BlockParent = this;
  if (flags & TemplateParamScope) TemplateParamParent = this;

  // If this is a prototype scope, record that. Lambdas have an extra prototype
  // scope that doesn't add any depth.
  if (flags & FunctionPrototypeScope && !(flags & LambdaScope))
    PrototypeDepth++;

  if (flags & DeclScope) {
    DeclParent = this;
    if (flags & FunctionPrototypeScope)
      ; // Prototype scopes are uninteresting.
    else if ((flags & ClassScope) && getParent()->isClassScope())
      ; // Nested class scopes aren't ambiguous.
    else if ((flags & ClassScope) && getParent()->getFlags() == DeclScope)
      ; // Classes inside of namespaces aren't ambiguous.
    else if ((flags & EnumScope))
      ; // Don't increment for enum scopes.
    else
      incrementMSManglingNumber();
  }
}

void Scope::Init(Scope *parent, unsigned flags) {
  setFlags(parent, flags);

  DeclsInScope.clear();
  UsingDirectives.clear();
  Entity = nullptr;
  ErrorTrap.reset();
  NRVO = std::nullopt;
}

bool Scope::containedInPrototypeScope() const {
  const Scope *S = this;
  while (S) {
    if (S->isFunctionPrototypeScope())
      return true;
    S = S->getParent();
  }
  return false;
}

void Scope::AddFlags(unsigned FlagsToSet) {
  assert((FlagsToSet & ~(BreakScope | ContinueScope)) == 0 &&
         "Unsupported scope flags");
  if (FlagsToSet & BreakScope) {
    assert((Flags & BreakScope) == 0 && "Already set");
    BreakParent = this;
  }
  if (FlagsToSet & ContinueScope) {
    assert((Flags & ContinueScope) == 0 && "Already set");
    ContinueParent = this;
  }
  Flags |= FlagsToSet;
}

// The algorithm for updating NRVO candidate is as follows:
//   1. All previous candidates become invalid because a new NRVO candidate is
//      obtained. Therefore, we need to clear return slots for other
//      variables defined before the current return statement in the current
//      scope and in outer scopes.
//   2. Store the new candidate if its return slot is available. Otherwise,
//      there is no NRVO candidate so far.
void Scope::updateNRVOCandidate(VarDecl *VD) {
  auto UpdateReturnSlotsInScopeForVD = [VD](Scope *S) -> bool {
    bool IsReturnSlotFound = S->ReturnSlots.contains(VD);

    // We found a candidate variable that can be put into a return slot.
    // Clear the set, because other variables cannot occupy a return
    // slot in the same scope.
    S->ReturnSlots.clear();

    if (IsReturnSlotFound)
      S->ReturnSlots.insert(VD);

    return IsReturnSlotFound;
  };

  bool CanBePutInReturnSlot = false;

  for (auto *S = this; S; S = S->getParent()) {
    CanBePutInReturnSlot |= UpdateReturnSlotsInScopeForVD(S);

    if (S->getEntity())
      break;
  }

  // Consider the variable as NRVO candidate if the return slot is available
  // for it in the current scope, or if it can be available in outer scopes.
  NRVO = CanBePutInReturnSlot ? VD : nullptr;
}

void Scope::applyNRVO() {
  // There is no NRVO candidate in the current scope.
  if (!NRVO.has_value())
    return;

  if (*NRVO && isDeclScope(*NRVO))
    (*NRVO)->setNRVOVariable(true);

  // It's necessary to propagate NRVO candidate to the parent scope for cases
  // when the parent scope doesn't contain a return statement.
  // For example:
  //    X foo(bool b) {
  //      X x;
  //      if (b)
  //        return x;
  //      exit(0);
  //    }
  // Also, we need to propagate nullptr value that means NRVO is not
  // allowed in this scope.
  // For example:
  //    X foo(bool b) {
  //      X x;
  //      if (b)
  //        return x;
  //      else
  //        return X(); // NRVO is not allowed
  //    }
  if (!getEntity())
    getParent()->NRVO = *NRVO;
}

LLVM_DUMP_METHOD void Scope::dump() const { dumpImpl(llvm::errs()); }

void Scope::dumpImpl(raw_ostream &OS) const {
  unsigned Flags = getFlags();
  bool HasFlags = Flags != 0;

  if (HasFlags)
    OS << "Flags: ";

  std::pair<unsigned, const char *> FlagInfo[] = {
      {FnScope, "FnScope"},
      {BreakScope, "BreakScope"},
      {ContinueScope, "ContinueScope"},
      {DeclScope, "DeclScope"},
      {ControlScope, "ControlScope"},
      {ClassScope, "ClassScope"},
      {BlockScope, "BlockScope"},
      {TemplateParamScope, "TemplateParamScope"},
      {FunctionPrototypeScope, "FunctionPrototypeScope"},
      {FunctionDeclarationScope, "FunctionDeclarationScope"},
      {AtCatchScope, "AtCatchScope"},
      {ObjCMethodScope, "ObjCMethodScope"},
      {SwitchScope, "SwitchScope"},
      {TryScope, "TryScope"},
      {FnTryCatchScope, "FnTryCatchScope"},
      {OpenMPDirectiveScope, "OpenMPDirectiveScope"},
      {OpenMPLoopDirectiveScope, "OpenMPLoopDirectiveScope"},
      {OpenMPSimdDirectiveScope, "OpenMPSimdDirectiveScope"},
      {EnumScope, "EnumScope"},
      {SEHTryScope, "SEHTryScope"},
      {SEHExceptScope, "SEHExceptScope"},
      {SEHFilterScope, "SEHFilterScope"},
      {CompoundStmtScope, "CompoundStmtScope"},
      {ClassInheritanceScope, "ClassInheritanceScope"},
      {CatchScope, "CatchScope"},
      {ConditionVarScope, "ConditionVarScope"},
      {OpenMPOrderClauseScope, "OpenMPOrderClauseScope"},
      {LambdaScope, "LambdaScope"},
      {OpenACCComputeConstructScope, "OpenACCComputeConstructScope"},
      {TypeAliasScope, "TypeAliasScope"},
      {FriendScope, "FriendScope"},
  };

  for (auto Info : FlagInfo) {
    if (Flags & Info.first) {
      OS << Info.second;
      Flags &= ~Info.first;
      if (Flags)
        OS << " | ";
    }
  }

  assert(Flags == 0 && "Unknown scope flags");

  if (HasFlags)
    OS << '\n';

  if (const Scope *Parent = getParent())
    OS << "Parent: (clang::Scope*)" << Parent << '\n';

  OS << "Depth: " << Depth << '\n';
  OS << "MSLastManglingNumber: " << getMSLastManglingNumber() << '\n';
  OS << "MSCurManglingNumber: " << getMSCurManglingNumber() << '\n';
  if (const DeclContext *DC = getEntity())
    OS << "Entity : (clang::DeclContext*)" << DC << '\n';

  if (!NRVO)
    OS << "there is no NRVO candidate\n";
  else if (*NRVO)
    OS << "NRVO candidate : (clang::VarDecl*)" << *NRVO << '\n';
  else
    OS << "NRVO is not allowed\n";
}
