//===-- Analysis/EHUtils.h - Exception handling related utils --*-//C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//

#ifndef LLVM_ANALYSIS_EHUTILS_H
#define LLVM_ANALYSIS_EHUTILS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace llvm {

/// Compute a list of blocks that are only reachable via EH paths.
template <typename FunctionT, typename BlockT>
static void computeEHOnlyBlocks(FunctionT &F, DenseSet<BlockT *> &EHBlocks) {
  // A block can be unknown if its not reachable from anywhere
  // EH if its only reachable from start blocks via some path through EH pads
  // NonEH if it's reachable from Non EH blocks as well.
  enum Status { Unknown = 0, EH = 1, NonEH = 2 };
  DenseSet<BlockT *> WorkList;
  DenseMap<BlockT *, Status> Statuses;

  auto GetStatus = [&](BlockT *BB) {
    if (Statuses.contains(BB))
      return Statuses[BB];
    else
      return Unknown;
  };

  auto CheckPredecessors = [&](BlockT *BB, Status Stat) {
    for (auto *PredBB : predecessors(BB)) {
      Status PredStatus = GetStatus(PredBB);
      // If status of predecessor block has gone above current block
      // we update current blocks status.
      if (PredStatus > Stat)
        Stat = PredStatus;
    }
    return Stat;
  };

  auto AddSuccesors = [&](BlockT *BB) {
    for (auto *SuccBB : successors(BB)) {
      if (!SuccBB->isEHPad())
        WorkList.insert(SuccBB);
    }
  };

  // Insert the successors of start block and landing pads successor.
  BlockT *StartBlock = &F.front();
  Statuses[StartBlock] = NonEH;
  AddSuccesors(StartBlock);

  for (auto &BB : F) {
    if (BB.isEHPad()) {
      AddSuccesors(&BB);
      Statuses[&BB] = EH;
    }
  }

  // Worklist iterative algorithm.
  while (!WorkList.empty()) {
    auto *BB = *WorkList.begin();
    WorkList.erase(BB);

    Status OldStatus = GetStatus(BB);

    // Check on predecessors and check for
    // Status update.
    Status NewStatus = CheckPredecessors(BB, OldStatus);

    // Did the block status change?
    bool Changed = OldStatus != NewStatus;
    if (Changed) {
      AddSuccesors(BB);
      Statuses[BB] = NewStatus;
    }
  }

  for (auto Entry : Statuses) {
    if (Entry.second == EH)
      EHBlocks.insert(Entry.first);
  }
}
} // namespace llvm

#endif
