//===- LoopDeletion.cpp - Dead Loop Deletion Pass ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Dead Loop Deletion Pass. This pass is responsible
// for eliminating loops with non-infinite computable trip counts that have no
// side effects or volatile instructions, and do not contribute to the
// computation of the function's return value.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Dominators.h"

#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

#define DEBUG_TYPE "loop-delete"

STATISTIC(NumDeleted, "Number of loops deleted");
STATISTIC(NumBackedgesBroken,
          "Number of loops for which we managed to break the backedge");

static cl::opt<bool> EnableSymbolicExecution(
    "loop-deletion-enable-symbolic-execution", cl::Hidden, cl::init(true),
    cl::desc("Break backedge through symbolic execution of 1st iteration "
             "attempting to prove that the backedge is never taken"));

enum class LoopDeletionResult {
  Unmodified,
  Modified,
  Deleted,
};

static LoopDeletionResult merge(LoopDeletionResult A, LoopDeletionResult B) {
  if (A == LoopDeletionResult::Deleted || B == LoopDeletionResult::Deleted)
    return LoopDeletionResult::Deleted;
  if (A == LoopDeletionResult::Modified || B == LoopDeletionResult::Modified)
    return LoopDeletionResult::Modified;
  return LoopDeletionResult::Unmodified;
}

/// Determines if a loop is dead.
///
/// This assumes that we've already checked for unique exit and exiting blocks,
/// and that the code is in LCSSA form.
static bool isLoopDead(Loop *L, ScalarEvolution &SE,
                       SmallVectorImpl<BasicBlock *> &ExitingBlocks,
                       BasicBlock *ExitBlock, bool &Changed,
                       BasicBlock *Preheader, LoopInfo &LI) {
  // Make sure that all PHI entries coming from the loop are loop invariant.
  // Because the code is in LCSSA form, any values used outside of the loop
  // must pass through a PHI in the exit block, meaning that this check is
  // sufficient to guarantee that no loop-variant values are used outside
  // of the loop.
  bool AllEntriesInvariant = true;
  bool AllOutgoingValuesSame = true;
  if (ExitBlock) {
    for (PHINode &P : ExitBlock->phis()) {
      Value *incoming = P.getIncomingValueForBlock(ExitingBlocks[0]);

      // Make sure all exiting blocks produce the same incoming value for the
      // block. If there are different incoming values for different exiting
      // blocks, then it is impossible to statically determine which value
      // should be used.
      AllOutgoingValuesSame =
          all_of(ArrayRef(ExitingBlocks).slice(1), [&](BasicBlock *BB) {
            return incoming == P.getIncomingValueForBlock(BB);
          });

      if (!AllOutgoingValuesSame)
        break;

      if (Instruction *I = dyn_cast<Instruction>(incoming)) {
        if (!L->makeLoopInvariant(I, Changed, Preheader->getTerminator(),
                                  /*MSSAU=*/nullptr, &SE)) {
          AllEntriesInvariant = false;
          break;
        }
      }
    }
  }

  if (!AllEntriesInvariant || !AllOutgoingValuesSame)
    return false;

  // Make sure that no instructions in the block have potential side-effects.
  // This includes instructions that could write to memory, and loads that are
  // marked volatile.
  for (const auto &I : L->blocks())
    if (any_of(*I, [](Instruction &I) {
          return I.mayHaveSideEffects() && !I.isDroppable();
        }))
      return false;

  // The loop or any of its sub-loops looping infinitely is legal. The loop can
  // only be considered dead if either
  // a. the function is mustprogress.
  // b. all (sub-)loops are mustprogress or have a known trip-count.
  if (L->getHeader()->getParent()->mustProgress())
    return true;

  LoopBlocksRPO RPOT(L);
  RPOT.perform(&LI);
  // If the loop contains an irreducible cycle, it may loop infinitely.
  if (containsIrreducibleCFG<const BasicBlock *>(RPOT, LI))
    return false;

  SmallVector<Loop *, 8> WorkList;
  WorkList.push_back(L);
  while (!WorkList.empty()) {
    Loop *Current = WorkList.pop_back_val();
    if (hasMustProgress(Current))
      continue;

    const SCEV *S = SE.getConstantMaxBackedgeTakenCount(Current);
    if (isa<SCEVCouldNotCompute>(S)) {
      LLVM_DEBUG(
          dbgs() << "Could not compute SCEV MaxBackedgeTakenCount and was "
                    "not required to make progress.\n");
      return false;
    }
    WorkList.append(Current->begin(), Current->end());
  }
  return true;
}

/// This function returns true if there is no viable path from the
/// entry block to the header of \p L. Right now, it only does
/// a local search to save compile time.
static bool isLoopNeverExecuted(Loop *L) {
  using namespace PatternMatch;

  auto *Preheader = L->getLoopPreheader();
  // TODO: We can relax this constraint, since we just need a loop
  // predecessor.
  assert(Preheader && "Needs preheader!");

  if (Preheader->isEntryBlock())
    return false;
  // All predecessors of the preheader should have a constant conditional
  // branch, with the loop's preheader as not-taken.
  for (auto *Pred: predecessors(Preheader)) {
    BasicBlock *Taken, *NotTaken;
    ConstantInt *Cond;
    if (!match(Pred->getTerminator(),
               m_Br(m_ConstantInt(Cond), Taken, NotTaken)))
      return false;
    if (!Cond->getZExtValue())
      std::swap(Taken, NotTaken);
    if (Taken == Preheader)
      return false;
  }
  assert(!pred_empty(Preheader) &&
         "Preheader should have predecessors at this point!");
  // All the predecessors have the loop preheader as not-taken target.
  return true;
}

static Value *
getValueOnFirstIteration(Value *V, DenseMap<Value *, Value *> &FirstIterValue,
                         const SimplifyQuery &SQ) {
  // Quick hack: do not flood cache with non-instruction values.
  if (!isa<Instruction>(V))
    return V;
  // Do we already know cached result?
  auto Existing = FirstIterValue.find(V);
  if (Existing != FirstIterValue.end())
    return Existing->second;
  Value *FirstIterV = nullptr;
  if (auto *BO = dyn_cast<BinaryOperator>(V)) {
    Value *LHS =
        getValueOnFirstIteration(BO->getOperand(0), FirstIterValue, SQ);
    Value *RHS =
        getValueOnFirstIteration(BO->getOperand(1), FirstIterValue, SQ);
    FirstIterV = simplifyBinOp(BO->getOpcode(), LHS, RHS, SQ);
  } else if (auto *Cmp = dyn_cast<ICmpInst>(V)) {
    Value *LHS =
        getValueOnFirstIteration(Cmp->getOperand(0), FirstIterValue, SQ);
    Value *RHS =
        getValueOnFirstIteration(Cmp->getOperand(1), FirstIterValue, SQ);
    FirstIterV = simplifyICmpInst(Cmp->getPredicate(), LHS, RHS, SQ);
  } else if (auto *Select = dyn_cast<SelectInst>(V)) {
    Value *Cond =
        getValueOnFirstIteration(Select->getCondition(), FirstIterValue, SQ);
    if (auto *C = dyn_cast<ConstantInt>(Cond)) {
      auto *Selected = C->isAllOnesValue() ? Select->getTrueValue()
                                           : Select->getFalseValue();
      FirstIterV = getValueOnFirstIteration(Selected, FirstIterValue, SQ);
    }
  }
  if (!FirstIterV)
    FirstIterV = V;
  FirstIterValue[V] = FirstIterV;
  return FirstIterV;
}

// Try to prove that one of conditions that dominates the latch must exit on 1st
// iteration.
static bool canProveExitOnFirstIteration(Loop *L, DominatorTree &DT,
                                         LoopInfo &LI) {
  // Disabled by option.
  if (!EnableSymbolicExecution)
    return false;

  BasicBlock *Predecessor = L->getLoopPredecessor();
  BasicBlock *Latch = L->getLoopLatch();

  if (!Predecessor || !Latch)
    return false;

  LoopBlocksRPO RPOT(L);
  RPOT.perform(&LI);

  // For the optimization to be correct, we need RPOT to have a property that
  // each block is processed after all its predecessors, which may only be
  // violated for headers of the current loop and all nested loops. Irreducible
  // CFG provides multiple ways to break this assumption, so we do not want to
  // deal with it.
  if (containsIrreducibleCFG<const BasicBlock *>(RPOT, LI))
    return false;

  BasicBlock *Header = L->getHeader();
  // Blocks that are reachable on the 1st iteration.
  SmallPtrSet<BasicBlock *, 4> LiveBlocks;
  // Edges that are reachable on the 1st iteration.
  DenseSet<BasicBlockEdge> LiveEdges;
  LiveBlocks.insert(Header);

  SmallPtrSet<BasicBlock *, 4> Visited;
  auto MarkLiveEdge = [&](BasicBlock *From, BasicBlock *To) {
    assert(LiveBlocks.count(From) && "Must be live!");
    assert((LI.isLoopHeader(To) || !Visited.count(To)) &&
           "Only canonical backedges are allowed. Irreducible CFG?");
    assert((LiveBlocks.count(To) || !Visited.count(To)) &&
           "We already discarded this block as dead!");
    LiveBlocks.insert(To);
    LiveEdges.insert({ From, To });
  };

  auto MarkAllSuccessorsLive = [&](BasicBlock *BB) {
    for (auto *Succ : successors(BB))
      MarkLiveEdge(BB, Succ);
  };

  // Check if there is only one value coming from all live predecessor blocks.
  // Note that because we iterate in RPOT, we have already visited all its
  // (non-latch) predecessors.
  auto GetSoleInputOnFirstIteration = [&](PHINode & PN)->Value * {
    BasicBlock *BB = PN.getParent();
    bool HasLivePreds = false;
    (void)HasLivePreds;
    if (BB == Header)
      return PN.getIncomingValueForBlock(Predecessor);
    Value *OnlyInput = nullptr;
    for (auto *Pred : predecessors(BB))
      if (LiveEdges.count({ Pred, BB })) {
        HasLivePreds = true;
        Value *Incoming = PN.getIncomingValueForBlock(Pred);
        // Skip poison. If they are present, we can assume they are equal to
        // the non-poison input.
        if (isa<PoisonValue>(Incoming))
          continue;
        // Two inputs.
        if (OnlyInput && OnlyInput != Incoming)
          return nullptr;
        OnlyInput = Incoming;
      }

    assert(HasLivePreds && "No live predecessors?");
    // If all incoming live value were poison, return poison.
    return OnlyInput ? OnlyInput : PoisonValue::get(PN.getType());
  };
  DenseMap<Value *, Value *> FirstIterValue;

  // Use the following algorithm to prove we never take the latch on the 1st
  // iteration:
  // 1. Traverse in topological order, so that whenever we visit a block, all
  //    its predecessors are already visited.
  // 2. If we can prove that the block may have only 1 predecessor on the 1st
  //    iteration, map all its phis onto input from this predecessor.
  // 3a. If we can prove which successor of out block is taken on the 1st
  //     iteration, mark this successor live.
  // 3b. If we cannot prove it, conservatively assume that all successors are
  //     live.
  auto &DL = Header->getDataLayout();
  const SimplifyQuery SQ(DL);
  for (auto *BB : RPOT) {
    Visited.insert(BB);

    // This block is not reachable on the 1st iterations.
    if (!LiveBlocks.count(BB))
      continue;

    // Skip inner loops.
    if (LI.getLoopFor(BB) != L) {
      MarkAllSuccessorsLive(BB);
      continue;
    }

    // If Phi has only one input from all live input blocks, use it.
    for (auto &PN : BB->phis()) {
      if (!PN.getType()->isIntegerTy())
        continue;
      auto *Incoming = GetSoleInputOnFirstIteration(PN);
      if (Incoming && DT.dominates(Incoming, BB->getTerminator())) {
        Value *FirstIterV =
            getValueOnFirstIteration(Incoming, FirstIterValue, SQ);
        FirstIterValue[&PN] = FirstIterV;
      }
    }

    using namespace PatternMatch;
    Value *Cond;
    BasicBlock *IfTrue, *IfFalse;
    auto *Term = BB->getTerminator();
    if (match(Term, m_Br(m_Value(Cond),
                         m_BasicBlock(IfTrue), m_BasicBlock(IfFalse)))) {
      auto *ICmp = dyn_cast<ICmpInst>(Cond);
      if (!ICmp || !ICmp->getType()->isIntegerTy()) {
        MarkAllSuccessorsLive(BB);
        continue;
      }

      // Can we prove constant true or false for this condition?
      auto *KnownCondition = getValueOnFirstIteration(ICmp, FirstIterValue, SQ);
      if (KnownCondition == ICmp) {
        // Failed to simplify.
        MarkAllSuccessorsLive(BB);
        continue;
      }
      if (isa<UndefValue>(KnownCondition)) {
        // TODO: According to langref, branching by undef is undefined behavior.
        // It means that, theoretically, we should be able to just continue
        // without marking any successors as live. However, we are not certain
        // how correct our compiler is at handling such cases. So we are being
        // very conservative here.
        //
        // If there is a non-loop successor, always assume this branch leaves the
        // loop. Otherwise, arbitrarily take IfTrue.
        //
        // Once we are certain that branching by undef is handled correctly by
        // other transforms, we should not mark any successors live here.
        if (L->contains(IfTrue) && L->contains(IfFalse))
          MarkLiveEdge(BB, IfTrue);
        continue;
      }
      auto *ConstCondition = dyn_cast<ConstantInt>(KnownCondition);
      if (!ConstCondition) {
        // Non-constant condition, cannot analyze any further.
        MarkAllSuccessorsLive(BB);
        continue;
      }
      if (ConstCondition->isAllOnesValue())
        MarkLiveEdge(BB, IfTrue);
      else
        MarkLiveEdge(BB, IfFalse);
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(Term)) {
      auto *SwitchValue = SI->getCondition();
      auto *SwitchValueOnFirstIter =
          getValueOnFirstIteration(SwitchValue, FirstIterValue, SQ);
      auto *ConstSwitchValue = dyn_cast<ConstantInt>(SwitchValueOnFirstIter);
      if (!ConstSwitchValue) {
        MarkAllSuccessorsLive(BB);
        continue;
      }
      auto CaseIterator = SI->findCaseValue(ConstSwitchValue);
      MarkLiveEdge(BB, CaseIterator->getCaseSuccessor());
    } else {
      MarkAllSuccessorsLive(BB);
      continue;
    }
  }

  // We can break the latch if it wasn't live.
  return !LiveEdges.count({ Latch, Header });
}

/// If we can prove the backedge is untaken, remove it.  This destroys the
/// loop, but leaves the (now trivially loop invariant) control flow and
/// side effects (if any) in place.
static LoopDeletionResult
breakBackedgeIfNotTaken(Loop *L, DominatorTree &DT, ScalarEvolution &SE,
                        LoopInfo &LI, MemorySSA *MSSA,
                        OptimizationRemarkEmitter &ORE) {
  assert(L->isLCSSAForm(DT) && "Expected LCSSA!");

  if (!L->getLoopLatch())
    return LoopDeletionResult::Unmodified;

  auto *BTCMax = SE.getConstantMaxBackedgeTakenCount(L);
  if (!BTCMax->isZero()) {
    auto *BTC = SE.getBackedgeTakenCount(L);
    if (!BTC->isZero()) {
      if (!isa<SCEVCouldNotCompute>(BTC) && SE.isKnownNonZero(BTC))
        return LoopDeletionResult::Unmodified;
      if (!canProveExitOnFirstIteration(L, DT, LI))
        return LoopDeletionResult::Unmodified;
    }
  }
  ++NumBackedgesBroken;
  breakLoopBackedge(L, DT, SE, LI, MSSA);
  return LoopDeletionResult::Deleted;
}

/// Remove a loop if it is dead.
///
/// A loop is considered dead either if it does not impact the observable
/// behavior of the program other than finite running time, or if it is
/// required to make progress by an attribute such as 'mustprogress' or
/// 'llvm.loop.mustprogress' and does not make any. This may remove
/// infinite loops that have been required to make progress.
///
/// This entire process relies pretty heavily on LoopSimplify form and LCSSA in
/// order to make various safety checks work.
///
/// \returns true if any changes were made. This may mutate the loop even if it
/// is unable to delete it due to hoisting trivially loop invariant
/// instructions out of the loop.
static LoopDeletionResult deleteLoopIfDead(Loop *L, DominatorTree &DT,
                                           ScalarEvolution &SE, LoopInfo &LI,
                                           MemorySSA *MSSA,
                                           OptimizationRemarkEmitter &ORE) {
  assert(L->isLCSSAForm(DT) && "Expected LCSSA!");

  // We can only remove the loop if there is a preheader that we can branch from
  // after removing it. Also, if LoopSimplify form is not available, stay out
  // of trouble.
  BasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader || !L->hasDedicatedExits()) {
    LLVM_DEBUG(
        dbgs()
        << "Deletion requires Loop with preheader and dedicated exits.\n");
    return LoopDeletionResult::Unmodified;
  }

  BasicBlock *ExitBlock = L->getUniqueExitBlock();

  // We can't directly branch to an EH pad. Don't bother handling this edge
  // case.
  if (ExitBlock && ExitBlock->isEHPad()) {
    LLVM_DEBUG(dbgs() << "Cannot delete loop exiting to EH pad.\n");
    return LoopDeletionResult::Unmodified;
  }

  if (ExitBlock && isLoopNeverExecuted(L)) {
    LLVM_DEBUG(dbgs() << "Loop is proven to never execute, delete it!\n");
    // We need to forget the loop before setting the incoming values of the exit
    // phis to poison, so we properly invalidate the SCEV expressions for those
    // phis.
    SE.forgetLoop(L);
    // Set incoming value to poison for phi nodes in the exit block.
    for (PHINode &P : ExitBlock->phis()) {
      std::fill(P.incoming_values().begin(), P.incoming_values().end(),
                PoisonValue::get(P.getType()));
    }
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "NeverExecutes", L->getStartLoc(),
                                L->getHeader())
             << "Loop deleted because it never executes";
    });
    deleteDeadLoop(L, &DT, &SE, &LI, MSSA);
    ++NumDeleted;
    return LoopDeletionResult::Deleted;
  }

  // The remaining checks below are for a loop being dead because all statements
  // in the loop are invariant.
  SmallVector<BasicBlock *, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  // We require that the loop has at most one exit block. Otherwise, we'd be in
  // the situation of needing to be able to solve statically which exit block
  // will be branched to, or trying to preserve the branching logic in a loop
  // invariant manner.
  if (!ExitBlock && !L->hasNoExitBlocks()) {
    LLVM_DEBUG(dbgs() << "Deletion requires at most one exit block.\n");
    return LoopDeletionResult::Unmodified;
  }

  // Finally, we have to check that the loop really is dead.
  bool Changed = false;
  if (!isLoopDead(L, SE, ExitingBlocks, ExitBlock, Changed, Preheader, LI)) {
    LLVM_DEBUG(dbgs() << "Loop is not invariant, cannot delete.\n");
    return Changed ? LoopDeletionResult::Modified
                   : LoopDeletionResult::Unmodified;
  }

  LLVM_DEBUG(dbgs() << "Loop is invariant, delete it!\n");
  ORE.emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "Invariant", L->getStartLoc(),
                              L->getHeader())
           << "Loop deleted because it is invariant";
  });
  deleteDeadLoop(L, &DT, &SE, &LI, MSSA);
  ++NumDeleted;

  return LoopDeletionResult::Deleted;
}

PreservedAnalyses LoopDeletionPass::run(Loop &L, LoopAnalysisManager &AM,
                                        LoopStandardAnalysisResults &AR,
                                        LPMUpdater &Updater) {

  LLVM_DEBUG(dbgs() << "Analyzing Loop for deletion: ");
  LLVM_DEBUG(L.dump());
  std::string LoopName = std::string(L.getName());
  // For the new PM, we can't use OptimizationRemarkEmitter as an analysis
  // pass. Function analyses need to be preserved across loop transformations
  // but ORE cannot be preserved (see comment before the pass definition).
  OptimizationRemarkEmitter ORE(L.getHeader()->getParent());
  auto Result = deleteLoopIfDead(&L, AR.DT, AR.SE, AR.LI, AR.MSSA, ORE);

  // If we can prove the backedge isn't taken, just break it and be done.  This
  // leaves the loop structure in place which means it can handle dispatching
  // to the right exit based on whatever loop invariant structure remains.
  if (Result != LoopDeletionResult::Deleted)
    Result = merge(Result, breakBackedgeIfNotTaken(&L, AR.DT, AR.SE, AR.LI,
                                                   AR.MSSA, ORE));

  if (Result == LoopDeletionResult::Unmodified)
    return PreservedAnalyses::all();

  if (Result == LoopDeletionResult::Deleted)
    Updater.markLoopAsDeleted(L, LoopName);

  auto PA = getLoopPassPreservedAnalyses();
  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}
