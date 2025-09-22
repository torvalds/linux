//===- JumpThreading.cpp - Thread control through conditional blocks ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Jump Threading pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

using namespace llvm;
using namespace jumpthreading;

#define DEBUG_TYPE "jump-threading"

STATISTIC(NumThreads, "Number of jumps threaded");
STATISTIC(NumFolds,   "Number of terminators folded");
STATISTIC(NumDupes,   "Number of branch blocks duplicated to eliminate phi");

static cl::opt<unsigned>
BBDuplicateThreshold("jump-threading-threshold",
          cl::desc("Max block size to duplicate for jump threading"),
          cl::init(6), cl::Hidden);

static cl::opt<unsigned>
ImplicationSearchThreshold(
  "jump-threading-implication-search-threshold",
  cl::desc("The number of predecessors to search for a stronger "
           "condition to use to thread over a weaker condition"),
  cl::init(3), cl::Hidden);

static cl::opt<unsigned> PhiDuplicateThreshold(
    "jump-threading-phi-threshold",
    cl::desc("Max PHIs in BB to duplicate for jump threading"), cl::init(76),
    cl::Hidden);

static cl::opt<bool> ThreadAcrossLoopHeaders(
    "jump-threading-across-loop-headers",
    cl::desc("Allow JumpThreading to thread across loop headers, for testing"),
    cl::init(false), cl::Hidden);

JumpThreadingPass::JumpThreadingPass(int T) {
  DefaultBBDupThreshold = (T == -1) ? BBDuplicateThreshold : unsigned(T);
}

// Update branch probability information according to conditional
// branch probability. This is usually made possible for cloned branches
// in inline instances by the context specific profile in the caller.
// For instance,
//
//  [Block PredBB]
//  [Branch PredBr]
//  if (t) {
//     Block A;
//  } else {
//     Block B;
//  }
//
//  [Block BB]
//  cond = PN([true, %A], [..., %B]); // PHI node
//  [Branch CondBr]
//  if (cond) {
//    ...  // P(cond == true) = 1%
//  }
//
//  Here we know that when block A is taken, cond must be true, which means
//      P(cond == true | A) = 1
//
//  Given that P(cond == true) = P(cond == true | A) * P(A) +
//                               P(cond == true | B) * P(B)
//  we get:
//     P(cond == true ) = P(A) + P(cond == true | B) * P(B)
//
//  which gives us:
//     P(A) is less than P(cond == true), i.e.
//     P(t == true) <= P(cond == true)
//
//  In other words, if we know P(cond == true) is unlikely, we know
//  that P(t == true) is also unlikely.
//
static void updatePredecessorProfileMetadata(PHINode *PN, BasicBlock *BB) {
  BranchInst *CondBr = dyn_cast<BranchInst>(BB->getTerminator());
  if (!CondBr)
    return;

  uint64_t TrueWeight, FalseWeight;
  if (!extractBranchWeights(*CondBr, TrueWeight, FalseWeight))
    return;

  if (TrueWeight + FalseWeight == 0)
    // Zero branch_weights do not give a hint for getting branch probabilities.
    // Technically it would result in division by zero denominator, which is
    // TrueWeight + FalseWeight.
    return;

  // Returns the outgoing edge of the dominating predecessor block
  // that leads to the PhiNode's incoming block:
  auto GetPredOutEdge =
      [](BasicBlock *IncomingBB,
         BasicBlock *PhiBB) -> std::pair<BasicBlock *, BasicBlock *> {
    auto *PredBB = IncomingBB;
    auto *SuccBB = PhiBB;
    SmallPtrSet<BasicBlock *, 16> Visited;
    while (true) {
      BranchInst *PredBr = dyn_cast<BranchInst>(PredBB->getTerminator());
      if (PredBr && PredBr->isConditional())
        return {PredBB, SuccBB};
      Visited.insert(PredBB);
      auto *SinglePredBB = PredBB->getSinglePredecessor();
      if (!SinglePredBB)
        return {nullptr, nullptr};

      // Stop searching when SinglePredBB has been visited. It means we see
      // an unreachable loop.
      if (Visited.count(SinglePredBB))
        return {nullptr, nullptr};

      SuccBB = PredBB;
      PredBB = SinglePredBB;
    }
  };

  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *PhiOpnd = PN->getIncomingValue(i);
    ConstantInt *CI = dyn_cast<ConstantInt>(PhiOpnd);

    if (!CI || !CI->getType()->isIntegerTy(1))
      continue;

    BranchProbability BP =
        (CI->isOne() ? BranchProbability::getBranchProbability(
                           TrueWeight, TrueWeight + FalseWeight)
                     : BranchProbability::getBranchProbability(
                           FalseWeight, TrueWeight + FalseWeight));

    auto PredOutEdge = GetPredOutEdge(PN->getIncomingBlock(i), BB);
    if (!PredOutEdge.first)
      return;

    BasicBlock *PredBB = PredOutEdge.first;
    BranchInst *PredBr = dyn_cast<BranchInst>(PredBB->getTerminator());
    if (!PredBr)
      return;

    uint64_t PredTrueWeight, PredFalseWeight;
    // FIXME: We currently only set the profile data when it is missing.
    // With PGO, this can be used to refine even existing profile data with
    // context information. This needs to be done after more performance
    // testing.
    if (extractBranchWeights(*PredBr, PredTrueWeight, PredFalseWeight))
      continue;

    // We can not infer anything useful when BP >= 50%, because BP is the
    // upper bound probability value.
    if (BP >= BranchProbability(50, 100))
      continue;

    uint32_t Weights[2];
    if (PredBr->getSuccessor(0) == PredOutEdge.second) {
      Weights[0] = BP.getNumerator();
      Weights[1] = BP.getCompl().getNumerator();
    } else {
      Weights[0] = BP.getCompl().getNumerator();
      Weights[1] = BP.getNumerator();
    }
    setBranchWeights(*PredBr, Weights, hasBranchWeightOrigin(*PredBr));
  }
}

PreservedAnalyses JumpThreadingPass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  // Jump Threading has no sense for the targets with divergent CF
  if (TTI.hasBranchDivergence(&F))
    return PreservedAnalyses::all();
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &LVI = AM.getResult<LazyValueAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);

  bool Changed =
      runImpl(F, &AM, &TLI, &TTI, &LVI, &AA,
              std::make_unique<DomTreeUpdater>(
                  &DT, nullptr, DomTreeUpdater::UpdateStrategy::Lazy),
              std::nullopt, std::nullopt);

  if (!Changed)
    return PreservedAnalyses::all();


  getDomTreeUpdater()->flush();

#if defined(EXPENSIVE_CHECKS)
  assert(getDomTreeUpdater()->getDomTree().verify(
             DominatorTree::VerificationLevel::Full) &&
         "DT broken after JumpThreading");
  assert((!getDomTreeUpdater()->hasPostDomTree() ||
          getDomTreeUpdater()->getPostDomTree().verify(
              PostDominatorTree::VerificationLevel::Full)) &&
         "PDT broken after JumpThreading");
#else
  assert(getDomTreeUpdater()->getDomTree().verify(
             DominatorTree::VerificationLevel::Fast) &&
         "DT broken after JumpThreading");
  assert((!getDomTreeUpdater()->hasPostDomTree() ||
          getDomTreeUpdater()->getPostDomTree().verify(
              PostDominatorTree::VerificationLevel::Fast)) &&
         "PDT broken after JumpThreading");
#endif

  return getPreservedAnalysis();
}

bool JumpThreadingPass::runImpl(Function &F_, FunctionAnalysisManager *FAM_,
                                TargetLibraryInfo *TLI_,
                                TargetTransformInfo *TTI_, LazyValueInfo *LVI_,
                                AliasAnalysis *AA_,
                                std::unique_ptr<DomTreeUpdater> DTU_,
                                std::optional<BlockFrequencyInfo *> BFI_,
                                std::optional<BranchProbabilityInfo *> BPI_) {
  LLVM_DEBUG(dbgs() << "Jump threading on function '" << F_.getName() << "'\n");
  F = &F_;
  FAM = FAM_;
  TLI = TLI_;
  TTI = TTI_;
  LVI = LVI_;
  AA = AA_;
  DTU = std::move(DTU_);
  BFI = BFI_;
  BPI = BPI_;
  auto *GuardDecl = F->getParent()->getFunction(
      Intrinsic::getName(Intrinsic::experimental_guard));
  HasGuards = GuardDecl && !GuardDecl->use_empty();

  // Reduce the number of instructions duplicated when optimizing strictly for
  // size.
  if (BBDuplicateThreshold.getNumOccurrences())
    BBDupThreshold = BBDuplicateThreshold;
  else if (F->hasFnAttribute(Attribute::MinSize))
    BBDupThreshold = 3;
  else
    BBDupThreshold = DefaultBBDupThreshold;

  // JumpThreading must not processes blocks unreachable from entry. It's a
  // waste of compute time and can potentially lead to hangs.
  SmallPtrSet<BasicBlock *, 16> Unreachable;
  assert(DTU && "DTU isn't passed into JumpThreading before using it.");
  assert(DTU->hasDomTree() && "JumpThreading relies on DomTree to proceed.");
  DominatorTree &DT = DTU->getDomTree();
  for (auto &BB : *F)
    if (!DT.isReachableFromEntry(&BB))
      Unreachable.insert(&BB);

  if (!ThreadAcrossLoopHeaders)
    findLoopHeaders(*F);

  bool EverChanged = false;
  bool Changed;
  do {
    Changed = false;
    for (auto &BB : *F) {
      if (Unreachable.count(&BB))
        continue;
      while (processBlock(&BB)) // Thread all of the branches we can over BB.
        Changed = ChangedSinceLastAnalysisUpdate = true;

      // Jump threading may have introduced redundant debug values into BB
      // which should be removed.
      if (Changed)
        RemoveRedundantDbgInstrs(&BB);

      // Stop processing BB if it's the entry or is now deleted. The following
      // routines attempt to eliminate BB and locating a suitable replacement
      // for the entry is non-trivial.
      if (&BB == &F->getEntryBlock() || DTU->isBBPendingDeletion(&BB))
        continue;

      if (pred_empty(&BB)) {
        // When processBlock makes BB unreachable it doesn't bother to fix up
        // the instructions in it. We must remove BB to prevent invalid IR.
        LLVM_DEBUG(dbgs() << "  JT: Deleting dead block '" << BB.getName()
                          << "' with terminator: " << *BB.getTerminator()
                          << '\n');
        LoopHeaders.erase(&BB);
        LVI->eraseBlock(&BB);
        DeleteDeadBlock(&BB, DTU.get());
        Changed = ChangedSinceLastAnalysisUpdate = true;
        continue;
      }

      // processBlock doesn't thread BBs with unconditional TIs. However, if BB
      // is "almost empty", we attempt to merge BB with its sole successor.
      auto *BI = dyn_cast<BranchInst>(BB.getTerminator());
      if (BI && BI->isUnconditional()) {
        BasicBlock *Succ = BI->getSuccessor(0);
        if (
            // The terminator must be the only non-phi instruction in BB.
            BB.getFirstNonPHIOrDbg(true)->isTerminator() &&
            // Don't alter Loop headers and latches to ensure another pass can
            // detect and transform nested loops later.
            !LoopHeaders.count(&BB) && !LoopHeaders.count(Succ) &&
            TryToSimplifyUncondBranchFromEmptyBlock(&BB, DTU.get())) {
          RemoveRedundantDbgInstrs(Succ);
          // BB is valid for cleanup here because we passed in DTU. F remains
          // BB's parent until a DTU->getDomTree() event.
          LVI->eraseBlock(&BB);
          Changed = ChangedSinceLastAnalysisUpdate = true;
        }
      }
    }
    EverChanged |= Changed;
  } while (Changed);

  LoopHeaders.clear();
  return EverChanged;
}

// Replace uses of Cond with ToVal when safe to do so. If all uses are
// replaced, we can remove Cond. We cannot blindly replace all uses of Cond
// because we may incorrectly replace uses when guards/assumes are uses of
// of `Cond` and we used the guards/assume to reason about the `Cond` value
// at the end of block. RAUW unconditionally replaces all uses
// including the guards/assumes themselves and the uses before the
// guard/assume.
static bool replaceFoldableUses(Instruction *Cond, Value *ToVal,
                                BasicBlock *KnownAtEndOfBB) {
  bool Changed = false;
  assert(Cond->getType() == ToVal->getType());
  // We can unconditionally replace all uses in non-local blocks (i.e. uses
  // strictly dominated by BB), since LVI information is true from the
  // terminator of BB.
  if (Cond->getParent() == KnownAtEndOfBB)
    Changed |= replaceNonLocalUsesWith(Cond, ToVal);
  for (Instruction &I : reverse(*KnownAtEndOfBB)) {
    // Replace any debug-info record users of Cond with ToVal.
    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
      DVR.replaceVariableLocationOp(Cond, ToVal, true);

    // Reached the Cond whose uses we are trying to replace, so there are no
    // more uses.
    if (&I == Cond)
      break;
    // We only replace uses in instructions that are guaranteed to reach the end
    // of BB, where we know Cond is ToVal.
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      break;
    Changed |= I.replaceUsesOfWith(Cond, ToVal);
  }
  if (Cond->use_empty() && !Cond->mayHaveSideEffects()) {
    Cond->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

/// Return the cost of duplicating a piece of this block from first non-phi
/// and before StopAt instruction to thread across it. Stop scanning the block
/// when exceeding the threshold. If duplication is impossible, returns ~0U.
static unsigned getJumpThreadDuplicationCost(const TargetTransformInfo *TTI,
                                             BasicBlock *BB,
                                             Instruction *StopAt,
                                             unsigned Threshold) {
  assert(StopAt->getParent() == BB && "Not an instruction from proper BB?");

  // Do not duplicate the BB if it has a lot of PHI nodes.
  // If a threadable chain is too long then the number of PHI nodes can add up,
  // leading to a substantial increase in compile time when rewriting the SSA.
  unsigned PhiCount = 0;
  Instruction *FirstNonPHI = nullptr;
  for (Instruction &I : *BB) {
    if (!isa<PHINode>(&I)) {
      FirstNonPHI = &I;
      break;
    }
    if (++PhiCount > PhiDuplicateThreshold)
      return ~0U;
  }

  /// Ignore PHI nodes, these will be flattened when duplication happens.
  BasicBlock::const_iterator I(FirstNonPHI);

  // FIXME: THREADING will delete values that are just used to compute the
  // branch, so they shouldn't count against the duplication cost.

  unsigned Bonus = 0;
  if (BB->getTerminator() == StopAt) {
    // Threading through a switch statement is particularly profitable.  If this
    // block ends in a switch, decrease its cost to make it more likely to
    // happen.
    if (isa<SwitchInst>(StopAt))
      Bonus = 6;

    // The same holds for indirect branches, but slightly more so.
    if (isa<IndirectBrInst>(StopAt))
      Bonus = 8;
  }

  // Bump the threshold up so the early exit from the loop doesn't skip the
  // terminator-based Size adjustment at the end.
  Threshold += Bonus;

  // Sum up the cost of each instruction until we get to the terminator.  Don't
  // include the terminator because the copy won't include it.
  unsigned Size = 0;
  for (; &*I != StopAt; ++I) {

    // Stop scanning the block if we've reached the threshold.
    if (Size > Threshold)
      return Size;

    // Bail out if this instruction gives back a token type, it is not possible
    // to duplicate it if it is used outside this BB.
    if (I->getType()->isTokenTy() && I->isUsedOutsideOfBlock(BB))
      return ~0U;

    // Blocks with NoDuplicate are modelled as having infinite cost, so they
    // are never duplicated.
    if (const CallInst *CI = dyn_cast<CallInst>(I))
      if (CI->cannotDuplicate() || CI->isConvergent())
        return ~0U;

    if (TTI->getInstructionCost(&*I, TargetTransformInfo::TCK_SizeAndLatency) ==
        TargetTransformInfo::TCC_Free)
      continue;

    // All other instructions count for at least one unit.
    ++Size;

    // Calls are more expensive.  If they are non-intrinsic calls, we model them
    // as having cost of 4.  If they are a non-vector intrinsic, we model them
    // as having cost of 2 total, and if they are a vector intrinsic, we model
    // them as having cost 1.
    if (const CallInst *CI = dyn_cast<CallInst>(I)) {
      if (!isa<IntrinsicInst>(CI))
        Size += 3;
      else if (!CI->getType()->isVectorTy())
        Size += 1;
    }
  }

  return Size > Bonus ? Size - Bonus : 0;
}

/// findLoopHeaders - We do not want jump threading to turn proper loop
/// structures into irreducible loops.  Doing this breaks up the loop nesting
/// hierarchy and pessimizes later transformations.  To prevent this from
/// happening, we first have to find the loop headers.  Here we approximate this
/// by finding targets of backedges in the CFG.
///
/// Note that there definitely are cases when we want to allow threading of
/// edges across a loop header.  For example, threading a jump from outside the
/// loop (the preheader) to an exit block of the loop is definitely profitable.
/// It is also almost always profitable to thread backedges from within the loop
/// to exit blocks, and is often profitable to thread backedges to other blocks
/// within the loop (forming a nested loop).  This simple analysis is not rich
/// enough to track all of these properties and keep it up-to-date as the CFG
/// mutates, so we don't allow any of these transformations.
void JumpThreadingPass::findLoopHeaders(Function &F) {
  SmallVector<std::pair<const BasicBlock*,const BasicBlock*>, 32> Edges;
  FindFunctionBackedges(F, Edges);

  for (const auto &Edge : Edges)
    LoopHeaders.insert(Edge.second);
}

/// getKnownConstant - Helper method to determine if we can thread over a
/// terminator with the given value as its condition, and if so what value to
/// use for that. What kind of value this is depends on whether we want an
/// integer or a block address, but an undef is always accepted.
/// Returns null if Val is null or not an appropriate constant.
static Constant *getKnownConstant(Value *Val, ConstantPreference Preference) {
  if (!Val)
    return nullptr;

  // Undef is "known" enough.
  if (UndefValue *U = dyn_cast<UndefValue>(Val))
    return U;

  if (Preference == WantBlockAddress)
    return dyn_cast<BlockAddress>(Val->stripPointerCasts());

  return dyn_cast<ConstantInt>(Val);
}

/// computeValueKnownInPredecessors - Given a basic block BB and a value V, see
/// if we can infer that the value is a known ConstantInt/BlockAddress or undef
/// in any of our predecessors.  If so, return the known list of value and pred
/// BB in the result vector.
///
/// This returns true if there were any known values.
bool JumpThreadingPass::computeValueKnownInPredecessorsImpl(
    Value *V, BasicBlock *BB, PredValueInfo &Result,
    ConstantPreference Preference, SmallPtrSet<Value *, 4> &RecursionSet,
    Instruction *CxtI) {
  const DataLayout &DL = BB->getDataLayout();

  // This method walks up use-def chains recursively.  Because of this, we could
  // get into an infinite loop going around loops in the use-def chain.  To
  // prevent this, keep track of what (value, block) pairs we've already visited
  // and terminate the search if we loop back to them
  if (!RecursionSet.insert(V).second)
    return false;

  // If V is a constant, then it is known in all predecessors.
  if (Constant *KC = getKnownConstant(V, Preference)) {
    for (BasicBlock *Pred : predecessors(BB))
      Result.emplace_back(KC, Pred);

    return !Result.empty();
  }

  // If V is a non-instruction value, or an instruction in a different block,
  // then it can't be derived from a PHI.
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I || I->getParent() != BB) {

    // Okay, if this is a live-in value, see if it has a known value at the any
    // edge from our predecessors.
    for (BasicBlock *P : predecessors(BB)) {
      using namespace PatternMatch;
      // If the value is known by LazyValueInfo to be a constant in a
      // predecessor, use that information to try to thread this block.
      Constant *PredCst = LVI->getConstantOnEdge(V, P, BB, CxtI);
      // If I is a non-local compare-with-constant instruction, use more-rich
      // 'getPredicateOnEdge' method. This would be able to handle value
      // inequalities better, for example if the compare is "X < 4" and "X < 3"
      // is known true but "X < 4" itself is not available.
      CmpInst::Predicate Pred;
      Value *Val;
      Constant *Cst;
      if (!PredCst && match(V, m_Cmp(Pred, m_Value(Val), m_Constant(Cst))))
        PredCst = LVI->getPredicateOnEdge(Pred, Val, Cst, P, BB, CxtI);
      if (Constant *KC = getKnownConstant(PredCst, Preference))
        Result.emplace_back(KC, P);
    }

    return !Result.empty();
  }

  /// If I is a PHI node, then we know the incoming values for any constants.
  if (PHINode *PN = dyn_cast<PHINode>(I)) {
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
      Value *InVal = PN->getIncomingValue(i);
      if (Constant *KC = getKnownConstant(InVal, Preference)) {
        Result.emplace_back(KC, PN->getIncomingBlock(i));
      } else {
        Constant *CI = LVI->getConstantOnEdge(InVal,
                                              PN->getIncomingBlock(i),
                                              BB, CxtI);
        if (Constant *KC = getKnownConstant(CI, Preference))
          Result.emplace_back(KC, PN->getIncomingBlock(i));
      }
    }

    return !Result.empty();
  }

  // Handle Cast instructions.
  if (CastInst *CI = dyn_cast<CastInst>(I)) {
    Value *Source = CI->getOperand(0);
    PredValueInfoTy Vals;
    computeValueKnownInPredecessorsImpl(Source, BB, Vals, Preference,
                                        RecursionSet, CxtI);
    if (Vals.empty())
      return false;

    // Convert the known values.
    for (auto &Val : Vals)
      if (Constant *Folded = ConstantFoldCastOperand(CI->getOpcode(), Val.first,
                                                     CI->getType(), DL))
        Result.emplace_back(Folded, Val.second);

    return !Result.empty();
  }

  if (FreezeInst *FI = dyn_cast<FreezeInst>(I)) {
    Value *Source = FI->getOperand(0);
    computeValueKnownInPredecessorsImpl(Source, BB, Result, Preference,
                                        RecursionSet, CxtI);

    erase_if(Result, [](auto &Pair) {
      return !isGuaranteedNotToBeUndefOrPoison(Pair.first);
    });

    return !Result.empty();
  }

  // Handle some boolean conditions.
  if (I->getType()->getPrimitiveSizeInBits() == 1) {
    using namespace PatternMatch;
    if (Preference != WantInteger)
      return false;
    // X | true -> true
    // X & false -> false
    Value *Op0, *Op1;
    if (match(I, m_LogicalOr(m_Value(Op0), m_Value(Op1))) ||
        match(I, m_LogicalAnd(m_Value(Op0), m_Value(Op1)))) {
      PredValueInfoTy LHSVals, RHSVals;

      computeValueKnownInPredecessorsImpl(Op0, BB, LHSVals, WantInteger,
                                          RecursionSet, CxtI);
      computeValueKnownInPredecessorsImpl(Op1, BB, RHSVals, WantInteger,
                                          RecursionSet, CxtI);

      if (LHSVals.empty() && RHSVals.empty())
        return false;

      ConstantInt *InterestingVal;
      if (match(I, m_LogicalOr()))
        InterestingVal = ConstantInt::getTrue(I->getContext());
      else
        InterestingVal = ConstantInt::getFalse(I->getContext());

      SmallPtrSet<BasicBlock*, 4> LHSKnownBBs;

      // Scan for the sentinel.  If we find an undef, force it to the
      // interesting value: x|undef -> true and x&undef -> false.
      for (const auto &LHSVal : LHSVals)
        if (LHSVal.first == InterestingVal || isa<UndefValue>(LHSVal.first)) {
          Result.emplace_back(InterestingVal, LHSVal.second);
          LHSKnownBBs.insert(LHSVal.second);
        }
      for (const auto &RHSVal : RHSVals)
        if (RHSVal.first == InterestingVal || isa<UndefValue>(RHSVal.first)) {
          // If we already inferred a value for this block on the LHS, don't
          // re-add it.
          if (!LHSKnownBBs.count(RHSVal.second))
            Result.emplace_back(InterestingVal, RHSVal.second);
        }

      return !Result.empty();
    }

    // Handle the NOT form of XOR.
    if (I->getOpcode() == Instruction::Xor &&
        isa<ConstantInt>(I->getOperand(1)) &&
        cast<ConstantInt>(I->getOperand(1))->isOne()) {
      computeValueKnownInPredecessorsImpl(I->getOperand(0), BB, Result,
                                          WantInteger, RecursionSet, CxtI);
      if (Result.empty())
        return false;

      // Invert the known values.
      for (auto &R : Result)
        R.first = ConstantExpr::getNot(R.first);

      return true;
    }

  // Try to simplify some other binary operator values.
  } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(I)) {
    if (Preference != WantInteger)
      return false;
    if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->getOperand(1))) {
      PredValueInfoTy LHSVals;
      computeValueKnownInPredecessorsImpl(BO->getOperand(0), BB, LHSVals,
                                          WantInteger, RecursionSet, CxtI);

      // Try to use constant folding to simplify the binary operator.
      for (const auto &LHSVal : LHSVals) {
        Constant *V = LHSVal.first;
        Constant *Folded =
            ConstantFoldBinaryOpOperands(BO->getOpcode(), V, CI, DL);

        if (Constant *KC = getKnownConstant(Folded, WantInteger))
          Result.emplace_back(KC, LHSVal.second);
      }
    }

    return !Result.empty();
  }

  // Handle compare with phi operand, where the PHI is defined in this block.
  if (CmpInst *Cmp = dyn_cast<CmpInst>(I)) {
    if (Preference != WantInteger)
      return false;
    Type *CmpType = Cmp->getType();
    Value *CmpLHS = Cmp->getOperand(0);
    Value *CmpRHS = Cmp->getOperand(1);
    CmpInst::Predicate Pred = Cmp->getPredicate();

    PHINode *PN = dyn_cast<PHINode>(CmpLHS);
    if (!PN)
      PN = dyn_cast<PHINode>(CmpRHS);
    // Do not perform phi translation across a loop header phi, because this
    // may result in comparison of values from two different loop iterations.
    // FIXME: This check is broken if LoopHeaders is not populated.
    if (PN && PN->getParent() == BB && !LoopHeaders.contains(BB)) {
      const DataLayout &DL = PN->getDataLayout();
      // We can do this simplification if any comparisons fold to true or false.
      // See if any do.
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        BasicBlock *PredBB = PN->getIncomingBlock(i);
        Value *LHS, *RHS;
        if (PN == CmpLHS) {
          LHS = PN->getIncomingValue(i);
          RHS = CmpRHS->DoPHITranslation(BB, PredBB);
        } else {
          LHS = CmpLHS->DoPHITranslation(BB, PredBB);
          RHS = PN->getIncomingValue(i);
        }
        Value *Res = simplifyCmpInst(Pred, LHS, RHS, {DL});
        if (!Res) {
          if (!isa<Constant>(RHS))
            continue;

          // getPredicateOnEdge call will make no sense if LHS is defined in BB.
          auto LHSInst = dyn_cast<Instruction>(LHS);
          if (LHSInst && LHSInst->getParent() == BB)
            continue;

          Res = LVI->getPredicateOnEdge(Pred, LHS, cast<Constant>(RHS), PredBB,
                                        BB, CxtI ? CxtI : Cmp);
        }

        if (Constant *KC = getKnownConstant(Res, WantInteger))
          Result.emplace_back(KC, PredBB);
      }

      return !Result.empty();
    }

    // If comparing a live-in value against a constant, see if we know the
    // live-in value on any predecessors.
    if (isa<Constant>(CmpRHS) && !CmpType->isVectorTy()) {
      Constant *CmpConst = cast<Constant>(CmpRHS);

      if (!isa<Instruction>(CmpLHS) ||
          cast<Instruction>(CmpLHS)->getParent() != BB) {
        for (BasicBlock *P : predecessors(BB)) {
          // If the value is known by LazyValueInfo to be a constant in a
          // predecessor, use that information to try to thread this block.
          Constant *Res = LVI->getPredicateOnEdge(Pred, CmpLHS, CmpConst, P, BB,
                                                  CxtI ? CxtI : Cmp);
          if (Constant *KC = getKnownConstant(Res, WantInteger))
            Result.emplace_back(KC, P);
        }

        return !Result.empty();
      }

      // InstCombine can fold some forms of constant range checks into
      // (icmp (add (x, C1)), C2). See if we have we have such a thing with
      // x as a live-in.
      {
        using namespace PatternMatch;

        Value *AddLHS;
        ConstantInt *AddConst;
        if (isa<ConstantInt>(CmpConst) &&
            match(CmpLHS, m_Add(m_Value(AddLHS), m_ConstantInt(AddConst)))) {
          if (!isa<Instruction>(AddLHS) ||
              cast<Instruction>(AddLHS)->getParent() != BB) {
            for (BasicBlock *P : predecessors(BB)) {
              // If the value is known by LazyValueInfo to be a ConstantRange in
              // a predecessor, use that information to try to thread this
              // block.
              ConstantRange CR = LVI->getConstantRangeOnEdge(
                  AddLHS, P, BB, CxtI ? CxtI : cast<Instruction>(CmpLHS));
              // Propagate the range through the addition.
              CR = CR.add(AddConst->getValue());

              // Get the range where the compare returns true.
              ConstantRange CmpRange = ConstantRange::makeExactICmpRegion(
                  Pred, cast<ConstantInt>(CmpConst)->getValue());

              Constant *ResC;
              if (CmpRange.contains(CR))
                ResC = ConstantInt::getTrue(CmpType);
              else if (CmpRange.inverse().contains(CR))
                ResC = ConstantInt::getFalse(CmpType);
              else
                continue;

              Result.emplace_back(ResC, P);
            }

            return !Result.empty();
          }
        }
      }

      // Try to find a constant value for the LHS of a comparison,
      // and evaluate it statically if we can.
      PredValueInfoTy LHSVals;
      computeValueKnownInPredecessorsImpl(I->getOperand(0), BB, LHSVals,
                                          WantInteger, RecursionSet, CxtI);

      for (const auto &LHSVal : LHSVals) {
        Constant *V = LHSVal.first;
        Constant *Folded =
            ConstantFoldCompareInstOperands(Pred, V, CmpConst, DL);
        if (Constant *KC = getKnownConstant(Folded, WantInteger))
          Result.emplace_back(KC, LHSVal.second);
      }

      return !Result.empty();
    }
  }

  if (SelectInst *SI = dyn_cast<SelectInst>(I)) {
    // Handle select instructions where at least one operand is a known constant
    // and we can figure out the condition value for any predecessor block.
    Constant *TrueVal = getKnownConstant(SI->getTrueValue(), Preference);
    Constant *FalseVal = getKnownConstant(SI->getFalseValue(), Preference);
    PredValueInfoTy Conds;
    if ((TrueVal || FalseVal) &&
        computeValueKnownInPredecessorsImpl(SI->getCondition(), BB, Conds,
                                            WantInteger, RecursionSet, CxtI)) {
      for (auto &C : Conds) {
        Constant *Cond = C.first;

        // Figure out what value to use for the condition.
        bool KnownCond;
        if (ConstantInt *CI = dyn_cast<ConstantInt>(Cond)) {
          // A known boolean.
          KnownCond = CI->isOne();
        } else {
          assert(isa<UndefValue>(Cond) && "Unexpected condition value");
          // Either operand will do, so be sure to pick the one that's a known
          // constant.
          // FIXME: Do this more cleverly if both values are known constants?
          KnownCond = (TrueVal != nullptr);
        }

        // See if the select has a known constant value for this predecessor.
        if (Constant *Val = KnownCond ? TrueVal : FalseVal)
          Result.emplace_back(Val, C.second);
      }

      return !Result.empty();
    }
  }

  // If all else fails, see if LVI can figure out a constant value for us.
  assert(CxtI->getParent() == BB && "CxtI should be in BB");
  Constant *CI = LVI->getConstant(V, CxtI);
  if (Constant *KC = getKnownConstant(CI, Preference)) {
    for (BasicBlock *Pred : predecessors(BB))
      Result.emplace_back(KC, Pred);
  }

  return !Result.empty();
}

/// GetBestDestForBranchOnUndef - If we determine that the specified block ends
/// in an undefined jump, decide which block is best to revector to.
///
/// Since we can pick an arbitrary destination, we pick the successor with the
/// fewest predecessors.  This should reduce the in-degree of the others.
static unsigned getBestDestForJumpOnUndef(BasicBlock *BB) {
  Instruction *BBTerm = BB->getTerminator();
  unsigned MinSucc = 0;
  BasicBlock *TestBB = BBTerm->getSuccessor(MinSucc);
  // Compute the successor with the minimum number of predecessors.
  unsigned MinNumPreds = pred_size(TestBB);
  for (unsigned i = 1, e = BBTerm->getNumSuccessors(); i != e; ++i) {
    TestBB = BBTerm->getSuccessor(i);
    unsigned NumPreds = pred_size(TestBB);
    if (NumPreds < MinNumPreds) {
      MinSucc = i;
      MinNumPreds = NumPreds;
    }
  }

  return MinSucc;
}

static bool hasAddressTakenAndUsed(BasicBlock *BB) {
  if (!BB->hasAddressTaken()) return false;

  // If the block has its address taken, it may be a tree of dead constants
  // hanging off of it.  These shouldn't keep the block alive.
  BlockAddress *BA = BlockAddress::get(BB);
  BA->removeDeadConstantUsers();
  return !BA->use_empty();
}

/// processBlock - If there are any predecessors whose control can be threaded
/// through to a successor, transform them now.
bool JumpThreadingPass::processBlock(BasicBlock *BB) {
  // If the block is trivially dead, just return and let the caller nuke it.
  // This simplifies other transformations.
  if (DTU->isBBPendingDeletion(BB) ||
      (pred_empty(BB) && BB != &BB->getParent()->getEntryBlock()))
    return false;

  // If this block has a single predecessor, and if that pred has a single
  // successor, merge the blocks.  This encourages recursive jump threading
  // because now the condition in this block can be threaded through
  // predecessors of our predecessor block.
  if (maybeMergeBasicBlockIntoOnlyPred(BB))
    return true;

  if (tryToUnfoldSelectInCurrBB(BB))
    return true;

  // Look if we can propagate guards to predecessors.
  if (HasGuards && processGuards(BB))
    return true;

  // What kind of constant we're looking for.
  ConstantPreference Preference = WantInteger;

  // Look to see if the terminator is a conditional branch, switch or indirect
  // branch, if not we can't thread it.
  Value *Condition;
  Instruction *Terminator = BB->getTerminator();
  if (BranchInst *BI = dyn_cast<BranchInst>(Terminator)) {
    // Can't thread an unconditional jump.
    if (BI->isUnconditional()) return false;
    Condition = BI->getCondition();
  } else if (SwitchInst *SI = dyn_cast<SwitchInst>(Terminator)) {
    Condition = SI->getCondition();
  } else if (IndirectBrInst *IB = dyn_cast<IndirectBrInst>(Terminator)) {
    // Can't thread indirect branch with no successors.
    if (IB->getNumSuccessors() == 0) return false;
    Condition = IB->getAddress()->stripPointerCasts();
    Preference = WantBlockAddress;
  } else {
    return false; // Must be an invoke or callbr.
  }

  // Keep track if we constant folded the condition in this invocation.
  bool ConstantFolded = false;

  // Run constant folding to see if we can reduce the condition to a simple
  // constant.
  if (Instruction *I = dyn_cast<Instruction>(Condition)) {
    Value *SimpleVal =
        ConstantFoldInstruction(I, BB->getDataLayout(), TLI);
    if (SimpleVal) {
      I->replaceAllUsesWith(SimpleVal);
      if (isInstructionTriviallyDead(I, TLI))
        I->eraseFromParent();
      Condition = SimpleVal;
      ConstantFolded = true;
    }
  }

  // If the terminator is branching on an undef or freeze undef, we can pick any
  // of the successors to branch to.  Let getBestDestForJumpOnUndef decide.
  auto *FI = dyn_cast<FreezeInst>(Condition);
  if (isa<UndefValue>(Condition) ||
      (FI && isa<UndefValue>(FI->getOperand(0)) && FI->hasOneUse())) {
    unsigned BestSucc = getBestDestForJumpOnUndef(BB);
    std::vector<DominatorTree::UpdateType> Updates;

    // Fold the branch/switch.
    Instruction *BBTerm = BB->getTerminator();
    Updates.reserve(BBTerm->getNumSuccessors());
    for (unsigned i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
      if (i == BestSucc) continue;
      BasicBlock *Succ = BBTerm->getSuccessor(i);
      Succ->removePredecessor(BB, true);
      Updates.push_back({DominatorTree::Delete, BB, Succ});
    }

    LLVM_DEBUG(dbgs() << "  In block '" << BB->getName()
                      << "' folding undef terminator: " << *BBTerm << '\n');
    Instruction *NewBI = BranchInst::Create(BBTerm->getSuccessor(BestSucc), BBTerm->getIterator());
    NewBI->setDebugLoc(BBTerm->getDebugLoc());
    ++NumFolds;
    BBTerm->eraseFromParent();
    DTU->applyUpdatesPermissive(Updates);
    if (FI)
      FI->eraseFromParent();
    return true;
  }

  // If the terminator of this block is branching on a constant, simplify the
  // terminator to an unconditional branch.  This can occur due to threading in
  // other blocks.
  if (getKnownConstant(Condition, Preference)) {
    LLVM_DEBUG(dbgs() << "  In block '" << BB->getName()
                      << "' folding terminator: " << *BB->getTerminator()
                      << '\n');
    ++NumFolds;
    ConstantFoldTerminator(BB, true, nullptr, DTU.get());
    if (auto *BPI = getBPI())
      BPI->eraseBlock(BB);
    return true;
  }

  Instruction *CondInst = dyn_cast<Instruction>(Condition);

  // All the rest of our checks depend on the condition being an instruction.
  if (!CondInst) {
    // FIXME: Unify this with code below.
    if (processThreadableEdges(Condition, BB, Preference, Terminator))
      return true;
    return ConstantFolded;
  }

  // Some of the following optimization can safely work on the unfrozen cond.
  Value *CondWithoutFreeze = CondInst;
  if (auto *FI = dyn_cast<FreezeInst>(CondInst))
    CondWithoutFreeze = FI->getOperand(0);

  if (CmpInst *CondCmp = dyn_cast<CmpInst>(CondWithoutFreeze)) {
    // If we're branching on a conditional, LVI might be able to determine
    // it's value at the branch instruction.  We only handle comparisons
    // against a constant at this time.
    if (Constant *CondConst = dyn_cast<Constant>(CondCmp->getOperand(1))) {
      Constant *Res =
          LVI->getPredicateAt(CondCmp->getPredicate(), CondCmp->getOperand(0),
                              CondConst, BB->getTerminator(),
                              /*UseBlockValue=*/false);
      if (Res) {
        // We can safely replace *some* uses of the CondInst if it has
        // exactly one value as returned by LVI. RAUW is incorrect in the
        // presence of guards and assumes, that have the `Cond` as the use. This
        // is because we use the guards/assume to reason about the `Cond` value
        // at the end of block, but RAUW unconditionally replaces all uses
        // including the guards/assumes themselves and the uses before the
        // guard/assume.
        if (replaceFoldableUses(CondCmp, Res, BB))
          return true;
      }

      // We did not manage to simplify this branch, try to see whether
      // CondCmp depends on a known phi-select pattern.
      if (tryToUnfoldSelect(CondCmp, BB))
        return true;
    }
  }

  if (SwitchInst *SI = dyn_cast<SwitchInst>(BB->getTerminator()))
    if (tryToUnfoldSelect(SI, BB))
      return true;

  // Check for some cases that are worth simplifying.  Right now we want to look
  // for loads that are used by a switch or by the condition for the branch.  If
  // we see one, check to see if it's partially redundant.  If so, insert a PHI
  // which can then be used to thread the values.
  Value *SimplifyValue = CondWithoutFreeze;

  if (CmpInst *CondCmp = dyn_cast<CmpInst>(SimplifyValue))
    if (isa<Constant>(CondCmp->getOperand(1)))
      SimplifyValue = CondCmp->getOperand(0);

  // TODO: There are other places where load PRE would be profitable, such as
  // more complex comparisons.
  if (LoadInst *LoadI = dyn_cast<LoadInst>(SimplifyValue))
    if (simplifyPartiallyRedundantLoad(LoadI))
      return true;

  // Before threading, try to propagate profile data backwards:
  if (PHINode *PN = dyn_cast<PHINode>(CondInst))
    if (PN->getParent() == BB && isa<BranchInst>(BB->getTerminator()))
      updatePredecessorProfileMetadata(PN, BB);

  // Handle a variety of cases where we are branching on something derived from
  // a PHI node in the current block.  If we can prove that any predecessors
  // compute a predictable value based on a PHI node, thread those predecessors.
  if (processThreadableEdges(CondInst, BB, Preference, Terminator))
    return true;

  // If this is an otherwise-unfoldable branch on a phi node or freeze(phi) in
  // the current block, see if we can simplify.
  PHINode *PN = dyn_cast<PHINode>(CondWithoutFreeze);
  if (PN && PN->getParent() == BB && isa<BranchInst>(BB->getTerminator()))
    return processBranchOnPHI(PN);

  // If this is an otherwise-unfoldable branch on a XOR, see if we can simplify.
  if (CondInst->getOpcode() == Instruction::Xor &&
      CondInst->getParent() == BB && isa<BranchInst>(BB->getTerminator()))
    return processBranchOnXOR(cast<BinaryOperator>(CondInst));

  // Search for a stronger dominating condition that can be used to simplify a
  // conditional branch leaving BB.
  if (processImpliedCondition(BB))
    return true;

  return false;
}

bool JumpThreadingPass::processImpliedCondition(BasicBlock *BB) {
  auto *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  Value *Cond = BI->getCondition();
  // Assuming that predecessor's branch was taken, if pred's branch condition
  // (V) implies Cond, Cond can be either true, undef, or poison. In this case,
  // freeze(Cond) is either true or a nondeterministic value.
  // If freeze(Cond) has only one use, we can freely fold freeze(Cond) to true
  // without affecting other instructions.
  auto *FICond = dyn_cast<FreezeInst>(Cond);
  if (FICond && FICond->hasOneUse())
    Cond = FICond->getOperand(0);
  else
    FICond = nullptr;

  BasicBlock *CurrentBB = BB;
  BasicBlock *CurrentPred = BB->getSinglePredecessor();
  unsigned Iter = 0;

  auto &DL = BB->getDataLayout();

  while (CurrentPred && Iter++ < ImplicationSearchThreshold) {
    auto *PBI = dyn_cast<BranchInst>(CurrentPred->getTerminator());
    if (!PBI || !PBI->isConditional())
      return false;
    if (PBI->getSuccessor(0) != CurrentBB && PBI->getSuccessor(1) != CurrentBB)
      return false;

    bool CondIsTrue = PBI->getSuccessor(0) == CurrentBB;
    std::optional<bool> Implication =
        isImpliedCondition(PBI->getCondition(), Cond, DL, CondIsTrue);

    // If the branch condition of BB (which is Cond) and CurrentPred are
    // exactly the same freeze instruction, Cond can be folded into CondIsTrue.
    if (!Implication && FICond && isa<FreezeInst>(PBI->getCondition())) {
      if (cast<FreezeInst>(PBI->getCondition())->getOperand(0) ==
          FICond->getOperand(0))
        Implication = CondIsTrue;
    }

    if (Implication) {
      BasicBlock *KeepSucc = BI->getSuccessor(*Implication ? 0 : 1);
      BasicBlock *RemoveSucc = BI->getSuccessor(*Implication ? 1 : 0);
      RemoveSucc->removePredecessor(BB);
      BranchInst *UncondBI = BranchInst::Create(KeepSucc, BI->getIterator());
      UncondBI->setDebugLoc(BI->getDebugLoc());
      ++NumFolds;
      BI->eraseFromParent();
      if (FICond)
        FICond->eraseFromParent();

      DTU->applyUpdatesPermissive({{DominatorTree::Delete, BB, RemoveSucc}});
      if (auto *BPI = getBPI())
        BPI->eraseBlock(BB);
      return true;
    }
    CurrentBB = CurrentPred;
    CurrentPred = CurrentBB->getSinglePredecessor();
  }

  return false;
}

/// Return true if Op is an instruction defined in the given block.
static bool isOpDefinedInBlock(Value *Op, BasicBlock *BB) {
  if (Instruction *OpInst = dyn_cast<Instruction>(Op))
    if (OpInst->getParent() == BB)
      return true;
  return false;
}

/// simplifyPartiallyRedundantLoad - If LoadI is an obviously partially
/// redundant load instruction, eliminate it by replacing it with a PHI node.
/// This is an important optimization that encourages jump threading, and needs
/// to be run interlaced with other jump threading tasks.
bool JumpThreadingPass::simplifyPartiallyRedundantLoad(LoadInst *LoadI) {
  // Don't hack volatile and ordered loads.
  if (!LoadI->isUnordered()) return false;

  // If the load is defined in a block with exactly one predecessor, it can't be
  // partially redundant.
  BasicBlock *LoadBB = LoadI->getParent();
  if (LoadBB->getSinglePredecessor())
    return false;

  // If the load is defined in an EH pad, it can't be partially redundant,
  // because the edges between the invoke and the EH pad cannot have other
  // instructions between them.
  if (LoadBB->isEHPad())
    return false;

  Value *LoadedPtr = LoadI->getOperand(0);

  // If the loaded operand is defined in the LoadBB and its not a phi,
  // it can't be available in predecessors.
  if (isOpDefinedInBlock(LoadedPtr, LoadBB) && !isa<PHINode>(LoadedPtr))
    return false;

  // Scan a few instructions up from the load, to see if it is obviously live at
  // the entry to its block.
  BasicBlock::iterator BBIt(LoadI);
  bool IsLoadCSE;
  BatchAAResults BatchAA(*AA);
  // The dominator tree is updated lazily and may not be valid at this point.
  BatchAA.disableDominatorTree();
  if (Value *AvailableVal = FindAvailableLoadedValue(
          LoadI, LoadBB, BBIt, DefMaxInstsToScan, &BatchAA, &IsLoadCSE)) {
    // If the value of the load is locally available within the block, just use
    // it.  This frequently occurs for reg2mem'd allocas.

    if (IsLoadCSE) {
      LoadInst *NLoadI = cast<LoadInst>(AvailableVal);
      combineMetadataForCSE(NLoadI, LoadI, false);
      LVI->forgetValue(NLoadI);
    };

    // If the returned value is the load itself, replace with poison. This can
    // only happen in dead loops.
    if (AvailableVal == LoadI)
      AvailableVal = PoisonValue::get(LoadI->getType());
    if (AvailableVal->getType() != LoadI->getType()) {
      AvailableVal = CastInst::CreateBitOrPointerCast(
          AvailableVal, LoadI->getType(), "", LoadI->getIterator());
      cast<Instruction>(AvailableVal)->setDebugLoc(LoadI->getDebugLoc());
    }
    LoadI->replaceAllUsesWith(AvailableVal);
    LoadI->eraseFromParent();
    return true;
  }

  // Otherwise, if we scanned the whole block and got to the top of the block,
  // we know the block is locally transparent to the load.  If not, something
  // might clobber its value.
  if (BBIt != LoadBB->begin())
    return false;

  // If all of the loads and stores that feed the value have the same AA tags,
  // then we can propagate them onto any newly inserted loads.
  AAMDNodes AATags = LoadI->getAAMetadata();

  SmallPtrSet<BasicBlock*, 8> PredsScanned;

  using AvailablePredsTy = SmallVector<std::pair<BasicBlock *, Value *>, 8>;

  AvailablePredsTy AvailablePreds;
  BasicBlock *OneUnavailablePred = nullptr;
  SmallVector<LoadInst*, 8> CSELoads;

  // If we got here, the loaded value is transparent through to the start of the
  // block.  Check to see if it is available in any of the predecessor blocks.
  for (BasicBlock *PredBB : predecessors(LoadBB)) {
    // If we already scanned this predecessor, skip it.
    if (!PredsScanned.insert(PredBB).second)
      continue;

    BBIt = PredBB->end();
    unsigned NumScanedInst = 0;
    Value *PredAvailable = nullptr;
    // NOTE: We don't CSE load that is volatile or anything stronger than
    // unordered, that should have been checked when we entered the function.
    assert(LoadI->isUnordered() &&
           "Attempting to CSE volatile or atomic loads");
    // If this is a load on a phi pointer, phi-translate it and search
    // for available load/store to the pointer in predecessors.
    Type *AccessTy = LoadI->getType();
    const auto &DL = LoadI->getDataLayout();
    MemoryLocation Loc(LoadedPtr->DoPHITranslation(LoadBB, PredBB),
                       LocationSize::precise(DL.getTypeStoreSize(AccessTy)),
                       AATags);
    PredAvailable = findAvailablePtrLoadStore(
        Loc, AccessTy, LoadI->isAtomic(), PredBB, BBIt, DefMaxInstsToScan,
        &BatchAA, &IsLoadCSE, &NumScanedInst);

    // If PredBB has a single predecessor, continue scanning through the
    // single predecessor.
    BasicBlock *SinglePredBB = PredBB;
    while (!PredAvailable && SinglePredBB && BBIt == SinglePredBB->begin() &&
           NumScanedInst < DefMaxInstsToScan) {
      SinglePredBB = SinglePredBB->getSinglePredecessor();
      if (SinglePredBB) {
        BBIt = SinglePredBB->end();
        PredAvailable = findAvailablePtrLoadStore(
            Loc, AccessTy, LoadI->isAtomic(), SinglePredBB, BBIt,
            (DefMaxInstsToScan - NumScanedInst), &BatchAA, &IsLoadCSE,
            &NumScanedInst);
      }
    }

    if (!PredAvailable) {
      OneUnavailablePred = PredBB;
      continue;
    }

    if (IsLoadCSE)
      CSELoads.push_back(cast<LoadInst>(PredAvailable));

    // If so, this load is partially redundant.  Remember this info so that we
    // can create a PHI node.
    AvailablePreds.emplace_back(PredBB, PredAvailable);
  }

  // If the loaded value isn't available in any predecessor, it isn't partially
  // redundant.
  if (AvailablePreds.empty()) return false;

  // Okay, the loaded value is available in at least one (and maybe all!)
  // predecessors.  If the value is unavailable in more than one unique
  // predecessor, we want to insert a merge block for those common predecessors.
  // This ensures that we only have to insert one reload, thus not increasing
  // code size.
  BasicBlock *UnavailablePred = nullptr;

  // If the value is unavailable in one of predecessors, we will end up
  // inserting a new instruction into them. It is only valid if all the
  // instructions before LoadI are guaranteed to pass execution to its
  // successor, or if LoadI is safe to speculate.
  // TODO: If this logic becomes more complex, and we will perform PRE insertion
  // farther than to a predecessor, we need to reuse the code from GVN's PRE.
  // It requires domination tree analysis, so for this simple case it is an
  // overkill.
  if (PredsScanned.size() != AvailablePreds.size() &&
      !isSafeToSpeculativelyExecute(LoadI))
    for (auto I = LoadBB->begin(); &*I != LoadI; ++I)
      if (!isGuaranteedToTransferExecutionToSuccessor(&*I))
        return false;

  // If there is exactly one predecessor where the value is unavailable, the
  // already computed 'OneUnavailablePred' block is it.  If it ends in an
  // unconditional branch, we know that it isn't a critical edge.
  if (PredsScanned.size() == AvailablePreds.size()+1 &&
      OneUnavailablePred->getTerminator()->getNumSuccessors() == 1) {
    UnavailablePred = OneUnavailablePred;
  } else if (PredsScanned.size() != AvailablePreds.size()) {
    // Otherwise, we had multiple unavailable predecessors or we had a critical
    // edge from the one.
    SmallVector<BasicBlock*, 8> PredsToSplit;
    SmallPtrSet<BasicBlock*, 8> AvailablePredSet;

    for (const auto &AvailablePred : AvailablePreds)
      AvailablePredSet.insert(AvailablePred.first);

    // Add all the unavailable predecessors to the PredsToSplit list.
    for (BasicBlock *P : predecessors(LoadBB)) {
      // If the predecessor is an indirect goto, we can't split the edge.
      if (isa<IndirectBrInst>(P->getTerminator()))
        return false;

      if (!AvailablePredSet.count(P))
        PredsToSplit.push_back(P);
    }

    // Split them out to their own block.
    UnavailablePred = splitBlockPreds(LoadBB, PredsToSplit, "thread-pre-split");
  }

  // If the value isn't available in all predecessors, then there will be
  // exactly one where it isn't available.  Insert a load on that edge and add
  // it to the AvailablePreds list.
  if (UnavailablePred) {
    assert(UnavailablePred->getTerminator()->getNumSuccessors() == 1 &&
           "Can't handle critical edge here!");
    LoadInst *NewVal = new LoadInst(
        LoadI->getType(), LoadedPtr->DoPHITranslation(LoadBB, UnavailablePred),
        LoadI->getName() + ".pr", false, LoadI->getAlign(),
        LoadI->getOrdering(), LoadI->getSyncScopeID(),
        UnavailablePred->getTerminator()->getIterator());
    NewVal->setDebugLoc(LoadI->getDebugLoc());
    if (AATags)
      NewVal->setAAMetadata(AATags);

    AvailablePreds.emplace_back(UnavailablePred, NewVal);
  }

  // Now we know that each predecessor of this block has a value in
  // AvailablePreds, sort them for efficient access as we're walking the preds.
  array_pod_sort(AvailablePreds.begin(), AvailablePreds.end());

  // Create a PHI node at the start of the block for the PRE'd load value.
  PHINode *PN = PHINode::Create(LoadI->getType(), pred_size(LoadBB), "");
  PN->insertBefore(LoadBB->begin());
  PN->takeName(LoadI);
  PN->setDebugLoc(LoadI->getDebugLoc());

  // Insert new entries into the PHI for each predecessor.  A single block may
  // have multiple entries here.
  for (BasicBlock *P : predecessors(LoadBB)) {
    AvailablePredsTy::iterator I =
        llvm::lower_bound(AvailablePreds, std::make_pair(P, (Value *)nullptr));

    assert(I != AvailablePreds.end() && I->first == P &&
           "Didn't find entry for predecessor!");

    // If we have an available predecessor but it requires casting, insert the
    // cast in the predecessor and use the cast. Note that we have to update the
    // AvailablePreds vector as we go so that all of the PHI entries for this
    // predecessor use the same bitcast.
    Value *&PredV = I->second;
    if (PredV->getType() != LoadI->getType())
      PredV = CastInst::CreateBitOrPointerCast(
          PredV, LoadI->getType(), "", P->getTerminator()->getIterator());

    PN->addIncoming(PredV, I->first);
  }

  for (LoadInst *PredLoadI : CSELoads) {
    combineMetadataForCSE(PredLoadI, LoadI, true);
    LVI->forgetValue(PredLoadI);
  }

  LoadI->replaceAllUsesWith(PN);
  LoadI->eraseFromParent();

  return true;
}

/// findMostPopularDest - The specified list contains multiple possible
/// threadable destinations.  Pick the one that occurs the most frequently in
/// the list.
static BasicBlock *
findMostPopularDest(BasicBlock *BB,
                    const SmallVectorImpl<std::pair<BasicBlock *,
                                          BasicBlock *>> &PredToDestList) {
  assert(!PredToDestList.empty());

  // Determine popularity.  If there are multiple possible destinations, we
  // explicitly choose to ignore 'undef' destinations.  We prefer to thread
  // blocks with known and real destinations to threading undef.  We'll handle
  // them later if interesting.
  MapVector<BasicBlock *, unsigned> DestPopularity;

  // Populate DestPopularity with the successors in the order they appear in the
  // successor list.  This way, we ensure determinism by iterating it in the
  // same order in llvm::max_element below.  We map nullptr to 0 so that we can
  // return nullptr when PredToDestList contains nullptr only.
  DestPopularity[nullptr] = 0;
  for (auto *SuccBB : successors(BB))
    DestPopularity[SuccBB] = 0;

  for (const auto &PredToDest : PredToDestList)
    if (PredToDest.second)
      DestPopularity[PredToDest.second]++;

  // Find the most popular dest.
  auto MostPopular = llvm::max_element(DestPopularity, llvm::less_second());

  // Okay, we have finally picked the most popular destination.
  return MostPopular->first;
}

// Try to evaluate the value of V when the control flows from PredPredBB to
// BB->getSinglePredecessor() and then on to BB.
Constant *JumpThreadingPass::evaluateOnPredecessorEdge(BasicBlock *BB,
                                                       BasicBlock *PredPredBB,
                                                       Value *V,
                                                       const DataLayout &DL) {
  BasicBlock *PredBB = BB->getSinglePredecessor();
  assert(PredBB && "Expected a single predecessor");

  if (Constant *Cst = dyn_cast<Constant>(V)) {
    return Cst;
  }

  // Consult LVI if V is not an instruction in BB or PredBB.
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I || (I->getParent() != BB && I->getParent() != PredBB)) {
    return LVI->getConstantOnEdge(V, PredPredBB, PredBB, nullptr);
  }

  // Look into a PHI argument.
  if (PHINode *PHI = dyn_cast<PHINode>(V)) {
    if (PHI->getParent() == PredBB)
      return dyn_cast<Constant>(PHI->getIncomingValueForBlock(PredPredBB));
    return nullptr;
  }

  // If we have a CmpInst, try to fold it for each incoming edge into PredBB.
  if (CmpInst *CondCmp = dyn_cast<CmpInst>(V)) {
    if (CondCmp->getParent() == BB) {
      Constant *Op0 =
          evaluateOnPredecessorEdge(BB, PredPredBB, CondCmp->getOperand(0), DL);
      Constant *Op1 =
          evaluateOnPredecessorEdge(BB, PredPredBB, CondCmp->getOperand(1), DL);
      if (Op0 && Op1) {
        return ConstantFoldCompareInstOperands(CondCmp->getPredicate(), Op0,
                                               Op1, DL);
      }
    }
    return nullptr;
  }

  return nullptr;
}

bool JumpThreadingPass::processThreadableEdges(Value *Cond, BasicBlock *BB,
                                               ConstantPreference Preference,
                                               Instruction *CxtI) {
  // If threading this would thread across a loop header, don't even try to
  // thread the edge.
  if (LoopHeaders.count(BB))
    return false;

  PredValueInfoTy PredValues;
  if (!computeValueKnownInPredecessors(Cond, BB, PredValues, Preference,
                                       CxtI)) {
    // We don't have known values in predecessors.  See if we can thread through
    // BB and its sole predecessor.
    return maybethreadThroughTwoBasicBlocks(BB, Cond);
  }

  assert(!PredValues.empty() &&
         "computeValueKnownInPredecessors returned true with no values");

  LLVM_DEBUG(dbgs() << "IN BB: " << *BB;
             for (const auto &PredValue : PredValues) {
               dbgs() << "  BB '" << BB->getName()
                      << "': FOUND condition = " << *PredValue.first
                      << " for pred '" << PredValue.second->getName() << "'.\n";
  });

  // Decide what we want to thread through.  Convert our list of known values to
  // a list of known destinations for each pred.  This also discards duplicate
  // predecessors and keeps track of the undefined inputs (which are represented
  // as a null dest in the PredToDestList).
  SmallPtrSet<BasicBlock*, 16> SeenPreds;
  SmallVector<std::pair<BasicBlock*, BasicBlock*>, 16> PredToDestList;

  BasicBlock *OnlyDest = nullptr;
  BasicBlock *MultipleDestSentinel = (BasicBlock*)(intptr_t)~0ULL;
  Constant *OnlyVal = nullptr;
  Constant *MultipleVal = (Constant *)(intptr_t)~0ULL;

  for (const auto &PredValue : PredValues) {
    BasicBlock *Pred = PredValue.second;
    if (!SeenPreds.insert(Pred).second)
      continue;  // Duplicate predecessor entry.

    Constant *Val = PredValue.first;

    BasicBlock *DestBB;
    if (isa<UndefValue>(Val))
      DestBB = nullptr;
    else if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
      assert(isa<ConstantInt>(Val) && "Expecting a constant integer");
      DestBB = BI->getSuccessor(cast<ConstantInt>(Val)->isZero());
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(BB->getTerminator())) {
      assert(isa<ConstantInt>(Val) && "Expecting a constant integer");
      DestBB = SI->findCaseValue(cast<ConstantInt>(Val))->getCaseSuccessor();
    } else {
      assert(isa<IndirectBrInst>(BB->getTerminator())
              && "Unexpected terminator");
      assert(isa<BlockAddress>(Val) && "Expecting a constant blockaddress");
      DestBB = cast<BlockAddress>(Val)->getBasicBlock();
    }

    // If we have exactly one destination, remember it for efficiency below.
    if (PredToDestList.empty()) {
      OnlyDest = DestBB;
      OnlyVal = Val;
    } else {
      if (OnlyDest != DestBB)
        OnlyDest = MultipleDestSentinel;
      // It possible we have same destination, but different value, e.g. default
      // case in switchinst.
      if (Val != OnlyVal)
        OnlyVal = MultipleVal;
    }

    // If the predecessor ends with an indirect goto, we can't change its
    // destination.
    if (isa<IndirectBrInst>(Pred->getTerminator()))
      continue;

    PredToDestList.emplace_back(Pred, DestBB);
  }

  // If all edges were unthreadable, we fail.
  if (PredToDestList.empty())
    return false;

  // If all the predecessors go to a single known successor, we want to fold,
  // not thread. By doing so, we do not need to duplicate the current block and
  // also miss potential opportunities in case we dont/cant duplicate.
  if (OnlyDest && OnlyDest != MultipleDestSentinel) {
    if (BB->hasNPredecessors(PredToDestList.size())) {
      bool SeenFirstBranchToOnlyDest = false;
      std::vector <DominatorTree::UpdateType> Updates;
      Updates.reserve(BB->getTerminator()->getNumSuccessors() - 1);
      for (BasicBlock *SuccBB : successors(BB)) {
        if (SuccBB == OnlyDest && !SeenFirstBranchToOnlyDest) {
          SeenFirstBranchToOnlyDest = true; // Don't modify the first branch.
        } else {
          SuccBB->removePredecessor(BB, true); // This is unreachable successor.
          Updates.push_back({DominatorTree::Delete, BB, SuccBB});
        }
      }

      // Finally update the terminator.
      Instruction *Term = BB->getTerminator();
      Instruction *NewBI = BranchInst::Create(OnlyDest, Term->getIterator());
      NewBI->setDebugLoc(Term->getDebugLoc());
      ++NumFolds;
      Term->eraseFromParent();
      DTU->applyUpdatesPermissive(Updates);
      if (auto *BPI = getBPI())
        BPI->eraseBlock(BB);

      // If the condition is now dead due to the removal of the old terminator,
      // erase it.
      if (auto *CondInst = dyn_cast<Instruction>(Cond)) {
        if (CondInst->use_empty() && !CondInst->mayHaveSideEffects())
          CondInst->eraseFromParent();
        // We can safely replace *some* uses of the CondInst if it has
        // exactly one value as returned by LVI. RAUW is incorrect in the
        // presence of guards and assumes, that have the `Cond` as the use. This
        // is because we use the guards/assume to reason about the `Cond` value
        // at the end of block, but RAUW unconditionally replaces all uses
        // including the guards/assumes themselves and the uses before the
        // guard/assume.
        else if (OnlyVal && OnlyVal != MultipleVal)
          replaceFoldableUses(CondInst, OnlyVal, BB);
      }
      return true;
    }
  }

  // Determine which is the most common successor.  If we have many inputs and
  // this block is a switch, we want to start by threading the batch that goes
  // to the most popular destination first.  If we only know about one
  // threadable destination (the common case) we can avoid this.
  BasicBlock *MostPopularDest = OnlyDest;

  if (MostPopularDest == MultipleDestSentinel) {
    // Remove any loop headers from the Dest list, threadEdge conservatively
    // won't process them, but we might have other destination that are eligible
    // and we still want to process.
    erase_if(PredToDestList,
             [&](const std::pair<BasicBlock *, BasicBlock *> &PredToDest) {
               return LoopHeaders.contains(PredToDest.second);
             });

    if (PredToDestList.empty())
      return false;

    MostPopularDest = findMostPopularDest(BB, PredToDestList);
  }

  // Now that we know what the most popular destination is, factor all
  // predecessors that will jump to it into a single predecessor.
  SmallVector<BasicBlock*, 16> PredsToFactor;
  for (const auto &PredToDest : PredToDestList)
    if (PredToDest.second == MostPopularDest) {
      BasicBlock *Pred = PredToDest.first;

      // This predecessor may be a switch or something else that has multiple
      // edges to the block.  Factor each of these edges by listing them
      // according to # occurrences in PredsToFactor.
      for (BasicBlock *Succ : successors(Pred))
        if (Succ == BB)
          PredsToFactor.push_back(Pred);
    }

  // If the threadable edges are branching on an undefined value, we get to pick
  // the destination that these predecessors should get to.
  if (!MostPopularDest)
    MostPopularDest = BB->getTerminator()->
                            getSuccessor(getBestDestForJumpOnUndef(BB));

  // Ok, try to thread it!
  return tryThreadEdge(BB, PredsToFactor, MostPopularDest);
}

/// processBranchOnPHI - We have an otherwise unthreadable conditional branch on
/// a PHI node (or freeze PHI) in the current block.  See if there are any
/// simplifications we can do based on inputs to the phi node.
bool JumpThreadingPass::processBranchOnPHI(PHINode *PN) {
  BasicBlock *BB = PN->getParent();

  // TODO: We could make use of this to do it once for blocks with common PHI
  // values.
  SmallVector<BasicBlock*, 1> PredBBs;
  PredBBs.resize(1);

  // If any of the predecessor blocks end in an unconditional branch, we can
  // *duplicate* the conditional branch into that block in order to further
  // encourage jump threading and to eliminate cases where we have branch on a
  // phi of an icmp (branch on icmp is much better).
  // This is still beneficial when a frozen phi is used as the branch condition
  // because it allows CodeGenPrepare to further canonicalize br(freeze(icmp))
  // to br(icmp(freeze ...)).
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    BasicBlock *PredBB = PN->getIncomingBlock(i);
    if (BranchInst *PredBr = dyn_cast<BranchInst>(PredBB->getTerminator()))
      if (PredBr->isUnconditional()) {
        PredBBs[0] = PredBB;
        // Try to duplicate BB into PredBB.
        if (duplicateCondBranchOnPHIIntoPred(BB, PredBBs))
          return true;
      }
  }

  return false;
}

/// processBranchOnXOR - We have an otherwise unthreadable conditional branch on
/// a xor instruction in the current block.  See if there are any
/// simplifications we can do based on inputs to the xor.
bool JumpThreadingPass::processBranchOnXOR(BinaryOperator *BO) {
  BasicBlock *BB = BO->getParent();

  // If either the LHS or RHS of the xor is a constant, don't do this
  // optimization.
  if (isa<ConstantInt>(BO->getOperand(0)) ||
      isa<ConstantInt>(BO->getOperand(1)))
    return false;

  // If the first instruction in BB isn't a phi, we won't be able to infer
  // anything special about any particular predecessor.
  if (!isa<PHINode>(BB->front()))
    return false;

  // If this BB is a landing pad, we won't be able to split the edge into it.
  if (BB->isEHPad())
    return false;

  // If we have a xor as the branch input to this block, and we know that the
  // LHS or RHS of the xor in any predecessor is true/false, then we can clone
  // the condition into the predecessor and fix that value to true, saving some
  // logical ops on that path and encouraging other paths to simplify.
  //
  // This copies something like this:
  //
  //  BB:
  //    %X = phi i1 [1],  [%X']
  //    %Y = icmp eq i32 %A, %B
  //    %Z = xor i1 %X, %Y
  //    br i1 %Z, ...
  //
  // Into:
  //  BB':
  //    %Y = icmp ne i32 %A, %B
  //    br i1 %Y, ...

  PredValueInfoTy XorOpValues;
  bool isLHS = true;
  if (!computeValueKnownInPredecessors(BO->getOperand(0), BB, XorOpValues,
                                       WantInteger, BO)) {
    assert(XorOpValues.empty());
    if (!computeValueKnownInPredecessors(BO->getOperand(1), BB, XorOpValues,
                                         WantInteger, BO))
      return false;
    isLHS = false;
  }

  assert(!XorOpValues.empty() &&
         "computeValueKnownInPredecessors returned true with no values");

  // Scan the information to see which is most popular: true or false.  The
  // predecessors can be of the set true, false, or undef.
  unsigned NumTrue = 0, NumFalse = 0;
  for (const auto &XorOpValue : XorOpValues) {
    if (isa<UndefValue>(XorOpValue.first))
      // Ignore undefs for the count.
      continue;
    if (cast<ConstantInt>(XorOpValue.first)->isZero())
      ++NumFalse;
    else
      ++NumTrue;
  }

  // Determine which value to split on, true, false, or undef if neither.
  ConstantInt *SplitVal = nullptr;
  if (NumTrue > NumFalse)
    SplitVal = ConstantInt::getTrue(BB->getContext());
  else if (NumTrue != 0 || NumFalse != 0)
    SplitVal = ConstantInt::getFalse(BB->getContext());

  // Collect all of the blocks that this can be folded into so that we can
  // factor this once and clone it once.
  SmallVector<BasicBlock*, 8> BlocksToFoldInto;
  for (const auto &XorOpValue : XorOpValues) {
    if (XorOpValue.first != SplitVal && !isa<UndefValue>(XorOpValue.first))
      continue;

    BlocksToFoldInto.push_back(XorOpValue.second);
  }

  // If we inferred a value for all of the predecessors, then duplication won't
  // help us.  However, we can just replace the LHS or RHS with the constant.
  if (BlocksToFoldInto.size() ==
      cast<PHINode>(BB->front()).getNumIncomingValues()) {
    if (!SplitVal) {
      // If all preds provide undef, just nuke the xor, because it is undef too.
      BO->replaceAllUsesWith(UndefValue::get(BO->getType()));
      BO->eraseFromParent();
    } else if (SplitVal->isZero() && BO != BO->getOperand(isLHS)) {
      // If all preds provide 0, replace the xor with the other input.
      BO->replaceAllUsesWith(BO->getOperand(isLHS));
      BO->eraseFromParent();
    } else {
      // If all preds provide 1, set the computed value to 1.
      BO->setOperand(!isLHS, SplitVal);
    }

    return true;
  }

  // If any of predecessors end with an indirect goto, we can't change its
  // destination.
  if (any_of(BlocksToFoldInto, [](BasicBlock *Pred) {
        return isa<IndirectBrInst>(Pred->getTerminator());
      }))
    return false;

  // Try to duplicate BB into PredBB.
  return duplicateCondBranchOnPHIIntoPred(BB, BlocksToFoldInto);
}

/// addPHINodeEntriesForMappedBlock - We're adding 'NewPred' as a new
/// predecessor to the PHIBB block.  If it has PHI nodes, add entries for
/// NewPred using the entries from OldPred (suitably mapped).
static void addPHINodeEntriesForMappedBlock(BasicBlock *PHIBB,
                                            BasicBlock *OldPred,
                                            BasicBlock *NewPred,
                                            ValueToValueMapTy &ValueMap) {
  for (PHINode &PN : PHIBB->phis()) {
    // Ok, we have a PHI node.  Figure out what the incoming value was for the
    // DestBlock.
    Value *IV = PN.getIncomingValueForBlock(OldPred);

    // Remap the value if necessary.
    if (Instruction *Inst = dyn_cast<Instruction>(IV)) {
      ValueToValueMapTy::iterator I = ValueMap.find(Inst);
      if (I != ValueMap.end())
        IV = I->second;
    }

    PN.addIncoming(IV, NewPred);
  }
}

/// Merge basic block BB into its sole predecessor if possible.
bool JumpThreadingPass::maybeMergeBasicBlockIntoOnlyPred(BasicBlock *BB) {
  BasicBlock *SinglePred = BB->getSinglePredecessor();
  if (!SinglePred)
    return false;

  const Instruction *TI = SinglePred->getTerminator();
  if (TI->isSpecialTerminator() || TI->getNumSuccessors() != 1 ||
      SinglePred == BB || hasAddressTakenAndUsed(BB))
    return false;

  // If SinglePred was a loop header, BB becomes one.
  if (LoopHeaders.erase(SinglePred))
    LoopHeaders.insert(BB);

  LVI->eraseBlock(SinglePred);
  MergeBasicBlockIntoOnlyPred(BB, DTU.get());

  // Now that BB is merged into SinglePred (i.e. SinglePred code followed by
  // BB code within one basic block `BB`), we need to invalidate the LVI
  // information associated with BB, because the LVI information need not be
  // true for all of BB after the merge. For example,
  // Before the merge, LVI info and code is as follows:
  // SinglePred: <LVI info1 for %p val>
  // %y = use of %p
  // call @exit() // need not transfer execution to successor.
  // assume(%p) // from this point on %p is true
  // br label %BB
  // BB: <LVI info2 for %p val, i.e. %p is true>
  // %x = use of %p
  // br label exit
  //
  // Note that this LVI info for blocks BB and SinglPred is correct for %p
  // (info2 and info1 respectively). After the merge and the deletion of the
  // LVI info1 for SinglePred. We have the following code:
  // BB: <LVI info2 for %p val>
  // %y = use of %p
  // call @exit()
  // assume(%p)
  // %x = use of %p <-- LVI info2 is correct from here onwards.
  // br label exit
  // LVI info2 for BB is incorrect at the beginning of BB.

  // Invalidate LVI information for BB if the LVI is not provably true for
  // all of BB.
  if (!isGuaranteedToTransferExecutionToSuccessor(BB))
    LVI->eraseBlock(BB);
  return true;
}

/// Update the SSA form.  NewBB contains instructions that are copied from BB.
/// ValueMapping maps old values in BB to new ones in NewBB.
void JumpThreadingPass::updateSSA(BasicBlock *BB, BasicBlock *NewBB,
                                  ValueToValueMapTy &ValueMapping) {
  // If there were values defined in BB that are used outside the block, then we
  // now have to update all uses of the value to use either the original value,
  // the cloned value, or some PHI derived value.  This can require arbitrary
  // PHI insertion, of which we are prepared to do, clean these up now.
  SSAUpdater SSAUpdate;
  SmallVector<Use *, 16> UsesToRename;
  SmallVector<DbgValueInst *, 4> DbgValues;
  SmallVector<DbgVariableRecord *, 4> DbgVariableRecords;

  for (Instruction &I : *BB) {
    // Scan all uses of this instruction to see if it is used outside of its
    // block, and if so, record them in UsesToRename.
    for (Use &U : I.uses()) {
      Instruction *User = cast<Instruction>(U.getUser());
      if (PHINode *UserPN = dyn_cast<PHINode>(User)) {
        if (UserPN->getIncomingBlock(U) == BB)
          continue;
      } else if (User->getParent() == BB)
        continue;

      UsesToRename.push_back(&U);
    }

    // Find debug values outside of the block
    findDbgValues(DbgValues, &I, &DbgVariableRecords);
    llvm::erase_if(DbgValues, [&](const DbgValueInst *DbgVal) {
      return DbgVal->getParent() == BB;
    });
    llvm::erase_if(DbgVariableRecords, [&](const DbgVariableRecord *DbgVarRec) {
      return DbgVarRec->getParent() == BB;
    });

    // If there are no uses outside the block, we're done with this instruction.
    if (UsesToRename.empty() && DbgValues.empty() && DbgVariableRecords.empty())
      continue;
    LLVM_DEBUG(dbgs() << "JT: Renaming non-local uses of: " << I << "\n");

    // We found a use of I outside of BB.  Rename all uses of I that are outside
    // its block to be uses of the appropriate PHI node etc.  See ValuesInBlocks
    // with the two values we know.
    SSAUpdate.Initialize(I.getType(), I.getName());
    SSAUpdate.AddAvailableValue(BB, &I);
    SSAUpdate.AddAvailableValue(NewBB, ValueMapping[&I]);

    while (!UsesToRename.empty())
      SSAUpdate.RewriteUse(*UsesToRename.pop_back_val());
    if (!DbgValues.empty() || !DbgVariableRecords.empty()) {
      SSAUpdate.UpdateDebugValues(&I, DbgValues);
      SSAUpdate.UpdateDebugValues(&I, DbgVariableRecords);
      DbgValues.clear();
      DbgVariableRecords.clear();
    }

    LLVM_DEBUG(dbgs() << "\n");
  }
}

/// Clone instructions in range [BI, BE) to NewBB.  For PHI nodes, we only clone
/// arguments that come from PredBB.  Return the map from the variables in the
/// source basic block to the variables in the newly created basic block.

void JumpThreadingPass::cloneInstructions(ValueToValueMapTy &ValueMapping,
                                          BasicBlock::iterator BI,
                                          BasicBlock::iterator BE,
                                          BasicBlock *NewBB,
                                          BasicBlock *PredBB) {
  // We are going to have to map operands from the source basic block to the new
  // copy of the block 'NewBB'.  If there are PHI nodes in the source basic
  // block, evaluate them to account for entry from PredBB.

  // Retargets llvm.dbg.value to any renamed variables.
  auto RetargetDbgValueIfPossible = [&](Instruction *NewInst) -> bool {
    auto DbgInstruction = dyn_cast<DbgValueInst>(NewInst);
    if (!DbgInstruction)
      return false;

    SmallSet<std::pair<Value *, Value *>, 16> OperandsToRemap;
    for (auto DbgOperand : DbgInstruction->location_ops()) {
      auto DbgOperandInstruction = dyn_cast<Instruction>(DbgOperand);
      if (!DbgOperandInstruction)
        continue;

      auto I = ValueMapping.find(DbgOperandInstruction);
      if (I != ValueMapping.end()) {
        OperandsToRemap.insert(
            std::pair<Value *, Value *>(DbgOperand, I->second));
      }
    }

    for (auto &[OldOp, MappedOp] : OperandsToRemap)
      DbgInstruction->replaceVariableLocationOp(OldOp, MappedOp);
    return true;
  };

  // Duplicate implementation of the above dbg.value code, using
  // DbgVariableRecords instead.
  auto RetargetDbgVariableRecordIfPossible = [&](DbgVariableRecord *DVR) {
    SmallSet<std::pair<Value *, Value *>, 16> OperandsToRemap;
    for (auto *Op : DVR->location_ops()) {
      Instruction *OpInst = dyn_cast<Instruction>(Op);
      if (!OpInst)
        continue;

      auto I = ValueMapping.find(OpInst);
      if (I != ValueMapping.end())
        OperandsToRemap.insert({OpInst, I->second});
    }

    for (auto &[OldOp, MappedOp] : OperandsToRemap)
      DVR->replaceVariableLocationOp(OldOp, MappedOp);
  };

  BasicBlock *RangeBB = BI->getParent();

  // Clone the phi nodes of the source basic block into NewBB.  The resulting
  // phi nodes are trivial since NewBB only has one predecessor, but SSAUpdater
  // might need to rewrite the operand of the cloned phi.
  for (; PHINode *PN = dyn_cast<PHINode>(BI); ++BI) {
    PHINode *NewPN = PHINode::Create(PN->getType(), 1, PN->getName(), NewBB);
    NewPN->addIncoming(PN->getIncomingValueForBlock(PredBB), PredBB);
    ValueMapping[PN] = NewPN;
  }

  // Clone noalias scope declarations in the threaded block. When threading a
  // loop exit, we would otherwise end up with two idential scope declarations
  // visible at the same time.
  SmallVector<MDNode *> NoAliasScopes;
  DenseMap<MDNode *, MDNode *> ClonedScopes;
  LLVMContext &Context = PredBB->getContext();
  identifyNoAliasScopesToClone(BI, BE, NoAliasScopes);
  cloneNoAliasScopes(NoAliasScopes, ClonedScopes, "thread", Context);

  auto CloneAndRemapDbgInfo = [&](Instruction *NewInst, Instruction *From) {
    auto DVRRange = NewInst->cloneDebugInfoFrom(From);
    for (DbgVariableRecord &DVR : filterDbgVars(DVRRange))
      RetargetDbgVariableRecordIfPossible(&DVR);
  };

  // Clone the non-phi instructions of the source basic block into NewBB,
  // keeping track of the mapping and using it to remap operands in the cloned
  // instructions.
  for (; BI != BE; ++BI) {
    Instruction *New = BI->clone();
    New->setName(BI->getName());
    New->insertInto(NewBB, NewBB->end());
    ValueMapping[&*BI] = New;
    adaptNoAliasScopes(New, ClonedScopes, Context);

    CloneAndRemapDbgInfo(New, &*BI);

    if (RetargetDbgValueIfPossible(New))
      continue;

    // Remap operands to patch up intra-block references.
    for (unsigned i = 0, e = New->getNumOperands(); i != e; ++i)
      if (Instruction *Inst = dyn_cast<Instruction>(New->getOperand(i))) {
        ValueToValueMapTy::iterator I = ValueMapping.find(Inst);
        if (I != ValueMapping.end())
          New->setOperand(i, I->second);
      }
  }

  // There may be DbgVariableRecords on the terminator, clone directly from
  // marker to marker as there isn't an instruction there.
  if (BE != RangeBB->end() && BE->hasDbgRecords()) {
    // Dump them at the end.
    DbgMarker *Marker = RangeBB->getMarker(BE);
    DbgMarker *EndMarker = NewBB->createMarker(NewBB->end());
    auto DVRRange = EndMarker->cloneDebugInfoFrom(Marker, std::nullopt);
    for (DbgVariableRecord &DVR : filterDbgVars(DVRRange))
      RetargetDbgVariableRecordIfPossible(&DVR);
  }

  return;
}

/// Attempt to thread through two successive basic blocks.
bool JumpThreadingPass::maybethreadThroughTwoBasicBlocks(BasicBlock *BB,
                                                         Value *Cond) {
  // Consider:
  //
  // PredBB:
  //   %var = phi i32* [ null, %bb1 ], [ @a, %bb2 ]
  //   %tobool = icmp eq i32 %cond, 0
  //   br i1 %tobool, label %BB, label ...
  //
  // BB:
  //   %cmp = icmp eq i32* %var, null
  //   br i1 %cmp, label ..., label ...
  //
  // We don't know the value of %var at BB even if we know which incoming edge
  // we take to BB.  However, once we duplicate PredBB for each of its incoming
  // edges (say, PredBB1 and PredBB2), we know the value of %var in each copy of
  // PredBB.  Then we can thread edges PredBB1->BB and PredBB2->BB through BB.

  // Require that BB end with a Branch for simplicity.
  BranchInst *CondBr = dyn_cast<BranchInst>(BB->getTerminator());
  if (!CondBr)
    return false;

  // BB must have exactly one predecessor.
  BasicBlock *PredBB = BB->getSinglePredecessor();
  if (!PredBB)
    return false;

  // Require that PredBB end with a conditional Branch. If PredBB ends with an
  // unconditional branch, we should be merging PredBB and BB instead. For
  // simplicity, we don't deal with a switch.
  BranchInst *PredBBBranch = dyn_cast<BranchInst>(PredBB->getTerminator());
  if (!PredBBBranch || PredBBBranch->isUnconditional())
    return false;

  // If PredBB has exactly one incoming edge, we don't gain anything by copying
  // PredBB.
  if (PredBB->getSinglePredecessor())
    return false;

  // Don't thread through PredBB if it contains a successor edge to itself, in
  // which case we would infinite loop.  Suppose we are threading an edge from
  // PredPredBB through PredBB and BB to SuccBB with PredBB containing a
  // successor edge to itself.  If we allowed jump threading in this case, we
  // could duplicate PredBB and BB as, say, PredBB.thread and BB.thread.  Since
  // PredBB.thread has a successor edge to PredBB, we would immediately come up
  // with another jump threading opportunity from PredBB.thread through PredBB
  // and BB to SuccBB.  This jump threading would repeatedly occur.  That is, we
  // would keep peeling one iteration from PredBB.
  if (llvm::is_contained(successors(PredBB), PredBB))
    return false;

  // Don't thread across a loop header.
  if (LoopHeaders.count(PredBB))
    return false;

  // Avoid complication with duplicating EH pads.
  if (PredBB->isEHPad())
    return false;

  // Find a predecessor that we can thread.  For simplicity, we only consider a
  // successor edge out of BB to which we thread exactly one incoming edge into
  // PredBB.
  unsigned ZeroCount = 0;
  unsigned OneCount = 0;
  BasicBlock *ZeroPred = nullptr;
  BasicBlock *OnePred = nullptr;
  const DataLayout &DL = BB->getDataLayout();
  for (BasicBlock *P : predecessors(PredBB)) {
    // If PredPred ends with IndirectBrInst, we can't handle it.
    if (isa<IndirectBrInst>(P->getTerminator()))
      continue;
    if (ConstantInt *CI = dyn_cast_or_null<ConstantInt>(
            evaluateOnPredecessorEdge(BB, P, Cond, DL))) {
      if (CI->isZero()) {
        ZeroCount++;
        ZeroPred = P;
      } else if (CI->isOne()) {
        OneCount++;
        OnePred = P;
      }
    }
  }

  // Disregard complicated cases where we have to thread multiple edges.
  BasicBlock *PredPredBB;
  if (ZeroCount == 1) {
    PredPredBB = ZeroPred;
  } else if (OneCount == 1) {
    PredPredBB = OnePred;
  } else {
    return false;
  }

  BasicBlock *SuccBB = CondBr->getSuccessor(PredPredBB == ZeroPred);

  // If threading to the same block as we come from, we would infinite loop.
  if (SuccBB == BB) {
    LLVM_DEBUG(dbgs() << "  Not threading across BB '" << BB->getName()
                      << "' - would thread to self!\n");
    return false;
  }

  // If threading this would thread across a loop header, don't thread the edge.
  // See the comments above findLoopHeaders for justifications and caveats.
  if (LoopHeaders.count(BB) || LoopHeaders.count(SuccBB)) {
    LLVM_DEBUG({
      bool BBIsHeader = LoopHeaders.count(BB);
      bool SuccIsHeader = LoopHeaders.count(SuccBB);
      dbgs() << "  Not threading across "
             << (BBIsHeader ? "loop header BB '" : "block BB '")
             << BB->getName() << "' to dest "
             << (SuccIsHeader ? "loop header BB '" : "block BB '")
             << SuccBB->getName()
             << "' - it might create an irreducible loop!\n";
    });
    return false;
  }

  // Compute the cost of duplicating BB and PredBB.
  unsigned BBCost = getJumpThreadDuplicationCost(
      TTI, BB, BB->getTerminator(), BBDupThreshold);
  unsigned PredBBCost = getJumpThreadDuplicationCost(
      TTI, PredBB, PredBB->getTerminator(), BBDupThreshold);

  // Give up if costs are too high.  We need to check BBCost and PredBBCost
  // individually before checking their sum because getJumpThreadDuplicationCost
  // return (unsigned)~0 for those basic blocks that cannot be duplicated.
  if (BBCost > BBDupThreshold || PredBBCost > BBDupThreshold ||
      BBCost + PredBBCost > BBDupThreshold) {
    LLVM_DEBUG(dbgs() << "  Not threading BB '" << BB->getName()
                      << "' - Cost is too high: " << PredBBCost
                      << " for PredBB, " << BBCost << "for BB\n");
    return false;
  }

  // Now we are ready to duplicate PredBB.
  threadThroughTwoBasicBlocks(PredPredBB, PredBB, BB, SuccBB);
  return true;
}

void JumpThreadingPass::threadThroughTwoBasicBlocks(BasicBlock *PredPredBB,
                                                    BasicBlock *PredBB,
                                                    BasicBlock *BB,
                                                    BasicBlock *SuccBB) {
  LLVM_DEBUG(dbgs() << "  Threading through '" << PredBB->getName() << "' and '"
                    << BB->getName() << "'\n");

  // Build BPI/BFI before any changes are made to IR.
  bool HasProfile = doesBlockHaveProfileData(BB);
  auto *BFI = getOrCreateBFI(HasProfile);
  auto *BPI = getOrCreateBPI(BFI != nullptr);

  BranchInst *CondBr = cast<BranchInst>(BB->getTerminator());
  BranchInst *PredBBBranch = cast<BranchInst>(PredBB->getTerminator());

  BasicBlock *NewBB =
      BasicBlock::Create(PredBB->getContext(), PredBB->getName() + ".thread",
                         PredBB->getParent(), PredBB);
  NewBB->moveAfter(PredBB);

  // Set the block frequency of NewBB.
  if (BFI) {
    assert(BPI && "It's expected BPI to exist along with BFI");
    auto NewBBFreq = BFI->getBlockFreq(PredPredBB) *
                     BPI->getEdgeProbability(PredPredBB, PredBB);
    BFI->setBlockFreq(NewBB, NewBBFreq);
  }

  // We are going to have to map operands from the original BB block to the new
  // copy of the block 'NewBB'.  If there are PHI nodes in PredBB, evaluate them
  // to account for entry from PredPredBB.
  ValueToValueMapTy ValueMapping;
  cloneInstructions(ValueMapping, PredBB->begin(), PredBB->end(), NewBB,
                    PredPredBB);

  // Copy the edge probabilities from PredBB to NewBB.
  if (BPI)
    BPI->copyEdgeProbabilities(PredBB, NewBB);

  // Update the terminator of PredPredBB to jump to NewBB instead of PredBB.
  // This eliminates predecessors from PredPredBB, which requires us to simplify
  // any PHI nodes in PredBB.
  Instruction *PredPredTerm = PredPredBB->getTerminator();
  for (unsigned i = 0, e = PredPredTerm->getNumSuccessors(); i != e; ++i)
    if (PredPredTerm->getSuccessor(i) == PredBB) {
      PredBB->removePredecessor(PredPredBB, true);
      PredPredTerm->setSuccessor(i, NewBB);
    }

  addPHINodeEntriesForMappedBlock(PredBBBranch->getSuccessor(0), PredBB, NewBB,
                                  ValueMapping);
  addPHINodeEntriesForMappedBlock(PredBBBranch->getSuccessor(1), PredBB, NewBB,
                                  ValueMapping);

  DTU->applyUpdatesPermissive(
      {{DominatorTree::Insert, NewBB, CondBr->getSuccessor(0)},
       {DominatorTree::Insert, NewBB, CondBr->getSuccessor(1)},
       {DominatorTree::Insert, PredPredBB, NewBB},
       {DominatorTree::Delete, PredPredBB, PredBB}});

  updateSSA(PredBB, NewBB, ValueMapping);

  // Clean up things like PHI nodes with single operands, dead instructions,
  // etc.
  SimplifyInstructionsInBlock(NewBB, TLI);
  SimplifyInstructionsInBlock(PredBB, TLI);

  SmallVector<BasicBlock *, 1> PredsToFactor;
  PredsToFactor.push_back(NewBB);
  threadEdge(BB, PredsToFactor, SuccBB);
}

/// tryThreadEdge - Thread an edge if it's safe and profitable to do so.
bool JumpThreadingPass::tryThreadEdge(
    BasicBlock *BB, const SmallVectorImpl<BasicBlock *> &PredBBs,
    BasicBlock *SuccBB) {
  // If threading to the same block as we come from, we would infinite loop.
  if (SuccBB == BB) {
    LLVM_DEBUG(dbgs() << "  Not threading across BB '" << BB->getName()
                      << "' - would thread to self!\n");
    return false;
  }

  // If threading this would thread across a loop header, don't thread the edge.
  // See the comments above findLoopHeaders for justifications and caveats.
  if (LoopHeaders.count(BB) || LoopHeaders.count(SuccBB)) {
    LLVM_DEBUG({
      bool BBIsHeader = LoopHeaders.count(BB);
      bool SuccIsHeader = LoopHeaders.count(SuccBB);
      dbgs() << "  Not threading across "
          << (BBIsHeader ? "loop header BB '" : "block BB '") << BB->getName()
          << "' to dest " << (SuccIsHeader ? "loop header BB '" : "block BB '")
          << SuccBB->getName() << "' - it might create an irreducible loop!\n";
    });
    return false;
  }

  unsigned JumpThreadCost = getJumpThreadDuplicationCost(
      TTI, BB, BB->getTerminator(), BBDupThreshold);
  if (JumpThreadCost > BBDupThreshold) {
    LLVM_DEBUG(dbgs() << "  Not threading BB '" << BB->getName()
                      << "' - Cost is too high: " << JumpThreadCost << "\n");
    return false;
  }

  threadEdge(BB, PredBBs, SuccBB);
  return true;
}

/// threadEdge - We have decided that it is safe and profitable to factor the
/// blocks in PredBBs to one predecessor, then thread an edge from it to SuccBB
/// across BB.  Transform the IR to reflect this change.
void JumpThreadingPass::threadEdge(BasicBlock *BB,
                                   const SmallVectorImpl<BasicBlock *> &PredBBs,
                                   BasicBlock *SuccBB) {
  assert(SuccBB != BB && "Don't create an infinite loop");

  assert(!LoopHeaders.count(BB) && !LoopHeaders.count(SuccBB) &&
         "Don't thread across loop headers");

  // Build BPI/BFI before any changes are made to IR.
  bool HasProfile = doesBlockHaveProfileData(BB);
  auto *BFI = getOrCreateBFI(HasProfile);
  auto *BPI = getOrCreateBPI(BFI != nullptr);

  // And finally, do it!  Start by factoring the predecessors if needed.
  BasicBlock *PredBB;
  if (PredBBs.size() == 1)
    PredBB = PredBBs[0];
  else {
    LLVM_DEBUG(dbgs() << "  Factoring out " << PredBBs.size()
                      << " common predecessors.\n");
    PredBB = splitBlockPreds(BB, PredBBs, ".thr_comm");
  }

  // And finally, do it!
  LLVM_DEBUG(dbgs() << "  Threading edge from '" << PredBB->getName()
                    << "' to '" << SuccBB->getName()
                    << ", across block:\n    " << *BB << "\n");

  LVI->threadEdge(PredBB, BB, SuccBB);

  BasicBlock *NewBB = BasicBlock::Create(BB->getContext(),
                                         BB->getName()+".thread",
                                         BB->getParent(), BB);
  NewBB->moveAfter(PredBB);

  // Set the block frequency of NewBB.
  if (BFI) {
    assert(BPI && "It's expected BPI to exist along with BFI");
    auto NewBBFreq =
        BFI->getBlockFreq(PredBB) * BPI->getEdgeProbability(PredBB, BB);
    BFI->setBlockFreq(NewBB, NewBBFreq);
  }

  // Copy all the instructions from BB to NewBB except the terminator.
  ValueToValueMapTy ValueMapping;
  cloneInstructions(ValueMapping, BB->begin(), std::prev(BB->end()), NewBB,
                    PredBB);

  // We didn't copy the terminator from BB over to NewBB, because there is now
  // an unconditional jump to SuccBB.  Insert the unconditional jump.
  BranchInst *NewBI = BranchInst::Create(SuccBB, NewBB);
  NewBI->setDebugLoc(BB->getTerminator()->getDebugLoc());

  // Check to see if SuccBB has PHI nodes. If so, we need to add entries to the
  // PHI nodes for NewBB now.
  addPHINodeEntriesForMappedBlock(SuccBB, BB, NewBB, ValueMapping);

  // Update the terminator of PredBB to jump to NewBB instead of BB.  This
  // eliminates predecessors from BB, which requires us to simplify any PHI
  // nodes in BB.
  Instruction *PredTerm = PredBB->getTerminator();
  for (unsigned i = 0, e = PredTerm->getNumSuccessors(); i != e; ++i)
    if (PredTerm->getSuccessor(i) == BB) {
      BB->removePredecessor(PredBB, true);
      PredTerm->setSuccessor(i, NewBB);
    }

  // Enqueue required DT updates.
  DTU->applyUpdatesPermissive({{DominatorTree::Insert, NewBB, SuccBB},
                               {DominatorTree::Insert, PredBB, NewBB},
                               {DominatorTree::Delete, PredBB, BB}});

  updateSSA(BB, NewBB, ValueMapping);

  // At this point, the IR is fully up to date and consistent.  Do a quick scan
  // over the new instructions and zap any that are constants or dead.  This
  // frequently happens because of phi translation.
  SimplifyInstructionsInBlock(NewBB, TLI);

  // Update the edge weight from BB to SuccBB, which should be less than before.
  updateBlockFreqAndEdgeWeight(PredBB, BB, NewBB, SuccBB, BFI, BPI, HasProfile);

  // Threaded an edge!
  ++NumThreads;
}

/// Create a new basic block that will be the predecessor of BB and successor of
/// all blocks in Preds. When profile data is available, update the frequency of
/// this new block.
BasicBlock *JumpThreadingPass::splitBlockPreds(BasicBlock *BB,
                                               ArrayRef<BasicBlock *> Preds,
                                               const char *Suffix) {
  SmallVector<BasicBlock *, 2> NewBBs;

  // Collect the frequencies of all predecessors of BB, which will be used to
  // update the edge weight of the result of splitting predecessors.
  DenseMap<BasicBlock *, BlockFrequency> FreqMap;
  auto *BFI = getBFI();
  if (BFI) {
    auto *BPI = getOrCreateBPI(true);
    for (auto *Pred : Preds)
      FreqMap.insert(std::make_pair(
          Pred, BFI->getBlockFreq(Pred) * BPI->getEdgeProbability(Pred, BB)));
  }

  // In the case when BB is a LandingPad block we create 2 new predecessors
  // instead of just one.
  if (BB->isLandingPad()) {
    std::string NewName = std::string(Suffix) + ".split-lp";
    SplitLandingPadPredecessors(BB, Preds, Suffix, NewName.c_str(), NewBBs);
  } else {
    NewBBs.push_back(SplitBlockPredecessors(BB, Preds, Suffix));
  }

  std::vector<DominatorTree::UpdateType> Updates;
  Updates.reserve((2 * Preds.size()) + NewBBs.size());
  for (auto *NewBB : NewBBs) {
    BlockFrequency NewBBFreq(0);
    Updates.push_back({DominatorTree::Insert, NewBB, BB});
    for (auto *Pred : predecessors(NewBB)) {
      Updates.push_back({DominatorTree::Delete, Pred, BB});
      Updates.push_back({DominatorTree::Insert, Pred, NewBB});
      if (BFI) // Update frequencies between Pred -> NewBB.
        NewBBFreq += FreqMap.lookup(Pred);
    }
    if (BFI) // Apply the summed frequency to NewBB.
      BFI->setBlockFreq(NewBB, NewBBFreq);
  }

  DTU->applyUpdatesPermissive(Updates);
  return NewBBs[0];
}

bool JumpThreadingPass::doesBlockHaveProfileData(BasicBlock *BB) {
  const Instruction *TI = BB->getTerminator();
  if (!TI || TI->getNumSuccessors() < 2)
    return false;

  return hasValidBranchWeightMD(*TI);
}

/// Update the block frequency of BB and branch weight and the metadata on the
/// edge BB->SuccBB. This is done by scaling the weight of BB->SuccBB by 1 -
/// Freq(PredBB->BB) / Freq(BB->SuccBB).
void JumpThreadingPass::updateBlockFreqAndEdgeWeight(BasicBlock *PredBB,
                                                     BasicBlock *BB,
                                                     BasicBlock *NewBB,
                                                     BasicBlock *SuccBB,
                                                     BlockFrequencyInfo *BFI,
                                                     BranchProbabilityInfo *BPI,
                                                     bool HasProfile) {
  assert(((BFI && BPI) || (!BFI && !BFI)) &&
         "Both BFI & BPI should either be set or unset");

  if (!BFI) {
    assert(!HasProfile &&
           "It's expected to have BFI/BPI when profile info exists");
    return;
  }

  // As the edge from PredBB to BB is deleted, we have to update the block
  // frequency of BB.
  auto BBOrigFreq = BFI->getBlockFreq(BB);
  auto NewBBFreq = BFI->getBlockFreq(NewBB);
  auto BB2SuccBBFreq = BBOrigFreq * BPI->getEdgeProbability(BB, SuccBB);
  auto BBNewFreq = BBOrigFreq - NewBBFreq;
  BFI->setBlockFreq(BB, BBNewFreq);

  // Collect updated outgoing edges' frequencies from BB and use them to update
  // edge probabilities.
  SmallVector<uint64_t, 4> BBSuccFreq;
  for (BasicBlock *Succ : successors(BB)) {
    auto SuccFreq = (Succ == SuccBB)
                        ? BB2SuccBBFreq - NewBBFreq
                        : BBOrigFreq * BPI->getEdgeProbability(BB, Succ);
    BBSuccFreq.push_back(SuccFreq.getFrequency());
  }

  uint64_t MaxBBSuccFreq = *llvm::max_element(BBSuccFreq);

  SmallVector<BranchProbability, 4> BBSuccProbs;
  if (MaxBBSuccFreq == 0)
    BBSuccProbs.assign(BBSuccFreq.size(),
                       {1, static_cast<unsigned>(BBSuccFreq.size())});
  else {
    for (uint64_t Freq : BBSuccFreq)
      BBSuccProbs.push_back(
          BranchProbability::getBranchProbability(Freq, MaxBBSuccFreq));
    // Normalize edge probabilities so that they sum up to one.
    BranchProbability::normalizeProbabilities(BBSuccProbs.begin(),
                                              BBSuccProbs.end());
  }

  // Update edge probabilities in BPI.
  BPI->setEdgeProbability(BB, BBSuccProbs);

  // Update the profile metadata as well.
  //
  // Don't do this if the profile of the transformed blocks was statically
  // estimated.  (This could occur despite the function having an entry
  // frequency in completely cold parts of the CFG.)
  //
  // In this case we don't want to suggest to subsequent passes that the
  // calculated weights are fully consistent.  Consider this graph:
  //
  //                 check_1
  //             50% /  |
  //             eq_1   | 50%
  //                 \  |
  //                 check_2
  //             50% /  |
  //             eq_2   | 50%
  //                 \  |
  //                 check_3
  //             50% /  |
  //             eq_3   | 50%
  //                 \  |
  //
  // Assuming the blocks check_* all compare the same value against 1, 2 and 3,
  // the overall probabilities are inconsistent; the total probability that the
  // value is either 1, 2 or 3 is 150%.
  //
  // As a consequence if we thread eq_1 -> check_2 to check_3, check_2->check_3
  // becomes 0%.  This is even worse if the edge whose probability becomes 0% is
  // the loop exit edge.  Then based solely on static estimation we would assume
  // the loop was extremely hot.
  //
  // FIXME this locally as well so that BPI and BFI are consistent as well.  We
  // shouldn't make edges extremely likely or unlikely based solely on static
  // estimation.
  if (BBSuccProbs.size() >= 2 && HasProfile) {
    SmallVector<uint32_t, 4> Weights;
    for (auto Prob : BBSuccProbs)
      Weights.push_back(Prob.getNumerator());

    auto TI = BB->getTerminator();
    setBranchWeights(*TI, Weights, hasBranchWeightOrigin(*TI));
  }
}

/// duplicateCondBranchOnPHIIntoPred - PredBB contains an unconditional branch
/// to BB which contains an i1 PHI node and a conditional branch on that PHI.
/// If we can duplicate the contents of BB up into PredBB do so now, this
/// improves the odds that the branch will be on an analyzable instruction like
/// a compare.
bool JumpThreadingPass::duplicateCondBranchOnPHIIntoPred(
    BasicBlock *BB, const SmallVectorImpl<BasicBlock *> &PredBBs) {
  assert(!PredBBs.empty() && "Can't handle an empty set");

  // If BB is a loop header, then duplicating this block outside the loop would
  // cause us to transform this into an irreducible loop, don't do this.
  // See the comments above findLoopHeaders for justifications and caveats.
  if (LoopHeaders.count(BB)) {
    LLVM_DEBUG(dbgs() << "  Not duplicating loop header '" << BB->getName()
                      << "' into predecessor block '" << PredBBs[0]->getName()
                      << "' - it might create an irreducible loop!\n");
    return false;
  }

  unsigned DuplicationCost = getJumpThreadDuplicationCost(
      TTI, BB, BB->getTerminator(), BBDupThreshold);
  if (DuplicationCost > BBDupThreshold) {
    LLVM_DEBUG(dbgs() << "  Not duplicating BB '" << BB->getName()
                      << "' - Cost is too high: " << DuplicationCost << "\n");
    return false;
  }

  // And finally, do it!  Start by factoring the predecessors if needed.
  std::vector<DominatorTree::UpdateType> Updates;
  BasicBlock *PredBB;
  if (PredBBs.size() == 1)
    PredBB = PredBBs[0];
  else {
    LLVM_DEBUG(dbgs() << "  Factoring out " << PredBBs.size()
                      << " common predecessors.\n");
    PredBB = splitBlockPreds(BB, PredBBs, ".thr_comm");
  }
  Updates.push_back({DominatorTree::Delete, PredBB, BB});

  // Okay, we decided to do this!  Clone all the instructions in BB onto the end
  // of PredBB.
  LLVM_DEBUG(dbgs() << "  Duplicating block '" << BB->getName()
                    << "' into end of '" << PredBB->getName()
                    << "' to eliminate branch on phi.  Cost: "
                    << DuplicationCost << " block is:" << *BB << "\n");

  // Unless PredBB ends with an unconditional branch, split the edge so that we
  // can just clone the bits from BB into the end of the new PredBB.
  BranchInst *OldPredBranch = dyn_cast<BranchInst>(PredBB->getTerminator());

  if (!OldPredBranch || !OldPredBranch->isUnconditional()) {
    BasicBlock *OldPredBB = PredBB;
    PredBB = SplitEdge(OldPredBB, BB);
    Updates.push_back({DominatorTree::Insert, OldPredBB, PredBB});
    Updates.push_back({DominatorTree::Insert, PredBB, BB});
    Updates.push_back({DominatorTree::Delete, OldPredBB, BB});
    OldPredBranch = cast<BranchInst>(PredBB->getTerminator());
  }

  // We are going to have to map operands from the original BB block into the
  // PredBB block.  Evaluate PHI nodes in BB.
  ValueToValueMapTy ValueMapping;

  BasicBlock::iterator BI = BB->begin();
  for (; PHINode *PN = dyn_cast<PHINode>(BI); ++BI)
    ValueMapping[PN] = PN->getIncomingValueForBlock(PredBB);
  // Clone the non-phi instructions of BB into PredBB, keeping track of the
  // mapping and using it to remap operands in the cloned instructions.
  for (; BI != BB->end(); ++BI) {
    Instruction *New = BI->clone();
    New->insertInto(PredBB, OldPredBranch->getIterator());

    // Remap operands to patch up intra-block references.
    for (unsigned i = 0, e = New->getNumOperands(); i != e; ++i)
      if (Instruction *Inst = dyn_cast<Instruction>(New->getOperand(i))) {
        ValueToValueMapTy::iterator I = ValueMapping.find(Inst);
        if (I != ValueMapping.end())
          New->setOperand(i, I->second);
      }

    // Remap debug variable operands.
    remapDebugVariable(ValueMapping, New);

    // If this instruction can be simplified after the operands are updated,
    // just use the simplified value instead.  This frequently happens due to
    // phi translation.
    if (Value *IV = simplifyInstruction(
            New,
            {BB->getDataLayout(), TLI, nullptr, nullptr, New})) {
      ValueMapping[&*BI] = IV;
      if (!New->mayHaveSideEffects()) {
        New->eraseFromParent();
        New = nullptr;
        // Clone debug-info on the elided instruction to the destination
        // position.
        OldPredBranch->cloneDebugInfoFrom(&*BI, std::nullopt, true);
      }
    } else {
      ValueMapping[&*BI] = New;
    }
    if (New) {
      // Otherwise, insert the new instruction into the block.
      New->setName(BI->getName());
      // Clone across any debug-info attached to the old instruction.
      New->cloneDebugInfoFrom(&*BI);
      // Update Dominance from simplified New instruction operands.
      for (unsigned i = 0, e = New->getNumOperands(); i != e; ++i)
        if (BasicBlock *SuccBB = dyn_cast<BasicBlock>(New->getOperand(i)))
          Updates.push_back({DominatorTree::Insert, PredBB, SuccBB});
    }
  }

  // Check to see if the targets of the branch had PHI nodes. If so, we need to
  // add entries to the PHI nodes for branch from PredBB now.
  BranchInst *BBBranch = cast<BranchInst>(BB->getTerminator());
  addPHINodeEntriesForMappedBlock(BBBranch->getSuccessor(0), BB, PredBB,
                                  ValueMapping);
  addPHINodeEntriesForMappedBlock(BBBranch->getSuccessor(1), BB, PredBB,
                                  ValueMapping);

  updateSSA(BB, PredBB, ValueMapping);

  // PredBB no longer jumps to BB, remove entries in the PHI node for the edge
  // that we nuked.
  BB->removePredecessor(PredBB, true);

  // Remove the unconditional branch at the end of the PredBB block.
  OldPredBranch->eraseFromParent();
  if (auto *BPI = getBPI())
    BPI->copyEdgeProbabilities(BB, PredBB);
  DTU->applyUpdatesPermissive(Updates);

  ++NumDupes;
  return true;
}

// Pred is a predecessor of BB with an unconditional branch to BB. SI is
// a Select instruction in Pred. BB has other predecessors and SI is used in
// a PHI node in BB. SI has no other use.
// A new basic block, NewBB, is created and SI is converted to compare and 
// conditional branch. SI is erased from parent.
void JumpThreadingPass::unfoldSelectInstr(BasicBlock *Pred, BasicBlock *BB,
                                          SelectInst *SI, PHINode *SIUse,
                                          unsigned Idx) {
  // Expand the select.
  //
  // Pred --
  //  |    v
  //  |  NewBB
  //  |    |
  //  |-----
  //  v
  // BB
  BranchInst *PredTerm = cast<BranchInst>(Pred->getTerminator());
  BasicBlock *NewBB = BasicBlock::Create(BB->getContext(), "select.unfold",
                                         BB->getParent(), BB);
  // Move the unconditional branch to NewBB.
  PredTerm->removeFromParent();
  PredTerm->insertInto(NewBB, NewBB->end());
  // Create a conditional branch and update PHI nodes.
  auto *BI = BranchInst::Create(NewBB, BB, SI->getCondition(), Pred);
  BI->applyMergedLocation(PredTerm->getDebugLoc(), SI->getDebugLoc());
  BI->copyMetadata(*SI, {LLVMContext::MD_prof});
  SIUse->setIncomingValue(Idx, SI->getFalseValue());
  SIUse->addIncoming(SI->getTrueValue(), NewBB);

  uint64_t TrueWeight = 1;
  uint64_t FalseWeight = 1;
  // Copy probabilities from 'SI' to created conditional branch in 'Pred'.
  if (extractBranchWeights(*SI, TrueWeight, FalseWeight) &&
      (TrueWeight + FalseWeight) != 0) {
    SmallVector<BranchProbability, 2> BP;
    BP.emplace_back(BranchProbability::getBranchProbability(
        TrueWeight, TrueWeight + FalseWeight));
    BP.emplace_back(BranchProbability::getBranchProbability(
        FalseWeight, TrueWeight + FalseWeight));
    // Update BPI if exists.
    if (auto *BPI = getBPI())
      BPI->setEdgeProbability(Pred, BP);
  }
  // Set the block frequency of NewBB.
  if (auto *BFI = getBFI()) {
    if ((TrueWeight + FalseWeight) == 0) {
      TrueWeight = 1;
      FalseWeight = 1;
    }
    BranchProbability PredToNewBBProb = BranchProbability::getBranchProbability(
        TrueWeight, TrueWeight + FalseWeight);
    auto NewBBFreq = BFI->getBlockFreq(Pred) * PredToNewBBProb;
    BFI->setBlockFreq(NewBB, NewBBFreq);
  }

  // The select is now dead.
  SI->eraseFromParent();
  DTU->applyUpdatesPermissive({{DominatorTree::Insert, NewBB, BB},
                               {DominatorTree::Insert, Pred, NewBB}});

  // Update any other PHI nodes in BB.
  for (BasicBlock::iterator BI = BB->begin();
       PHINode *Phi = dyn_cast<PHINode>(BI); ++BI)
    if (Phi != SIUse)
      Phi->addIncoming(Phi->getIncomingValueForBlock(Pred), NewBB);
}

bool JumpThreadingPass::tryToUnfoldSelect(SwitchInst *SI, BasicBlock *BB) {
  PHINode *CondPHI = dyn_cast<PHINode>(SI->getCondition());

  if (!CondPHI || CondPHI->getParent() != BB)
    return false;

  for (unsigned I = 0, E = CondPHI->getNumIncomingValues(); I != E; ++I) {
    BasicBlock *Pred = CondPHI->getIncomingBlock(I);
    SelectInst *PredSI = dyn_cast<SelectInst>(CondPHI->getIncomingValue(I));

    // The second and third condition can be potentially relaxed. Currently
    // the conditions help to simplify the code and allow us to reuse existing
    // code, developed for tryToUnfoldSelect(CmpInst *, BasicBlock *)
    if (!PredSI || PredSI->getParent() != Pred || !PredSI->hasOneUse())
      continue;

    BranchInst *PredTerm = dyn_cast<BranchInst>(Pred->getTerminator());
    if (!PredTerm || !PredTerm->isUnconditional())
      continue;

    unfoldSelectInstr(Pred, BB, PredSI, CondPHI, I);
    return true;
  }
  return false;
}

/// tryToUnfoldSelect - Look for blocks of the form
/// bb1:
///   %a = select
///   br bb2
///
/// bb2:
///   %p = phi [%a, %bb1] ...
///   %c = icmp %p
///   br i1 %c
///
/// And expand the select into a branch structure if one of its arms allows %c
/// to be folded. This later enables threading from bb1 over bb2.
bool JumpThreadingPass::tryToUnfoldSelect(CmpInst *CondCmp, BasicBlock *BB) {
  BranchInst *CondBr = dyn_cast<BranchInst>(BB->getTerminator());
  PHINode *CondLHS = dyn_cast<PHINode>(CondCmp->getOperand(0));
  Constant *CondRHS = cast<Constant>(CondCmp->getOperand(1));

  if (!CondBr || !CondBr->isConditional() || !CondLHS ||
      CondLHS->getParent() != BB)
    return false;

  for (unsigned I = 0, E = CondLHS->getNumIncomingValues(); I != E; ++I) {
    BasicBlock *Pred = CondLHS->getIncomingBlock(I);
    SelectInst *SI = dyn_cast<SelectInst>(CondLHS->getIncomingValue(I));

    // Look if one of the incoming values is a select in the corresponding
    // predecessor.
    if (!SI || SI->getParent() != Pred || !SI->hasOneUse())
      continue;

    BranchInst *PredTerm = dyn_cast<BranchInst>(Pred->getTerminator());
    if (!PredTerm || !PredTerm->isUnconditional())
      continue;

    // Now check if one of the select values would allow us to constant fold the
    // terminator in BB. We don't do the transform if both sides fold, those
    // cases will be threaded in any case.
    Constant *LHSRes =
        LVI->getPredicateOnEdge(CondCmp->getPredicate(), SI->getOperand(1),
                                CondRHS, Pred, BB, CondCmp);
    Constant *RHSRes =
        LVI->getPredicateOnEdge(CondCmp->getPredicate(), SI->getOperand(2),
                                CondRHS, Pred, BB, CondCmp);
    if ((LHSRes || RHSRes) && LHSRes != RHSRes) {
      unfoldSelectInstr(Pred, BB, SI, CondLHS, I);
      return true;
    }
  }
  return false;
}

/// tryToUnfoldSelectInCurrBB - Look for PHI/Select or PHI/CMP/Select in the
/// same BB in the form
/// bb:
///   %p = phi [false, %bb1], [true, %bb2], [false, %bb3], [true, %bb4], ...
///   %s = select %p, trueval, falseval
///
/// or
///
/// bb:
///   %p = phi [0, %bb1], [1, %bb2], [0, %bb3], [1, %bb4], ...
///   %c = cmp %p, 0
///   %s = select %c, trueval, falseval
///
/// And expand the select into a branch structure. This later enables
/// jump-threading over bb in this pass.
///
/// Using the similar approach of SimplifyCFG::FoldCondBranchOnPHI(), unfold
/// select if the associated PHI has at least one constant.  If the unfolded
/// select is not jump-threaded, it will be folded again in the later
/// optimizations.
bool JumpThreadingPass::tryToUnfoldSelectInCurrBB(BasicBlock *BB) {
  // This transform would reduce the quality of msan diagnostics.
  // Disable this transform under MemorySanitizer.
  if (BB->getParent()->hasFnAttribute(Attribute::SanitizeMemory))
    return false;

  // If threading this would thread across a loop header, don't thread the edge.
  // See the comments above findLoopHeaders for justifications and caveats.
  if (LoopHeaders.count(BB))
    return false;

  for (BasicBlock::iterator BI = BB->begin();
       PHINode *PN = dyn_cast<PHINode>(BI); ++BI) {
    // Look for a Phi having at least one constant incoming value.
    if (llvm::all_of(PN->incoming_values(),
                     [](Value *V) { return !isa<ConstantInt>(V); }))
      continue;

    auto isUnfoldCandidate = [BB](SelectInst *SI, Value *V) {
      using namespace PatternMatch;

      // Check if SI is in BB and use V as condition.
      if (SI->getParent() != BB)
        return false;
      Value *Cond = SI->getCondition();
      bool IsAndOr = match(SI, m_CombineOr(m_LogicalAnd(), m_LogicalOr()));
      return Cond && Cond == V && Cond->getType()->isIntegerTy(1) && !IsAndOr;
    };

    SelectInst *SI = nullptr;
    for (Use &U : PN->uses()) {
      if (ICmpInst *Cmp = dyn_cast<ICmpInst>(U.getUser())) {
        // Look for a ICmp in BB that compares PN with a constant and is the
        // condition of a Select.
        if (Cmp->getParent() == BB && Cmp->hasOneUse() &&
            isa<ConstantInt>(Cmp->getOperand(1 - U.getOperandNo())))
          if (SelectInst *SelectI = dyn_cast<SelectInst>(Cmp->user_back()))
            if (isUnfoldCandidate(SelectI, Cmp->use_begin()->get())) {
              SI = SelectI;
              break;
            }
      } else if (SelectInst *SelectI = dyn_cast<SelectInst>(U.getUser())) {
        // Look for a Select in BB that uses PN as condition.
        if (isUnfoldCandidate(SelectI, U.get())) {
          SI = SelectI;
          break;
        }
      }
    }

    if (!SI)
      continue;
    // Expand the select.
    Value *Cond = SI->getCondition();
    if (!isGuaranteedNotToBeUndefOrPoison(Cond, nullptr, SI))
      Cond = new FreezeInst(Cond, "cond.fr", SI->getIterator());
    MDNode *BranchWeights = getBranchWeightMDNode(*SI);
    Instruction *Term =
        SplitBlockAndInsertIfThen(Cond, SI, false, BranchWeights);
    BasicBlock *SplitBB = SI->getParent();
    BasicBlock *NewBB = Term->getParent();
    PHINode *NewPN = PHINode::Create(SI->getType(), 2, "", SI->getIterator());
    NewPN->addIncoming(SI->getTrueValue(), Term->getParent());
    NewPN->addIncoming(SI->getFalseValue(), BB);
    NewPN->setDebugLoc(SI->getDebugLoc());
    SI->replaceAllUsesWith(NewPN);
    SI->eraseFromParent();
    // NewBB and SplitBB are newly created blocks which require insertion.
    std::vector<DominatorTree::UpdateType> Updates;
    Updates.reserve((2 * SplitBB->getTerminator()->getNumSuccessors()) + 3);
    Updates.push_back({DominatorTree::Insert, BB, SplitBB});
    Updates.push_back({DominatorTree::Insert, BB, NewBB});
    Updates.push_back({DominatorTree::Insert, NewBB, SplitBB});
    // BB's successors were moved to SplitBB, update DTU accordingly.
    for (auto *Succ : successors(SplitBB)) {
      Updates.push_back({DominatorTree::Delete, BB, Succ});
      Updates.push_back({DominatorTree::Insert, SplitBB, Succ});
    }
    DTU->applyUpdatesPermissive(Updates);
    return true;
  }
  return false;
}

/// Try to propagate a guard from the current BB into one of its predecessors
/// in case if another branch of execution implies that the condition of this
/// guard is always true. Currently we only process the simplest case that
/// looks like:
///
/// Start:
///   %cond = ...
///   br i1 %cond, label %T1, label %F1
/// T1:
///   br label %Merge
/// F1:
///   br label %Merge
/// Merge:
///   %condGuard = ...
///   call void(i1, ...) @llvm.experimental.guard( i1 %condGuard )[ "deopt"() ]
///
/// And cond either implies condGuard or !condGuard. In this case all the
/// instructions before the guard can be duplicated in both branches, and the
/// guard is then threaded to one of them.
bool JumpThreadingPass::processGuards(BasicBlock *BB) {
  using namespace PatternMatch;

  // We only want to deal with two predecessors.
  BasicBlock *Pred1, *Pred2;
  auto PI = pred_begin(BB), PE = pred_end(BB);
  if (PI == PE)
    return false;
  Pred1 = *PI++;
  if (PI == PE)
    return false;
  Pred2 = *PI++;
  if (PI != PE)
    return false;
  if (Pred1 == Pred2)
    return false;

  // Try to thread one of the guards of the block.
  // TODO: Look up deeper than to immediate predecessor?
  auto *Parent = Pred1->getSinglePredecessor();
  if (!Parent || Parent != Pred2->getSinglePredecessor())
    return false;

  if (auto *BI = dyn_cast<BranchInst>(Parent->getTerminator()))
    for (auto &I : *BB)
      if (isGuard(&I) && threadGuard(BB, cast<IntrinsicInst>(&I), BI))
        return true;

  return false;
}

/// Try to propagate the guard from BB which is the lower block of a diamond
/// to one of its branches, in case if diamond's condition implies guard's
/// condition.
bool JumpThreadingPass::threadGuard(BasicBlock *BB, IntrinsicInst *Guard,
                                    BranchInst *BI) {
  assert(BI->getNumSuccessors() == 2 && "Wrong number of successors?");
  assert(BI->isConditional() && "Unconditional branch has 2 successors?");
  Value *GuardCond = Guard->getArgOperand(0);
  Value *BranchCond = BI->getCondition();
  BasicBlock *TrueDest = BI->getSuccessor(0);
  BasicBlock *FalseDest = BI->getSuccessor(1);

  auto &DL = BB->getDataLayout();
  bool TrueDestIsSafe = false;
  bool FalseDestIsSafe = false;

  // True dest is safe if BranchCond => GuardCond.
  auto Impl = isImpliedCondition(BranchCond, GuardCond, DL);
  if (Impl && *Impl)
    TrueDestIsSafe = true;
  else {
    // False dest is safe if !BranchCond => GuardCond.
    Impl = isImpliedCondition(BranchCond, GuardCond, DL, /* LHSIsTrue */ false);
    if (Impl && *Impl)
      FalseDestIsSafe = true;
  }

  if (!TrueDestIsSafe && !FalseDestIsSafe)
    return false;

  BasicBlock *PredUnguardedBlock = TrueDestIsSafe ? TrueDest : FalseDest;
  BasicBlock *PredGuardedBlock = FalseDestIsSafe ? TrueDest : FalseDest;

  ValueToValueMapTy UnguardedMapping, GuardedMapping;
  Instruction *AfterGuard = Guard->getNextNode();
  unsigned Cost =
      getJumpThreadDuplicationCost(TTI, BB, AfterGuard, BBDupThreshold);
  if (Cost > BBDupThreshold)
    return false;
  // Duplicate all instructions before the guard and the guard itself to the
  // branch where implication is not proved.
  BasicBlock *GuardedBlock = DuplicateInstructionsInSplitBetween(
      BB, PredGuardedBlock, AfterGuard, GuardedMapping, *DTU);
  assert(GuardedBlock && "Could not create the guarded block?");
  // Duplicate all instructions before the guard in the unguarded branch.
  // Since we have successfully duplicated the guarded block and this block
  // has fewer instructions, we expect it to succeed.
  BasicBlock *UnguardedBlock = DuplicateInstructionsInSplitBetween(
      BB, PredUnguardedBlock, Guard, UnguardedMapping, *DTU);
  assert(UnguardedBlock && "Could not create the unguarded block?");
  LLVM_DEBUG(dbgs() << "Moved guard " << *Guard << " to block "
                    << GuardedBlock->getName() << "\n");
  // Some instructions before the guard may still have uses. For them, we need
  // to create Phi nodes merging their copies in both guarded and unguarded
  // branches. Those instructions that have no uses can be just removed.
  SmallVector<Instruction *, 4> ToRemove;
  for (auto BI = BB->begin(); &*BI != AfterGuard; ++BI)
    if (!isa<PHINode>(&*BI))
      ToRemove.push_back(&*BI);

  BasicBlock::iterator InsertionPoint = BB->getFirstInsertionPt();
  assert(InsertionPoint != BB->end() && "Empty block?");
  // Substitute with Phis & remove.
  for (auto *Inst : reverse(ToRemove)) {
    if (!Inst->use_empty()) {
      PHINode *NewPN = PHINode::Create(Inst->getType(), 2);
      NewPN->addIncoming(UnguardedMapping[Inst], UnguardedBlock);
      NewPN->addIncoming(GuardedMapping[Inst], GuardedBlock);
      NewPN->setDebugLoc(Inst->getDebugLoc());
      NewPN->insertBefore(InsertionPoint);
      Inst->replaceAllUsesWith(NewPN);
    }
    Inst->dropDbgRecords();
    Inst->eraseFromParent();
  }
  return true;
}

PreservedAnalyses JumpThreadingPass::getPreservedAnalysis() const {
  PreservedAnalyses PA;
  PA.preserve<LazyValueAnalysis>();
  PA.preserve<DominatorTreeAnalysis>();

  // TODO: We would like to preserve BPI/BFI. Enable once all paths update them.
  // TODO: Would be nice to verify BPI/BFI consistency as well.
  return PA;
}

template <typename AnalysisT>
typename AnalysisT::Result *JumpThreadingPass::runExternalAnalysis() {
  assert(FAM && "Can't run external analysis without FunctionAnalysisManager");

  // If there were no changes since last call to 'runExternalAnalysis' then all
  // analysis is either up to date or explicitly invalidated. Just go ahead and
  // run the "external" analysis.
  if (!ChangedSinceLastAnalysisUpdate) {
    assert(!DTU->hasPendingUpdates() &&
           "Lost update of 'ChangedSinceLastAnalysisUpdate'?");
    // Run the "external" analysis.
    return &FAM->getResult<AnalysisT>(*F);
  }
  ChangedSinceLastAnalysisUpdate = false;

  auto PA = getPreservedAnalysis();
  // TODO: This shouldn't be needed once 'getPreservedAnalysis' reports BPI/BFI
  // as preserved.
  PA.preserve<BranchProbabilityAnalysis>();
  PA.preserve<BlockFrequencyAnalysis>();
  // Report everything except explicitly preserved as invalid.
  FAM->invalidate(*F, PA);
  // Update DT/PDT.
  DTU->flush();
  // Make sure DT/PDT are valid before running "external" analysis.
  assert(DTU->getDomTree().verify(DominatorTree::VerificationLevel::Fast));
  assert((!DTU->hasPostDomTree() ||
          DTU->getPostDomTree().verify(
              PostDominatorTree::VerificationLevel::Fast)));
  // Run the "external" analysis.
  auto *Result = &FAM->getResult<AnalysisT>(*F);
  // Update analysis JumpThreading depends on and not explicitly preserved.
  TTI = &FAM->getResult<TargetIRAnalysis>(*F);
  TLI = &FAM->getResult<TargetLibraryAnalysis>(*F);
  AA = &FAM->getResult<AAManager>(*F);

  return Result;
}

BranchProbabilityInfo *JumpThreadingPass::getBPI() {
  if (!BPI) {
    assert(FAM && "Can't create BPI without FunctionAnalysisManager");
    BPI = FAM->getCachedResult<BranchProbabilityAnalysis>(*F);
  }
  return *BPI;
}

BlockFrequencyInfo *JumpThreadingPass::getBFI() {
  if (!BFI) {
    assert(FAM && "Can't create BFI without FunctionAnalysisManager");
    BFI = FAM->getCachedResult<BlockFrequencyAnalysis>(*F);
  }
  return *BFI;
}

// Important note on validity of BPI/BFI. JumpThreading tries to preserve
// BPI/BFI as it goes. Thus if cached instance exists it will be updated.
// Otherwise, new instance of BPI/BFI is created (up to date by definition).
BranchProbabilityInfo *JumpThreadingPass::getOrCreateBPI(bool Force) {
  auto *Res = getBPI();
  if (Res)
    return Res;

  if (Force)
    BPI = runExternalAnalysis<BranchProbabilityAnalysis>();

  return *BPI;
}

BlockFrequencyInfo *JumpThreadingPass::getOrCreateBFI(bool Force) {
  auto *Res = getBFI();
  if (Res)
    return Res;

  if (Force)
    BFI = runExternalAnalysis<BlockFrequencyAnalysis>();

  return *BFI;
}
