//===- DomTreeUpdater.h - DomTree/Post DomTree Updater ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DomTreeUpdater class, which provides a uniform way to
// update dominator tree related data structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMTREEUPDATER_H
#define LLVM_ANALYSIS_DOMTREEUPDATER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/GenericDomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <functional>
#include <vector>

namespace llvm {

class PostDominatorTree;

class DomTreeUpdater
    : public GenericDomTreeUpdater<DomTreeUpdater, DominatorTree,
                                   PostDominatorTree> {
  friend GenericDomTreeUpdater<DomTreeUpdater, DominatorTree,
                               PostDominatorTree>;

public:
  using Base =
      GenericDomTreeUpdater<DomTreeUpdater, DominatorTree, PostDominatorTree>;
  using Base::Base;

  ~DomTreeUpdater() { flush(); }

  ///@{
  /// \name Mutation APIs
  ///
  /// These methods provide APIs for submitting updates to the DominatorTree and
  /// the PostDominatorTree.
  ///
  /// Note: There are two strategies to update the DominatorTree and the
  /// PostDominatorTree:
  /// 1. Eager UpdateStrategy: Updates are submitted and then flushed
  /// immediately.
  /// 2. Lazy UpdateStrategy: Updates are submitted but only flushed when you
  /// explicitly call Flush APIs. It is recommended to use this update strategy
  /// when you submit a bunch of updates multiple times which can then
  /// add up to a large number of updates between two queries on the
  /// DominatorTree. The incremental updater can reschedule the updates or
  /// decide to recalculate the dominator tree in order to speedup the updating
  /// process depending on the number of updates.
  ///
  /// Although GenericDomTree provides several update primitives,
  /// it is not encouraged to use these APIs directly.

  /// Delete DelBB. DelBB will be removed from its Parent and
  /// erased from available trees if it exists and finally get deleted.
  /// Under Eager UpdateStrategy, DelBB will be processed immediately.
  /// Under Lazy UpdateStrategy, DelBB will be queued until a flush event and
  /// all available trees are up-to-date. Assert if any instruction of DelBB is
  /// modified while awaiting deletion. When both DT and PDT are nullptrs, DelBB
  /// will be queued until flush() is called.
  void deleteBB(BasicBlock *DelBB);

  /// Delete DelBB. DelBB will be removed from its Parent and
  /// erased from available trees if it exists. Then the callback will
  /// be called. Finally, DelBB will be deleted.
  /// Under Eager UpdateStrategy, DelBB will be processed immediately.
  /// Under Lazy UpdateStrategy, DelBB will be queued until a flush event and
  /// all available trees are up-to-date. Assert if any instruction of DelBB is
  /// modified while awaiting deletion. Multiple callbacks can be queued for one
  /// DelBB under Lazy UpdateStrategy.
  void callbackDeleteBB(BasicBlock *DelBB,
                        std::function<void(BasicBlock *)> Callback);

  ///@}

private:
  class CallBackOnDeletion final : public CallbackVH {
  public:
    CallBackOnDeletion(BasicBlock *V,
                       std::function<void(BasicBlock *)> Callback)
        : CallbackVH(V), DelBB(V), Callback_(Callback) {}

  private:
    BasicBlock *DelBB = nullptr;
    std::function<void(BasicBlock *)> Callback_;

    void deleted() override {
      Callback_(DelBB);
      CallbackVH::deleted();
    }
  };

  std::vector<CallBackOnDeletion> Callbacks;

  /// First remove all the instructions of DelBB and then make sure DelBB has a
  /// valid terminator instruction which is necessary to have when DelBB still
  /// has to be inside of its parent Function while awaiting deletion under Lazy
  /// UpdateStrategy to prevent other routines from asserting the state of the
  /// IR is inconsistent. Assert if DelBB is nullptr or has predecessors.
  void validateDeleteBB(BasicBlock *DelBB);

  /// Returns true if at least one BasicBlock is deleted.
  bool forceFlushDeletedBB();

  /// Debug method to help view the internal state of this class.
  LLVM_DUMP_METHOD void dump() const;
};

extern template class GenericDomTreeUpdater<DomTreeUpdater, DominatorTree,
                                            PostDominatorTree>;

extern template void
GenericDomTreeUpdater<DomTreeUpdater, DominatorTree,
                      PostDominatorTree>::recalculate(Function &F);
} // namespace llvm

#endif // LLVM_ANALYSIS_DOMTREEUPDATER_H
