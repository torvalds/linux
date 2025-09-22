//===- GenericDomTreeUpdaterImpl.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the GenericDomTreeUpdater class. This file should only
// be included by files that implement a specialization of the relevant
// templates. Currently these are:
// - llvm/lib/Analysis/DomTreeUpdater.cpp
// - llvm/lib/CodeGen/MachineDomTreeUpdater.cpp
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_ANALYSIS_GENERICDOMTREEUPDATERIMPL_H
#define LLVM_ANALYSIS_GENERICDOMTREEUPDATERIMPL_H

#include "llvm/Analysis/GenericDomTreeUpdater.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
template <typename FuncT>
void GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::recalculate(
    FuncT &F) {
  if (Strategy == UpdateStrategy::Eager) {
    if (DT)
      DT->recalculate(F);
    if (PDT)
      PDT->recalculate(F);
    return;
  }

  // There is little performance gain if we pend the recalculation under
  // Lazy UpdateStrategy so we recalculate available trees immediately.

  // Prevent forceFlushDeletedBB() from erasing DomTree or PostDomTree nodes.
  IsRecalculatingDomTree = IsRecalculatingPostDomTree = true;

  // Because all trees are going to be up-to-date after recalculation,
  // flush awaiting deleted BasicBlocks.
  derived().forceFlushDeletedBB();
  if (DT)
    DT->recalculate(F);
  if (PDT)
    PDT->recalculate(F);

  // Resume forceFlushDeletedBB() to erase DomTree or PostDomTree nodes.
  IsRecalculatingDomTree = IsRecalculatingPostDomTree = false;
  PendDTUpdateIndex = PendPDTUpdateIndex = PendUpdates.size();
  dropOutOfDateUpdates();
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
void GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::applyUpdates(
    ArrayRef<typename DomTreeT::UpdateType> Updates) {
  if (!DT && !PDT)
    return;

  if (Strategy == UpdateStrategy::Lazy) {
    PendUpdates.reserve(PendUpdates.size() + Updates.size());
    for (const auto &U : Updates)
      if (!isSelfDominance(U))
        PendUpdates.push_back(U);

    return;
  }

  if (DT)
    DT->applyUpdates(Updates);
  if (PDT)
    PDT->applyUpdates(Updates);
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
void GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::
    applyUpdatesPermissive(ArrayRef<typename DomTreeT::UpdateType> Updates) {
  if (!DT && !PDT)
    return;

  SmallSet<std::pair<BasicBlockT *, BasicBlockT *>, 8> Seen;
  SmallVector<typename DomTreeT::UpdateType, 8> DeduplicatedUpdates;
  for (const auto &U : Updates) {
    auto Edge = std::make_pair(U.getFrom(), U.getTo());
    // Because it is illegal to submit updates that have already been applied
    // and updates to an edge need to be strictly ordered,
    // it is safe to infer the existence of an edge from the first update
    // to this edge.
    // If the first update to an edge is "Delete", it means that the edge
    // existed before. If the first update to an edge is "Insert", it means
    // that the edge didn't exist before.
    //
    // For example, if the user submits {{Delete, A, B}, {Insert, A, B}},
    // because
    // 1. it is illegal to submit updates that have already been applied,
    // i.e., user cannot delete an nonexistent edge,
    // 2. updates to an edge need to be strictly ordered,
    // So, initially edge A -> B existed.
    // We can then safely ignore future updates to this edge and directly
    // inspect the current CFG:
    // a. If the edge still exists, because the user cannot insert an existent
    // edge, so both {Delete, A, B}, {Insert, A, B} actually happened and
    // resulted in a no-op. DTU won't submit any update in this case.
    // b. If the edge doesn't exist, we can then infer that {Delete, A, B}
    // actually happened but {Insert, A, B} was an invalid update which never
    // happened. DTU will submit {Delete, A, B} in this case.
    if (!isSelfDominance(U) && Seen.count(Edge) == 0) {
      Seen.insert(Edge);
      // If the update doesn't appear in the CFG, it means that
      // either the change isn't made or relevant operations
      // result in a no-op.
      if (isUpdateValid(U)) {
        if (isLazy())
          PendUpdates.push_back(U);
        else
          DeduplicatedUpdates.push_back(U);
      }
    }
  }

  if (Strategy == UpdateStrategy::Lazy)
    return;

  if (DT)
    DT->applyUpdates(DeduplicatedUpdates);
  if (PDT)
    PDT->applyUpdates(DeduplicatedUpdates);
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
DomTreeT &
GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::getDomTree() {
  assert(DT && "Invalid acquisition of a null DomTree");
  applyDomTreeUpdates();
  dropOutOfDateUpdates();
  return *DT;
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
PostDomTreeT &
GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::getPostDomTree() {
  assert(PDT && "Invalid acquisition of a null PostDomTree");
  applyPostDomTreeUpdates();
  dropOutOfDateUpdates();
  return *PDT;
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
LLVM_DUMP_METHOD void
GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::dump() const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  raw_ostream &OS = llvm::dbgs();

  OS << "Available Trees: ";
  if (DT || PDT) {
    if (DT)
      OS << "DomTree ";
    if (PDT)
      OS << "PostDomTree ";
    OS << "\n";
  } else
    OS << "None\n";

  OS << "UpdateStrategy: ";
  if (Strategy == UpdateStrategy::Eager) {
    OS << "Eager\n";
    return;
  } else
    OS << "Lazy\n";
  int Index = 0;

  auto printUpdates =
      [&](typename ArrayRef<typename DomTreeT::UpdateType>::const_iterator
              begin,
          typename ArrayRef<typename DomTreeT::UpdateType>::const_iterator
              end) {
        if (begin == end)
          OS << "  None\n";
        Index = 0;
        for (auto It = begin, ItEnd = end; It != ItEnd; ++It) {
          auto U = *It;
          OS << "  " << Index << " : ";
          ++Index;
          if (U.getKind() == DomTreeT::Insert)
            OS << "Insert, ";
          else
            OS << "Delete, ";
          BasicBlockT *From = U.getFrom();
          if (From) {
            auto S = From->getName();
            if (!From->hasName())
              S = "(no name)";
            OS << S << "(" << From << "), ";
          } else {
            OS << "(badref), ";
          }
          BasicBlockT *To = U.getTo();
          if (To) {
            auto S = To->getName();
            if (!To->hasName())
              S = "(no_name)";
            OS << S << "(" << To << ")\n";
          } else {
            OS << "(badref)\n";
          }
        }
      };

  if (DT) {
    const auto I = PendUpdates.begin() + PendDTUpdateIndex;
    assert(PendUpdates.begin() <= I && I <= PendUpdates.end() &&
           "Iterator out of range.");
    OS << "Applied but not cleared DomTreeUpdates:\n";
    printUpdates(PendUpdates.begin(), I);
    OS << "Pending DomTreeUpdates:\n";
    printUpdates(I, PendUpdates.end());
  }

  if (PDT) {
    const auto I = PendUpdates.begin() + PendPDTUpdateIndex;
    assert(PendUpdates.begin() <= I && I <= PendUpdates.end() &&
           "Iterator out of range.");
    OS << "Applied but not cleared PostDomTreeUpdates:\n";
    printUpdates(PendUpdates.begin(), I);
    OS << "Pending PostDomTreeUpdates:\n";
    printUpdates(I, PendUpdates.end());
  }

  OS << "Pending DeletedBBs:\n";
  Index = 0;
  for (const auto *BB : DeletedBBs) {
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

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
void GenericDomTreeUpdater<DerivedT, DomTreeT,
                           PostDomTreeT>::applyDomTreeUpdates() {
  // No pending DomTreeUpdates.
  if (Strategy != UpdateStrategy::Lazy || !DT)
    return;

  // Only apply updates not are applied by DomTree.
  if (hasPendingDomTreeUpdates()) {
    const auto I = PendUpdates.begin() + PendDTUpdateIndex;
    const auto E = PendUpdates.end();
    assert(I < E && "Iterator range invalid; there should be DomTree updates.");
    DT->applyUpdates(ArrayRef<typename DomTreeT::UpdateType>(I, E));
    PendDTUpdateIndex = PendUpdates.size();
  }
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
void GenericDomTreeUpdater<DerivedT, DomTreeT,
                           PostDomTreeT>::applyPostDomTreeUpdates() {
  // No pending PostDomTreeUpdates.
  if (Strategy != UpdateStrategy::Lazy || !PDT)
    return;

  // Only apply updates not are applied by PostDomTree.
  if (hasPendingPostDomTreeUpdates()) {
    const auto I = PendUpdates.begin() + PendPDTUpdateIndex;
    const auto E = PendUpdates.end();
    assert(I < E &&
           "Iterator range invalid; there should be PostDomTree updates.");
    PDT->applyUpdates(ArrayRef<typename DomTreeT::UpdateType>(I, E));
    PendPDTUpdateIndex = PendUpdates.size();
  }
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
bool GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::isUpdateValid(
    typename DomTreeT::UpdateType Update) const {
  const auto *From = Update.getFrom();
  const auto *To = Update.getTo();
  const auto Kind = Update.getKind();

  // Discard updates by inspecting the current state of successors of From.
  // Since isUpdateValid() must be called *after* the Terminator of From is
  // altered we can determine if the update is unnecessary for batch updates
  // or invalid for a single update.
  const bool HasEdge = llvm::is_contained(successors(From), To);

  // If the IR does not match the update,
  // 1. In batch updates, this update is unnecessary.
  // 2. When called by insertEdge*()/deleteEdge*(), this update is invalid.
  // Edge does not exist in IR.
  if (Kind == DomTreeT::Insert && !HasEdge)
    return false;

  // Edge exists in IR.
  if (Kind == DomTreeT::Delete && HasEdge)
    return false;

  return true;
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
void GenericDomTreeUpdater<DerivedT, DomTreeT, PostDomTreeT>::eraseDelBBNode(
    BasicBlockT *DelBB) {
  if (DT && !IsRecalculatingDomTree)
    if (DT->getNode(DelBB))
      DT->eraseNode(DelBB);

  if (PDT && !IsRecalculatingPostDomTree)
    if (PDT->getNode(DelBB))
      PDT->eraseNode(DelBB);
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
void GenericDomTreeUpdater<DerivedT, DomTreeT,
                           PostDomTreeT>::tryFlushDeletedBB() {
  if (!hasPendingUpdates())
    derived().forceFlushDeletedBB();
}

template <typename DerivedT, typename DomTreeT, typename PostDomTreeT>
void GenericDomTreeUpdater<DerivedT, DomTreeT,
                           PostDomTreeT>::dropOutOfDateUpdates() {
  if (Strategy == UpdateStrategy::Eager)
    return;

  tryFlushDeletedBB();

  // Drop all updates applied by both trees.
  if (!DT)
    PendDTUpdateIndex = PendUpdates.size();
  if (!PDT)
    PendPDTUpdateIndex = PendUpdates.size();

  const size_t dropIndex = std::min(PendDTUpdateIndex, PendPDTUpdateIndex);
  const auto B = PendUpdates.begin();
  const auto E = PendUpdates.begin() + dropIndex;
  assert(B <= E && "Iterator out of range.");
  PendUpdates.erase(B, E);
  // Calculate current index.
  PendDTUpdateIndex -= dropIndex;
  PendPDTUpdateIndex -= dropIndex;
}

} // namespace llvm

#endif // LLVM_ANALYSIS_GENERICDOMTREEUPDATERIMPL_H
