//===- DataflowWorklist.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A simple and reusable worklist for flow-sensitive analyses.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWWORKLIST_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWWORKLIST_H

#include "clang/Analysis/Analyses/IntervalPartition.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/PriorityQueue.h"

namespace clang {
/// A worklist implementation where the enqueued blocks will be dequeued based
/// on the order defined by 'Comp'.
template <typename Comp, unsigned QueueSize> class DataflowWorklistBase {
  llvm::BitVector EnqueuedBlocks;
  llvm::PriorityQueue<const CFGBlock *,
                      SmallVector<const CFGBlock *, QueueSize>, Comp>
      WorkList;

public:
  DataflowWorklistBase(const CFG &Cfg, Comp C)
      : EnqueuedBlocks(Cfg.getNumBlockIDs()), WorkList(C) {}

  void enqueueBlock(const CFGBlock *Block) {
    if (Block && !EnqueuedBlocks[Block->getBlockID()]) {
      EnqueuedBlocks[Block->getBlockID()] = true;
      WorkList.push(Block);
    }
  }

  const CFGBlock *dequeue() {
    if (WorkList.empty())
      return nullptr;
    const CFGBlock *B = WorkList.top();
    WorkList.pop();
    EnqueuedBlocks[B->getBlockID()] = false;
    return B;
  }
};

struct ReversePostOrderCompare {
  PostOrderCFGView::BlockOrderCompare Cmp;
  bool operator()(const CFGBlock *lhs, const CFGBlock *rhs) const {
    return Cmp(rhs, lhs);
  }
};

/// A worklist implementation for forward dataflow analysis. The enqueued
/// blocks will be dequeued in reverse post order. The worklist cannot contain
/// the same block multiple times at once.
struct ForwardDataflowWorklist
    : DataflowWorklistBase<ReversePostOrderCompare, 20> {
  ForwardDataflowWorklist(const CFG &Cfg, PostOrderCFGView *POV)
      : DataflowWorklistBase(Cfg,
                             ReversePostOrderCompare{POV->getComparator()}) {}

  ForwardDataflowWorklist(const CFG &Cfg, AnalysisDeclContext &Ctx)
      : ForwardDataflowWorklist(Cfg, Ctx.getAnalysis<PostOrderCFGView>()) {}

  void enqueueSuccessors(const CFGBlock *Block) {
    for (auto B : Block->succs())
      enqueueBlock(B);
  }
};

/// A worklist implementation for forward dataflow analysis based on a weak
/// topological ordering of the nodes. The worklist cannot contain the same
/// block multiple times at once.
struct WTODataflowWorklist : DataflowWorklistBase<WTOCompare, 20> {
  WTODataflowWorklist(const CFG &Cfg, const WTOCompare &Cmp)
      : DataflowWorklistBase(Cfg, Cmp) {}

  void enqueueSuccessors(const CFGBlock *Block) {
    for (auto B : Block->succs())
      enqueueBlock(B);
  }
};

/// A worklist implementation for backward dataflow analysis. The enqueued
/// block will be dequeued in post order. The worklist cannot contain the same
/// block multiple times at once.
struct BackwardDataflowWorklist
    : DataflowWorklistBase<PostOrderCFGView::BlockOrderCompare, 20> {
  BackwardDataflowWorklist(const CFG &Cfg, AnalysisDeclContext &Ctx)
      : DataflowWorklistBase(
            Cfg, Ctx.getAnalysis<PostOrderCFGView>()->getComparator()) {}

  void enqueuePredecessors(const CFGBlock *Block) {
    for (auto B : Block->preds())
      enqueueBlock(B);
  }
};

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWWORKLIST_H
