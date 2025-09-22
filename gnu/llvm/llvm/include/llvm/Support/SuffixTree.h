//===- llvm/ADT/SuffixTree.h - Tree for substrings --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// A data structure for fast substring queries.
//
// Suffix trees represent the suffixes of their input strings in their leaves.
// A suffix tree is a type of compressed trie structure where each node
// represents an entire substring rather than a single character. Each leaf
// of the tree is a suffix.
//
// A suffix tree can be seen as a type of state machine where each state is a
// substring of the full string. The tree is structured so that, for a string
// of length N, there are exactly N leaves in the tree. This structure allows
// us to quickly find repeated substrings of the input string.
//
// In this implementation, a "string" is a vector of unsigned integers.
// These integers may result from hashing some data type. A suffix tree can
// contain 1 or many strings, which can then be queried as one large string.
//
// The suffix tree is implemented using Ukkonen's algorithm for linear-time
// suffix tree construction. Ukkonen's algorithm is explained in more detail
// in the paper by Esko Ukkonen "On-line construction of suffix trees. The
// paper is available at
//
// https://www.cs.helsinki.fi/u/ukkonen/SuffixT1withFigs.pdf
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SUFFIXTREE_H
#define LLVM_SUPPORT_SUFFIXTREE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/SuffixTreeNode.h"

namespace llvm {
class SuffixTree {
public:
  /// Each element is an integer representing an instruction in the module.
  ArrayRef<unsigned> Str;

  /// Whether to consider leaf descendants or only leaf children.
  bool OutlinerLeafDescendants;

  /// A repeated substring in the tree.
  struct RepeatedSubstring {
    /// The length of the string.
    unsigned Length;

    /// The start indices of each occurrence.
    SmallVector<unsigned> StartIndices;
  };

private:
  /// Maintains internal nodes in the tree.
  SpecificBumpPtrAllocator<SuffixTreeInternalNode> InternalNodeAllocator;
  /// Maintains leaf nodes in the tree.
  SpecificBumpPtrAllocator<SuffixTreeLeafNode> LeafNodeAllocator;

  /// The root of the suffix tree.
  ///
  /// The root represents the empty string. It is maintained by the
  /// \p NodeAllocator like every other node in the tree.
  SuffixTreeInternalNode *Root = nullptr;

  /// The end index of each leaf in the tree.
  unsigned LeafEndIdx = SuffixTreeNode::EmptyIdx;

  /// Helper struct which keeps track of the next insertion point in
  /// Ukkonen's algorithm.
  struct ActiveState {
    /// The next node to insert at.
    SuffixTreeInternalNode *Node = nullptr;

    /// The index of the first character in the substring currently being added.
    unsigned Idx = SuffixTreeNode::EmptyIdx;

    /// The length of the substring we have to add at the current step.
    unsigned Len = 0;
  };

  /// The point the next insertion will take place at in the
  /// construction algorithm.
  ActiveState Active;

  /// Allocate a leaf node and add it to the tree.
  ///
  /// \param Parent The parent of this node.
  /// \param StartIdx The start index of this node's associated string.
  /// \param Edge The label on the edge leaving \p Parent to this node.
  ///
  /// \returns A pointer to the allocated leaf node.
  SuffixTreeNode *insertLeaf(SuffixTreeInternalNode &Parent, unsigned StartIdx,
                             unsigned Edge);

  /// Allocate an internal node and add it to the tree.
  ///
  /// \param Parent The parent of this node. Only null when allocating the root.
  /// \param StartIdx The start index of this node's associated string.
  /// \param EndIdx The end index of this node's associated string.
  /// \param Edge The label on the edge leaving \p Parent to this node.
  ///
  /// \returns A pointer to the allocated internal node.
  SuffixTreeInternalNode *insertInternalNode(SuffixTreeInternalNode *Parent,
                                             unsigned StartIdx, unsigned EndIdx,
                                             unsigned Edge);

  /// Allocate the root node and add it to the tree.
  ///
  /// \returns A pointer to the root.
  SuffixTreeInternalNode *insertRoot();

  /// Set the suffix indices of the leaves to the start indices of their
  /// respective suffixes.
  void setSuffixIndices();

  /// Construct the suffix tree for the prefix of the input ending at
  /// \p EndIdx.
  ///
  /// Used to construct the full suffix tree iteratively. At the end of each
  /// step, the constructed suffix tree is either a valid suffix tree, or a
  /// suffix tree with implicit suffixes. At the end of the final step, the
  /// suffix tree is a valid tree.
  ///
  /// \param EndIdx The end index of the current prefix in the main string.
  /// \param SuffixesToAdd The number of suffixes that must be added
  /// to complete the suffix tree at the current phase.
  ///
  /// \returns The number of suffixes that have not been added at the end of
  /// this step.
  unsigned extend(unsigned EndIdx, unsigned SuffixesToAdd);

  /// This vector contains all leaf nodes of this suffix tree. These leaf nodes
  /// are identified using post-order depth-first traversal, so that the order
  /// of these leaf nodes in the vector matches the order of the leaves in the
  /// tree from left to right if one were to draw the tree on paper.
  std::vector<SuffixTreeLeafNode *> LeafNodes;

  /// Perform a post-order depth-first traversal of the tree and perform two
  /// tasks during the traversal. The first is to populate LeafNodes, adding
  /// nodes in order of the traversal. The second is to keep track of the leaf
  /// descendants of every internal node by assigning values to LeftLeafIndex
  /// and RightLefIndex fields of SuffixTreeNode for all internal nodes.
  void setLeafNodes();

public:
  /// Construct a suffix tree from a sequence of unsigned integers.
  ///
  /// \param Str The string to construct the suffix tree for.
  /// \param OutlinerLeafDescendants Whether to consider leaf descendants or
  /// only leaf children (used by Machine Outliner).
  SuffixTree(const ArrayRef<unsigned> &Str,
             bool OutlinerLeafDescendants = false);

  /// Iterator for finding all repeated substrings in the suffix tree.
  struct RepeatedSubstringIterator {
  private:
    /// The current node we're visiting.
    SuffixTreeNode *N = nullptr;

    /// The repeated substring associated with this node.
    RepeatedSubstring RS;

    /// The nodes left to visit.
    SmallVector<SuffixTreeInternalNode *> InternalNodesToVisit;

    /// The minimum length of a repeated substring to find.
    /// Since we're outlining, we want at least two instructions in the range.
    /// FIXME: This may not be true for targets like X86 which support many
    /// instruction lengths.
    const unsigned MinLength = 2;

    /// Vector of leaf nodes of the suffix tree.
    const std::vector<SuffixTreeLeafNode *> &LeafNodes;

    /// Whether to consider leaf descendants or only leaf children.
    bool OutlinerLeafDescendants = !LeafNodes.empty();

    /// Move the iterator to the next repeated substring.
    void advance();

  public:
    /// Return the current repeated substring.
    RepeatedSubstring &operator*() { return RS; }

    RepeatedSubstringIterator &operator++() {
      advance();
      return *this;
    }

    RepeatedSubstringIterator operator++(int I) {
      RepeatedSubstringIterator It(*this);
      advance();
      return It;
    }

    bool operator==(const RepeatedSubstringIterator &Other) const {
      return N == Other.N;
    }
    bool operator!=(const RepeatedSubstringIterator &Other) const {
      return !(*this == Other);
    }

    RepeatedSubstringIterator(
        SuffixTreeInternalNode *N,
        const std::vector<SuffixTreeLeafNode *> &LeafNodes = {})
        : N(N), LeafNodes(LeafNodes) {
      // Do we have a non-null node?
      if (!N)
        return;
      // Yes. At the first step, we need to visit all of N's children.
      // Note: This means that we visit N last.
      InternalNodesToVisit.push_back(N);
      advance();
    }
  };

  typedef RepeatedSubstringIterator iterator;
  iterator begin() { return iterator(Root, LeafNodes); }
  iterator end() { return iterator(nullptr); }
};

} // namespace llvm

#endif // LLVM_SUPPORT_SUFFIXTREE_H
