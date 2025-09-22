//===-- LoopSink.cpp - Loop Sink Pass -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does the inverse transformation of what LICM does.
// It traverses all of the instructions in the loop's preheader and sinks
// them to the loop body where frequency is lower than the loop's preheader.
// This pass is a reverse-transformation of LICM. It differs from the Sink
// pass in the following ways:
//
// * It only handles sinking of instructions from the loop's preheader to the
//   loop's body
// * It uses alias set tracker to get more accurate alias info
// * It uses block frequency info to find the optimal sinking locations
//
// Overall algorithm:
//
// For I in Preheader:
//   InsertBBs = BBs that uses I
//   For BB in sorted(LoopBBs):
//     DomBBs = BBs in InsertBBs that are dominated by BB
//     if freq(DomBBs) > freq(BB)
//       InsertBBs = UseBBs - DomBBs + BB
//   For BB in InsertBBs:
//     Insert I at BB's beginning
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
using namespace llvm;

#define DEBUG_TYPE "loopsink"

STATISTIC(NumLoopSunk, "Number of instructions sunk into loop");
STATISTIC(NumLoopSunkCloned, "Number of cloned instructions sunk into loop");

static cl::opt<unsigned> SinkFrequencyPercentThreshold(
    "sink-freq-percent-threshold", cl::Hidden, cl::init(90),
    cl::desc("Do not sink instructions that require cloning unless they "
             "execute less than this percent of the time."));

static cl::opt<unsigned> MaxNumberOfUseBBsForSinking(
    "max-uses-for-sinking", cl::Hidden, cl::init(30),
    cl::desc("Do not sink instructions that have too many uses."));

/// Return adjusted total frequency of \p BBs.
///
/// * If there is only one BB, sinking instruction will not introduce code
///   size increase. Thus there is no need to adjust the frequency.
/// * If there are more than one BB, sinking would lead to code size increase.
///   In this case, we add some "tax" to the total frequency to make it harder
///   to sink. E.g.
///     Freq(Preheader) = 100
///     Freq(BBs) = sum(50, 49) = 99
///   Even if Freq(BBs) < Freq(Preheader), we will not sink from Preheade to
///   BBs as the difference is too small to justify the code size increase.
///   To model this, The adjusted Freq(BBs) will be:
///     AdjustedFreq(BBs) = 99 / SinkFrequencyPercentThreshold%
static BlockFrequency adjustedSumFreq(SmallPtrSetImpl<BasicBlock *> &BBs,
                                      BlockFrequencyInfo &BFI) {
  BlockFrequency T(0);
  for (BasicBlock *B : BBs)
    T += BFI.getBlockFreq(B);
  if (BBs.size() > 1)
    T /= BranchProbability(SinkFrequencyPercentThreshold, 100);
  return T;
}

/// Return a set of basic blocks to insert sinked instructions.
///
/// The returned set of basic blocks (BBsToSinkInto) should satisfy:
///
/// * Inside the loop \p L
/// * For each UseBB in \p UseBBs, there is at least one BB in BBsToSinkInto
///   that domintates the UseBB
/// * Has minimum total frequency that is no greater than preheader frequency
///
/// The purpose of the function is to find the optimal sinking points to
/// minimize execution cost, which is defined as "sum of frequency of
/// BBsToSinkInto".
/// As a result, the returned BBsToSinkInto needs to have minimum total
/// frequency.
/// Additionally, if the total frequency of BBsToSinkInto exceeds preheader
/// frequency, the optimal solution is not sinking (return empty set).
///
/// \p ColdLoopBBs is used to help find the optimal sinking locations.
/// It stores a list of BBs that is:
///
/// * Inside the loop \p L
/// * Has a frequency no larger than the loop's preheader
/// * Sorted by BB frequency
///
/// The complexity of the function is O(UseBBs.size() * ColdLoopBBs.size()).
/// To avoid expensive computation, we cap the maximum UseBBs.size() in its
/// caller.
static SmallPtrSet<BasicBlock *, 2>
findBBsToSinkInto(const Loop &L, const SmallPtrSetImpl<BasicBlock *> &UseBBs,
                  const SmallVectorImpl<BasicBlock *> &ColdLoopBBs,
                  DominatorTree &DT, BlockFrequencyInfo &BFI) {
  SmallPtrSet<BasicBlock *, 2> BBsToSinkInto;
  if (UseBBs.size() == 0)
    return BBsToSinkInto;

  BBsToSinkInto.insert(UseBBs.begin(), UseBBs.end());
  SmallPtrSet<BasicBlock *, 2> BBsDominatedByColdestBB;

  // For every iteration:
  //   * Pick the ColdestBB from ColdLoopBBs
  //   * Find the set BBsDominatedByColdestBB that satisfy:
  //     - BBsDominatedByColdestBB is a subset of BBsToSinkInto
  //     - Every BB in BBsDominatedByColdestBB is dominated by ColdestBB
  //   * If Freq(ColdestBB) < Freq(BBsDominatedByColdestBB), remove
  //     BBsDominatedByColdestBB from BBsToSinkInto, add ColdestBB to
  //     BBsToSinkInto
  for (BasicBlock *ColdestBB : ColdLoopBBs) {
    BBsDominatedByColdestBB.clear();
    for (BasicBlock *SinkedBB : BBsToSinkInto)
      if (DT.dominates(ColdestBB, SinkedBB))
        BBsDominatedByColdestBB.insert(SinkedBB);
    if (BBsDominatedByColdestBB.size() == 0)
      continue;
    if (adjustedSumFreq(BBsDominatedByColdestBB, BFI) >
        BFI.getBlockFreq(ColdestBB)) {
      for (BasicBlock *DominatedBB : BBsDominatedByColdestBB) {
        BBsToSinkInto.erase(DominatedBB);
      }
      BBsToSinkInto.insert(ColdestBB);
    }
  }

  // Can't sink into blocks that have no valid insertion point.
  for (BasicBlock *BB : BBsToSinkInto) {
    if (BB->getFirstInsertionPt() == BB->end()) {
      BBsToSinkInto.clear();
      break;
    }
  }

  // If the total frequency of BBsToSinkInto is larger than preheader frequency,
  // do not sink.
  if (adjustedSumFreq(BBsToSinkInto, BFI) >
      BFI.getBlockFreq(L.getLoopPreheader()))
    BBsToSinkInto.clear();
  return BBsToSinkInto;
}

// Sinks \p I from the loop \p L's preheader to its uses. Returns true if
// sinking is successful.
// \p LoopBlockNumber is used to sort the insertion blocks to ensure
// determinism.
static bool sinkInstruction(
    Loop &L, Instruction &I, const SmallVectorImpl<BasicBlock *> &ColdLoopBBs,
    const SmallDenseMap<BasicBlock *, int, 16> &LoopBlockNumber, LoopInfo &LI,
    DominatorTree &DT, BlockFrequencyInfo &BFI, MemorySSAUpdater *MSSAU) {
  // Compute the set of blocks in loop L which contain a use of I.
  SmallPtrSet<BasicBlock *, 2> BBs;
  for (auto &U : I.uses()) {
    Instruction *UI = cast<Instruction>(U.getUser());

    // We cannot sink I if it has uses outside of the loop.
    if (!L.contains(LI.getLoopFor(UI->getParent())))
      return false;

    if (!isa<PHINode>(UI)) {
      BBs.insert(UI->getParent());
      continue;
    }

    // We cannot sink I to PHI-uses, try to look through PHI to find the incoming
    // block of the value being used.
    PHINode *PN = dyn_cast<PHINode>(UI);
    BasicBlock *PhiBB = PN->getIncomingBlock(U);

    // If value's incoming block is from loop preheader directly, there's no
    // place to sink to, bailout.
    if (L.getLoopPreheader() == PhiBB)
      return false;

    BBs.insert(PhiBB);
  }

  // findBBsToSinkInto is O(BBs.size() * ColdLoopBBs.size()). We cap the max
  // BBs.size() to avoid expensive computation.
  // FIXME: Handle code size growth for min_size and opt_size.
  if (BBs.size() > MaxNumberOfUseBBsForSinking)
    return false;

  // Find the set of BBs that we should insert a copy of I.
  SmallPtrSet<BasicBlock *, 2> BBsToSinkInto =
      findBBsToSinkInto(L, BBs, ColdLoopBBs, DT, BFI);
  if (BBsToSinkInto.empty())
    return false;

  // Return if any of the candidate blocks to sink into is non-cold.
  if (BBsToSinkInto.size() > 1 &&
      !llvm::set_is_subset(BBsToSinkInto, LoopBlockNumber))
    return false;

  // Copy the final BBs into a vector and sort them using the total ordering
  // of the loop block numbers as iterating the set doesn't give a useful
  // order. No need to stable sort as the block numbers are a total ordering.
  SmallVector<BasicBlock *, 2> SortedBBsToSinkInto;
  llvm::append_range(SortedBBsToSinkInto, BBsToSinkInto);
  if (SortedBBsToSinkInto.size() > 1) {
    llvm::sort(SortedBBsToSinkInto, [&](BasicBlock *A, BasicBlock *B) {
      return LoopBlockNumber.find(A)->second < LoopBlockNumber.find(B)->second;
    });
  }

  BasicBlock *MoveBB = *SortedBBsToSinkInto.begin();
  // FIXME: Optimize the efficiency for cloned value replacement. The current
  //        implementation is O(SortedBBsToSinkInto.size() * I.num_uses()).
  for (BasicBlock *N : ArrayRef(SortedBBsToSinkInto).drop_front(1)) {
    assert(LoopBlockNumber.find(N)->second >
               LoopBlockNumber.find(MoveBB)->second &&
           "BBs not sorted!");
    // Clone I and replace its uses.
    Instruction *IC = I.clone();
    IC->setName(I.getName());
    IC->insertBefore(&*N->getFirstInsertionPt());

    if (MSSAU && MSSAU->getMemorySSA()->getMemoryAccess(&I)) {
      // Create a new MemoryAccess and let MemorySSA set its defining access.
      MemoryAccess *NewMemAcc =
          MSSAU->createMemoryAccessInBB(IC, nullptr, N, MemorySSA::Beginning);
      if (NewMemAcc) {
        if (auto *MemDef = dyn_cast<MemoryDef>(NewMemAcc))
          MSSAU->insertDef(MemDef, /*RenameUses=*/true);
        else {
          auto *MemUse = cast<MemoryUse>(NewMemAcc);
          MSSAU->insertUse(MemUse, /*RenameUses=*/true);
        }
      }
    }

    // Replaces uses of I with IC in N, except PHI-use which is being taken
    // care of by defs in PHI's incoming blocks.
    I.replaceUsesWithIf(IC, [N](Use &U) {
      Instruction *UIToReplace = cast<Instruction>(U.getUser());
      return UIToReplace->getParent() == N && !isa<PHINode>(UIToReplace);
    });
    // Replaces uses of I with IC in blocks dominated by N
    replaceDominatedUsesWith(&I, IC, DT, N);
    LLVM_DEBUG(dbgs() << "Sinking a clone of " << I << " To: " << N->getName()
                      << '\n');
    NumLoopSunkCloned++;
  }
  LLVM_DEBUG(dbgs() << "Sinking " << I << " To: " << MoveBB->getName() << '\n');
  NumLoopSunk++;
  I.moveBefore(&*MoveBB->getFirstInsertionPt());

  if (MSSAU)
    if (MemoryUseOrDef *OldMemAcc = cast_or_null<MemoryUseOrDef>(
            MSSAU->getMemorySSA()->getMemoryAccess(&I)))
      MSSAU->moveToPlace(OldMemAcc, MoveBB, MemorySSA::Beginning);

  return true;
}

/// Sinks instructions from loop's preheader to the loop body if the
/// sum frequency of inserted copy is smaller than preheader's frequency.
static bool sinkLoopInvariantInstructions(Loop &L, AAResults &AA, LoopInfo &LI,
                                          DominatorTree &DT,
                                          BlockFrequencyInfo &BFI,
                                          MemorySSA &MSSA,
                                          ScalarEvolution *SE) {
  BasicBlock *Preheader = L.getLoopPreheader();
  assert(Preheader && "Expected loop to have preheader");

  assert(Preheader->getParent()->hasProfileData() &&
         "Unexpected call when profile data unavailable.");

  const BlockFrequency PreheaderFreq = BFI.getBlockFreq(Preheader);
  // If there are no basic blocks with lower frequency than the preheader then
  // we can avoid the detailed analysis as we will never find profitable sinking
  // opportunities.
  if (all_of(L.blocks(), [&](const BasicBlock *BB) {
        return BFI.getBlockFreq(BB) > PreheaderFreq;
      }))
    return false;

  MemorySSAUpdater MSSAU(&MSSA);
  SinkAndHoistLICMFlags LICMFlags(/*IsSink=*/true, L, MSSA);

  bool Changed = false;

  // Sort loop's basic blocks by frequency
  SmallVector<BasicBlock *, 10> ColdLoopBBs;
  SmallDenseMap<BasicBlock *, int, 16> LoopBlockNumber;
  int i = 0;
  for (BasicBlock *B : L.blocks())
    if (BFI.getBlockFreq(B) < BFI.getBlockFreq(L.getLoopPreheader())) {
      ColdLoopBBs.push_back(B);
      LoopBlockNumber[B] = ++i;
    }
  llvm::stable_sort(ColdLoopBBs, [&](BasicBlock *A, BasicBlock *B) {
    return BFI.getBlockFreq(A) < BFI.getBlockFreq(B);
  });

  // Traverse preheader's instructions in reverse order because if A depends
  // on B (A appears after B), A needs to be sunk first before B can be
  // sinked.
  for (Instruction &I : llvm::make_early_inc_range(llvm::reverse(*Preheader))) {
    if (isa<PHINode>(&I))
      continue;
    // No need to check for instruction's operands are loop invariant.
    assert(L.hasLoopInvariantOperands(&I) &&
           "Insts in a loop's preheader should have loop invariant operands!");
    if (!canSinkOrHoistInst(I, &AA, &DT, &L, MSSAU, false, LICMFlags))
      continue;
    if (sinkInstruction(L, I, ColdLoopBBs, LoopBlockNumber, LI, DT, BFI,
                        &MSSAU)) {
      Changed = true;
      if (SE)
        SE->forgetBlockAndLoopDispositions(&I);
    }
  }

  return Changed;
}

PreservedAnalyses LoopSinkPass::run(Function &F, FunctionAnalysisManager &FAM) {
  // Enable LoopSink only when runtime profile is available.
  // With static profile, the sinking decision may be sub-optimal.
  if (!F.hasProfileData())
    return PreservedAnalyses::all();

  LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
  // Nothing to do if there are no loops.
  if (LI.empty())
    return PreservedAnalyses::all();

  AAResults &AA = FAM.getResult<AAManager>(F);
  DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  BlockFrequencyInfo &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
  MemorySSA &MSSA = FAM.getResult<MemorySSAAnalysis>(F).getMSSA();

  // We want to do a postorder walk over the loops. Since loops are a tree this
  // is equivalent to a reversed preorder walk and preorder is easy to compute
  // without recursion. Since we reverse the preorder, we will visit siblings
  // in reverse program order. This isn't expected to matter at all but is more
  // consistent with sinking algorithms which generally work bottom-up.
  SmallVector<Loop *, 4> PreorderLoops = LI.getLoopsInPreorder();

  bool Changed = false;
  do {
    Loop &L = *PreorderLoops.pop_back_val();

    BasicBlock *Preheader = L.getLoopPreheader();
    if (!Preheader)
      continue;

    // Note that we don't pass SCEV here because it is only used to invalidate
    // loops in SCEV and we don't preserve (or request) SCEV at all making that
    // unnecessary.
    Changed |= sinkLoopInvariantInstructions(L, AA, LI, DT, BFI, MSSA,
                                             /*ScalarEvolution*/ nullptr);
  } while (!PreorderLoops.empty());

  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<MemorySSAAnalysis>();

  if (VerifyMemorySSA)
    MSSA.verifyMemorySSA();

  return PA;
}
