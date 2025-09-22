//===- IteratedDominanceFrontier.h - Calculate IDF --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Compute iterated dominance frontiers using a linear time algorithm.
///
/// The algorithm used here is based on:
///
///   Sreedhar and Gao. A linear time algorithm for placing phi-nodes.
///   In Proceedings of the 22nd ACM SIGPLAN-SIGACT Symposium on Principles of
///   Programming Languages
///   POPL '95. ACM, New York, NY, 62-73.
///
/// It has been modified to not explicitly use the DJ graph data structure and
/// to directly compute pruned SSA using per-variable liveness information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GENERICITERATEDDOMINANCEFRONTIER_H
#define LLVM_SUPPORT_GENERICITERATEDDOMINANCEFRONTIER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/GenericDomTree.h"
#include <queue>

namespace llvm {

namespace IDFCalculatorDetail {

/// Generic utility class used for getting the children of a basic block.
/// May be specialized if, for example, one wouldn't like to return nullpointer
/// successors.
template <class NodeTy, bool IsPostDom> struct ChildrenGetterTy {
  using NodeRef = typename GraphTraits<NodeTy *>::NodeRef;
  using ChildIteratorType = typename GraphTraits<NodeTy *>::ChildIteratorType;
  using range = iterator_range<ChildIteratorType>;

  range get(const NodeRef &N);
};

} // end of namespace IDFCalculatorDetail

/// Determine the iterated dominance frontier, given a set of defining
/// blocks, and optionally, a set of live-in blocks.
///
/// In turn, the results can be used to place phi nodes.
///
/// This algorithm is a linear time computation of Iterated Dominance Frontiers,
/// pruned using the live-in set.
/// By default, liveness is not used to prune the IDF computation.
/// The template parameters should be of a CFG block type.
template <class NodeTy, bool IsPostDom> class IDFCalculatorBase {
public:
  using OrderedNodeTy =
      std::conditional_t<IsPostDom, Inverse<NodeTy *>, NodeTy *>;
  using ChildrenGetterTy =
      IDFCalculatorDetail::ChildrenGetterTy<NodeTy, IsPostDom>;

  IDFCalculatorBase(DominatorTreeBase<NodeTy, IsPostDom> &DT) : DT(DT) {}

  IDFCalculatorBase(DominatorTreeBase<NodeTy, IsPostDom> &DT,
                    const ChildrenGetterTy &C)
      : DT(DT), ChildrenGetter(C) {}

  /// Give the IDF calculator the set of blocks in which the value is
  /// defined.  This is equivalent to the set of starting blocks it should be
  /// calculating the IDF for (though later gets pruned based on liveness).
  ///
  /// Note: This set *must* live for the entire lifetime of the IDF calculator.
  void setDefiningBlocks(const SmallPtrSetImpl<NodeTy *> &Blocks) {
    DefBlocks = &Blocks;
  }

  /// Give the IDF calculator the set of blocks in which the value is
  /// live on entry to the block.   This is used to prune the IDF calculation to
  /// not include blocks where any phi insertion would be dead.
  ///
  /// Note: This set *must* live for the entire lifetime of the IDF calculator.
  void setLiveInBlocks(const SmallPtrSetImpl<NodeTy *> &Blocks) {
    LiveInBlocks = &Blocks;
    useLiveIn = true;
  }

  /// Reset the live-in block set to be empty, and tell the IDF
  /// calculator to not use liveness anymore.
  void resetLiveInBlocks() {
    LiveInBlocks = nullptr;
    useLiveIn = false;
  }

  /// Calculate iterated dominance frontiers
  ///
  /// This uses the linear-time phi algorithm based on DJ-graphs mentioned in
  /// the file-level comment.  It performs DF->IDF pruning using the live-in
  /// set, to avoid computing the IDF for blocks where an inserted PHI node
  /// would be dead.
  void calculate(SmallVectorImpl<NodeTy *> &IDFBlocks);

private:
  DominatorTreeBase<NodeTy, IsPostDom> &DT;
  ChildrenGetterTy ChildrenGetter;
  bool useLiveIn = false;
  const SmallPtrSetImpl<NodeTy *> *LiveInBlocks;
  const SmallPtrSetImpl<NodeTy *> *DefBlocks;
};

//===----------------------------------------------------------------------===//
// Implementation.
//===----------------------------------------------------------------------===//

namespace IDFCalculatorDetail {

template <class NodeTy, bool IsPostDom>
typename ChildrenGetterTy<NodeTy, IsPostDom>::range
ChildrenGetterTy<NodeTy, IsPostDom>::get(const NodeRef &N) {
  using OrderedNodeTy =
      typename IDFCalculatorBase<NodeTy, IsPostDom>::OrderedNodeTy;

  return children<OrderedNodeTy>(N);
}

} // end of namespace IDFCalculatorDetail

template <class NodeTy, bool IsPostDom>
void IDFCalculatorBase<NodeTy, IsPostDom>::calculate(
    SmallVectorImpl<NodeTy *> &IDFBlocks) {
  // Use a priority queue keyed on dominator tree level so that inserted nodes
  // are handled from the bottom of the dominator tree upwards. We also augment
  // the level with a DFS number to ensure that the blocks are ordered in a
  // deterministic way.
  using DomTreeNodePair =
      std::pair<DomTreeNodeBase<NodeTy> *, std::pair<unsigned, unsigned>>;
  using IDFPriorityQueue =
      std::priority_queue<DomTreeNodePair, SmallVector<DomTreeNodePair, 32>,
                          less_second>;

  IDFPriorityQueue PQ;

  DT.updateDFSNumbers();

  SmallVector<DomTreeNodeBase<NodeTy> *, 32> Worklist;
  SmallPtrSet<DomTreeNodeBase<NodeTy> *, 32> VisitedPQ;
  SmallPtrSet<DomTreeNodeBase<NodeTy> *, 32> VisitedWorklist;

  for (NodeTy *BB : *DefBlocks)
    if (DomTreeNodeBase<NodeTy> *Node = DT.getNode(BB)) {
      PQ.push({Node, std::make_pair(Node->getLevel(), Node->getDFSNumIn())});
      VisitedWorklist.insert(Node);
    }

  while (!PQ.empty()) {
    DomTreeNodePair RootPair = PQ.top();
    PQ.pop();
    DomTreeNodeBase<NodeTy> *Root = RootPair.first;
    unsigned RootLevel = RootPair.second.first;

    // Walk all dominator tree children of Root, inspecting their CFG edges with
    // targets elsewhere on the dominator tree. Only targets whose level is at
    // most Root's level are added to the iterated dominance frontier of the
    // definition set.

    assert(Worklist.empty());
    Worklist.push_back(Root);

    while (!Worklist.empty()) {
      DomTreeNodeBase<NodeTy> *Node = Worklist.pop_back_val();
      NodeTy *BB = Node->getBlock();
      // Succ is the successor in the direction we are calculating IDF, so it is
      // successor for IDF, and predecessor for Reverse IDF.
      auto DoWork = [&](NodeTy *Succ) {
        DomTreeNodeBase<NodeTy> *SuccNode = DT.getNode(Succ);

        const unsigned SuccLevel = SuccNode->getLevel();
        if (SuccLevel > RootLevel)
          return;

        if (!VisitedPQ.insert(SuccNode).second)
          return;

        NodeTy *SuccBB = SuccNode->getBlock();
        if (useLiveIn && !LiveInBlocks->count(SuccBB))
          return;

        IDFBlocks.emplace_back(SuccBB);
        if (!DefBlocks->count(SuccBB))
          PQ.push(std::make_pair(
              SuccNode, std::make_pair(SuccLevel, SuccNode->getDFSNumIn())));
      };

      for (auto *Succ : ChildrenGetter.get(BB))
        DoWork(Succ);

      for (auto DomChild : *Node) {
        if (VisitedWorklist.insert(DomChild).second)
          Worklist.push_back(DomChild);
      }
    }
  }
}

} // end of namespace llvm

#endif
