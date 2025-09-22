//===- ParentMapContext.h - Map of parents using DynTypedNode ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Similar to ParentMap.h, but generalizes to non-Stmt nodes, which can have
// multiple parents.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_PARENTMAPCONTEXT_H
#define LLVM_CLANG_AST_PARENTMAPCONTEXT_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTTypeTraits.h"

namespace clang {
class DynTypedNodeList;

class ParentMapContext {
public:
  ParentMapContext(ASTContext &Ctx);

  ~ParentMapContext();

  /// Returns the parents of the given node (within the traversal scope).
  ///
  /// Note that this will lazily compute the parents of all nodes
  /// and store them for later retrieval. Thus, the first call is O(n)
  /// in the number of AST nodes.
  ///
  /// Caveats and FIXMEs:
  /// Calculating the parent map over all AST nodes will need to load the
  /// full AST. This can be undesirable in the case where the full AST is
  /// expensive to create (for example, when using precompiled header
  /// preambles). Thus, there are good opportunities for optimization here.
  /// One idea is to walk the given node downwards, looking for references
  /// to declaration contexts - once a declaration context is found, compute
  /// the parent map for the declaration context; if that can satisfy the
  /// request, loading the whole AST can be avoided. Note that this is made
  /// more complex by statements in templates having multiple parents - those
  /// problems can be solved by building closure over the templated parts of
  /// the AST, which also avoids touching large parts of the AST.
  /// Additionally, we will want to add an interface to already give a hint
  /// where to search for the parents, for example when looking at a statement
  /// inside a certain function.
  ///
  /// 'NodeT' can be one of Decl, Stmt, Type, TypeLoc,
  /// NestedNameSpecifier or NestedNameSpecifierLoc.
  template <typename NodeT> DynTypedNodeList getParents(const NodeT &Node);

  DynTypedNodeList getParents(const DynTypedNode &Node);

  /// Clear parent maps.
  void clear();

  TraversalKind getTraversalKind() const { return Traversal; }
  void setTraversalKind(TraversalKind TK) { Traversal = TK; }

  const Expr *traverseIgnored(const Expr *E) const;
  Expr *traverseIgnored(Expr *E) const;
  DynTypedNode traverseIgnored(const DynTypedNode &N) const;

  class ParentMap;

private:
  ASTContext &ASTCtx;
  TraversalKind Traversal = TK_AsIs;
  std::unique_ptr<ParentMap> Parents;
};

class TraversalKindScope {
  ParentMapContext &Ctx;
  TraversalKind TK = TK_AsIs;

public:
  TraversalKindScope(ASTContext &ASTCtx, std::optional<TraversalKind> ScopeTK)
      : Ctx(ASTCtx.getParentMapContext()) {
    TK = Ctx.getTraversalKind();
    if (ScopeTK)
      Ctx.setTraversalKind(*ScopeTK);
  }

  ~TraversalKindScope() { Ctx.setTraversalKind(TK); }
};

/// Container for either a single DynTypedNode or for an ArrayRef to
/// DynTypedNode. For use with ParentMap.
class DynTypedNodeList {
  union {
    DynTypedNode SingleNode;
    ArrayRef<DynTypedNode> Nodes;
  };
  bool IsSingleNode;

public:
  DynTypedNodeList(const DynTypedNode &N) : IsSingleNode(true) {
    new (&SingleNode) DynTypedNode(N);
  }

  DynTypedNodeList(ArrayRef<DynTypedNode> A) : IsSingleNode(false) {
    new (&Nodes) ArrayRef<DynTypedNode>(A);
  }

  const DynTypedNode *begin() const {
    return !IsSingleNode ? Nodes.begin() : &SingleNode;
  }

  const DynTypedNode *end() const {
    return !IsSingleNode ? Nodes.end() : &SingleNode + 1;
  }

  size_t size() const { return end() - begin(); }
  bool empty() const { return begin() == end(); }

  const DynTypedNode &operator[](size_t N) const {
    assert(N < size() && "Out of bounds!");
    return *(begin() + N);
  }
};

template <typename NodeT>
inline DynTypedNodeList ParentMapContext::getParents(const NodeT &Node) {
  return getParents(DynTypedNode::create(Node));
}

template <typename NodeT>
inline DynTypedNodeList ASTContext::getParents(const NodeT &Node) {
  return getParentMapContext().getParents(Node);
}

template <>
inline DynTypedNodeList ASTContext::getParents(const DynTypedNode &Node) {
  return getParentMapContext().getParents(Node);
}

} // namespace clang

#endif
