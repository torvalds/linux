//===-- UnrollLoopRuntime.cpp - Runtime Loop unrolling utilities ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements some loop unrolling utilities for loops with run-time
// trip counts.  See LoopUnroll.cpp for unrolling loops with compile-time
// trip counts.
//
// The functions in this file are used to generate extra code when the
// run-time trip count modulo the unroll factor is not 0.  When this is the
// case, we need to generate code to execute these 'left over' iterations.
//
// The current strategy generates an if-then-else sequence prior to the
// unrolled loop to execute the 'left over' iterations before or after the
// unrolled loop.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <algorithm>

using namespace llvm;

#define DEBUG_TYPE "loop-unroll"

STATISTIC(NumRuntimeUnrolled,
          "Number of loops unrolled with run-time trip counts");
static cl::opt<bool> UnrollRuntimeMultiExit(
    "unroll-runtime-multi-exit", cl::init(false), cl::Hidden,
    cl::desc("Allow runtime unrolling for loops with multiple exits, when "
             "epilog is generated"));
static cl::opt<bool> UnrollRuntimeOtherExitPredictable(
    "unroll-runtime-other-exit-predictable", cl::init(false), cl::Hidden,
    cl::desc("Assume the non latch exit block to be predictable"));

// Probability that the loop trip count is so small that after the prolog
// we do not enter the unrolled loop at all.
// It is unlikely that the loop trip count is smaller than the unroll factor;
// other than that, the choice of constant is not tuned yet.
static const uint32_t UnrolledLoopHeaderWeights[] = {1, 127};
// Probability that the loop trip count is so small that we skip the unrolled
// loop completely and immediately enter the epilogue loop.
// It is unlikely that the loop trip count is smaller than the unroll factor;
// other than that, the choice of constant is not tuned yet.
static const uint32_t EpilogHeaderWeights[] = {1, 127};

/// Connect the unrolling prolog code to the original loop.
/// The unrolling prolog code contains code to execute the
/// 'extra' iterations if the run-time trip count modulo the
/// unroll count is non-zero.
///
/// This function performs the following:
/// - Create PHI nodes at prolog end block to combine values
///   that exit the prolog code and jump around the prolog.
/// - Add a PHI operand to a PHI node at the loop exit block
///   for values that exit the prolog and go around the loop.
/// - Branch around the original loop if the trip count is less
///   than the unroll factor.
///
static void ConnectProlog(Loop *L, Value *BECount, unsigned Count,
                          BasicBlock *PrologExit,
                          BasicBlock *OriginalLoopLatchExit,
                          BasicBlock *PreHeader, BasicBlock *NewPreHeader,
                          ValueToValueMapTy &VMap, DominatorTree *DT,
                          LoopInfo *LI, bool PreserveLCSSA,
                          ScalarEvolution &SE) {
  // Loop structure should be the following:
  // Preheader
  //  PrologHeader
  //  ...
  //  PrologLatch
  //  PrologExit
  //   NewPreheader
  //    Header
  //    ...
  //    Latch
  //      LatchExit
  BasicBlock *Latch = L->getLoopLatch();
  assert(Latch && "Loop must have a latch");
  BasicBlock *PrologLatch = cast<BasicBlock>(VMap[Latch]);

  // Create a PHI node for each outgoing value from the original loop
  // (which means it is an outgoing value from the prolog code too).
  // The new PHI node is inserted in the prolog end basic block.
  // The new PHI node value is added as an operand of a PHI node in either
  // the loop header or the loop exit block.
  for (BasicBlock *Succ : successors(Latch)) {
    for (PHINode &PN : Succ->phis()) {
      // Add a new PHI node to the prolog end block and add the
      // appropriate incoming values.
      // TODO: This code assumes that the PrologExit (or the LatchExit block for
      // prolog loop) contains only one predecessor from the loop, i.e. the
      // PrologLatch. When supporting multiple-exiting block loops, we can have
      // two or more blocks that have the LatchExit as the target in the
      // original loop.
      PHINode *NewPN = PHINode::Create(PN.getType(), 2, PN.getName() + ".unr");
      NewPN->insertBefore(PrologExit->getFirstNonPHIIt());
      // Adding a value to the new PHI node from the original loop preheader.
      // This is the value that skips all the prolog code.
      if (L->contains(&PN)) {
        // Succ is loop header.
        NewPN->addIncoming(PN.getIncomingValueForBlock(NewPreHeader),
                           PreHeader);
      } else {
        // Succ is LatchExit.
        NewPN->addIncoming(PoisonValue::get(PN.getType()), PreHeader);
      }

      Value *V = PN.getIncomingValueForBlock(Latch);
      if (Instruction *I = dyn_cast<Instruction>(V)) {
        if (L->contains(I)) {
          V = VMap.lookup(I);
        }
      }
      // Adding a value to the new PHI node from the last prolog block
      // that was created.
      NewPN->addIncoming(V, PrologLatch);

      // Update the existing PHI node operand with the value from the
      // new PHI node.  How this is done depends on if the existing
      // PHI node is in the original loop block, or the exit block.
      if (L->contains(&PN))
        PN.setIncomingValueForBlock(NewPreHeader, NewPN);
      else
        PN.addIncoming(NewPN, PrologExit);
      SE.forgetValue(&PN);
    }
  }

  // Make sure that created prolog loop is in simplified form
  SmallVector<BasicBlock *, 4> PrologExitPreds;
  Loop *PrologLoop = LI->getLoopFor(PrologLatch);
  if (PrologLoop) {
    for (BasicBlock *PredBB : predecessors(PrologExit))
      if (PrologLoop->contains(PredBB))
        PrologExitPreds.push_back(PredBB);

    SplitBlockPredecessors(PrologExit, PrologExitPreds, ".unr-lcssa", DT, LI,
                           nullptr, PreserveLCSSA);
  }

  // Create a branch around the original loop, which is taken if there are no
  // iterations remaining to be executed after running the prologue.
  Instruction *InsertPt = PrologExit->getTerminator();
  IRBuilder<> B(InsertPt);

  assert(Count != 0 && "nonsensical Count!");

  // If BECount <u (Count - 1) then (BECount + 1) % Count == (BECount + 1)
  // This means %xtraiter is (BECount + 1) and all of the iterations of this
  // loop were executed by the prologue.  Note that if BECount <u (Count - 1)
  // then (BECount + 1) cannot unsigned-overflow.
  Value *BrLoopExit =
      B.CreateICmpULT(BECount, ConstantInt::get(BECount->getType(), Count - 1));
  // Split the exit to maintain loop canonicalization guarantees
  SmallVector<BasicBlock *, 4> Preds(predecessors(OriginalLoopLatchExit));
  SplitBlockPredecessors(OriginalLoopLatchExit, Preds, ".unr-lcssa", DT, LI,
                         nullptr, PreserveLCSSA);
  // Add the branch to the exit block (around the unrolled loop)
  MDNode *BranchWeights = nullptr;
  if (hasBranchWeightMD(*Latch->getTerminator())) {
    // Assume loop is nearly always entered.
    MDBuilder MDB(B.getContext());
    BranchWeights = MDB.createBranchWeights(UnrolledLoopHeaderWeights);
  }
  B.CreateCondBr(BrLoopExit, OriginalLoopLatchExit, NewPreHeader,
                 BranchWeights);
  InsertPt->eraseFromParent();
  if (DT) {
    auto *NewDom = DT->findNearestCommonDominator(OriginalLoopLatchExit,
                                                  PrologExit);
    DT->changeImmediateDominator(OriginalLoopLatchExit, NewDom);
  }
}

/// Connect the unrolling epilog code to the original loop.
/// The unrolling epilog code contains code to execute the
/// 'extra' iterations if the run-time trip count modulo the
/// unroll count is non-zero.
///
/// This function performs the following:
/// - Update PHI nodes at the unrolling loop exit and epilog loop exit
/// - Create PHI nodes at the unrolling loop exit to combine
///   values that exit the unrolling loop code and jump around it.
/// - Update PHI operands in the epilog loop by the new PHI nodes
/// - Branch around the epilog loop if extra iters (ModVal) is zero.
///
static void ConnectEpilog(Loop *L, Value *ModVal, BasicBlock *NewExit,
                          BasicBlock *Exit, BasicBlock *PreHeader,
                          BasicBlock *EpilogPreHeader, BasicBlock *NewPreHeader,
                          ValueToValueMapTy &VMap, DominatorTree *DT,
                          LoopInfo *LI, bool PreserveLCSSA, ScalarEvolution &SE,
                          unsigned Count) {
  BasicBlock *Latch = L->getLoopLatch();
  assert(Latch && "Loop must have a latch");
  BasicBlock *EpilogLatch = cast<BasicBlock>(VMap[Latch]);

  // Loop structure should be the following:
  //
  // PreHeader
  // NewPreHeader
  //   Header
  //   ...
  //   Latch
  // NewExit (PN)
  // EpilogPreHeader
  //   EpilogHeader
  //   ...
  //   EpilogLatch
  // Exit (EpilogPN)

  // Update PHI nodes at NewExit and Exit.
  for (PHINode &PN : NewExit->phis()) {
    // PN should be used in another PHI located in Exit block as
    // Exit was split by SplitBlockPredecessors into Exit and NewExit
    // Basically it should look like:
    // NewExit:
    //   PN = PHI [I, Latch]
    // ...
    // Exit:
    //   EpilogPN = PHI [PN, EpilogPreHeader], [X, Exit2], [Y, Exit2.epil]
    //
    // Exits from non-latch blocks point to the original exit block and the
    // epilogue edges have already been added.
    //
    // There is EpilogPreHeader incoming block instead of NewExit as
    // NewExit was spilt 1 more time to get EpilogPreHeader.
    assert(PN.hasOneUse() && "The phi should have 1 use");
    PHINode *EpilogPN = cast<PHINode>(PN.use_begin()->getUser());
    assert(EpilogPN->getParent() == Exit && "EpilogPN should be in Exit block");

    // Add incoming PreHeader from branch around the Loop
    PN.addIncoming(PoisonValue::get(PN.getType()), PreHeader);
    SE.forgetValue(&PN);

    Value *V = PN.getIncomingValueForBlock(Latch);
    Instruction *I = dyn_cast<Instruction>(V);
    if (I && L->contains(I))
      // If value comes from an instruction in the loop add VMap value.
      V = VMap.lookup(I);
    // For the instruction out of the loop, constant or undefined value
    // insert value itself.
    EpilogPN->addIncoming(V, EpilogLatch);

    assert(EpilogPN->getBasicBlockIndex(EpilogPreHeader) >= 0 &&
          "EpilogPN should have EpilogPreHeader incoming block");
    // Change EpilogPreHeader incoming block to NewExit.
    EpilogPN->setIncomingBlock(EpilogPN->getBasicBlockIndex(EpilogPreHeader),
                               NewExit);
    // Now PHIs should look like:
    // NewExit:
    //   PN = PHI [I, Latch], [poison, PreHeader]
    // ...
    // Exit:
    //   EpilogPN = PHI [PN, NewExit], [VMap[I], EpilogLatch]
  }

  // Create PHI nodes at NewExit (from the unrolling loop Latch and PreHeader).
  // Update corresponding PHI nodes in epilog loop.
  for (BasicBlock *Succ : successors(Latch)) {
    // Skip this as we already updated phis in exit blocks.
    if (!L->contains(Succ))
      continue;
    for (PHINode &PN : Succ->phis()) {
      // Add new PHI nodes to the loop exit block and update epilog
      // PHIs with the new PHI values.
      PHINode *NewPN = PHINode::Create(PN.getType(), 2, PN.getName() + ".unr");
      NewPN->insertBefore(NewExit->getFirstNonPHIIt());
      // Adding a value to the new PHI node from the unrolling loop preheader.
      NewPN->addIncoming(PN.getIncomingValueForBlock(NewPreHeader), PreHeader);
      // Adding a value to the new PHI node from the unrolling loop latch.
      NewPN->addIncoming(PN.getIncomingValueForBlock(Latch), Latch);

      // Update the existing PHI node operand with the value from the new PHI
      // node.  Corresponding instruction in epilog loop should be PHI.
      PHINode *VPN = cast<PHINode>(VMap[&PN]);
      VPN->setIncomingValueForBlock(EpilogPreHeader, NewPN);
    }
  }

  Instruction *InsertPt = NewExit->getTerminator();
  IRBuilder<> B(InsertPt);
  Value *BrLoopExit = B.CreateIsNotNull(ModVal, "lcmp.mod");
  assert(Exit && "Loop must have a single exit block only");
  // Split the epilogue exit to maintain loop canonicalization guarantees
  SmallVector<BasicBlock*, 4> Preds(predecessors(Exit));
  SplitBlockPredecessors(Exit, Preds, ".epilog-lcssa", DT, LI, nullptr,
                         PreserveLCSSA);
  // Add the branch to the exit block (around the unrolling loop)
  MDNode *BranchWeights = nullptr;
  if (hasBranchWeightMD(*Latch->getTerminator())) {
    // Assume equal distribution in interval [0, Count).
    MDBuilder MDB(B.getContext());
    BranchWeights = MDB.createBranchWeights(1, Count - 1);
  }
  B.CreateCondBr(BrLoopExit, EpilogPreHeader, Exit, BranchWeights);
  InsertPt->eraseFromParent();
  if (DT) {
    auto *NewDom = DT->findNearestCommonDominator(Exit, NewExit);
    DT->changeImmediateDominator(Exit, NewDom);
  }

  // Split the main loop exit to maintain canonicalization guarantees.
  SmallVector<BasicBlock*, 4> NewExitPreds{Latch};
  SplitBlockPredecessors(NewExit, NewExitPreds, ".loopexit", DT, LI, nullptr,
                         PreserveLCSSA);
}

/// Create a clone of the blocks in a loop and connect them together. A new
/// loop will be created including all cloned blocks, and the iterator of the
/// new loop switched to count NewIter down to 0.
/// The cloned blocks should be inserted between InsertTop and InsertBot.
/// InsertTop should be new preheader, InsertBot new loop exit.
/// Returns the new cloned loop that is created.
static Loop *
CloneLoopBlocks(Loop *L, Value *NewIter, const bool UseEpilogRemainder,
                const bool UnrollRemainder,
                BasicBlock *InsertTop,
                BasicBlock *InsertBot, BasicBlock *Preheader,
                             std::vector<BasicBlock *> &NewBlocks,
                             LoopBlocksDFS &LoopBlocks, ValueToValueMapTy &VMap,
                             DominatorTree *DT, LoopInfo *LI, unsigned Count) {
  StringRef suffix = UseEpilogRemainder ? "epil" : "prol";
  BasicBlock *Header = L->getHeader();
  BasicBlock *Latch = L->getLoopLatch();
  Function *F = Header->getParent();
  LoopBlocksDFS::RPOIterator BlockBegin = LoopBlocks.beginRPO();
  LoopBlocksDFS::RPOIterator BlockEnd = LoopBlocks.endRPO();
  Loop *ParentLoop = L->getParentLoop();
  NewLoopsMap NewLoops;
  NewLoops[ParentLoop] = ParentLoop;

  // For each block in the original loop, create a new copy,
  // and update the value map with the newly created values.
  for (LoopBlocksDFS::RPOIterator BB = BlockBegin; BB != BlockEnd; ++BB) {
    BasicBlock *NewBB = CloneBasicBlock(*BB, VMap, "." + suffix, F);
    NewBlocks.push_back(NewBB);

    addClonedBlockToLoopInfo(*BB, NewBB, LI, NewLoops);

    VMap[*BB] = NewBB;
    if (Header == *BB) {
      // For the first block, add a CFG connection to this newly
      // created block.
      InsertTop->getTerminator()->setSuccessor(0, NewBB);
    }

    if (DT) {
      if (Header == *BB) {
        // The header is dominated by the preheader.
        DT->addNewBlock(NewBB, InsertTop);
      } else {
        // Copy information from original loop to unrolled loop.
        BasicBlock *IDomBB = DT->getNode(*BB)->getIDom()->getBlock();
        DT->addNewBlock(NewBB, cast<BasicBlock>(VMap[IDomBB]));
      }
    }

    if (Latch == *BB) {
      // For the last block, create a loop back to cloned head.
      VMap.erase((*BB)->getTerminator());
      // Use an incrementing IV.  Pre-incr/post-incr is backedge/trip count.
      // Subtle: NewIter can be 0 if we wrapped when computing the trip count,
      // thus we must compare the post-increment (wrapping) value.
      BasicBlock *FirstLoopBB = cast<BasicBlock>(VMap[Header]);
      BranchInst *LatchBR = cast<BranchInst>(NewBB->getTerminator());
      IRBuilder<> Builder(LatchBR);
      PHINode *NewIdx =
          PHINode::Create(NewIter->getType(), 2, suffix + ".iter");
      NewIdx->insertBefore(FirstLoopBB->getFirstNonPHIIt());
      auto *Zero = ConstantInt::get(NewIdx->getType(), 0);
      auto *One = ConstantInt::get(NewIdx->getType(), 1);
      Value *IdxNext =
          Builder.CreateAdd(NewIdx, One, NewIdx->getName() + ".next");
      Value *IdxCmp = Builder.CreateICmpNE(IdxNext, NewIter, NewIdx->getName() + ".cmp");
      MDNode *BranchWeights = nullptr;
      if (hasBranchWeightMD(*LatchBR)) {
        uint32_t ExitWeight;
        uint32_t BackEdgeWeight;
        if (Count >= 3) {
          // Note: We do not enter this loop for zero-remainders. The check
          // is at the end of the loop. We assume equal distribution between
          // possible remainders in [1, Count).
          ExitWeight = 1;
          BackEdgeWeight = (Count - 2) / 2;
        } else {
          // Unnecessary backedge, should never be taken. The conditional
          // jump should be optimized away later.
          ExitWeight = 1;
          BackEdgeWeight = 0;
        }
        MDBuilder MDB(Builder.getContext());
        BranchWeights = MDB.createBranchWeights(BackEdgeWeight, ExitWeight);
      }
      Builder.CreateCondBr(IdxCmp, FirstLoopBB, InsertBot, BranchWeights);
      NewIdx->addIncoming(Zero, InsertTop);
      NewIdx->addIncoming(IdxNext, NewBB);
      LatchBR->eraseFromParent();
    }
  }

  // Change the incoming values to the ones defined in the preheader or
  // cloned loop.
  for (BasicBlock::iterator I = Header->begin(); isa<PHINode>(I); ++I) {
    PHINode *NewPHI = cast<PHINode>(VMap[&*I]);
    unsigned idx = NewPHI->getBasicBlockIndex(Preheader);
    NewPHI->setIncomingBlock(idx, InsertTop);
    BasicBlock *NewLatch = cast<BasicBlock>(VMap[Latch]);
    idx = NewPHI->getBasicBlockIndex(Latch);
    Value *InVal = NewPHI->getIncomingValue(idx);
    NewPHI->setIncomingBlock(idx, NewLatch);
    if (Value *V = VMap.lookup(InVal))
      NewPHI->setIncomingValue(idx, V);
  }

  Loop *NewLoop = NewLoops[L];
  assert(NewLoop && "L should have been cloned");
  MDNode *LoopID = NewLoop->getLoopID();

  // Only add loop metadata if the loop is not going to be completely
  // unrolled.
  if (UnrollRemainder)
    return NewLoop;

  std::optional<MDNode *> NewLoopID = makeFollowupLoopID(
      LoopID, {LLVMLoopUnrollFollowupAll, LLVMLoopUnrollFollowupRemainder});
  if (NewLoopID) {
    NewLoop->setLoopID(*NewLoopID);

    // Do not setLoopAlreadyUnrolled if loop attributes have been defined
    // explicitly.
    return NewLoop;
  }

  // Add unroll disable metadata to disable future unrolling for this loop.
  NewLoop->setLoopAlreadyUnrolled();
  return NewLoop;
}

/// Returns true if we can profitably unroll the multi-exit loop L. Currently,
/// we return true only if UnrollRuntimeMultiExit is set to true.
static bool canProfitablyUnrollMultiExitLoop(
    Loop *L, SmallVectorImpl<BasicBlock *> &OtherExits, BasicBlock *LatchExit,
    bool UseEpilogRemainder) {

  // Priority goes to UnrollRuntimeMultiExit if it's supplied.
  if (UnrollRuntimeMultiExit.getNumOccurrences())
    return UnrollRuntimeMultiExit;

  // The main pain point with multi-exit loop unrolling is that once unrolled,
  // we will not be able to merge all blocks into a straight line code.
  // There are branches within the unrolled loop that go to the OtherExits.
  // The second point is the increase in code size, but this is true
  // irrespective of multiple exits.

  // Note: Both the heuristics below are coarse grained. We are essentially
  // enabling unrolling of loops that have a single side exit other than the
  // normal LatchExit (i.e. exiting into a deoptimize block).
  // The heuristics considered are:
  // 1. low number of branches in the unrolled version.
  // 2. high predictability of these extra branches.
  // We avoid unrolling loops that have more than two exiting blocks. This
  // limits the total number of branches in the unrolled loop to be atmost
  // the unroll factor (since one of the exiting blocks is the latch block).
  SmallVector<BasicBlock*, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  if (ExitingBlocks.size() > 2)
    return false;

  // Allow unrolling of loops with no non latch exit blocks.
  if (OtherExits.size() == 0)
    return true;

  // The second heuristic is that L has one exit other than the latchexit and
  // that exit is a deoptimize block. We know that deoptimize blocks are rarely
  // taken, which also implies the branch leading to the deoptimize block is
  // highly predictable. When UnrollRuntimeOtherExitPredictable is specified, we
  // assume the other exit branch is predictable even if it has no deoptimize
  // call.
  return (OtherExits.size() == 1 &&
          (UnrollRuntimeOtherExitPredictable ||
           OtherExits[0]->getPostdominatingDeoptimizeCall()));
  // TODO: These can be fine-tuned further to consider code size or deopt states
  // that are captured by the deoptimize exit block.
  // Also, we can extend this to support more cases, if we actually
  // know of kinds of multiexit loops that would benefit from unrolling.
}

/// Calculate ModVal = (BECount + 1) % Count on the abstract integer domain
/// accounting for the possibility of unsigned overflow in the 2s complement
/// domain. Preconditions:
/// 1) TripCount = BECount + 1 (allowing overflow)
/// 2) Log2(Count) <= BitWidth(BECount)
static Value *CreateTripRemainder(IRBuilder<> &B, Value *BECount,
                                  Value *TripCount, unsigned Count) {
  // Note that TripCount is BECount + 1.
  if (isPowerOf2_32(Count))
    // If the expression is zero, then either:
    //  1. There are no iterations to be run in the prolog/epilog loop.
    // OR
    //  2. The addition computing TripCount overflowed.
    //
    // If (2) is true, we know that TripCount really is (1 << BEWidth) and so
    // the number of iterations that remain to be run in the original loop is a
    // multiple Count == (1 << Log2(Count)) because Log2(Count) <= BEWidth (a
    // precondition of this method).
    return B.CreateAnd(TripCount, Count - 1, "xtraiter");

  // As (BECount + 1) can potentially unsigned overflow we count
  // (BECount % Count) + 1 which is overflow safe as BECount % Count < Count.
  Constant *CountC = ConstantInt::get(BECount->getType(), Count);
  Value *ModValTmp = B.CreateURem(BECount, CountC);
  Value *ModValAdd = B.CreateAdd(ModValTmp,
                                 ConstantInt::get(ModValTmp->getType(), 1));
  // At that point (BECount % Count) + 1 could be equal to Count.
  // To handle this case we need to take mod by Count one more time.
  return B.CreateURem(ModValAdd, CountC, "xtraiter");
}


/// Insert code in the prolog/epilog code when unrolling a loop with a
/// run-time trip-count.
///
/// This method assumes that the loop unroll factor is total number
/// of loop bodies in the loop after unrolling. (Some folks refer
/// to the unroll factor as the number of *extra* copies added).
/// We assume also that the loop unroll factor is a power-of-two. So, after
/// unrolling the loop, the number of loop bodies executed is 2,
/// 4, 8, etc.  Note - LLVM converts the if-then-sequence to a switch
/// instruction in SimplifyCFG.cpp.  Then, the backend decides how code for
/// the switch instruction is generated.
///
/// ***Prolog case***
///        extraiters = tripcount % loopfactor
///        if (extraiters == 0) jump Loop:
///        else jump Prol:
/// Prol:  LoopBody;
///        extraiters -= 1                 // Omitted if unroll factor is 2.
///        if (extraiters != 0) jump Prol: // Omitted if unroll factor is 2.
///        if (tripcount < loopfactor) jump End:
/// Loop:
/// ...
/// End:
///
/// ***Epilog case***
///        extraiters = tripcount % loopfactor
///        if (tripcount < loopfactor) jump LoopExit:
///        unroll_iters = tripcount - extraiters
/// Loop:  LoopBody; (executes unroll_iter times);
///        unroll_iter -= 1
///        if (unroll_iter != 0) jump Loop:
/// LoopExit:
///        if (extraiters == 0) jump EpilExit:
/// Epil:  LoopBody; (executes extraiters times)
///        extraiters -= 1                 // Omitted if unroll factor is 2.
///        if (extraiters != 0) jump Epil: // Omitted if unroll factor is 2.
/// EpilExit:

bool llvm::UnrollRuntimeLoopRemainder(
    Loop *L, unsigned Count, bool AllowExpensiveTripCount,
    bool UseEpilogRemainder, bool UnrollRemainder, bool ForgetAllSCEV,
    LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT, AssumptionCache *AC,
    const TargetTransformInfo *TTI, bool PreserveLCSSA, Loop **ResultLoop) {
  LLVM_DEBUG(dbgs() << "Trying runtime unrolling on Loop: \n");
  LLVM_DEBUG(L->dump());
  LLVM_DEBUG(UseEpilogRemainder ? dbgs() << "Using epilog remainder.\n"
                                : dbgs() << "Using prolog remainder.\n");

  // Make sure the loop is in canonical form.
  if (!L->isLoopSimplifyForm()) {
    LLVM_DEBUG(dbgs() << "Not in simplify form!\n");
    return false;
  }

  // Guaranteed by LoopSimplifyForm.
  BasicBlock *Latch = L->getLoopLatch();
  BasicBlock *Header = L->getHeader();

  BranchInst *LatchBR = cast<BranchInst>(Latch->getTerminator());

  if (!LatchBR || LatchBR->isUnconditional()) {
    // The loop-rotate pass can be helpful to avoid this in many cases.
    LLVM_DEBUG(
        dbgs()
        << "Loop latch not terminated by a conditional branch.\n");
    return false;
  }

  unsigned ExitIndex = LatchBR->getSuccessor(0) == Header ? 1 : 0;
  BasicBlock *LatchExit = LatchBR->getSuccessor(ExitIndex);

  if (L->contains(LatchExit)) {
    // Cloning the loop basic blocks (`CloneLoopBlocks`) requires that one of the
    // targets of the Latch be an exit block out of the loop.
    LLVM_DEBUG(
        dbgs()
        << "One of the loop latch successors must be the exit block.\n");
    return false;
  }

  // These are exit blocks other than the target of the latch exiting block.
  SmallVector<BasicBlock *, 4> OtherExits;
  L->getUniqueNonLatchExitBlocks(OtherExits);
  // Support only single exit and exiting block unless multi-exit loop
  // unrolling is enabled.
  if (!L->getExitingBlock() || OtherExits.size()) {
    // We rely on LCSSA form being preserved when the exit blocks are transformed.
    // (Note that only an off-by-default mode of the old PM disables PreserveLCCA.)
    if (!PreserveLCSSA)
      return false;

    if (!canProfitablyUnrollMultiExitLoop(L, OtherExits, LatchExit,
                                          UseEpilogRemainder)) {
      LLVM_DEBUG(
          dbgs()
          << "Multiple exit/exiting blocks in loop and multi-exit unrolling not "
             "enabled!\n");
      return false;
    }
  }
  // Use Scalar Evolution to compute the trip count. This allows more loops to
  // be unrolled than relying on induction var simplification.
  if (!SE)
    return false;

  // Only unroll loops with a computable trip count.
  // We calculate the backedge count by using getExitCount on the Latch block,
  // which is proven to be the only exiting block in this loop. This is same as
  // calculating getBackedgeTakenCount on the loop (which computes SCEV for all
  // exiting blocks).
  const SCEV *BECountSC = SE->getExitCount(L, Latch);
  if (isa<SCEVCouldNotCompute>(BECountSC)) {
    LLVM_DEBUG(dbgs() << "Could not compute exit block SCEV\n");
    return false;
  }

  unsigned BEWidth = cast<IntegerType>(BECountSC->getType())->getBitWidth();

  // Add 1 since the backedge count doesn't include the first loop iteration.
  // (Note that overflow can occur, this is handled explicitly below)
  const SCEV *TripCountSC =
      SE->getAddExpr(BECountSC, SE->getConstant(BECountSC->getType(), 1));
  if (isa<SCEVCouldNotCompute>(TripCountSC)) {
    LLVM_DEBUG(dbgs() << "Could not compute trip count SCEV.\n");
    return false;
  }

  BasicBlock *PreHeader = L->getLoopPreheader();
  BranchInst *PreHeaderBR = cast<BranchInst>(PreHeader->getTerminator());
  const DataLayout &DL = Header->getDataLayout();
  SCEVExpander Expander(*SE, DL, "loop-unroll");
  if (!AllowExpensiveTripCount &&
      Expander.isHighCostExpansion(TripCountSC, L, SCEVCheapExpansionBudget,
                                   TTI, PreHeaderBR)) {
    LLVM_DEBUG(dbgs() << "High cost for expanding trip count scev!\n");
    return false;
  }

  // This constraint lets us deal with an overflowing trip count easily; see the
  // comment on ModVal below.
  if (Log2_32(Count) > BEWidth) {
    LLVM_DEBUG(
        dbgs()
        << "Count failed constraint on overflow trip count calculation.\n");
    return false;
  }

  // Loop structure is the following:
  //
  // PreHeader
  //   Header
  //   ...
  //   Latch
  // LatchExit

  BasicBlock *NewPreHeader;
  BasicBlock *NewExit = nullptr;
  BasicBlock *PrologExit = nullptr;
  BasicBlock *EpilogPreHeader = nullptr;
  BasicBlock *PrologPreHeader = nullptr;

  if (UseEpilogRemainder) {
    // If epilog remainder
    // Split PreHeader to insert a branch around loop for unrolling.
    NewPreHeader = SplitBlock(PreHeader, PreHeader->getTerminator(), DT, LI);
    NewPreHeader->setName(PreHeader->getName() + ".new");
    // Split LatchExit to create phi nodes from branch above.
    NewExit = SplitBlockPredecessors(LatchExit, {Latch}, ".unr-lcssa", DT, LI,
                                     nullptr, PreserveLCSSA);
    // NewExit gets its DebugLoc from LatchExit, which is not part of the
    // original Loop.
    // Fix this by setting Loop's DebugLoc to NewExit.
    auto *NewExitTerminator = NewExit->getTerminator();
    NewExitTerminator->setDebugLoc(Header->getTerminator()->getDebugLoc());
    // Split NewExit to insert epilog remainder loop.
    EpilogPreHeader = SplitBlock(NewExit, NewExitTerminator, DT, LI);
    EpilogPreHeader->setName(Header->getName() + ".epil.preheader");

    // If the latch exits from multiple level of nested loops, then
    // by assumption there must be another loop exit which branches to the
    // outer loop and we must adjust the loop for the newly inserted blocks
    // to account for the fact that our epilogue is still in the same outer
    // loop. Note that this leaves loopinfo temporarily out of sync with the
    // CFG until the actual epilogue loop is inserted.
    if (auto *ParentL = L->getParentLoop())
      if (LI->getLoopFor(LatchExit) != ParentL) {
        LI->removeBlock(NewExit);
        ParentL->addBasicBlockToLoop(NewExit, *LI);
        LI->removeBlock(EpilogPreHeader);
        ParentL->addBasicBlockToLoop(EpilogPreHeader, *LI);
      }

  } else {
    // If prolog remainder
    // Split the original preheader twice to insert prolog remainder loop
    PrologPreHeader = SplitEdge(PreHeader, Header, DT, LI);
    PrologPreHeader->setName(Header->getName() + ".prol.preheader");
    PrologExit = SplitBlock(PrologPreHeader, PrologPreHeader->getTerminator(),
                            DT, LI);
    PrologExit->setName(Header->getName() + ".prol.loopexit");
    // Split PrologExit to get NewPreHeader.
    NewPreHeader = SplitBlock(PrologExit, PrologExit->getTerminator(), DT, LI);
    NewPreHeader->setName(PreHeader->getName() + ".new");
  }
  // Loop structure should be the following:
  //  Epilog             Prolog
  //
  // PreHeader         PreHeader
  // *NewPreHeader     *PrologPreHeader
  //   Header          *PrologExit
  //   ...             *NewPreHeader
  //   Latch             Header
  // *NewExit            ...
  // *EpilogPreHeader    Latch
  // LatchExit              LatchExit

  // Calculate conditions for branch around loop for unrolling
  // in epilog case and around prolog remainder loop in prolog case.
  // Compute the number of extra iterations required, which is:
  //  extra iterations = run-time trip count % loop unroll factor
  PreHeaderBR = cast<BranchInst>(PreHeader->getTerminator());
  IRBuilder<> B(PreHeaderBR);
  Value *TripCount = Expander.expandCodeFor(TripCountSC, TripCountSC->getType(),
                                            PreHeaderBR);
  Value *BECount;
  // If there are other exits before the latch, that may cause the latch exit
  // branch to never be executed, and the latch exit count may be poison.
  // In this case, freeze the TripCount and base BECount on the frozen
  // TripCount. We will introduce two branches using these values, and it's
  // important that they see a consistent value (which would not be guaranteed
  // if were frozen independently.)
  if ((!OtherExits.empty() || !SE->loopHasNoAbnormalExits(L)) &&
      !isGuaranteedNotToBeUndefOrPoison(TripCount, AC, PreHeaderBR, DT)) {
    TripCount = B.CreateFreeze(TripCount);
    BECount =
        B.CreateAdd(TripCount, Constant::getAllOnesValue(TripCount->getType()));
  } else {
    // If we don't need to freeze, use SCEVExpander for BECount as well, to
    // allow slightly better value reuse.
    BECount =
        Expander.expandCodeFor(BECountSC, BECountSC->getType(), PreHeaderBR);
  }

  Value * const ModVal = CreateTripRemainder(B, BECount, TripCount, Count);

  Value *BranchVal =
      UseEpilogRemainder ? B.CreateICmpULT(BECount,
                                           ConstantInt::get(BECount->getType(),
                                                            Count - 1)) :
                           B.CreateIsNotNull(ModVal, "lcmp.mod");
  BasicBlock *RemainderLoop = UseEpilogRemainder ? NewExit : PrologPreHeader;
  BasicBlock *UnrollingLoop = UseEpilogRemainder ? NewPreHeader : PrologExit;
  // Branch to either remainder (extra iterations) loop or unrolling loop.
  MDNode *BranchWeights = nullptr;
  if (hasBranchWeightMD(*Latch->getTerminator())) {
    // Assume loop is nearly always entered.
    MDBuilder MDB(B.getContext());
    BranchWeights = MDB.createBranchWeights(EpilogHeaderWeights);
  }
  B.CreateCondBr(BranchVal, RemainderLoop, UnrollingLoop, BranchWeights);
  PreHeaderBR->eraseFromParent();
  if (DT) {
    if (UseEpilogRemainder)
      DT->changeImmediateDominator(NewExit, PreHeader);
    else
      DT->changeImmediateDominator(PrologExit, PreHeader);
  }
  Function *F = Header->getParent();
  // Get an ordered list of blocks in the loop to help with the ordering of the
  // cloned blocks in the prolog/epilog code
  LoopBlocksDFS LoopBlocks(L);
  LoopBlocks.perform(LI);

  //
  // For each extra loop iteration, create a copy of the loop's basic blocks
  // and generate a condition that branches to the copy depending on the
  // number of 'left over' iterations.
  //
  std::vector<BasicBlock *> NewBlocks;
  ValueToValueMapTy VMap;

  // Clone all the basic blocks in the loop. If Count is 2, we don't clone
  // the loop, otherwise we create a cloned loop to execute the extra
  // iterations. This function adds the appropriate CFG connections.
  BasicBlock *InsertBot = UseEpilogRemainder ? LatchExit : PrologExit;
  BasicBlock *InsertTop = UseEpilogRemainder ? EpilogPreHeader : PrologPreHeader;
  Loop *remainderLoop = CloneLoopBlocks(
      L, ModVal, UseEpilogRemainder, UnrollRemainder, InsertTop, InsertBot,
      NewPreHeader, NewBlocks, LoopBlocks, VMap, DT, LI, Count);

  // Insert the cloned blocks into the function.
  F->splice(InsertBot->getIterator(), F, NewBlocks[0]->getIterator(), F->end());

  // Now the loop blocks are cloned and the other exiting blocks from the
  // remainder are connected to the original Loop's exit blocks. The remaining
  // work is to update the phi nodes in the original loop, and take in the
  // values from the cloned region.
  for (auto *BB : OtherExits) {
    // Given we preserve LCSSA form, we know that the values used outside the
    // loop will be used through these phi nodes at the exit blocks that are
    // transformed below.
    for (PHINode &PN : BB->phis()) {
     unsigned oldNumOperands = PN.getNumIncomingValues();
     // Add the incoming values from the remainder code to the end of the phi
     // node.
     for (unsigned i = 0; i < oldNumOperands; i++){
       auto *PredBB =PN.getIncomingBlock(i);
       if (PredBB == Latch)
         // The latch exit is handled separately, see connectX
         continue;
       if (!L->contains(PredBB))
         // Even if we had dedicated exits, the code above inserted an
         // extra branch which can reach the latch exit.
         continue;

       auto *V = PN.getIncomingValue(i);
       if (Instruction *I = dyn_cast<Instruction>(V))
         if (L->contains(I))
           V = VMap.lookup(I);
       PN.addIncoming(V, cast<BasicBlock>(VMap[PredBB]));
     }
   }
#if defined(EXPENSIVE_CHECKS) && !defined(NDEBUG)
    for (BasicBlock *SuccBB : successors(BB)) {
      assert(!(llvm::is_contained(OtherExits, SuccBB) || SuccBB == LatchExit) &&
             "Breaks the definition of dedicated exits!");
    }
#endif
  }

  // Update the immediate dominator of the exit blocks and blocks that are
  // reachable from the exit blocks. This is needed because we now have paths
  // from both the original loop and the remainder code reaching the exit
  // blocks. While the IDom of these exit blocks were from the original loop,
  // now the IDom is the preheader (which decides whether the original loop or
  // remainder code should run).
  if (DT && !L->getExitingBlock()) {
    SmallVector<BasicBlock *, 16> ChildrenToUpdate;
    // NB! We have to examine the dom children of all loop blocks, not just
    // those which are the IDom of the exit blocks. This is because blocks
    // reachable from the exit blocks can have their IDom as the nearest common
    // dominator of the exit blocks.
    for (auto *BB : L->blocks()) {
      auto *DomNodeBB = DT->getNode(BB);
      for (auto *DomChild : DomNodeBB->children()) {
        auto *DomChildBB = DomChild->getBlock();
        if (!L->contains(LI->getLoopFor(DomChildBB)))
          ChildrenToUpdate.push_back(DomChildBB);
      }
    }
    for (auto *BB : ChildrenToUpdate)
      DT->changeImmediateDominator(BB, PreHeader);
  }

  // Loop structure should be the following:
  //  Epilog             Prolog
  //
  // PreHeader         PreHeader
  // NewPreHeader      PrologPreHeader
  //   Header            PrologHeader
  //   ...               ...
  //   Latch             PrologLatch
  // NewExit           PrologExit
  // EpilogPreHeader   NewPreHeader
  //   EpilogHeader      Header
  //   ...               ...
  //   EpilogLatch       Latch
  // LatchExit              LatchExit

  // Rewrite the cloned instruction operands to use the values created when the
  // clone is created.
  for (BasicBlock *BB : NewBlocks) {
    Module *M = BB->getModule();
    for (Instruction &I : *BB) {
      RemapInstruction(&I, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      RemapDbgRecordRange(M, I.getDbgRecordRange(), VMap,
                          RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
    }
  }

  if (UseEpilogRemainder) {
    // Connect the epilog code to the original loop and update the
    // PHI functions.
    ConnectEpilog(L, ModVal, NewExit, LatchExit, PreHeader, EpilogPreHeader,
                  NewPreHeader, VMap, DT, LI, PreserveLCSSA, *SE, Count);

    // Update counter in loop for unrolling.
    // Use an incrementing IV.  Pre-incr/post-incr is backedge/trip count.
    // Subtle: TestVal can be 0 if we wrapped when computing the trip count,
    // thus we must compare the post-increment (wrapping) value.
    IRBuilder<> B2(NewPreHeader->getTerminator());
    Value *TestVal = B2.CreateSub(TripCount, ModVal, "unroll_iter");
    BranchInst *LatchBR = cast<BranchInst>(Latch->getTerminator());
    PHINode *NewIdx = PHINode::Create(TestVal->getType(), 2, "niter");
    NewIdx->insertBefore(Header->getFirstNonPHIIt());
    B2.SetInsertPoint(LatchBR);
    auto *Zero = ConstantInt::get(NewIdx->getType(), 0);
    auto *One = ConstantInt::get(NewIdx->getType(), 1);
    Value *IdxNext = B2.CreateAdd(NewIdx, One, NewIdx->getName() + ".next");
    auto Pred = LatchBR->getSuccessor(0) == Header ? ICmpInst::ICMP_NE : ICmpInst::ICMP_EQ;
    Value *IdxCmp = B2.CreateICmp(Pred, IdxNext, TestVal, NewIdx->getName() + ".ncmp");
    NewIdx->addIncoming(Zero, NewPreHeader);
    NewIdx->addIncoming(IdxNext, Latch);
    LatchBR->setCondition(IdxCmp);
  } else {
    // Connect the prolog code to the original loop and update the
    // PHI functions.
    ConnectProlog(L, BECount, Count, PrologExit, LatchExit, PreHeader,
                  NewPreHeader, VMap, DT, LI, PreserveLCSSA, *SE);
  }

  // If this loop is nested, then the loop unroller changes the code in the any
  // of its parent loops, so the Scalar Evolution pass needs to be run again.
  SE->forgetTopmostLoop(L);

  // Verify that the Dom Tree and Loop Info are correct.
#if defined(EXPENSIVE_CHECKS) && !defined(NDEBUG)
  if (DT) {
    assert(DT->verify(DominatorTree::VerificationLevel::Full));
    LI->verify(*DT);
  }
#endif

  // For unroll factor 2 remainder loop will have 1 iteration.
  if (Count == 2 && DT && LI && SE) {
    // TODO: This code could probably be pulled out into a helper function
    // (e.g. breakLoopBackedgeAndSimplify) and reused in loop-deletion.
    BasicBlock *RemainderLatch = remainderLoop->getLoopLatch();
    assert(RemainderLatch);
    SmallVector<BasicBlock*> RemainderBlocks(remainderLoop->getBlocks().begin(),
                                             remainderLoop->getBlocks().end());
    breakLoopBackedge(remainderLoop, *DT, *SE, *LI, nullptr);
    remainderLoop = nullptr;

    // Simplify loop values after breaking the backedge
    const DataLayout &DL = L->getHeader()->getDataLayout();
    SmallVector<WeakTrackingVH, 16> DeadInsts;
    for (BasicBlock *BB : RemainderBlocks) {
      for (Instruction &Inst : llvm::make_early_inc_range(*BB)) {
        if (Value *V = simplifyInstruction(&Inst, {DL, nullptr, DT, AC}))
          if (LI->replacementPreservesLCSSAForm(&Inst, V))
            Inst.replaceAllUsesWith(V);
        if (isInstructionTriviallyDead(&Inst))
          DeadInsts.emplace_back(&Inst);
      }
      // We can't do recursive deletion until we're done iterating, as we might
      // have a phi which (potentially indirectly) uses instructions later in
      // the block we're iterating through.
      RecursivelyDeleteTriviallyDeadInstructions(DeadInsts);
    }

    // Merge latch into exit block.
    auto *ExitBB = RemainderLatch->getSingleSuccessor();
    assert(ExitBB && "required after breaking cond br backedge");
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
    MergeBlockIntoPredecessor(ExitBB, &DTU, LI);
  }

  // Canonicalize to LoopSimplifyForm both original and remainder loops. We
  // cannot rely on the LoopUnrollPass to do this because it only does
  // canonicalization for parent/subloops and not the sibling loops.
  if (OtherExits.size() > 0) {
    // Generate dedicated exit blocks for the original loop, to preserve
    // LoopSimplifyForm.
    formDedicatedExitBlocks(L, DT, LI, nullptr, PreserveLCSSA);
    // Generate dedicated exit blocks for the remainder loop if one exists, to
    // preserve LoopSimplifyForm.
    if (remainderLoop)
      formDedicatedExitBlocks(remainderLoop, DT, LI, nullptr, PreserveLCSSA);
  }

  auto UnrollResult = LoopUnrollResult::Unmodified;
  if (remainderLoop && UnrollRemainder) {
    LLVM_DEBUG(dbgs() << "Unrolling remainder loop\n");
    UnrollLoopOptions ULO;
    ULO.Count = Count - 1;
    ULO.Force = false;
    ULO.Runtime = false;
    ULO.AllowExpensiveTripCount = false;
    ULO.UnrollRemainder = false;
    ULO.ForgetAllSCEV = ForgetAllSCEV;
    assert(!getLoopConvergenceHeart(L) &&
           "A loop with a convergence heart does not allow runtime unrolling.");
    UnrollResult = UnrollLoop(remainderLoop, ULO, LI, SE, DT, AC, TTI,
                              /*ORE*/ nullptr, PreserveLCSSA);
  }

  if (ResultLoop && UnrollResult != LoopUnrollResult::FullyUnrolled)
    *ResultLoop = remainderLoop;
  NumRuntimeUnrolled++;
  return true;
}
