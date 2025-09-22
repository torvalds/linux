//===- Tree.h - structure of the syntax tree ------------------*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Defines the basic structure of the syntax tree. There are two kinds of nodes:
//   - leaf nodes correspond to tokens,
//   - tree nodes correspond to language grammar constructs.
//
// The tree is initially built from an AST. Each node of a newly built tree
// covers a continuous subrange of expanded tokens (i.e. tokens after
// preprocessing), the specific tokens coverered are stored in the leaf nodes of
// a tree. A post-order traversal of a tree will visit leaf nodes in an order
// corresponding the original order of expanded tokens.
//
// This is still work in progress and highly experimental, we leave room for
// ourselves to completely change the design and/or implementation.
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_TOOLING_SYNTAX_TREE_H
#define LLVM_CLANG_TOOLING_SYNTAX_TREE_H

#include "clang/Basic/TokenKinds.h"
#include "clang/Tooling/Syntax/TokenManager.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include <cstdint>
#include <vector>

namespace clang {
namespace syntax {

/// A memory arena for syntax trees.
// FIXME: use BumpPtrAllocator directly.
class Arena {
public:
  llvm::BumpPtrAllocator &getAllocator() { return Allocator; }
private:
  /// Keeps all the allocated nodes and their intermediate data structures.
  llvm::BumpPtrAllocator Allocator;
};

class Tree;
class TreeBuilder;
class FactoryImpl;
class MutationsImpl;

enum class NodeKind : uint16_t;
enum class NodeRole : uint8_t;

/// A node in a syntax tree. Each node is either a Leaf (representing tokens) or
/// a Tree (representing language constructrs).
class Node {
protected:
  /// Newly created nodes are detached from a tree, parent and sibling links are
  /// set when the node is added as a child to another one.
  Node(NodeKind Kind);
  /// Nodes are allocated on Arenas; the destructor is never called.
  ~Node() = default;

public:
  /// Nodes cannot simply be copied without violating tree invariants.
  Node(const Node &) = delete;
  Node &operator=(const Node &) = delete;
  /// Idiomatically, nodes are allocated on an Arena and never moved.
  Node(Node &&) = delete;
  Node &operator=(Node &&) = delete;

  NodeKind getKind() const { return static_cast<NodeKind>(Kind); }
  NodeRole getRole() const { return static_cast<NodeRole>(Role); }

  /// Whether the node is detached from a tree, i.e. does not have a parent.
  bool isDetached() const;
  /// Whether the node was created from the AST backed by the source code
  /// rather than added later through mutation APIs or created with factory
  /// functions.
  /// When this flag is true, all subtrees are also original.
  /// This flag is set to false on any modifications to the node or any of its
  /// subtrees, even if this simply involves swapping existing subtrees.
  bool isOriginal() const { return Original; }
  /// If this function return false, the tree cannot be modified because there
  /// is no reasonable way to produce the corresponding textual replacements.
  /// This can happen when the node crosses macro expansion boundaries.
  ///
  /// Note that even if the node is not modifiable, its child nodes can be
  /// modifiable.
  bool canModify() const { return CanModify; }

  const Tree *getParent() const { return Parent; }
  Tree *getParent() { return Parent; }

  const Node *getNextSibling() const { return NextSibling; }
  Node *getNextSibling() { return NextSibling; }
  const Node *getPreviousSibling() const { return PreviousSibling; }
  Node *getPreviousSibling() { return PreviousSibling; }

  /// Dumps the structure of a subtree. For debugging and testing purposes.
  std::string dump(const TokenManager &SM) const;
  /// Dumps the tokens forming this subtree.
  std::string dumpTokens(const TokenManager &SM) const;

  /// Asserts invariants on this node of the tree and its immediate children.
  /// Will not recurse into the subtree. No-op if NDEBUG is set.
  void assertInvariants() const;
  /// Runs checkInvariants on all nodes in the subtree. No-op if NDEBUG is set.
  void assertInvariantsRecursive() const;

private:
  // Tree is allowed to change the Parent link and Role.
  friend class Tree;
  // TreeBuilder is allowed to set the Original and CanModify flags.
  friend class TreeBuilder;
  // MutationsImpl sets roles and CanModify flag.
  friend class MutationsImpl;
  // FactoryImpl sets CanModify flag.
  friend class FactoryImpl;

  void setRole(NodeRole NR);

  Tree *Parent;
  Node *NextSibling;
  Node *PreviousSibling;
  unsigned Kind : 16;
  unsigned Role : 8;
  unsigned Original : 1;
  unsigned CanModify : 1;
};

/// A leaf node points to a single token.
// FIXME: add TokenKind field (borrow some bits from the Node::kind).
class Leaf final : public Node {
public:
  Leaf(TokenManager::Key K);
  static bool classof(const Node *N);

  TokenManager::Key getTokenKey() const { return K; }

private:
  TokenManager::Key K;
};

/// A node that has children and represents a syntactic language construct.
class Tree : public Node {
  /// Iterator over children (common base for const/non-const).
  /// Not invalidated by tree mutations (holds a stable node pointer).
  template <typename DerivedT, typename NodeT>
  class ChildIteratorBase
      : public llvm::iterator_facade_base<DerivedT, std::forward_iterator_tag,
                                          NodeT> {
  protected:
    NodeT *N = nullptr;
    using Base = ChildIteratorBase;

  public:
    ChildIteratorBase() = default;
    explicit ChildIteratorBase(NodeT *N) : N(N) {}

    friend bool operator==(const DerivedT &LHS, const DerivedT &RHS) {
      return LHS.N == RHS.N;
    }

    NodeT &operator*() const { return *N; }
    DerivedT &operator++() {
      N = N->getNextSibling();
      return *static_cast<DerivedT *>(this);
    }

    /// Truthy if valid (not past-the-end).
    /// This allows: if (auto It = find_if(N.children(), ...) )
    explicit operator bool() const { return N != nullptr; }
    /// The element, or nullptr if past-the-end.
    NodeT *asPointer() const { return N; }
  };

public:
  static bool classof(const Node *N);

  Node *getFirstChild() { return FirstChild; }
  const Node *getFirstChild() const { return FirstChild; }
  Node *getLastChild() { return LastChild; }
  const Node *getLastChild() const { return LastChild; }

  const Leaf *findFirstLeaf() const;
  Leaf *findFirstLeaf() {
    return const_cast<Leaf *>(const_cast<const Tree *>(this)->findFirstLeaf());
  }

  const Leaf *findLastLeaf() const;
  Leaf *findLastLeaf() {
    return const_cast<Leaf *>(const_cast<const Tree *>(this)->findLastLeaf());
  }

  /// child_iterator is not invalidated by mutations.
  struct ChildIterator : ChildIteratorBase<ChildIterator, Node> {
    using Base::ChildIteratorBase;
  };
  struct ConstChildIterator
      : ChildIteratorBase<ConstChildIterator, const Node> {
    using Base::ChildIteratorBase;
    ConstChildIterator() = default;
    ConstChildIterator(const ChildIterator &I) : Base(I.asPointer()) {}
  };

  llvm::iterator_range<ChildIterator> getChildren() {
    return {ChildIterator(getFirstChild()), ChildIterator()};
  }
  llvm::iterator_range<ConstChildIterator> getChildren() const {
    return {ConstChildIterator(getFirstChild()), ConstChildIterator()};
  }

  /// Find the first node with a corresponding role.
  const Node *findChild(NodeRole R) const;
  Node *findChild(NodeRole R) {
    return const_cast<Node *>(const_cast<const Tree *>(this)->findChild(R));
  }

protected:
  using Node::Node;

private:
  /// Append \p Child to the list of children and sets the parent pointer.
  /// A very low-level operation that does not check any invariants, only used
  /// by TreeBuilder and FactoryImpl.
  /// EXPECTS: Role != Detached.
  void appendChildLowLevel(Node *Child, NodeRole Role);
  /// Similar but prepends.
  void prependChildLowLevel(Node *Child, NodeRole Role);

  /// Like the previous overloads, but does not set role for \p Child.
  /// EXPECTS: Child->Role != Detached
  void appendChildLowLevel(Node *Child);
  void prependChildLowLevel(Node *Child);
  friend class TreeBuilder;
  friend class FactoryImpl;

  /// Replace a range of children [Begin, End) with a list of
  /// new nodes starting at \p New.
  /// Only used by MutationsImpl to implement higher-level mutation operations.
  /// (!) \p New can be null to model removal of the child range.
  /// (!) \p End can be null to model one past the end.
  /// (!) \p Begin can be null to model an append.
  void replaceChildRangeLowLevel(Node *Begin, Node *End, Node *New);
  friend class MutationsImpl;

  Node *FirstChild = nullptr;
  Node *LastChild = nullptr;
};

/// A list of Elements separated or terminated by a fixed token.
///
/// This type models the following grammar construct:
/// delimited-list(element, delimiter, termination, canBeEmpty)
class List : public Tree {
public:
  template <typename Element> struct ElementAndDelimiter {
    Element *element;
    Leaf *delimiter;
  };

  enum class TerminationKind {
    Terminated,
    MaybeTerminated,
    Separated,
  };

  using Tree::Tree;
  static bool classof(const Node *N);
  /// Returns the elements and corresponding delimiters. Missing elements
  /// and delimiters are represented as null pointers.
  ///
  /// For example, in a separated list:
  /// "a, b, c"  <=> [("a" , ","), ("b" , "," ), ("c" , null)]
  /// "a,  , c"  <=> [("a" , ","), (null, "," ), ("c" , null)]
  /// "a, b  c"  <=> [("a" , ","), ("b" , null), ("c" , null)]
  /// "a, b,"    <=> [("a" , ","), ("b" , "," ), (null, null)]
  ///
  /// In a terminated or maybe-terminated list:
  /// "a; b; c;" <=> [("a" , ";"), ("b" , ";" ), ("c" , ";" )]
  /// "a;  ; c;" <=> [("a" , ";"), (null, ";" ), ("c" , ";" )]
  /// "a; b  c;" <=> [("a" , ";"), ("b" , null), ("c" , ";" )]
  /// "a; b; c"  <=> [("a" , ";"), ("b" , ";" ), ("c" , null)]
  std::vector<ElementAndDelimiter<Node>> getElementsAsNodesAndDelimiters();

  /// Returns the elements of the list. Missing elements are represented
  /// as null pointers in the same way as in the return value of
  /// `getElementsAsNodesAndDelimiters()`.
  std::vector<Node *> getElementsAsNodes();

  // These can't be implemented with the information we have!

  /// Returns the appropriate delimiter for this list.
  ///
  /// Useful for discovering the correct delimiter to use when adding
  /// elements to empty or one-element lists.
  clang::tok::TokenKind getDelimiterTokenKind() const;

  TerminationKind getTerminationKind() const;

  /// Whether this list can be empty in syntactically and semantically correct
  /// code.
  ///
  /// This list may be empty when the source code has errors even if
  /// canBeEmpty() returns false.
  bool canBeEmpty() const;
};

} // namespace syntax
} // namespace clang

#endif
