//===- SimplifyCFG.cpp ----------------------------------------------------===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the control flow graph (CFG) simplifications
// presented as part of the 'Getting Started With LLVM: Basics' tutorial at the
// US LLVM Developers Meeting 2019. It also contains additional material.
//
// The current file contains three different CFG simplifications. There are
// multiple versions of each implementation (e.g. _v1 and _v2), which implement
// additional functionality (e.g. preserving analysis like the DominatorTree) or
// use additional utilities to simplify the code (e.g. LLVM's PatternMatch.h).
// The available simplifications are:
//  1. Trivially Dead block Removal (removeDeadBlocks_v[1,2]).
//     This simplifications removes all blocks without predecessors in the CFG
//     from a function.
//  2. Conditional Branch Elimination (eliminateCondBranches_v[1,2,3])
//     This simplification replaces conditional branches with constant integer
//     conditions with unconditional branches.
//  3. Single Predecessor Block Merging (mergeIntoSinglePredecessor_v[1,2])
//     This simplification merges blocks with a single predecessor into the
//     predecessor, if that block has a single successor.
//
// TODOs
//  * Preserve LoopInfo.
//  * Add fixed point iteration to delete all dead blocks
//  * Add implementation using reachability to discover dead blocks.
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace PatternMatch;

enum TutorialVersion { V1, V2, V3 };
static cl::opt<TutorialVersion>
    Version("tut-simplifycfg-version", cl::desc("Select tutorial version"),
            cl::Hidden, cl::ValueOptional, cl::init(V1),
            cl::values(clEnumValN(V1, "v1", "version 1"),
                       clEnumValN(V2, "v2", "version 2"),
                       clEnumValN(V3, "v3", "version 3"),
                       // Sentinel value for unspecified option.
                       clEnumValN(V3, "", "")));

#define DEBUG_TYPE "tut-simplifycfg"

// Remove trivially dead blocks. First version, not preserving the
// DominatorTree.
static bool removeDeadBlocks_v1(Function &F) {
  bool Changed = false;

  // Remove trivially dead blocks.
  for (BasicBlock &BB : make_early_inc_range(F)) {
    // Skip blocks we know to not be trivially dead. We know a block is
    // guaranteed to be dead, iff it is neither the entry block nor
    // has any predecessors.
    if (&F.getEntryBlock() == &BB || !pred_empty(&BB))
      continue;

    // Notify successors of BB that BB is going to be removed. This removes
    // incoming values from BB from PHIs in the successors. Note that this will
    // not actually remove BB from the predecessor lists of its successors.
    for (BasicBlock *Succ : successors(&BB))
      Succ->removePredecessor(&BB);
    // TODO: Find a better place to put such small variations.
    // Alternatively, we can update the PHI nodes manually:
    // for (PHINode &PN : make_early_inc_range(Succ->phis()))
    //  PN.removeIncomingValue(&BB);

    // Replace all instructions in BB with a poison constant. The block is
    // unreachable, so the results of the instructions should never get used.
    while (!BB.empty()) {
      Instruction &I = BB.back();
      I.replaceAllUsesWith(PoisonValue::get(I.getType()));
      I.eraseFromParent();
    }

    // Finally remove the basic block.
    BB.eraseFromParent();
    Changed = true;
  }

  return Changed;
}

// Remove trivially dead blocks. This is the second version and preserves the
// dominator tree.
static bool removeDeadBlocks_v2(Function &F, DominatorTree &DT) {
  bool Changed = false;
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  SmallVector<DominatorTree::UpdateType, 8> DTUpdates;

  // Remove trivially dead blocks.
  for (BasicBlock &BB : make_early_inc_range(F)) {
    // Skip blocks we know to not be trivially dead. We know a block is
    // guaranteed to be dead, iff it is neither the entry block nor
    // has any predecessors.
    if (&F.getEntryBlock() == &BB || !pred_empty(&BB))
      continue;

    // Notify successors of BB that BB is going to be removed. This removes
    // incoming values from BB from PHIs in the successors. Note that this will
    // not actually remove BB from the predecessor lists of its successors.
    for (BasicBlock *Succ : successors(&BB)) {
      Succ->removePredecessor(&BB);

      // Collect updates that need to be applied to the dominator tree.
      DTUpdates.push_back({DominatorTree::Delete, &BB, Succ});
    }

    // Remove BB via the DomTreeUpdater. DomTreeUpdater::deleteBB conveniently
    // removes the instructions in BB as well.
    DTU.deleteBB(&BB);
    Changed = true;
  }

  // Apply updates permissively, to remove duplicates.
  DTU.applyUpdatesPermissive(DTUpdates);

  return Changed;
}

// Eliminate branches with constant conditionals. This is the first version,
// which *does not* preserve the dominator tree.
static bool eliminateCondBranches_v1(Function &F) {
  bool Changed = false;

  // Eliminate branches with constant conditionals.
  for (BasicBlock &BB : F) {
    // Skip blocks without conditional branches as terminators.
    BranchInst *BI = dyn_cast<BranchInst>(BB.getTerminator());
    if (!BI || !BI->isConditional())
      continue;

    // Skip blocks with conditional branches without ConstantInt conditions.
    ConstantInt *CI = dyn_cast<ConstantInt>(BI->getCondition());
    if (!CI)
      continue;

    // We use the branch condition (CI), to select the successor we remove:
    // if CI == 1 (true), we remove the second successor, otherwise the first.
    BasicBlock *RemovedSucc = BI->getSuccessor(CI->isOne());
    // Tell RemovedSucc we will remove BB from its predecessors.
    RemovedSucc->removePredecessor(&BB);

    // Replace the conditional branch with an unconditional one, by creating
    // a new unconditional branch to the selected successor and removing the
    // conditional one.
    BranchInst::Create(BI->getSuccessor(CI->isZero()), BI);
    BI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

// Eliminate branches with constant conditionals. This is the second
// version, which *does* preserve the dominator tree.
static bool eliminateCondBranches_v2(Function &F, DominatorTree &DT) {
  bool Changed = false;

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  SmallVector<DominatorTree::UpdateType, 8> DTUpdates;
  // Eliminate branches with constant conditionals.
  for (BasicBlock &BB : F) {
    // Skip blocks without conditional branches as terminators.
    BranchInst *BI = dyn_cast<BranchInst>(BB.getTerminator());
    if (!BI || !BI->isConditional())
      continue;

    // Skip blocks with conditional branches without ConstantInt conditions.
    ConstantInt *CI = dyn_cast<ConstantInt>(BI->getCondition());
    if (!CI)
      continue;

    // We use the branch condition (CI), to select the successor we remove:
    // if CI == 1 (true), we remove the second successor, otherwise the first.
    BasicBlock *RemovedSucc = BI->getSuccessor(CI->isOne());
    // Tell RemovedSucc we will remove BB from its predecessors.
    RemovedSucc->removePredecessor(&BB);

    // Replace the conditional branch with an unconditional one, by creating
    // a new unconditional branch to the selected successor and removing the
    // conditional one.
    BranchInst *NewBranch =
        BranchInst::Create(BI->getSuccessor(CI->isZero()), BI);
    BI->eraseFromParent();

    // Delete the edge between BB and RemovedSucc in the DominatorTree, iff
    // the conditional branch did not use RemovedSucc as both the true and false
    // branches.
    if (NewBranch->getSuccessor(0) != RemovedSucc)
      DTUpdates.push_back({DominatorTree::Delete, &BB, RemovedSucc});
    Changed = true;
  }

  // Apply updates permissively, to remove duplicates.
  DTU.applyUpdatesPermissive(DTUpdates);

  return Changed;
}

// Eliminate branches with constant conditionals. This is the third
// version, which uses PatternMatch.h.
static bool eliminateCondBranches_v3(Function &F, DominatorTree &DT) {
  bool Changed = false;
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  SmallVector<DominatorTree::UpdateType, 8> DTUpdates;

  // Eliminate branches with constant conditionals.
  for (BasicBlock &BB : F) {
    ConstantInt *CI = nullptr;
    BasicBlock *TakenSucc, *RemovedSucc;
    // Check if the terminator is a conditional branch, with constant integer
    // condition and also capture the successor blocks as TakenSucc and
    // RemovedSucc.
    if (!match(BB.getTerminator(),
               m_Br(m_ConstantInt(CI), m_BasicBlock(TakenSucc),
                    m_BasicBlock(RemovedSucc))))
      continue;

    // If the condition is false, swap TakenSucc and RemovedSucc.
    if (CI->isZero())
      std::swap(TakenSucc, RemovedSucc);

    // Tell RemovedSucc we will remove BB from its predecessors.
    RemovedSucc->removePredecessor(&BB);

    // Replace the conditional branch with an unconditional one, by creating
    // a new unconditional branch to the selected successor and removing the
    // conditional one.

    BranchInst *NewBranch = BranchInst::Create(TakenSucc, BB.getTerminator());
    BB.getTerminator()->eraseFromParent();

    // Delete the edge between BB and RemovedSucc in the DominatorTree, iff
    // the conditional branch did not use RemovedSucc as both the true and false
    // branches.
    if (NewBranch->getSuccessor(0) != RemovedSucc)
      DTUpdates.push_back({DominatorTree::Delete, &BB, RemovedSucc});
    Changed = true;
  }

  // Apply updates permissively, to remove duplicates.
  DTU.applyUpdatesPermissive(DTUpdates);
  return Changed;
}

// Merge basic blocks into their single predecessor, if their predecessor has a
// single successor. This is the first version and does not preserve the
// DominatorTree.
static bool mergeIntoSinglePredecessor_v1(Function &F) {
  bool Changed = false;

  // Merge blocks with single predecessors.
  for (BasicBlock &BB : make_early_inc_range(F)) {
    BasicBlock *Pred = BB.getSinglePredecessor();
    // Make sure  BB has a single predecessor Pred and BB is the single
    // successor of Pred.
    if (!Pred || Pred->getSingleSuccessor() != &BB)
      continue;

    // Do not try to merge self loops. That can happen in dead blocks.
    if (Pred == &BB)
      continue;

    // Need to replace it before nuking the branch.
    BB.replaceAllUsesWith(Pred);
    // PHI nodes in BB can only have a single incoming value. Remove them.
    for (PHINode &PN : make_early_inc_range(BB.phis())) {
      PN.replaceAllUsesWith(PN.getIncomingValue(0));
      PN.eraseFromParent();
    }
    // Move all instructions from BB to Pred.
    for (Instruction &I : make_early_inc_range(BB))
      I.moveBefore(Pred->getTerminator());

    // Remove the Pred's terminator (which jumped to BB). BB's terminator
    // will become Pred's terminator.
    Pred->getTerminator()->eraseFromParent();
    BB.eraseFromParent();

    Changed = true;
  }

  return Changed;
}

// Merge basic blocks into their single predecessor, if their predecessor has a
// single successor. This is the second version and does preserve the
// DominatorTree.
static bool mergeIntoSinglePredecessor_v2(Function &F, DominatorTree &DT) {
  bool Changed = false;
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  SmallVector<DominatorTree::UpdateType, 8> DTUpdates;

  // Merge blocks with single predecessors.
  for (BasicBlock &BB : make_early_inc_range(F)) {
    BasicBlock *Pred = BB.getSinglePredecessor();
    // Make sure  BB has a single predecessor Pred and BB is the single
    // successor of Pred.
    if (!Pred || Pred->getSingleSuccessor() != &BB)
      continue;

    // Do not try to merge self loops. That can happen in dead blocks.
    if (Pred == &BB)
      continue;

    // Tell DTU about the changes to the CFG: All edges from BB to its
    // successors get removed and we add edges between Pred and BB's successors.
    for (BasicBlock *Succ : successors(&BB)) {
      DTUpdates.push_back({DominatorTree::Delete, &BB, Succ});
      DTUpdates.push_back({DominatorTree::Insert, Pred, Succ});
    }
    // Also remove the edge between Pred and BB.
    DTUpdates.push_back({DominatorTree::Delete, Pred, &BB});

    // Need to replace it before nuking the branch.
    BB.replaceAllUsesWith(Pred);
    // PHI nodes in BB can only have a single incoming value. Remove them.
    for (PHINode &PN : make_early_inc_range(BB.phis())) {
      PN.replaceAllUsesWith(PN.getIncomingValue(0));
      PN.eraseFromParent();
    }
    // Move all instructions from BB to Pred.
    for (Instruction &I : make_early_inc_range(BB))
      I.moveBefore(Pred->getTerminator());

    // Remove the Pred's terminator (which jumped to BB). BB's terminator
    // will become Pred's terminator.
    Pred->getTerminator()->eraseFromParent();
    DTU.deleteBB(&BB);

    Changed = true;
  }

  // Apply updates permissively, to remove duplicates.
  DTU.applyUpdatesPermissive(DTUpdates);
  return Changed;
}

static bool doSimplify_v1(Function &F) {
  return (int)eliminateCondBranches_v1(F) | mergeIntoSinglePredecessor_v1(F) |
         removeDeadBlocks_v1(F);
}

static bool doSimplify_v2(Function &F, DominatorTree &DT) {
  return (int)eliminateCondBranches_v2(F, DT) |
         mergeIntoSinglePredecessor_v2(F, DT) | removeDeadBlocks_v2(F, DT);
}

static bool doSimplify_v3(Function &F, DominatorTree &DT) {
  return (int)eliminateCondBranches_v3(F, DT) |
         mergeIntoSinglePredecessor_v2(F, DT) | removeDeadBlocks_v2(F, DT);
}

namespace {
struct SimplifyCFGPass : public PassInfoMixin<SimplifyCFGPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    switch (Version) {
    case V1:
      doSimplify_v1(F);
      break;
    case V2: {
      DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
      doSimplify_v2(F, DT);
      break;
    }
    case V3: {
      DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
      doSimplify_v3(F, DT);
      break;
    }
    }

    return PreservedAnalyses::none();
  }
};
} // namespace

/* New PM Registration */
llvm::PassPluginLibraryInfo getExampleIRTransformsPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "SimplifyCFG", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, llvm::FunctionPassManager &PM,
                   ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "tut-simplifycfg") {
                    PM.addPass(SimplifyCFGPass());
                    return true;
                  }
                  return false;
                });
          }};
}

#ifndef LLVM_SIMPLIFYCFG_LINK_INTO_TOOLS
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getExampleIRTransformsPluginInfo();
}
#endif
