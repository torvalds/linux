//===- Tree.cpp -----------------------------------------------*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Tooling/Syntax/Tree.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Tooling/Syntax/Nodes.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include <cassert>

using namespace clang;

namespace {
static void traverse(const syntax::Node *N,
                     llvm::function_ref<void(const syntax::Node *)> Visit) {
  if (auto *T = dyn_cast<syntax::Tree>(N)) {
    for (const syntax::Node &C : T->getChildren())
      traverse(&C, Visit);
  }
  Visit(N);
}
static void traverse(syntax::Node *N,
                     llvm::function_ref<void(syntax::Node *)> Visit) {
  traverse(static_cast<const syntax::Node *>(N), [&](const syntax::Node *N) {
    Visit(const_cast<syntax::Node *>(N));
  });
}
} // namespace

syntax::Leaf::Leaf(syntax::TokenManager::Key K) : Node(NodeKind::Leaf), K(K) {}

syntax::Node::Node(NodeKind Kind)
    : Parent(nullptr), NextSibling(nullptr), PreviousSibling(nullptr),
      Kind(static_cast<unsigned>(Kind)), Role(0), Original(false),
      CanModify(false) {
  this->setRole(NodeRole::Detached);
}

bool syntax::Node::isDetached() const {
  return getRole() == NodeRole::Detached;
}

void syntax::Node::setRole(NodeRole NR) {
  this->Role = static_cast<unsigned>(NR);
}

void syntax::Tree::appendChildLowLevel(Node *Child, NodeRole Role) {
  assert(Child->getRole() == NodeRole::Detached);
  assert(Role != NodeRole::Detached);

  Child->setRole(Role);
  appendChildLowLevel(Child);
}

void syntax::Tree::appendChildLowLevel(Node *Child) {
  assert(Child->Parent == nullptr);
  assert(Child->NextSibling == nullptr);
  assert(Child->PreviousSibling == nullptr);
  assert(Child->getRole() != NodeRole::Detached);

  Child->Parent = this;
  if (this->LastChild) {
    Child->PreviousSibling = this->LastChild;
    this->LastChild->NextSibling = Child;
  } else
    this->FirstChild = Child;

  this->LastChild = Child;
}

void syntax::Tree::prependChildLowLevel(Node *Child, NodeRole Role) {
  assert(Child->getRole() == NodeRole::Detached);
  assert(Role != NodeRole::Detached);

  Child->setRole(Role);
  prependChildLowLevel(Child);
}

void syntax::Tree::prependChildLowLevel(Node *Child) {
  assert(Child->Parent == nullptr);
  assert(Child->NextSibling == nullptr);
  assert(Child->PreviousSibling == nullptr);
  assert(Child->getRole() != NodeRole::Detached);

  Child->Parent = this;
  if (this->FirstChild) {
    Child->NextSibling = this->FirstChild;
    this->FirstChild->PreviousSibling = Child;
  } else
    this->LastChild = Child;

  this->FirstChild = Child;
}

void syntax::Tree::replaceChildRangeLowLevel(Node *Begin, Node *End,
                                             Node *New) {
  assert((!Begin || Begin->Parent == this) &&
         "`Begin` is not a child of `this`.");
  assert((!End || End->Parent == this) && "`End` is not a child of `this`.");
  assert(canModify() && "Cannot modify `this`.");

#ifndef NDEBUG
  for (auto *N = New; N; N = N->NextSibling) {
    assert(N->Parent == nullptr);
    assert(N->getRole() != NodeRole::Detached && "Roles must be set");
    // FIXME: validate the role.
  }

  auto Reachable = [](Node *From, Node *N) {
    if (!N)
      return true;
    for (auto *It = From; It; It = It->NextSibling)
      if (It == N)
        return true;
    return false;
  };
  assert(Reachable(FirstChild, Begin) && "`Begin` is not reachable.");
  assert(Reachable(Begin, End) && "`End` is not after `Begin`.");
#endif

  if (!New && Begin == End)
    return;

  // Mark modification.
  for (auto *T = this; T && T->Original; T = T->Parent)
    T->Original = false;

  // Save the node before the range to be removed. Later we insert the `New`
  // range after this node.
  auto *BeforeBegin = Begin ? Begin->PreviousSibling : LastChild;

  // Detach old nodes.
  for (auto *N = Begin; N != End;) {
    auto *Next = N->NextSibling;

    N->setRole(NodeRole::Detached);
    N->Parent = nullptr;
    N->NextSibling = nullptr;
    N->PreviousSibling = nullptr;
    if (N->Original)
      traverse(N, [](Node *C) { C->Original = false; });

    N = Next;
  }

  // Attach new range.
  auto *&NewFirst = BeforeBegin ? BeforeBegin->NextSibling : FirstChild;
  auto *&NewLast = End ? End->PreviousSibling : LastChild;

  if (!New) {
    NewFirst = End;
    NewLast = BeforeBegin;
    return;
  }

  New->PreviousSibling = BeforeBegin;
  NewFirst = New;

  Node *LastInNew;
  for (auto *N = New; N != nullptr; N = N->NextSibling) {
    LastInNew = N;
    N->Parent = this;
  }
  LastInNew->NextSibling = End;
  NewLast = LastInNew;
}

namespace {
static void dumpNode(raw_ostream &OS, const syntax::Node *N,
                     const syntax::TokenManager &TM, llvm::BitVector IndentMask) {
  auto DumpExtraInfo = [&OS](const syntax::Node *N) {
    if (N->getRole() != syntax::NodeRole::Unknown)
      OS << " " << N->getRole();
    if (!N->isOriginal())
      OS << " synthesized";
    if (!N->canModify())
      OS << " unmodifiable";
  };

  assert(N);
  if (const auto *L = dyn_cast<syntax::Leaf>(N)) {
    OS << "'";
    OS << TM.getText(L->getTokenKey());
    OS << "'";
    DumpExtraInfo(N);
    OS << "\n";
    return;
  }

  const auto *T = cast<syntax::Tree>(N);
  OS << T->getKind();
  DumpExtraInfo(N);
  OS << "\n";

  for (const syntax::Node &It : T->getChildren()) {
    for (unsigned Idx = 0; Idx < IndentMask.size(); ++Idx) {
      if (IndentMask[Idx])
        OS << "| ";
      else
        OS << "  ";
    }
    if (!It.getNextSibling()) {
      OS << "`-";
      IndentMask.push_back(false);
    } else {
      OS << "|-";
      IndentMask.push_back(true);
    }
    dumpNode(OS, &It, TM, IndentMask);
    IndentMask.pop_back();
  }
}
} // namespace

std::string syntax::Node::dump(const TokenManager &TM) const {
  std::string Str;
  llvm::raw_string_ostream OS(Str);
  dumpNode(OS, this, TM, /*IndentMask=*/{});
  return std::move(OS.str());
}

std::string syntax::Node::dumpTokens(const TokenManager &TM) const {
  std::string Storage;
  llvm::raw_string_ostream OS(Storage);
  traverse(this, [&](const syntax::Node *N) {
    if (const auto *L = dyn_cast<syntax::Leaf>(N)) {
      OS << TM.getText(L->getTokenKey());
      OS << " ";
    }
  });
  return Storage;
}

void syntax::Node::assertInvariants() const {
#ifndef NDEBUG
  if (isDetached())
    assert(getParent() == nullptr);
  else
    assert(getParent() != nullptr);

  const auto *T = dyn_cast<Tree>(this);
  if (!T)
    return;
  for (const Node &C : T->getChildren()) {
    if (T->isOriginal())
      assert(C.isOriginal());
    assert(!C.isDetached());
    assert(C.getParent() == T);
    const auto *Next = C.getNextSibling();
    assert(!Next || &C == Next->getPreviousSibling());
    if (!C.getNextSibling())
      assert(&C == T->getLastChild() &&
             "Last child is reachable by advancing from the first child.");
  }

  const auto *L = dyn_cast<List>(T);
  if (!L)
    return;
  for (const Node &C : T->getChildren()) {
    assert(C.getRole() == NodeRole::ListElement ||
           C.getRole() == NodeRole::ListDelimiter);
    if (C.getRole() == NodeRole::ListDelimiter) {
      assert(isa<Leaf>(C));
      // FIXME: re-enable it when there is way to retrieve token kind in Leaf.
      // assert(cast<Leaf>(C).getToken()->kind() == L->getDelimiterTokenKind());
    }
  }

#endif
}

void syntax::Node::assertInvariantsRecursive() const {
#ifndef NDEBUG
  traverse(this, [&](const syntax::Node *N) { N->assertInvariants(); });
#endif
}

const syntax::Leaf *syntax::Tree::findFirstLeaf() const {
  for (const Node &C : getChildren()) {
    if (const auto *L = dyn_cast<syntax::Leaf>(&C))
      return L;
    if (const auto *L = cast<syntax::Tree>(C).findFirstLeaf())
      return L;
  }
  return nullptr;
}

const syntax::Leaf *syntax::Tree::findLastLeaf() const {
  for (const auto *C = getLastChild(); C; C = C->getPreviousSibling()) {
    if (const auto *L = dyn_cast<syntax::Leaf>(C))
      return L;
    if (const auto *L = cast<syntax::Tree>(C)->findLastLeaf())
      return L;
  }
  return nullptr;
}

const syntax::Node *syntax::Tree::findChild(NodeRole R) const {
  for (const Node &C : getChildren()) {
    if (C.getRole() == R)
      return &C;
  }
  return nullptr;
}

std::vector<syntax::List::ElementAndDelimiter<syntax::Node>>
syntax::List::getElementsAsNodesAndDelimiters() {
  if (!getFirstChild())
    return {};

  std::vector<syntax::List::ElementAndDelimiter<Node>> Children;
  syntax::Node *ElementWithoutDelimiter = nullptr;
  for (Node &C : getChildren()) {
    switch (C.getRole()) {
    case syntax::NodeRole::ListElement: {
      if (ElementWithoutDelimiter) {
        Children.push_back({ElementWithoutDelimiter, nullptr});
      }
      ElementWithoutDelimiter = &C;
      break;
    }
    case syntax::NodeRole::ListDelimiter: {
      Children.push_back({ElementWithoutDelimiter, cast<syntax::Leaf>(&C)});
      ElementWithoutDelimiter = nullptr;
      break;
    }
    default:
      llvm_unreachable(
          "A list can have only elements and delimiters as children.");
    }
  }

  switch (getTerminationKind()) {
  case syntax::List::TerminationKind::Separated: {
    Children.push_back({ElementWithoutDelimiter, nullptr});
    break;
  }
  case syntax::List::TerminationKind::Terminated:
  case syntax::List::TerminationKind::MaybeTerminated: {
    if (ElementWithoutDelimiter) {
      Children.push_back({ElementWithoutDelimiter, nullptr});
    }
    break;
  }
  }

  return Children;
}

// Almost the same implementation of `getElementsAsNodesAndDelimiters` but
// ignoring delimiters
std::vector<syntax::Node *> syntax::List::getElementsAsNodes() {
  if (!getFirstChild())
    return {};

  std::vector<syntax::Node *> Children;
  syntax::Node *ElementWithoutDelimiter = nullptr;
  for (Node &C : getChildren()) {
    switch (C.getRole()) {
    case syntax::NodeRole::ListElement: {
      if (ElementWithoutDelimiter) {
        Children.push_back(ElementWithoutDelimiter);
      }
      ElementWithoutDelimiter = &C;
      break;
    }
    case syntax::NodeRole::ListDelimiter: {
      Children.push_back(ElementWithoutDelimiter);
      ElementWithoutDelimiter = nullptr;
      break;
    }
    default:
      llvm_unreachable("A list has only elements or delimiters.");
    }
  }

  switch (getTerminationKind()) {
  case syntax::List::TerminationKind::Separated: {
    Children.push_back(ElementWithoutDelimiter);
    break;
  }
  case syntax::List::TerminationKind::Terminated:
  case syntax::List::TerminationKind::MaybeTerminated: {
    if (ElementWithoutDelimiter) {
      Children.push_back(ElementWithoutDelimiter);
    }
    break;
  }
  }

  return Children;
}

clang::tok::TokenKind syntax::List::getDelimiterTokenKind() const {
  switch (this->getKind()) {
  case NodeKind::NestedNameSpecifier:
    return clang::tok::coloncolon;
  case NodeKind::CallArguments:
  case NodeKind::ParameterDeclarationList:
  case NodeKind::DeclaratorList:
    return clang::tok::comma;
  default:
    llvm_unreachable("This is not a subclass of List, thus "
                     "getDelimiterTokenKind() cannot be called");
  }
}

syntax::List::TerminationKind syntax::List::getTerminationKind() const {
  switch (this->getKind()) {
  case NodeKind::NestedNameSpecifier:
    return TerminationKind::Terminated;
  case NodeKind::CallArguments:
  case NodeKind::ParameterDeclarationList:
  case NodeKind::DeclaratorList:
    return TerminationKind::Separated;
  default:
    llvm_unreachable("This is not a subclass of List, thus "
                     "getTerminationKind() cannot be called");
  }
}

bool syntax::List::canBeEmpty() const {
  switch (this->getKind()) {
  case NodeKind::NestedNameSpecifier:
    return false;
  case NodeKind::CallArguments:
    return true;
  case NodeKind::ParameterDeclarationList:
    return true;
  case NodeKind::DeclaratorList:
    return true;
  default:
    llvm_unreachable("This is not a subclass of List, thus canBeEmpty() "
                     "cannot be called");
  }
}
