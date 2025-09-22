//===-- LCSSA.cpp - Convert loops into loop-closed SSA form ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass transforms loops by placing phi nodes at the end of the loops for
// all values that are live across the loop boundary.  For example, it turns
// the left into the right code:
//
// for (...)                for (...)
//   if (c)                   if (c)
//     X1 = ...                 X1 = ...
//   else                     else
//     X2 = ...                 X2 = ...
//   X3 = phi(X1, X2)         X3 = phi(X1, X2)
// ... = X3 + 4             X4 = phi(X3)
//                          ... = X4 + 4
//
// This is still valid LLVM; the extra phi nodes are purely redundant, and will
// be trivially eliminated by InstCombine.  The major benefit of this
// transformation is that it makes many other loop optimizations, such as
// LoopUnswitching, simpler.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PredIteratorCache.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
using namespace llvm;

#define DEBUG_TYPE "lcssa"

STATISTIC(NumLCSSA, "Number of live out of a loop variables");

#ifdef EXPENSIVE_CHECKS
static bool VerifyLoopLCSSA = true;
#else
static bool VerifyLoopLCSSA = false;
#endif
static cl::opt<bool, true>
    VerifyLoopLCSSAFlag("verify-loop-lcssa", cl::location(VerifyLoopLCSSA),
                        cl::Hidden,
                        cl::desc("Verify loop lcssa form (time consuming)"));

/// Return true if the specified block is in the list.
static bool isExitBlock(BasicBlock *BB,
                        const SmallVectorImpl<BasicBlock *> &ExitBlocks) {
  return is_contained(ExitBlocks, BB);
}

/// For every instruction from the worklist, check to see if it has any uses
/// that are outside the current loop.  If so, insert LCSSA PHI nodes and
/// rewrite the uses.
bool llvm::formLCSSAForInstructions(SmallVectorImpl<Instruction *> &Worklist,
                                    const DominatorTree &DT, const LoopInfo &LI,
                                    ScalarEvolution *SE,
                                    SmallVectorImpl<PHINode *> *PHIsToRemove,
                                    SmallVectorImpl<PHINode *> *InsertedPHIs) {
  SmallVector<Use *, 16> UsesToRewrite;
  SmallSetVector<PHINode *, 16> LocalPHIsToRemove;
  PredIteratorCache PredCache;
  bool Changed = false;

  // Cache the Loop ExitBlocks across this loop.  We expect to get a lot of
  // instructions within the same loops, computing the exit blocks is
  // expensive, and we're not mutating the loop structure.
  SmallDenseMap<Loop*, SmallVector<BasicBlock *,1>> LoopExitBlocks;

  while (!Worklist.empty()) {
    UsesToRewrite.clear();

    Instruction *I = Worklist.pop_back_val();
    assert(!I->getType()->isTokenTy() && "Tokens shouldn't be in the worklist");
    BasicBlock *InstBB = I->getParent();
    Loop *L = LI.getLoopFor(InstBB);
    assert(L && "Instruction belongs to a BB that's not part of a loop");
    if (!LoopExitBlocks.count(L))
      L->getExitBlocks(LoopExitBlocks[L]);
    assert(LoopExitBlocks.count(L));
    const SmallVectorImpl<BasicBlock *> &ExitBlocks = LoopExitBlocks[L];

    if (ExitBlocks.empty())
      continue;

    for (Use &U : make_early_inc_range(I->uses())) {
      Instruction *User = cast<Instruction>(U.getUser());
      BasicBlock *UserBB = User->getParent();

      // Skip uses in unreachable blocks.
      if (!DT.isReachableFromEntry(UserBB)) {
        U.set(PoisonValue::get(I->getType()));
        continue;
      }

      // For practical purposes, we consider that the use in a PHI
      // occurs in the respective predecessor block. For more info,
      // see the `phi` doc in LangRef and the LCSSA doc.
      if (auto *PN = dyn_cast<PHINode>(User))
        UserBB = PN->getIncomingBlock(U);

      if (InstBB != UserBB && !L->contains(UserBB))
        UsesToRewrite.push_back(&U);
    }

    // If there are no uses outside the loop, exit with no change.
    if (UsesToRewrite.empty())
      continue;

    ++NumLCSSA; // We are applying the transformation

    // Invoke instructions are special in that their result value is not
    // available along their unwind edge. The code below tests to see whether
    // DomBB dominates the value, so adjust DomBB to the normal destination
    // block, which is effectively where the value is first usable.
    BasicBlock *DomBB = InstBB;
    if (auto *Inv = dyn_cast<InvokeInst>(I))
      DomBB = Inv->getNormalDest();

    const DomTreeNode *DomNode = DT.getNode(DomBB);

    SmallVector<PHINode *, 16> AddedPHIs;
    SmallVector<PHINode *, 8> PostProcessPHIs;

    SmallVector<PHINode *, 4> LocalInsertedPHIs;
    SSAUpdater SSAUpdate(&LocalInsertedPHIs);
    SSAUpdate.Initialize(I->getType(), I->getName());

    // Insert the LCSSA phi's into all of the exit blocks dominated by the
    // value, and add them to the Phi's map.
    bool HasSCEV = SE && SE->isSCEVable(I->getType()) &&
                   SE->getExistingSCEV(I) != nullptr;
    for (BasicBlock *ExitBB : ExitBlocks) {
      if (!DT.dominates(DomNode, DT.getNode(ExitBB)))
        continue;

      // If we already inserted something for this BB, don't reprocess it.
      if (SSAUpdate.HasValueForBlock(ExitBB))
        continue;
      PHINode *PN = PHINode::Create(I->getType(), PredCache.size(ExitBB),
                                    I->getName() + ".lcssa");
      PN->insertBefore(ExitBB->begin());
      if (InsertedPHIs)
        InsertedPHIs->push_back(PN);
      // Get the debug location from the original instruction.
      PN->setDebugLoc(I->getDebugLoc());

      // Add inputs from inside the loop for this PHI. This is valid
      // because `I` dominates `ExitBB` (checked above).  This implies
      // that every incoming block/edge is dominated by `I` as well,
      // i.e. we can add uses of `I` to those incoming edges/append to the incoming
      // blocks without violating the SSA dominance property.
      for (BasicBlock *Pred : PredCache.get(ExitBB)) {
        PN->addIncoming(I, Pred);

        // If the exit block has a predecessor not within the loop, arrange for
        // the incoming value use corresponding to that predecessor to be
        // rewritten in terms of a different LCSSA PHI.
        if (!L->contains(Pred))
          UsesToRewrite.push_back(
              &PN->getOperandUse(PN->getOperandNumForIncomingValue(
                  PN->getNumIncomingValues() - 1)));
      }

      AddedPHIs.push_back(PN);

      // Remember that this phi makes the value alive in this block.
      SSAUpdate.AddAvailableValue(ExitBB, PN);

      // LoopSimplify might fail to simplify some loops (e.g. when indirect
      // branches are involved). In such situations, it might happen that an
      // exit for Loop L1 is the header of a disjoint Loop L2. Thus, when we
      // create PHIs in such an exit block, we are also inserting PHIs into L2's
      // header. This could break LCSSA form for L2 because these inserted PHIs
      // can also have uses outside of L2. Remember all PHIs in such situation
      // as to revisit than later on. FIXME: Remove this if indirectbr support
      // into LoopSimplify gets improved.
      if (auto *OtherLoop = LI.getLoopFor(ExitBB))
        if (!L->contains(OtherLoop))
          PostProcessPHIs.push_back(PN);

      // If we have a cached SCEV for the original instruction, make sure the
      // new LCSSA phi node is also cached. This makes sures that BECounts
      // based on it will be invalidated when the LCSSA phi node is invalidated,
      // which some passes rely on.
      if (HasSCEV)
        SE->getSCEV(PN);
    }

    // Rewrite all uses outside the loop in terms of the new PHIs we just
    // inserted.
    for (Use *UseToRewrite : UsesToRewrite) {
      Instruction *User = cast<Instruction>(UseToRewrite->getUser());
      BasicBlock *UserBB = User->getParent();

      // For practical purposes, we consider that the use in a PHI
      // occurs in the respective predecessor block. For more info,
      // see the `phi` doc in LangRef and the LCSSA doc.
      if (auto *PN = dyn_cast<PHINode>(User))
        UserBB = PN->getIncomingBlock(*UseToRewrite);

      // If this use is in an exit block, rewrite to use the newly inserted PHI.
      // This is required for correctness because SSAUpdate doesn't handle uses
      // in the same block.  It assumes the PHI we inserted is at the end of the
      // block.
      if (isa<PHINode>(UserBB->begin()) && isExitBlock(UserBB, ExitBlocks)) {
        UseToRewrite->set(&UserBB->front());
        continue;
      }

      // If we added a single PHI, it must dominate all uses and we can directly
      // rename it.
      if (AddedPHIs.size() == 1) {
        UseToRewrite->set(AddedPHIs[0]);
        continue;
      }

      // Otherwise, do full PHI insertion.
      SSAUpdate.RewriteUse(*UseToRewrite);
    }

    SmallVector<DbgValueInst *, 4> DbgValues;
    SmallVector<DbgVariableRecord *, 4> DbgVariableRecords;
    llvm::findDbgValues(DbgValues, I, &DbgVariableRecords);

    // Update pre-existing debug value uses that reside outside the loop.
    for (auto *DVI : DbgValues) {
      BasicBlock *UserBB = DVI->getParent();
      if (InstBB == UserBB || L->contains(UserBB))
        continue;
      // We currently only handle debug values residing in blocks that were
      // traversed while rewriting the uses. If we inserted just a single PHI,
      // we will handle all relevant debug values.
      Value *V = AddedPHIs.size() == 1 ? AddedPHIs[0]
                                       : SSAUpdate.FindValueForBlock(UserBB);
      if (V)
        DVI->replaceVariableLocationOp(I, V);
    }

    // RemoveDIs: copy-paste of block above, using non-instruction debug-info
    // records.
    for (DbgVariableRecord *DVR : DbgVariableRecords) {
      BasicBlock *UserBB = DVR->getMarker()->getParent();
      if (InstBB == UserBB || L->contains(UserBB))
        continue;
      // We currently only handle debug values residing in blocks that were
      // traversed while rewriting the uses. If we inserted just a single PHI,
      // we will handle all relevant debug values.
      Value *V = AddedPHIs.size() == 1 ? AddedPHIs[0]
                                       : SSAUpdate.FindValueForBlock(UserBB);
      if (V)
        DVR->replaceVariableLocationOp(I, V);
    }

    // SSAUpdater might have inserted phi-nodes inside other loops. We'll need
    // to post-process them to keep LCSSA form.
    for (PHINode *InsertedPN : LocalInsertedPHIs) {
      if (auto *OtherLoop = LI.getLoopFor(InsertedPN->getParent()))
        if (!L->contains(OtherLoop))
          PostProcessPHIs.push_back(InsertedPN);
      if (InsertedPHIs)
        InsertedPHIs->push_back(InsertedPN);
    }

    // Post process PHI instructions that were inserted into another disjoint
    // loop and update their exits properly.
    for (auto *PostProcessPN : PostProcessPHIs)
      if (!PostProcessPN->use_empty())
        Worklist.push_back(PostProcessPN);

    // Keep track of PHI nodes that we want to remove because they did not have
    // any uses rewritten.
    for (PHINode *PN : AddedPHIs)
      if (PN->use_empty())
        LocalPHIsToRemove.insert(PN);

    Changed = true;
  }

  // Remove PHI nodes that did not have any uses rewritten or add them to
  // PHIsToRemove, so the caller can remove them after some additional cleanup.
  // We need to redo the use_empty() check here, because even if the PHI node
  // wasn't used when added to LocalPHIsToRemove, later added PHI nodes can be
  // using it.  This cleanup is not guaranteed to handle trees/cycles of PHI
  // nodes that only are used by each other. Such situations has only been
  // noticed when the input IR contains unreachable code, and leaving some extra
  // redundant PHI nodes in such situations is considered a minor problem.
  if (PHIsToRemove) {
    PHIsToRemove->append(LocalPHIsToRemove.begin(), LocalPHIsToRemove.end());
  } else {
    for (PHINode *PN : LocalPHIsToRemove)
      if (PN->use_empty())
        PN->eraseFromParent();
  }
  return Changed;
}

// Compute the set of BasicBlocks in the loop `L` dominating at least one exit.
static void computeBlocksDominatingExits(
    Loop &L, const DominatorTree &DT, SmallVector<BasicBlock *, 8> &ExitBlocks,
    SmallSetVector<BasicBlock *, 8> &BlocksDominatingExits) {
  // We start from the exit blocks, as every block trivially dominates itself
  // (not strictly).
  SmallVector<BasicBlock *, 8> BBWorklist(ExitBlocks);

  while (!BBWorklist.empty()) {
    BasicBlock *BB = BBWorklist.pop_back_val();

    // Check if this is a loop header. If this is the case, we're done.
    if (L.getHeader() == BB)
      continue;

    // Otherwise, add its immediate predecessor in the dominator tree to the
    // worklist, unless we visited it already.
    BasicBlock *IDomBB = DT.getNode(BB)->getIDom()->getBlock();

    // Exit blocks can have an immediate dominator not belonging to the
    // loop. For an exit block to be immediately dominated by another block
    // outside the loop, it implies not all paths from that dominator, to the
    // exit block, go through the loop.
    // Example:
    //
    // |---- A
    // |     |
    // |     B<--
    // |     |  |
    // |---> C --
    //       |
    //       D
    //
    // C is the exit block of the loop and it's immediately dominated by A,
    // which doesn't belong to the loop.
    if (!L.contains(IDomBB))
      continue;

    if (BlocksDominatingExits.insert(IDomBB))
      BBWorklist.push_back(IDomBB);
  }
}

bool llvm::formLCSSA(Loop &L, const DominatorTree &DT, const LoopInfo *LI,
                     ScalarEvolution *SE) {
  bool Changed = false;

#ifdef EXPENSIVE_CHECKS
  // Verify all sub-loops are in LCSSA form already.
  for (Loop *SubLoop: L) {
    (void)SubLoop; // Silence unused variable warning.
    assert(SubLoop->isRecursivelyLCSSAForm(DT, *LI) && "Subloop not in LCSSA!");
  }
#endif

  SmallVector<BasicBlock *, 8> ExitBlocks;
  L.getExitBlocks(ExitBlocks);
  if (ExitBlocks.empty())
    return false;

  SmallSetVector<BasicBlock *, 8> BlocksDominatingExits;

  // We want to avoid use-scanning leveraging dominance informations.
  // If a block doesn't dominate any of the loop exits, the none of the values
  // defined in the loop can be used outside.
  // We compute the set of blocks fullfilling the conditions in advance
  // walking the dominator tree upwards until we hit a loop header.
  computeBlocksDominatingExits(L, DT, ExitBlocks, BlocksDominatingExits);

  SmallVector<Instruction *, 8> Worklist;

  // Look at all the instructions in the loop, checking to see if they have uses
  // outside the loop.  If so, put them into the worklist to rewrite those uses.
  for (BasicBlock *BB : BlocksDominatingExits) {
    // Skip blocks that are part of any sub-loops, they must be in LCSSA
    // already.
    if (LI->getLoopFor(BB) != &L)
      continue;
    for (Instruction &I : *BB) {
      // Reject two common cases fast: instructions with no uses (like stores)
      // and instructions with one use that is in the same block as this.
      if (I.use_empty() ||
          (I.hasOneUse() && I.user_back()->getParent() == BB &&
           !isa<PHINode>(I.user_back())))
        continue;

      // Tokens cannot be used in PHI nodes, so we skip over them.
      // We can run into tokens which are live out of a loop with catchswitch
      // instructions in Windows EH if the catchswitch has one catchpad which
      // is inside the loop and another which is not.
      if (I.getType()->isTokenTy())
        continue;

      Worklist.push_back(&I);
    }
  }

  Changed = formLCSSAForInstructions(Worklist, DT, *LI, SE);

  assert(L.isLCSSAForm(DT));

  return Changed;
}

/// Process a loop nest depth first.
bool llvm::formLCSSARecursively(Loop &L, const DominatorTree &DT,
                                const LoopInfo *LI, ScalarEvolution *SE) {
  bool Changed = false;

  // Recurse depth-first through inner loops.
  for (Loop *SubLoop : L.getSubLoops())
    Changed |= formLCSSARecursively(*SubLoop, DT, LI, SE);

  Changed |= formLCSSA(L, DT, LI, SE);
  return Changed;
}

/// Process all loops in the function, inner-most out.
static bool formLCSSAOnAllLoops(const LoopInfo *LI, const DominatorTree &DT,
                                ScalarEvolution *SE) {
  bool Changed = false;
  for (const auto &L : *LI)
    Changed |= formLCSSARecursively(*L, DT, LI, SE);
  return Changed;
}

namespace {
struct LCSSAWrapperPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  LCSSAWrapperPass() : FunctionPass(ID) {
    initializeLCSSAWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  // Cached analysis information for the current function.
  DominatorTree *DT;
  LoopInfo *LI;
  ScalarEvolution *SE;

  bool runOnFunction(Function &F) override;
  void verifyAnalysis() const override {
    // This check is very expensive. On the loop intensive compiles it may cause
    // up to 10x slowdown. Currently it's disabled by default. LPPassManager
    // always does limited form of the LCSSA verification. Similar reasoning
    // was used for the LoopInfo verifier.
    if (VerifyLoopLCSSA) {
      assert(all_of(*LI,
                    [&](Loop *L) {
                      return L->isRecursivelyLCSSAForm(*DT, *LI);
                    }) &&
             "LCSSA form is broken!");
    }
  };

  /// This transformation requires natural loop information & requires that
  /// loop preheaders be inserted into the CFG.  It maintains both of these,
  /// as well as the CFG.  It also requires dominator information.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();

    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreservedID(LoopSimplifyID);
    AU.addPreserved<AAResultsWrapperPass>();
    AU.addPreserved<BasicAAWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<ScalarEvolutionWrapperPass>();
    AU.addPreserved<SCEVAAWrapperPass>();
    AU.addPreserved<BranchProbabilityInfoWrapperPass>();
    AU.addPreserved<MemorySSAWrapperPass>();

    // This is needed to perform LCSSA verification inside LPPassManager
    AU.addRequired<LCSSAVerificationPass>();
    AU.addPreserved<LCSSAVerificationPass>();
  }
};
}

char LCSSAWrapperPass::ID = 0;
INITIALIZE_PASS_BEGIN(LCSSAWrapperPass, "lcssa", "Loop-Closed SSA Form Pass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LCSSAVerificationPass)
INITIALIZE_PASS_END(LCSSAWrapperPass, "lcssa", "Loop-Closed SSA Form Pass",
                    false, false)

Pass *llvm::createLCSSAPass() { return new LCSSAWrapperPass(); }
char &llvm::LCSSAID = LCSSAWrapperPass::ID;

/// Transform \p F into loop-closed SSA form.
bool LCSSAWrapperPass::runOnFunction(Function &F) {
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto *SEWP = getAnalysisIfAvailable<ScalarEvolutionWrapperPass>();
  SE = SEWP ? &SEWP->getSE() : nullptr;

  return formLCSSAOnAllLoops(LI, *DT, SE);
}

PreservedAnalyses LCSSAPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto *SE = AM.getCachedResult<ScalarEvolutionAnalysis>(F);
  if (!formLCSSAOnAllLoops(&LI, DT, SE))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<ScalarEvolutionAnalysis>();
  // BPI maps terminators to probabilities, since we don't modify the CFG, no
  // updates are needed to preserve it.
  PA.preserve<BranchProbabilityAnalysis>();
  PA.preserve<MemorySSAAnalysis>();
  return PA;
}
