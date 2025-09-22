//===-- LoopPredication.cpp - Guard based loop predication pass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The LoopPredication pass tries to convert loop variant range checks to loop
// invariant by widening checks across loop iterations. For example, it will
// convert
//
//   for (i = 0; i < n; i++) {
//     guard(i < len);
//     ...
//   }
//
// to
//
//   for (i = 0; i < n; i++) {
//     guard(n - 1 < len);
//     ...
//   }
//
// After this transformation the condition of the guard is loop invariant, so
// loop-unswitch can later unswitch the loop by this condition which basically
// predicates the loop by the widened condition:
//
//   if (n - 1 < len)
//     for (i = 0; i < n; i++) {
//       ...
//     }
//   else
//     deoptimize
//
// It's tempting to rely on SCEV here, but it has proven to be problematic.
// Generally the facts SCEV provides about the increment step of add
// recurrences are true if the backedge of the loop is taken, which implicitly
// assumes that the guard doesn't fail. Using these facts to optimize the
// guard results in a circular logic where the guard is optimized under the
// assumption that it never fails.
//
// For example, in the loop below the induction variable will be marked as nuw
// basing on the guard. Basing on nuw the guard predicate will be considered
// monotonic. Given a monotonic condition it's tempting to replace the induction
// variable in the condition with its value on the last iteration. But this
// transformation is not correct, e.g. e = 4, b = 5 breaks the loop.
//
//   for (int i = b; i != e; i++)
//     guard(i u< len)
//
// One of the ways to reason about this problem is to use an inductive proof
// approach. Given the loop:
//
//   if (B(0)) {
//     do {
//       I = PHI(0, I.INC)
//       I.INC = I + Step
//       guard(G(I));
//     } while (B(I));
//   }
//
// where B(x) and G(x) are predicates that map integers to booleans, we want a
// loop invariant expression M such the following program has the same semantics
// as the above:
//
//   if (B(0)) {
//     do {
//       I = PHI(0, I.INC)
//       I.INC = I + Step
//       guard(G(0) && M);
//     } while (B(I));
//   }
//
// One solution for M is M = forall X . (G(X) && B(X)) => G(X + Step)
//
// Informal proof that the transformation above is correct:
//
//   By the definition of guards we can rewrite the guard condition to:
//     G(I) && G(0) && M
//
//   Let's prove that for each iteration of the loop:
//     G(0) && M => G(I)
//   And the condition above can be simplified to G(Start) && M.
//
//   Induction base.
//     G(0) && M => G(0)
//
//   Induction step. Assuming G(0) && M => G(I) on the subsequent
//   iteration:
//
//     B(I) is true because it's the backedge condition.
//     G(I) is true because the backedge is guarded by this condition.
//
//   So M = forall X . (G(X) && B(X)) => G(X + Step) implies G(I + Step).
//
// Note that we can use anything stronger than M, i.e. any condition which
// implies M.
//
// When S = 1 (i.e. forward iterating loop), the transformation is supported
// when:
//   * The loop has a single latch with the condition of the form:
//     B(X) = latchStart + X <pred> latchLimit,
//     where <pred> is u<, u<=, s<, or s<=.
//   * The guard condition is of the form
//     G(X) = guardStart + X u< guardLimit
//
//   For the ult latch comparison case M is:
//     forall X . guardStart + X u< guardLimit && latchStart + X <u latchLimit =>
//        guardStart + X + 1 u< guardLimit
//
//   The only way the antecedent can be true and the consequent can be false is
//   if
//     X == guardLimit - 1 - guardStart
//   (and guardLimit is non-zero, but we won't use this latter fact).
//   If X == guardLimit - 1 - guardStart then the second half of the antecedent is
//     latchStart + guardLimit - 1 - guardStart u< latchLimit
//   and its negation is
//     latchStart + guardLimit - 1 - guardStart u>= latchLimit
//
//   In other words, if
//     latchLimit u<= latchStart + guardLimit - 1 - guardStart
//   then:
//   (the ranges below are written in ConstantRange notation, where [A, B) is the
//   set for (I = A; I != B; I++ /*maywrap*/) yield(I);)
//
//      forall X . guardStart + X u< guardLimit &&
//                 latchStart + X u< latchLimit =>
//        guardStart + X + 1 u< guardLimit
//   == forall X . guardStart + X u< guardLimit &&
//                 latchStart + X u< latchStart + guardLimit - 1 - guardStart =>
//        guardStart + X + 1 u< guardLimit
//   == forall X . (guardStart + X) in [0, guardLimit) &&
//                 (latchStart + X) in [0, latchStart + guardLimit - 1 - guardStart) =>
//        (guardStart + X + 1) in [0, guardLimit)
//   == forall X . X in [-guardStart, guardLimit - guardStart) &&
//                 X in [-latchStart, guardLimit - 1 - guardStart) =>
//         X in [-guardStart - 1, guardLimit - guardStart - 1)
//   == true
//
//   So the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart u>= latchLimit
//   Similarly for ule condition the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart u> latchLimit
//   For slt condition the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart s>= latchLimit
//   For sle condition the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart s> latchLimit
//
// When S = -1 (i.e. reverse iterating loop), the transformation is supported
// when:
//   * The loop has a single latch with the condition of the form:
//     B(X) = X <pred> latchLimit, where <pred> is u>, u>=, s>, or s>=.
//   * The guard condition is of the form
//     G(X) = X - 1 u< guardLimit
//
//   For the ugt latch comparison case M is:
//     forall X. X-1 u< guardLimit and X u> latchLimit => X-2 u< guardLimit
//
//   The only way the antecedent can be true and the consequent can be false is if
//     X == 1.
//   If X == 1 then the second half of the antecedent is
//     1 u> latchLimit, and its negation is latchLimit u>= 1.
//
//   So the widened condition is:
//     guardStart u< guardLimit && latchLimit u>= 1.
//   Similarly for sgt condition the widened condition is:
//     guardStart u< guardLimit && latchLimit s>= 1.
//   For uge condition the widened condition is:
//     guardStart u< guardLimit && latchLimit u> 1.
//   For sge condition the widened condition is:
//     guardStart u< guardLimit && latchLimit s> 1.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopPredication.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/GuardUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <optional>

#define DEBUG_TYPE "loop-predication"

STATISTIC(TotalConsidered, "Number of guards considered");
STATISTIC(TotalWidened, "Number of checks widened");

using namespace llvm;

static cl::opt<bool> EnableIVTruncation("loop-predication-enable-iv-truncation",
                                        cl::Hidden, cl::init(true));

static cl::opt<bool> EnableCountDownLoop("loop-predication-enable-count-down-loop",
                                        cl::Hidden, cl::init(true));

static cl::opt<bool>
    SkipProfitabilityChecks("loop-predication-skip-profitability-checks",
                            cl::Hidden, cl::init(false));

// This is the scale factor for the latch probability. We use this during
// profitability analysis to find other exiting blocks that have a much higher
// probability of exiting the loop instead of loop exiting via latch.
// This value should be greater than 1 for a sane profitability check.
static cl::opt<float> LatchExitProbabilityScale(
    "loop-predication-latch-probability-scale", cl::Hidden, cl::init(2.0),
    cl::desc("scale factor for the latch probability. Value should be greater "
             "than 1. Lower values are ignored"));

static cl::opt<bool> PredicateWidenableBranchGuards(
    "loop-predication-predicate-widenable-branches-to-deopt", cl::Hidden,
    cl::desc("Whether or not we should predicate guards "
             "expressed as widenable branches to deoptimize blocks"),
    cl::init(true));

static cl::opt<bool> InsertAssumesOfPredicatedGuardsConditions(
    "loop-predication-insert-assumes-of-predicated-guards-conditions",
    cl::Hidden,
    cl::desc("Whether or not we should insert assumes of conditions of "
             "predicated guards"),
    cl::init(true));

namespace {
/// Represents an induction variable check:
///   icmp Pred, <induction variable>, <loop invariant limit>
struct LoopICmp {
  ICmpInst::Predicate Pred;
  const SCEVAddRecExpr *IV;
  const SCEV *Limit;
  LoopICmp(ICmpInst::Predicate Pred, const SCEVAddRecExpr *IV,
           const SCEV *Limit)
    : Pred(Pred), IV(IV), Limit(Limit) {}
  LoopICmp() = default;
  void dump() {
    dbgs() << "LoopICmp Pred = " << Pred << ", IV = " << *IV
           << ", Limit = " << *Limit << "\n";
  }
};

class LoopPredication {
  AliasAnalysis *AA;
  DominatorTree *DT;
  ScalarEvolution *SE;
  LoopInfo *LI;
  MemorySSAUpdater *MSSAU;

  Loop *L;
  const DataLayout *DL;
  BasicBlock *Preheader;
  LoopICmp LatchCheck;

  bool isSupportedStep(const SCEV* Step);
  std::optional<LoopICmp> parseLoopICmp(ICmpInst *ICI);
  std::optional<LoopICmp> parseLoopLatchICmp();

  /// Return an insertion point suitable for inserting a safe to speculate
  /// instruction whose only user will be 'User' which has operands 'Ops'.  A
  /// trivial result would be the at the User itself, but we try to return a
  /// loop invariant location if possible.
  Instruction *findInsertPt(Instruction *User, ArrayRef<Value*> Ops);
  /// Same as above, *except* that this uses the SCEV definition of invariant
  /// which is that an expression *can be made* invariant via SCEVExpander.
  /// Thus, this version is only suitable for finding an insert point to be
  /// passed to SCEVExpander!
  Instruction *findInsertPt(const SCEVExpander &Expander, Instruction *User,
                            ArrayRef<const SCEV *> Ops);

  /// Return true if the value is known to produce a single fixed value across
  /// all iterations on which it executes.  Note that this does not imply
  /// speculation safety.  That must be established separately.
  bool isLoopInvariantValue(const SCEV* S);

  Value *expandCheck(SCEVExpander &Expander, Instruction *Guard,
                     ICmpInst::Predicate Pred, const SCEV *LHS,
                     const SCEV *RHS);

  std::optional<Value *> widenICmpRangeCheck(ICmpInst *ICI,
                                             SCEVExpander &Expander,
                                             Instruction *Guard);
  std::optional<Value *>
  widenICmpRangeCheckIncrementingLoop(LoopICmp LatchCheck, LoopICmp RangeCheck,
                                      SCEVExpander &Expander,
                                      Instruction *Guard);
  std::optional<Value *>
  widenICmpRangeCheckDecrementingLoop(LoopICmp LatchCheck, LoopICmp RangeCheck,
                                      SCEVExpander &Expander,
                                      Instruction *Guard);
  void widenChecks(SmallVectorImpl<Value *> &Checks,
                   SmallVectorImpl<Value *> &WidenedChecks,
                   SCEVExpander &Expander, Instruction *Guard);
  bool widenGuardConditions(IntrinsicInst *II, SCEVExpander &Expander);
  bool widenWidenableBranchGuardConditions(BranchInst *Guard, SCEVExpander &Expander);
  // If the loop always exits through another block in the loop, we should not
  // predicate based on the latch check. For example, the latch check can be a
  // very coarse grained check and there can be more fine grained exit checks
  // within the loop.
  bool isLoopProfitableToPredicate();

  bool predicateLoopExits(Loop *L, SCEVExpander &Rewriter);

public:
  LoopPredication(AliasAnalysis *AA, DominatorTree *DT, ScalarEvolution *SE,
                  LoopInfo *LI, MemorySSAUpdater *MSSAU)
      : AA(AA), DT(DT), SE(SE), LI(LI), MSSAU(MSSAU){};
  bool runOnLoop(Loop *L);
};

} // end namespace

PreservedAnalyses LoopPredicationPass::run(Loop &L, LoopAnalysisManager &AM,
                                           LoopStandardAnalysisResults &AR,
                                           LPMUpdater &U) {
  std::unique_ptr<MemorySSAUpdater> MSSAU;
  if (AR.MSSA)
    MSSAU = std::make_unique<MemorySSAUpdater>(AR.MSSA);
  LoopPredication LP(&AR.AA, &AR.DT, &AR.SE, &AR.LI,
                     MSSAU ? MSSAU.get() : nullptr);
  if (!LP.runOnLoop(&L))
    return PreservedAnalyses::all();

  auto PA = getLoopPassPreservedAnalyses();
  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}

std::optional<LoopICmp> LoopPredication::parseLoopICmp(ICmpInst *ICI) {
  auto Pred = ICI->getPredicate();
  auto *LHS = ICI->getOperand(0);
  auto *RHS = ICI->getOperand(1);

  const SCEV *LHSS = SE->getSCEV(LHS);
  if (isa<SCEVCouldNotCompute>(LHSS))
    return std::nullopt;
  const SCEV *RHSS = SE->getSCEV(RHS);
  if (isa<SCEVCouldNotCompute>(RHSS))
    return std::nullopt;

  // Canonicalize RHS to be loop invariant bound, LHS - a loop computable IV
  if (SE->isLoopInvariant(LHSS, L)) {
    std::swap(LHS, RHS);
    std::swap(LHSS, RHSS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(LHSS);
  if (!AR || AR->getLoop() != L)
    return std::nullopt;

  return LoopICmp(Pred, AR, RHSS);
}

Value *LoopPredication::expandCheck(SCEVExpander &Expander,
                                    Instruction *Guard,
                                    ICmpInst::Predicate Pred, const SCEV *LHS,
                                    const SCEV *RHS) {
  Type *Ty = LHS->getType();
  assert(Ty == RHS->getType() && "expandCheck operands have different types?");

  if (SE->isLoopInvariant(LHS, L) && SE->isLoopInvariant(RHS, L)) {
    IRBuilder<> Builder(Guard);
    if (SE->isLoopEntryGuardedByCond(L, Pred, LHS, RHS))
      return Builder.getTrue();
    if (SE->isLoopEntryGuardedByCond(L, ICmpInst::getInversePredicate(Pred),
                                     LHS, RHS))
      return Builder.getFalse();
  }

  Value *LHSV =
      Expander.expandCodeFor(LHS, Ty, findInsertPt(Expander, Guard, {LHS}));
  Value *RHSV =
      Expander.expandCodeFor(RHS, Ty, findInsertPt(Expander, Guard, {RHS}));
  IRBuilder<> Builder(findInsertPt(Guard, {LHSV, RHSV}));
  return Builder.CreateICmp(Pred, LHSV, RHSV);
}

// Returns true if its safe to truncate the IV to RangeCheckType.
// When the IV type is wider than the range operand type, we can still do loop
// predication, by generating SCEVs for the range and latch that are of the
// same type. We achieve this by generating a SCEV truncate expression for the
// latch IV. This is done iff truncation of the IV is a safe operation,
// without loss of information.
// Another way to achieve this is by generating a wider type SCEV for the
// range check operand, however, this needs a more involved check that
// operands do not overflow. This can lead to loss of information when the
// range operand is of the form: add i32 %offset, %iv. We need to prove that
// sext(x + y) is same as sext(x) + sext(y).
// This function returns true if we can safely represent the IV type in
// the RangeCheckType without loss of information.
static bool isSafeToTruncateWideIVType(const DataLayout &DL,
                                       ScalarEvolution &SE,
                                       const LoopICmp LatchCheck,
                                       Type *RangeCheckType) {
  if (!EnableIVTruncation)
    return false;
  assert(DL.getTypeSizeInBits(LatchCheck.IV->getType()).getFixedValue() >
             DL.getTypeSizeInBits(RangeCheckType).getFixedValue() &&
         "Expected latch check IV type to be larger than range check operand "
         "type!");
  // The start and end values of the IV should be known. This is to guarantee
  // that truncating the wide type will not lose information.
  auto *Limit = dyn_cast<SCEVConstant>(LatchCheck.Limit);
  auto *Start = dyn_cast<SCEVConstant>(LatchCheck.IV->getStart());
  if (!Limit || !Start)
    return false;
  // This check makes sure that the IV does not change sign during loop
  // iterations. Consider latchType = i64, LatchStart = 5, Pred = ICMP_SGE,
  // LatchEnd = 2, rangeCheckType = i32. If it's not a monotonic predicate, the
  // IV wraps around, and the truncation of the IV would lose the range of
  // iterations between 2^32 and 2^64.
  if (!SE.getMonotonicPredicateType(LatchCheck.IV, LatchCheck.Pred))
    return false;
  // The active bits should be less than the bits in the RangeCheckType. This
  // guarantees that truncating the latch check to RangeCheckType is a safe
  // operation.
  auto RangeCheckTypeBitSize =
      DL.getTypeSizeInBits(RangeCheckType).getFixedValue();
  return Start->getAPInt().getActiveBits() < RangeCheckTypeBitSize &&
         Limit->getAPInt().getActiveBits() < RangeCheckTypeBitSize;
}


// Return an LoopICmp describing a latch check equivlent to LatchCheck but with
// the requested type if safe to do so.  May involve the use of a new IV.
static std::optional<LoopICmp> generateLoopLatchCheck(const DataLayout &DL,
                                                      ScalarEvolution &SE,
                                                      const LoopICmp LatchCheck,
                                                      Type *RangeCheckType) {

  auto *LatchType = LatchCheck.IV->getType();
  if (RangeCheckType == LatchType)
    return LatchCheck;
  // For now, bail out if latch type is narrower than range type.
  if (DL.getTypeSizeInBits(LatchType).getFixedValue() <
      DL.getTypeSizeInBits(RangeCheckType).getFixedValue())
    return std::nullopt;
  if (!isSafeToTruncateWideIVType(DL, SE, LatchCheck, RangeCheckType))
    return std::nullopt;
  // We can now safely identify the truncated version of the IV and limit for
  // RangeCheckType.
  LoopICmp NewLatchCheck;
  NewLatchCheck.Pred = LatchCheck.Pred;
  NewLatchCheck.IV = dyn_cast<SCEVAddRecExpr>(
      SE.getTruncateExpr(LatchCheck.IV, RangeCheckType));
  if (!NewLatchCheck.IV)
    return std::nullopt;
  NewLatchCheck.Limit = SE.getTruncateExpr(LatchCheck.Limit, RangeCheckType);
  LLVM_DEBUG(dbgs() << "IV of type: " << *LatchType
                    << "can be represented as range check type:"
                    << *RangeCheckType << "\n");
  LLVM_DEBUG(dbgs() << "LatchCheck.IV: " << *NewLatchCheck.IV << "\n");
  LLVM_DEBUG(dbgs() << "LatchCheck.Limit: " << *NewLatchCheck.Limit << "\n");
  return NewLatchCheck;
}

bool LoopPredication::isSupportedStep(const SCEV* Step) {
  return Step->isOne() || (Step->isAllOnesValue() && EnableCountDownLoop);
}

Instruction *LoopPredication::findInsertPt(Instruction *Use,
                                           ArrayRef<Value*> Ops) {
  for (Value *Op : Ops)
    if (!L->isLoopInvariant(Op))
      return Use;
  return Preheader->getTerminator();
}

Instruction *LoopPredication::findInsertPt(const SCEVExpander &Expander,
                                           Instruction *Use,
                                           ArrayRef<const SCEV *> Ops) {
  // Subtlety: SCEV considers things to be invariant if the value produced is
  // the same across iterations.  This is not the same as being able to
  // evaluate outside the loop, which is what we actually need here.
  for (const SCEV *Op : Ops)
    if (!SE->isLoopInvariant(Op, L) ||
        !Expander.isSafeToExpandAt(Op, Preheader->getTerminator()))
      return Use;
  return Preheader->getTerminator();
}

bool LoopPredication::isLoopInvariantValue(const SCEV* S) {
  // Handling expressions which produce invariant results, but *haven't* yet
  // been removed from the loop serves two important purposes.
  // 1) Most importantly, it resolves a pass ordering cycle which would
  // otherwise need us to iteration licm, loop-predication, and either
  // loop-unswitch or loop-peeling to make progress on examples with lots of
  // predicable range checks in a row.  (Since, in the general case,  we can't
  // hoist the length checks until the dominating checks have been discharged
  // as we can't prove doing so is safe.)
  // 2) As a nice side effect, this exposes the value of peeling or unswitching
  // much more obviously in the IR.  Otherwise, the cost modeling for other
  // transforms would end up needing to duplicate all of this logic to model a
  // check which becomes predictable based on a modeled peel or unswitch.
  //
  // The cost of doing so in the worst case is an extra fill from the stack  in
  // the loop to materialize the loop invariant test value instead of checking
  // against the original IV which is presumable in a register inside the loop.
  // Such cases are presumably rare, and hint at missing oppurtunities for
  // other passes.

  if (SE->isLoopInvariant(S, L))
    // Note: This the SCEV variant, so the original Value* may be within the
    // loop even though SCEV has proven it is loop invariant.
    return true;

  // Handle a particular important case which SCEV doesn't yet know about which
  // shows up in range checks on arrays with immutable lengths.
  // TODO: This should be sunk inside SCEV.
  if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S))
    if (const auto *LI = dyn_cast<LoadInst>(U->getValue()))
      if (LI->isUnordered() && L->hasLoopInvariantOperands(LI))
        if (!isModSet(AA->getModRefInfoMask(LI->getOperand(0))) ||
            LI->hasMetadata(LLVMContext::MD_invariant_load))
          return true;
  return false;
}

std::optional<Value *> LoopPredication::widenICmpRangeCheckIncrementingLoop(
    LoopICmp LatchCheck, LoopICmp RangeCheck, SCEVExpander &Expander,
    Instruction *Guard) {
  auto *Ty = RangeCheck.IV->getType();
  // Generate the widened condition for the forward loop:
  //   guardStart u< guardLimit &&
  //   latchLimit <pred> guardLimit - 1 - guardStart + latchStart
  // where <pred> depends on the latch condition predicate. See the file
  // header comment for the reasoning.
  // guardLimit - guardStart + latchStart - 1
  const SCEV *GuardStart = RangeCheck.IV->getStart();
  const SCEV *GuardLimit = RangeCheck.Limit;
  const SCEV *LatchStart = LatchCheck.IV->getStart();
  const SCEV *LatchLimit = LatchCheck.Limit;
  // Subtlety: We need all the values to be *invariant* across all iterations,
  // but we only need to check expansion safety for those which *aren't*
  // already guaranteed to dominate the guard.
  if (!isLoopInvariantValue(GuardStart) ||
      !isLoopInvariantValue(GuardLimit) ||
      !isLoopInvariantValue(LatchStart) ||
      !isLoopInvariantValue(LatchLimit)) {
    LLVM_DEBUG(dbgs() << "Can't expand limit check!\n");
    return std::nullopt;
  }
  if (!Expander.isSafeToExpandAt(LatchStart, Guard) ||
      !Expander.isSafeToExpandAt(LatchLimit, Guard)) {
    LLVM_DEBUG(dbgs() << "Can't expand limit check!\n");
    return std::nullopt;
  }

  // guardLimit - guardStart + latchStart - 1
  const SCEV *RHS =
      SE->getAddExpr(SE->getMinusSCEV(GuardLimit, GuardStart),
                     SE->getMinusSCEV(LatchStart, SE->getOne(Ty)));
  auto LimitCheckPred =
      ICmpInst::getFlippedStrictnessPredicate(LatchCheck.Pred);

  LLVM_DEBUG(dbgs() << "LHS: " << *LatchLimit << "\n");
  LLVM_DEBUG(dbgs() << "RHS: " << *RHS << "\n");
  LLVM_DEBUG(dbgs() << "Pred: " << LimitCheckPred << "\n");

  auto *LimitCheck =
      expandCheck(Expander, Guard, LimitCheckPred, LatchLimit, RHS);
  auto *FirstIterationCheck = expandCheck(Expander, Guard, RangeCheck.Pred,
                                          GuardStart, GuardLimit);
  IRBuilder<> Builder(findInsertPt(Guard, {FirstIterationCheck, LimitCheck}));
  return Builder.CreateFreeze(
      Builder.CreateAnd(FirstIterationCheck, LimitCheck));
}

std::optional<Value *> LoopPredication::widenICmpRangeCheckDecrementingLoop(
    LoopICmp LatchCheck, LoopICmp RangeCheck, SCEVExpander &Expander,
    Instruction *Guard) {
  auto *Ty = RangeCheck.IV->getType();
  const SCEV *GuardStart = RangeCheck.IV->getStart();
  const SCEV *GuardLimit = RangeCheck.Limit;
  const SCEV *LatchStart = LatchCheck.IV->getStart();
  const SCEV *LatchLimit = LatchCheck.Limit;
  // Subtlety: We need all the values to be *invariant* across all iterations,
  // but we only need to check expansion safety for those which *aren't*
  // already guaranteed to dominate the guard.
  if (!isLoopInvariantValue(GuardStart) ||
      !isLoopInvariantValue(GuardLimit) ||
      !isLoopInvariantValue(LatchStart) ||
      !isLoopInvariantValue(LatchLimit)) {
    LLVM_DEBUG(dbgs() << "Can't expand limit check!\n");
    return std::nullopt;
  }
  if (!Expander.isSafeToExpandAt(LatchStart, Guard) ||
      !Expander.isSafeToExpandAt(LatchLimit, Guard)) {
    LLVM_DEBUG(dbgs() << "Can't expand limit check!\n");
    return std::nullopt;
  }
  // The decrement of the latch check IV should be the same as the
  // rangeCheckIV.
  auto *PostDecLatchCheckIV = LatchCheck.IV->getPostIncExpr(*SE);
  if (RangeCheck.IV != PostDecLatchCheckIV) {
    LLVM_DEBUG(dbgs() << "Not the same. PostDecLatchCheckIV: "
                      << *PostDecLatchCheckIV
                      << "  and RangeCheckIV: " << *RangeCheck.IV << "\n");
    return std::nullopt;
  }

  // Generate the widened condition for CountDownLoop:
  // guardStart u< guardLimit &&
  // latchLimit <pred> 1.
  // See the header comment for reasoning of the checks.
  auto LimitCheckPred =
      ICmpInst::getFlippedStrictnessPredicate(LatchCheck.Pred);
  auto *FirstIterationCheck = expandCheck(Expander, Guard,
                                          ICmpInst::ICMP_ULT,
                                          GuardStart, GuardLimit);
  auto *LimitCheck = expandCheck(Expander, Guard, LimitCheckPred, LatchLimit,
                                 SE->getOne(Ty));
  IRBuilder<> Builder(findInsertPt(Guard, {FirstIterationCheck, LimitCheck}));
  return Builder.CreateFreeze(
      Builder.CreateAnd(FirstIterationCheck, LimitCheck));
}

static void normalizePredicate(ScalarEvolution *SE, Loop *L,
                               LoopICmp& RC) {
  // LFTR canonicalizes checks to the ICMP_NE/EQ form; normalize back to the
  // ULT/UGE form for ease of handling by our caller.
  if (ICmpInst::isEquality(RC.Pred) &&
      RC.IV->getStepRecurrence(*SE)->isOne() &&
      SE->isKnownPredicate(ICmpInst::ICMP_ULE, RC.IV->getStart(), RC.Limit))
    RC.Pred = RC.Pred == ICmpInst::ICMP_NE ?
      ICmpInst::ICMP_ULT : ICmpInst::ICMP_UGE;
}

/// If ICI can be widened to a loop invariant condition emits the loop
/// invariant condition in the loop preheader and return it, otherwise
/// returns std::nullopt.
std::optional<Value *>
LoopPredication::widenICmpRangeCheck(ICmpInst *ICI, SCEVExpander &Expander,
                                     Instruction *Guard) {
  LLVM_DEBUG(dbgs() << "Analyzing ICmpInst condition:\n");
  LLVM_DEBUG(ICI->dump());

  // parseLoopStructure guarantees that the latch condition is:
  //   ++i <pred> latchLimit, where <pred> is u<, u<=, s<, or s<=.
  // We are looking for the range checks of the form:
  //   i u< guardLimit
  auto RangeCheck = parseLoopICmp(ICI);
  if (!RangeCheck) {
    LLVM_DEBUG(dbgs() << "Failed to parse the loop latch condition!\n");
    return std::nullopt;
  }
  LLVM_DEBUG(dbgs() << "Guard check:\n");
  LLVM_DEBUG(RangeCheck->dump());
  if (RangeCheck->Pred != ICmpInst::ICMP_ULT) {
    LLVM_DEBUG(dbgs() << "Unsupported range check predicate("
                      << RangeCheck->Pred << ")!\n");
    return std::nullopt;
  }
  auto *RangeCheckIV = RangeCheck->IV;
  if (!RangeCheckIV->isAffine()) {
    LLVM_DEBUG(dbgs() << "Range check IV is not affine!\n");
    return std::nullopt;
  }
  auto *Step = RangeCheckIV->getStepRecurrence(*SE);
  // We cannot just compare with latch IV step because the latch and range IVs
  // may have different types.
  if (!isSupportedStep(Step)) {
    LLVM_DEBUG(dbgs() << "Range check and latch have IVs different steps!\n");
    return std::nullopt;
  }
  auto *Ty = RangeCheckIV->getType();
  auto CurrLatchCheckOpt = generateLoopLatchCheck(*DL, *SE, LatchCheck, Ty);
  if (!CurrLatchCheckOpt) {
    LLVM_DEBUG(dbgs() << "Failed to generate a loop latch check "
                         "corresponding to range type: "
                      << *Ty << "\n");
    return std::nullopt;
  }

  LoopICmp CurrLatchCheck = *CurrLatchCheckOpt;
  // At this point, the range and latch step should have the same type, but need
  // not have the same value (we support both 1 and -1 steps).
  assert(Step->getType() ==
             CurrLatchCheck.IV->getStepRecurrence(*SE)->getType() &&
         "Range and latch steps should be of same type!");
  if (Step != CurrLatchCheck.IV->getStepRecurrence(*SE)) {
    LLVM_DEBUG(dbgs() << "Range and latch have different step values!\n");
    return std::nullopt;
  }

  if (Step->isOne())
    return widenICmpRangeCheckIncrementingLoop(CurrLatchCheck, *RangeCheck,
                                               Expander, Guard);
  else {
    assert(Step->isAllOnesValue() && "Step should be -1!");
    return widenICmpRangeCheckDecrementingLoop(CurrLatchCheck, *RangeCheck,
                                               Expander, Guard);
  }
}

void LoopPredication::widenChecks(SmallVectorImpl<Value *> &Checks,
                                  SmallVectorImpl<Value *> &WidenedChecks,
                                  SCEVExpander &Expander, Instruction *Guard) {
  for (auto &Check : Checks)
    if (ICmpInst *ICI = dyn_cast<ICmpInst>(Check))
      if (auto NewRangeCheck = widenICmpRangeCheck(ICI, Expander, Guard)) {
        WidenedChecks.push_back(Check);
        Check = *NewRangeCheck;
      }
}

bool LoopPredication::widenGuardConditions(IntrinsicInst *Guard,
                                           SCEVExpander &Expander) {
  LLVM_DEBUG(dbgs() << "Processing guard:\n");
  LLVM_DEBUG(Guard->dump());

  TotalConsidered++;
  SmallVector<Value *, 4> Checks;
  SmallVector<Value *> WidenedChecks;
  parseWidenableGuard(Guard, Checks);
  widenChecks(Checks, WidenedChecks, Expander, Guard);
  if (WidenedChecks.empty())
    return false;

  TotalWidened += WidenedChecks.size();

  // Emit the new guard condition
  IRBuilder<> Builder(findInsertPt(Guard, Checks));
  Value *AllChecks = Builder.CreateAnd(Checks);
  auto *OldCond = Guard->getOperand(0);
  Guard->setOperand(0, AllChecks);
  if (InsertAssumesOfPredicatedGuardsConditions) {
    Builder.SetInsertPoint(&*++BasicBlock::iterator(Guard));
    Builder.CreateAssumption(OldCond);
  }
  RecursivelyDeleteTriviallyDeadInstructions(OldCond, nullptr /* TLI */, MSSAU);

  LLVM_DEBUG(dbgs() << "Widened checks = " << WidenedChecks.size() << "\n");
  return true;
}

bool LoopPredication::widenWidenableBranchGuardConditions(
    BranchInst *BI, SCEVExpander &Expander) {
  assert(isGuardAsWidenableBranch(BI) && "Must be!");
  LLVM_DEBUG(dbgs() << "Processing guard:\n");
  LLVM_DEBUG(BI->dump());

  TotalConsidered++;
  SmallVector<Value *, 4> Checks;
  SmallVector<Value *> WidenedChecks;
  parseWidenableGuard(BI, Checks);
  // At the moment, our matching logic for wideable conditions implicitly
  // assumes we preserve the form: (br (and Cond, WC())).  FIXME
  auto WC = extractWidenableCondition(BI);
  Checks.push_back(WC);
  widenChecks(Checks, WidenedChecks, Expander, BI);
  if (WidenedChecks.empty())
    return false;

  TotalWidened += WidenedChecks.size();

  // Emit the new guard condition
  IRBuilder<> Builder(findInsertPt(BI, Checks));
  Value *AllChecks = Builder.CreateAnd(Checks);
  auto *OldCond = BI->getCondition();
  BI->setCondition(AllChecks);
  if (InsertAssumesOfPredicatedGuardsConditions) {
    BasicBlock *IfTrueBB = BI->getSuccessor(0);
    Builder.SetInsertPoint(IfTrueBB, IfTrueBB->getFirstInsertionPt());
    // If this block has other predecessors, we might not be able to use Cond.
    // In this case, create a Phi where every other input is `true` and input
    // from guard block is Cond.
    Value *AssumeCond = Builder.CreateAnd(WidenedChecks);
    if (!IfTrueBB->getUniquePredecessor()) {
      auto *GuardBB = BI->getParent();
      auto *PN = Builder.CreatePHI(AssumeCond->getType(), pred_size(IfTrueBB),
                                   "assume.cond");
      for (auto *Pred : predecessors(IfTrueBB))
        PN->addIncoming(Pred == GuardBB ? AssumeCond : Builder.getTrue(), Pred);
      AssumeCond = PN;
    }
    Builder.CreateAssumption(AssumeCond);
  }
  RecursivelyDeleteTriviallyDeadInstructions(OldCond, nullptr /* TLI */, MSSAU);
  assert(isGuardAsWidenableBranch(BI) &&
         "Stopped being a guard after transform?");

  LLVM_DEBUG(dbgs() << "Widened checks = " << WidenedChecks.size() << "\n");
  return true;
}

std::optional<LoopICmp> LoopPredication::parseLoopLatchICmp() {
  using namespace PatternMatch;

  BasicBlock *LoopLatch = L->getLoopLatch();
  if (!LoopLatch) {
    LLVM_DEBUG(dbgs() << "The loop doesn't have a single latch!\n");
    return std::nullopt;
  }

  auto *BI = dyn_cast<BranchInst>(LoopLatch->getTerminator());
  if (!BI || !BI->isConditional()) {
    LLVM_DEBUG(dbgs() << "Failed to match the latch terminator!\n");
    return std::nullopt;
  }
  BasicBlock *TrueDest = BI->getSuccessor(0);
  assert(
      (TrueDest == L->getHeader() || BI->getSuccessor(1) == L->getHeader()) &&
      "One of the latch's destinations must be the header");

  auto *ICI = dyn_cast<ICmpInst>(BI->getCondition());
  if (!ICI) {
    LLVM_DEBUG(dbgs() << "Failed to match the latch condition!\n");
    return std::nullopt;
  }
  auto Result = parseLoopICmp(ICI);
  if (!Result) {
    LLVM_DEBUG(dbgs() << "Failed to parse the loop latch condition!\n");
    return std::nullopt;
  }

  if (TrueDest != L->getHeader())
    Result->Pred = ICmpInst::getInversePredicate(Result->Pred);

  // Check affine first, so if it's not we don't try to compute the step
  // recurrence.
  if (!Result->IV->isAffine()) {
    LLVM_DEBUG(dbgs() << "The induction variable is not affine!\n");
    return std::nullopt;
  }

  auto *Step = Result->IV->getStepRecurrence(*SE);
  if (!isSupportedStep(Step)) {
    LLVM_DEBUG(dbgs() << "Unsupported loop stride(" << *Step << ")!\n");
    return std::nullopt;
  }

  auto IsUnsupportedPredicate = [](const SCEV *Step, ICmpInst::Predicate Pred) {
    if (Step->isOne()) {
      return Pred != ICmpInst::ICMP_ULT && Pred != ICmpInst::ICMP_SLT &&
             Pred != ICmpInst::ICMP_ULE && Pred != ICmpInst::ICMP_SLE;
    } else {
      assert(Step->isAllOnesValue() && "Step should be -1!");
      return Pred != ICmpInst::ICMP_UGT && Pred != ICmpInst::ICMP_SGT &&
             Pred != ICmpInst::ICMP_UGE && Pred != ICmpInst::ICMP_SGE;
    }
  };

  normalizePredicate(SE, L, *Result);
  if (IsUnsupportedPredicate(Step, Result->Pred)) {
    LLVM_DEBUG(dbgs() << "Unsupported loop latch predicate(" << Result->Pred
                      << ")!\n");
    return std::nullopt;
  }

  return Result;
}

bool LoopPredication::isLoopProfitableToPredicate() {
  if (SkipProfitabilityChecks)
    return true;

  SmallVector<std::pair<BasicBlock *, BasicBlock *>, 8> ExitEdges;
  L->getExitEdges(ExitEdges);
  // If there is only one exiting edge in the loop, it is always profitable to
  // predicate the loop.
  if (ExitEdges.size() == 1)
    return true;

  // Calculate the exiting probabilities of all exiting edges from the loop,
  // starting with the LatchExitProbability.
  // Heuristic for profitability: If any of the exiting blocks' probability of
  // exiting the loop is larger than exiting through the latch block, it's not
  // profitable to predicate the loop.
  auto *LatchBlock = L->getLoopLatch();
  assert(LatchBlock && "Should have a single latch at this point!");
  auto *LatchTerm = LatchBlock->getTerminator();
  assert(LatchTerm->getNumSuccessors() == 2 &&
         "expected to be an exiting block with 2 succs!");
  unsigned LatchBrExitIdx =
      LatchTerm->getSuccessor(0) == L->getHeader() ? 1 : 0;
  // We compute branch probabilities without BPI. We do not rely on BPI since
  // Loop predication is usually run in an LPM and BPI is only preserved
  // lossily within loop pass managers, while BPI has an inherent notion of
  // being complete for an entire function.

  // If the latch exits into a deoptimize or an unreachable block, do not
  // predicate on that latch check.
  auto *LatchExitBlock = LatchTerm->getSuccessor(LatchBrExitIdx);
  if (isa<UnreachableInst>(LatchTerm) ||
      LatchExitBlock->getTerminatingDeoptimizeCall())
    return false;

  // Latch terminator has no valid profile data, so nothing to check
  // profitability on.
  if (!hasValidBranchWeightMD(*LatchTerm))
    return true;

  auto ComputeBranchProbability =
      [&](const BasicBlock *ExitingBlock,
          const BasicBlock *ExitBlock) -> BranchProbability {
    auto *Term = ExitingBlock->getTerminator();
    unsigned NumSucc = Term->getNumSuccessors();
    if (MDNode *ProfileData = getValidBranchWeightMDNode(*Term)) {
      SmallVector<uint32_t> Weights;
      extractBranchWeights(ProfileData, Weights);
      uint64_t Numerator = 0, Denominator = 0;
      for (auto [i, Weight] : llvm::enumerate(Weights)) {
        if (Term->getSuccessor(i) == ExitBlock)
          Numerator += Weight;
        Denominator += Weight;
      }
      // If all weights are zero act as if there was no profile data
      if (Denominator == 0)
        return BranchProbability::getBranchProbability(1, NumSucc);
      return BranchProbability::getBranchProbability(Numerator, Denominator);
    } else {
      assert(LatchBlock != ExitingBlock &&
             "Latch term should always have profile data!");
      // No profile data, so we choose the weight as 1/num_of_succ(Src)
      return BranchProbability::getBranchProbability(1, NumSucc);
    }
  };

  BranchProbability LatchExitProbability =
      ComputeBranchProbability(LatchBlock, LatchExitBlock);

  // Protect against degenerate inputs provided by the user. Providing a value
  // less than one, can invert the definition of profitable loop predication.
  float ScaleFactor = LatchExitProbabilityScale;
  if (ScaleFactor < 1) {
    LLVM_DEBUG(
        dbgs()
        << "Ignored user setting for loop-predication-latch-probability-scale: "
        << LatchExitProbabilityScale << "\n");
    LLVM_DEBUG(dbgs() << "The value is set to 1.0\n");
    ScaleFactor = 1.0;
  }
  const auto LatchProbabilityThreshold = LatchExitProbability * ScaleFactor;

  for (const auto &ExitEdge : ExitEdges) {
    BranchProbability ExitingBlockProbability =
        ComputeBranchProbability(ExitEdge.first, ExitEdge.second);
    // Some exiting edge has higher probability than the latch exiting edge.
    // No longer profitable to predicate.
    if (ExitingBlockProbability > LatchProbabilityThreshold)
      return false;
  }

  // We have concluded that the most probable way to exit from the
  // loop is through the latch (or there's no profile information and all
  // exits are equally likely).
  return true;
}

/// If we can (cheaply) find a widenable branch which controls entry into the
/// loop, return it.
static BranchInst *FindWidenableTerminatorAboveLoop(Loop *L, LoopInfo &LI) {
  // Walk back through any unconditional executed blocks and see if we can find
  // a widenable condition which seems to control execution of this loop.  Note
  // that we predict that maythrow calls are likely untaken and thus that it's
  // profitable to widen a branch before a maythrow call with a condition
  // afterwards even though that may cause the slow path to run in a case where
  // it wouldn't have otherwise.
  BasicBlock *BB = L->getLoopPreheader();
  if (!BB)
    return nullptr;
  do {
    if (BasicBlock *Pred = BB->getSinglePredecessor())
      if (BB == Pred->getSingleSuccessor()) {
        BB = Pred;
        continue;
      }
    break;
  } while (true);

  if (BasicBlock *Pred = BB->getSinglePredecessor()) {
    if (auto *BI = dyn_cast<BranchInst>(Pred->getTerminator()))
      if (BI->getSuccessor(0) == BB && isWidenableBranch(BI))
        return BI;
  }
  return nullptr;
}

/// Return the minimum of all analyzeable exit counts.  This is an upper bound
/// on the actual exit count.  If there are not at least two analyzeable exits,
/// returns SCEVCouldNotCompute.
static const SCEV *getMinAnalyzeableBackedgeTakenCount(ScalarEvolution &SE,
                                                       DominatorTree &DT,
                                                       Loop *L) {
  SmallVector<BasicBlock *, 16> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  SmallVector<const SCEV *, 4> ExitCounts;
  for (BasicBlock *ExitingBB : ExitingBlocks) {
    const SCEV *ExitCount = SE.getExitCount(L, ExitingBB);
    if (isa<SCEVCouldNotCompute>(ExitCount))
      continue;
    assert(DT.dominates(ExitingBB, L->getLoopLatch()) &&
           "We should only have known counts for exiting blocks that "
           "dominate latch!");
    ExitCounts.push_back(ExitCount);
  }
  if (ExitCounts.size() < 2)
    return SE.getCouldNotCompute();
  return SE.getUMinFromMismatchedTypes(ExitCounts);
}

/// This implements an analogous, but entirely distinct transform from the main
/// loop predication transform.  This one is phrased in terms of using a
/// widenable branch *outside* the loop to allow us to simplify loop exits in a
/// following loop.  This is close in spirit to the IndVarSimplify transform
/// of the same name, but is materially different widening loosens legality
/// sharply.
bool LoopPredication::predicateLoopExits(Loop *L, SCEVExpander &Rewriter) {
  // The transformation performed here aims to widen a widenable condition
  // above the loop such that all analyzeable exit leading to deopt are dead.
  // It assumes that the latch is the dominant exit for profitability and that
  // exits branching to deoptimizing blocks are rarely taken. It relies on the
  // semantics of widenable expressions for legality. (i.e. being able to fall
  // down the widenable path spuriously allows us to ignore exit order,
  // unanalyzeable exits, side effects, exceptional exits, and other challenges
  // which restrict the applicability of the non-WC based version of this
  // transform in IndVarSimplify.)
  //
  // NOTE ON POISON/UNDEF - We're hoisting an expression above guards which may
  // imply flags on the expression being hoisted and inserting new uses (flags
  // are only correct for current uses).  The result is that we may be
  // inserting a branch on the value which can be either poison or undef.  In
  // this case, the branch can legally go either way; we just need to avoid
  // introducing UB.  This is achieved through the use of the freeze
  // instruction.

  SmallVector<BasicBlock *, 16> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  if (ExitingBlocks.empty())
    return false; // Nothing to do.

  auto *Latch = L->getLoopLatch();
  if (!Latch)
    return false;

  auto *WidenableBR = FindWidenableTerminatorAboveLoop(L, *LI);
  if (!WidenableBR)
    return false;

  const SCEV *LatchEC = SE->getExitCount(L, Latch);
  if (isa<SCEVCouldNotCompute>(LatchEC))
    return false; // profitability - want hot exit in analyzeable set

  // At this point, we have found an analyzeable latch, and a widenable
  // condition above the loop.  If we have a widenable exit within the loop
  // (for which we can't compute exit counts), drop the ability to further
  // widen so that we gain ability to analyze it's exit count and perform this
  // transform.  TODO: It'd be nice to know for sure the exit became
  // analyzeable after dropping widenability.
  bool ChangedLoop = false;

  for (auto *ExitingBB : ExitingBlocks) {
    if (LI->getLoopFor(ExitingBB) != L)
      continue;

    auto *BI = dyn_cast<BranchInst>(ExitingBB->getTerminator());
    if (!BI)
      continue;

    if (auto WC = extractWidenableCondition(BI))
      if (L->contains(BI->getSuccessor(0))) {
        assert(WC->hasOneUse() && "Not appropriate widenable branch!");
        WC->user_back()->replaceUsesOfWith(
            WC, ConstantInt::getTrue(BI->getContext()));
        ChangedLoop = true;
      }
  }
  if (ChangedLoop)
    SE->forgetLoop(L);

  // The insertion point for the widening should be at the widenably call, not
  // at the WidenableBR. If we do this at the widenableBR, we can incorrectly
  // change a loop-invariant condition to a loop-varying one.
  auto *IP = cast<Instruction>(WidenableBR->getCondition());

  // The use of umin(all analyzeable exits) instead of latch is subtle, but
  // important for profitability.  We may have a loop which hasn't been fully
  // canonicalized just yet.  If the exit we chose to widen is provably never
  // taken, we want the widened form to *also* be provably never taken.  We
  // can't guarantee this as a current unanalyzeable exit may later become
  // analyzeable, but we can at least avoid the obvious cases.
  const SCEV *MinEC = getMinAnalyzeableBackedgeTakenCount(*SE, *DT, L);
  if (isa<SCEVCouldNotCompute>(MinEC) || MinEC->getType()->isPointerTy() ||
      !SE->isLoopInvariant(MinEC, L) ||
      !Rewriter.isSafeToExpandAt(MinEC, IP))
    return ChangedLoop;

  Rewriter.setInsertPoint(IP);
  IRBuilder<> B(IP);

  bool InvalidateLoop = false;
  Value *MinECV = nullptr; // lazily generated if needed
  for (BasicBlock *ExitingBB : ExitingBlocks) {
    // If our exiting block exits multiple loops, we can only rewrite the
    // innermost one.  Otherwise, we're changing how many times the innermost
    // loop runs before it exits.
    if (LI->getLoopFor(ExitingBB) != L)
      continue;

    // Can't rewrite non-branch yet.
    auto *BI = dyn_cast<BranchInst>(ExitingBB->getTerminator());
    if (!BI)
      continue;

    // If already constant, nothing to do.
    if (isa<Constant>(BI->getCondition()))
      continue;

    const SCEV *ExitCount = SE->getExitCount(L, ExitingBB);
    if (isa<SCEVCouldNotCompute>(ExitCount) ||
        ExitCount->getType()->isPointerTy() ||
        !Rewriter.isSafeToExpandAt(ExitCount, WidenableBR))
      continue;

    const bool ExitIfTrue = !L->contains(*succ_begin(ExitingBB));
    BasicBlock *ExitBB = BI->getSuccessor(ExitIfTrue ? 0 : 1);
    if (!ExitBB->getPostdominatingDeoptimizeCall())
      continue;

    /// Here we can be fairly sure that executing this exit will most likely
    /// lead to executing llvm.experimental.deoptimize.
    /// This is a profitability heuristic, not a legality constraint.

    // If we found a widenable exit condition, do two things:
    // 1) fold the widened exit test into the widenable condition
    // 2) fold the branch to untaken - avoids infinite looping

    Value *ECV = Rewriter.expandCodeFor(ExitCount);
    if (!MinECV)
      MinECV = Rewriter.expandCodeFor(MinEC);
    Value *RHS = MinECV;
    if (ECV->getType() != RHS->getType()) {
      Type *WiderTy = SE->getWiderType(ECV->getType(), RHS->getType());
      ECV = B.CreateZExt(ECV, WiderTy);
      RHS = B.CreateZExt(RHS, WiderTy);
    }
    assert(!Latch || DT->dominates(ExitingBB, Latch));
    Value *NewCond = B.CreateICmp(ICmpInst::ICMP_UGT, ECV, RHS);
    // Freeze poison or undef to an arbitrary bit pattern to ensure we can
    // branch without introducing UB.  See NOTE ON POISON/UNDEF above for
    // context.
    NewCond = B.CreateFreeze(NewCond);

    widenWidenableBranch(WidenableBR, NewCond);

    Value *OldCond = BI->getCondition();
    BI->setCondition(ConstantInt::get(OldCond->getType(), !ExitIfTrue));
    InvalidateLoop = true;
  }

  if (InvalidateLoop)
    // We just mutated a bunch of loop exits changing there exit counts
    // widely.  We need to force recomputation of the exit counts given these
    // changes.  Note that all of the inserted exits are never taken, and
    // should be removed next time the CFG is modified.
    SE->forgetLoop(L);

  // Always return `true` since we have moved the WidenableBR's condition.
  return true;
}

bool LoopPredication::runOnLoop(Loop *Loop) {
  L = Loop;

  LLVM_DEBUG(dbgs() << "Analyzing ");
  LLVM_DEBUG(L->dump());

  Module *M = L->getHeader()->getModule();

  // There is nothing to do if the module doesn't use guards
  auto *GuardDecl =
      M->getFunction(Intrinsic::getName(Intrinsic::experimental_guard));
  bool HasIntrinsicGuards = GuardDecl && !GuardDecl->use_empty();
  auto *WCDecl = M->getFunction(
      Intrinsic::getName(Intrinsic::experimental_widenable_condition));
  bool HasWidenableConditions =
      PredicateWidenableBranchGuards && WCDecl && !WCDecl->use_empty();
  if (!HasIntrinsicGuards && !HasWidenableConditions)
    return false;

  DL = &M->getDataLayout();

  Preheader = L->getLoopPreheader();
  if (!Preheader)
    return false;

  auto LatchCheckOpt = parseLoopLatchICmp();
  if (!LatchCheckOpt)
    return false;
  LatchCheck = *LatchCheckOpt;

  LLVM_DEBUG(dbgs() << "Latch check:\n");
  LLVM_DEBUG(LatchCheck.dump());

  if (!isLoopProfitableToPredicate()) {
    LLVM_DEBUG(dbgs() << "Loop not profitable to predicate!\n");
    return false;
  }
  // Collect all the guards into a vector and process later, so as not
  // to invalidate the instruction iterator.
  SmallVector<IntrinsicInst *, 4> Guards;
  SmallVector<BranchInst *, 4> GuardsAsWidenableBranches;
  for (const auto BB : L->blocks()) {
    for (auto &I : *BB)
      if (isGuard(&I))
        Guards.push_back(cast<IntrinsicInst>(&I));
    if (PredicateWidenableBranchGuards &&
        isGuardAsWidenableBranch(BB->getTerminator()))
      GuardsAsWidenableBranches.push_back(
          cast<BranchInst>(BB->getTerminator()));
  }

  SCEVExpander Expander(*SE, *DL, "loop-predication");
  bool Changed = false;
  for (auto *Guard : Guards)
    Changed |= widenGuardConditions(Guard, Expander);
  for (auto *Guard : GuardsAsWidenableBranches)
    Changed |= widenWidenableBranchGuardConditions(Guard, Expander);
  Changed |= predicateLoopExits(L, Expander);

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();
  return Changed;
}
