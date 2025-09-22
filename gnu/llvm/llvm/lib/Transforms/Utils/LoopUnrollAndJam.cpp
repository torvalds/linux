//===-- LoopUnrollAndJam.cpp - Loop unrolling utilities -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements loop unroll and jam as a routine, much like
// LoopUnroll.cpp implements loop unroll.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <assert.h>
#include <memory>
#include <type_traits>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "loop-unroll-and-jam"

STATISTIC(NumUnrolledAndJammed, "Number of loops unroll and jammed");
STATISTIC(NumCompletelyUnrolledAndJammed, "Number of loops unroll and jammed");

typedef SmallPtrSet<BasicBlock *, 4> BasicBlockSet;

// Partition blocks in an outer/inner loop pair into blocks before and after
// the loop
static bool partitionLoopBlocks(Loop &L, BasicBlockSet &ForeBlocks,
                                BasicBlockSet &AftBlocks, DominatorTree &DT) {
  Loop *SubLoop = L.getSubLoops()[0];
  BasicBlock *SubLoopLatch = SubLoop->getLoopLatch();

  for (BasicBlock *BB : L.blocks()) {
    if (!SubLoop->contains(BB)) {
      if (DT.dominates(SubLoopLatch, BB))
        AftBlocks.insert(BB);
      else
        ForeBlocks.insert(BB);
    }
  }

  // Check that all blocks in ForeBlocks together dominate the subloop
  // TODO: This might ideally be done better with a dominator/postdominators.
  BasicBlock *SubLoopPreHeader = SubLoop->getLoopPreheader();
  for (BasicBlock *BB : ForeBlocks) {
    if (BB == SubLoopPreHeader)
      continue;
    Instruction *TI = BB->getTerminator();
    for (BasicBlock *Succ : successors(TI))
      if (!ForeBlocks.count(Succ))
        return false;
  }

  return true;
}

/// Partition blocks in a loop nest into blocks before and after each inner
/// loop.
static bool partitionOuterLoopBlocks(
    Loop &Root, Loop &JamLoop, BasicBlockSet &JamLoopBlocks,
    DenseMap<Loop *, BasicBlockSet> &ForeBlocksMap,
    DenseMap<Loop *, BasicBlockSet> &AftBlocksMap, DominatorTree &DT) {
  JamLoopBlocks.insert(JamLoop.block_begin(), JamLoop.block_end());

  for (Loop *L : Root.getLoopsInPreorder()) {
    if (L == &JamLoop)
      break;

    if (!partitionLoopBlocks(*L, ForeBlocksMap[L], AftBlocksMap[L], DT))
      return false;
  }

  return true;
}

// TODO Remove when UnrollAndJamLoop changed to support unroll and jamming more
// than 2 levels loop.
static bool partitionOuterLoopBlocks(Loop *L, Loop *SubLoop,
                                     BasicBlockSet &ForeBlocks,
                                     BasicBlockSet &SubLoopBlocks,
                                     BasicBlockSet &AftBlocks,
                                     DominatorTree *DT) {
  SubLoopBlocks.insert(SubLoop->block_begin(), SubLoop->block_end());
  return partitionLoopBlocks(*L, ForeBlocks, AftBlocks, *DT);
}

// Looks at the phi nodes in Header for values coming from Latch. For these
// instructions and all their operands calls Visit on them, keeping going for
// all the operands in AftBlocks. Returns false if Visit returns false,
// otherwise returns true. This is used to process the instructions in the
// Aft blocks that need to be moved before the subloop. It is used in two
// places. One to check that the required set of instructions can be moved
// before the loop. Then to collect the instructions to actually move in
// moveHeaderPhiOperandsToForeBlocks.
template <typename T>
static bool processHeaderPhiOperands(BasicBlock *Header, BasicBlock *Latch,
                                     BasicBlockSet &AftBlocks, T Visit) {
  SmallPtrSet<Instruction *, 8> VisitedInstr;

  std::function<bool(Instruction * I)> ProcessInstr = [&](Instruction *I) {
    if (VisitedInstr.count(I))
      return true;

    VisitedInstr.insert(I);

    if (AftBlocks.count(I->getParent()))
      for (auto &U : I->operands())
        if (Instruction *II = dyn_cast<Instruction>(U))
          if (!ProcessInstr(II))
            return false;

    return Visit(I);
  };

  for (auto &Phi : Header->phis()) {
    Value *V = Phi.getIncomingValueForBlock(Latch);
    if (Instruction *I = dyn_cast<Instruction>(V))
      if (!ProcessInstr(I))
        return false;
  }

  return true;
}

// Move the phi operands of Header from Latch out of AftBlocks to InsertLoc.
static void moveHeaderPhiOperandsToForeBlocks(BasicBlock *Header,
                                              BasicBlock *Latch,
                                              Instruction *InsertLoc,
                                              BasicBlockSet &AftBlocks) {
  // We need to ensure we move the instructions in the correct order,
  // starting with the earliest required instruction and moving forward.
  processHeaderPhiOperands(Header, Latch, AftBlocks,
                           [&AftBlocks, &InsertLoc](Instruction *I) {
                             if (AftBlocks.count(I->getParent()))
                               I->moveBefore(InsertLoc);
                             return true;
                           });
}

/*
  This method performs Unroll and Jam. For a simple loop like:
  for (i = ..)
    Fore(i)
    for (j = ..)
      SubLoop(i, j)
    Aft(i)

  Instead of doing normal inner or outer unrolling, we do:
  for (i = .., i+=2)
    Fore(i)
    Fore(i+1)
    for (j = ..)
      SubLoop(i, j)
      SubLoop(i+1, j)
    Aft(i)
    Aft(i+1)

  So the outer loop is essetially unrolled and then the inner loops are fused
  ("jammed") together into a single loop. This can increase speed when there
  are loads in SubLoop that are invariant to i, as they become shared between
  the now jammed inner loops.

  We do this by spliting the blocks in the loop into Fore, Subloop and Aft.
  Fore blocks are those before the inner loop, Aft are those after. Normal
  Unroll code is used to copy each of these sets of blocks and the results are
  combined together into the final form above.

  isSafeToUnrollAndJam should be used prior to calling this to make sure the
  unrolling will be valid. Checking profitablility is also advisable.

  If EpilogueLoop is non-null, it receives the epilogue loop (if it was
  necessary to create one and not fully unrolled).
*/
LoopUnrollResult
llvm::UnrollAndJamLoop(Loop *L, unsigned Count, unsigned TripCount,
                       unsigned TripMultiple, bool UnrollRemainder,
                       LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT,
                       AssumptionCache *AC, const TargetTransformInfo *TTI,
                       OptimizationRemarkEmitter *ORE, Loop **EpilogueLoop) {

  // When we enter here we should have already checked that it is safe
  BasicBlock *Header = L->getHeader();
  assert(Header && "No header.");
  assert(L->getSubLoops().size() == 1);
  Loop *SubLoop = *L->begin();

  // Don't enter the unroll code if there is nothing to do.
  if (TripCount == 0 && Count < 2) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; almost nothing to do\n");
    return LoopUnrollResult::Unmodified;
  }

  assert(Count > 0);
  assert(TripMultiple > 0);
  assert(TripCount == 0 || TripCount % TripMultiple == 0);

  // Are we eliminating the loop control altogether?
  bool CompletelyUnroll = (Count == TripCount);

  // We use the runtime remainder in cases where we don't know trip multiple
  if (TripMultiple % Count != 0) {
    if (!UnrollRuntimeLoopRemainder(L, Count, /*AllowExpensiveTripCount*/ false,
                                    /*UseEpilogRemainder*/ true,
                                    UnrollRemainder, /*ForgetAllSCEV*/ false,
                                    LI, SE, DT, AC, TTI, true, EpilogueLoop)) {
      LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; remainder loop could not be "
                           "generated when assuming runtime trip count\n");
      return LoopUnrollResult::Unmodified;
    }
  }

  // Notify ScalarEvolution that the loop will be substantially changed,
  // if not outright eliminated.
  if (SE) {
    SE->forgetLoop(L);
    SE->forgetBlockAndLoopDispositions();
  }

  using namespace ore;
  // Report the unrolling decision.
  if (CompletelyUnroll) {
    LLVM_DEBUG(dbgs() << "COMPLETELY UNROLL AND JAMMING loop %"
                      << Header->getName() << " with trip count " << TripCount
                      << "!\n");
    ORE->emit(OptimizationRemark(DEBUG_TYPE, "FullyUnrolled", L->getStartLoc(),
                                 L->getHeader())
              << "completely unroll and jammed loop with "
              << NV("UnrollCount", TripCount) << " iterations");
  } else {
    auto DiagBuilder = [&]() {
      OptimizationRemark Diag(DEBUG_TYPE, "PartialUnrolled", L->getStartLoc(),
                              L->getHeader());
      return Diag << "unroll and jammed loop by a factor of "
                  << NV("UnrollCount", Count);
    };

    LLVM_DEBUG(dbgs() << "UNROLL AND JAMMING loop %" << Header->getName()
                      << " by " << Count);
    if (TripMultiple != 1) {
      LLVM_DEBUG(dbgs() << " with " << TripMultiple << " trips per branch");
      ORE->emit([&]() {
        return DiagBuilder() << " with " << NV("TripMultiple", TripMultiple)
                             << " trips per branch";
      });
    } else {
      LLVM_DEBUG(dbgs() << " with run-time trip count");
      ORE->emit([&]() { return DiagBuilder() << " with run-time trip count"; });
    }
    LLVM_DEBUG(dbgs() << "!\n");
  }

  BasicBlock *Preheader = L->getLoopPreheader();
  BasicBlock *LatchBlock = L->getLoopLatch();
  assert(Preheader && "No preheader");
  assert(LatchBlock && "No latch block");
  BranchInst *BI = dyn_cast<BranchInst>(LatchBlock->getTerminator());
  assert(BI && !BI->isUnconditional());
  bool ContinueOnTrue = L->contains(BI->getSuccessor(0));
  BasicBlock *LoopExit = BI->getSuccessor(ContinueOnTrue);
  bool SubLoopContinueOnTrue = SubLoop->contains(
      SubLoop->getLoopLatch()->getTerminator()->getSuccessor(0));

  // Partition blocks in an outer/inner loop pair into blocks before and after
  // the loop
  BasicBlockSet SubLoopBlocks;
  BasicBlockSet ForeBlocks;
  BasicBlockSet AftBlocks;
  partitionOuterLoopBlocks(L, SubLoop, ForeBlocks, SubLoopBlocks, AftBlocks,
                           DT);

  // We keep track of the entering/first and exiting/last block of each of
  // Fore/SubLoop/Aft in each iteration. This helps make the stapling up of
  // blocks easier.
  std::vector<BasicBlock *> ForeBlocksFirst;
  std::vector<BasicBlock *> ForeBlocksLast;
  std::vector<BasicBlock *> SubLoopBlocksFirst;
  std::vector<BasicBlock *> SubLoopBlocksLast;
  std::vector<BasicBlock *> AftBlocksFirst;
  std::vector<BasicBlock *> AftBlocksLast;
  ForeBlocksFirst.push_back(Header);
  ForeBlocksLast.push_back(SubLoop->getLoopPreheader());
  SubLoopBlocksFirst.push_back(SubLoop->getHeader());
  SubLoopBlocksLast.push_back(SubLoop->getExitingBlock());
  AftBlocksFirst.push_back(SubLoop->getExitBlock());
  AftBlocksLast.push_back(L->getExitingBlock());
  // Maps Blocks[0] -> Blocks[It]
  ValueToValueMapTy LastValueMap;

  // Move any instructions from fore phi operands from AftBlocks into Fore.
  moveHeaderPhiOperandsToForeBlocks(
      Header, LatchBlock, ForeBlocksLast[0]->getTerminator(), AftBlocks);

  // The current on-the-fly SSA update requires blocks to be processed in
  // reverse postorder so that LastValueMap contains the correct value at each
  // exit.
  LoopBlocksDFS DFS(L);
  DFS.perform(LI);
  // Stash the DFS iterators before adding blocks to the loop.
  LoopBlocksDFS::RPOIterator BlockBegin = DFS.beginRPO();
  LoopBlocksDFS::RPOIterator BlockEnd = DFS.endRPO();

  // When a FSDiscriminator is enabled, we don't need to add the multiply
  // factors to the discriminators.
  if (Header->getParent()->shouldEmitDebugInfoForProfiling() &&
      !EnableFSDiscriminator)
    for (BasicBlock *BB : L->getBlocks())
      for (Instruction &I : *BB)
        if (!I.isDebugOrPseudoInst())
          if (const DILocation *DIL = I.getDebugLoc()) {
            auto NewDIL = DIL->cloneByMultiplyingDuplicationFactor(Count);
            if (NewDIL)
              I.setDebugLoc(*NewDIL);
            else
              LLVM_DEBUG(dbgs()
                         << "Failed to create new discriminator: "
                         << DIL->getFilename() << " Line: " << DIL->getLine());
          }

  // Copy all blocks
  for (unsigned It = 1; It != Count; ++It) {
    SmallVector<BasicBlock *, 8> NewBlocks;
    // Maps Blocks[It] -> Blocks[It-1]
    DenseMap<Value *, Value *> PrevItValueMap;
    SmallDenseMap<const Loop *, Loop *, 4> NewLoops;
    NewLoops[L] = L;
    NewLoops[SubLoop] = SubLoop;

    for (LoopBlocksDFS::RPOIterator BB = BlockBegin; BB != BlockEnd; ++BB) {
      ValueToValueMapTy VMap;
      BasicBlock *New = CloneBasicBlock(*BB, VMap, "." + Twine(It));
      Header->getParent()->insert(Header->getParent()->end(), New);

      // Tell LI about New.
      addClonedBlockToLoopInfo(*BB, New, LI, NewLoops);

      if (ForeBlocks.count(*BB)) {
        if (*BB == ForeBlocksFirst[0])
          ForeBlocksFirst.push_back(New);
        if (*BB == ForeBlocksLast[0])
          ForeBlocksLast.push_back(New);
      } else if (SubLoopBlocks.count(*BB)) {
        if (*BB == SubLoopBlocksFirst[0])
          SubLoopBlocksFirst.push_back(New);
        if (*BB == SubLoopBlocksLast[0])
          SubLoopBlocksLast.push_back(New);
      } else if (AftBlocks.count(*BB)) {
        if (*BB == AftBlocksFirst[0])
          AftBlocksFirst.push_back(New);
        if (*BB == AftBlocksLast[0])
          AftBlocksLast.push_back(New);
      } else {
        llvm_unreachable("BB being cloned should be in Fore/Sub/Aft");
      }

      // Update our running maps of newest clones
      PrevItValueMap[New] = (It == 1 ? *BB : LastValueMap[*BB]);
      LastValueMap[*BB] = New;
      for (ValueToValueMapTy::iterator VI = VMap.begin(), VE = VMap.end();
           VI != VE; ++VI) {
        PrevItValueMap[VI->second] =
            const_cast<Value *>(It == 1 ? VI->first : LastValueMap[VI->first]);
        LastValueMap[VI->first] = VI->second;
      }

      NewBlocks.push_back(New);

      // Update DomTree:
      if (*BB == ForeBlocksFirst[0])
        DT->addNewBlock(New, ForeBlocksLast[It - 1]);
      else if (*BB == SubLoopBlocksFirst[0])
        DT->addNewBlock(New, SubLoopBlocksLast[It - 1]);
      else if (*BB == AftBlocksFirst[0])
        DT->addNewBlock(New, AftBlocksLast[It - 1]);
      else {
        // Each set of blocks (Fore/Sub/Aft) will have the same internal domtree
        // structure.
        auto BBDomNode = DT->getNode(*BB);
        auto BBIDom = BBDomNode->getIDom();
        BasicBlock *OriginalBBIDom = BBIDom->getBlock();
        assert(OriginalBBIDom);
        assert(LastValueMap[cast<Value>(OriginalBBIDom)]);
        DT->addNewBlock(
            New, cast<BasicBlock>(LastValueMap[cast<Value>(OriginalBBIDom)]));
      }
    }

    // Remap all instructions in the most recent iteration
    remapInstructionsInBlocks(NewBlocks, LastValueMap);
    for (BasicBlock *NewBlock : NewBlocks) {
      for (Instruction &I : *NewBlock) {
        if (auto *II = dyn_cast<AssumeInst>(&I))
          AC->registerAssumption(II);
      }
    }

    // Alter the ForeBlocks phi's, pointing them at the latest version of the
    // value from the previous iteration's phis
    for (PHINode &Phi : ForeBlocksFirst[It]->phis()) {
      Value *OldValue = Phi.getIncomingValueForBlock(AftBlocksLast[It]);
      assert(OldValue && "should have incoming edge from Aft[It]");
      Value *NewValue = OldValue;
      if (Value *PrevValue = PrevItValueMap[OldValue])
        NewValue = PrevValue;

      assert(Phi.getNumOperands() == 2);
      Phi.setIncomingBlock(0, ForeBlocksLast[It - 1]);
      Phi.setIncomingValue(0, NewValue);
      Phi.removeIncomingValue(1);
    }
  }

  // Now that all the basic blocks for the unrolled iterations are in place,
  // finish up connecting the blocks and phi nodes. At this point LastValueMap
  // is the last unrolled iterations values.

  // Update Phis in BB from OldBB to point to NewBB and use the latest value
  // from LastValueMap
  auto updatePHIBlocksAndValues = [](BasicBlock *BB, BasicBlock *OldBB,
                                     BasicBlock *NewBB,
                                     ValueToValueMapTy &LastValueMap) {
    for (PHINode &Phi : BB->phis()) {
      for (unsigned b = 0; b < Phi.getNumIncomingValues(); ++b) {
        if (Phi.getIncomingBlock(b) == OldBB) {
          Value *OldValue = Phi.getIncomingValue(b);
          if (Value *LastValue = LastValueMap[OldValue])
            Phi.setIncomingValue(b, LastValue);
          Phi.setIncomingBlock(b, NewBB);
          break;
        }
      }
    }
  };
  // Move all the phis from Src into Dest
  auto movePHIs = [](BasicBlock *Src, BasicBlock *Dest) {
    BasicBlock::iterator insertPoint = Dest->getFirstNonPHIIt();
    while (PHINode *Phi = dyn_cast<PHINode>(Src->begin()))
      Phi->moveBefore(*Dest, insertPoint);
  };

  // Update the PHI values outside the loop to point to the last block
  updatePHIBlocksAndValues(LoopExit, AftBlocksLast[0], AftBlocksLast.back(),
                           LastValueMap);

  // Update ForeBlocks successors and phi nodes
  BranchInst *ForeTerm =
      cast<BranchInst>(ForeBlocksLast.back()->getTerminator());
  assert(ForeTerm->getNumSuccessors() == 1 && "Expecting one successor");
  ForeTerm->setSuccessor(0, SubLoopBlocksFirst[0]);

  if (CompletelyUnroll) {
    while (PHINode *Phi = dyn_cast<PHINode>(ForeBlocksFirst[0]->begin())) {
      Phi->replaceAllUsesWith(Phi->getIncomingValueForBlock(Preheader));
      Phi->eraseFromParent();
    }
  } else {
    // Update the PHI values to point to the last aft block
    updatePHIBlocksAndValues(ForeBlocksFirst[0], AftBlocksLast[0],
                             AftBlocksLast.back(), LastValueMap);
  }

  for (unsigned It = 1; It != Count; It++) {
    // Remap ForeBlock successors from previous iteration to this
    BranchInst *ForeTerm =
        cast<BranchInst>(ForeBlocksLast[It - 1]->getTerminator());
    assert(ForeTerm->getNumSuccessors() == 1 && "Expecting one successor");
    ForeTerm->setSuccessor(0, ForeBlocksFirst[It]);
  }

  // Subloop successors and phis
  BranchInst *SubTerm =
      cast<BranchInst>(SubLoopBlocksLast.back()->getTerminator());
  SubTerm->setSuccessor(!SubLoopContinueOnTrue, SubLoopBlocksFirst[0]);
  SubTerm->setSuccessor(SubLoopContinueOnTrue, AftBlocksFirst[0]);
  SubLoopBlocksFirst[0]->replacePhiUsesWith(ForeBlocksLast[0],
                                            ForeBlocksLast.back());
  SubLoopBlocksFirst[0]->replacePhiUsesWith(SubLoopBlocksLast[0],
                                            SubLoopBlocksLast.back());

  for (unsigned It = 1; It != Count; It++) {
    // Replace the conditional branch of the previous iteration subloop with an
    // unconditional one to this one
    BranchInst *SubTerm =
        cast<BranchInst>(SubLoopBlocksLast[It - 1]->getTerminator());
    BranchInst::Create(SubLoopBlocksFirst[It], SubTerm->getIterator());
    SubTerm->eraseFromParent();

    SubLoopBlocksFirst[It]->replacePhiUsesWith(ForeBlocksLast[It],
                                               ForeBlocksLast.back());
    SubLoopBlocksFirst[It]->replacePhiUsesWith(SubLoopBlocksLast[It],
                                               SubLoopBlocksLast.back());
    movePHIs(SubLoopBlocksFirst[It], SubLoopBlocksFirst[0]);
  }

  // Aft blocks successors and phis
  BranchInst *AftTerm = cast<BranchInst>(AftBlocksLast.back()->getTerminator());
  if (CompletelyUnroll) {
    BranchInst::Create(LoopExit, AftTerm->getIterator());
    AftTerm->eraseFromParent();
  } else {
    AftTerm->setSuccessor(!ContinueOnTrue, ForeBlocksFirst[0]);
    assert(AftTerm->getSuccessor(ContinueOnTrue) == LoopExit &&
           "Expecting the ContinueOnTrue successor of AftTerm to be LoopExit");
  }
  AftBlocksFirst[0]->replacePhiUsesWith(SubLoopBlocksLast[0],
                                        SubLoopBlocksLast.back());

  for (unsigned It = 1; It != Count; It++) {
    // Replace the conditional branch of the previous iteration subloop with an
    // unconditional one to this one
    BranchInst *AftTerm =
        cast<BranchInst>(AftBlocksLast[It - 1]->getTerminator());
    BranchInst::Create(AftBlocksFirst[It], AftTerm->getIterator());
    AftTerm->eraseFromParent();

    AftBlocksFirst[It]->replacePhiUsesWith(SubLoopBlocksLast[It],
                                           SubLoopBlocksLast.back());
    movePHIs(AftBlocksFirst[It], AftBlocksFirst[0]);
  }

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  // Dominator Tree. Remove the old links between Fore, Sub and Aft, adding the
  // new ones required.
  if (Count != 1) {
    SmallVector<DominatorTree::UpdateType, 4> DTUpdates;
    DTUpdates.emplace_back(DominatorTree::UpdateKind::Delete, ForeBlocksLast[0],
                           SubLoopBlocksFirst[0]);
    DTUpdates.emplace_back(DominatorTree::UpdateKind::Delete,
                           SubLoopBlocksLast[0], AftBlocksFirst[0]);

    DTUpdates.emplace_back(DominatorTree::UpdateKind::Insert,
                           ForeBlocksLast.back(), SubLoopBlocksFirst[0]);
    DTUpdates.emplace_back(DominatorTree::UpdateKind::Insert,
                           SubLoopBlocksLast.back(), AftBlocksFirst[0]);
    DTU.applyUpdatesPermissive(DTUpdates);
  }

  // Merge adjacent basic blocks, if possible.
  SmallPtrSet<BasicBlock *, 16> MergeBlocks;
  MergeBlocks.insert(ForeBlocksLast.begin(), ForeBlocksLast.end());
  MergeBlocks.insert(SubLoopBlocksLast.begin(), SubLoopBlocksLast.end());
  MergeBlocks.insert(AftBlocksLast.begin(), AftBlocksLast.end());

  MergeBlockSuccessorsIntoGivenBlocks(MergeBlocks, L, &DTU, LI);

  // Apply updates to the DomTree.
  DT = &DTU.getDomTree();

  // At this point, the code is well formed.  We now do a quick sweep over the
  // inserted code, doing constant propagation and dead code elimination as we
  // go.
  simplifyLoopAfterUnroll(SubLoop, true, LI, SE, DT, AC, TTI);
  simplifyLoopAfterUnroll(L, !CompletelyUnroll && Count > 1, LI, SE, DT, AC,
                          TTI);

  NumCompletelyUnrolledAndJammed += CompletelyUnroll;
  ++NumUnrolledAndJammed;

  // Update LoopInfo if the loop is completely removed.
  if (CompletelyUnroll)
    LI->erase(L);

#ifndef NDEBUG
  // We shouldn't have done anything to break loop simplify form or LCSSA.
  Loop *OutestLoop = SubLoop->getParentLoop()
                         ? SubLoop->getParentLoop()->getParentLoop()
                               ? SubLoop->getParentLoop()->getParentLoop()
                               : SubLoop->getParentLoop()
                         : SubLoop;
  assert(DT->verify());
  LI->verify(*DT);
  assert(OutestLoop->isRecursivelyLCSSAForm(*DT, *LI));
  if (!CompletelyUnroll)
    assert(L->isLoopSimplifyForm());
  assert(SubLoop->isLoopSimplifyForm());
  SE->verify();
#endif

  return CompletelyUnroll ? LoopUnrollResult::FullyUnrolled
                          : LoopUnrollResult::PartiallyUnrolled;
}

static bool getLoadsAndStores(BasicBlockSet &Blocks,
                              SmallVector<Instruction *, 4> &MemInstr) {
  // Scan the BBs and collect legal loads and stores.
  // Returns false if non-simple loads/stores are found.
  for (BasicBlock *BB : Blocks) {
    for (Instruction &I : *BB) {
      if (auto *Ld = dyn_cast<LoadInst>(&I)) {
        if (!Ld->isSimple())
          return false;
        MemInstr.push_back(&I);
      } else if (auto *St = dyn_cast<StoreInst>(&I)) {
        if (!St->isSimple())
          return false;
        MemInstr.push_back(&I);
      } else if (I.mayReadOrWriteMemory()) {
        return false;
      }
    }
  }
  return true;
}

static bool preservesForwardDependence(Instruction *Src, Instruction *Dst,
                                       unsigned UnrollLevel, unsigned JamLevel,
                                       bool Sequentialized, Dependence *D) {
  // UnrollLevel might carry the dependency Src --> Dst
  // Does a different loop after unrolling?
  for (unsigned CurLoopDepth = UnrollLevel + 1; CurLoopDepth <= JamLevel;
       ++CurLoopDepth) {
    auto JammedDir = D->getDirection(CurLoopDepth);
    if (JammedDir == Dependence::DVEntry::LT)
      return true;

    if (JammedDir & Dependence::DVEntry::GT)
      return false;
  }

  return true;
}

static bool preservesBackwardDependence(Instruction *Src, Instruction *Dst,
                                        unsigned UnrollLevel, unsigned JamLevel,
                                        bool Sequentialized, Dependence *D) {
  // UnrollLevel might carry the dependency Dst --> Src
  for (unsigned CurLoopDepth = UnrollLevel + 1; CurLoopDepth <= JamLevel;
       ++CurLoopDepth) {
    auto JammedDir = D->getDirection(CurLoopDepth);
    if (JammedDir == Dependence::DVEntry::GT)
      return true;

    if (JammedDir & Dependence::DVEntry::LT)
      return false;
  }

  // Backward dependencies are only preserved if not interleaved.
  return Sequentialized;
}

// Check whether it is semantically safe Src and Dst considering any potential
// dependency between them.
//
// @param UnrollLevel The level of the loop being unrolled
// @param JamLevel    The level of the loop being jammed; if Src and Dst are on
// different levels, the outermost common loop counts as jammed level
//
// @return true if is safe and false if there is a dependency violation.
static bool checkDependency(Instruction *Src, Instruction *Dst,
                            unsigned UnrollLevel, unsigned JamLevel,
                            bool Sequentialized, DependenceInfo &DI) {
  assert(UnrollLevel <= JamLevel &&
         "Expecting JamLevel to be at least UnrollLevel");

  if (Src == Dst)
    return true;
  // Ignore Input dependencies.
  if (isa<LoadInst>(Src) && isa<LoadInst>(Dst))
    return true;

  // Check whether unroll-and-jam may violate a dependency.
  // By construction, every dependency will be lexicographically non-negative
  // (if it was, it would violate the current execution order), such as
  //   (0,0,>,*,*)
  // Unroll-and-jam changes the GT execution of two executions to the same
  // iteration of the chosen unroll level. That is, a GT dependence becomes a GE
  // dependence (or EQ, if we fully unrolled the loop) at the loop's position:
  //   (0,0,>=,*,*)
  // Now, the dependency is not necessarily non-negative anymore, i.e.
  // unroll-and-jam may violate correctness.
  std::unique_ptr<Dependence> D = DI.depends(Src, Dst, true);
  if (!D)
    return true;
  assert(D->isOrdered() && "Expected an output, flow or anti dep.");

  if (D->isConfused()) {
    LLVM_DEBUG(dbgs() << "  Confused dependency between:\n"
                      << "  " << *Src << "\n"
                      << "  " << *Dst << "\n");
    return false;
  }

  // If outer levels (levels enclosing the loop being unroll-and-jammed) have a
  // non-equal direction, then the locations accessed in the inner levels cannot
  // overlap in memory. We assumes the indexes never overlap into neighboring
  // dimensions.
  for (unsigned CurLoopDepth = 1; CurLoopDepth < UnrollLevel; ++CurLoopDepth)
    if (!(D->getDirection(CurLoopDepth) & Dependence::DVEntry::EQ))
      return true;

  auto UnrollDirection = D->getDirection(UnrollLevel);

  // If the distance carried by the unrolled loop is 0, then after unrolling
  // that distance will become non-zero resulting in non-overlapping accesses in
  // the inner loops.
  if (UnrollDirection == Dependence::DVEntry::EQ)
    return true;

  if (UnrollDirection & Dependence::DVEntry::LT &&
      !preservesForwardDependence(Src, Dst, UnrollLevel, JamLevel,
                                  Sequentialized, D.get()))
    return false;

  if (UnrollDirection & Dependence::DVEntry::GT &&
      !preservesBackwardDependence(Src, Dst, UnrollLevel, JamLevel,
                                   Sequentialized, D.get()))
    return false;

  return true;
}

static bool
checkDependencies(Loop &Root, const BasicBlockSet &SubLoopBlocks,
                  const DenseMap<Loop *, BasicBlockSet> &ForeBlocksMap,
                  const DenseMap<Loop *, BasicBlockSet> &AftBlocksMap,
                  DependenceInfo &DI, LoopInfo &LI) {
  SmallVector<BasicBlockSet, 8> AllBlocks;
  for (Loop *L : Root.getLoopsInPreorder())
    if (ForeBlocksMap.contains(L))
      AllBlocks.push_back(ForeBlocksMap.lookup(L));
  AllBlocks.push_back(SubLoopBlocks);
  for (Loop *L : Root.getLoopsInPreorder())
    if (AftBlocksMap.contains(L))
      AllBlocks.push_back(AftBlocksMap.lookup(L));

  unsigned LoopDepth = Root.getLoopDepth();
  SmallVector<Instruction *, 4> EarlierLoadsAndStores;
  SmallVector<Instruction *, 4> CurrentLoadsAndStores;
  for (BasicBlockSet &Blocks : AllBlocks) {
    CurrentLoadsAndStores.clear();
    if (!getLoadsAndStores(Blocks, CurrentLoadsAndStores))
      return false;

    Loop *CurLoop = LI.getLoopFor((*Blocks.begin())->front().getParent());
    unsigned CurLoopDepth = CurLoop->getLoopDepth();

    for (auto *Earlier : EarlierLoadsAndStores) {
      Loop *EarlierLoop = LI.getLoopFor(Earlier->getParent());
      unsigned EarlierDepth = EarlierLoop->getLoopDepth();
      unsigned CommonLoopDepth = std::min(EarlierDepth, CurLoopDepth);
      for (auto *Later : CurrentLoadsAndStores) {
        if (!checkDependency(Earlier, Later, LoopDepth, CommonLoopDepth, false,
                             DI))
          return false;
      }
    }

    size_t NumInsts = CurrentLoadsAndStores.size();
    for (size_t I = 0; I < NumInsts; ++I) {
      for (size_t J = I; J < NumInsts; ++J) {
        if (!checkDependency(CurrentLoadsAndStores[I], CurrentLoadsAndStores[J],
                             LoopDepth, CurLoopDepth, true, DI))
          return false;
      }
    }

    EarlierLoadsAndStores.append(CurrentLoadsAndStores.begin(),
                                 CurrentLoadsAndStores.end());
  }
  return true;
}

static bool isEligibleLoopForm(const Loop &Root) {
  // Root must have a child.
  if (Root.getSubLoops().size() != 1)
    return false;

  const Loop *L = &Root;
  do {
    // All loops in Root need to be in simplify and rotated form.
    if (!L->isLoopSimplifyForm())
      return false;

    if (!L->isRotatedForm())
      return false;

    if (L->getHeader()->hasAddressTaken()) {
      LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Address taken\n");
      return false;
    }

    unsigned SubLoopsSize = L->getSubLoops().size();
    if (SubLoopsSize == 0)
      return true;

    // Only one child is allowed.
    if (SubLoopsSize != 1)
      return false;

    // Only loops with a single exit block can be unrolled and jammed.
    // The function getExitBlock() is used for this check, rather than
    // getUniqueExitBlock() to ensure loops with mulitple exit edges are
    // disallowed.
    if (!L->getExitBlock()) {
      LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; only loops with single exit "
                           "blocks can be unrolled and jammed.\n");
      return false;
    }

    // Only loops with a single exiting block can be unrolled and jammed.
    if (!L->getExitingBlock()) {
      LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; only loops with single "
                           "exiting blocks can be unrolled and jammed.\n");
      return false;
    }

    L = L->getSubLoops()[0];
  } while (L);

  return true;
}

static Loop *getInnerMostLoop(Loop *L) {
  while (!L->getSubLoops().empty())
    L = L->getSubLoops()[0];
  return L;
}

bool llvm::isSafeToUnrollAndJam(Loop *L, ScalarEvolution &SE, DominatorTree &DT,
                                DependenceInfo &DI, LoopInfo &LI) {
  if (!isEligibleLoopForm(*L)) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Ineligible loop form\n");
    return false;
  }

  /* We currently handle outer loops like this:
        |
    ForeFirst    <------\   }
     Blocks             |   } ForeBlocks of L
    ForeLast            |   }
        |               |
       ...              |
        |               |
    ForeFirst    <----\ |   }
     Blocks           | |   } ForeBlocks of a inner loop of L
    ForeLast          | |   }
        |             | |
    JamLoopFirst  <\  | |   }
     Blocks        |  | |   } JamLoopBlocks of the innermost loop
    JamLoopLast   -/  | |   }
        |             | |
    AftFirst          | |   }
     Blocks           | |   } AftBlocks of a inner loop of L
    AftLast     ------/ |   }
        |               |
       ...              |
        |               |
    AftFirst            |   }
     Blocks             |   } AftBlocks of L
    AftLast     --------/   }
        |

    There are (theoretically) any number of blocks in ForeBlocks, SubLoopBlocks
    and AftBlocks, providing that there is one edge from Fores to SubLoops,
    one edge from SubLoops to Afts and a single outer loop exit (from Afts).
    In practice we currently limit Aft blocks to a single block, and limit
    things further in the profitablility checks of the unroll and jam pass.

    Because of the way we rearrange basic blocks, we also require that
    the Fore blocks of L on all unrolled iterations are safe to move before the
    blocks of the direct child of L of all iterations. So we require that the
    phi node looping operands of ForeHeader can be moved to at least the end of
    ForeEnd, so that we can arrange cloned Fore Blocks before the subloop and
    match up Phi's correctly.

    i.e. The old order of blocks used to be
           (F1)1 (F2)1 J1_1 J1_2 (A2)1 (A1)1 (F1)2 (F2)2 J2_1 J2_2 (A2)2 (A1)2.
         It needs to be safe to transform this to
           (F1)1 (F1)2 (F2)1 (F2)2 J1_1 J1_2 J2_1 J2_2 (A2)1 (A2)2 (A1)1 (A1)2.

    There are then a number of checks along the lines of no calls, no
    exceptions, inner loop IV is consistent, etc. Note that for loops requiring
    runtime unrolling, UnrollRuntimeLoopRemainder can also fail in
    UnrollAndJamLoop if the trip count cannot be easily calculated.
  */

  // Split blocks into Fore/SubLoop/Aft based on dominators
  Loop *JamLoop = getInnerMostLoop(L);
  BasicBlockSet SubLoopBlocks;
  DenseMap<Loop *, BasicBlockSet> ForeBlocksMap;
  DenseMap<Loop *, BasicBlockSet> AftBlocksMap;
  if (!partitionOuterLoopBlocks(*L, *JamLoop, SubLoopBlocks, ForeBlocksMap,
                                AftBlocksMap, DT)) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Incompatible loop layout\n");
    return false;
  }

  // Aft blocks may need to move instructions to fore blocks, which becomes more
  // difficult if there are multiple (potentially conditionally executed)
  // blocks. For now we just exclude loops with multiple aft blocks.
  if (AftBlocksMap[L].size() != 1) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Can't currently handle "
                         "multiple blocks after the loop\n");
    return false;
  }

  // Check inner loop backedge count is consistent on all iterations of the
  // outer loop
  if (any_of(L->getLoopsInPreorder(), [&SE](Loop *SubLoop) {
        return !hasIterationCountInvariantInParent(SubLoop, SE);
      })) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Inner loop iteration count is "
                         "not consistent on each iteration\n");
    return false;
  }

  // Check the loop safety info for exceptions.
  SimpleLoopSafetyInfo LSI;
  LSI.computeLoopSafetyInfo(L);
  if (LSI.anyBlockMayThrow()) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Something may throw\n");
    return false;
  }

  // We've ruled out the easy stuff and now need to check that there are no
  // interdependencies which may prevent us from moving the:
  //  ForeBlocks before Subloop and AftBlocks.
  //  Subloop before AftBlocks.
  //  ForeBlock phi operands before the subloop

  // Make sure we can move all instructions we need to before the subloop
  BasicBlock *Header = L->getHeader();
  BasicBlock *Latch = L->getLoopLatch();
  BasicBlockSet AftBlocks = AftBlocksMap[L];
  Loop *SubLoop = L->getSubLoops()[0];
  if (!processHeaderPhiOperands(
          Header, Latch, AftBlocks, [&AftBlocks, &SubLoop](Instruction *I) {
            if (SubLoop->contains(I->getParent()))
              return false;
            if (AftBlocks.count(I->getParent())) {
              // If we hit a phi node in afts we know we are done (probably
              // LCSSA)
              if (isa<PHINode>(I))
                return false;
              // Can't move instructions with side effects or memory
              // reads/writes
              if (I->mayHaveSideEffects() || I->mayReadOrWriteMemory())
                return false;
            }
            // Keep going
            return true;
          })) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; can't move required "
                         "instructions after subloop to before it\n");
    return false;
  }

  // Check for memory dependencies which prohibit the unrolling we are doing.
  // Because of the way we are unrolling Fore/Sub/Aft blocks, we need to check
  // there are no dependencies between Fore-Sub, Fore-Aft, Sub-Aft and Sub-Sub.
  if (!checkDependencies(*L, SubLoopBlocks, ForeBlocksMap, AftBlocksMap, DI,
                         LI)) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; failed dependency check\n");
    return false;
  }

  return true;
}
