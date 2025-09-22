//===- IntervalPartition.h - CFG Partitioning into Intervals -----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines functionality for partitioning a CFG into intervals and
//  building a weak topological order (WTO) of the nodes, based on the
//  partitioning. The concepts and implementations for the graph partitioning
//  are based on the presentation in "Compilers" by Aho, Sethi and Ullman (the
//  "dragon book"), pages 664-666. The concepts around WTOs is taken from the
//  paper "Efficient chaotic iteration strategies with widenings," by
//  F. Bourdoncle ([Bourdoncle1993]).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_INTERVALPARTITION_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_INTERVALPARTITION_H

#include "clang/Analysis/CFG.h"
#include "llvm/ADT/DenseSet.h"
#include <deque>
#include <memory>
#include <vector>

namespace clang {
/// A _weak topological ordering_ (WTO) of CFG nodes provides a total order over
/// the CFG (defined in `WTOCompare`, below), which can guide the order in which
/// to visit nodes in fixpoint computations over the CFG.
///
/// Roughly, a WTO a) groups the blocks so that loop heads are grouped with
/// their bodies and any nodes they dominate after the loop and b) orders the
/// groups topologically. As a result, the blocks in a series of loops are
/// ordered such that all nodes in loop `i` are earlier in the order than nodes
/// in loop `j`. This ordering, when combined with widening, bounds the number
/// of times a node must be visited for a dataflow algorithm to reach a
/// fixpoint. For the precise definition of a WTO and its properties, see
/// [Bourdoncle1993].
///
/// Here, we provide a simplified WTO which drops its nesting structure,
/// maintaining only the ordering itself. The ordering is built from the limit
/// flow graph of `Cfg` (derived from iteratively partitioning it into
/// intervals) if and only if it is reducible (its limit flow graph has one
/// node). Returns `nullopt` when `Cfg` is not reducible.
///
/// This WTO construction is described in Section 4.2 of [Bourdoncle1993].
using WeakTopologicalOrdering = std::vector<const CFGBlock *>;
std::optional<WeakTopologicalOrdering> getIntervalWTO(const CFG &Cfg);

struct WTOCompare {
  WTOCompare(const WeakTopologicalOrdering &WTO);

  bool operator()(const CFGBlock *B1, const CFGBlock *B2) const {
    auto ID1 = B1->getBlockID();
    auto ID2 = B2->getBlockID();

    unsigned V1 = ID1 >= BlockOrder.size() ? 0 : BlockOrder[ID1];
    unsigned V2 = ID2 >= BlockOrder.size() ? 0 : BlockOrder[ID2];
    return V1 > V2;
  }

  std::vector<unsigned> BlockOrder;
};

namespace internal {
// An interval is a strongly-connected component of the CFG along with a
// trailing acyclic structure. An interval can be constructed directly from CFG
// blocks or from a graph of other intervals. Each interval has one _header_
// block, from which the interval is built. The _header_ of the interval is
// either the graph's entry block or has at least one predecessor outside of the
// interval. All other blocks in the interval have only predecessors also in the
// interval.
struct CFGIntervalNode {
  CFGIntervalNode() = default;
  CFGIntervalNode(unsigned ID) : ID(ID) {}

  CFGIntervalNode(unsigned ID, std::vector<const CFGBlock *> Nodes)
      : ID(ID), Nodes(std::move(Nodes)) {}

  const llvm::SmallDenseSet<const CFGIntervalNode *> &preds() const {
    return Predecessors;
  }
  const llvm::SmallDenseSet<const CFGIntervalNode *> &succs() const {
    return Successors;
  }

  // Unique identifier of this interval relative to other intervals in the same
  // graph.
  unsigned ID;

  std::vector<const CFGBlock *> Nodes;

  // Predessor intervals of this interval: those intervals for which there
  // exists an edge from a node in that other interval to the head of this
  // interval.
  llvm::SmallDenseSet<const CFGIntervalNode *> Predecessors;

  // Successor intervals of this interval: those intervals for which there
  // exists an edge from a node in this interval to the head of that other
  // interval.
  llvm::SmallDenseSet<const CFGIntervalNode *> Successors;
};

// Since graphs are built from pointers to nodes, we use a deque to ensure
// pointer stability.
using CFGIntervalGraph = std::deque<CFGIntervalNode>;

std::vector<const CFGBlock *> buildInterval(const CFGBlock *Header);

// Partitions `Cfg` into intervals and constructs the graph of the intervals
// based on the edges between nodes in these intervals.
CFGIntervalGraph partitionIntoIntervals(const CFG &Cfg);

// (Further) partitions `Graph` into intervals and constructs the graph of the
// intervals based on the edges between nodes (themselves intervals) in these
// intervals.
CFGIntervalGraph partitionIntoIntervals(const CFGIntervalGraph &Graph);
} // namespace internal
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_INTERVALPARTITION_H
