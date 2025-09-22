//===- llvm/CodeGen/MachinePostDominators.h ----------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information for
// target-specific code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEPOSTDOMINATORS_H
#define LLVM_CODEGEN_MACHINEPOSTDOMINATORS_H

#include "llvm/CodeGen/MachineDominators.h"

namespace llvm {

extern template class DominatorTreeBase<MachineBasicBlock, true>; // PostDomTree

namespace DomTreeBuilder {
using MBBPostDomTree = PostDomTreeBase<MachineBasicBlock>;
using MBBPostDomTreeGraphDiff = GraphDiff<MachineBasicBlock *, true>;

extern template void Calculate<MBBPostDomTree>(MBBPostDomTree &DT);
extern template void InsertEdge<MBBPostDomTree>(MBBPostDomTree &DT,
                                                MachineBasicBlock *From,
                                                MachineBasicBlock *To);
extern template void DeleteEdge<MBBPostDomTree>(MBBPostDomTree &DT,
                                                MachineBasicBlock *From,
                                                MachineBasicBlock *To);
extern template void ApplyUpdates<MBBPostDomTree>(MBBPostDomTree &DT,
                                                  MBBPostDomTreeGraphDiff &,
                                                  MBBPostDomTreeGraphDiff *);
extern template bool
Verify<MBBPostDomTree>(const MBBPostDomTree &DT,
                       MBBPostDomTree::VerificationLevel VL);
} // namespace DomTreeBuilder

///
/// MachinePostDominatorTree - an analysis pass wrapper for DominatorTree
/// used to compute the post-dominator tree for MachineFunctions.
///
class MachinePostDominatorTree : public PostDomTreeBase<MachineBasicBlock> {
  using Base = PostDomTreeBase<MachineBasicBlock>;

public:
  MachinePostDominatorTree() = default;

  explicit MachinePostDominatorTree(MachineFunction &MF) { recalculate(MF); }

  /// Handle invalidation explicitly.
  bool invalidate(MachineFunction &, const PreservedAnalyses &PA,
                  MachineFunctionAnalysisManager::Invalidator &);

  /// Make findNearestCommonDominator(const NodeT *A, const NodeT *B) available.
  using Base::findNearestCommonDominator;

  /// Returns the nearest common dominator of the given blocks.
  /// If that tree node is a virtual root, a nullptr will be returned.
  MachineBasicBlock *
  findNearestCommonDominator(ArrayRef<MachineBasicBlock *> Blocks) const;
};

class MachinePostDominatorTreeAnalysis
    : public AnalysisInfoMixin<MachinePostDominatorTreeAnalysis> {
  friend AnalysisInfoMixin<MachinePostDominatorTreeAnalysis>;

  static AnalysisKey Key;

public:
  using Result = MachinePostDominatorTree;

  Result run(MachineFunction &MF, MachineFunctionAnalysisManager &MFAM);
};

class MachinePostDominatorTreePrinterPass
    : public PassInfoMixin<MachinePostDominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  explicit MachinePostDominatorTreePrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
  static bool isRequired() { return true; }
};

class MachinePostDominatorTreeWrapperPass : public MachineFunctionPass {
  std::optional<MachinePostDominatorTree> PDT;

public:
  static char ID;

  MachinePostDominatorTreeWrapperPass();

  MachinePostDominatorTree &getPostDomTree() { return *PDT; }
  const MachinePostDominatorTree &getPostDomTree() const { return *PDT; }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override { PDT.reset(); }
  void verifyAnalysis() const override;
  void print(llvm::raw_ostream &OS, const Module *M = nullptr) const override;
};
} //end of namespace llvm

#endif
