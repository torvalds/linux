//===- DivRemPairs.cpp - Hoist/[dr]ecompose division and remainder --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass hoists and/or decomposes/recomposes integer division and remainder
// instructions to enable CFG improvements and better codegen.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Transforms/Utils/BypassSlowDivision.h"
#include <optional>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "div-rem-pairs"
STATISTIC(NumPairs, "Number of div/rem pairs");
STATISTIC(NumRecomposed, "Number of instructions recomposed");
STATISTIC(NumHoisted, "Number of instructions hoisted");
STATISTIC(NumDecomposed, "Number of instructions decomposed");
DEBUG_COUNTER(DRPCounter, "div-rem-pairs-transform",
              "Controls transformations in div-rem-pairs pass");

namespace {
struct ExpandedMatch {
  DivRemMapKey Key;
  Instruction *Value;
};
} // namespace

/// See if we can match: (which is the form we expand into)
///   X - ((X ?/ Y) * Y)
/// which is equivalent to:
///   X ?% Y
static std::optional<ExpandedMatch> matchExpandedRem(Instruction &I) {
  Value *Dividend, *XroundedDownToMultipleOfY;
  if (!match(&I, m_Sub(m_Value(Dividend), m_Value(XroundedDownToMultipleOfY))))
    return std::nullopt;

  Value *Divisor;
  Instruction *Div;
  // Look for  ((X / Y) * Y)
  if (!match(
          XroundedDownToMultipleOfY,
          m_c_Mul(m_CombineAnd(m_IDiv(m_Specific(Dividend), m_Value(Divisor)),
                               m_Instruction(Div)),
                  m_Deferred(Divisor))))
    return std::nullopt;

  ExpandedMatch M;
  M.Key.SignedOp = Div->getOpcode() == Instruction::SDiv;
  M.Key.Dividend = Dividend;
  M.Key.Divisor = Divisor;
  M.Value = &I;
  return M;
}

namespace {
/// A thin wrapper to store two values that we matched as div-rem pair.
/// We want this extra indirection to avoid dealing with RAUW'ing the map keys.
struct DivRemPairWorklistEntry {
  /// The actual udiv/sdiv instruction. Source of truth.
  AssertingVH<Instruction> DivInst;

  /// The instruction that we have matched as a remainder instruction.
  /// Should only be used as Value, don't introspect it.
  AssertingVH<Instruction> RemInst;

  DivRemPairWorklistEntry(Instruction *DivInst_, Instruction *RemInst_)
      : DivInst(DivInst_), RemInst(RemInst_) {
    assert((DivInst->getOpcode() == Instruction::UDiv ||
            DivInst->getOpcode() == Instruction::SDiv) &&
           "Not a division.");
    assert(DivInst->getType() == RemInst->getType() && "Types should match.");
    // We can't check anything else about remainder instruction,
    // it's not strictly required to be a urem/srem.
  }

  /// The type for this pair, identical for both the div and rem.
  Type *getType() const { return DivInst->getType(); }

  /// Is this pair signed or unsigned?
  bool isSigned() const { return DivInst->getOpcode() == Instruction::SDiv; }

  /// In this pair, what are the divident and divisor?
  Value *getDividend() const { return DivInst->getOperand(0); }
  Value *getDivisor() const { return DivInst->getOperand(1); }

  bool isRemExpanded() const {
    switch (RemInst->getOpcode()) {
    case Instruction::SRem:
    case Instruction::URem:
      return false; // single 'rem' instruction - unexpanded form.
    default:
      return true; // anything else means we have remainder in expanded form.
    }
  }
};
} // namespace
using DivRemWorklistTy = SmallVector<DivRemPairWorklistEntry, 4>;

/// Find matching pairs of integer div/rem ops (they have the same numerator,
/// denominator, and signedness). Place those pairs into a worklist for further
/// processing. This indirection is needed because we have to use TrackingVH<>
/// because we will be doing RAUW, and if one of the rem instructions we change
/// happens to be an input to another div/rem in the maps, we'd have problems.
static DivRemWorklistTy getWorklist(Function &F) {
  // Insert all divide and remainder instructions into maps keyed by their
  // operands and opcode (signed or unsigned).
  DenseMap<DivRemMapKey, Instruction *> DivMap;
  // Use a MapVector for RemMap so that instructions are moved/inserted in a
  // deterministic order.
  MapVector<DivRemMapKey, Instruction *> RemMap;
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (I.getOpcode() == Instruction::SDiv)
        DivMap[DivRemMapKey(true, I.getOperand(0), I.getOperand(1))] = &I;
      else if (I.getOpcode() == Instruction::UDiv)
        DivMap[DivRemMapKey(false, I.getOperand(0), I.getOperand(1))] = &I;
      else if (I.getOpcode() == Instruction::SRem)
        RemMap[DivRemMapKey(true, I.getOperand(0), I.getOperand(1))] = &I;
      else if (I.getOpcode() == Instruction::URem)
        RemMap[DivRemMapKey(false, I.getOperand(0), I.getOperand(1))] = &I;
      else if (auto Match = matchExpandedRem(I))
        RemMap[Match->Key] = Match->Value;
    }
  }

  // We'll accumulate the matching pairs of div-rem instructions here.
  DivRemWorklistTy Worklist;

  // We can iterate over either map because we are only looking for matched
  // pairs. Choose remainders for efficiency because they are usually even more
  // rare than division.
  for (auto &RemPair : RemMap) {
    // Find the matching division instruction from the division map.
    auto It = DivMap.find(RemPair.first);
    if (It == DivMap.end())
      continue;

    // We have a matching pair of div/rem instructions.
    NumPairs++;
    Instruction *RemInst = RemPair.second;

    // Place it in the worklist.
    Worklist.emplace_back(It->second, RemInst);
  }

  return Worklist;
}

/// Find matching pairs of integer div/rem ops (they have the same numerator,
/// denominator, and signedness). If they exist in different basic blocks, bring
/// them together by hoisting or replace the common division operation that is
/// implicit in the remainder:
/// X % Y <--> X - ((X / Y) * Y).
///
/// We can largely ignore the normal safety and cost constraints on speculation
/// of these ops when we find a matching pair. This is because we are already
/// guaranteed that any exceptions and most cost are already incurred by the
/// first member of the pair.
///
/// Note: This transform could be an oddball enhancement to EarlyCSE, GVN, or
/// SimplifyCFG, but it's split off on its own because it's different enough
/// that it doesn't quite match the stated objectives of those passes.
static bool optimizeDivRem(Function &F, const TargetTransformInfo &TTI,
                           const DominatorTree &DT) {
  bool Changed = false;

  // Get the matching pairs of div-rem instructions. We want this extra
  // indirection to avoid dealing with having to RAUW the keys of the maps.
  DivRemWorklistTy Worklist = getWorklist(F);

  // Process each entry in the worklist.
  for (DivRemPairWorklistEntry &E : Worklist) {
    if (!DebugCounter::shouldExecute(DRPCounter))
      continue;

    bool HasDivRemOp = TTI.hasDivRemOp(E.getType(), E.isSigned());

    auto &DivInst = E.DivInst;
    auto &RemInst = E.RemInst;

    const bool RemOriginallyWasInExpandedForm = E.isRemExpanded();
    (void)RemOriginallyWasInExpandedForm; // suppress unused variable warning

    if (HasDivRemOp && E.isRemExpanded()) {
      // The target supports div+rem but the rem is expanded.
      // We should recompose it first.
      Value *X = E.getDividend();
      Value *Y = E.getDivisor();
      Instruction *RealRem = E.isSigned() ? BinaryOperator::CreateSRem(X, Y)
                                          : BinaryOperator::CreateURem(X, Y);
      // Note that we place it right next to the original expanded instruction,
      // and letting further handling to move it if needed.
      RealRem->setName(RemInst->getName() + ".recomposed");
      RealRem->insertAfter(RemInst);
      Instruction *OrigRemInst = RemInst;
      // Update AssertingVH<> with new instruction so it doesn't assert.
      RemInst = RealRem;
      // And replace the original instruction with the new one.
      OrigRemInst->replaceAllUsesWith(RealRem);
      RealRem->setDebugLoc(OrigRemInst->getDebugLoc());
      OrigRemInst->eraseFromParent();
      NumRecomposed++;
      // Note that we have left ((X / Y) * Y) around.
      // If it had other uses we could rewrite it as X - X % Y
      Changed = true;
    }

    assert((!E.isRemExpanded() || !HasDivRemOp) &&
           "*If* the target supports div-rem, then by now the RemInst *is* "
           "Instruction::[US]Rem.");

    // If the target supports div+rem and the instructions are in the same block
    // already, there's nothing to do. The backend should handle this. If the
    // target does not support div+rem, then we will decompose the rem.
    if (HasDivRemOp && RemInst->getParent() == DivInst->getParent())
      continue;

    bool DivDominates = DT.dominates(DivInst, RemInst);
    if (!DivDominates && !DT.dominates(RemInst, DivInst)) {
      // We have matching div-rem pair, but they are in two different blocks,
      // neither of which dominates one another.

      BasicBlock *PredBB = nullptr;
      BasicBlock *DivBB = DivInst->getParent();
      BasicBlock *RemBB = RemInst->getParent();

      // It's only safe to hoist if every instruction before the Div/Rem in the
      // basic block is guaranteed to transfer execution.
      auto IsSafeToHoist = [](Instruction *DivOrRem, BasicBlock *ParentBB) {
        for (auto I = ParentBB->begin(), E = DivOrRem->getIterator(); I != E;
             ++I)
          if (!isGuaranteedToTransferExecutionToSuccessor(&*I))
            return false;

        return true;
      };

      // Look for something like this
      // PredBB
      //   |  \
      //   |  Rem
      //   |  /
      //  Div
      //
      // If the Rem block has a single predecessor and successor, and all paths
      // from PredBB go to either RemBB or DivBB, and execution of RemBB and
      // DivBB will always reach the Div/Rem, we can hoist Div to PredBB. If
      // we have a DivRem operation we can also hoist Rem. Otherwise we'll leave
      // Rem where it is and rewrite it to mul/sub.
      if (RemBB->getSingleSuccessor() == DivBB) {
        PredBB = RemBB->getUniquePredecessor();

        // Look for something like this
        //     PredBB
        //     /    \
        //   Div   Rem
        //
        // If the Rem and Din blocks share a unique predecessor, and all
        // paths from PredBB go to either RemBB or DivBB, and execution of RemBB
        // and DivBB will always reach the Div/Rem, we can hoist Div to PredBB.
        // If we have a DivRem operation we can also hoist Rem. By hoisting both
        // ops to the same block, we reduce code size and allow the DivRem to
        // issue sooner. Without a DivRem op, this transformation is
        // unprofitable because we would end up performing an extra Mul+Sub on
        // the Rem path.
      } else if (BasicBlock *RemPredBB = RemBB->getUniquePredecessor()) {
        // This hoist is only profitable when the target has a DivRem op.
        if (HasDivRemOp && RemPredBB == DivBB->getUniquePredecessor())
          PredBB = RemPredBB;
      }
      // FIXME: We could handle more hoisting cases.

      if (PredBB && !isa<CatchSwitchInst>(PredBB->getTerminator()) &&
          isGuaranteedToTransferExecutionToSuccessor(PredBB->getTerminator()) &&
          IsSafeToHoist(RemInst, RemBB) && IsSafeToHoist(DivInst, DivBB) &&
          all_of(successors(PredBB),
                 [&](BasicBlock *BB) { return BB == DivBB || BB == RemBB; }) &&
          all_of(predecessors(DivBB),
                 [&](BasicBlock *BB) { return BB == RemBB || BB == PredBB; })) {
        DivDominates = true;
        DivInst->moveBefore(PredBB->getTerminator());
        Changed = true;
        if (HasDivRemOp) {
          RemInst->moveBefore(PredBB->getTerminator());
          continue;
        }
      } else
        continue;
    }

    // The target does not have a single div/rem operation,
    // and the rem is already in expanded form. Nothing to do.
    if (!HasDivRemOp && E.isRemExpanded())
      continue;

    if (HasDivRemOp) {
      // The target has a single div/rem operation. Hoist the lower instruction
      // to make the matched pair visible to the backend.
      if (DivDominates)
        RemInst->moveAfter(DivInst);
      else
        DivInst->moveAfter(RemInst);
      NumHoisted++;
    } else {
      // The target does not have a single div/rem operation,
      // and the rem is *not* in a already-expanded form.
      // Decompose the remainder calculation as:
      // X % Y --> X - ((X / Y) * Y).

      assert(!RemOriginallyWasInExpandedForm &&
             "We should not be expanding if the rem was in expanded form to "
             "begin with.");

      Value *X = E.getDividend();
      Value *Y = E.getDivisor();
      Instruction *Mul = BinaryOperator::CreateMul(DivInst, Y);
      Instruction *Sub = BinaryOperator::CreateSub(X, Mul);

      // If the remainder dominates, then hoist the division up to that block:
      //
      // bb1:
      //   %rem = srem %x, %y
      // bb2:
      //   %div = sdiv %x, %y
      // -->
      // bb1:
      //   %div = sdiv %x, %y
      //   %mul = mul %div, %y
      //   %rem = sub %x, %mul
      //
      // If the division dominates, it's already in the right place. The mul+sub
      // will be in a different block because we don't assume that they are
      // cheap to speculatively execute:
      //
      // bb1:
      //   %div = sdiv %x, %y
      // bb2:
      //   %rem = srem %x, %y
      // -->
      // bb1:
      //   %div = sdiv %x, %y
      // bb2:
      //   %mul = mul %div, %y
      //   %rem = sub %x, %mul
      //
      // If the div and rem are in the same block, we do the same transform,
      // but any code movement would be within the same block.

      if (!DivDominates)
        DivInst->moveBefore(RemInst);
      Mul->insertAfter(RemInst);
      Mul->setDebugLoc(RemInst->getDebugLoc());
      Sub->insertAfter(Mul);
      Sub->setDebugLoc(RemInst->getDebugLoc());

      // If DivInst has the exact flag, remove it. Otherwise this optimization
      // may replace a well-defined value 'X % Y' with poison.
      DivInst->dropPoisonGeneratingFlags();

      // If X can be undef, X should be frozen first.
      // For example, let's assume that Y = 1 & X = undef:
      //   %div = sdiv undef, 1 // %div = undef
      //   %rem = srem undef, 1 // %rem = 0
      // =>
      //   %div = sdiv undef, 1 // %div = undef
      //   %mul = mul %div, 1   // %mul = undef
      //   %rem = sub %x, %mul  // %rem = undef - undef = undef
      // If X is not frozen, %rem becomes undef after transformation.
      if (!isGuaranteedNotToBeUndef(X, nullptr, DivInst, &DT)) {
        auto *FrX =
            new FreezeInst(X, X->getName() + ".frozen", DivInst->getIterator());
        FrX->setDebugLoc(DivInst->getDebugLoc());
        DivInst->setOperand(0, FrX);
        Sub->setOperand(0, FrX);
      }
      // Same for Y. If X = 1 and Y = (undef | 1), %rem in src is either 1 or 0,
      // but %rem in tgt can be one of many integer values.
      if (!isGuaranteedNotToBeUndef(Y, nullptr, DivInst, &DT)) {
        auto *FrY =
            new FreezeInst(Y, Y->getName() + ".frozen", DivInst->getIterator());
        FrY->setDebugLoc(DivInst->getDebugLoc());
        DivInst->setOperand(1, FrY);
        Mul->setOperand(1, FrY);
      }

      // Now kill the explicit remainder. We have replaced it with:
      // (sub X, (mul (div X, Y), Y)
      Sub->setName(RemInst->getName() + ".decomposed");
      Instruction *OrigRemInst = RemInst;
      // Update AssertingVH<> with new instruction so it doesn't assert.
      RemInst = Sub;
      // And replace the original instruction with the new one.
      OrigRemInst->replaceAllUsesWith(Sub);
      OrigRemInst->eraseFromParent();
      NumDecomposed++;
    }
    Changed = true;
  }

  return Changed;
}

// Pass manager boilerplate below here.

PreservedAnalyses DivRemPairsPass::run(Function &F,
                                       FunctionAnalysisManager &FAM) {
  TargetTransformInfo &TTI = FAM.getResult<TargetIRAnalysis>(F);
  DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  if (!optimizeDivRem(F, TTI, DT))
    return PreservedAnalyses::all();
  // TODO: This pass just hoists/replaces math ops - all analyses are preserved?
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
