//===-- SPIRVMergeRegionExitTargets.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Merge the multiple exit targets of a convergence region into a single block.
// Each exit target will be assigned a constant value, and a phi node + switch
// will allow the new exit target to re-route to the correct basic block.
//
//===----------------------------------------------------------------------===//

#include "Analysis/SPIRVConvergenceRegionAnalysis.h"
#include "SPIRV.h"
#include "SPIRVSubtarget.h"
#include "SPIRVTargetMachine.h"
#include "SPIRVUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"

using namespace llvm;

namespace llvm {
void initializeSPIRVMergeRegionExitTargetsPass(PassRegistry &);

class SPIRVMergeRegionExitTargets : public FunctionPass {
public:
  static char ID;

  SPIRVMergeRegionExitTargets() : FunctionPass(ID) {
    initializeSPIRVMergeRegionExitTargetsPass(*PassRegistry::getPassRegistry());
  };

  // Gather all the successors of |BB|.
  // This function asserts if the terminator neither a branch, switch or return.
  std::unordered_set<BasicBlock *> gatherSuccessors(BasicBlock *BB) {
    std::unordered_set<BasicBlock *> output;
    auto *T = BB->getTerminator();

    if (auto *BI = dyn_cast<BranchInst>(T)) {
      output.insert(BI->getSuccessor(0));
      if (BI->isConditional())
        output.insert(BI->getSuccessor(1));
      return output;
    }

    if (auto *SI = dyn_cast<SwitchInst>(T)) {
      output.insert(SI->getDefaultDest());
      for (auto &Case : SI->cases())
        output.insert(Case.getCaseSuccessor());
      return output;
    }

    assert(isa<ReturnInst>(T) && "Unhandled terminator type.");
    return output;
  }

  /// Create a value in BB set to the value associated with the branch the block
  /// terminator will take.
  llvm::Value *createExitVariable(
      BasicBlock *BB,
      const DenseMap<BasicBlock *, ConstantInt *> &TargetToValue) {
    auto *T = BB->getTerminator();
    if (isa<ReturnInst>(T))
      return nullptr;

    IRBuilder<> Builder(BB);
    Builder.SetInsertPoint(T);

    if (auto *BI = dyn_cast<BranchInst>(T)) {

      BasicBlock *LHSTarget = BI->getSuccessor(0);
      BasicBlock *RHSTarget =
          BI->isConditional() ? BI->getSuccessor(1) : nullptr;

      Value *LHS = TargetToValue.count(LHSTarget) != 0
                       ? TargetToValue.at(LHSTarget)
                       : nullptr;
      Value *RHS = TargetToValue.count(RHSTarget) != 0
                       ? TargetToValue.at(RHSTarget)
                       : nullptr;

      if (LHS == nullptr || RHS == nullptr)
        return LHS == nullptr ? RHS : LHS;
      return Builder.CreateSelect(BI->getCondition(), LHS, RHS);
    }

    // TODO: add support for switch cases.
    llvm_unreachable("Unhandled terminator type.");
  }

  /// Replaces |BB|'s branch targets present in |ToReplace| with |NewTarget|.
  void replaceBranchTargets(BasicBlock *BB,
                            const SmallPtrSet<BasicBlock *, 4> &ToReplace,
                            BasicBlock *NewTarget) {
    auto *T = BB->getTerminator();
    if (isa<ReturnInst>(T))
      return;

    if (auto *BI = dyn_cast<BranchInst>(T)) {
      for (size_t i = 0; i < BI->getNumSuccessors(); i++) {
        if (ToReplace.count(BI->getSuccessor(i)) != 0)
          BI->setSuccessor(i, NewTarget);
      }
      return;
    }

    if (auto *SI = dyn_cast<SwitchInst>(T)) {
      for (size_t i = 0; i < SI->getNumSuccessors(); i++) {
        if (ToReplace.count(SI->getSuccessor(i)) != 0)
          SI->setSuccessor(i, NewTarget);
      }
      return;
    }

    assert(false && "Unhandled terminator type.");
  }

  // Run the pass on the given convergence region, ignoring the sub-regions.
  // Returns true if the CFG changed, false otherwise.
  bool runOnConvergenceRegionNoRecurse(LoopInfo &LI,
                                       const SPIRV::ConvergenceRegion *CR) {
    // Gather all the exit targets for this region.
    SmallPtrSet<BasicBlock *, 4> ExitTargets;
    for (BasicBlock *Exit : CR->Exits) {
      for (BasicBlock *Target : gatherSuccessors(Exit)) {
        if (CR->Blocks.count(Target) == 0)
          ExitTargets.insert(Target);
      }
    }

    // If we have zero or one exit target, nothing do to.
    if (ExitTargets.size() <= 1)
      return false;

    // Create the new single exit target.
    auto F = CR->Entry->getParent();
    auto NewExitTarget = BasicBlock::Create(F->getContext(), "new.exit", F);
    IRBuilder<> Builder(NewExitTarget);

    // CodeGen output needs to be stable. Using the set as-is would order
    // the targets differently depending on the allocation pattern.
    // Sorting per basic-block ordering in the function.
    std::vector<BasicBlock *> SortedExitTargets;
    std::vector<BasicBlock *> SortedExits;
    for (BasicBlock &BB : *F) {
      if (ExitTargets.count(&BB) != 0)
        SortedExitTargets.push_back(&BB);
      if (CR->Exits.count(&BB) != 0)
        SortedExits.push_back(&BB);
    }

    // Creating one constant per distinct exit target. This will be route to the
    // correct target.
    DenseMap<BasicBlock *, ConstantInt *> TargetToValue;
    for (BasicBlock *Target : SortedExitTargets)
      TargetToValue.insert(
          std::make_pair(Target, Builder.getInt32(TargetToValue.size())));

    // Creating one variable per exit node, set to the constant matching the
    // targeted external block.
    std::vector<std::pair<BasicBlock *, Value *>> ExitToVariable;
    for (auto Exit : SortedExits) {
      llvm::Value *Value = createExitVariable(Exit, TargetToValue);
      ExitToVariable.emplace_back(std::make_pair(Exit, Value));
    }

    // Gather the correct value depending on the exit we came from.
    llvm::PHINode *node =
        Builder.CreatePHI(Builder.getInt32Ty(), ExitToVariable.size());
    for (auto [BB, Value] : ExitToVariable) {
      node->addIncoming(Value, BB);
    }

    // Creating the switch to jump to the correct exit target.
    llvm::SwitchInst *Sw = Builder.CreateSwitch(node, SortedExitTargets[0],
                                                SortedExitTargets.size() - 1);
    for (size_t i = 1; i < SortedExitTargets.size(); i++) {
      BasicBlock *BB = SortedExitTargets[i];
      Sw->addCase(TargetToValue[BB], BB);
    }

    // Fix exit branches to redirect to the new exit.
    for (auto Exit : CR->Exits)
      replaceBranchTargets(Exit, ExitTargets, NewExitTarget);

    return true;
  }

  /// Run the pass on the given convergence region and sub-regions (DFS).
  /// Returns true if a region/sub-region was modified, false otherwise.
  /// This returns as soon as one region/sub-region has been modified.
  bool runOnConvergenceRegion(LoopInfo &LI,
                              const SPIRV::ConvergenceRegion *CR) {
    for (auto *Child : CR->Children)
      if (runOnConvergenceRegion(LI, Child))
        return true;

    return runOnConvergenceRegionNoRecurse(LI, CR);
  }

#if !NDEBUG
  /// Validates each edge exiting the region has the same destination basic
  /// block.
  void validateRegionExits(const SPIRV::ConvergenceRegion *CR) {
    for (auto *Child : CR->Children)
      validateRegionExits(Child);

    std::unordered_set<BasicBlock *> ExitTargets;
    for (auto *Exit : CR->Exits) {
      auto Set = gatherSuccessors(Exit);
      for (auto *BB : Set) {
        if (CR->Blocks.count(BB) == 0)
          ExitTargets.insert(BB);
      }
    }

    assert(ExitTargets.size() <= 1);
  }
#endif

  virtual bool runOnFunction(Function &F) override {
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    const auto *TopLevelRegion =
        getAnalysis<SPIRVConvergenceRegionAnalysisWrapperPass>()
            .getRegionInfo()
            .getTopLevelRegion();

    // FIXME: very inefficient method: each time a region is modified, we bubble
    // back up, and recompute the whole convergence region tree. Once the
    // algorithm is completed and test coverage good enough, rewrite this pass
    // to be efficient instead of simple.
    bool modified = false;
    while (runOnConvergenceRegion(LI, TopLevelRegion)) {
      TopLevelRegion = getAnalysis<SPIRVConvergenceRegionAnalysisWrapperPass>()
                           .getRegionInfo()
                           .getTopLevelRegion();
      modified = true;
    }

#if !defined(NDEBUG) || defined(EXPENSIVE_CHECKS)
    validateRegionExits(TopLevelRegion);
#endif
    return modified;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<SPIRVConvergenceRegionAnalysisWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }
};
} // namespace llvm

char SPIRVMergeRegionExitTargets::ID = 0;

INITIALIZE_PASS_BEGIN(SPIRVMergeRegionExitTargets, "split-region-exit-blocks",
                      "SPIRV split region exit blocks", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(SPIRVConvergenceRegionAnalysisWrapperPass)

INITIALIZE_PASS_END(SPIRVMergeRegionExitTargets, "split-region-exit-blocks",
                    "SPIRV split region exit blocks", false, false)

FunctionPass *llvm::createSPIRVMergeRegionExitTargetsPass() {
  return new SPIRVMergeRegionExitTargets();
}
