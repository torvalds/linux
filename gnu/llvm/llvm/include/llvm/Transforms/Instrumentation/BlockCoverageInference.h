//===-- BlockCoverageInference.h - Minimal Execution Coverage ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file finds the minimum set of blocks on a CFG that must be instrumented
/// to infer execution coverage for the whole graph.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BLOCKCOVERAGEINFERENCE_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BLOCKCOVERAGEINFERENCE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Function;
class BasicBlock;
class DotFuncBCIInfo;

class BlockCoverageInference {
  friend class DotFuncBCIInfo;

public:
  using BlockSet = SmallSetVector<const BasicBlock *, 4>;

  BlockCoverageInference(const Function &F, bool ForceInstrumentEntry);

  /// \return true if \p BB should be instrumented for coverage.
  bool shouldInstrumentBlock(const BasicBlock &BB) const;

  /// \return the set of blocks \p Deps such that \p BB is covered iff any
  /// blocks in \p Deps are covered.
  BlockSet getDependencies(const BasicBlock &BB) const;

  /// \return a hash that depends on the set of instrumented blocks.
  uint64_t getInstrumentedBlocksHash() const;

  /// Dump the inference graph.
  void dump(raw_ostream &OS) const;

  /// View the inferred block coverage as a dot file.
  /// Filled gray blocks are instrumented, red outlined blocks are found to be
  /// covered, red edges show that a block's coverage can be inferred from its
  /// successors, and blue edges show that a block's coverage can be inferred
  /// from its predecessors.
  void viewBlockCoverageGraph(
      const DenseMap<const BasicBlock *, bool> *Coverage = nullptr) const;

private:
  const Function &F;
  bool ForceInstrumentEntry;

  /// Maps blocks to a minimal list of predecessors that can be used to infer
  /// this block's coverage.
  DenseMap<const BasicBlock *, BlockSet> PredecessorDependencies;

  /// Maps blocks to a minimal list of successors that can be used to infer
  /// this block's coverage.
  DenseMap<const BasicBlock *, BlockSet> SuccessorDependencies;

  /// Compute \p PredecessorDependencies and \p SuccessorDependencies.
  void findDependencies();

  /// Find the set of basic blocks that are reachable from \p Start without the
  /// basic block \p Avoid.
  void getReachableAvoiding(const BasicBlock &Start, const BasicBlock &Avoid,
                            bool IsForward, BlockSet &Reachable) const;

  static std::string getBlockNames(ArrayRef<const BasicBlock *> BBs);
  static std::string getBlockNames(BlockSet BBs) {
    return getBlockNames(ArrayRef<const BasicBlock *>(BBs.begin(), BBs.end()));
  }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_BLOCKCOVERAGEINFERENCE_H
