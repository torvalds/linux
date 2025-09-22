//==- llvm/CodeGen/MachineDominators.h - Machine Dom Calculation -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes mirroring those in llvm/Analysis/Dominators.h,
// but for target-specific code rather than target-independent IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEDOMINATORS_H
#define LLVM_CODEGEN_MACHINEDOMINATORS_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Support/GenericDomTree.h"
#include <cassert>
#include <memory>
#include <optional>

namespace llvm {
class AnalysisUsage;
class MachineFunction;
class Module;
class raw_ostream;

template <>
inline void DominatorTreeBase<MachineBasicBlock, false>::addRoot(
    MachineBasicBlock *MBB) {
  this->Roots.push_back(MBB);
}

extern template class DomTreeNodeBase<MachineBasicBlock>;
extern template class DominatorTreeBase<MachineBasicBlock, false>; // DomTree

using MachineDomTreeNode = DomTreeNodeBase<MachineBasicBlock>;

namespace DomTreeBuilder {
using MBBDomTree = DomTreeBase<MachineBasicBlock>;
using MBBUpdates = ArrayRef<llvm::cfg::Update<MachineBasicBlock *>>;
using MBBDomTreeGraphDiff = GraphDiff<MachineBasicBlock *, false>;

extern template void Calculate<MBBDomTree>(MBBDomTree &DT);
extern template void CalculateWithUpdates<MBBDomTree>(MBBDomTree &DT,
                                                      MBBUpdates U);

extern template void InsertEdge<MBBDomTree>(MBBDomTree &DT,
                                            MachineBasicBlock *From,
                                            MachineBasicBlock *To);

extern template void DeleteEdge<MBBDomTree>(MBBDomTree &DT,
                                            MachineBasicBlock *From,
                                            MachineBasicBlock *To);

extern template void ApplyUpdates<MBBDomTree>(MBBDomTree &DT,
                                              MBBDomTreeGraphDiff &,
                                              MBBDomTreeGraphDiff *);

extern template bool Verify<MBBDomTree>(const MBBDomTree &DT,
                                        MBBDomTree::VerificationLevel VL);
} // namespace DomTreeBuilder

//===-------------------------------------
/// DominatorTree Class - Concrete subclass of DominatorTreeBase that is used to
/// compute a normal dominator tree.
///
class MachineDominatorTree : public DomTreeBase<MachineBasicBlock> {
  /// Helper structure used to hold all the basic blocks
  /// involved in the split of a critical edge.
  struct CriticalEdge {
    MachineBasicBlock *FromBB;
    MachineBasicBlock *ToBB;
    MachineBasicBlock *NewBB;
  };

  /// Pile up all the critical edges to be split.
  /// The splitting of a critical edge is local and thus, it is possible
  /// to apply several of those changes at the same time.
  mutable SmallVector<CriticalEdge, 32> CriticalEdgesToSplit;

  /// Remember all the basic blocks that are inserted during
  /// edge splitting.
  /// Invariant: NewBBs == all the basic blocks contained in the NewBB
  /// field of all the elements of CriticalEdgesToSplit.
  /// I.e., forall elt in CriticalEdgesToSplit, it exists BB in NewBBs
  /// such as BB == elt.NewBB.
  mutable SmallSet<MachineBasicBlock *, 32> NewBBs;

  /// Apply all the recorded critical edges to the DT.
  /// This updates the underlying DT information in a way that uses
  /// the fast query path of DT as much as possible.
  /// FIXME: This method should not be a const member!
  ///
  /// \post CriticalEdgesToSplit.empty().
  void applySplitCriticalEdges() const;

public:
  using Base = DomTreeBase<MachineBasicBlock>;

  MachineDominatorTree() = default;
  explicit MachineDominatorTree(MachineFunction &MF) { calculate(MF); }

  /// Handle invalidation explicitly.
  bool invalidate(MachineFunction &, const PreservedAnalyses &PA,
                  MachineFunctionAnalysisManager::Invalidator &);

  // FIXME: If there is an updater for MachineDominatorTree,
  // migrate to this updater and remove these wrappers.

  MachineDominatorTree &getBase() {
    applySplitCriticalEdges();
    return *this;
  }

  MachineBasicBlock *getRoot() const {
    applySplitCriticalEdges();
    return Base::getRoot();
  }

  MachineDomTreeNode *getRootNode() const {
    applySplitCriticalEdges();
    return const_cast<MachineDomTreeNode *>(Base::getRootNode());
  }

  void calculate(MachineFunction &F);

  bool dominates(const MachineDomTreeNode *A,
                 const MachineDomTreeNode *B) const {
    applySplitCriticalEdges();
    return Base::dominates(A, B);
  }

  void getDescendants(MachineBasicBlock *A,
                      SmallVectorImpl<MachineBasicBlock *> &Result) {
    applySplitCriticalEdges();
    Base::getDescendants(A, Result);
  }

  bool dominates(const MachineBasicBlock *A, const MachineBasicBlock *B) const {
    applySplitCriticalEdges();
    return Base::dominates(A, B);
  }

  // dominates - Return true if A dominates B. This performs the
  // special checks necessary if A and B are in the same basic block.
  bool dominates(const MachineInstr *A, const MachineInstr *B) const {
    applySplitCriticalEdges();
    const MachineBasicBlock *BBA = A->getParent(), *BBB = B->getParent();
    if (BBA != BBB)
      return Base::dominates(BBA, BBB);

    // Loop through the basic block until we find A or B.
    MachineBasicBlock::const_iterator I = BBA->begin();
    for (; &*I != A && &*I != B; ++I)
      /*empty*/ ;

    return &*I == A;
  }

  bool properlyDominates(const MachineDomTreeNode *A,
                         const MachineDomTreeNode *B) const {
    applySplitCriticalEdges();
    return Base::properlyDominates(A, B);
  }

  bool properlyDominates(const MachineBasicBlock *A,
                         const MachineBasicBlock *B) const {
    applySplitCriticalEdges();
    return Base::properlyDominates(A, B);
  }

  /// findNearestCommonDominator - Find nearest common dominator basic block
  /// for basic block A and B. If there is no such block then return NULL.
  MachineBasicBlock *findNearestCommonDominator(MachineBasicBlock *A,
                                                MachineBasicBlock *B) {
    applySplitCriticalEdges();
    return Base::findNearestCommonDominator(A, B);
  }

  MachineDomTreeNode *operator[](MachineBasicBlock *BB) const {
    applySplitCriticalEdges();
    return Base::getNode(BB);
  }

  /// getNode - return the (Post)DominatorTree node for the specified basic
  /// block.  This is the same as using operator[] on this class.
  ///
  MachineDomTreeNode *getNode(MachineBasicBlock *BB) const {
    applySplitCriticalEdges();
    return Base::getNode(BB);
  }

  /// addNewBlock - Add a new node to the dominator tree information.  This
  /// creates a new node as a child of DomBB dominator node,linking it into
  /// the children list of the immediate dominator.
  MachineDomTreeNode *addNewBlock(MachineBasicBlock *BB,
                                  MachineBasicBlock *DomBB) {
    applySplitCriticalEdges();
    return Base::addNewBlock(BB, DomBB);
  }

  /// changeImmediateDominator - This method is used to update the dominator
  /// tree information when a node's immediate dominator changes.
  ///
  void changeImmediateDominator(MachineBasicBlock *N,
                                MachineBasicBlock *NewIDom) {
    applySplitCriticalEdges();
    Base::changeImmediateDominator(N, NewIDom);
  }

  void changeImmediateDominator(MachineDomTreeNode *N,
                                MachineDomTreeNode *NewIDom) {
    applySplitCriticalEdges();
    Base::changeImmediateDominator(N, NewIDom);
  }

  /// eraseNode - Removes a node from  the dominator tree. Block must not
  /// dominate any other blocks. Removes node from its immediate dominator's
  /// children list. Deletes dominator node associated with basic block BB.
  void eraseNode(MachineBasicBlock *BB) {
    applySplitCriticalEdges();
    Base::eraseNode(BB);
  }

  /// splitBlock - BB is split and now it has one successor. Update dominator
  /// tree to reflect this change.
  void splitBlock(MachineBasicBlock* NewBB) {
    applySplitCriticalEdges();
    Base::splitBlock(NewBB);
  }

  /// isReachableFromEntry - Return true if A is dominated by the entry
  /// block of the function containing it.
  bool isReachableFromEntry(const MachineBasicBlock *A) {
    applySplitCriticalEdges();
    return Base::isReachableFromEntry(A);
  }

  /// Record that the critical edge (FromBB, ToBB) has been
  /// split with NewBB.
  /// This is best to use this method instead of directly update the
  /// underlying information, because this helps mitigating the
  /// number of time the DT information is invalidated.
  ///
  /// \note Do not use this method with regular edges.
  ///
  /// \note To benefit from the compile time improvement incurred by this
  /// method, the users of this method have to limit the queries to the DT
  /// interface between two edges splitting. In other words, they have to
  /// pack the splitting of critical edges as much as possible.
  void recordSplitCriticalEdge(MachineBasicBlock *FromBB,
                              MachineBasicBlock *ToBB,
                              MachineBasicBlock *NewBB) {
    bool Inserted = NewBBs.insert(NewBB).second;
    (void)Inserted;
    assert(Inserted &&
           "A basic block inserted via edge splitting cannot appear twice");
    CriticalEdgesToSplit.push_back({FromBB, ToBB, NewBB});
  }
};

/// \brief Analysis pass which computes a \c MachineDominatorTree.
class MachineDominatorTreeAnalysis
    : public AnalysisInfoMixin<MachineDominatorTreeAnalysis> {
  friend AnalysisInfoMixin<MachineDominatorTreeAnalysis>;

  static AnalysisKey Key;

public:
  using Result = MachineDominatorTree;

  Result run(MachineFunction &MF, MachineFunctionAnalysisManager &);
};

/// \brief Machine function pass which print \c MachineDominatorTree.
class MachineDominatorTreePrinterPass
    : public PassInfoMixin<MachineDominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  explicit MachineDominatorTreePrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
  static bool isRequired() { return true; }
};

/// \brief Analysis pass which computes a \c MachineDominatorTree.
class MachineDominatorTreeWrapperPass : public MachineFunctionPass {
  // MachineFunctionPass may verify the analysis result without running pass,
  // e.g. when `F.hasAvailableExternallyLinkage` is true.
  std::optional<MachineDominatorTree> DT;

public:
  static char ID;

  MachineDominatorTreeWrapperPass();

  MachineDominatorTree &getDomTree() { return *DT; }
  const MachineDominatorTree &getDomTree() const { return *DT; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void verifyAnalysis() const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  void releaseMemory() override;

  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};

//===-------------------------------------
/// DominatorTree GraphTraits specialization so the DominatorTree can be
/// iterable by generic graph iterators.
///

template <class Node, class ChildIterator>
struct MachineDomTreeGraphTraitsBase {
  using NodeRef = Node *;
  using ChildIteratorType = ChildIterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

template <class T> struct GraphTraits;

template <>
struct GraphTraits<MachineDomTreeNode *>
    : public MachineDomTreeGraphTraitsBase<MachineDomTreeNode,
                                           MachineDomTreeNode::const_iterator> {
};

template <>
struct GraphTraits<const MachineDomTreeNode *>
    : public MachineDomTreeGraphTraitsBase<const MachineDomTreeNode,
                                           MachineDomTreeNode::const_iterator> {
};

template <> struct GraphTraits<MachineDominatorTree*>
  : public GraphTraits<MachineDomTreeNode *> {
  static NodeRef getEntryNode(MachineDominatorTree *DT) {
    return DT->getRootNode();
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEDOMINATORS_H
