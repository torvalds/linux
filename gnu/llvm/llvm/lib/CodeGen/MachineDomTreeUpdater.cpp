//===- MachineDomTreeUpdater.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MachineDomTreeUpdater class, which provides a
// uniform way to update dominator tree related data structures.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineDomTreeUpdater.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/GenericDomTreeUpdaterImpl.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/Support/GenericDomTree.h"
#include <algorithm>
#include <functional>
#include <utility>

namespace llvm {

template class GenericDomTreeUpdater<
    MachineDomTreeUpdater, MachineDominatorTree, MachinePostDominatorTree>;

template void
GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                      MachinePostDominatorTree>::recalculate(MachineFunction
                                                                 &MF);

bool MachineDomTreeUpdater::forceFlushDeletedBB() {
  if (DeletedBBs.empty())
    return false;

  for (auto *BB : DeletedBBs) {
    eraseDelBBNode(BB);
    BB->eraseFromParent();
  }
  DeletedBBs.clear();
  return true;
}

// The DT and PDT require the nodes related to updates
// are not deleted when update functions are called.
// So MachineBasicBlock deletions must be pended when the
// UpdateStrategy is Lazy. When the UpdateStrategy is
// Eager, the MachineBasicBlock will be deleted immediately.
void MachineDomTreeUpdater::deleteBB(MachineBasicBlock *DelBB) {
  validateDeleteBB(DelBB);
  if (Strategy == UpdateStrategy::Lazy) {
    DeletedBBs.insert(DelBB);
    return;
  }

  eraseDelBBNode(DelBB);
  DelBB->eraseFromParent();
}

void MachineDomTreeUpdater::validateDeleteBB(MachineBasicBlock *DelBB) {
  assert(DelBB && "Invalid push_back of nullptr DelBB.");
  assert(DelBB->pred_empty() && "DelBB has one or more predecessors.");
}

} // namespace llvm
