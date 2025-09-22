//===- PostOrderCFGView.h - Post order view of CFG blocks -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements post order view of the blocks in a CFG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_POSTORDERCFGVIEW_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_POSTORDERCFGVIEW_H

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include <utility>
#include <vector>

namespace clang {

class PostOrderCFGView : public ManagedAnalysis {
  virtual void anchor();

public:
  /// Implements a set of CFGBlocks using a BitVector.
  ///
  /// This class contains a minimal interface, primarily dictated by the SetType
  /// template parameter of the llvm::po_iterator template, as used with
  /// external storage. We also use this set to keep track of which CFGBlocks we
  /// visit during the analysis.
  class CFGBlockSet {
    llvm::BitVector VisitedBlockIDs;

  public:
    // po_iterator requires this iterator, but the only interface needed is the
    // value_type type.
    struct iterator { using value_type = const CFGBlock *; };

    CFGBlockSet() = default;
    CFGBlockSet(const CFG *G) : VisitedBlockIDs(G->getNumBlockIDs(), false) {}

    /// Set the bit associated with a particular CFGBlock.
    /// This is the important method for the SetType template parameter.
    std::pair<std::nullopt_t, bool> insert(const CFGBlock *Block) {
      // Note that insert() is called by po_iterator, which doesn't check to
      // make sure that Block is non-null.  Moreover, the CFGBlock iterator will
      // occasionally hand out null pointers for pruned edges, so we catch those
      // here.
      if (!Block)
        return std::make_pair(std::nullopt,
                              false); // if an edge is trivially false.
      if (VisitedBlockIDs.test(Block->getBlockID()))
        return std::make_pair(std::nullopt, false);
      VisitedBlockIDs.set(Block->getBlockID());
      return std::make_pair(std::nullopt, true);
    }

    /// Check if the bit for a CFGBlock has been already set.
    /// This method is for tracking visited blocks in the main threadsafety
    /// loop. Block must not be null.
    bool alreadySet(const CFGBlock *Block) {
      return VisitedBlockIDs.test(Block->getBlockID());
    }
  };

private:
  // The CFG orders the blocks of loop bodies before those of loop successors
  // (both numerically, and in the successor order of the loop condition
  // block). So, RPO necessarily reverses that order, placing the loop successor
  // *before* the loop body. For many analyses, particularly those that converge
  // to a fixpoint, this results in potentially significant extra work because
  // loop successors will necessarily need to be reconsidered once the algorithm
  // has reached a fixpoint on the loop body.
  //
  // This definition of CFG graph traits reverses the order of children, so that
  // loop bodies will come first in an RPO.
  struct CFGLoopBodyFirstTraits {
    using NodeRef = const ::clang::CFGBlock *;
    using ChildIteratorType = ::clang::CFGBlock::const_succ_reverse_iterator;

    static ChildIteratorType child_begin(NodeRef N) { return N->succ_rbegin(); }
    static ChildIteratorType child_end(NodeRef N) { return N->succ_rend(); }

    using nodes_iterator = ::clang::CFG::const_iterator;

    static NodeRef getEntryNode(const ::clang::CFG *F) {
      return &F->getEntry();
    }

    static nodes_iterator nodes_begin(const ::clang::CFG *F) {
      return F->nodes_begin();
    }

    static nodes_iterator nodes_end(const ::clang::CFG *F) {
      return F->nodes_end();
    }

    static unsigned size(const ::clang::CFG *F) { return F->size(); }
  };
  using po_iterator =
      llvm::po_iterator<const CFG *, CFGBlockSet, true, CFGLoopBodyFirstTraits>;
  std::vector<const CFGBlock *> Blocks;

  using BlockOrderTy = llvm::DenseMap<const CFGBlock *, unsigned>;
  BlockOrderTy BlockOrder;

public:
  friend struct BlockOrderCompare;

  using iterator = std::vector<const CFGBlock *>::reverse_iterator;
  using const_iterator = std::vector<const CFGBlock *>::const_reverse_iterator;

  PostOrderCFGView(const CFG *cfg);

  iterator begin() { return Blocks.rbegin(); }
  iterator end() { return Blocks.rend(); }

  const_iterator begin() const { return Blocks.rbegin(); }
  const_iterator end() const { return Blocks.rend(); }

  bool empty() const { return begin() == end(); }

  struct BlockOrderCompare {
    const PostOrderCFGView &POV;

  public:
    BlockOrderCompare(const PostOrderCFGView &pov) : POV(pov) {}

    bool operator()(const CFGBlock *b1, const CFGBlock *b2) const;
  };

  BlockOrderCompare getComparator() const {
    return BlockOrderCompare(*this);
  }

  // Used by AnalyisContext to construct this object.
  static const void *getTag();

  static std::unique_ptr<PostOrderCFGView>
  create(AnalysisDeclContext &analysisContext);
};

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_POSTORDERCFGVIEW_H
