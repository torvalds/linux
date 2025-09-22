//===- VPlanCFG.h - GraphTraits for VP blocks -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// Specializations of GraphTraits that allow VPBlockBase graphs to be
/// treated as proper graphs for generic algorithms;
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPLANCFG_H
#define LLVM_TRANSFORMS_VECTORIZE_VPLANCFG_H

#include "VPlan.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

//===----------------------------------------------------------------------===//
// GraphTraits specializations for VPlan Hierarchical Control-Flow Graphs     //
//===----------------------------------------------------------------------===//

/// Iterator to traverse all successors of a VPBlockBase node. This includes the
/// entry node of VPRegionBlocks. Exit blocks of a region implicitly have their
/// parent region's successors. This ensures all blocks in a region are visited
/// before any blocks in a successor region when doing a reverse post-order
// traversal of the graph. Region blocks themselves traverse only their entries
// directly and not their successors. Those will be traversed when a region's
// exiting block is traversed
template <typename BlockPtrTy>
class VPAllSuccessorsIterator
    : public iterator_facade_base<VPAllSuccessorsIterator<BlockPtrTy>,
                                  std::bidirectional_iterator_tag,
                                  VPBlockBase> {
  BlockPtrTy Block;
  /// Index of the current successor. For VPBasicBlock nodes, this simply is the
  /// index for the successor array. For VPRegionBlock, SuccessorIdx == 0 is
  /// used for the region's entry block, and SuccessorIdx - 1 are the indices
  /// for the successor array.
  size_t SuccessorIdx;

  static BlockPtrTy getBlockWithSuccs(BlockPtrTy Current) {
    while (Current && Current->getNumSuccessors() == 0)
      Current = Current->getParent();
    return Current;
  }

  /// Templated helper to dereference successor \p SuccIdx of \p Block. Used by
  /// both the const and non-const operator* implementations.
  template <typename T1> static T1 deref(T1 Block, unsigned SuccIdx) {
    if (auto *R = dyn_cast<VPRegionBlock>(Block)) {
      assert(SuccIdx == 0);
      return R->getEntry();
    }

    // For exit blocks, use the next parent region with successors.
    return getBlockWithSuccs(Block)->getSuccessors()[SuccIdx];
  }

public:
  /// Used by iterator_facade_base with bidirectional_iterator_tag.
  using reference = BlockPtrTy;

  VPAllSuccessorsIterator(BlockPtrTy Block, size_t Idx = 0)
      : Block(Block), SuccessorIdx(Idx) {}
  VPAllSuccessorsIterator(const VPAllSuccessorsIterator &Other)
      : Block(Other.Block), SuccessorIdx(Other.SuccessorIdx) {}

  VPAllSuccessorsIterator &operator=(const VPAllSuccessorsIterator &R) {
    Block = R.Block;
    SuccessorIdx = R.SuccessorIdx;
    return *this;
  }

  static VPAllSuccessorsIterator end(BlockPtrTy Block) {
    if (auto *R = dyn_cast<VPRegionBlock>(Block)) {
      // Traverse through the region's entry node.
      return {R, 1};
    }
    BlockPtrTy ParentWithSuccs = getBlockWithSuccs(Block);
    unsigned NumSuccessors =
        ParentWithSuccs ? ParentWithSuccs->getNumSuccessors() : 0;
    return {Block, NumSuccessors};
  }

  bool operator==(const VPAllSuccessorsIterator &R) const {
    return Block == R.Block && SuccessorIdx == R.SuccessorIdx;
  }

  const VPBlockBase *operator*() const { return deref(Block, SuccessorIdx); }

  BlockPtrTy operator*() { return deref(Block, SuccessorIdx); }

  VPAllSuccessorsIterator &operator++() {
    SuccessorIdx++;
    return *this;
  }

  VPAllSuccessorsIterator &operator--() {
    SuccessorIdx--;
    return *this;
  }

  VPAllSuccessorsIterator operator++(int X) {
    VPAllSuccessorsIterator Orig = *this;
    SuccessorIdx++;
    return Orig;
  }
};

/// Helper for GraphTraits specialization that traverses through VPRegionBlocks.
template <typename BlockTy> class VPBlockDeepTraversalWrapper {
  BlockTy Entry;

public:
  VPBlockDeepTraversalWrapper(BlockTy Entry) : Entry(Entry) {}
  BlockTy getEntry() { return Entry; }
};

/// GraphTraits specialization to recursively traverse VPBlockBase nodes,
/// including traversing through VPRegionBlocks.  Exit blocks of a region
/// implicitly have their parent region's successors. This ensures all blocks in
/// a region are visited before any blocks in a successor region when doing a
/// reverse post-order traversal of the graph.
template <> struct GraphTraits<VPBlockDeepTraversalWrapper<VPBlockBase *>> {
  using NodeRef = VPBlockBase *;
  using ChildIteratorType = VPAllSuccessorsIterator<VPBlockBase *>;

  static NodeRef getEntryNode(VPBlockDeepTraversalWrapper<VPBlockBase *> N) {
    return N.getEntry();
  }

  static inline ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N);
  }

  static inline ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType::end(N);
  }
};

template <>
struct GraphTraits<VPBlockDeepTraversalWrapper<const VPBlockBase *>> {
  using NodeRef = const VPBlockBase *;
  using ChildIteratorType = VPAllSuccessorsIterator<const VPBlockBase *>;

  static NodeRef
  getEntryNode(VPBlockDeepTraversalWrapper<const VPBlockBase *> N) {
    return N.getEntry();
  }

  static inline ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N);
  }

  static inline ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType::end(N);
  }
};

/// Helper for GraphTraits specialization that does not traverses through
/// VPRegionBlocks.
template <typename BlockTy> class VPBlockShallowTraversalWrapper {
  BlockTy Entry;

public:
  VPBlockShallowTraversalWrapper(BlockTy Entry) : Entry(Entry) {}
  BlockTy getEntry() { return Entry; }
};

template <> struct GraphTraits<VPBlockShallowTraversalWrapper<VPBlockBase *>> {
  using NodeRef = VPBlockBase *;
  using ChildIteratorType = SmallVectorImpl<VPBlockBase *>::iterator;

  static NodeRef getEntryNode(VPBlockShallowTraversalWrapper<VPBlockBase *> N) {
    return N.getEntry();
  }

  static inline ChildIteratorType child_begin(NodeRef N) {
    return N->getSuccessors().begin();
  }

  static inline ChildIteratorType child_end(NodeRef N) {
    return N->getSuccessors().end();
  }
};

template <>
struct GraphTraits<VPBlockShallowTraversalWrapper<const VPBlockBase *>> {
  using NodeRef = const VPBlockBase *;
  using ChildIteratorType = SmallVectorImpl<VPBlockBase *>::const_iterator;

  static NodeRef
  getEntryNode(VPBlockShallowTraversalWrapper<const VPBlockBase *> N) {
    return N.getEntry();
  }

  static inline ChildIteratorType child_begin(NodeRef N) {
    return N->getSuccessors().begin();
  }

  static inline ChildIteratorType child_end(NodeRef N) {
    return N->getSuccessors().end();
  }
};

/// Returns an iterator range to traverse the graph starting at \p G in
/// depth-first order. The iterator won't traverse through region blocks.
inline iterator_range<
    df_iterator<VPBlockShallowTraversalWrapper<VPBlockBase *>>>
vp_depth_first_shallow(VPBlockBase *G) {
  return depth_first(VPBlockShallowTraversalWrapper<VPBlockBase *>(G));
}
inline iterator_range<
    df_iterator<VPBlockShallowTraversalWrapper<const VPBlockBase *>>>
vp_depth_first_shallow(const VPBlockBase *G) {
  return depth_first(VPBlockShallowTraversalWrapper<const VPBlockBase *>(G));
}

/// Returns an iterator range to traverse the graph starting at \p G in
/// depth-first order while traversing through region blocks.
inline iterator_range<df_iterator<VPBlockDeepTraversalWrapper<VPBlockBase *>>>
vp_depth_first_deep(VPBlockBase *G) {
  return depth_first(VPBlockDeepTraversalWrapper<VPBlockBase *>(G));
}
inline iterator_range<
    df_iterator<VPBlockDeepTraversalWrapper<const VPBlockBase *>>>
vp_depth_first_deep(const VPBlockBase *G) {
  return depth_first(VPBlockDeepTraversalWrapper<const VPBlockBase *>(G));
}

// The following set of template specializations implement GraphTraits to treat
// any VPBlockBase as a node in a graph of VPBlockBases. It's important to note
// that VPBlockBase traits don't recurse into VPRegioBlocks, i.e., if the
// VPBlockBase is a VPRegionBlock, this specialization provides access to its
// successors/predecessors but not to the blocks inside the region.

template <> struct GraphTraits<VPBlockBase *> {
  using NodeRef = VPBlockBase *;
  using ChildIteratorType = VPAllSuccessorsIterator<VPBlockBase *>;

  static NodeRef getEntryNode(NodeRef N) { return N; }

  static inline ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N);
  }

  static inline ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType::end(N);
  }
};

template <> struct GraphTraits<const VPBlockBase *> {
  using NodeRef = const VPBlockBase *;
  using ChildIteratorType = VPAllSuccessorsIterator<const VPBlockBase *>;

  static NodeRef getEntryNode(NodeRef N) { return N; }

  static inline ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N);
  }

  static inline ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType::end(N);
  }
};

/// Inverse graph traits are not implemented yet.
/// TODO: Implement a version of VPBlockNonRecursiveTraversalWrapper to traverse
/// predecessors recursively through regions.
template <> struct GraphTraits<Inverse<VPBlockBase *>> {
  using NodeRef = VPBlockBase *;
  using ChildIteratorType = SmallVectorImpl<VPBlockBase *>::iterator;

  static NodeRef getEntryNode(Inverse<NodeRef> B) {
    llvm_unreachable("not implemented");
  }

  static inline ChildIteratorType child_begin(NodeRef N) {
    llvm_unreachable("not implemented");
  }

  static inline ChildIteratorType child_end(NodeRef N) {
    llvm_unreachable("not implemented");
  }
};

template <> struct GraphTraits<VPlan *> {
  using GraphRef = VPlan *;
  using NodeRef = VPBlockBase *;
  using nodes_iterator = df_iterator<NodeRef>;

  static NodeRef getEntryNode(GraphRef N) { return N->getEntry(); }

  static nodes_iterator nodes_begin(GraphRef N) {
    return nodes_iterator::begin(N->getEntry());
  }

  static nodes_iterator nodes_end(GraphRef N) {
    // df_iterator::end() returns an empty iterator so the node used doesn't
    // matter.
    return nodes_iterator::end(N->getEntry());
  }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_VPLANCFG_H
