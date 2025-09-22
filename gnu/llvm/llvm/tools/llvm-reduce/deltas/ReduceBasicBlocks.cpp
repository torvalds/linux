//===- ReduceBasicBlocks.cpp - Specialized Delta Pass ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting BasicBlocks from defined functions.
//
//===----------------------------------------------------------------------===//

#include "ReduceBasicBlocks.h"
#include "Utils.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"

#include <vector>

#define DEBUG_TYPE "llvm-reduce"

using namespace llvm;

/// Replaces BB Terminator with one that only contains Chunk BBs
static void replaceBranchTerminator(BasicBlock &BB,
                                    const DenseSet<BasicBlock *> &BBsToDelete) {
  auto *Term = BB.getTerminator();
  std::vector<BasicBlock *> ChunkSuccessors;
  for (auto *Succ : successors(&BB)) {
    if (!BBsToDelete.count(Succ))
      ChunkSuccessors.push_back(Succ);
  }

  // BB only references Chunk BBs
  if (ChunkSuccessors.size() == Term->getNumSuccessors())
    return;

  bool IsBranch = isa<BranchInst>(Term);
  if (InvokeInst *Invoke = dyn_cast<InvokeInst>(Term)) {
    LandingPadInst *LP = Invoke->getLandingPadInst();
    // Remove landingpad instruction if the containing block isn't used by other
    // invokes.
    if (none_of(LP->getParent()->users(), [Invoke](User *U) {
          return U != Invoke && isa<InvokeInst>(U);
        })) {
      LP->replaceAllUsesWith(getDefaultValue(LP->getType()));
      LP->eraseFromParent();
    } else if (!ChunkSuccessors.empty() &&
               ChunkSuccessors[0] == LP->getParent()) {
      // If the selected successor is the landing pad, clear the chunk
      // successors to avoid creating a regular branch to the landing pad which
      // would result in invalid IR.
      ChunkSuccessors.clear();
    }
    IsBranch = true;
  }

  Value *Address = nullptr;
  if (auto *IndBI = dyn_cast<IndirectBrInst>(Term))
    Address = IndBI->getAddress();

  Term->replaceAllUsesWith(getDefaultValue(Term->getType()));
  Term->eraseFromParent();

  if (ChunkSuccessors.empty()) {
    // If that fails then resort to replacing with a ret.
    auto *FnRetTy = BB.getParent()->getReturnType();
    ReturnInst::Create(BB.getContext(),
                       FnRetTy->isVoidTy() ? nullptr : getDefaultValue(FnRetTy),
                       &BB);
    return;
  }

  if (IsBranch)
    BranchInst::Create(ChunkSuccessors[0], &BB);

  if (Address) {
    auto *NewIndBI =
        IndirectBrInst::Create(Address, ChunkSuccessors.size(), &BB);
    for (auto *Dest : ChunkSuccessors)
      NewIndBI->addDestination(Dest);
  }
}

/// Removes uninteresting BBs from switch, if the default case ends up being
/// uninteresting, the switch is replaced with a void return (since it has to be
/// replace with something)
static void
removeUninterestingBBsFromSwitch(SwitchInst &SwInst,
                                 const DenseSet<BasicBlock *> &BBsToDelete) {
  for (int I = 0, E = SwInst.getNumCases(); I != E; ++I) {
    auto Case = SwInst.case_begin() + I;
    if (BBsToDelete.count(Case->getCaseSuccessor())) {
      SwInst.removeCase(Case);
      --I;
      --E;
    }
  }

  if (BBsToDelete.count(SwInst.getDefaultDest())) {
    if (SwInst.getNumCases() == 0) {
      auto *FnRetTy = SwInst.getParent()->getParent()->getReturnType();
      Value *RetValue =
          FnRetTy->isVoidTy() ? nullptr : getDefaultValue(FnRetTy);
      ReturnInst::Create(SwInst.getContext(), RetValue, SwInst.getParent());
      SwInst.eraseFromParent();
      return;
    }

    // Replace the default dest with one of the other cases
    auto Case = SwInst.case_begin();

    BasicBlock *NewDefault = Case->getCaseSuccessor();
    SwInst.setDefaultDest(NewDefault);

    for (PHINode &SuccPHI : NewDefault->phis()) {
      SuccPHI.addIncoming(SuccPHI.getIncomingValueForBlock(SwInst.getParent()),
                          SwInst.getParent());
    }
  }
}

/// Removes out-of-chunk arguments from functions, and modifies their calls
/// accordingly. It also removes allocations of out-of-chunk arguments.
static void extractBasicBlocksFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  DenseSet<BasicBlock *> BBsToDelete;
  df_iterator_default_set<BasicBlock *> Reachable;

  for (auto &F : WorkItem.getModule()) {
    if (F.empty())
      continue;

    BasicBlock &Entry = F.getEntryBlock();
    for (auto *BB : depth_first_ext(&Entry, Reachable))
      (void)BB;

    // Skip any function with unreachable blocks. It's somewhat difficult to
    // avoid producing invalid IR without deleting them.
    //
    // We also do not want to unconditionally delete them, as doing so would
    // break the invariant of changing the number of chunks during counting.

    const bool HasUnreachableBlocks = Reachable.size() != F.size();
    Reachable.clear();

    if (HasUnreachableBlocks) {
      LLVM_DEBUG(dbgs() << "Skipping function with unreachable blocks\n");
      continue;
    }

    for (BasicBlock &BB : F) {
      if (&BB != &Entry && !O.shouldKeep())
        BBsToDelete.insert(&BB);
    }

    // Replace terminators that reference out-of-chunk BBs
    for (BasicBlock &BB : F) {
      if (auto *SwInst = dyn_cast<SwitchInst>(BB.getTerminator()))
        removeUninterestingBBsFromSwitch(*SwInst, BBsToDelete);
      else
        replaceBranchTerminator(BB, BBsToDelete);
    }

    // Cleanup any blocks that are now dead after eliminating this set. This
    // will likely be larger than the number of blocks the oracle told us to
    // delete.
    EliminateUnreachableBlocks(F);
    BBsToDelete.clear();
  }
}

void llvm::reduceBasicBlocksDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractBasicBlocksFromModule, "Reducing Basic Blocks");
}

static void removeUnreachableBasicBlocksFromModule(Oracle &O,
                                                   ReducerWorkItem &WorkItem) {
  std::vector<BasicBlock *> DeadBlocks;
  df_iterator_default_set<BasicBlock *> Reachable;

  for (Function &F : WorkItem.getModule()) {
    if (F.empty())
      continue;

    // Mark all reachable blocks.
    for (BasicBlock *BB : depth_first_ext(&F, Reachable))
      (void)BB;

    if (Reachable.size() != F.size() && !O.shouldKeep()) {
      for (BasicBlock &BB : F) {
        if (!Reachable.count(&BB))
          DeadBlocks.push_back(&BB);
      }

      // Delete the dead blocks.
      DeleteDeadBlocks(DeadBlocks, nullptr, /*KeepOneInputPHIs*/ false);
      DeadBlocks.clear();
    }

    Reachable.clear();
  }
}

void llvm::reduceUnreachableBasicBlocksDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, removeUnreachableBasicBlocksFromModule,
               "Removing Unreachable Basic Blocks");
}
