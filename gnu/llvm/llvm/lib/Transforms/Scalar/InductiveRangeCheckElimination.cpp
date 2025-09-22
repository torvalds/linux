//===- InductiveRangeCheckElimination.cpp - -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The InductiveRangeCheckElimination pass splits a loop's iteration space into
// three disjoint ranges.  It does that in a way such that the loop running in
// the middle loop provably does not need range checks. As an example, it will
// convert
//
//   len = < known positive >
//   for (i = 0; i < n; i++) {
//     if (0 <= i && i < len) {
//       do_something();
//     } else {
//       throw_out_of_bounds();
//     }
//   }
//
// to
//
//   len = < known positive >
//   limit = smin(n, len)
//   // no first segment
//   for (i = 0; i < limit; i++) {
//     if (0 <= i && i < len) { // this check is fully redundant
//       do_something();
//     } else {
//       throw_out_of_bounds();
//     }
//   }
//   for (i = limit; i < n; i++) {
//     if (0 <= i && i < len) {
//       do_something();
//     } else {
//       throw_out_of_bounds();
//     }
//   }
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/InductiveRangeCheckElimination.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopConstrainer.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <optional>
#include <utility>

using namespace llvm;
using namespace llvm::PatternMatch;

static cl::opt<unsigned> LoopSizeCutoff("irce-loop-size-cutoff", cl::Hidden,
                                        cl::init(64));

static cl::opt<bool> PrintChangedLoops("irce-print-changed-loops", cl::Hidden,
                                       cl::init(false));

static cl::opt<bool> PrintRangeChecks("irce-print-range-checks", cl::Hidden,
                                      cl::init(false));

static cl::opt<bool> SkipProfitabilityChecks("irce-skip-profitability-checks",
                                             cl::Hidden, cl::init(false));

static cl::opt<unsigned> MinRuntimeIterations("irce-min-runtime-iterations",
                                              cl::Hidden, cl::init(10));

static cl::opt<bool> AllowUnsignedLatchCondition("irce-allow-unsigned-latch",
                                                 cl::Hidden, cl::init(true));

static cl::opt<bool> AllowNarrowLatchCondition(
    "irce-allow-narrow-latch", cl::Hidden, cl::init(true),
    cl::desc("If set to true, IRCE may eliminate wide range checks in loops "
             "with narrow latch condition."));

static cl::opt<unsigned> MaxTypeSizeForOverflowCheck(
    "irce-max-type-size-for-overflow-check", cl::Hidden, cl::init(32),
    cl::desc(
        "Maximum size of range check type for which can be produced runtime "
        "overflow check of its limit's computation"));

static cl::opt<bool>
    PrintScaledBoundaryRangeChecks("irce-print-scaled-boundary-range-checks",
                                   cl::Hidden, cl::init(false));

#define DEBUG_TYPE "irce"

namespace {

/// An inductive range check is conditional branch in a loop with
///
///  1. a very cold successor (i.e. the branch jumps to that successor very
///     rarely)
///
///  and
///
///  2. a condition that is provably true for some contiguous range of values
///     taken by the containing loop's induction variable.
///
class InductiveRangeCheck {

  const SCEV *Begin = nullptr;
  const SCEV *Step = nullptr;
  const SCEV *End = nullptr;
  Use *CheckUse = nullptr;

  static bool parseRangeCheckICmp(Loop *L, ICmpInst *ICI, ScalarEvolution &SE,
                                  const SCEVAddRecExpr *&Index,
                                  const SCEV *&End);

  static void
  extractRangeChecksFromCond(Loop *L, ScalarEvolution &SE, Use &ConditionUse,
                             SmallVectorImpl<InductiveRangeCheck> &Checks,
                             SmallPtrSetImpl<Value *> &Visited);

  static bool parseIvAgaisntLimit(Loop *L, Value *LHS, Value *RHS,
                                  ICmpInst::Predicate Pred, ScalarEvolution &SE,
                                  const SCEVAddRecExpr *&Index,
                                  const SCEV *&End);

  static bool reassociateSubLHS(Loop *L, Value *VariantLHS, Value *InvariantRHS,
                                ICmpInst::Predicate Pred, ScalarEvolution &SE,
                                const SCEVAddRecExpr *&Index, const SCEV *&End);

public:
  const SCEV *getBegin() const { return Begin; }
  const SCEV *getStep() const { return Step; }
  const SCEV *getEnd() const { return End; }

  void print(raw_ostream &OS) const {
    OS << "InductiveRangeCheck:\n";
    OS << "  Begin: ";
    Begin->print(OS);
    OS << "  Step: ";
    Step->print(OS);
    OS << "  End: ";
    End->print(OS);
    OS << "\n  CheckUse: ";
    getCheckUse()->getUser()->print(OS);
    OS << " Operand: " << getCheckUse()->getOperandNo() << "\n";
  }

  LLVM_DUMP_METHOD
  void dump() {
    print(dbgs());
  }

  Use *getCheckUse() const { return CheckUse; }

  /// Represents an signed integer range [Range.getBegin(), Range.getEnd()).  If
  /// R.getEnd() le R.getBegin(), then R denotes the empty range.

  class Range {
    const SCEV *Begin;
    const SCEV *End;

  public:
    Range(const SCEV *Begin, const SCEV *End) : Begin(Begin), End(End) {
      assert(Begin->getType() == End->getType() && "ill-typed range!");
    }

    Type *getType() const { return Begin->getType(); }
    const SCEV *getBegin() const { return Begin; }
    const SCEV *getEnd() const { return End; }
    bool isEmpty(ScalarEvolution &SE, bool IsSigned) const {
      if (Begin == End)
        return true;
      if (IsSigned)
        return SE.isKnownPredicate(ICmpInst::ICMP_SGE, Begin, End);
      else
        return SE.isKnownPredicate(ICmpInst::ICMP_UGE, Begin, End);
    }
  };

  /// This is the value the condition of the branch needs to evaluate to for the
  /// branch to take the hot successor (see (1) above).
  bool getPassingDirection() { return true; }

  /// Computes a range for the induction variable (IndVar) in which the range
  /// check is redundant and can be constant-folded away.  The induction
  /// variable is not required to be the canonical {0,+,1} induction variable.
  std::optional<Range> computeSafeIterationSpace(ScalarEvolution &SE,
                                                 const SCEVAddRecExpr *IndVar,
                                                 bool IsLatchSigned) const;

  /// Parse out a set of inductive range checks from \p BI and append them to \p
  /// Checks.
  ///
  /// NB! There may be conditions feeding into \p BI that aren't inductive range
  /// checks, and hence don't end up in \p Checks.
  static void extractRangeChecksFromBranch(
      BranchInst *BI, Loop *L, ScalarEvolution &SE, BranchProbabilityInfo *BPI,
      SmallVectorImpl<InductiveRangeCheck> &Checks, bool &Changed);
};

class InductiveRangeCheckElimination {
  ScalarEvolution &SE;
  BranchProbabilityInfo *BPI;
  DominatorTree &DT;
  LoopInfo &LI;

  using GetBFIFunc =
      std::optional<llvm::function_ref<llvm::BlockFrequencyInfo &()>>;
  GetBFIFunc GetBFI;

  // Returns true if it is profitable to do a transform basing on estimation of
  // number of iterations.
  bool isProfitableToTransform(const Loop &L, LoopStructure &LS);

public:
  InductiveRangeCheckElimination(ScalarEvolution &SE,
                                 BranchProbabilityInfo *BPI, DominatorTree &DT,
                                 LoopInfo &LI, GetBFIFunc GetBFI = std::nullopt)
      : SE(SE), BPI(BPI), DT(DT), LI(LI), GetBFI(GetBFI) {}

  bool run(Loop *L, function_ref<void(Loop *, bool)> LPMAddNewLoop);
};

} // end anonymous namespace

/// Parse a single ICmp instruction, `ICI`, into a range check.  If `ICI` cannot
/// be interpreted as a range check, return false.  Otherwise set `Index` to the
/// SCEV being range checked, and set `End` to the upper or lower limit `Index`
/// is being range checked.
bool InductiveRangeCheck::parseRangeCheckICmp(Loop *L, ICmpInst *ICI,
                                              ScalarEvolution &SE,
                                              const SCEVAddRecExpr *&Index,
                                              const SCEV *&End) {
  auto IsLoopInvariant = [&SE, L](Value *V) {
    return SE.isLoopInvariant(SE.getSCEV(V), L);
  };

  ICmpInst::Predicate Pred = ICI->getPredicate();
  Value *LHS = ICI->getOperand(0);
  Value *RHS = ICI->getOperand(1);

  if (!LHS->getType()->isIntegerTy())
    return false;

  // Canonicalize to the `Index Pred Invariant` comparison
  if (IsLoopInvariant(LHS)) {
    std::swap(LHS, RHS);
    Pred = CmpInst::getSwappedPredicate(Pred);
  } else if (!IsLoopInvariant(RHS))
    // Both LHS and RHS are loop variant
    return false;

  if (parseIvAgaisntLimit(L, LHS, RHS, Pred, SE, Index, End))
    return true;

  if (reassociateSubLHS(L, LHS, RHS, Pred, SE, Index, End))
    return true;

  // TODO: support ReassociateAddLHS
  return false;
}

// Try to parse range check in the form of "IV vs Limit"
bool InductiveRangeCheck::parseIvAgaisntLimit(Loop *L, Value *LHS, Value *RHS,
                                              ICmpInst::Predicate Pred,
                                              ScalarEvolution &SE,
                                              const SCEVAddRecExpr *&Index,
                                              const SCEV *&End) {

  auto SIntMaxSCEV = [&](Type *T) {
    unsigned BitWidth = cast<IntegerType>(T)->getBitWidth();
    return SE.getConstant(APInt::getSignedMaxValue(BitWidth));
  };

  const auto *AddRec = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(LHS));
  if (!AddRec)
    return false;

  // We strengthen "0 <= I" to "0 <= I < INT_SMAX" and "I < L" to "0 <= I < L".
  // We can potentially do much better here.
  // If we want to adjust upper bound for the unsigned range check as we do it
  // for signed one, we will need to pick Unsigned max
  switch (Pred) {
  default:
    return false;

  case ICmpInst::ICMP_SGE:
    if (match(RHS, m_ConstantInt<0>())) {
      Index = AddRec;
      End = SIntMaxSCEV(Index->getType());
      return true;
    }
    return false;

  case ICmpInst::ICMP_SGT:
    if (match(RHS, m_ConstantInt<-1>())) {
      Index = AddRec;
      End = SIntMaxSCEV(Index->getType());
      return true;
    }
    return false;

  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_ULT:
    Index = AddRec;
    End = SE.getSCEV(RHS);
    return true;

  case ICmpInst::ICMP_SLE:
  case ICmpInst::ICMP_ULE:
    const SCEV *One = SE.getOne(RHS->getType());
    const SCEV *RHSS = SE.getSCEV(RHS);
    bool Signed = Pred == ICmpInst::ICMP_SLE;
    if (SE.willNotOverflow(Instruction::BinaryOps::Add, Signed, RHSS, One)) {
      Index = AddRec;
      End = SE.getAddExpr(RHSS, One);
      return true;
    }
    return false;
  }

  llvm_unreachable("default clause returns!");
}

// Try to parse range check in the form of "IV - Offset vs Limit" or "Offset -
// IV vs Limit"
bool InductiveRangeCheck::reassociateSubLHS(
    Loop *L, Value *VariantLHS, Value *InvariantRHS, ICmpInst::Predicate Pred,
    ScalarEvolution &SE, const SCEVAddRecExpr *&Index, const SCEV *&End) {
  Value *LHS, *RHS;
  if (!match(VariantLHS, m_Sub(m_Value(LHS), m_Value(RHS))))
    return false;

  const SCEV *IV = SE.getSCEV(LHS);
  const SCEV *Offset = SE.getSCEV(RHS);
  const SCEV *Limit = SE.getSCEV(InvariantRHS);

  bool OffsetSubtracted = false;
  if (SE.isLoopInvariant(IV, L))
    // "Offset - IV vs Limit"
    std::swap(IV, Offset);
  else if (SE.isLoopInvariant(Offset, L))
    // "IV - Offset vs Limit"
    OffsetSubtracted = true;
  else
    return false;

  const auto *AddRec = dyn_cast<SCEVAddRecExpr>(IV);
  if (!AddRec)
    return false;

  // In order to turn "IV - Offset < Limit" into "IV < Limit + Offset", we need
  // to be able to freely move values from left side of inequality to right side
  // (just as in normal linear arithmetics). Overflows make things much more
  // complicated, so we want to avoid this.
  //
  // Let's prove that the initial subtraction doesn't overflow with all IV's
  // values from the safe range constructed for that check.
  //
  // [Case 1] IV - Offset < Limit
  // It doesn't overflow if:
  //     SINT_MIN <= IV - Offset <= SINT_MAX
  // In terms of scaled SINT we need to prove:
  //     SINT_MIN + Offset <= IV <= SINT_MAX + Offset
  // Safe range will be constructed:
  //     0 <= IV < Limit + Offset
  // It means that 'IV - Offset' doesn't underflow, because:
  //     SINT_MIN + Offset < 0 <= IV
  // and doesn't overflow:
  //     IV < Limit + Offset <= SINT_MAX + Offset
  //
  // [Case 2] Offset - IV > Limit
  // It doesn't overflow if:
  //     SINT_MIN <= Offset - IV <= SINT_MAX
  // In terms of scaled SINT we need to prove:
  //     -SINT_MIN >= IV - Offset >= -SINT_MAX
  //     Offset - SINT_MIN >= IV >= Offset - SINT_MAX
  // Safe range will be constructed:
  //     0 <= IV < Offset - Limit
  // It means that 'Offset - IV' doesn't underflow, because
  //     Offset - SINT_MAX < 0 <= IV
  // and doesn't overflow:
  //     IV < Offset - Limit <= Offset - SINT_MIN
  //
  // For the computed upper boundary of the IV's range (Offset +/- Limit) we
  // don't know exactly whether it overflows or not. So if we can't prove this
  // fact at compile time, we scale boundary computations to a wider type with
  // the intention to add runtime overflow check.

  auto getExprScaledIfOverflow = [&](Instruction::BinaryOps BinOp,
                                     const SCEV *LHS,
                                     const SCEV *RHS) -> const SCEV * {
    const SCEV *(ScalarEvolution::*Operation)(const SCEV *, const SCEV *,
                                              SCEV::NoWrapFlags, unsigned);
    switch (BinOp) {
    default:
      llvm_unreachable("Unsupported binary op");
    case Instruction::Add:
      Operation = &ScalarEvolution::getAddExpr;
      break;
    case Instruction::Sub:
      Operation = &ScalarEvolution::getMinusSCEV;
      break;
    }

    if (SE.willNotOverflow(BinOp, ICmpInst::isSigned(Pred), LHS, RHS,
                           cast<Instruction>(VariantLHS)))
      return (SE.*Operation)(LHS, RHS, SCEV::FlagAnyWrap, 0);

    // We couldn't prove that the expression does not overflow.
    // Than scale it to a wider type to check overflow at runtime.
    auto *Ty = cast<IntegerType>(LHS->getType());
    if (Ty->getBitWidth() > MaxTypeSizeForOverflowCheck)
      return nullptr;

    auto WideTy = IntegerType::get(Ty->getContext(), Ty->getBitWidth() * 2);
    return (SE.*Operation)(SE.getSignExtendExpr(LHS, WideTy),
                           SE.getSignExtendExpr(RHS, WideTy), SCEV::FlagAnyWrap,
                           0);
  };

  if (OffsetSubtracted)
    // "IV - Offset < Limit" -> "IV" < Offset + Limit
    Limit = getExprScaledIfOverflow(Instruction::BinaryOps::Add, Offset, Limit);
  else {
    // "Offset - IV > Limit" -> "IV" < Offset - Limit
    Limit = getExprScaledIfOverflow(Instruction::BinaryOps::Sub, Offset, Limit);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE) {
    // "Expr <= Limit" -> "Expr < Limit + 1"
    if (Pred == ICmpInst::ICMP_SLE && Limit)
      Limit = getExprScaledIfOverflow(Instruction::BinaryOps::Add, Limit,
                                      SE.getOne(Limit->getType()));
    if (Limit) {
      Index = AddRec;
      End = Limit;
      return true;
    }
  }
  return false;
}

void InductiveRangeCheck::extractRangeChecksFromCond(
    Loop *L, ScalarEvolution &SE, Use &ConditionUse,
    SmallVectorImpl<InductiveRangeCheck> &Checks,
    SmallPtrSetImpl<Value *> &Visited) {
  Value *Condition = ConditionUse.get();
  if (!Visited.insert(Condition).second)
    return;

  // TODO: Do the same for OR, XOR, NOT etc?
  if (match(Condition, m_LogicalAnd(m_Value(), m_Value()))) {
    extractRangeChecksFromCond(L, SE, cast<User>(Condition)->getOperandUse(0),
                               Checks, Visited);
    extractRangeChecksFromCond(L, SE, cast<User>(Condition)->getOperandUse(1),
                               Checks, Visited);
    return;
  }

  ICmpInst *ICI = dyn_cast<ICmpInst>(Condition);
  if (!ICI)
    return;

  const SCEV *End = nullptr;
  const SCEVAddRecExpr *IndexAddRec = nullptr;
  if (!parseRangeCheckICmp(L, ICI, SE, IndexAddRec, End))
    return;

  assert(IndexAddRec && "IndexAddRec was not computed");
  assert(End && "End was not computed");

  if ((IndexAddRec->getLoop() != L) || !IndexAddRec->isAffine())
    return;

  InductiveRangeCheck IRC;
  IRC.End = End;
  IRC.Begin = IndexAddRec->getStart();
  IRC.Step = IndexAddRec->getStepRecurrence(SE);
  IRC.CheckUse = &ConditionUse;
  Checks.push_back(IRC);
}

void InductiveRangeCheck::extractRangeChecksFromBranch(
    BranchInst *BI, Loop *L, ScalarEvolution &SE, BranchProbabilityInfo *BPI,
    SmallVectorImpl<InductiveRangeCheck> &Checks, bool &Changed) {
  if (BI->isUnconditional() || BI->getParent() == L->getLoopLatch())
    return;

  unsigned IndexLoopSucc = L->contains(BI->getSuccessor(0)) ? 0 : 1;
  assert(L->contains(BI->getSuccessor(IndexLoopSucc)) &&
         "No edges coming to loop?");
  BranchProbability LikelyTaken(15, 16);

  if (!SkipProfitabilityChecks && BPI &&
      BPI->getEdgeProbability(BI->getParent(), IndexLoopSucc) < LikelyTaken)
    return;

  // IRCE expects branch's true edge comes to loop. Invert branch for opposite
  // case.
  if (IndexLoopSucc != 0) {
    IRBuilder<> Builder(BI);
    InvertBranch(BI, Builder);
    if (BPI)
      BPI->swapSuccEdgesProbabilities(BI->getParent());
    Changed = true;
  }

  SmallPtrSet<Value *, 8> Visited;
  InductiveRangeCheck::extractRangeChecksFromCond(L, SE, BI->getOperandUse(0),
                                                  Checks, Visited);
}

/// If the type of \p S matches with \p Ty, return \p S. Otherwise, return
/// signed or unsigned extension of \p S to type \p Ty.
static const SCEV *NoopOrExtend(const SCEV *S, Type *Ty, ScalarEvolution &SE,
                                bool Signed) {
  return Signed ? SE.getNoopOrSignExtend(S, Ty) : SE.getNoopOrZeroExtend(S, Ty);
}

// Compute a safe set of limits for the main loop to run in -- effectively the
// intersection of `Range' and the iteration space of the original loop.
// Return std::nullopt if unable to compute the set of subranges.
static std::optional<LoopConstrainer::SubRanges>
calculateSubRanges(ScalarEvolution &SE, const Loop &L,
                   InductiveRangeCheck::Range &Range,
                   const LoopStructure &MainLoopStructure) {
  auto *RTy = cast<IntegerType>(Range.getType());
  // We only support wide range checks and narrow latches.
  if (!AllowNarrowLatchCondition && RTy != MainLoopStructure.ExitCountTy)
    return std::nullopt;
  if (RTy->getBitWidth() < MainLoopStructure.ExitCountTy->getBitWidth())
    return std::nullopt;

  LoopConstrainer::SubRanges Result;

  bool IsSignedPredicate = MainLoopStructure.IsSignedPredicate;
  // I think we can be more aggressive here and make this nuw / nsw if the
  // addition that feeds into the icmp for the latch's terminating branch is nuw
  // / nsw.  In any case, a wrapping 2's complement addition is safe.
  const SCEV *Start = NoopOrExtend(SE.getSCEV(MainLoopStructure.IndVarStart),
                                   RTy, SE, IsSignedPredicate);
  const SCEV *End = NoopOrExtend(SE.getSCEV(MainLoopStructure.LoopExitAt), RTy,
                                 SE, IsSignedPredicate);

  bool Increasing = MainLoopStructure.IndVarIncreasing;

  // We compute `Smallest` and `Greatest` such that [Smallest, Greatest), or
  // [Smallest, GreatestSeen] is the range of values the induction variable
  // takes.

  const SCEV *Smallest = nullptr, *Greatest = nullptr, *GreatestSeen = nullptr;

  const SCEV *One = SE.getOne(RTy);
  if (Increasing) {
    Smallest = Start;
    Greatest = End;
    // No overflow, because the range [Smallest, GreatestSeen] is not empty.
    GreatestSeen = SE.getMinusSCEV(End, One);
  } else {
    // These two computations may sign-overflow.  Here is why that is okay:
    //
    // We know that the induction variable does not sign-overflow on any
    // iteration except the last one, and it starts at `Start` and ends at
    // `End`, decrementing by one every time.
    //
    //  * if `Smallest` sign-overflows we know `End` is `INT_SMAX`. Since the
    //    induction variable is decreasing we know that the smallest value
    //    the loop body is actually executed with is `INT_SMIN` == `Smallest`.
    //
    //  * if `Greatest` sign-overflows, we know it can only be `INT_SMIN`.  In
    //    that case, `Clamp` will always return `Smallest` and
    //    [`Result.LowLimit`, `Result.HighLimit`) = [`Smallest`, `Smallest`)
    //    will be an empty range.  Returning an empty range is always safe.

    Smallest = SE.getAddExpr(End, One);
    Greatest = SE.getAddExpr(Start, One);
    GreatestSeen = Start;
  }

  auto Clamp = [&SE, Smallest, Greatest, IsSignedPredicate](const SCEV *S) {
    return IsSignedPredicate
               ? SE.getSMaxExpr(Smallest, SE.getSMinExpr(Greatest, S))
               : SE.getUMaxExpr(Smallest, SE.getUMinExpr(Greatest, S));
  };

  // In some cases we can prove that we don't need a pre or post loop.
  ICmpInst::Predicate PredLE =
      IsSignedPredicate ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE;
  ICmpInst::Predicate PredLT =
      IsSignedPredicate ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;

  bool ProvablyNoPreloop =
      SE.isKnownPredicate(PredLE, Range.getBegin(), Smallest);
  if (!ProvablyNoPreloop)
    Result.LowLimit = Clamp(Range.getBegin());

  bool ProvablyNoPostLoop =
      SE.isKnownPredicate(PredLT, GreatestSeen, Range.getEnd());
  if (!ProvablyNoPostLoop)
    Result.HighLimit = Clamp(Range.getEnd());

  return Result;
}

/// Computes and returns a range of values for the induction variable (IndVar)
/// in which the range check can be safely elided.  If it cannot compute such a
/// range, returns std::nullopt.
std::optional<InductiveRangeCheck::Range>
InductiveRangeCheck::computeSafeIterationSpace(ScalarEvolution &SE,
                                               const SCEVAddRecExpr *IndVar,
                                               bool IsLatchSigned) const {
  // We can deal when types of latch check and range checks don't match in case
  // if latch check is more narrow.
  auto *IVType = dyn_cast<IntegerType>(IndVar->getType());
  auto *RCType = dyn_cast<IntegerType>(getBegin()->getType());
  auto *EndType = dyn_cast<IntegerType>(getEnd()->getType());
  // Do not work with pointer types.
  if (!IVType || !RCType)
    return std::nullopt;
  if (IVType->getBitWidth() > RCType->getBitWidth())
    return std::nullopt;

  // IndVar is of the form "A + B * I" (where "I" is the canonical induction
  // variable, that may or may not exist as a real llvm::Value in the loop) and
  // this inductive range check is a range check on the "C + D * I" ("C" is
  // getBegin() and "D" is getStep()).  We rewrite the value being range
  // checked to "M + N * IndVar" where "N" = "D * B^(-1)" and "M" = "C - NA".
  //
  // The actual inequalities we solve are of the form
  //
  //   0 <= M + 1 * IndVar < L given L >= 0  (i.e. N == 1)
  //
  // Here L stands for upper limit of the safe iteration space.
  // The inequality is satisfied by (0 - M) <= IndVar < (L - M). To avoid
  // overflows when calculating (0 - M) and (L - M) we, depending on type of
  // IV's iteration space, limit the calculations by borders of the iteration
  // space. For example, if IndVar is unsigned, (0 - M) overflows for any M > 0.
  // If we figured out that "anything greater than (-M) is safe", we strengthen
  // this to "everything greater than 0 is safe", assuming that values between
  // -M and 0 just do not exist in unsigned iteration space, and we don't want
  // to deal with overflown values.

  if (!IndVar->isAffine())
    return std::nullopt;

  const SCEV *A = NoopOrExtend(IndVar->getStart(), RCType, SE, IsLatchSigned);
  const SCEVConstant *B = dyn_cast<SCEVConstant>(
      NoopOrExtend(IndVar->getStepRecurrence(SE), RCType, SE, IsLatchSigned));
  if (!B)
    return std::nullopt;
  assert(!B->isZero() && "Recurrence with zero step?");

  const SCEV *C = getBegin();
  const SCEVConstant *D = dyn_cast<SCEVConstant>(getStep());
  if (D != B)
    return std::nullopt;

  assert(!D->getValue()->isZero() && "Recurrence with zero step?");
  unsigned BitWidth = RCType->getBitWidth();
  const SCEV *SIntMax = SE.getConstant(APInt::getSignedMaxValue(BitWidth));
  const SCEV *SIntMin = SE.getConstant(APInt::getSignedMinValue(BitWidth));

  // Subtract Y from X so that it does not go through border of the IV
  // iteration space. Mathematically, it is equivalent to:
  //
  //    ClampedSubtract(X, Y) = min(max(X - Y, INT_MIN), INT_MAX).        [1]
  //
  // In [1], 'X - Y' is a mathematical subtraction (result is not bounded to
  // any width of bit grid). But after we take min/max, the result is
  // guaranteed to be within [INT_MIN, INT_MAX].
  //
  // In [1], INT_MAX and INT_MIN are respectively signed and unsigned max/min
  // values, depending on type of latch condition that defines IV iteration
  // space.
  auto ClampedSubtract = [&](const SCEV *X, const SCEV *Y) {
    // FIXME: The current implementation assumes that X is in [0, SINT_MAX].
    // This is required to ensure that SINT_MAX - X does not overflow signed and
    // that X - Y does not overflow unsigned if Y is negative. Can we lift this
    // restriction and make it work for negative X either?
    if (IsLatchSigned) {
      // X is a number from signed range, Y is interpreted as signed.
      // Even if Y is SINT_MAX, (X - Y) does not reach SINT_MIN. So the only
      // thing we should care about is that we didn't cross SINT_MAX.
      // So, if Y is positive, we subtract Y safely.
      //   Rule 1: Y > 0 ---> Y.
      // If 0 <= -Y <= (SINT_MAX - X), we subtract Y safely.
      //   Rule 2: Y >=s (X - SINT_MAX) ---> Y.
      // If 0 <= (SINT_MAX - X) < -Y, we can only subtract (X - SINT_MAX).
      //   Rule 3: Y <s (X - SINT_MAX) ---> (X - SINT_MAX).
      // It gives us smax(Y, X - SINT_MAX) to subtract in all cases.
      const SCEV *XMinusSIntMax = SE.getMinusSCEV(X, SIntMax);
      return SE.getMinusSCEV(X, SE.getSMaxExpr(Y, XMinusSIntMax),
                             SCEV::FlagNSW);
    } else
      // X is a number from unsigned range, Y is interpreted as signed.
      // Even if Y is SINT_MIN, (X - Y) does not reach UINT_MAX. So the only
      // thing we should care about is that we didn't cross zero.
      // So, if Y is negative, we subtract Y safely.
      //   Rule 1: Y <s 0 ---> Y.
      // If 0 <= Y <= X, we subtract Y safely.
      //   Rule 2: Y <=s X ---> Y.
      // If 0 <= X < Y, we should stop at 0 and can only subtract X.
      //   Rule 3: Y >s X ---> X.
      // It gives us smin(X, Y) to subtract in all cases.
      return SE.getMinusSCEV(X, SE.getSMinExpr(X, Y), SCEV::FlagNUW);
  };
  const SCEV *M = SE.getMinusSCEV(C, A);
  const SCEV *Zero = SE.getZero(M->getType());

  // This function returns SCEV equal to 1 if X is non-negative 0 otherwise.
  auto SCEVCheckNonNegative = [&](const SCEV *X) {
    const Loop *L = IndVar->getLoop();
    const SCEV *Zero = SE.getZero(X->getType());
    const SCEV *One = SE.getOne(X->getType());
    // Can we trivially prove that X is a non-negative or negative value?
    if (isKnownNonNegativeInLoop(X, L, SE))
      return One;
    else if (isKnownNegativeInLoop(X, L, SE))
      return Zero;
    // If not, we will have to figure it out during the execution.
    // Function smax(smin(X, 0), -1) + 1 equals to 1 if X >= 0 and 0 if X < 0.
    const SCEV *NegOne = SE.getNegativeSCEV(One);
    return SE.getAddExpr(SE.getSMaxExpr(SE.getSMinExpr(X, Zero), NegOne), One);
  };

  // This function returns SCEV equal to 1 if X will not overflow in terms of
  // range check type, 0 otherwise.
  auto SCEVCheckWillNotOverflow = [&](const SCEV *X) {
    // X doesn't overflow if SINT_MAX >= X.
    // Then if (SINT_MAX - X) >= 0, X doesn't overflow
    const SCEV *SIntMaxExt = SE.getSignExtendExpr(SIntMax, X->getType());
    const SCEV *OverflowCheck =
        SCEVCheckNonNegative(SE.getMinusSCEV(SIntMaxExt, X));

    // X doesn't underflow if X >= SINT_MIN.
    // Then if (X - SINT_MIN) >= 0, X doesn't underflow
    const SCEV *SIntMinExt = SE.getSignExtendExpr(SIntMin, X->getType());
    const SCEV *UnderflowCheck =
        SCEVCheckNonNegative(SE.getMinusSCEV(X, SIntMinExt));

    return SE.getMulExpr(OverflowCheck, UnderflowCheck);
  };

  // FIXME: Current implementation of ClampedSubtract implicitly assumes that
  // X is non-negative (in sense of a signed value). We need to re-implement
  // this function in a way that it will correctly handle negative X as well.
  // We use it twice: for X = 0 everything is fine, but for X = getEnd() we can
  // end up with a negative X and produce wrong results. So currently we ensure
  // that if getEnd() is negative then both ends of the safe range are zero.
  // Note that this may pessimize elimination of unsigned range checks against
  // negative values.
  const SCEV *REnd = getEnd();
  const SCEV *EndWillNotOverflow = SE.getOne(RCType);

  auto PrintRangeCheck = [&](raw_ostream &OS) {
    auto L = IndVar->getLoop();
    OS << "irce: in function ";
    OS << L->getHeader()->getParent()->getName();
    OS << ", in ";
    L->print(OS);
    OS << "there is range check with scaled boundary:\n";
    print(OS);
  };

  if (EndType->getBitWidth() > RCType->getBitWidth()) {
    assert(EndType->getBitWidth() == RCType->getBitWidth() * 2);
    if (PrintScaledBoundaryRangeChecks)
      PrintRangeCheck(errs());
    // End is computed with extended type but will be truncated to a narrow one
    // type of range check. Therefore we need a check that the result will not
    // overflow in terms of narrow type.
    EndWillNotOverflow =
        SE.getTruncateExpr(SCEVCheckWillNotOverflow(REnd), RCType);
    REnd = SE.getTruncateExpr(REnd, RCType);
  }

  const SCEV *RuntimeChecks =
      SE.getMulExpr(SCEVCheckNonNegative(REnd), EndWillNotOverflow);
  const SCEV *Begin = SE.getMulExpr(ClampedSubtract(Zero, M), RuntimeChecks);
  const SCEV *End = SE.getMulExpr(ClampedSubtract(REnd, M), RuntimeChecks);

  return InductiveRangeCheck::Range(Begin, End);
}

static std::optional<InductiveRangeCheck::Range>
IntersectSignedRange(ScalarEvolution &SE,
                     const std::optional<InductiveRangeCheck::Range> &R1,
                     const InductiveRangeCheck::Range &R2) {
  if (R2.isEmpty(SE, /* IsSigned */ true))
    return std::nullopt;
  if (!R1)
    return R2;
  auto &R1Value = *R1;
  // We never return empty ranges from this function, and R1 is supposed to be
  // a result of intersection. Thus, R1 is never empty.
  assert(!R1Value.isEmpty(SE, /* IsSigned */ true) &&
         "We should never have empty R1!");

  // TODO: we could widen the smaller range and have this work; but for now we
  // bail out to keep things simple.
  if (R1Value.getType() != R2.getType())
    return std::nullopt;

  const SCEV *NewBegin = SE.getSMaxExpr(R1Value.getBegin(), R2.getBegin());
  const SCEV *NewEnd = SE.getSMinExpr(R1Value.getEnd(), R2.getEnd());

  // If the resulting range is empty, just return std::nullopt.
  auto Ret = InductiveRangeCheck::Range(NewBegin, NewEnd);
  if (Ret.isEmpty(SE, /* IsSigned */ true))
    return std::nullopt;
  return Ret;
}

static std::optional<InductiveRangeCheck::Range>
IntersectUnsignedRange(ScalarEvolution &SE,
                       const std::optional<InductiveRangeCheck::Range> &R1,
                       const InductiveRangeCheck::Range &R2) {
  if (R2.isEmpty(SE, /* IsSigned */ false))
    return std::nullopt;
  if (!R1)
    return R2;
  auto &R1Value = *R1;
  // We never return empty ranges from this function, and R1 is supposed to be
  // a result of intersection. Thus, R1 is never empty.
  assert(!R1Value.isEmpty(SE, /* IsSigned */ false) &&
         "We should never have empty R1!");

  // TODO: we could widen the smaller range and have this work; but for now we
  // bail out to keep things simple.
  if (R1Value.getType() != R2.getType())
    return std::nullopt;

  const SCEV *NewBegin = SE.getUMaxExpr(R1Value.getBegin(), R2.getBegin());
  const SCEV *NewEnd = SE.getUMinExpr(R1Value.getEnd(), R2.getEnd());

  // If the resulting range is empty, just return std::nullopt.
  auto Ret = InductiveRangeCheck::Range(NewBegin, NewEnd);
  if (Ret.isEmpty(SE, /* IsSigned */ false))
    return std::nullopt;
  return Ret;
}

PreservedAnalyses IRCEPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
  // There are no loops in the function. Return before computing other expensive
  // analyses.
  if (LI.empty())
    return PreservedAnalyses::all();
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &BPI = AM.getResult<BranchProbabilityAnalysis>(F);

  // Get BFI analysis result on demand. Please note that modification of
  // CFG invalidates this analysis and we should handle it.
  auto getBFI = [&F, &AM ]()->BlockFrequencyInfo & {
    return AM.getResult<BlockFrequencyAnalysis>(F);
  };
  InductiveRangeCheckElimination IRCE(SE, &BPI, DT, LI, { getBFI });

  bool Changed = false;
  {
    bool CFGChanged = false;
    for (const auto &L : LI) {
      CFGChanged |= simplifyLoop(L, &DT, &LI, &SE, nullptr, nullptr,
                                 /*PreserveLCSSA=*/false);
      Changed |= formLCSSARecursively(*L, DT, &LI, &SE);
    }
    Changed |= CFGChanged;

    if (CFGChanged && !SkipProfitabilityChecks) {
      PreservedAnalyses PA = PreservedAnalyses::all();
      PA.abandon<BlockFrequencyAnalysis>();
      AM.invalidate(F, PA);
    }
  }

  SmallPriorityWorklist<Loop *, 4> Worklist;
  appendLoopsToWorklist(LI, Worklist);
  auto LPMAddNewLoop = [&Worklist](Loop *NL, bool IsSubloop) {
    if (!IsSubloop)
      appendLoopsToWorklist(*NL, Worklist);
  };

  while (!Worklist.empty()) {
    Loop *L = Worklist.pop_back_val();
    if (IRCE.run(L, LPMAddNewLoop)) {
      Changed = true;
      if (!SkipProfitabilityChecks) {
        PreservedAnalyses PA = PreservedAnalyses::all();
        PA.abandon<BlockFrequencyAnalysis>();
        AM.invalidate(F, PA);
      }
    }
  }

  if (!Changed)
    return PreservedAnalyses::all();
  return getLoopPassPreservedAnalyses();
}

bool
InductiveRangeCheckElimination::isProfitableToTransform(const Loop &L,
                                                        LoopStructure &LS) {
  if (SkipProfitabilityChecks)
    return true;
  if (GetBFI) {
    BlockFrequencyInfo &BFI = (*GetBFI)();
    uint64_t hFreq = BFI.getBlockFreq(LS.Header).getFrequency();
    uint64_t phFreq = BFI.getBlockFreq(L.getLoopPreheader()).getFrequency();
    if (phFreq != 0 && hFreq != 0 && (hFreq / phFreq < MinRuntimeIterations)) {
      LLVM_DEBUG(dbgs() << "irce: could not prove profitability: "
                        << "the estimated number of iterations basing on "
                           "frequency info is " << (hFreq / phFreq) << "\n";);
      return false;
    }
    return true;
  }

  if (!BPI)
    return true;
  BranchProbability ExitProbability =
      BPI->getEdgeProbability(LS.Latch, LS.LatchBrExitIdx);
  if (ExitProbability > BranchProbability(1, MinRuntimeIterations)) {
    LLVM_DEBUG(dbgs() << "irce: could not prove profitability: "
                      << "the exit probability is too big " << ExitProbability
                      << "\n";);
    return false;
  }
  return true;
}

bool InductiveRangeCheckElimination::run(
    Loop *L, function_ref<void(Loop *, bool)> LPMAddNewLoop) {
  if (L->getBlocks().size() >= LoopSizeCutoff) {
    LLVM_DEBUG(dbgs() << "irce: giving up constraining loop, too large\n");
    return false;
  }

  BasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader) {
    LLVM_DEBUG(dbgs() << "irce: loop has no preheader, leaving\n");
    return false;
  }

  LLVMContext &Context = Preheader->getContext();
  SmallVector<InductiveRangeCheck, 16> RangeChecks;
  bool Changed = false;

  for (auto *BBI : L->getBlocks())
    if (BranchInst *TBI = dyn_cast<BranchInst>(BBI->getTerminator()))
      InductiveRangeCheck::extractRangeChecksFromBranch(TBI, L, SE, BPI,
                                                        RangeChecks, Changed);

  if (RangeChecks.empty())
    return Changed;

  auto PrintRecognizedRangeChecks = [&](raw_ostream &OS) {
    OS << "irce: looking at loop "; L->print(OS);
    OS << "irce: loop has " << RangeChecks.size()
       << " inductive range checks: \n";
    for (InductiveRangeCheck &IRC : RangeChecks)
      IRC.print(OS);
  };

  LLVM_DEBUG(PrintRecognizedRangeChecks(dbgs()));

  if (PrintRangeChecks)
    PrintRecognizedRangeChecks(errs());

  const char *FailureReason = nullptr;
  std::optional<LoopStructure> MaybeLoopStructure =
      LoopStructure::parseLoopStructure(SE, *L, AllowUnsignedLatchCondition,
                                        FailureReason);
  if (!MaybeLoopStructure) {
    LLVM_DEBUG(dbgs() << "irce: could not parse loop structure: "
                      << FailureReason << "\n";);
    return Changed;
  }
  LoopStructure LS = *MaybeLoopStructure;
  if (!isProfitableToTransform(*L, LS))
    return Changed;
  const SCEVAddRecExpr *IndVar =
      cast<SCEVAddRecExpr>(SE.getMinusSCEV(SE.getSCEV(LS.IndVarBase), SE.getSCEV(LS.IndVarStep)));

  std::optional<InductiveRangeCheck::Range> SafeIterRange;

  SmallVector<InductiveRangeCheck, 4> RangeChecksToEliminate;
  // Basing on the type of latch predicate, we interpret the IV iteration range
  // as signed or unsigned range. We use different min/max functions (signed or
  // unsigned) when intersecting this range with safe iteration ranges implied
  // by range checks.
  auto IntersectRange =
      LS.IsSignedPredicate ? IntersectSignedRange : IntersectUnsignedRange;

  for (InductiveRangeCheck &IRC : RangeChecks) {
    auto Result = IRC.computeSafeIterationSpace(SE, IndVar,
                                                LS.IsSignedPredicate);
    if (Result) {
      auto MaybeSafeIterRange = IntersectRange(SE, SafeIterRange, *Result);
      if (MaybeSafeIterRange) {
        assert(!MaybeSafeIterRange->isEmpty(SE, LS.IsSignedPredicate) &&
               "We should never return empty ranges!");
        RangeChecksToEliminate.push_back(IRC);
        SafeIterRange = *MaybeSafeIterRange;
      }
    }
  }

  if (!SafeIterRange)
    return Changed;

  std::optional<LoopConstrainer::SubRanges> MaybeSR =
      calculateSubRanges(SE, *L, *SafeIterRange, LS);
  if (!MaybeSR) {
    LLVM_DEBUG(dbgs() << "irce: could not compute subranges\n");
    return false;
  }

  LoopConstrainer LC(*L, LI, LPMAddNewLoop, LS, SE, DT,
                     SafeIterRange->getBegin()->getType(), *MaybeSR);

  if (LC.run()) {
    Changed = true;

    auto PrintConstrainedLoopInfo = [L]() {
      dbgs() << "irce: in function ";
      dbgs() << L->getHeader()->getParent()->getName() << ": ";
      dbgs() << "constrained ";
      L->print(dbgs());
    };

    LLVM_DEBUG(PrintConstrainedLoopInfo());

    if (PrintChangedLoops)
      PrintConstrainedLoopInfo();

    // Optimize away the now-redundant range checks.

    for (InductiveRangeCheck &IRC : RangeChecksToEliminate) {
      ConstantInt *FoldedRangeCheck = IRC.getPassingDirection()
                                          ? ConstantInt::getTrue(Context)
                                          : ConstantInt::getFalse(Context);
      IRC.getCheckUse()->set(FoldedRangeCheck);
    }
  }

  return Changed;
}
