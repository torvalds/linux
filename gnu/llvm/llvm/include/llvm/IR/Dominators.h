//===- Dominators.h - Dominator Info Calculation ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DominatorTree class, which provides fast and efficient
// dominance queries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DOMINATORS_H
#define LLVM_IR_DOMINATORS_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_iterator.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFGDiff.h"
#include "llvm/Support/CFGUpdate.h"
#include "llvm/Support/GenericDomTree.h"
#include <algorithm>
#include <utility>

namespace llvm {

class Function;
class Instruction;
class Module;
class Value;
class raw_ostream;
template <class GraphType> struct GraphTraits;

extern template class DomTreeNodeBase<BasicBlock>;
extern template class DominatorTreeBase<BasicBlock, false>; // DomTree
extern template class DominatorTreeBase<BasicBlock, true>; // PostDomTree

extern template class cfg::Update<BasicBlock *>;

namespace DomTreeBuilder {
using BBDomTree = DomTreeBase<BasicBlock>;
using BBPostDomTree = PostDomTreeBase<BasicBlock>;

using BBUpdates = ArrayRef<llvm::cfg::Update<BasicBlock *>>;

using BBDomTreeGraphDiff = GraphDiff<BasicBlock *, false>;
using BBPostDomTreeGraphDiff = GraphDiff<BasicBlock *, true>;

extern template void Calculate<BBDomTree>(BBDomTree &DT);
extern template void CalculateWithUpdates<BBDomTree>(BBDomTree &DT,
                                                     BBUpdates U);

extern template void Calculate<BBPostDomTree>(BBPostDomTree &DT);

extern template void InsertEdge<BBDomTree>(BBDomTree &DT, BasicBlock *From,
                                           BasicBlock *To);
extern template void InsertEdge<BBPostDomTree>(BBPostDomTree &DT,
                                               BasicBlock *From,
                                               BasicBlock *To);

extern template void DeleteEdge<BBDomTree>(BBDomTree &DT, BasicBlock *From,
                                           BasicBlock *To);
extern template void DeleteEdge<BBPostDomTree>(BBPostDomTree &DT,
                                               BasicBlock *From,
                                               BasicBlock *To);

extern template void ApplyUpdates<BBDomTree>(BBDomTree &DT,
                                             BBDomTreeGraphDiff &,
                                             BBDomTreeGraphDiff *);
extern template void ApplyUpdates<BBPostDomTree>(BBPostDomTree &DT,
                                                 BBPostDomTreeGraphDiff &,
                                                 BBPostDomTreeGraphDiff *);

extern template bool Verify<BBDomTree>(const BBDomTree &DT,
                                       BBDomTree::VerificationLevel VL);
extern template bool Verify<BBPostDomTree>(const BBPostDomTree &DT,
                                           BBPostDomTree::VerificationLevel VL);
}  // namespace DomTreeBuilder

using DomTreeNode = DomTreeNodeBase<BasicBlock>;

class BasicBlockEdge {
  const BasicBlock *Start;
  const BasicBlock *End;

public:
  BasicBlockEdge(const BasicBlock *Start_, const BasicBlock *End_) :
    Start(Start_), End(End_) {}

  BasicBlockEdge(const std::pair<BasicBlock *, BasicBlock *> &Pair)
      : Start(Pair.first), End(Pair.second) {}

  BasicBlockEdge(const std::pair<const BasicBlock *, const BasicBlock *> &Pair)
      : Start(Pair.first), End(Pair.second) {}

  const BasicBlock *getStart() const {
    return Start;
  }

  const BasicBlock *getEnd() const {
    return End;
  }

  /// Check if this is the only edge between Start and End.
  bool isSingleEdge() const;
};

template <> struct DenseMapInfo<BasicBlockEdge> {
  using BBInfo = DenseMapInfo<const BasicBlock *>;

  static unsigned getHashValue(const BasicBlockEdge *V);

  static inline BasicBlockEdge getEmptyKey() {
    return BasicBlockEdge(BBInfo::getEmptyKey(), BBInfo::getEmptyKey());
  }

  static inline BasicBlockEdge getTombstoneKey() {
    return BasicBlockEdge(BBInfo::getTombstoneKey(), BBInfo::getTombstoneKey());
  }

  static unsigned getHashValue(const BasicBlockEdge &Edge) {
    return hash_combine(BBInfo::getHashValue(Edge.getStart()),
                        BBInfo::getHashValue(Edge.getEnd()));
  }

  static bool isEqual(const BasicBlockEdge &LHS, const BasicBlockEdge &RHS) {
    return BBInfo::isEqual(LHS.getStart(), RHS.getStart()) &&
           BBInfo::isEqual(LHS.getEnd(), RHS.getEnd());
  }
};

/// Concrete subclass of DominatorTreeBase that is used to compute a
/// normal dominator tree.
///
/// Definition: A block is said to be forward statically reachable if there is
/// a path from the entry of the function to the block.  A statically reachable
/// block may become statically unreachable during optimization.
///
/// A forward unreachable block may appear in the dominator tree, or it may
/// not.  If it does, dominance queries will return results as if all reachable
/// blocks dominate it.  When asking for a Node corresponding to a potentially
/// unreachable block, calling code must handle the case where the block was
/// unreachable and the result of getNode() is nullptr.
///
/// Generally, a block known to be unreachable when the dominator tree is
/// constructed will not be in the tree.  One which becomes unreachable after
/// the dominator tree is initially constructed may still exist in the tree,
/// even if the tree is properly updated. Calling code should not rely on the
/// preceding statements; this is stated only to assist human understanding.
class DominatorTree : public DominatorTreeBase<BasicBlock, false> {
 public:
  using Base = DominatorTreeBase<BasicBlock, false>;

  DominatorTree() = default;
  explicit DominatorTree(Function &F) { recalculate(F); }
  explicit DominatorTree(DominatorTree &DT, DomTreeBuilder::BBUpdates U) {
    recalculate(*DT.Parent, U);
  }

  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);

  // Ensure base-class overloads are visible.
  using Base::dominates;

  /// Return true if the (end of the) basic block BB dominates the use U.
  bool dominates(const BasicBlock *BB, const Use &U) const;

  /// Return true if value Def dominates use U, in the sense that Def is
  /// available at U, and could be substituted as the used value without
  /// violating the SSA dominance requirement.
  ///
  /// In particular, it is worth noting that:
  ///  * Non-instruction Defs dominate everything.
  ///  * Def does not dominate a use in Def itself (outside of degenerate cases
  ///    like unreachable code or trivial phi cycles).
  ///  * Invoke Defs only dominate uses in their default destination.
  bool dominates(const Value *Def, const Use &U) const;

  /// Return true if value Def dominates all possible uses inside instruction
  /// User. Same comments as for the Use-based API apply.
  bool dominates(const Value *Def, const Instruction *User) const;
  bool dominates(const Value *Def, BasicBlock::iterator User) const {
    return dominates(Def, &*User);
  }

  /// Returns true if Def would dominate a use in any instruction in BB.
  /// If Def is an instruction in BB, then Def does not dominate BB.
  ///
  /// Does not accept Value to avoid ambiguity with dominance checks between
  /// two basic blocks.
  bool dominates(const Instruction *Def, const BasicBlock *BB) const;

  /// Return true if an edge dominates a use.
  ///
  /// If BBE is not a unique edge between start and end of the edge, it can
  /// never dominate the use.
  bool dominates(const BasicBlockEdge &BBE, const Use &U) const;
  bool dominates(const BasicBlockEdge &BBE, const BasicBlock *BB) const;
  /// Returns true if edge \p BBE1 dominates edge \p BBE2.
  bool dominates(const BasicBlockEdge &BBE1, const BasicBlockEdge &BBE2) const;

  // Ensure base class overloads are visible.
  using Base::isReachableFromEntry;

  /// Provide an overload for a Use.
  bool isReachableFromEntry(const Use &U) const;

  // Ensure base class overloads are visible.
  using Base::findNearestCommonDominator;

  /// Find the nearest instruction I that dominates both I1 and I2, in the sense
  /// that a result produced before I will be available at both I1 and I2.
  Instruction *findNearestCommonDominator(Instruction *I1,
                                          Instruction *I2) const;

  // Pop up a GraphViz/gv window with the Dominator Tree rendered using `dot`.
  void viewGraph(const Twine &Name, const Twine &Title);
  void viewGraph();
};

//===-------------------------------------
// DominatorTree GraphTraits specializations so the DominatorTree can be
// iterable by generic graph iterators.

template <class Node, class ChildIterator> struct DomTreeGraphTraitsBase {
  using NodeRef = Node *;
  using ChildIteratorType = ChildIterator;
  using nodes_iterator = df_iterator<Node *, df_iterator_default_set<Node*>>;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }

  static nodes_iterator nodes_begin(NodeRef N) {
    return df_begin(getEntryNode(N));
  }

  static nodes_iterator nodes_end(NodeRef N) { return df_end(getEntryNode(N)); }
};

template <>
struct GraphTraits<DomTreeNode *>
    : public DomTreeGraphTraitsBase<DomTreeNode, DomTreeNode::const_iterator> {
};

template <>
struct GraphTraits<const DomTreeNode *>
    : public DomTreeGraphTraitsBase<const DomTreeNode,
                                    DomTreeNode::const_iterator> {};

template <> struct GraphTraits<DominatorTree*>
  : public GraphTraits<DomTreeNode*> {
  static NodeRef getEntryNode(DominatorTree *DT) { return DT->getRootNode(); }

  static nodes_iterator nodes_begin(DominatorTree *N) {
    return df_begin(getEntryNode(N));
  }

  static nodes_iterator nodes_end(DominatorTree *N) {
    return df_end(getEntryNode(N));
  }
};

/// Analysis pass which computes a \c DominatorTree.
class DominatorTreeAnalysis : public AnalysisInfoMixin<DominatorTreeAnalysis> {
  friend AnalysisInfoMixin<DominatorTreeAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = DominatorTree;

  /// Run the analysis pass over a function and produce a dominator tree.
  DominatorTree run(Function &F, FunctionAnalysisManager &);
};

/// Printer pass for the \c DominatorTree.
class DominatorTreePrinterPass
    : public PassInfoMixin<DominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  explicit DominatorTreePrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

/// Verifier pass for the \c DominatorTree.
struct DominatorTreeVerifierPass : PassInfoMixin<DominatorTreeVerifierPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

/// Enables verification of dominator trees.
///
/// This check is expensive and is disabled by default.  `-verify-dom-info`
/// allows selectively enabling the check without needing to recompile.
extern bool VerifyDomInfo;

/// Legacy analysis pass which computes a \c DominatorTree.
class DominatorTreeWrapperPass : public FunctionPass {
  DominatorTree DT;

public:
  static char ID;

  DominatorTreeWrapperPass();

  DominatorTree &getDomTree() { return DT; }
  const DominatorTree &getDomTree() const { return DT; }

  bool runOnFunction(Function &F) override;

  void verifyAnalysis() const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  void releaseMemory() override { DT.reset(); }

  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};
} // end namespace llvm

#endif // LLVM_IR_DOMINATORS_H
