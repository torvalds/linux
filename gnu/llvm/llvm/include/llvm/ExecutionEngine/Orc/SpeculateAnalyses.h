//===-- SpeculateAnalyses.h  --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Contains the Analyses and Result Interpretation to select likely functions
/// to Speculatively compile before they are called. [Purely Experimentation]
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SPECULATEANALYSES_H
#define LLVM_EXECUTIONENGINE_ORC_SPECULATEANALYSES_H

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Speculation.h"

namespace llvm {

namespace orc {

// Provides common code.
class SpeculateQuery {
protected:
  void findCalles(const BasicBlock *, DenseSet<StringRef> &);
  bool isStraightLine(const Function &F);

public:
  using ResultTy = std::optional<DenseMap<StringRef, DenseSet<StringRef>>>;
};

// Direct calls in high frequency basic blocks are extracted.
class BlockFreqQuery : public SpeculateQuery {
  size_t numBBToGet(size_t);

public:
  // Find likely next executables based on IR Block Frequency
  ResultTy operator()(Function &F);
};

// This Query generates a sequence of basic blocks which follows the order of
// execution.
// A handful of BB with higher block frequencies are taken, then path to entry
// and end BB are discovered by traversing up & down the CFG.
class SequenceBBQuery : public SpeculateQuery {
  struct WalkDirection {
    bool Upward = true, Downward = true;
    // the block associated contain a call
    bool CallerBlock = false;
  };

public:
  using VisitedBlocksInfoTy = DenseMap<const BasicBlock *, WalkDirection>;
  using BlockListTy = SmallVector<const BasicBlock *, 8>;
  using BackEdgesInfoTy =
      SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8>;
  using BlockFreqInfoTy =
      SmallVector<std::pair<const BasicBlock *, uint64_t>, 8>;

private:
  std::size_t getHottestBlocks(std::size_t TotalBlocks);
  BlockListTy rearrangeBB(const Function &, const BlockListTy &);
  BlockListTy queryCFG(Function &, const BlockListTy &);
  void traverseToEntryBlock(const BasicBlock *, const BlockListTy &,
                            const BackEdgesInfoTy &,
                            const BranchProbabilityInfo *,
                            VisitedBlocksInfoTy &);
  void traverseToExitBlock(const BasicBlock *, const BlockListTy &,
                           const BackEdgesInfoTy &,
                           const BranchProbabilityInfo *,
                           VisitedBlocksInfoTy &);

public:
  ResultTy operator()(Function &F);
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SPECULATEANALYSES_H
