//===- DomTreeUpdater.cpp - DomTree/Post DomTree Updater --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the DomTreeUpdater class, which provides a uniform way
// to update dominator tree related data structures.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/GenericDomTreeUpdaterImpl.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/GenericDomTree.h"
#include <algorithm>
#include <functional>
#include <utility>

namespace llvm {

template class GenericDomTreeUpdater<DomTreeUpdater, DominatorTree,
                                     PostDominatorTree>;

template void
GenericDomTreeUpdater<DomTreeUpdater, DominatorTree,
                      PostDominatorTree>::recalculate(Function &F);

bool DomTreeUpdater::forceFlushDeletedBB() {
  if (DeletedBBs.empty())
    return false;

  for (auto *BB : DeletedBBs) {
    // After calling deleteBB or callbackDeleteBB under Lazy UpdateStrategy,
    // validateDeleteBB() removes all instructions of DelBB and adds an
    // UnreachableInst as its terminator. So we check whether the BasicBlock to
    // delete only has an UnreachableInst inside.
    assert(BB->size() == 1 && isa<UnreachableInst>(BB->getTerminator()) &&
           "DelBB has been modified while awaiting deletion.");
    BB->removeFromParent();
    eraseDelBBNode(BB);
    delete BB;
  }
  DeletedBBs.clear();
  Callbacks.clear();
  return true;
}

// The DT and PDT require the nodes related to updates
// are not deleted when update functions are called.
// So BasicBlock deletions must be pended when the
// UpdateStrategy is Lazy. When the UpdateStrategy is
// Eager, the BasicBlock will be deleted immediately.
void DomTreeUpdater::deleteBB(BasicBlock *DelBB) {
  validateDeleteBB(DelBB);
  if (Strategy == UpdateStrategy::Lazy) {
    DeletedBBs.insert(DelBB);
    return;
  }

  DelBB->removeFromParent();
  eraseDelBBNode(DelBB);
  delete DelBB;
}

void DomTreeUpdater::callbackDeleteBB(
    BasicBlock *DelBB, std::function<void(BasicBlock *)> Callback) {
  validateDeleteBB(DelBB);
  if (Strategy == UpdateStrategy::Lazy) {
    Callbacks.push_back(CallBackOnDeletion(DelBB, Callback));
    DeletedBBs.insert(DelBB);
    return;
  }

  DelBB->removeFromParent();
  eraseDelBBNode(DelBB);
  Callback(DelBB);
  delete DelBB;
}

void DomTreeUpdater::validateDeleteBB(BasicBlock *DelBB) {
  assert(DelBB && "Invalid push_back of nullptr DelBB.");
  assert(pred_empty(DelBB) && "DelBB has one or more predecessors.");
  // DelBB is unreachable and all its instructions are dead.
  while (!DelBB->empty()) {
    Instruction &I = DelBB->back();
    // Replace used instructions with an arbitrary value (poison).
    if (!I.use_empty())
      I.replaceAllUsesWith(PoisonValue::get(I.getType()));
    DelBB->back().eraseFromParent();
  }
  // Make sure DelBB has a valid terminator instruction. As long as DelBB is a
  // Child of Function F it must contain valid IR.
  new UnreachableInst(DelBB->getContext(), DelBB);
}

LLVM_DUMP_METHOD
void DomTreeUpdater::dump() const {
  Base::dump();
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  raw_ostream &OS = dbgs();
  OS << "Pending Callbacks:\n";
  int Index = 0;
  for (const auto &BB : Callbacks) {
    OS << "  " << Index << " : ";
    ++Index;
    if (BB->hasName())
      OS << BB->getName() << "(";
    else
      OS << "(no_name)(";
    OS << BB << ")\n";
  }
#endif
}

} // namespace llvm
