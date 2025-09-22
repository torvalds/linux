//===- IntervalPartition.cpp - CFG Partitioning into Intervals --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines functionality for partitioning a CFG into intervals.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/IntervalPartition.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include <optional>
#include <queue>
#include <vector>

namespace clang {

// Intermediate data used in constructing a CFGIntervalNode.
template <typename Node> struct BuildResult {
  // Use a vector to maintain the insertion order. Given the expected small
  // number of nodes, vector should be sufficiently efficient. Elements must not
  // be null.
  std::vector<const Node *> Nodes;
  // Elements must not be null.
  llvm::SmallDenseSet<const Node *> Successors;
};

namespace internal {
static unsigned getID(const CFGBlock &B) { return B.getBlockID(); }
static unsigned getID(const CFGIntervalNode &I) { return I.ID; }

// `Node` must be one of `CFGBlock` or `CFGIntervalNode`.
template <typename Node>
BuildResult<Node> buildInterval(llvm::BitVector &Partitioned,
                                const Node *Header) {
  assert(Header != nullptr);
  BuildResult<Node> Interval;
  Interval.Nodes.push_back(Header);
  Partitioned.set(getID(*Header));

  // FIXME: Compare performance against using RPO to consider nodes, rather than
  // following successors.
  //
  // Elements must not be null. Duplicates are prevented using `Workset`, below.
  std::queue<const Node *> Worklist;
  llvm::BitVector Workset(Partitioned.size(), false);
  for (const Node *S : Header->succs())
    if (S != nullptr)
      if (auto SID = getID(*S); !Partitioned.test(SID)) {
        // Successors are unique, so we don't test against `Workset` before
        // adding to `Worklist`.
        Worklist.push(S);
        Workset.set(SID);
      }

  // Contains successors of blocks in the interval that couldn't be added to the
  // interval on their first encounter. This occurs when they have a predecessor
  // that is either definitively outside the interval or hasn't been considered
  // yet. In the latter case, we'll revisit the block through some other path
  // from the interval. At the end of processing the worklist, we filter out any
  // that ended up in the interval to produce the output set of interval
  // successors. Elements are never null.
  std::vector<const Node *> MaybeSuccessors;

  while (!Worklist.empty()) {
    const auto *B = Worklist.front();
    auto ID = getID(*B);
    Worklist.pop();
    Workset.reset(ID);

    // Check whether all predecessors are in the interval, in which case `B`
    // is included as well.
    bool AllInInterval = llvm::all_of(B->preds(), [&](const Node *P) {
      return llvm::is_contained(Interval.Nodes, P);
    });
    if (AllInInterval) {
      Interval.Nodes.push_back(B);
      Partitioned.set(ID);
      for (const Node *S : B->succs())
        if (S != nullptr)
          if (auto SID = getID(*S);
              !Partitioned.test(SID) && !Workset.test(SID)) {
            Worklist.push(S);
            Workset.set(SID);
          }
    } else {
      MaybeSuccessors.push_back(B);
    }
  }

  // Any block successors not in the current interval are interval successors.
  for (const Node *B : MaybeSuccessors)
    if (!llvm::is_contained(Interval.Nodes, B))
      Interval.Successors.insert(B);

  return Interval;
}

template <typename Node>
void fillIntervalNode(CFGIntervalGraph &Graph,
                      std::vector<CFGIntervalNode *> &Index,
                      std::queue<const Node *> &Successors,
                      llvm::BitVector &Partitioned, const Node *Header) {
  BuildResult<Node> Result = buildInterval(Partitioned, Header);
  for (const auto *S : Result.Successors)
    Successors.push(S);

  CFGIntervalNode &Interval = Graph.emplace_back(Graph.size());

  // Index the nodes of the new interval. The index maps nodes from the input
  // graph (specifically, `Result.Nodes`) to identifiers of nodes in the output
  // graph. In this case, the new interval has identifier `ID` so all of its
  // nodes (`Result.Nodes`) map to `ID`.
  for (const auto *N : Result.Nodes) {
    assert(N != nullptr);
    assert(getID(*N) < Index.size());
    Index[getID(*N)] = &Interval;
  }

  if constexpr (std::is_same_v<std::decay_t<Node>, CFGBlock>)
    Interval.Nodes = std::move(Result.Nodes);
  else {
    std::vector<const CFGBlock *> Nodes;
    // Flatten the sub vectors into a single list.
    size_t Count = 0;
    for (auto &N : Result.Nodes)
      Count += N->Nodes.size();
    Nodes.reserve(Count);
    for (auto &N : Result.Nodes)
      Nodes.insert(Nodes.end(), N->Nodes.begin(), N->Nodes.end());
    Interval.Nodes = std::move(Nodes);
  }
}

template <typename Node>
CFGIntervalGraph partitionIntoIntervalsImpl(unsigned NumBlockIDs,
                                            const Node *EntryBlock) {
  assert(EntryBlock != nullptr);
  CFGIntervalGraph Graph;
  // `Index` maps all of the nodes of the input graph to the interval to which
  // they are assigned in the output graph. The values (interval pointers) are
  // never null.
  std::vector<CFGIntervalNode *> Index(NumBlockIDs, nullptr);

  // Lists header nodes (from the input graph) and their associated
  // interval. Since header nodes can vary in type and are only needed within
  // this function, we record them separately from `CFGIntervalNode`. This
  // choice enables to express `CFGIntervalNode` without using a variant.
  std::vector<std::pair<const Node *, CFGIntervalNode *>> Intervals;
  llvm::BitVector Partitioned(NumBlockIDs, false);
  std::queue<const Node *> Successors;

  fillIntervalNode(Graph, Index, Successors, Partitioned, EntryBlock);
  Intervals.emplace_back(EntryBlock, &Graph.back());

  while (!Successors.empty()) {
    const auto *B = Successors.front();
    Successors.pop();
    assert(B != nullptr);
    if (Partitioned.test(getID(*B)))
      continue;

    // B has not been partitioned, but it has a predecessor that has. Create a
    // new interval from `B`.
    fillIntervalNode(Graph, Index, Successors, Partitioned, B);
    Intervals.emplace_back(B, &Graph.back());
  }

  // Go back and patch up all the Intervals -- the successors and predecessors.
  for (auto [H, N] : Intervals) {
    // Map input-graph predecessors to output-graph nodes and mark those as
    // predecessors of `N`. Then, mark `N` as a successor of said predecessor.
    for (const Node *P : H->preds()) {
      if (P == nullptr)
        continue;

      assert(getID(*P) < NumBlockIDs);
      CFGIntervalNode *Pred = Index[getID(*P)];
      if (Pred == nullptr)
        // Unreachable node.
        continue;
      if (Pred != N // Not a backedge.
          && N->Predecessors.insert(Pred).second)
        // Note: given the guard above, which guarantees we only ever insert
        // unique elements, we could use a simple list (like `vector`) for
        // `Successors`, rather than a set.
        Pred->Successors.insert(N);
    }
  }

  return Graph;
}

std::vector<const CFGBlock *> buildInterval(const CFGBlock *Header) {
  llvm::BitVector Partitioned(Header->getParent()->getNumBlockIDs(), false);
  return buildInterval(Partitioned, Header).Nodes;
}

CFGIntervalGraph partitionIntoIntervals(const CFG &Cfg) {
  return partitionIntoIntervalsImpl(Cfg.getNumBlockIDs(), &Cfg.getEntry());
}

CFGIntervalGraph partitionIntoIntervals(const CFGIntervalGraph &Graph) {
  return partitionIntoIntervalsImpl(Graph.size(), &Graph[0]);
}
} // namespace internal

std::optional<std::vector<const CFGBlock *>> getIntervalWTO(const CFG &Cfg) {
  // Backing storage for the allocated nodes in each graph.
  unsigned PrevSize = Cfg.size();
  if (PrevSize == 0)
    return {};
  internal::CFGIntervalGraph Graph = internal::partitionIntoIntervals(Cfg);
  unsigned Size = Graph.size();
  while (Size > 1 && Size < PrevSize) {
    PrevSize = Graph.size();
    Graph = internal::partitionIntoIntervals(Graph);
    Size = Graph.size();
  }
  if (Size > 1)
    // Not reducible.
    return std::nullopt;

  assert(Size != 0);
  return std::move(Graph[0].Nodes);
}

WTOCompare::WTOCompare(const WeakTopologicalOrdering &WTO) {
  if (WTO.empty())
    return;
  auto N = WTO[0]->getParent()->getNumBlockIDs();
  BlockOrder.resize(N, 0);
  for (unsigned I = 0, S = WTO.size(); I < S; ++I)
    BlockOrder[WTO[I]->getBlockID()] = I + 1;
}
} // namespace clang
