//===--------- LoopIterator.h - Iterate over loop blocks --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines iterators to visit the basic blocks within a loop.
//
// These iterators currently visit blocks within subloops as well.
// Unfortunately we have no efficient way of summarizing loop exits which would
// allow skipping subloops during traversal.
//
// If you want to visit all blocks in a loop and don't need an ordered traveral,
// use Loop::block_begin() instead.
//
// This is intentionally designed to work with ill-formed loops in which the
// backedge has been deleted. The only prerequisite is that all blocks
// contained within the loop according to the most recent LoopInfo analysis are
// reachable from the loop header.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPITERATOR_H
#define LLVM_ANALYSIS_LOOPITERATOR_H

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/LoopInfo.h"

namespace llvm {

class LoopBlocksTraversal;

// A traits type that is intended to be used in graph algorithms. The graph
// traits starts at the loop header, and traverses the BasicBlocks that are in
// the loop body, but not the loop header. Since the loop header is skipped,
// the back edges are excluded.
//
// TODO: Explore the possibility to implement LoopBlocksTraversal in terms of
//       LoopBodyTraits, so that insertEdge doesn't have to be specialized.
struct LoopBodyTraits {
  using NodeRef = std::pair<const Loop *, BasicBlock *>;

  // This wraps a const Loop * into the iterator, so we know which edges to
  // filter out.
  class WrappedSuccIterator
      : public iterator_adaptor_base<
            WrappedSuccIterator, succ_iterator,
            typename std::iterator_traits<succ_iterator>::iterator_category,
            NodeRef, std::ptrdiff_t, NodeRef *, NodeRef> {
    using BaseT = iterator_adaptor_base<
        WrappedSuccIterator, succ_iterator,
        typename std::iterator_traits<succ_iterator>::iterator_category,
        NodeRef, std::ptrdiff_t, NodeRef *, NodeRef>;

    const Loop *L;

  public:
    WrappedSuccIterator(succ_iterator Begin, const Loop *L)
        : BaseT(Begin), L(L) {}

    NodeRef operator*() const { return {L, *I}; }
  };

  struct LoopBodyFilter {
    bool operator()(NodeRef N) const {
      const Loop *L = N.first;
      return N.second != L->getHeader() && L->contains(N.second);
    }
  };

  using ChildIteratorType =
      filter_iterator<WrappedSuccIterator, LoopBodyFilter>;

  static NodeRef getEntryNode(const Loop &G) { return {&G, G.getHeader()}; }

  static ChildIteratorType child_begin(NodeRef Node) {
    return make_filter_range(make_range<WrappedSuccIterator>(
                                 {succ_begin(Node.second), Node.first},
                                 {succ_end(Node.second), Node.first}),
                             LoopBodyFilter{})
        .begin();
  }

  static ChildIteratorType child_end(NodeRef Node) {
    return make_filter_range(make_range<WrappedSuccIterator>(
                                 {succ_begin(Node.second), Node.first},
                                 {succ_end(Node.second), Node.first}),
                             LoopBodyFilter{})
        .end();
  }
};

/// Store the result of a depth first search within basic blocks contained by a
/// single loop.
///
/// TODO: This could be generalized for any CFG region, or the entire CFG.
class LoopBlocksDFS {
public:
  /// Postorder list iterators.
  typedef std::vector<BasicBlock*>::const_iterator POIterator;
  typedef std::vector<BasicBlock*>::const_reverse_iterator RPOIterator;

  friend class LoopBlocksTraversal;

private:
  Loop *L;

  /// Map each block to its postorder number. A block is only mapped after it is
  /// preorder visited by DFS. It's postorder number is initially zero and set
  /// to nonzero after it is finished by postorder traversal.
  DenseMap<BasicBlock*, unsigned> PostNumbers;
  std::vector<BasicBlock*> PostBlocks;

public:
  LoopBlocksDFS(Loop *Container) :
    L(Container), PostNumbers(NextPowerOf2(Container->getNumBlocks())) {
    PostBlocks.reserve(Container->getNumBlocks());
  }

  Loop *getLoop() const { return L; }

  /// Traverse the loop blocks and store the DFS result.
  void perform(const LoopInfo *LI);

  /// Return true if postorder numbers are assigned to all loop blocks.
  bool isComplete() const { return PostBlocks.size() == L->getNumBlocks(); }

  /// Iterate over the cached postorder blocks.
  POIterator beginPostorder() const {
    assert(isComplete() && "bad loop DFS");
    return PostBlocks.begin();
  }
  POIterator endPostorder() const { return PostBlocks.end(); }

  /// Reverse iterate over the cached postorder blocks.
  RPOIterator beginRPO() const {
    assert(isComplete() && "bad loop DFS");
    return PostBlocks.rbegin();
  }
  RPOIterator endRPO() const { return PostBlocks.rend(); }

  /// Return true if this block has been preorder visited.
  bool hasPreorder(BasicBlock *BB) const { return PostNumbers.count(BB); }

  /// Return true if this block has a postorder number.
  bool hasPostorder(BasicBlock *BB) const {
    DenseMap<BasicBlock*, unsigned>::const_iterator I = PostNumbers.find(BB);
    return I != PostNumbers.end() && I->second;
  }

  /// Get a block's postorder number.
  unsigned getPostorder(BasicBlock *BB) const {
    DenseMap<BasicBlock*, unsigned>::const_iterator I = PostNumbers.find(BB);
    assert(I != PostNumbers.end() && "block not visited by DFS");
    assert(I->second && "block not finished by DFS");
    return I->second;
  }

  /// Get a block's reverse postorder number.
  unsigned getRPO(BasicBlock *BB) const {
    return 1 + PostBlocks.size() - getPostorder(BB);
  }

  void clear() {
    PostNumbers.clear();
    PostBlocks.clear();
  }
};

/// Wrapper class to LoopBlocksDFS that provides a standard begin()/end()
/// interface for the DFS reverse post-order traversal of blocks in a loop body.
class LoopBlocksRPO {
private:
  LoopBlocksDFS DFS;

public:
  LoopBlocksRPO(Loop *Container) : DFS(Container) {}

  /// Traverse the loop blocks and store the DFS result.
  void perform(const LoopInfo *LI) {
    DFS.perform(LI);
  }

  /// Reverse iterate over the cached postorder blocks.
  LoopBlocksDFS::RPOIterator begin() const { return DFS.beginRPO(); }
  LoopBlocksDFS::RPOIterator end() const { return DFS.endRPO(); }
};

/// Specialize po_iterator_storage to record postorder numbers.
template<> class po_iterator_storage<LoopBlocksTraversal, true> {
  LoopBlocksTraversal &LBT;
public:
  po_iterator_storage(LoopBlocksTraversal &lbs) : LBT(lbs) {}
  // These functions are defined below.
  bool insertEdge(std::optional<BasicBlock *> From, BasicBlock *To);
  void finishPostorder(BasicBlock *BB);
};

/// Traverse the blocks in a loop using a depth-first search.
class LoopBlocksTraversal {
public:
  /// Graph traversal iterator.
  typedef po_iterator<BasicBlock*, LoopBlocksTraversal, true> POTIterator;

private:
  LoopBlocksDFS &DFS;
  const LoopInfo *LI;

public:
  LoopBlocksTraversal(LoopBlocksDFS &Storage, const LoopInfo *LInfo) :
    DFS(Storage), LI(LInfo) {}

  /// Postorder traversal over the graph. This only needs to be done once.
  /// po_iterator "automatically" calls back to visitPreorder and
  /// finishPostorder to record the DFS result.
  POTIterator begin() {
    assert(DFS.PostBlocks.empty() && "Need clear DFS result before traversing");
    assert(DFS.L->getNumBlocks() && "po_iterator cannot handle an empty graph");
    return po_ext_begin(DFS.L->getHeader(), *this);
  }
  POTIterator end() {
    // po_ext_end interface requires a basic block, but ignores its value.
    return po_ext_end(DFS.L->getHeader(), *this);
  }

  /// Called by po_iterator upon reaching a block via a CFG edge. If this block
  /// is contained in the loop and has not been visited, then mark it preorder
  /// visited and return true.
  ///
  /// TODO: If anyone is interested, we could record preorder numbers here.
  bool visitPreorder(BasicBlock *BB) {
    if (!DFS.L->contains(LI->getLoopFor(BB)))
      return false;

    return DFS.PostNumbers.insert(std::make_pair(BB, 0)).second;
  }

  /// Called by po_iterator each time it advances, indicating a block's
  /// postorder.
  void finishPostorder(BasicBlock *BB) {
    assert(DFS.PostNumbers.count(BB) && "Loop DFS skipped preorder");
    DFS.PostBlocks.push_back(BB);
    DFS.PostNumbers[BB] = DFS.PostBlocks.size();
  }
};

inline bool po_iterator_storage<LoopBlocksTraversal, true>::insertEdge(
    std::optional<BasicBlock *> From, BasicBlock *To) {
  return LBT.visitPreorder(To);
}

inline void po_iterator_storage<LoopBlocksTraversal, true>::
finishPostorder(BasicBlock *BB) {
  LBT.finishPostorder(BB);
}

} // End namespace llvm

#endif
