//===- GenericDomTreeUpdater.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the GenericDomTreeUpdater class, which provides a uniform
// way to update dominator tree related data structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GENERICDOMTREEUPDATER_H
#define LLVM_ANALYSIS_GENERICDOMTREEUPDATER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
class GenericDomTreeUpdater {
  DerivedT &derived() { return *static_cast<DerivedT *>(this); }
  const DerivedT &derived() const {
    return *static_cast<const DerivedT *>(this);
  }

public:
  enum class UpdateStrategy : unsigned char { Eager = 0, Lazy = 1 };
  using BasicBlockT = typename DomTreeT::NodeType;

  explicit GenericDomTreeUpdater(UpdateStrategy Strategy_)
      : Strategy(Strategy_) {}
  GenericDomTreeUpdater(DomTreeT &DT_, UpdateStrategy Strategy_)
      : DT(&DT_), Strategy(Strategy_) {}
  GenericDomTreeUpdater(DomTreeT *DT_, UpdateStrategy Strategy_)
      : DT(DT_), Strategy(Strategy_) {}
  GenericDomTreeUpdater(PostDomTreeT &PDT_, UpdateStrategy Strategy_)
      : PDT(&PDT_), Strategy(Strategy_) {}
  GenericDomTreeUpdater(PostDomTreeT *PDT_, UpdateStrategy Strategy_)
      : PDT(PDT_), Strategy(Strategy_) {}
  GenericDomTreeUpdater(DomTreeT &DT_, PostDomTreeT &PDT_,
                        UpdateStrategy Strategy_)
      : DT(&DT_), PDT(&PDT_), Strategy(Strategy_) {}
  GenericDomTreeUpdater(DomTreeT *DT_, PostDomTreeT *PDT_,
                        UpdateStrategy Strategy_)
      : DT(DT_), PDT(PDT_), Strategy(Strategy_) {}

  ~GenericDomTreeUpdater() {
    // We cannot call into derived() here as it will already be destroyed.
    assert(!hasPendingUpdates() &&
           "Pending updates were not flushed by derived class.");
  }

  /// Returns true if the current strategy is Lazy.
  bool isLazy() const { return Strategy == UpdateStrategy::Lazy; }

  /// Returns true if the current strategy is Eager.
  bool isEager() const { return Strategy == UpdateStrategy::Eager; }

  /// Returns true if it holds a DomTreeT.
  bool hasDomTree() const { return DT != nullptr; }

  /// Returns true if it holds a PostDomTreeT.
  bool hasPostDomTree() const { return PDT != nullptr; }

  /// Returns true if there is BasicBlockT awaiting deletion.
  /// The deletion will only happen until a flush event and
  /// all available trees are up-to-date.
  /// Returns false under Eager UpdateStrategy.
  bool hasPendingDeletedBB() const { return !DeletedBBs.empty(); }

  /// Returns true if DelBB is awaiting deletion.
  /// Returns false under Eager UpdateStrategy.
  bool isBBPendingDeletion(BasicBlockT *DelBB) const {
    if (Strategy == UpdateStrategy::Eager || DeletedBBs.empty())
      return false;
    return DeletedBBs.contains(DelBB);
  }

  /// Returns true if either of DT or PDT is valid and the tree has at
  /// least one update pending. If DT or PDT is nullptr it is treated
  /// as having no pending updates. This function does not check
  /// whether there is MachineBasicBlock awaiting deletion.
  /// Returns false under Eager UpdateStrategy.
  bool hasPendingUpdates() const {
    return hasPendingDomTreeUpdates() || hasPendingPostDomTreeUpdates();
  }

  /// Returns true if there are DomTreeT updates queued.
  /// Returns false under Eager UpdateStrategy or DT is nullptr.
  bool hasPendingDomTreeUpdates() const {
    if (!DT)
      return false;
    return PendUpdates.size() != PendDTUpdateIndex;
  }

  /// Returns true if there are PostDomTreeT updates queued.
  /// Returns false under Eager UpdateStrategy or PDT is nullptr.
  bool hasPendingPostDomTreeUpdates() const {
    if (!PDT)
      return false;
    return PendUpdates.size() != PendPDTUpdateIndex;
  }

  ///@{
  /// \name Mutation APIs
  ///
  /// These methods provide APIs for submitting updates to the DomTreeT and
  /// the PostDominatorTree.
  ///
  /// Note: There are two strategies to update the DomTreeT and the
  /// PostDominatorTree:
  /// 1. Eager UpdateStrategy: Updates are submitted and then flushed
  /// immediately.
  /// 2. Lazy UpdateStrategy: Updates are submitted but only flushed when you
  /// explicitly call Flush APIs. It is recommended to use this update strategy
  /// when you submit a bunch of updates multiple times which can then
  /// add up to a large number of updates between two queries on the
  /// DomTreeT. The incremental updater can reschedule the updates or
  /// decide to recalculate the dominator tree in order to speedup the updating
  /// process depending on the number of updates.
  ///
  /// Although GenericDomTree provides several update primitives,
  /// it is not encouraged to use these APIs directly.

  /// Notify DTU that the entry block was replaced.
  /// Recalculate all available trees and flush all BasicBlocks
  /// awaiting deletion immediately.
  template <typename FuncT> void recalculate(FuncT &F);

  /// Submit updates to all available trees.
  /// The Eager Strategy flushes updates immediately while the Lazy Strategy
  /// queues the updates.
  ///
  /// Note: The "existence" of an edge in a CFG refers to the CFG which DTU is
  /// in sync with + all updates before that single update.
  ///
  /// CAUTION!
  /// 1. It is required for the state of the LLVM IR to be updated
  /// *before* submitting the updates because the internal update routine will
  /// analyze the current state of the CFG to determine whether an update
  /// is valid.
  /// 2. It is illegal to submit any update that has already been submitted,
  /// i.e., you are supposed not to insert an existent edge or delete a
  /// nonexistent edge.
  void applyUpdates(ArrayRef<typename DomTreeT::UpdateType> Updates);

  /// Submit updates to all available trees. It will also
  /// 1. discard duplicated updates,
  /// 2. remove invalid updates. (Invalid updates means deletion of an edge that
  /// still exists or insertion of an edge that does not exist.)
  /// The Eager Strategy flushes updates immediately while the Lazy Strategy
  /// queues the updates.
  ///
  /// Note: The "existence" of an edge in a CFG refers to the CFG which DTU is
  /// in sync with + all updates before that single update.
  ///
  /// CAUTION!
  /// 1. It is required for the state of the LLVM IR to be updated
  /// *before* submitting the updates because the internal update routine will
  /// analyze the current state of the CFG to determine whether an update
  /// is valid.
  /// 2. It is illegal to submit any update that has already been submitted,
  /// i.e., you are supposed not to insert an existent edge or delete a
  /// nonexistent edge.
  /// 3. It is only legal to submit updates to an edge in the order CFG changes
  /// are made. The order you submit updates on different edges is not
  /// restricted.
  void applyUpdatesPermissive(ArrayRef<typename DomTreeT::UpdateType> Updates);

  ///@}

  ///@{
  /// \name Flush APIs
  ///
  /// CAUTION! By the moment these flush APIs are called, the current CFG needs
  /// to be the same as the CFG which DTU is in sync with + all updates
  /// submitted.

  /// Flush DomTree updates and return DomTree.
  /// It flushes Deleted BBs if both trees are up-to-date.
  /// It must only be called when it has a DomTree.
  DomTreeT &getDomTree();

  /// Flush PostDomTree updates and return PostDomTree.
  /// It flushes Deleted BBs if both trees are up-to-date.
  /// It must only be called when it has a PostDomTree.
  PostDomTreeT &getPostDomTree();

  /// Apply all pending updates to available trees and flush all BasicBlocks
  /// awaiting deletion.

  void flush() {
    applyDomTreeUpdates();
    applyPostDomTreeUpdates();
    dropOutOfDateUpdates();
  }

  ///@}

  /// Debug method to help view the internal state of this class.
  LLVM_DUMP_METHOD void dump() const;

protected:
  SmallVector<typename DomTreeT::UpdateType, 16> PendUpdates;
  size_t PendDTUpdateIndex = 0;
  size_t PendPDTUpdateIndex = 0;
  DomTreeT *DT = nullptr;
  PostDomTreeT *PDT = nullptr;
  const UpdateStrategy Strategy;
  SmallPtrSet<BasicBlockT *, 8> DeletedBBs;
  bool IsRecalculatingDomTree = false;
  bool IsRecalculatingPostDomTree = false;

  /// Returns true if the update is self dominance.
  bool isSelfDominance(typename DomTreeT::UpdateType Update) const {
    // Won't affect DomTree and PostDomTree.
    return Update.getFrom() == Update.getTo();
  }

  /// Helper function to apply all pending DomTree updates.
  void applyDomTreeUpdates();

  /// Helper function to apply all pending PostDomTree updates.
  void applyPostDomTreeUpdates();

  /// Returns true if the update appears in the LLVM IR.
  /// It is used to check whether an update is valid in
  /// insertEdge/deleteEdge or is unnecessary in the batch update.
  bool isUpdateValid(typename DomTreeT::UpdateType Update) const;

  /// Erase Basic Block node that has been unlinked from Function
  /// in the DomTree and PostDomTree.
  void eraseDelBBNode(BasicBlockT *DelBB);

  /// Helper function to flush deleted BasicBlocks if all available
  /// trees are up-to-date.
  void tryFlushDeletedBB();

  /// Drop all updates applied by all available trees and delete BasicBlocks if
  /// all available trees are up-to-date.
  void dropOutOfDateUpdates();
};

} // namespace llvm

#endif // LLVM_ANALYSIS_GENERICDOMTREEUPDATER_H
