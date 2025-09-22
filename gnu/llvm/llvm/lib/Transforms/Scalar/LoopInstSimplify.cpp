//===- LoopInstSimplify.cpp - Loop Instruction Simplification Pass --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs lightweight instruction simplification on loop bodies.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopInstSimplify.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "loop-instsimplify"

STATISTIC(NumSimplified, "Number of redundant instructions simplified");

static bool simplifyLoopInst(Loop &L, DominatorTree &DT, LoopInfo &LI,
                             AssumptionCache &AC, const TargetLibraryInfo &TLI,
                             MemorySSAUpdater *MSSAU) {
  const DataLayout &DL = L.getHeader()->getDataLayout();
  SimplifyQuery SQ(DL, &TLI, &DT, &AC);

  // On the first pass over the loop body we try to simplify every instruction.
  // On subsequent passes, we can restrict this to only simplifying instructions
  // where the inputs have been updated. We end up needing two sets: one
  // containing the instructions we are simplifying in *this* pass, and one for
  // the instructions we will want to simplify in the *next* pass. We use
  // pointers so we can swap between two stably allocated sets.
  SmallPtrSet<const Instruction *, 8> S1, S2, *ToSimplify = &S1, *Next = &S2;

  // Track the PHI nodes that have already been visited during each iteration so
  // that we can identify when it is necessary to iterate.
  SmallPtrSet<PHINode *, 4> VisitedPHIs;

  // While simplifying we may discover dead code or cause code to become dead.
  // Keep track of all such instructions and we will delete them at the end.
  SmallVector<WeakTrackingVH, 8> DeadInsts;

  // First we want to create an RPO traversal of the loop body. By processing in
  // RPO we can ensure that definitions are processed prior to uses (for non PHI
  // uses) in all cases. This ensures we maximize the simplifications in each
  // iteration over the loop and minimizes the possible causes for continuing to
  // iterate.
  LoopBlocksRPO RPOT(&L);
  RPOT.perform(&LI);
  MemorySSA *MSSA = MSSAU ? MSSAU->getMemorySSA() : nullptr;

  bool Changed = false;
  for (;;) {
    if (MSSAU && VerifyMemorySSA)
      MSSA->verifyMemorySSA();
    for (BasicBlock *BB : RPOT) {
      for (Instruction &I : *BB) {
        if (auto *PI = dyn_cast<PHINode>(&I))
          VisitedPHIs.insert(PI);

        if (I.use_empty()) {
          if (isInstructionTriviallyDead(&I, &TLI))
            DeadInsts.push_back(&I);
          continue;
        }

        // We special case the first iteration which we can detect due to the
        // empty `ToSimplify` set.
        bool IsFirstIteration = ToSimplify->empty();

        if (!IsFirstIteration && !ToSimplify->count(&I))
          continue;

        Value *V = simplifyInstruction(&I, SQ.getWithInstruction(&I));
        if (!V || !LI.replacementPreservesLCSSAForm(&I, V))
          continue;

        for (Use &U : llvm::make_early_inc_range(I.uses())) {
          auto *UserI = cast<Instruction>(U.getUser());
          U.set(V);

          // Do not bother dealing with unreachable code.
          if (!DT.isReachableFromEntry(UserI->getParent()))
            continue;

          // If the instruction is used by a PHI node we have already processed
          // we'll need to iterate on the loop body to converge, so add it to
          // the next set.
          if (auto *UserPI = dyn_cast<PHINode>(UserI))
            if (VisitedPHIs.count(UserPI)) {
              Next->insert(UserPI);
              continue;
            }

          // If we are only simplifying targeted instructions and the user is an
          // instruction in the loop body, add it to our set of targeted
          // instructions. Because we process defs before uses (outside of PHIs)
          // we won't have visited it yet.
          //
          // We also skip any uses outside of the loop being simplified. Those
          // should always be PHI nodes due to LCSSA form, and we don't want to
          // try to simplify those away.
          assert((L.contains(UserI) || isa<PHINode>(UserI)) &&
                 "Uses outside the loop should be PHI nodes due to LCSSA!");
          if (!IsFirstIteration && L.contains(UserI))
            ToSimplify->insert(UserI);
        }

        if (MSSAU)
          if (Instruction *SimpleI = dyn_cast_or_null<Instruction>(V))
            if (MemoryAccess *MA = MSSA->getMemoryAccess(&I))
              if (MemoryAccess *ReplacementMA = MSSA->getMemoryAccess(SimpleI))
                MA->replaceAllUsesWith(ReplacementMA);

        assert(I.use_empty() && "Should always have replaced all uses!");
        if (isInstructionTriviallyDead(&I, &TLI))
          DeadInsts.push_back(&I);
        ++NumSimplified;
        Changed = true;
      }
    }

    // Delete any dead instructions found thus far now that we've finished an
    // iteration over all instructions in all the loop blocks.
    if (!DeadInsts.empty()) {
      Changed = true;
      RecursivelyDeleteTriviallyDeadInstructions(DeadInsts, &TLI, MSSAU);
    }

    if (MSSAU && VerifyMemorySSA)
      MSSA->verifyMemorySSA();

    // If we never found a PHI that needs to be simplified in the next
    // iteration, we're done.
    if (Next->empty())
      break;

    // Otherwise, put the next set in place for the next iteration and reset it
    // and the visited PHIs for that iteration.
    std::swap(Next, ToSimplify);
    Next->clear();
    VisitedPHIs.clear();
    DeadInsts.clear();
  }

  return Changed;
}

PreservedAnalyses LoopInstSimplifyPass::run(Loop &L, LoopAnalysisManager &AM,
                                            LoopStandardAnalysisResults &AR,
                                            LPMUpdater &) {
  std::optional<MemorySSAUpdater> MSSAU;
  if (AR.MSSA) {
    MSSAU = MemorySSAUpdater(AR.MSSA);
    if (VerifyMemorySSA)
      AR.MSSA->verifyMemorySSA();
  }
  if (!simplifyLoopInst(L, AR.DT, AR.LI, AR.AC, AR.TLI,
                        MSSAU ? &*MSSAU : nullptr))
    return PreservedAnalyses::all();

  auto PA = getLoopPassPreservedAnalyses();
  PA.preserveSet<CFGAnalyses>();
  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}
