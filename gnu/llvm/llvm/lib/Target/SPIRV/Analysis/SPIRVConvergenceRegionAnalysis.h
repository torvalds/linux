//===- SPIRVConvergenceRegionAnalysis.h ------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The analysis determines the convergence region for each basic block of
// the module, and provides a tree-like structure describing the region
// hierarchy.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVCONVERGENCEREGIONANALYSIS_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVCONVERGENCEREGIONANALYSIS_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include <iostream>
#include <optional>
#include <unordered_set>

namespace llvm {
class SPIRVSubtarget;
class MachineFunction;
class MachineModuleInfo;

namespace SPIRV {

// Returns the first convergence intrinsic found in |BB|, |nullopt| otherwise.
std::optional<IntrinsicInst *> getConvergenceToken(BasicBlock *BB);
std::optional<const IntrinsicInst *> getConvergenceToken(const BasicBlock *BB);

// Describes a hierarchy of convergence regions.
// A convergence region defines a CFG for which the execution flow can diverge
// starting from the entry block, but should reconverge back before the end of
// the exit blocks.
class ConvergenceRegion {
  DominatorTree &DT;
  LoopInfo &LI;

public:
  // The parent region of this region, if any.
  ConvergenceRegion *Parent = nullptr;
  // The sub-regions contained in this region, if any.
  SmallVector<ConvergenceRegion *> Children = {};
  // The convergence instruction linked to this region, if any.
  std::optional<IntrinsicInst *> ConvergenceToken = std::nullopt;
  // The only block with a predecessor outside of this region.
  BasicBlock *Entry = nullptr;
  // All the blocks with an edge leaving this convergence region.
  SmallPtrSet<BasicBlock *, 2> Exits = {};
  // All the blocks that belongs to this region, including its subregions'.
  SmallPtrSet<BasicBlock *, 8> Blocks = {};

  // Creates a single convergence region encapsulating the whole function |F|.
  ConvergenceRegion(DominatorTree &DT, LoopInfo &LI, Function &F);

  // Creates a single convergence region defined by entry and exits nodes, a
  // list of blocks, and possibly a convergence token.
  ConvergenceRegion(DominatorTree &DT, LoopInfo &LI,
                    std::optional<IntrinsicInst *> ConvergenceToken,
                    BasicBlock *Entry, SmallPtrSet<BasicBlock *, 8> &&Blocks,
                    SmallPtrSet<BasicBlock *, 2> &&Exits);

  ConvergenceRegion(ConvergenceRegion &&CR)
      : DT(CR.DT), LI(CR.LI), Parent(std::move(CR.Parent)),
        Children(std::move(CR.Children)),
        ConvergenceToken(std::move(CR.ConvergenceToken)),
        Entry(std::move(CR.Entry)), Exits(std::move(CR.Exits)),
        Blocks(std::move(CR.Blocks)) {}

  ConvergenceRegion(const ConvergenceRegion &other) = delete;

  // Returns true if the given basic block belongs to this region, or to one of
  // its subregion.
  bool contains(const BasicBlock *BB) const { return Blocks.count(BB) != 0; }

  void releaseMemory();

  // Write to the debug output this region's hierarchy.
  // |IndentSize| defines the number of tabs to print before any new line.
  void dump(const unsigned IndentSize = 0) const;
};

// Holds a ConvergenceRegion hierarchy.
class ConvergenceRegionInfo {
  // The convergence region this structure holds.
  ConvergenceRegion *TopLevelRegion;

public:
  ConvergenceRegionInfo() : TopLevelRegion(nullptr) {}

  // Creates a new ConvergenceRegionInfo. Ownership of the TopLevelRegion is
  // passed to this object.
  ConvergenceRegionInfo(ConvergenceRegion *TopLevelRegion)
      : TopLevelRegion(TopLevelRegion) {}

  ~ConvergenceRegionInfo() { releaseMemory(); }

  ConvergenceRegionInfo(ConvergenceRegionInfo &&LHS)
      : TopLevelRegion(LHS.TopLevelRegion) {
    if (TopLevelRegion != LHS.TopLevelRegion) {
      releaseMemory();
      TopLevelRegion = LHS.TopLevelRegion;
    }
    LHS.TopLevelRegion = nullptr;
  }

  ConvergenceRegionInfo &operator=(ConvergenceRegionInfo &&LHS) {
    if (TopLevelRegion != LHS.TopLevelRegion) {
      releaseMemory();
      TopLevelRegion = LHS.TopLevelRegion;
    }
    LHS.TopLevelRegion = nullptr;
    return *this;
  }

  void releaseMemory() {
    if (TopLevelRegion == nullptr)
      return;

    TopLevelRegion->releaseMemory();
    delete TopLevelRegion;
    TopLevelRegion = nullptr;
  }

  const ConvergenceRegion *getTopLevelRegion() const { return TopLevelRegion; }
};

} // namespace SPIRV

// Wrapper around the function above to use it with the legacy pass manager.
class SPIRVConvergenceRegionAnalysisWrapperPass : public FunctionPass {
  SPIRV::ConvergenceRegionInfo CRI;

public:
  static char ID;

  SPIRVConvergenceRegionAnalysisWrapperPass();

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
  };

  bool runOnFunction(Function &F) override;

  SPIRV::ConvergenceRegionInfo &getRegionInfo() { return CRI; }
  const SPIRV::ConvergenceRegionInfo &getRegionInfo() const { return CRI; }
};

// Wrapper around the function above to use it with the new pass manager.
class SPIRVConvergenceRegionAnalysis
    : public AnalysisInfoMixin<SPIRVConvergenceRegionAnalysis> {
  friend AnalysisInfoMixin<SPIRVConvergenceRegionAnalysis>;
  static AnalysisKey Key;

public:
  using Result = SPIRV::ConvergenceRegionInfo;

  Result run(Function &F, FunctionAnalysisManager &AM);
};

namespace SPIRV {
ConvergenceRegionInfo getConvergenceRegions(Function &F, DominatorTree &DT,
                                            LoopInfo &LI);
} // namespace SPIRV

} // namespace llvm
#endif // LLVM_LIB_TARGET_SPIRV_SPIRVCONVERGENCEREGIONANALYSIS_H
