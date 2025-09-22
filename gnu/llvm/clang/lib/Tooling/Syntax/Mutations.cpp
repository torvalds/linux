//===- Mutations.cpp ------------------------------------------*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Tooling/Syntax/Mutations.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Token.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Syntax/BuildTree.h"
#include "clang/Tooling/Syntax/Nodes.h"
#include "clang/Tooling/Syntax/Tokens.h"
#include "clang/Tooling/Syntax/Tree.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <string>

using namespace clang;

// This class has access to the internals of tree nodes. Its sole purpose is to
// define helpers that allow implementing the high-level mutation operations.
class syntax::MutationsImpl {
public:
  /// Add a new node with a specified role.
  static void addAfter(syntax::Node *Anchor, syntax::Node *New, NodeRole Role) {
    assert(Anchor != nullptr);
    assert(Anchor->Parent != nullptr);
    assert(New->Parent == nullptr);
    assert(New->NextSibling == nullptr);
    assert(New->PreviousSibling == nullptr);
    assert(New->isDetached());
    assert(Role != NodeRole::Detached);

    New->setRole(Role);
    auto *P = Anchor->getParent();
    P->replaceChildRangeLowLevel(Anchor->getNextSibling(),
                                 Anchor->getNextSibling(), New);

    P->assertInvariants();
  }

  /// Replace the node, keeping the role.
  static void replace(syntax::Node *Old, syntax::Node *New) {
    assert(Old != nullptr);
    assert(Old->Parent != nullptr);
    assert(Old->canModify());
    assert(New->Parent == nullptr);
    assert(New->NextSibling == nullptr);
    assert(New->PreviousSibling == nullptr);
    assert(New->isDetached());

    New->Role = Old->Role;
    auto *P = Old->getParent();
    P->replaceChildRangeLowLevel(Old, Old->getNextSibling(), New);

    P->assertInvariants();
  }

  /// Completely remove the node from its parent.
  static void remove(syntax::Node *N) {
    assert(N != nullptr);
    assert(N->Parent != nullptr);
    assert(N->canModify());

    auto *P = N->getParent();
    P->replaceChildRangeLowLevel(N, N->getNextSibling(),
                                 /*New=*/nullptr);

    P->assertInvariants();
    N->assertInvariants();
  }
};

void syntax::removeStatement(syntax::Arena &A, TokenBufferTokenManager &TBTM,
                             syntax::Statement *S) {
  assert(S);
  assert(S->canModify());

  if (isa<CompoundStatement>(S->getParent())) {
    // A child of CompoundStatement can just be safely removed.
    MutationsImpl::remove(S);
    return;
  }
  // For the rest, we have to replace with an empty statement.
  if (isa<EmptyStatement>(S))
    return; // already an empty statement, nothing to do.

  MutationsImpl::replace(S, createEmptyStatement(A, TBTM));
}
