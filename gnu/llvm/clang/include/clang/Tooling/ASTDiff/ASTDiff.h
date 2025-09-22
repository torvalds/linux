//===- ASTDiff.h - AST differencing API -----------------------*- C++ -*- -===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file specifies an interface that can be used to compare C++ syntax
// trees.
//
// We use the gumtree algorithm which combines a heuristic top-down search that
// is able to match large subtrees that are equivalent, with an optimal
// algorithm to match small subtrees.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_ASTDIFF_ASTDIFF_H
#define LLVM_CLANG_TOOLING_ASTDIFF_ASTDIFF_H

#include "clang/Tooling/ASTDiff/ASTDiffInternal.h"
#include <optional>

namespace clang {
namespace diff {

enum ChangeKind {
  None,
  Delete,    // (Src): delete node Src.
  Update,    // (Src, Dst): update the value of node Src to match Dst.
  Insert,    // (Src, Dst, Pos): insert Src as child of Dst at offset Pos.
  Move,      // (Src, Dst, Pos): move Src to be a child of Dst at offset Pos.
  UpdateMove // Same as Move plus Update.
};

/// Represents a Clang AST node, alongside some additional information.
struct Node {
  NodeId Parent, LeftMostDescendant, RightMostDescendant;
  int Depth, Height, Shift = 0;
  DynTypedNode ASTNode;
  SmallVector<NodeId, 4> Children;
  ChangeKind Change = None;

  ASTNodeKind getType() const;
  StringRef getTypeLabel() const;
  bool isLeaf() const { return Children.empty(); }
  std::optional<StringRef> getIdentifier() const;
  std::optional<std::string> getQualifiedIdentifier() const;
};

/// SyntaxTree objects represent subtrees of the AST.
/// They can be constructed from any Decl or Stmt.
class SyntaxTree {
public:
  /// Constructs a tree from a translation unit.
  SyntaxTree(ASTContext &AST);
  /// Constructs a tree from any AST node.
  template <class T>
  SyntaxTree(T *Node, ASTContext &AST)
      : TreeImpl(std::make_unique<Impl>(this, Node, AST)) {}
  SyntaxTree(SyntaxTree &&Other) = default;
  ~SyntaxTree();

  const ASTContext &getASTContext() const;
  StringRef getFilename() const;

  int getSize() const;
  NodeId getRootId() const;
  using PreorderIterator = NodeId;
  PreorderIterator begin() const;
  PreorderIterator end() const;

  const Node &getNode(NodeId Id) const;
  int findPositionInParent(NodeId Id) const;

  // Returns the starting and ending offset of the node in its source file.
  std::pair<unsigned, unsigned> getSourceRangeOffsets(const Node &N) const;

  /// Serialize the node attributes to a string representation. This should
  /// uniquely distinguish nodes of the same kind. Note that this function just
  /// returns a representation of the node value, not considering descendants.
  std::string getNodeValue(NodeId Id) const;
  std::string getNodeValue(const Node &Node) const;

  class Impl;
  std::unique_ptr<Impl> TreeImpl;
};

struct ComparisonOptions {
  /// During top-down matching, only consider nodes of at least this height.
  int MinHeight = 2;

  /// During bottom-up matching, match only nodes with at least this value as
  /// the ratio of their common descendants.
  double MinSimilarity = 0.5;

  /// Whenever two subtrees are matched in the bottom-up phase, the optimal
  /// mapping is computed, unless the size of either subtrees exceeds this.
  int MaxSize = 100;

  bool StopAfterTopDown = false;

  /// Returns false if the nodes should never be matched.
  bool isMatchingAllowed(const Node &N1, const Node &N2) const {
    return N1.getType().isSame(N2.getType());
  }
};

class ASTDiff {
public:
  ASTDiff(SyntaxTree &Src, SyntaxTree &Dst, const ComparisonOptions &Options);
  ~ASTDiff();

  // Returns the ID of the node that is mapped to the given node in SourceTree.
  NodeId getMapped(const SyntaxTree &SourceTree, NodeId Id) const;

  class Impl;

private:
  std::unique_ptr<Impl> DiffImpl;
};

} // end namespace diff
} // end namespace clang

#endif
