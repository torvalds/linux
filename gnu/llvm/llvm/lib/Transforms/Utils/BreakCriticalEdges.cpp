//===- BreakCriticalEdges.cpp - Critical Edge Elimination Pass ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BreakCriticalEdges pass - Break all of the critical edges in the CFG by
// inserting a dummy basic block.  This pass may be "required" by passes that
// cannot deal with critical edges.  For this usage, the structure type is
// forward declared.  This pass obviously invalidates the CFG, but can update
// dominator trees.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/BreakCriticalEdges.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
using namespace llvm;

#define DEBUG_TYPE "break-crit-edges"

STATISTIC(NumBroken, "Number of blocks inserted");

namespace {
  struct BreakCriticalEdges : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    BreakCriticalEdges() : FunctionPass(ID) {
      initializeBreakCriticalEdgesPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override {
      auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
      auto *DT = DTWP ? &DTWP->getDomTree() : nullptr;

      auto *PDTWP = getAnalysisIfAvailable<PostDominatorTreeWrapperPass>();
      auto *PDT = PDTWP ? &PDTWP->getPostDomTree() : nullptr;

      auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>();
      auto *LI = LIWP ? &LIWP->getLoopInfo() : nullptr;
      unsigned N =
          SplitAllCriticalEdges(F, CriticalEdgeSplittingOptions(DT, LI, nullptr, PDT));
      NumBroken += N;
      return N > 0;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addPreserved<DominatorTreeWrapperPass>();
      AU.addPreserved<LoopInfoWrapperPass>();

      // No loop canonicalization guarantees are broken by this pass.
      AU.addPreservedID(LoopSimplifyID);
    }
  };
}

char BreakCriticalEdges::ID = 0;
INITIALIZE_PASS(BreakCriticalEdges, "break-crit-edges",
                "Break critical edges in CFG", false, false)

// Publicly exposed interface to pass...
char &llvm::BreakCriticalEdgesID = BreakCriticalEdges::ID;
FunctionPass *llvm::createBreakCriticalEdgesPass() {
  return new BreakCriticalEdges();
}

PreservedAnalyses BreakCriticalEdgesPass::run(Function &F,
                                              FunctionAnalysisManager &AM) {
  auto *DT = AM.getCachedResult<DominatorTreeAnalysis>(F);
  auto *LI = AM.getCachedResult<LoopAnalysis>(F);
  unsigned N = SplitAllCriticalEdges(F, CriticalEdgeSplittingOptions(DT, LI));
  NumBroken += N;
  if (N == 0)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}

//===----------------------------------------------------------------------===//
//    Implementation of the external critical edge manipulation functions
//===----------------------------------------------------------------------===//

BasicBlock *llvm::SplitCriticalEdge(Instruction *TI, unsigned SuccNum,
                                    const CriticalEdgeSplittingOptions &Options,
                                    const Twine &BBName) {
  if (!isCriticalEdge(TI, SuccNum, Options.MergeIdenticalEdges))
    return nullptr;

  return SplitKnownCriticalEdge(TI, SuccNum, Options, BBName);
}

BasicBlock *
llvm::SplitKnownCriticalEdge(Instruction *TI, unsigned SuccNum,
                             const CriticalEdgeSplittingOptions &Options,
                             const Twine &BBName) {
  assert(!isa<IndirectBrInst>(TI) &&
         "Cannot split critical edge from IndirectBrInst");

  BasicBlock *TIBB = TI->getParent();
  BasicBlock *DestBB = TI->getSuccessor(SuccNum);

  // Splitting the critical edge to a pad block is non-trivial. Don't do
  // it in this generic function.
  if (DestBB->isEHPad()) return nullptr;

  if (Options.IgnoreUnreachableDests &&
      isa<UnreachableInst>(DestBB->getFirstNonPHIOrDbgOrLifetime()))
    return nullptr;

  auto *LI = Options.LI;
  SmallVector<BasicBlock *, 4> LoopPreds;
  // Check if extra modifications will be required to preserve loop-simplify
  // form after splitting. If it would require splitting blocks with IndirectBr
  // terminators, bail out if preserving loop-simplify form is requested.
  if (LI) {
    if (Loop *TIL = LI->getLoopFor(TIBB)) {

      // The only way that we can break LoopSimplify form by splitting a
      // critical edge is if after the split there exists some edge from TIL to
      // DestBB *and* the only edge into DestBB from outside of TIL is that of
      // NewBB. If the first isn't true, then LoopSimplify still holds, NewBB
      // is the new exit block and it has no non-loop predecessors. If the
      // second isn't true, then DestBB was not in LoopSimplify form prior to
      // the split as it had a non-loop predecessor. In both of these cases,
      // the predecessor must be directly in TIL, not in a subloop, or again
      // LoopSimplify doesn't hold.
      for (BasicBlock *P : predecessors(DestBB)) {
        if (P == TIBB)
          continue; // The new block is known.
        if (LI->getLoopFor(P) != TIL) {
          // No need to re-simplify, it wasn't to start with.
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
        if (Options.PreserveLoopSimplify)
          return nullptr;
        LoopPreds.clear();
      }
    }
  }

  // Create a new basic block, linking it into the CFG.
  BasicBlock *NewBB = nullptr;
  if (BBName.str() != "")
    NewBB = BasicBlock::Create(TI->getContext(), BBName);
  else
    NewBB = BasicBlock::Create(TI->getContext(), TIBB->getName() + "." +
                                                     DestBB->getName() +
                                                     "_crit_edge");
  // Create our unconditional branch.
  BranchInst *NewBI = BranchInst::Create(DestBB, NewBB);
  NewBI->setDebugLoc(TI->getDebugLoc());

  // Insert the block into the function... right after the block TI lives in.
  Function &F = *TIBB->getParent();
  Function::iterator FBBI = TIBB->getIterator();
  F.insert(++FBBI, NewBB);

  // Branch to the new block, breaking the edge.
  TI->setSuccessor(SuccNum, NewBB);

  // If there are any PHI nodes in DestBB, we need to update them so that they
  // merge incoming values from NewBB instead of from TIBB.
  {
    unsigned BBIdx = 0;
    for (BasicBlock::iterator I = DestBB->begin(); isa<PHINode>(I); ++I) {
      // We no longer enter through TIBB, now we come in through NewBB.
      // Revector exactly one entry in the PHI node that used to come from
      // TIBB to come from NewBB.
      PHINode *PN = cast<PHINode>(I);

      // Reuse the previous value of BBIdx if it lines up.  In cases where we
      // have multiple phi nodes with *lots* of predecessors, this is a speed
      // win because we don't have to scan the PHI looking for TIBB.  This
      // happens because the BB list of PHI nodes are usually in the same
      // order.
      if (PN->getIncomingBlock(BBIdx) != TIBB)
        BBIdx = PN->getBasicBlockIndex(TIBB);
      PN->setIncomingBlock(BBIdx, NewBB);
    }
  }

  // If there are any other edges from TIBB to DestBB, update those to go
  // through the split block, making those edges non-critical as well (and
  // reducing the number of phi entries in the DestBB if relevant).
  if (Options.MergeIdenticalEdges) {
    for (unsigned i = SuccNum+1, e = TI->getNumSuccessors(); i != e; ++i) {
      if (TI->getSuccessor(i) != DestBB) continue;

      // Remove an entry for TIBB from DestBB phi nodes.
      DestBB->removePredecessor(TIBB, Options.KeepOneInputPHIs);

      // We found another edge to DestBB, go to NewBB instead.
      TI->setSuccessor(i, NewBB);
    }
  }

  // If we have nothing to update, just return.
  auto *DT = Options.DT;
  auto *PDT = Options.PDT;
  auto *MSSAU = Options.MSSAU;
  if (MSSAU)
    MSSAU->wireOldPredecessorsToNewImmediatePredecessor(
        DestBB, NewBB, {TIBB}, Options.MergeIdenticalEdges);

  if (!DT && !PDT && !LI)
    return NewBB;

  if (DT || PDT) {
    // Update the DominatorTree.
    //       ---> NewBB -----\
    //      /                 V
    //  TIBB -------\\------> DestBB
    //
    // First, inform the DT about the new path from TIBB to DestBB via NewBB,
    // then delete the old edge from TIBB to DestBB. By doing this in that order
    // DestBB stays reachable in the DT the whole time and its subtree doesn't
    // get disconnected.
    SmallVector<DominatorTree::UpdateType, 3> Updates;
    Updates.push_back({DominatorTree::Insert, TIBB, NewBB});
    Updates.push_back({DominatorTree::Insert, NewBB, DestBB});
    if (!llvm::is_contained(successors(TIBB), DestBB))
      Updates.push_back({DominatorTree::Delete, TIBB, DestBB});

    if (DT)
      DT->applyUpdates(Updates);
    if (PDT)
      PDT->applyUpdates(Updates);
  }

  // Update LoopInfo if it is around.
  if (LI) {
    if (Loop *TIL = LI->getLoopFor(TIBB)) {
      // If one or the other blocks were not in a loop, the new block is not
      // either, and thus LI doesn't need to be updated.
      if (Loop *DestLoop = LI->getLoopFor(DestBB)) {
        if (TIL == DestLoop) {
          // Both in the same loop, the NewBB joins loop.
          DestLoop->addBasicBlockToLoop(NewBB, *LI);
        } else if (TIL->contains(DestLoop)) {
          // Edge from an outer loop to an inner loop.  Add to the outer loop.
          TIL->addBasicBlockToLoop(NewBB, *LI);
        } else if (DestLoop->contains(TIL)) {
          // Edge from an inner loop to an outer loop.  Add to the outer loop.
          DestLoop->addBasicBlockToLoop(NewBB, *LI);
        } else {
          // Edge from two loops with no containment relation.  Because these
          // are natural loops, we know that the destination block must be the
          // header of its loop (adding a branch into a loop elsewhere would
          // create an irreducible loop).
          assert(DestLoop->getHeader() == DestBB &&
                 "Should not create irreducible loops!");
          if (Loop *P = DestLoop->getParentLoop())
            P->addBasicBlockToLoop(NewBB, *LI);
        }
      }

      // If TIBB is in a loop and DestBB is outside of that loop, we may need
      // to update LoopSimplify form and LCSSA form.
      if (!TIL->contains(DestBB)) {
        assert(!TIL->contains(NewBB) &&
               "Split point for loop exit is contained in loop!");

        // Update LCSSA form in the newly created exit block.
        if (Options.PreserveLCSSA) {
          createPHIsForSplitLoopExit(TIBB, NewBB, DestBB);
        }

        if (!LoopPreds.empty()) {
          assert(!DestBB->isEHPad() && "We don't split edges to EH pads!");
          BasicBlock *NewExitBB = SplitBlockPredecessors(
              DestBB, LoopPreds, "split", DT, LI, MSSAU, Options.PreserveLCSSA);
          if (Options.PreserveLCSSA)
            createPHIsForSplitLoopExit(LoopPreds, NewExitBB, DestBB);
        }
      }
    }
  }

  return NewBB;
}

// Return the unique indirectbr predecessor of a block. This may return null
// even if such a predecessor exists, if it's not useful for splitting.
// If a predecessor is found, OtherPreds will contain all other (non-indirectbr)
// predecessors of BB.
static BasicBlock *
findIBRPredecessor(BasicBlock *BB, SmallVectorImpl<BasicBlock *> &OtherPreds) {
  // Verify we have exactly one IBR predecessor.
  // Conservatively bail out if one of the other predecessors is not a "regular"
  // terminator (that is, not a switch or a br).
  BasicBlock *IBB = nullptr;
  for (BasicBlock *PredBB : predecessors(BB)) {
    Instruction *PredTerm = PredBB->getTerminator();
    switch (PredTerm->getOpcode()) {
    case Instruction::IndirectBr:
      if (IBB)
        return nullptr;
      IBB = PredBB;
      break;
    case Instruction::Br:
    case Instruction::Switch:
      OtherPreds.push_back(PredBB);
      continue;
    default:
      return nullptr;
    }
  }

  return IBB;
}

bool llvm::SplitIndirectBrCriticalEdges(Function &F,
                                        bool IgnoreBlocksWithoutPHI,
                                        BranchProbabilityInfo *BPI,
                                        BlockFrequencyInfo *BFI) {
  // Check whether the function has any indirectbrs, and collect which blocks
  // they may jump to. Since most functions don't have indirect branches,
  // this lowers the common case's overhead to O(Blocks) instead of O(Edges).
  SmallSetVector<BasicBlock *, 16> Targets;
  for (auto &BB : F) {
    if (isa<IndirectBrInst>(BB.getTerminator()))
      for (BasicBlock *Succ : successors(&BB))
        Targets.insert(Succ);
  }

  if (Targets.empty())
    return false;

  bool ShouldUpdateAnalysis = BPI && BFI;
  bool Changed = false;
  for (BasicBlock *Target : Targets) {
    if (IgnoreBlocksWithoutPHI && Target->phis().empty())
      continue;

    SmallVector<BasicBlock *, 16> OtherPreds;
    BasicBlock *IBRPred = findIBRPredecessor(Target, OtherPreds);
    // If we did not found an indirectbr, or the indirectbr is the only
    // incoming edge, this isn't the kind of edge we're looking for.
    if (!IBRPred || OtherPreds.empty())
      continue;

    // Don't even think about ehpads/landingpads.
    Instruction *FirstNonPHI = Target->getFirstNonPHI();
    if (FirstNonPHI->isEHPad() || Target->isLandingPad())
      continue;

    // Remember edge probabilities if needed.
    SmallVector<BranchProbability, 4> EdgeProbabilities;
    if (ShouldUpdateAnalysis) {
      EdgeProbabilities.reserve(Target->getTerminator()->getNumSuccessors());
      for (unsigned I = 0, E = Target->getTerminator()->getNumSuccessors();
           I < E; ++I)
        EdgeProbabilities.emplace_back(BPI->getEdgeProbability(Target, I));
      BPI->eraseBlock(Target);
    }

    BasicBlock *BodyBlock = Target->splitBasicBlock(FirstNonPHI, ".split");
    if (ShouldUpdateAnalysis) {
      // Copy the BFI/BPI from Target to BodyBlock.
      BPI->setEdgeProbability(BodyBlock, EdgeProbabilities);
      BFI->setBlockFreq(BodyBlock, BFI->getBlockFreq(Target));
    }
    // It's possible Target was its own successor through an indirectbr.
    // In this case, the indirectbr now comes from BodyBlock.
    if (IBRPred == Target)
      IBRPred = BodyBlock;

    // At this point Target only has PHIs, and BodyBlock has the rest of the
    // block's body. Create a copy of Target that will be used by the "direct"
    // preds.
    ValueToValueMapTy VMap;
    BasicBlock *DirectSucc = CloneBasicBlock(Target, VMap, ".clone", &F);

    BlockFrequency BlockFreqForDirectSucc;
    for (BasicBlock *Pred : OtherPreds) {
      // If the target is a loop to itself, then the terminator of the split
      // block (BodyBlock) needs to be updated.
      BasicBlock *Src = Pred != Target ? Pred : BodyBlock;
      Src->getTerminator()->replaceUsesOfWith(Target, DirectSucc);
      if (ShouldUpdateAnalysis)
        BlockFreqForDirectSucc += BFI->getBlockFreq(Src) *
            BPI->getEdgeProbability(Src, DirectSucc);
    }
    if (ShouldUpdateAnalysis) {
      BFI->setBlockFreq(DirectSucc, BlockFreqForDirectSucc);
      BlockFrequency NewBlockFreqForTarget =
          BFI->getBlockFreq(Target) - BlockFreqForDirectSucc;
      BFI->setBlockFreq(Target, NewBlockFreqForTarget);
    }

    // Ok, now fix up the PHIs. We know the two blocks only have PHIs, and that
    // they are clones, so the number of PHIs are the same.
    // (a) Remove the edge coming from IBRPred from the "Direct" PHI
    // (b) Leave that as the only edge in the "Indirect" PHI.
    // (c) Merge the two in the body block.
    BasicBlock::iterator Indirect = Target->begin(),
                         End = Target->getFirstNonPHIIt();
    BasicBlock::iterator Direct = DirectSucc->begin();
    BasicBlock::iterator MergeInsert = BodyBlock->getFirstInsertionPt();

    assert(&*End == Target->getTerminator() &&
           "Block was expected to only contain PHIs");

    while (Indirect != End) {
      PHINode *DirPHI = cast<PHINode>(Direct);
      PHINode *IndPHI = cast<PHINode>(Indirect);
      BasicBlock::iterator InsertPt = Indirect;

      // Now, clean up - the direct block shouldn't get the indirect value,
      // and vice versa.
      DirPHI->removeIncomingValue(IBRPred);
      Direct++;

      // Advance the pointer here, to avoid invalidation issues when the old
      // PHI is erased.
      Indirect++;

      PHINode *NewIndPHI = PHINode::Create(IndPHI->getType(), 1, "ind", InsertPt);
      NewIndPHI->addIncoming(IndPHI->getIncomingValueForBlock(IBRPred),
                             IBRPred);

      // Create a PHI in the body block, to merge the direct and indirect
      // predecessors.
      PHINode *MergePHI = PHINode::Create(IndPHI->getType(), 2, "merge");
      MergePHI->insertBefore(MergeInsert);
      MergePHI->addIncoming(NewIndPHI, Target);
      MergePHI->addIncoming(DirPHI, DirectSucc);

      IndPHI->replaceAllUsesWith(MergePHI);
      IndPHI->eraseFromParent();
    }

    Changed = true;
  }

  return Changed;
}
