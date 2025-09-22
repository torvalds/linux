//===- BasicBlockUtils.cpp - BasicBlock Utilities --------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on basic blocks, and
// instructions contained within basic blocks.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "basicblock-utils"

static cl::opt<unsigned> MaxDeoptOrUnreachableSuccessorCheckDepth(
    "max-deopt-or-unreachable-succ-check-depth", cl::init(8), cl::Hidden,
    cl::desc("Set the maximum path length when checking whether a basic block "
             "is followed by a block that either has a terminating "
             "deoptimizing call or is terminated with an unreachable"));

void llvm::detachDeadBlocks(
    ArrayRef<BasicBlock *> BBs,
    SmallVectorImpl<DominatorTree::UpdateType> *Updates,
    bool KeepOneInputPHIs) {
  for (auto *BB : BBs) {
    // Loop through all of our successors and make sure they know that one
    // of their predecessors is going away.
    SmallPtrSet<BasicBlock *, 4> UniqueSuccessors;
    for (BasicBlock *Succ : successors(BB)) {
      Succ->removePredecessor(BB, KeepOneInputPHIs);
      if (Updates && UniqueSuccessors.insert(Succ).second)
        Updates->push_back({DominatorTree::Delete, BB, Succ});
    }

    // Zap all the instructions in the block.
    while (!BB->empty()) {
      Instruction &I = BB->back();
      // If this instruction is used, replace uses with an arbitrary value.
      // Because control flow can't get here, we don't care what we replace the
      // value with.  Note that since this block is unreachable, and all values
      // contained within it must dominate their uses, that all uses will
      // eventually be removed (they are themselves dead).
      if (!I.use_empty())
        I.replaceAllUsesWith(PoisonValue::get(I.getType()));
      BB->back().eraseFromParent();
    }
    new UnreachableInst(BB->getContext(), BB);
    assert(BB->size() == 1 &&
           isa<UnreachableInst>(BB->getTerminator()) &&
           "The successor list of BB isn't empty before "
           "applying corresponding DTU updates.");
  }
}

void llvm::DeleteDeadBlock(BasicBlock *BB, DomTreeUpdater *DTU,
                           bool KeepOneInputPHIs) {
  DeleteDeadBlocks({BB}, DTU, KeepOneInputPHIs);
}

void llvm::DeleteDeadBlocks(ArrayRef <BasicBlock *> BBs, DomTreeUpdater *DTU,
                            bool KeepOneInputPHIs) {
#ifndef NDEBUG
  // Make sure that all predecessors of each dead block is also dead.
  SmallPtrSet<BasicBlock *, 4> Dead(BBs.begin(), BBs.end());
  assert(Dead.size() == BBs.size() && "Duplicating blocks?");
  for (auto *BB : Dead)
    for (BasicBlock *Pred : predecessors(BB))
      assert(Dead.count(Pred) && "All predecessors must be dead!");
#endif

  SmallVector<DominatorTree::UpdateType, 4> Updates;
  detachDeadBlocks(BBs, DTU ? &Updates : nullptr, KeepOneInputPHIs);

  if (DTU)
    DTU->applyUpdates(Updates);

  for (BasicBlock *BB : BBs)
    if (DTU)
      DTU->deleteBB(BB);
    else
      BB->eraseFromParent();
}

bool llvm::EliminateUnreachableBlocks(Function &F, DomTreeUpdater *DTU,
                                      bool KeepOneInputPHIs) {
  df_iterator_default_set<BasicBlock*> Reachable;

  // Mark all reachable blocks.
  for (BasicBlock *BB : depth_first_ext(&F, Reachable))
    (void)BB/* Mark all reachable blocks */;

  // Collect all dead blocks.
  std::vector<BasicBlock*> DeadBlocks;
  for (BasicBlock &BB : F)
    if (!Reachable.count(&BB))
      DeadBlocks.push_back(&BB);

  // Delete the dead blocks.
  DeleteDeadBlocks(DeadBlocks, DTU, KeepOneInputPHIs);

  return !DeadBlocks.empty();
}

bool llvm::FoldSingleEntryPHINodes(BasicBlock *BB,
                                   MemoryDependenceResults *MemDep) {
  if (!isa<PHINode>(BB->begin()))
    return false;

  while (PHINode *PN = dyn_cast<PHINode>(BB->begin())) {
    if (PN->getIncomingValue(0) != PN)
      PN->replaceAllUsesWith(PN->getIncomingValue(0));
    else
      PN->replaceAllUsesWith(PoisonValue::get(PN->getType()));

    if (MemDep)
      MemDep->removeInstruction(PN);  // Memdep updates AA itself.

    PN->eraseFromParent();
  }
  return true;
}

bool llvm::DeleteDeadPHIs(BasicBlock *BB, const TargetLibraryInfo *TLI,
                          MemorySSAUpdater *MSSAU) {
  // Recursively deleting a PHI may cause multiple PHIs to be deleted
  // or RAUW'd undef, so use an array of WeakTrackingVH for the PHIs to delete.
  SmallVector<WeakTrackingVH, 8> PHIs;
  for (PHINode &PN : BB->phis())
    PHIs.push_back(&PN);

  bool Changed = false;
  for (unsigned i = 0, e = PHIs.size(); i != e; ++i)
    if (PHINode *PN = dyn_cast_or_null<PHINode>(PHIs[i].operator Value*()))
      Changed |= RecursivelyDeleteDeadPHINode(PN, TLI, MSSAU);

  return Changed;
}

bool llvm::MergeBlockIntoPredecessor(BasicBlock *BB, DomTreeUpdater *DTU,
                                     LoopInfo *LI, MemorySSAUpdater *MSSAU,
                                     MemoryDependenceResults *MemDep,
                                     bool PredecessorWithTwoSuccessors,
                                     DominatorTree *DT) {
  if (BB->hasAddressTaken())
    return false;

  // Can't merge if there are multiple predecessors, or no predecessors.
  BasicBlock *PredBB = BB->getUniquePredecessor();
  if (!PredBB) return false;

  // Don't break self-loops.
  if (PredBB == BB) return false;

  // Don't break unwinding instructions or terminators with other side-effects.
  Instruction *PTI = PredBB->getTerminator();
  if (PTI->isSpecialTerminator() || PTI->mayHaveSideEffects())
    return false;

  // Can't merge if there are multiple distinct successors.
  if (!PredecessorWithTwoSuccessors && PredBB->getUniqueSuccessor() != BB)
    return false;

  // Currently only allow PredBB to have two predecessors, one being BB.
  // Update BI to branch to BB's only successor instead of BB.
  BranchInst *PredBB_BI;
  BasicBlock *NewSucc = nullptr;
  unsigned FallThruPath;
  if (PredecessorWithTwoSuccessors) {
    if (!(PredBB_BI = dyn_cast<BranchInst>(PTI)))
      return false;
    BranchInst *BB_JmpI = dyn_cast<BranchInst>(BB->getTerminator());
    if (!BB_JmpI || !BB_JmpI->isUnconditional())
      return false;
    NewSucc = BB_JmpI->getSuccessor(0);
    FallThruPath = PredBB_BI->getSuccessor(0) == BB ? 0 : 1;
  }

  // Can't merge if there is PHI loop.
  for (PHINode &PN : BB->phis())
    if (llvm::is_contained(PN.incoming_values(), &PN))
      return false;

  LLVM_DEBUG(dbgs() << "Merging: " << BB->getName() << " into "
                    << PredBB->getName() << "\n");

  // Begin by getting rid of unneeded PHIs.
  SmallVector<AssertingVH<Value>, 4> IncomingValues;
  if (isa<PHINode>(BB->front())) {
    for (PHINode &PN : BB->phis())
      if (!isa<PHINode>(PN.getIncomingValue(0)) ||
          cast<PHINode>(PN.getIncomingValue(0))->getParent() != BB)
        IncomingValues.push_back(PN.getIncomingValue(0));
    FoldSingleEntryPHINodes(BB, MemDep);
  }

  if (DT) {
    assert(!DTU && "cannot use both DT and DTU for updates");
    DomTreeNode *PredNode = DT->getNode(PredBB);
    DomTreeNode *BBNode = DT->getNode(BB);
    if (PredNode) {
      assert(BBNode && "PredNode unreachable but BBNode reachable?");
      for (DomTreeNode *C : to_vector(BBNode->children()))
        C->setIDom(PredNode);
    }
  }
  // DTU update: Collect all the edges that exit BB.
  // These dominator edges will be redirected from Pred.
  std::vector<DominatorTree::UpdateType> Updates;
  if (DTU) {
    assert(!DT && "cannot use both DT and DTU for updates");
    // To avoid processing the same predecessor more than once.
    SmallPtrSet<BasicBlock *, 8> SeenSuccs;
    SmallPtrSet<BasicBlock *, 2> SuccsOfPredBB(succ_begin(PredBB),
                                               succ_end(PredBB));
    Updates.reserve(Updates.size() + 2 * succ_size(BB) + 1);
    // Add insert edges first. Experimentally, for the particular case of two
    // blocks that can be merged, with a single successor and single predecessor
    // respectively, it is beneficial to have all insert updates first. Deleting
    // edges first may lead to unreachable blocks, followed by inserting edges
    // making the blocks reachable again. Such DT updates lead to high compile
    // times. We add inserts before deletes here to reduce compile time.
    for (BasicBlock *SuccOfBB : successors(BB))
      // This successor of BB may already be a PredBB's successor.
      if (!SuccsOfPredBB.contains(SuccOfBB))
        if (SeenSuccs.insert(SuccOfBB).second)
          Updates.push_back({DominatorTree::Insert, PredBB, SuccOfBB});
    SeenSuccs.clear();
    for (BasicBlock *SuccOfBB : successors(BB))
      if (SeenSuccs.insert(SuccOfBB).second)
        Updates.push_back({DominatorTree::Delete, BB, SuccOfBB});
    Updates.push_back({DominatorTree::Delete, PredBB, BB});
  }

  Instruction *STI = BB->getTerminator();
  Instruction *Start = &*BB->begin();
  // If there's nothing to move, mark the starting instruction as the last
  // instruction in the block. Terminator instruction is handled separately.
  if (Start == STI)
    Start = PTI;

  // Move all definitions in the successor to the predecessor...
  PredBB->splice(PTI->getIterator(), BB, BB->begin(), STI->getIterator());

  if (MSSAU)
    MSSAU->moveAllAfterMergeBlocks(BB, PredBB, Start);

  // Make all PHI nodes that referred to BB now refer to Pred as their
  // source...
  BB->replaceAllUsesWith(PredBB);

  if (PredecessorWithTwoSuccessors) {
    // Delete the unconditional branch from BB.
    BB->back().eraseFromParent();

    // Update branch in the predecessor.
    PredBB_BI->setSuccessor(FallThruPath, NewSucc);
  } else {
    // Delete the unconditional branch from the predecessor.
    PredBB->back().eraseFromParent();

    // Move terminator instruction.
    BB->back().moveBeforePreserving(*PredBB, PredBB->end());

    // Terminator may be a memory accessing instruction too.
    if (MSSAU)
      if (MemoryUseOrDef *MUD = cast_or_null<MemoryUseOrDef>(
              MSSAU->getMemorySSA()->getMemoryAccess(PredBB->getTerminator())))
        MSSAU->moveToPlace(MUD, PredBB, MemorySSA::End);
  }
  // Add unreachable to now empty BB.
  new UnreachableInst(BB->getContext(), BB);

  // Inherit predecessors name if it exists.
  if (!PredBB->hasName())
    PredBB->takeName(BB);

  if (LI)
    LI->removeBlock(BB);

  if (MemDep)
    MemDep->invalidateCachedPredecessors();

  if (DTU)
    DTU->applyUpdates(Updates);

  if (DT) {
    assert(succ_empty(BB) &&
           "successors should have been transferred to PredBB");
    DT->eraseNode(BB);
  }

  // Finally, erase the old block and update dominator info.
  DeleteDeadBlock(BB, DTU);

  return true;
}

bool llvm::MergeBlockSuccessorsIntoGivenBlocks(
    SmallPtrSetImpl<BasicBlock *> &MergeBlocks, Loop *L, DomTreeUpdater *DTU,
    LoopInfo *LI) {
  assert(!MergeBlocks.empty() && "MergeBlocks should not be empty");

  bool BlocksHaveBeenMerged = false;
  while (!MergeBlocks.empty()) {
    BasicBlock *BB = *MergeBlocks.begin();
    BasicBlock *Dest = BB->getSingleSuccessor();
    if (Dest && (!L || L->contains(Dest))) {
      BasicBlock *Fold = Dest->getUniquePredecessor();
      (void)Fold;
      if (MergeBlockIntoPredecessor(Dest, DTU, LI)) {
        assert(Fold == BB &&
               "Expecting BB to be unique predecessor of the Dest block");
        MergeBlocks.erase(Dest);
        BlocksHaveBeenMerged = true;
      } else
        MergeBlocks.erase(BB);
    } else
      MergeBlocks.erase(BB);
  }
  return BlocksHaveBeenMerged;
}

/// Remove redundant instructions within sequences of consecutive dbg.value
/// instructions. This is done using a backward scan to keep the last dbg.value
/// describing a specific variable/fragment.
///
/// BackwardScan strategy:
/// ----------------------
/// Given a sequence of consecutive DbgValueInst like this
///
///   dbg.value ..., "x", FragmentX1  (*)
///   dbg.value ..., "y", FragmentY1
///   dbg.value ..., "x", FragmentX2
///   dbg.value ..., "x", FragmentX1  (**)
///
/// then the instruction marked with (*) can be removed (it is guaranteed to be
/// obsoleted by the instruction marked with (**) as the latter instruction is
/// describing the same variable using the same fragment info).
///
/// Possible improvements:
/// - Check fully overlapping fragments and not only identical fragments.
/// - Support dbg.declare. dbg.label, and possibly other meta instructions being
///   part of the sequence of consecutive instructions.
static bool
DbgVariableRecordsRemoveRedundantDbgInstrsUsingBackwardScan(BasicBlock *BB) {
  SmallVector<DbgVariableRecord *, 8> ToBeRemoved;
  SmallDenseSet<DebugVariable> VariableSet;
  for (auto &I : reverse(*BB)) {
    for (DbgRecord &DR : reverse(I.getDbgRecordRange())) {
      if (isa<DbgLabelRecord>(DR)) {
        // Emulate existing behaviour (see comment below for dbg.declares).
        // FIXME: Don't do this.
        VariableSet.clear();
        continue;
      }

      DbgVariableRecord &DVR = cast<DbgVariableRecord>(DR);
      // Skip declare-type records, as the debug intrinsic method only works
      // on dbg.value intrinsics.
      if (DVR.getType() == DbgVariableRecord::LocationType::Declare) {
        // The debug intrinsic method treats dbg.declares are "non-debug"
        // instructions (i.e., a break in a consecutive range of debug
        // intrinsics). Emulate that to create identical outputs. See
        // "Possible improvements" above.
        // FIXME: Delete the line below.
        VariableSet.clear();
        continue;
      }

      DebugVariable Key(DVR.getVariable(), DVR.getExpression(),
                        DVR.getDebugLoc()->getInlinedAt());
      auto R = VariableSet.insert(Key);
      // If the same variable fragment is described more than once it is enough
      // to keep the last one (i.e. the first found since we for reverse
      // iteration).
      if (R.second)
        continue;

      if (DVR.isDbgAssign()) {
        // Don't delete dbg.assign intrinsics that are linked to instructions.
        if (!at::getAssignmentInsts(&DVR).empty())
          continue;
        // Unlinked dbg.assign intrinsics can be treated like dbg.values.
      }

      ToBeRemoved.push_back(&DVR);
      continue;
    }
    // Sequence with consecutive dbg.value instrs ended. Clear the map to
    // restart identifying redundant instructions if case we find another
    // dbg.value sequence.
    VariableSet.clear();
  }

  for (auto &DVR : ToBeRemoved)
    DVR->eraseFromParent();

  return !ToBeRemoved.empty();
}

static bool removeRedundantDbgInstrsUsingBackwardScan(BasicBlock *BB) {
  if (BB->IsNewDbgInfoFormat)
    return DbgVariableRecordsRemoveRedundantDbgInstrsUsingBackwardScan(BB);

  SmallVector<DbgValueInst *, 8> ToBeRemoved;
  SmallDenseSet<DebugVariable> VariableSet;
  for (auto &I : reverse(*BB)) {
    if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(&I)) {
      DebugVariable Key(DVI->getVariable(),
                        DVI->getExpression(),
                        DVI->getDebugLoc()->getInlinedAt());
      auto R = VariableSet.insert(Key);
      // If the variable fragment hasn't been seen before then we don't want
      // to remove this dbg intrinsic.
      if (R.second)
        continue;

      if (auto *DAI = dyn_cast<DbgAssignIntrinsic>(DVI)) {
        // Don't delete dbg.assign intrinsics that are linked to instructions.
        if (!at::getAssignmentInsts(DAI).empty())
          continue;
        // Unlinked dbg.assign intrinsics can be treated like dbg.values.
      }

      // If the same variable fragment is described more than once it is enough
      // to keep the last one (i.e. the first found since we for reverse
      // iteration).
      ToBeRemoved.push_back(DVI);
      continue;
    }
    // Sequence with consecutive dbg.value instrs ended. Clear the map to
    // restart identifying redundant instructions if case we find another
    // dbg.value sequence.
    VariableSet.clear();
  }

  for (auto &Instr : ToBeRemoved)
    Instr->eraseFromParent();

  return !ToBeRemoved.empty();
}

/// Remove redundant dbg.value instructions using a forward scan. This can
/// remove a dbg.value instruction that is redundant due to indicating that a
/// variable has the same value as already being indicated by an earlier
/// dbg.value.
///
/// ForwardScan strategy:
/// ---------------------
/// Given two identical dbg.value instructions, separated by a block of
/// instructions that isn't describing the same variable, like this
///
///   dbg.value X1, "x", FragmentX1  (**)
///   <block of instructions, none being "dbg.value ..., "x", ...">
///   dbg.value X1, "x", FragmentX1  (*)
///
/// then the instruction marked with (*) can be removed. Variable "x" is already
/// described as being mapped to the SSA value X1.
///
/// Possible improvements:
/// - Keep track of non-overlapping fragments.
static bool
DbgVariableRecordsRemoveRedundantDbgInstrsUsingForwardScan(BasicBlock *BB) {
  SmallVector<DbgVariableRecord *, 8> ToBeRemoved;
  DenseMap<DebugVariable, std::pair<SmallVector<Value *, 4>, DIExpression *>>
      VariableMap;
  for (auto &I : *BB) {
    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
      if (DVR.getType() == DbgVariableRecord::LocationType::Declare)
        continue;
      DebugVariable Key(DVR.getVariable(), std::nullopt,
                        DVR.getDebugLoc()->getInlinedAt());
      auto VMI = VariableMap.find(Key);
      // A dbg.assign with no linked instructions can be treated like a
      // dbg.value (i.e. can be deleted).
      bool IsDbgValueKind =
          (!DVR.isDbgAssign() || at::getAssignmentInsts(&DVR).empty());

      // Update the map if we found a new value/expression describing the
      // variable, or if the variable wasn't mapped already.
      SmallVector<Value *, 4> Values(DVR.location_ops());
      if (VMI == VariableMap.end() || VMI->second.first != Values ||
          VMI->second.second != DVR.getExpression()) {
        if (IsDbgValueKind)
          VariableMap[Key] = {Values, DVR.getExpression()};
        else
          VariableMap[Key] = {Values, nullptr};
        continue;
      }
      // Don't delete dbg.assign intrinsics that are linked to instructions.
      if (!IsDbgValueKind)
        continue;
      // Found an identical mapping. Remember the instruction for later removal.
      ToBeRemoved.push_back(&DVR);
    }
  }

  for (auto *DVR : ToBeRemoved)
    DVR->eraseFromParent();

  return !ToBeRemoved.empty();
}

static bool
DbgVariableRecordsRemoveUndefDbgAssignsFromEntryBlock(BasicBlock *BB) {
  assert(BB->isEntryBlock() && "expected entry block");
  SmallVector<DbgVariableRecord *, 8> ToBeRemoved;
  DenseSet<DebugVariable> SeenDefForAggregate;
  // Returns the DebugVariable for DVI with no fragment info.
  auto GetAggregateVariable = [](const DbgVariableRecord &DVR) {
    return DebugVariable(DVR.getVariable(), std::nullopt,
                         DVR.getDebugLoc().getInlinedAt());
  };

  // Remove undef dbg.assign intrinsics that are encountered before
  // any non-undef intrinsics from the entry block.
  for (auto &I : *BB) {
    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
      if (!DVR.isDbgValue() && !DVR.isDbgAssign())
        continue;
      bool IsDbgValueKind =
          (DVR.isDbgValue() || at::getAssignmentInsts(&DVR).empty());
      DebugVariable Aggregate = GetAggregateVariable(DVR);
      if (!SeenDefForAggregate.contains(Aggregate)) {
        bool IsKill = DVR.isKillLocation() && IsDbgValueKind;
        if (!IsKill) {
          SeenDefForAggregate.insert(Aggregate);
        } else if (DVR.isDbgAssign()) {
          ToBeRemoved.push_back(&DVR);
        }
      }
    }
  }

  for (DbgVariableRecord *DVR : ToBeRemoved)
    DVR->eraseFromParent();

  return !ToBeRemoved.empty();
}

static bool removeRedundantDbgInstrsUsingForwardScan(BasicBlock *BB) {
  if (BB->IsNewDbgInfoFormat)
    return DbgVariableRecordsRemoveRedundantDbgInstrsUsingForwardScan(BB);

  SmallVector<DbgValueInst *, 8> ToBeRemoved;
  DenseMap<DebugVariable, std::pair<SmallVector<Value *, 4>, DIExpression *>>
      VariableMap;
  for (auto &I : *BB) {
    if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(&I)) {
      DebugVariable Key(DVI->getVariable(), std::nullopt,
                        DVI->getDebugLoc()->getInlinedAt());
      auto VMI = VariableMap.find(Key);
      auto *DAI = dyn_cast<DbgAssignIntrinsic>(DVI);
      // A dbg.assign with no linked instructions can be treated like a
      // dbg.value (i.e. can be deleted).
      bool IsDbgValueKind = (!DAI || at::getAssignmentInsts(DAI).empty());

      // Update the map if we found a new value/expression describing the
      // variable, or if the variable wasn't mapped already.
      SmallVector<Value *, 4> Values(DVI->getValues());
      if (VMI == VariableMap.end() || VMI->second.first != Values ||
          VMI->second.second != DVI->getExpression()) {
        // Use a sentinel value (nullptr) for the DIExpression when we see a
        // linked dbg.assign so that the next debug intrinsic will never match
        // it (i.e. always treat linked dbg.assigns as if they're unique).
        if (IsDbgValueKind)
          VariableMap[Key] = {Values, DVI->getExpression()};
        else
          VariableMap[Key] = {Values, nullptr};
        continue;
      }

      // Don't delete dbg.assign intrinsics that are linked to instructions.
      if (!IsDbgValueKind)
        continue;
      ToBeRemoved.push_back(DVI);
    }
  }

  for (auto &Instr : ToBeRemoved)
    Instr->eraseFromParent();

  return !ToBeRemoved.empty();
}

/// Remove redundant undef dbg.assign intrinsic from an entry block using a
/// forward scan.
/// Strategy:
/// ---------------------
/// Scanning forward, delete dbg.assign intrinsics iff they are undef, not
/// linked to an intrinsic, and don't share an aggregate variable with a debug
/// intrinsic that didn't meet the criteria. In other words, undef dbg.assigns
/// that come before non-undef debug intrinsics for the variable are
/// deleted. Given:
///
///   dbg.assign undef, "x", FragmentX1 (*)
///   <block of instructions, none being "dbg.value ..., "x", ...">
///   dbg.value %V, "x", FragmentX2
///   <block of instructions, none being "dbg.value ..., "x", ...">
///   dbg.assign undef, "x", FragmentX1
///
/// then (only) the instruction marked with (*) can be removed.
/// Possible improvements:
/// - Keep track of non-overlapping fragments.
static bool removeUndefDbgAssignsFromEntryBlock(BasicBlock *BB) {
  if (BB->IsNewDbgInfoFormat)
    return DbgVariableRecordsRemoveUndefDbgAssignsFromEntryBlock(BB);

  assert(BB->isEntryBlock() && "expected entry block");
  SmallVector<DbgAssignIntrinsic *, 8> ToBeRemoved;
  DenseSet<DebugVariable> SeenDefForAggregate;
  // Returns the DebugVariable for DVI with no fragment info.
  auto GetAggregateVariable = [](DbgValueInst *DVI) {
    return DebugVariable(DVI->getVariable(), std::nullopt,
                         DVI->getDebugLoc()->getInlinedAt());
  };

  // Remove undef dbg.assign intrinsics that are encountered before
  // any non-undef intrinsics from the entry block.
  for (auto &I : *BB) {
    DbgValueInst *DVI = dyn_cast<DbgValueInst>(&I);
    if (!DVI)
      continue;
    auto *DAI = dyn_cast<DbgAssignIntrinsic>(DVI);
    bool IsDbgValueKind = (!DAI || at::getAssignmentInsts(DAI).empty());
    DebugVariable Aggregate = GetAggregateVariable(DVI);
    if (!SeenDefForAggregate.contains(Aggregate)) {
      bool IsKill = DVI->isKillLocation() && IsDbgValueKind;
      if (!IsKill) {
        SeenDefForAggregate.insert(Aggregate);
      } else if (DAI) {
        ToBeRemoved.push_back(DAI);
      }
    }
  }

  for (DbgAssignIntrinsic *DAI : ToBeRemoved)
    DAI->eraseFromParent();

  return !ToBeRemoved.empty();
}

bool llvm::RemoveRedundantDbgInstrs(BasicBlock *BB) {
  bool MadeChanges = false;
  // By using the "backward scan" strategy before the "forward scan" strategy we
  // can remove both dbg.value (2) and (3) in a situation like this:
  //
  //   (1) dbg.value V1, "x", DIExpression()
  //       ...
  //   (2) dbg.value V2, "x", DIExpression()
  //   (3) dbg.value V1, "x", DIExpression()
  //
  // The backward scan will remove (2), it is made obsolete by (3). After
  // getting (2) out of the way, the foward scan will remove (3) since "x"
  // already is described as having the value V1 at (1).
  MadeChanges |= removeRedundantDbgInstrsUsingBackwardScan(BB);
  if (BB->isEntryBlock() &&
      isAssignmentTrackingEnabled(*BB->getParent()->getParent()))
    MadeChanges |= removeUndefDbgAssignsFromEntryBlock(BB);
  MadeChanges |= removeRedundantDbgInstrsUsingForwardScan(BB);

  if (MadeChanges)
    LLVM_DEBUG(dbgs() << "Removed redundant dbg instrs from: "
                      << BB->getName() << "\n");
  return MadeChanges;
}

void llvm::ReplaceInstWithValue(BasicBlock::iterator &BI, Value *V) {
  Instruction &I = *BI;
  // Replaces all of the uses of the instruction with uses of the value
  I.replaceAllUsesWith(V);

  // Make sure to propagate a name if there is one already.
  if (I.hasName() && !V->hasName())
    V->takeName(&I);

  // Delete the unnecessary instruction now...
  BI = BI->eraseFromParent();
}

void llvm::ReplaceInstWithInst(BasicBlock *BB, BasicBlock::iterator &BI,
                               Instruction *I) {
  assert(I->getParent() == nullptr &&
         "ReplaceInstWithInst: Instruction already inserted into basic block!");

  // Copy debug location to newly added instruction, if it wasn't already set
  // by the caller.
  if (!I->getDebugLoc())
    I->setDebugLoc(BI->getDebugLoc());

  // Insert the new instruction into the basic block...
  BasicBlock::iterator New = I->insertInto(BB, BI);

  // Replace all uses of the old instruction, and delete it.
  ReplaceInstWithValue(BI, I);

  // Move BI back to point to the newly inserted instruction
  BI = New;
}

bool llvm::IsBlockFollowedByDeoptOrUnreachable(const BasicBlock *BB) {
  // Remember visited blocks to avoid infinite loop
  SmallPtrSet<const BasicBlock *, 8> VisitedBlocks;
  unsigned Depth = 0;
  while (BB && Depth++ < MaxDeoptOrUnreachableSuccessorCheckDepth &&
         VisitedBlocks.insert(BB).second) {
    if (isa<UnreachableInst>(BB->getTerminator()) ||
        BB->getTerminatingDeoptimizeCall())
      return true;
    BB = BB->getUniqueSuccessor();
  }
  return false;
}

void llvm::ReplaceInstWithInst(Instruction *From, Instruction *To) {
  BasicBlock::iterator BI(From);
  ReplaceInstWithInst(From->getParent(), BI, To);
}

BasicBlock *llvm::SplitEdge(BasicBlock *BB, BasicBlock *Succ, DominatorTree *DT,
                            LoopInfo *LI, MemorySSAUpdater *MSSAU,
                            const Twine &BBName) {
  unsigned SuccNum = GetSuccessorNumber(BB, Succ);

  Instruction *LatchTerm = BB->getTerminator();

  CriticalEdgeSplittingOptions Options =
      CriticalEdgeSplittingOptions(DT, LI, MSSAU).setPreserveLCSSA();

  if ((isCriticalEdge(LatchTerm, SuccNum, Options.MergeIdenticalEdges))) {
    // If it is a critical edge, and the succesor is an exception block, handle
    // the split edge logic in this specific function
    if (Succ->isEHPad())
      return ehAwareSplitEdge(BB, Succ, nullptr, nullptr, Options, BBName);

    // If this is a critical edge, let SplitKnownCriticalEdge do it.
    return SplitKnownCriticalEdge(LatchTerm, SuccNum, Options, BBName);
  }

  // If the edge isn't critical, then BB has a single successor or Succ has a
  // single pred.  Split the block.
  if (BasicBlock *SP = Succ->getSinglePredecessor()) {
    // If the successor only has a single pred, split the top of the successor
    // block.
    assert(SP == BB && "CFG broken");
    (void)SP;
    return SplitBlock(Succ, &Succ->front(), DT, LI, MSSAU, BBName,
                      /*Before=*/true);
  }

  // Otherwise, if BB has a single successor, split it at the bottom of the
  // block.
  assert(BB->getTerminator()->getNumSuccessors() == 1 &&
         "Should have a single succ!");
  return SplitBlock(BB, BB->getTerminator(), DT, LI, MSSAU, BBName);
}

void llvm::setUnwindEdgeTo(Instruction *TI, BasicBlock *Succ) {
  if (auto *II = dyn_cast<InvokeInst>(TI))
    II->setUnwindDest(Succ);
  else if (auto *CS = dyn_cast<CatchSwitchInst>(TI))
    CS->setUnwindDest(Succ);
  else if (auto *CR = dyn_cast<CleanupReturnInst>(TI))
    CR->setUnwindDest(Succ);
  else
    llvm_unreachable("unexpected terminator instruction");
}

void llvm::updatePhiNodes(BasicBlock *DestBB, BasicBlock *OldPred,
                          BasicBlock *NewPred, PHINode *Until) {
  int BBIdx = 0;
  for (PHINode &PN : DestBB->phis()) {
    // We manually update the LandingPadReplacement PHINode and it is the last
    // PHI Node. So, if we find it, we are done.
    if (Until == &PN)
      break;

    // Reuse the previous value of BBIdx if it lines up.  In cases where we
    // have multiple phi nodes with *lots* of predecessors, this is a speed
    // win because we don't have to scan the PHI looking for TIBB.  This
    // happens because the BB list of PHI nodes are usually in the same
    // order.
    if (PN.getIncomingBlock(BBIdx) != OldPred)
      BBIdx = PN.getBasicBlockIndex(OldPred);

    assert(BBIdx != -1 && "Invalid PHI Index!");
    PN.setIncomingBlock(BBIdx, NewPred);
  }
}

BasicBlock *llvm::ehAwareSplitEdge(BasicBlock *BB, BasicBlock *Succ,
                                   LandingPadInst *OriginalPad,
                                   PHINode *LandingPadReplacement,
                                   const CriticalEdgeSplittingOptions &Options,
                                   const Twine &BBName) {

  auto *PadInst = Succ->getFirstNonPHI();
  if (!LandingPadReplacement && !PadInst->isEHPad())
    return SplitEdge(BB, Succ, Options.DT, Options.LI, Options.MSSAU, BBName);

  auto *LI = Options.LI;
  SmallVector<BasicBlock *, 4> LoopPreds;
  // Check if extra modifications will be required to preserve loop-simplify
  // form after splitting. If it would require splitting blocks with IndirectBr
  // terminators, bail out if preserving loop-simplify form is requested.
  if (Options.PreserveLoopSimplify && LI) {
    if (Loop *BBLoop = LI->getLoopFor(BB)) {

      // The only way that we can break LoopSimplify form by splitting a
      // critical edge is when there exists some edge from BBLoop to Succ *and*
      // the only edge into Succ from outside of BBLoop is that of NewBB after
      // the split. If the first isn't true, then LoopSimplify still holds,
      // NewBB is the new exit block and it has no non-loop predecessors. If the
      // second isn't true, then Succ was not in LoopSimplify form prior to
      // the split as it had a non-loop predecessor. In both of these cases,
      // the predecessor must be directly in BBLoop, not in a subloop, or again
      // LoopSimplify doesn't hold.
      for (BasicBlock *P : predecessors(Succ)) {
        if (P == BB)
          continue; // The new block is known.
        if (LI->getLoopFor(P) != BBLoop) {
          // Loop is not in LoopSimplify form, no need to re simplify after
          // splitting edge.
          LoopPreds.clear();
          break;
        }
        LoopPreds.push_back(P);
      }
      // Loop-simplify form can be preserved, if we can split all in-loop
      // predecessors.
      if (any_of(LoopPreds, [](BasicBlock *Pred) {
            return isa<IndirectBrInst>(Pred->getTerminator());
          })) {
        return nullptr;
      }
    }
  }

  auto *NewBB =
      BasicBlock::Create(BB->getContext(), BBName, BB->getParent(), Succ);
  setUnwindEdgeTo(BB->getTerminator(), NewBB);
  updatePhiNodes(Succ, BB, NewBB, LandingPadReplacement);

  if (LandingPadReplacement) {
    auto *NewLP = OriginalPad->clone();
    auto *Terminator = BranchInst::Create(Succ, NewBB);
    NewLP->insertBefore(Terminator);
    LandingPadReplacement->addIncoming(NewLP, NewBB);
  } else {
    Value *ParentPad = nullptr;
    if (auto *FuncletPad = dyn_cast<FuncletPadInst>(PadInst))
      ParentPad = FuncletPad->getParentPad();
    else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(PadInst))
      ParentPad = CatchSwitch->getParentPad();
    else if (auto *CleanupPad = dyn_cast<CleanupPadInst>(PadInst))
      ParentPad = CleanupPad->getParentPad();
    else if (auto *LandingPad = dyn_cast<LandingPadInst>(PadInst))
      ParentPad = LandingPad->getParent();
    else
      llvm_unreachable("handling for other EHPads not implemented yet");

    auto *NewCleanupPad = CleanupPadInst::Create(ParentPad, {}, BBName, NewBB);
    CleanupReturnInst::Create(NewCleanupPad, Succ, NewBB);
  }

  auto *DT = Options.DT;
  auto *MSSAU = Options.MSSAU;
  if (!DT && !LI)
    return NewBB;

  if (DT) {
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
    SmallVector<DominatorTree::UpdateType, 3> Updates;

    Updates.push_back({DominatorTree::Insert, BB, NewBB});
    Updates.push_back({DominatorTree::Insert, NewBB, Succ});
    Updates.push_back({DominatorTree::Delete, BB, Succ});

    DTU.applyUpdates(Updates);
    DTU.flush();

    if (MSSAU) {
      MSSAU->applyUpdates(Updates, *DT);
      if (VerifyMemorySSA)
        MSSAU->getMemorySSA()->verifyMemorySSA();
    }
  }

  if (LI) {
    if (Loop *BBLoop = LI->getLoopFor(BB)) {
      // If one or the other blocks were not in a loop, the new block is not
      // either, and thus LI doesn't need to be updated.
      if (Loop *SuccLoop = LI->getLoopFor(Succ)) {
        if (BBLoop == SuccLoop) {
          // Both in the same loop, the NewBB joins loop.
          SuccLoop->addBasicBlockToLoop(NewBB, *LI);
        } else if (BBLoop->contains(SuccLoop)) {
          // Edge from an outer loop to an inner loop.  Add to the outer loop.
          BBLoop->addBasicBlockToLoop(NewBB, *LI);
        } else if (SuccLoop->contains(BBLoop)) {
          // Edge from an inner loop to an outer loop.  Add to the outer loop.
          SuccLoop->addBasicBlockToLoop(NewBB, *LI);
        } else {
          // Edge from two loops with no containment relation.  Because these
          // are natural loops, we know that the destination block must be the
          // header of its loop (adding a branch into a loop elsewhere would
          // create an irreducible loop).
          assert(SuccLoop->getHeader() == Succ &&
                 "Should not create irreducible loops!");
          if (Loop *P = SuccLoop->getParentLoop())
            P->addBasicBlockToLoop(NewBB, *LI);
        }
      }

      // If BB is in a loop and Succ is outside of that loop, we may need to
      // update LoopSimplify form and LCSSA form.
      if (!BBLoop->contains(Succ)) {
        assert(!BBLoop->contains(NewBB) &&
               "Split point for loop exit is contained in loop!");

        // Update LCSSA form in the newly created exit block.
        if (Options.PreserveLCSSA) {
          createPHIsForSplitLoopExit(BB, NewBB, Succ);
        }

        if (!LoopPreds.empty()) {
          BasicBlock *NewExitBB = SplitBlockPredecessors(
              Succ, LoopPreds, "split", DT, LI, MSSAU, Options.PreserveLCSSA);
          if (Options.PreserveLCSSA)
            createPHIsForSplitLoopExit(LoopPreds, NewExitBB, Succ);
        }
      }
    }
  }

  return NewBB;
}

void llvm::createPHIsForSplitLoopExit(ArrayRef<BasicBlock *> Preds,
                                      BasicBlock *SplitBB, BasicBlock *DestBB) {
  // SplitBB shouldn't have anything non-trivial in it yet.
  assert((SplitBB->getFirstNonPHI() == SplitBB->getTerminator() ||
          SplitBB->isLandingPad()) &&
         "SplitBB has non-PHI nodes!");

  // For each PHI in the destination block.
  for (PHINode &PN : DestBB->phis()) {
    int Idx = PN.getBasicBlockIndex(SplitBB);
    assert(Idx >= 0 && "Invalid Block Index");
    Value *V = PN.getIncomingValue(Idx);

    // If the input is a PHI which already satisfies LCSSA, don't create
    // a new one.
    if (const PHINode *VP = dyn_cast<PHINode>(V))
      if (VP->getParent() == SplitBB)
        continue;

    // Otherwise a new PHI is needed. Create one and populate it.
    PHINode *NewPN = PHINode::Create(PN.getType(), Preds.size(), "split");
    BasicBlock::iterator InsertPos =
        SplitBB->isLandingPad() ? SplitBB->begin()
                                : SplitBB->getTerminator()->getIterator();
    NewPN->insertBefore(InsertPos);
    for (BasicBlock *BB : Preds)
      NewPN->addIncoming(V, BB);

    // Update the original PHI.
    PN.setIncomingValue(Idx, NewPN);
  }
}

unsigned
llvm::SplitAllCriticalEdges(Function &F,
                            const CriticalEdgeSplittingOptions &Options) {
  unsigned NumBroken = 0;
  for (BasicBlock &BB : F) {
    Instruction *TI = BB.getTerminator();
    if (TI->getNumSuccessors() > 1 && !isa<IndirectBrInst>(TI))
      for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
        if (SplitCriticalEdge(TI, i, Options))
          ++NumBroken;
  }
  return NumBroken;
}

static BasicBlock *SplitBlockImpl(BasicBlock *Old, BasicBlock::iterator SplitPt,
                                  DomTreeUpdater *DTU, DominatorTree *DT,
                                  LoopInfo *LI, MemorySSAUpdater *MSSAU,
                                  const Twine &BBName, bool Before) {
  if (Before) {
    DomTreeUpdater LocalDTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
    return splitBlockBefore(Old, SplitPt,
                            DTU ? DTU : (DT ? &LocalDTU : nullptr), LI, MSSAU,
                            BBName);
  }
  BasicBlock::iterator SplitIt = SplitPt;
  while (isa<PHINode>(SplitIt) || SplitIt->isEHPad()) {
    ++SplitIt;
    assert(SplitIt != SplitPt->getParent()->end());
  }
  std::string Name = BBName.str();
  BasicBlock *New = Old->splitBasicBlock(
      SplitIt, Name.empty() ? Old->getName() + ".split" : Name);

  // The new block lives in whichever loop the old one did. This preserves
  // LCSSA as well, because we force the split point to be after any PHI nodes.
  if (LI)
    if (Loop *L = LI->getLoopFor(Old))
      L->addBasicBlockToLoop(New, *LI);

  if (DTU) {
    SmallVector<DominatorTree::UpdateType, 8> Updates;
    // Old dominates New. New node dominates all other nodes dominated by Old.
    SmallPtrSet<BasicBlock *, 8> UniqueSuccessorsOfOld;
    Updates.push_back({DominatorTree::Insert, Old, New});
    Updates.reserve(Updates.size() + 2 * succ_size(New));
    for (BasicBlock *SuccessorOfOld : successors(New))
      if (UniqueSuccessorsOfOld.insert(SuccessorOfOld).second) {
        Updates.push_back({DominatorTree::Insert, New, SuccessorOfOld});
        Updates.push_back({DominatorTree::Delete, Old, SuccessorOfOld});
      }

    DTU->applyUpdates(Updates);
  } else if (DT)
    // Old dominates New. New node dominates all other nodes dominated by Old.
    if (DomTreeNode *OldNode = DT->getNode(Old)) {
      std::vector<DomTreeNode *> Children(OldNode->begin(), OldNode->end());

      DomTreeNode *NewNode = DT->addNewBlock(New, Old);
      for (DomTreeNode *I : Children)
        DT->changeImmediateDominator(I, NewNode);
    }

  // Move MemoryAccesses still tracked in Old, but part of New now.
  // Update accesses in successor blocks accordingly.
  if (MSSAU)
    MSSAU->moveAllAfterSpliceBlocks(Old, New, &*(New->begin()));

  return New;
}

BasicBlock *llvm::SplitBlock(BasicBlock *Old, BasicBlock::iterator SplitPt,
                             DominatorTree *DT, LoopInfo *LI,
                             MemorySSAUpdater *MSSAU, const Twine &BBName,
                             bool Before) {
  return SplitBlockImpl(Old, SplitPt, /*DTU=*/nullptr, DT, LI, MSSAU, BBName,
                        Before);
}
BasicBlock *llvm::SplitBlock(BasicBlock *Old, BasicBlock::iterator SplitPt,
                             DomTreeUpdater *DTU, LoopInfo *LI,
                             MemorySSAUpdater *MSSAU, const Twine &BBName,
                             bool Before) {
  return SplitBlockImpl(Old, SplitPt, DTU, /*DT=*/nullptr, LI, MSSAU, BBName,
                        Before);
}

BasicBlock *llvm::splitBlockBefore(BasicBlock *Old, BasicBlock::iterator SplitPt,
                                   DomTreeUpdater *DTU, LoopInfo *LI,
                                   MemorySSAUpdater *MSSAU,
                                   const Twine &BBName) {

  BasicBlock::iterator SplitIt = SplitPt;
  while (isa<PHINode>(SplitIt) || SplitIt->isEHPad())
    ++SplitIt;
  std::string Name = BBName.str();
  BasicBlock *New = Old->splitBasicBlock(
      SplitIt, Name.empty() ? Old->getName() + ".split" : Name,
      /* Before=*/true);

  // The new block lives in whichever loop the old one did. This preserves
  // LCSSA as well, because we force the split point to be after any PHI nodes.
  if (LI)
    if (Loop *L = LI->getLoopFor(Old))
      L->addBasicBlockToLoop(New, *LI);

  if (DTU) {
    SmallVector<DominatorTree::UpdateType, 8> DTUpdates;
    // New dominates Old. The predecessor nodes of the Old node dominate
    // New node.
    SmallPtrSet<BasicBlock *, 8> UniquePredecessorsOfOld;
    DTUpdates.push_back({DominatorTree::Insert, New, Old});
    DTUpdates.reserve(DTUpdates.size() + 2 * pred_size(New));
    for (BasicBlock *PredecessorOfOld : predecessors(New))
      if (UniquePredecessorsOfOld.insert(PredecessorOfOld).second) {
        DTUpdates.push_back({DominatorTree::Insert, PredecessorOfOld, New});
        DTUpdates.push_back({DominatorTree::Delete, PredecessorOfOld, Old});
      }

    DTU->applyUpdates(DTUpdates);

    // Move MemoryAccesses still tracked in Old, but part of New now.
    // Update accesses in successor blocks accordingly.
    if (MSSAU) {
      MSSAU->applyUpdates(DTUpdates, DTU->getDomTree());
      if (VerifyMemorySSA)
        MSSAU->getMemorySSA()->verifyMemorySSA();
    }
  }
  return New;
}

/// Update DominatorTree, LoopInfo, and LCCSA analysis information.
/// Invalidates DFS Numbering when DTU or DT is provided.
static void UpdateAnalysisInformation(BasicBlock *OldBB, BasicBlock *NewBB,
                                      ArrayRef<BasicBlock *> Preds,
                                      DomTreeUpdater *DTU, DominatorTree *DT,
                                      LoopInfo *LI, MemorySSAUpdater *MSSAU,
                                      bool PreserveLCSSA, bool &HasLoopExit) {
  // Update dominator tree if available.
  if (DTU) {
    // Recalculation of DomTree is needed when updating a forward DomTree and
    // the Entry BB is replaced.
    if (NewBB->isEntryBlock() && DTU->hasDomTree()) {
      // The entry block was removed and there is no external interface for
      // the dominator tree to be notified of this change. In this corner-case
      // we recalculate the entire tree.
      DTU->recalculate(*NewBB->getParent());
    } else {
      // Split block expects NewBB to have a non-empty set of predecessors.
      SmallVector<DominatorTree::UpdateType, 8> Updates;
      SmallPtrSet<BasicBlock *, 8> UniquePreds;
      Updates.push_back({DominatorTree::Insert, NewBB, OldBB});
      Updates.reserve(Updates.size() + 2 * Preds.size());
      for (auto *Pred : Preds)
        if (UniquePreds.insert(Pred).second) {
          Updates.push_back({DominatorTree::Insert, Pred, NewBB});
          Updates.push_back({DominatorTree::Delete, Pred, OldBB});
        }
      DTU->applyUpdates(Updates);
    }
  } else if (DT) {
    if (OldBB == DT->getRootNode()->getBlock()) {
      assert(NewBB->isEntryBlock());
      DT->setNewRoot(NewBB);
    } else {
      // Split block expects NewBB to have a non-empty set of predecessors.
      DT->splitBlock(NewBB);
    }
  }

  // Update MemoryPhis after split if MemorySSA is available
  if (MSSAU)
    MSSAU->wireOldPredecessorsToNewImmediatePredecessor(OldBB, NewBB, Preds);

  // The rest of the logic is only relevant for updating the loop structures.
  if (!LI)
    return;

  if (DTU && DTU->hasDomTree())
    DT = &DTU->getDomTree();
  assert(DT && "DT should be available to update LoopInfo!");
  Loop *L = LI->getLoopFor(OldBB);

  // If we need to preserve loop analyses, collect some information about how
  // this split will affect loops.
  bool IsLoopEntry = !!L;
  bool SplitMakesNewLoopHeader = false;
  for (BasicBlock *Pred : Preds) {
    // Preds that are not reachable from entry should not be used to identify if
    // OldBB is a loop entry or if SplitMakesNewLoopHeader. Unreachable blocks
    // are not within any loops, so we incorrectly mark SplitMakesNewLoopHeader
    // as true and make the NewBB the header of some loop. This breaks LI.
    if (!DT->isReachableFromEntry(Pred))
      continue;
    // If we need to preserve LCSSA, determine if any of the preds is a loop
    // exit.
    if (PreserveLCSSA)
      if (Loop *PL = LI->getLoopFor(Pred))
        if (!PL->contains(OldBB))
          HasLoopExit = true;

    // If we need to preserve LoopInfo, note whether any of the preds crosses
    // an interesting loop boundary.
    if (!L)
      continue;
    if (L->contains(Pred))
      IsLoopEntry = false;
    else
      SplitMakesNewLoopHeader = true;
  }

  // Unless we have a loop for OldBB, nothing else to do here.
  if (!L)
    return;

  if (IsLoopEntry) {
    // Add the new block to the nearest enclosing loop (and not an adjacent
    // loop). To find this, examine each of the predecessors and determine which
    // loops enclose them, and select the most-nested loop which contains the
    // loop containing the block being split.
    Loop *InnermostPredLoop = nullptr;
    for (BasicBlock *Pred : Preds) {
      if (Loop *PredLoop = LI->getLoopFor(Pred)) {
        // Seek a loop which actually contains the block being split (to avoid
        // adjacent loops).
        while (PredLoop && !PredLoop->contains(OldBB))
          PredLoop = PredLoop->getParentLoop();

        // Select the most-nested of these loops which contains the block.
        if (PredLoop && PredLoop->contains(OldBB) &&
            (!InnermostPredLoop ||
             InnermostPredLoop->getLoopDepth() < PredLoop->getLoopDepth()))
          InnermostPredLoop = PredLoop;
      }
    }

    if (InnermostPredLoop)
      InnermostPredLoop->addBasicBlockToLoop(NewBB, *LI);
  } else {
    L->addBasicBlockToLoop(NewBB, *LI);
    if (SplitMakesNewLoopHeader)
      L->moveToHeader(NewBB);
  }
}

/// Update the PHI nodes in OrigBB to include the values coming from NewBB.
/// This also updates AliasAnalysis, if available.
static void UpdatePHINodes(BasicBlock *OrigBB, BasicBlock *NewBB,
                           ArrayRef<BasicBlock *> Preds, BranchInst *BI,
                           bool HasLoopExit) {
  // Otherwise, create a new PHI node in NewBB for each PHI node in OrigBB.
  SmallPtrSet<BasicBlock *, 16> PredSet(Preds.begin(), Preds.end());
  for (BasicBlock::iterator I = OrigBB->begin(); isa<PHINode>(I); ) {
    PHINode *PN = cast<PHINode>(I++);

    // Check to see if all of the values coming in are the same.  If so, we
    // don't need to create a new PHI node, unless it's needed for LCSSA.
    Value *InVal = nullptr;
    if (!HasLoopExit) {
      InVal = PN->getIncomingValueForBlock(Preds[0]);
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        if (!PredSet.count(PN->getIncomingBlock(i)))
          continue;
        if (!InVal)
          InVal = PN->getIncomingValue(i);
        else if (InVal != PN->getIncomingValue(i)) {
          InVal = nullptr;
          break;
        }
      }
    }

    if (InVal) {
      // If all incoming values for the new PHI would be the same, just don't
      // make a new PHI.  Instead, just remove the incoming values from the old
      // PHI.
      PN->removeIncomingValueIf(
          [&](unsigned Idx) {
            return PredSet.contains(PN->getIncomingBlock(Idx));
          },
          /* DeletePHIIfEmpty */ false);

      // Add an incoming value to the PHI node in the loop for the preheader
      // edge.
      PN->addIncoming(InVal, NewBB);
      continue;
    }

    // If the values coming into the block are not the same, we need a new
    // PHI.
    // Create the new PHI node, insert it into NewBB at the end of the block
    PHINode *NewPHI =
        PHINode::Create(PN->getType(), Preds.size(), PN->getName() + ".ph", BI->getIterator());

    // NOTE! This loop walks backwards for a reason! First off, this minimizes
    // the cost of removal if we end up removing a large number of values, and
    // second off, this ensures that the indices for the incoming values aren't
    // invalidated when we remove one.
    for (int64_t i = PN->getNumIncomingValues() - 1; i >= 0; --i) {
      BasicBlock *IncomingBB = PN->getIncomingBlock(i);
      if (PredSet.count(IncomingBB)) {
        Value *V = PN->removeIncomingValue(i, false);
        NewPHI->addIncoming(V, IncomingBB);
      }
    }

    PN->addIncoming(NewPHI, NewBB);
  }
}

static void SplitLandingPadPredecessorsImpl(
    BasicBlock *OrigBB, ArrayRef<BasicBlock *> Preds, const char *Suffix1,
    const char *Suffix2, SmallVectorImpl<BasicBlock *> &NewBBs,
    DomTreeUpdater *DTU, DominatorTree *DT, LoopInfo *LI,
    MemorySSAUpdater *MSSAU, bool PreserveLCSSA);

static BasicBlock *
SplitBlockPredecessorsImpl(BasicBlock *BB, ArrayRef<BasicBlock *> Preds,
                           const char *Suffix, DomTreeUpdater *DTU,
                           DominatorTree *DT, LoopInfo *LI,
                           MemorySSAUpdater *MSSAU, bool PreserveLCSSA) {
  // Do not attempt to split that which cannot be split.
  if (!BB->canSplitPredecessors())
    return nullptr;

  // For the landingpads we need to act a bit differently.
  // Delegate this work to the SplitLandingPadPredecessors.
  if (BB->isLandingPad()) {
    SmallVector<BasicBlock*, 2> NewBBs;
    std::string NewName = std::string(Suffix) + ".split-lp";

    SplitLandingPadPredecessorsImpl(BB, Preds, Suffix, NewName.c_str(), NewBBs,
                                    DTU, DT, LI, MSSAU, PreserveLCSSA);
    return NewBBs[0];
  }

  // Create new basic block, insert right before the original block.
  BasicBlock *NewBB = BasicBlock::Create(
      BB->getContext(), BB->getName() + Suffix, BB->getParent(), BB);

  // The new block unconditionally branches to the old block.
  BranchInst *BI = BranchInst::Create(BB, NewBB);

  Loop *L = nullptr;
  BasicBlock *OldLatch = nullptr;
  // Splitting the predecessors of a loop header creates a preheader block.
  if (LI && LI->isLoopHeader(BB)) {
    L = LI->getLoopFor(BB);
    // Using the loop start line number prevents debuggers stepping into the
    // loop body for this instruction.
    BI->setDebugLoc(L->getStartLoc());

    // If BB is the header of the Loop, it is possible that the loop is
    // modified, such that the current latch does not remain the latch of the
    // loop. If that is the case, the loop metadata from the current latch needs
    // to be applied to the new latch.
    OldLatch = L->getLoopLatch();
  } else
    BI->setDebugLoc(BB->getFirstNonPHIOrDbg()->getDebugLoc());

  // Move the edges from Preds to point to NewBB instead of BB.
  for (BasicBlock *Pred : Preds) {
    // This is slightly more strict than necessary; the minimum requirement
    // is that there be no more than one indirectbr branching to BB. And
    // all BlockAddress uses would need to be updated.
    assert(!isa<IndirectBrInst>(Pred->getTerminator()) &&
           "Cannot split an edge from an IndirectBrInst");
    Pred->getTerminator()->replaceSuccessorWith(BB, NewBB);
  }

  // Insert a new PHI node into NewBB for every PHI node in BB and that new PHI
  // node becomes an incoming value for BB's phi node.  However, if the Preds
  // list is empty, we need to insert dummy entries into the PHI nodes in BB to
  // account for the newly created predecessor.
  if (Preds.empty()) {
    // Insert dummy values as the incoming value.
    for (BasicBlock::iterator I = BB->begin(); isa<PHINode>(I); ++I)
      cast<PHINode>(I)->addIncoming(PoisonValue::get(I->getType()), NewBB);
  }

  // Update DominatorTree, LoopInfo, and LCCSA analysis information.
  bool HasLoopExit = false;
  UpdateAnalysisInformation(BB, NewBB, Preds, DTU, DT, LI, MSSAU, PreserveLCSSA,
                            HasLoopExit);

  if (!Preds.empty()) {
    // Update the PHI nodes in BB with the values coming from NewBB.
    UpdatePHINodes(BB, NewBB, Preds, BI, HasLoopExit);
  }

  if (OldLatch) {
    BasicBlock *NewLatch = L->getLoopLatch();
    if (NewLatch != OldLatch) {
      MDNode *MD = OldLatch->getTerminator()->getMetadata(LLVMContext::MD_loop);
      NewLatch->getTerminator()->setMetadata(LLVMContext::MD_loop, MD);
      // It's still possible that OldLatch is the latch of another inner loop,
      // in which case we do not remove the metadata.
      Loop *IL = LI->getLoopFor(OldLatch);
      if (IL && IL->getLoopLatch() != OldLatch)
        OldLatch->getTerminator()->setMetadata(LLVMContext::MD_loop, nullptr);
    }
  }

  return NewBB;
}

BasicBlock *llvm::SplitBlockPredecessors(BasicBlock *BB,
                                         ArrayRef<BasicBlock *> Preds,
                                         const char *Suffix, DominatorTree *DT,
                                         LoopInfo *LI, MemorySSAUpdater *MSSAU,
                                         bool PreserveLCSSA) {
  return SplitBlockPredecessorsImpl(BB, Preds, Suffix, /*DTU=*/nullptr, DT, LI,
                                    MSSAU, PreserveLCSSA);
}
BasicBlock *llvm::SplitBlockPredecessors(BasicBlock *BB,
                                         ArrayRef<BasicBlock *> Preds,
                                         const char *Suffix,
                                         DomTreeUpdater *DTU, LoopInfo *LI,
                                         MemorySSAUpdater *MSSAU,
                                         bool PreserveLCSSA) {
  return SplitBlockPredecessorsImpl(BB, Preds, Suffix, DTU,
                                    /*DT=*/nullptr, LI, MSSAU, PreserveLCSSA);
}

static void SplitLandingPadPredecessorsImpl(
    BasicBlock *OrigBB, ArrayRef<BasicBlock *> Preds, const char *Suffix1,
    const char *Suffix2, SmallVectorImpl<BasicBlock *> &NewBBs,
    DomTreeUpdater *DTU, DominatorTree *DT, LoopInfo *LI,
    MemorySSAUpdater *MSSAU, bool PreserveLCSSA) {
  assert(OrigBB->isLandingPad() && "Trying to split a non-landing pad!");

  // Create a new basic block for OrigBB's predecessors listed in Preds. Insert
  // it right before the original block.
  BasicBlock *NewBB1 = BasicBlock::Create(OrigBB->getContext(),
                                          OrigBB->getName() + Suffix1,
                                          OrigBB->getParent(), OrigBB);
  NewBBs.push_back(NewBB1);

  // The new block unconditionally branches to the old block.
  BranchInst *BI1 = BranchInst::Create(OrigBB, NewBB1);
  BI1->setDebugLoc(OrigBB->getFirstNonPHI()->getDebugLoc());

  // Move the edges from Preds to point to NewBB1 instead of OrigBB.
  for (BasicBlock *Pred : Preds) {
    // This is slightly more strict than necessary; the minimum requirement
    // is that there be no more than one indirectbr branching to BB. And
    // all BlockAddress uses would need to be updated.
    assert(!isa<IndirectBrInst>(Pred->getTerminator()) &&
           "Cannot split an edge from an IndirectBrInst");
    Pred->getTerminator()->replaceUsesOfWith(OrigBB, NewBB1);
  }

  bool HasLoopExit = false;
  UpdateAnalysisInformation(OrigBB, NewBB1, Preds, DTU, DT, LI, MSSAU,
                            PreserveLCSSA, HasLoopExit);

  // Update the PHI nodes in OrigBB with the values coming from NewBB1.
  UpdatePHINodes(OrigBB, NewBB1, Preds, BI1, HasLoopExit);

  // Move the remaining edges from OrigBB to point to NewBB2.
  SmallVector<BasicBlock*, 8> NewBB2Preds;
  for (pred_iterator i = pred_begin(OrigBB), e = pred_end(OrigBB);
       i != e; ) {
    BasicBlock *Pred = *i++;
    if (Pred == NewBB1) continue;
    assert(!isa<IndirectBrInst>(Pred->getTerminator()) &&
           "Cannot split an edge from an IndirectBrInst");
    NewBB2Preds.push_back(Pred);
    e = pred_end(OrigBB);
  }

  BasicBlock *NewBB2 = nullptr;
  if (!NewBB2Preds.empty()) {
    // Create another basic block for the rest of OrigBB's predecessors.
    NewBB2 = BasicBlock::Create(OrigBB->getContext(),
                                OrigBB->getName() + Suffix2,
                                OrigBB->getParent(), OrigBB);
    NewBBs.push_back(NewBB2);

    // The new block unconditionally branches to the old block.
    BranchInst *BI2 = BranchInst::Create(OrigBB, NewBB2);
    BI2->setDebugLoc(OrigBB->getFirstNonPHI()->getDebugLoc());

    // Move the remaining edges from OrigBB to point to NewBB2.
    for (BasicBlock *NewBB2Pred : NewBB2Preds)
      NewBB2Pred->getTerminator()->replaceUsesOfWith(OrigBB, NewBB2);

    // Update DominatorTree, LoopInfo, and LCCSA analysis information.
    HasLoopExit = false;
    UpdateAnalysisInformation(OrigBB, NewBB2, NewBB2Preds, DTU, DT, LI, MSSAU,
                              PreserveLCSSA, HasLoopExit);

    // Update the PHI nodes in OrigBB with the values coming from NewBB2.
    UpdatePHINodes(OrigBB, NewBB2, NewBB2Preds, BI2, HasLoopExit);
  }

  LandingPadInst *LPad = OrigBB->getLandingPadInst();
  Instruction *Clone1 = LPad->clone();
  Clone1->setName(Twine("lpad") + Suffix1);
  Clone1->insertInto(NewBB1, NewBB1->getFirstInsertionPt());

  if (NewBB2) {
    Instruction *Clone2 = LPad->clone();
    Clone2->setName(Twine("lpad") + Suffix2);
    Clone2->insertInto(NewBB2, NewBB2->getFirstInsertionPt());

    // Create a PHI node for the two cloned landingpad instructions only
    // if the original landingpad instruction has some uses.
    if (!LPad->use_empty()) {
      assert(!LPad->getType()->isTokenTy() &&
             "Split cannot be applied if LPad is token type. Otherwise an "
             "invalid PHINode of token type would be created.");
      PHINode *PN = PHINode::Create(LPad->getType(), 2, "lpad.phi", LPad->getIterator());
      PN->addIncoming(Clone1, NewBB1);
      PN->addIncoming(Clone2, NewBB2);
      LPad->replaceAllUsesWith(PN);
    }
    LPad->eraseFromParent();
  } else {
    // There is no second clone. Just replace the landing pad with the first
    // clone.
    LPad->replaceAllUsesWith(Clone1);
    LPad->eraseFromParent();
  }
}

void llvm::SplitLandingPadPredecessors(BasicBlock *OrigBB,
                                       ArrayRef<BasicBlock *> Preds,
                                       const char *Suffix1, const char *Suffix2,
                                       SmallVectorImpl<BasicBlock *> &NewBBs,
                                       DomTreeUpdater *DTU, LoopInfo *LI,
                                       MemorySSAUpdater *MSSAU,
                                       bool PreserveLCSSA) {
  return SplitLandingPadPredecessorsImpl(OrigBB, Preds, Suffix1, Suffix2,
                                         NewBBs, DTU, /*DT=*/nullptr, LI, MSSAU,
                                         PreserveLCSSA);
}

ReturnInst *llvm::FoldReturnIntoUncondBranch(ReturnInst *RI, BasicBlock *BB,
                                             BasicBlock *Pred,
                                             DomTreeUpdater *DTU) {
  Instruction *UncondBranch = Pred->getTerminator();
  // Clone the return and add it to the end of the predecessor.
  Instruction *NewRet = RI->clone();
  NewRet->insertInto(Pred, Pred->end());

  // If the return instruction returns a value, and if the value was a
  // PHI node in "BB", propagate the right value into the return.
  for (Use &Op : NewRet->operands()) {
    Value *V = Op;
    Instruction *NewBC = nullptr;
    if (BitCastInst *BCI = dyn_cast<BitCastInst>(V)) {
      // Return value might be bitcasted. Clone and insert it before the
      // return instruction.
      V = BCI->getOperand(0);
      NewBC = BCI->clone();
      NewBC->insertInto(Pred, NewRet->getIterator());
      Op = NewBC;
    }

    Instruction *NewEV = nullptr;
    if (ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(V)) {
      V = EVI->getOperand(0);
      NewEV = EVI->clone();
      if (NewBC) {
        NewBC->setOperand(0, NewEV);
        NewEV->insertInto(Pred, NewBC->getIterator());
      } else {
        NewEV->insertInto(Pred, NewRet->getIterator());
        Op = NewEV;
      }
    }

    if (PHINode *PN = dyn_cast<PHINode>(V)) {
      if (PN->getParent() == BB) {
        if (NewEV) {
          NewEV->setOperand(0, PN->getIncomingValueForBlock(Pred));
        } else if (NewBC)
          NewBC->setOperand(0, PN->getIncomingValueForBlock(Pred));
        else
          Op = PN->getIncomingValueForBlock(Pred);
      }
    }
  }

  // Update any PHI nodes in the returning block to realize that we no
  // longer branch to them.
  BB->removePredecessor(Pred);
  UncondBranch->eraseFromParent();

  if (DTU)
    DTU->applyUpdates({{DominatorTree::Delete, Pred, BB}});

  return cast<ReturnInst>(NewRet);
}

Instruction *llvm::SplitBlockAndInsertIfThen(Value *Cond,
                                             BasicBlock::iterator SplitBefore,
                                             bool Unreachable,
                                             MDNode *BranchWeights,
                                             DomTreeUpdater *DTU, LoopInfo *LI,
                                             BasicBlock *ThenBlock) {
  SplitBlockAndInsertIfThenElse(
      Cond, SplitBefore, &ThenBlock, /* ElseBlock */ nullptr,
      /* UnreachableThen */ Unreachable,
      /* UnreachableElse */ false, BranchWeights, DTU, LI);
  return ThenBlock->getTerminator();
}

Instruction *llvm::SplitBlockAndInsertIfElse(Value *Cond,
                                             BasicBlock::iterator SplitBefore,
                                             bool Unreachable,
                                             MDNode *BranchWeights,
                                             DomTreeUpdater *DTU, LoopInfo *LI,
                                             BasicBlock *ElseBlock) {
  SplitBlockAndInsertIfThenElse(
      Cond, SplitBefore, /* ThenBlock */ nullptr, &ElseBlock,
      /* UnreachableThen */ false,
      /* UnreachableElse */ Unreachable, BranchWeights, DTU, LI);
  return ElseBlock->getTerminator();
}

void llvm::SplitBlockAndInsertIfThenElse(Value *Cond, BasicBlock::iterator SplitBefore,
                                         Instruction **ThenTerm,
                                         Instruction **ElseTerm,
                                         MDNode *BranchWeights,
                                         DomTreeUpdater *DTU, LoopInfo *LI) {
  BasicBlock *ThenBlock = nullptr;
  BasicBlock *ElseBlock = nullptr;
  SplitBlockAndInsertIfThenElse(
      Cond, SplitBefore, &ThenBlock, &ElseBlock, /* UnreachableThen */ false,
      /* UnreachableElse */ false, BranchWeights, DTU, LI);

  *ThenTerm = ThenBlock->getTerminator();
  *ElseTerm = ElseBlock->getTerminator();
}

void llvm::SplitBlockAndInsertIfThenElse(
    Value *Cond, BasicBlock::iterator SplitBefore, BasicBlock **ThenBlock,
    BasicBlock **ElseBlock, bool UnreachableThen, bool UnreachableElse,
    MDNode *BranchWeights, DomTreeUpdater *DTU, LoopInfo *LI) {
  assert((ThenBlock || ElseBlock) &&
         "At least one branch block must be created");
  assert((!UnreachableThen || !UnreachableElse) &&
         "Split block tail must be reachable");

  SmallVector<DominatorTree::UpdateType, 8> Updates;
  SmallPtrSet<BasicBlock *, 8> UniqueOrigSuccessors;
  BasicBlock *Head = SplitBefore->getParent();
  if (DTU) {
    UniqueOrigSuccessors.insert(succ_begin(Head), succ_end(Head));
    Updates.reserve(4 + 2 * UniqueOrigSuccessors.size());
  }

  LLVMContext &C = Head->getContext();
  BasicBlock *Tail = Head->splitBasicBlock(SplitBefore);
  BasicBlock *TrueBlock = Tail;
  BasicBlock *FalseBlock = Tail;
  bool ThenToTailEdge = false;
  bool ElseToTailEdge = false;

  // Encapsulate the logic around creation/insertion/etc of a new block.
  auto handleBlock = [&](BasicBlock **PBB, bool Unreachable, BasicBlock *&BB,
                         bool &ToTailEdge) {
    if (PBB == nullptr)
      return; // Do not create/insert a block.

    if (*PBB)
      BB = *PBB; // Caller supplied block, use it.
    else {
      // Create a new block.
      BB = BasicBlock::Create(C, "", Head->getParent(), Tail);
      if (Unreachable)
        (void)new UnreachableInst(C, BB);
      else {
        (void)BranchInst::Create(Tail, BB);
        ToTailEdge = true;
      }
      BB->getTerminator()->setDebugLoc(SplitBefore->getDebugLoc());
      // Pass the new block back to the caller.
      *PBB = BB;
    }
  };

  handleBlock(ThenBlock, UnreachableThen, TrueBlock, ThenToTailEdge);
  handleBlock(ElseBlock, UnreachableElse, FalseBlock, ElseToTailEdge);

  Instruction *HeadOldTerm = Head->getTerminator();
  BranchInst *HeadNewTerm =
      BranchInst::Create(/*ifTrue*/ TrueBlock, /*ifFalse*/ FalseBlock, Cond);
  HeadNewTerm->setMetadata(LLVMContext::MD_prof, BranchWeights);
  ReplaceInstWithInst(HeadOldTerm, HeadNewTerm);

  if (DTU) {
    Updates.emplace_back(DominatorTree::Insert, Head, TrueBlock);
    Updates.emplace_back(DominatorTree::Insert, Head, FalseBlock);
    if (ThenToTailEdge)
      Updates.emplace_back(DominatorTree::Insert, TrueBlock, Tail);
    if (ElseToTailEdge)
      Updates.emplace_back(DominatorTree::Insert, FalseBlock, Tail);
    for (BasicBlock *UniqueOrigSuccessor : UniqueOrigSuccessors)
      Updates.emplace_back(DominatorTree::Insert, Tail, UniqueOrigSuccessor);
    for (BasicBlock *UniqueOrigSuccessor : UniqueOrigSuccessors)
      Updates.emplace_back(DominatorTree::Delete, Head, UniqueOrigSuccessor);
    DTU->applyUpdates(Updates);
  }

  if (LI) {
    if (Loop *L = LI->getLoopFor(Head); L) {
      if (ThenToTailEdge)
        L->addBasicBlockToLoop(TrueBlock, *LI);
      if (ElseToTailEdge)
        L->addBasicBlockToLoop(FalseBlock, *LI);
      L->addBasicBlockToLoop(Tail, *LI);
    }
  }
}

std::pair<Instruction*, Value*>
llvm::SplitBlockAndInsertSimpleForLoop(Value *End, Instruction *SplitBefore) {
  BasicBlock *LoopPred = SplitBefore->getParent();
  BasicBlock *LoopBody = SplitBlock(SplitBefore->getParent(), SplitBefore);
  BasicBlock *LoopExit = SplitBlock(SplitBefore->getParent(), SplitBefore);

  auto *Ty = End->getType();
  auto &DL = SplitBefore->getDataLayout();
  const unsigned Bitwidth = DL.getTypeSizeInBits(Ty);

  IRBuilder<> Builder(LoopBody->getTerminator());
  auto *IV = Builder.CreatePHI(Ty, 2, "iv");
  auto *IVNext =
    Builder.CreateAdd(IV, ConstantInt::get(Ty, 1), IV->getName() + ".next",
                      /*HasNUW=*/true, /*HasNSW=*/Bitwidth != 2);
  auto *IVCheck = Builder.CreateICmpEQ(IVNext, End,
                                       IV->getName() + ".check");
  Builder.CreateCondBr(IVCheck, LoopExit, LoopBody);
  LoopBody->getTerminator()->eraseFromParent();

  // Populate the IV PHI.
  IV->addIncoming(ConstantInt::get(Ty, 0), LoopPred);
  IV->addIncoming(IVNext, LoopBody);

  return std::make_pair(LoopBody->getFirstNonPHI(), IV);
}

void llvm::SplitBlockAndInsertForEachLane(ElementCount EC,
     Type *IndexTy, Instruction *InsertBefore,
     std::function<void(IRBuilderBase&, Value*)> Func) {

  IRBuilder<> IRB(InsertBefore);

  if (EC.isScalable()) {
    Value *NumElements = IRB.CreateElementCount(IndexTy, EC);

    auto [BodyIP, Index] =
      SplitBlockAndInsertSimpleForLoop(NumElements, InsertBefore);

    IRB.SetInsertPoint(BodyIP);
    Func(IRB, Index);
    return;
  }

  unsigned Num = EC.getFixedValue();
  for (unsigned Idx = 0; Idx < Num; ++Idx) {
    IRB.SetInsertPoint(InsertBefore);
    Func(IRB, ConstantInt::get(IndexTy, Idx));
  }
}

void llvm::SplitBlockAndInsertForEachLane(
    Value *EVL, Instruction *InsertBefore,
    std::function<void(IRBuilderBase &, Value *)> Func) {

  IRBuilder<> IRB(InsertBefore);
  Type *Ty = EVL->getType();

  if (!isa<ConstantInt>(EVL)) {
    auto [BodyIP, Index] = SplitBlockAndInsertSimpleForLoop(EVL, InsertBefore);
    IRB.SetInsertPoint(BodyIP);
    Func(IRB, Index);
    return;
  }

  unsigned Num = cast<ConstantInt>(EVL)->getZExtValue();
  for (unsigned Idx = 0; Idx < Num; ++Idx) {
    IRB.SetInsertPoint(InsertBefore);
    Func(IRB, ConstantInt::get(Ty, Idx));
  }
}

BranchInst *llvm::GetIfCondition(BasicBlock *BB, BasicBlock *&IfTrue,
                                 BasicBlock *&IfFalse) {
  PHINode *SomePHI = dyn_cast<PHINode>(BB->begin());
  BasicBlock *Pred1 = nullptr;
  BasicBlock *Pred2 = nullptr;

  if (SomePHI) {
    if (SomePHI->getNumIncomingValues() != 2)
      return nullptr;
    Pred1 = SomePHI->getIncomingBlock(0);
    Pred2 = SomePHI->getIncomingBlock(1);
  } else {
    pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
    if (PI == PE) // No predecessor
      return nullptr;
    Pred1 = *PI++;
    if (PI == PE) // Only one predecessor
      return nullptr;
    Pred2 = *PI++;
    if (PI != PE) // More than two predecessors
      return nullptr;
  }

  // We can only handle branches.  Other control flow will be lowered to
  // branches if possible anyway.
  BranchInst *Pred1Br = dyn_cast<BranchInst>(Pred1->getTerminator());
  BranchInst *Pred2Br = dyn_cast<BranchInst>(Pred2->getTerminator());
  if (!Pred1Br || !Pred2Br)
    return nullptr;

  // Eliminate code duplication by ensuring that Pred1Br is conditional if
  // either are.
  if (Pred2Br->isConditional()) {
    // If both branches are conditional, we don't have an "if statement".  In
    // reality, we could transform this case, but since the condition will be
    // required anyway, we stand no chance of eliminating it, so the xform is
    // probably not profitable.
    if (Pred1Br->isConditional())
      return nullptr;

    std::swap(Pred1, Pred2);
    std::swap(Pred1Br, Pred2Br);
  }

  if (Pred1Br->isConditional()) {
    // The only thing we have to watch out for here is to make sure that Pred2
    // doesn't have incoming edges from other blocks.  If it does, the condition
    // doesn't dominate BB.
    if (!Pred2->getSinglePredecessor())
      return nullptr;

    // If we found a conditional branch predecessor, make sure that it branches
    // to BB and Pred2Br.  If it doesn't, this isn't an "if statement".
    if (Pred1Br->getSuccessor(0) == BB &&
        Pred1Br->getSuccessor(1) == Pred2) {
      IfTrue = Pred1;
      IfFalse = Pred2;
    } else if (Pred1Br->getSuccessor(0) == Pred2 &&
               Pred1Br->getSuccessor(1) == BB) {
      IfTrue = Pred2;
      IfFalse = Pred1;
    } else {
      // We know that one arm of the conditional goes to BB, so the other must
      // go somewhere unrelated, and this must not be an "if statement".
      return nullptr;
    }

    return Pred1Br;
  }

  // Ok, if we got here, both predecessors end with an unconditional branch to
  // BB.  Don't panic!  If both blocks only have a single (identical)
  // predecessor, and THAT is a conditional branch, then we're all ok!
  BasicBlock *CommonPred = Pred1->getSinglePredecessor();
  if (CommonPred == nullptr || CommonPred != Pred2->getSinglePredecessor())
    return nullptr;

  // Otherwise, if this is a conditional branch, then we can use it!
  BranchInst *BI = dyn_cast<BranchInst>(CommonPred->getTerminator());
  if (!BI) return nullptr;

  assert(BI->isConditional() && "Two successors but not conditional?");
  if (BI->getSuccessor(0) == Pred1) {
    IfTrue = Pred1;
    IfFalse = Pred2;
  } else {
    IfTrue = Pred2;
    IfFalse = Pred1;
  }
  return BI;
}

// After creating a control flow hub, the operands of PHINodes in an outgoing
// block Out no longer match the predecessors of that block. Predecessors of Out
// that are incoming blocks to the hub are now replaced by just one edge from
// the hub. To match this new control flow, the corresponding values from each
// PHINode must now be moved a new PHINode in the first guard block of the hub.
//
// This operation cannot be performed with SSAUpdater, because it involves one
// new use: If the block Out is in the list of Incoming blocks, then the newly
// created PHI in the Hub will use itself along that edge from Out to Hub.
static void reconnectPhis(BasicBlock *Out, BasicBlock *GuardBlock,
                          const SetVector<BasicBlock *> &Incoming,
                          BasicBlock *FirstGuardBlock) {
  auto I = Out->begin();
  while (I != Out->end() && isa<PHINode>(I)) {
    auto Phi = cast<PHINode>(I);
    auto NewPhi =
        PHINode::Create(Phi->getType(), Incoming.size(),
                        Phi->getName() + ".moved", FirstGuardBlock->begin());
    for (auto *In : Incoming) {
      Value *V = UndefValue::get(Phi->getType());
      if (In == Out) {
        V = NewPhi;
      } else if (Phi->getBasicBlockIndex(In) != -1) {
        V = Phi->removeIncomingValue(In, false);
      }
      NewPhi->addIncoming(V, In);
    }
    assert(NewPhi->getNumIncomingValues() == Incoming.size());
    if (Phi->getNumOperands() == 0) {
      Phi->replaceAllUsesWith(NewPhi);
      I = Phi->eraseFromParent();
      continue;
    }
    Phi->addIncoming(NewPhi, GuardBlock);
    ++I;
  }
}

using BBPredicates = DenseMap<BasicBlock *, Instruction *>;
using BBSetVector = SetVector<BasicBlock *>;

// Redirects the terminator of the incoming block to the first guard
// block in the hub. The condition of the original terminator (if it
// was conditional) and its original successors are returned as a
// tuple <condition, succ0, succ1>. The function additionally filters
// out successors that are not in the set of outgoing blocks.
//
// - condition is non-null iff the branch is conditional.
// - Succ1 is non-null iff the sole/taken target is an outgoing block.
// - Succ2 is non-null iff condition is non-null and the fallthrough
//         target is an outgoing block.
static std::tuple<Value *, BasicBlock *, BasicBlock *>
redirectToHub(BasicBlock *BB, BasicBlock *FirstGuardBlock,
              const BBSetVector &Outgoing) {
  assert(isa<BranchInst>(BB->getTerminator()) &&
         "Only support branch terminator.");
  auto Branch = cast<BranchInst>(BB->getTerminator());
  auto Condition = Branch->isConditional() ? Branch->getCondition() : nullptr;

  BasicBlock *Succ0 = Branch->getSuccessor(0);
  BasicBlock *Succ1 = nullptr;
  Succ0 = Outgoing.count(Succ0) ? Succ0 : nullptr;

  if (Branch->isUnconditional()) {
    Branch->setSuccessor(0, FirstGuardBlock);
    assert(Succ0);
  } else {
    Succ1 = Branch->getSuccessor(1);
    Succ1 = Outgoing.count(Succ1) ? Succ1 : nullptr;
    assert(Succ0 || Succ1);
    if (Succ0 && !Succ1) {
      Branch->setSuccessor(0, FirstGuardBlock);
    } else if (Succ1 && !Succ0) {
      Branch->setSuccessor(1, FirstGuardBlock);
    } else {
      Branch->eraseFromParent();
      BranchInst::Create(FirstGuardBlock, BB);
    }
  }

  assert(Succ0 || Succ1);
  return std::make_tuple(Condition, Succ0, Succ1);
}
// Setup the branch instructions for guard blocks.
//
// Each guard block terminates in a conditional branch that transfers
// control to the corresponding outgoing block or the next guard
// block. The last guard block has two outgoing blocks as successors
// since the condition for the final outgoing block is trivially
// true. So we create one less block (including the first guard block)
// than the number of outgoing blocks.
static void setupBranchForGuard(SmallVectorImpl<BasicBlock *> &GuardBlocks,
                                const BBSetVector &Outgoing,
                                BBPredicates &GuardPredicates) {
  // To help keep the loop simple, temporarily append the last
  // outgoing block to the list of guard blocks.
  GuardBlocks.push_back(Outgoing.back());

  for (int i = 0, e = GuardBlocks.size() - 1; i != e; ++i) {
    auto Out = Outgoing[i];
    assert(GuardPredicates.count(Out));
    BranchInst::Create(Out, GuardBlocks[i + 1], GuardPredicates[Out],
                       GuardBlocks[i]);
  }

  // Remove the last block from the guard list.
  GuardBlocks.pop_back();
}

/// We are using one integer to represent the block we are branching to. Then at
/// each guard block, the predicate was calcuated using a simple `icmp eq`.
static void calcPredicateUsingInteger(
    const BBSetVector &Incoming, const BBSetVector &Outgoing,
    SmallVectorImpl<BasicBlock *> &GuardBlocks, BBPredicates &GuardPredicates) {
  auto &Context = Incoming.front()->getContext();
  auto FirstGuardBlock = GuardBlocks.front();

  auto Phi = PHINode::Create(Type::getInt32Ty(Context), Incoming.size(),
                             "merged.bb.idx", FirstGuardBlock);

  for (auto In : Incoming) {
    Value *Condition;
    BasicBlock *Succ0;
    BasicBlock *Succ1;
    std::tie(Condition, Succ0, Succ1) =
        redirectToHub(In, FirstGuardBlock, Outgoing);
    Value *IncomingId = nullptr;
    if (Succ0 && Succ1) {
      // target_bb_index = Condition ? index_of_succ0 : index_of_succ1.
      auto Succ0Iter = find(Outgoing, Succ0);
      auto Succ1Iter = find(Outgoing, Succ1);
      Value *Id0 = ConstantInt::get(Type::getInt32Ty(Context),
                                    std::distance(Outgoing.begin(), Succ0Iter));
      Value *Id1 = ConstantInt::get(Type::getInt32Ty(Context),
                                    std::distance(Outgoing.begin(), Succ1Iter));
      IncomingId = SelectInst::Create(Condition, Id0, Id1, "target.bb.idx",
                                      In->getTerminator()->getIterator());
    } else {
      // Get the index of the non-null successor.
      auto SuccIter = Succ0 ? find(Outgoing, Succ0) : find(Outgoing, Succ1);
      IncomingId = ConstantInt::get(Type::getInt32Ty(Context),
                                    std::distance(Outgoing.begin(), SuccIter));
    }
    Phi->addIncoming(IncomingId, In);
  }

  for (int i = 0, e = Outgoing.size() - 1; i != e; ++i) {
    auto Out = Outgoing[i];
    auto Cmp = ICmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_EQ, Phi,
                                ConstantInt::get(Type::getInt32Ty(Context), i),
                                Out->getName() + ".predicate", GuardBlocks[i]);
    GuardPredicates[Out] = Cmp;
  }
}

/// We record the predicate of each outgoing block using a phi of boolean.
static void calcPredicateUsingBooleans(
    const BBSetVector &Incoming, const BBSetVector &Outgoing,
    SmallVectorImpl<BasicBlock *> &GuardBlocks, BBPredicates &GuardPredicates,
    SmallVectorImpl<WeakVH> &DeletionCandidates) {
  auto &Context = Incoming.front()->getContext();
  auto BoolTrue = ConstantInt::getTrue(Context);
  auto BoolFalse = ConstantInt::getFalse(Context);
  auto FirstGuardBlock = GuardBlocks.front();

  // The predicate for the last outgoing is trivially true, and so we
  // process only the first N-1 successors.
  for (int i = 0, e = Outgoing.size() - 1; i != e; ++i) {
    auto Out = Outgoing[i];
    LLVM_DEBUG(dbgs() << "Creating guard for " << Out->getName() << "\n");

    auto Phi =
        PHINode::Create(Type::getInt1Ty(Context), Incoming.size(),
                        StringRef("Guard.") + Out->getName(), FirstGuardBlock);
    GuardPredicates[Out] = Phi;
  }

  for (auto *In : Incoming) {
    Value *Condition;
    BasicBlock *Succ0;
    BasicBlock *Succ1;
    std::tie(Condition, Succ0, Succ1) =
        redirectToHub(In, FirstGuardBlock, Outgoing);

    // Optimization: Consider an incoming block A with both successors
    // Succ0 and Succ1 in the set of outgoing blocks. The predicates
    // for Succ0 and Succ1 complement each other. If Succ0 is visited
    // first in the loop below, control will branch to Succ0 using the
    // corresponding predicate. But if that branch is not taken, then
    // control must reach Succ1, which means that the incoming value of
    // the predicate from `In` is true for Succ1.
    bool OneSuccessorDone = false;
    for (int i = 0, e = Outgoing.size() - 1; i != e; ++i) {
      auto Out = Outgoing[i];
      PHINode *Phi = cast<PHINode>(GuardPredicates[Out]);
      if (Out != Succ0 && Out != Succ1) {
        Phi->addIncoming(BoolFalse, In);
      } else if (!Succ0 || !Succ1 || OneSuccessorDone) {
        // Optimization: When only one successor is an outgoing block,
        // the incoming predicate from `In` is always true.
        Phi->addIncoming(BoolTrue, In);
      } else {
        assert(Succ0 && Succ1);
        if (Out == Succ0) {
          Phi->addIncoming(Condition, In);
        } else {
          auto Inverted = invertCondition(Condition);
          DeletionCandidates.push_back(Condition);
          Phi->addIncoming(Inverted, In);
        }
        OneSuccessorDone = true;
      }
    }
  }
}

// Capture the existing control flow as guard predicates, and redirect
// control flow from \p Incoming block through the \p GuardBlocks to the
// \p Outgoing blocks.
//
// There is one guard predicate for each outgoing block OutBB. The
// predicate represents whether the hub should transfer control flow
// to OutBB. These predicates are NOT ORTHOGONAL. The Hub evaluates
// them in the same order as the Outgoing set-vector, and control
// branches to the first outgoing block whose predicate evaluates to true.
static void
convertToGuardPredicates(SmallVectorImpl<BasicBlock *> &GuardBlocks,
                         SmallVectorImpl<WeakVH> &DeletionCandidates,
                         const BBSetVector &Incoming,
                         const BBSetVector &Outgoing, const StringRef Prefix,
                         std::optional<unsigned> MaxControlFlowBooleans) {
  BBPredicates GuardPredicates;
  auto F = Incoming.front()->getParent();

  for (int i = 0, e = Outgoing.size() - 1; i != e; ++i)
    GuardBlocks.push_back(
        BasicBlock::Create(F->getContext(), Prefix + ".guard", F));

  // When we are using an integer to record which target block to jump to, we
  // are creating less live values, actually we are using one single integer to
  // store the index of the target block. When we are using booleans to store
  // the branching information, we need (N-1) boolean values, where N is the
  // number of outgoing block.
  if (!MaxControlFlowBooleans || Outgoing.size() <= *MaxControlFlowBooleans)
    calcPredicateUsingBooleans(Incoming, Outgoing, GuardBlocks, GuardPredicates,
                               DeletionCandidates);
  else
    calcPredicateUsingInteger(Incoming, Outgoing, GuardBlocks, GuardPredicates);

  setupBranchForGuard(GuardBlocks, Outgoing, GuardPredicates);
}

BasicBlock *llvm::CreateControlFlowHub(
    DomTreeUpdater *DTU, SmallVectorImpl<BasicBlock *> &GuardBlocks,
    const BBSetVector &Incoming, const BBSetVector &Outgoing,
    const StringRef Prefix, std::optional<unsigned> MaxControlFlowBooleans) {
  if (Outgoing.size() < 2)
    return Outgoing.front();

  SmallVector<DominatorTree::UpdateType, 16> Updates;
  if (DTU) {
    for (auto *In : Incoming) {
      for (auto Succ : successors(In))
        if (Outgoing.count(Succ))
          Updates.push_back({DominatorTree::Delete, In, Succ});
    }
  }

  SmallVector<WeakVH, 8> DeletionCandidates;
  convertToGuardPredicates(GuardBlocks, DeletionCandidates, Incoming, Outgoing,
                           Prefix, MaxControlFlowBooleans);
  auto FirstGuardBlock = GuardBlocks.front();
  
  // Update the PHINodes in each outgoing block to match the new control flow.
  for (int i = 0, e = GuardBlocks.size(); i != e; ++i)
    reconnectPhis(Outgoing[i], GuardBlocks[i], Incoming, FirstGuardBlock);

  reconnectPhis(Outgoing.back(), GuardBlocks.back(), Incoming, FirstGuardBlock);

  if (DTU) {
    int NumGuards = GuardBlocks.size();
    assert((int)Outgoing.size() == NumGuards + 1);

    for (auto In : Incoming)
      Updates.push_back({DominatorTree::Insert, In, FirstGuardBlock});

    for (int i = 0; i != NumGuards - 1; ++i) {
      Updates.push_back({DominatorTree::Insert, GuardBlocks[i], Outgoing[i]});
      Updates.push_back(
          {DominatorTree::Insert, GuardBlocks[i], GuardBlocks[i + 1]});
    }
    Updates.push_back({DominatorTree::Insert, GuardBlocks[NumGuards - 1],
                       Outgoing[NumGuards - 1]});
    Updates.push_back({DominatorTree::Insert, GuardBlocks[NumGuards - 1],
                       Outgoing[NumGuards]});
    DTU->applyUpdates(Updates);
  }

  for (auto I : DeletionCandidates) {
    if (I->use_empty())
      if (auto Inst = dyn_cast_or_null<Instruction>(I))
        Inst->eraseFromParent();
  }

  return FirstGuardBlock;
}

void llvm::InvertBranch(BranchInst *PBI, IRBuilderBase &Builder) {
  Value *NewCond = PBI->getCondition();
  // If this is a "cmp" instruction, only used for branching (and nowhere
  // else), then we can simply invert the predicate.
  if (NewCond->hasOneUse() && isa<CmpInst>(NewCond)) {
    CmpInst *CI = cast<CmpInst>(NewCond);
    CI->setPredicate(CI->getInversePredicate());
  } else
    NewCond = Builder.CreateNot(NewCond, NewCond->getName() + ".not");

  PBI->setCondition(NewCond);
  PBI->swapSuccessors();
}

bool llvm::hasOnlySimpleTerminator(const Function &F) {
  for (auto &BB : F) {
    auto *Term = BB.getTerminator();
    if (!(isa<ReturnInst>(Term) || isa<UnreachableInst>(Term) ||
          isa<BranchInst>(Term)))
      return false;
  }
  return true;
}

bool llvm::isPresplitCoroSuspendExitEdge(const BasicBlock &Src,
                                         const BasicBlock &Dest) {
  assert(Src.getParent() == Dest.getParent());
  if (!Src.getParent()->isPresplitCoroutine())
    return false;
  if (auto *SW = dyn_cast<SwitchInst>(Src.getTerminator()))
    if (auto *Intr = dyn_cast<IntrinsicInst>(SW->getCondition()))
      return Intr->getIntrinsicID() == Intrinsic::coro_suspend &&
             SW->getDefaultDest() == &Dest;
  return false;
}
