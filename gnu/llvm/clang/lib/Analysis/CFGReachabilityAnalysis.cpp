//===- CFGReachabilityAnalysis.cpp - Basic reachability analysis ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a flow-sensitive, (mostly) path-insensitive reachability
// analysis based on Clang's CFGs.  Clients can query if a given basic block
// is reachable within the CFG.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/CFGReachabilityAnalysis.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"

using namespace clang;

CFGReverseBlockReachabilityAnalysis::CFGReverseBlockReachabilityAnalysis(
    const CFG &cfg)
    : analyzed(cfg.getNumBlockIDs(), false) {}

bool CFGReverseBlockReachabilityAnalysis::isReachable(const CFGBlock *Src,
                                          const CFGBlock *Dst) {
  const unsigned DstBlockID = Dst->getBlockID();

  // If we haven't analyzed the destination node, run the analysis now
  if (!analyzed[DstBlockID]) {
    mapReachability(Dst);
    analyzed[DstBlockID] = true;
  }

  // Return the cached result
  return reachable[DstBlockID][Src->getBlockID()];
}

// Maps reachability to a common node by walking the predecessors of the
// destination node.
void CFGReverseBlockReachabilityAnalysis::mapReachability(const CFGBlock *Dst) {
  SmallVector<const CFGBlock *, 11> worklist;
  llvm::BitVector visited(analyzed.size());

  ReachableSet &DstReachability = reachable[Dst->getBlockID()];
  DstReachability.resize(analyzed.size(), false);

  // Start searching from the destination node, since we commonly will perform
  // multiple queries relating to a destination node.
  worklist.push_back(Dst);
  bool firstRun = true;

  while (!worklist.empty()) {
    const CFGBlock *block = worklist.pop_back_val();

    if (visited[block->getBlockID()])
      continue;
    visited[block->getBlockID()] = true;

    // Update reachability information for this node -> Dst
    if (!firstRun) {
      // Don't insert Dst -> Dst unless it was a predecessor of itself
      DstReachability[block->getBlockID()] = true;
    }
    else
      firstRun = false;

    // Add the predecessors to the worklist.
    for (CFGBlock::const_pred_iterator i = block->pred_begin(),
         e = block->pred_end(); i != e; ++i) {
      if (*i)
        worklist.push_back(*i);
    }
  }
}
