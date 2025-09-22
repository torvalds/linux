//=- llvm/Analysis/PostDominators.h - Post Dominator Calculation --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_POSTDOMINATORS_H
#define LLVM_ANALYSIS_POSTDOMINATORS_H

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

class Function;
class raw_ostream;

/// PostDominatorTree Class - Concrete subclass of DominatorTree that is used to
/// compute the post-dominator tree.
class PostDominatorTree : public PostDomTreeBase<BasicBlock> {
public:
  using Base = PostDomTreeBase<BasicBlock>;

  PostDominatorTree() = default;
  explicit PostDominatorTree(Function &F) { recalculate(F); }
  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);

  // Ensure base-class overloads are visible.
  using Base::dominates;

  /// Return true if \p I1 dominates \p I2. This checks if \p I2 comes before
  /// \p I1 if they belongs to the same basic block.
  bool dominates(const Instruction *I1, const Instruction *I2) const;
};

/// Analysis pass which computes a \c PostDominatorTree.
class PostDominatorTreeAnalysis
    : public AnalysisInfoMixin<PostDominatorTreeAnalysis> {
  friend AnalysisInfoMixin<PostDominatorTreeAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = PostDominatorTree;

  /// Run the analysis pass over a function and produce a post dominator
  ///        tree.
  PostDominatorTree run(Function &F, FunctionAnalysisManager &);
};

/// Printer pass for the \c PostDominatorTree.
class PostDominatorTreePrinterPass
    : public PassInfoMixin<PostDominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  explicit PostDominatorTreePrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

struct PostDominatorTreeWrapperPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid

  PostDominatorTree DT;

  PostDominatorTreeWrapperPass();

  PostDominatorTree &getPostDomTree() { return DT; }
  const PostDominatorTree &getPostDomTree() const { return DT; }

  bool runOnFunction(Function &F) override;

  void verifyAnalysis() const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  void releaseMemory() override { DT.reset(); }

  void print(raw_ostream &OS, const Module*) const override;
};

FunctionPass* createPostDomTree();

template <> struct GraphTraits<PostDominatorTree*>
  : public GraphTraits<DomTreeNode*> {
  static NodeRef getEntryNode(PostDominatorTree *DT) {
    return DT->getRootNode();
  }

  static nodes_iterator nodes_begin(PostDominatorTree *N) {
    return df_begin(getEntryNode(N));
  }

  static nodes_iterator nodes_end(PostDominatorTree *N) {
    return df_end(getEntryNode(N));
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_POSTDOMINATORS_H
