#include "llvm/Transforms/Utils/LoopConstrainer.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;

static const char *ClonedLoopTag = "loop_constrainer.loop.clone";

#define DEBUG_TYPE "loop-constrainer"

/// Given a loop with an deccreasing induction variable, is it possible to
/// safely calculate the bounds of a new loop using the given Predicate.
static bool isSafeDecreasingBound(const SCEV *Start, const SCEV *BoundSCEV,
                                  const SCEV *Step, ICmpInst::Predicate Pred,
                                  unsigned LatchBrExitIdx, Loop *L,
                                  ScalarEvolution &SE) {
  if (Pred != ICmpInst::ICMP_SLT && Pred != ICmpInst::ICMP_SGT &&
      Pred != ICmpInst::ICMP_ULT && Pred != ICmpInst::ICMP_UGT)
    return false;

  if (!SE.isAvailableAtLoopEntry(BoundSCEV, L))
    return false;

  assert(SE.isKnownNegative(Step) && "expecting negative step");

  LLVM_DEBUG(dbgs() << "isSafeDecreasingBound with:\n");
  LLVM_DEBUG(dbgs() << "Start: " << *Start << "\n");
  LLVM_DEBUG(dbgs() << "Step: " << *Step << "\n");
  LLVM_DEBUG(dbgs() << "BoundSCEV: " << *BoundSCEV << "\n");
  LLVM_DEBUG(dbgs() << "Pred: " << Pred << "\n");
  LLVM_DEBUG(dbgs() << "LatchExitBrIdx: " << LatchBrExitIdx << "\n");

  bool IsSigned = ICmpInst::isSigned(Pred);
  // The predicate that we need to check that the induction variable lies
  // within bounds.
  ICmpInst::Predicate BoundPred =
      IsSigned ? CmpInst::ICMP_SGT : CmpInst::ICMP_UGT;

  auto StartLG = SE.applyLoopGuards(Start, L);
  auto BoundLG = SE.applyLoopGuards(BoundSCEV, L);

  if (LatchBrExitIdx == 1)
    return SE.isLoopEntryGuardedByCond(L, BoundPred, StartLG, BoundLG);

  assert(LatchBrExitIdx == 0 && "LatchBrExitIdx should be either 0 or 1");

  const SCEV *StepPlusOne = SE.getAddExpr(Step, SE.getOne(Step->getType()));
  unsigned BitWidth = cast<IntegerType>(BoundSCEV->getType())->getBitWidth();
  APInt Min = IsSigned ? APInt::getSignedMinValue(BitWidth)
                       : APInt::getMinValue(BitWidth);
  const SCEV *Limit = SE.getMinusSCEV(SE.getConstant(Min), StepPlusOne);

  const SCEV *MinusOne =
      SE.getMinusSCEV(BoundLG, SE.getOne(BoundLG->getType()));

  return SE.isLoopEntryGuardedByCond(L, BoundPred, StartLG, MinusOne) &&
         SE.isLoopEntryGuardedByCond(L, BoundPred, BoundLG, Limit);
}

/// Given a loop with an increasing induction variable, is it possible to
/// safely calculate the bounds of a new loop using the given Predicate.
static bool isSafeIncreasingBound(const SCEV *Start, const SCEV *BoundSCEV,
                                  const SCEV *Step, ICmpInst::Predicate Pred,
                                  unsigned LatchBrExitIdx, Loop *L,
                                  ScalarEvolution &SE) {
  if (Pred != ICmpInst::ICMP_SLT && Pred != ICmpInst::ICMP_SGT &&
      Pred != ICmpInst::ICMP_ULT && Pred != ICmpInst::ICMP_UGT)
    return false;

  if (!SE.isAvailableAtLoopEntry(BoundSCEV, L))
    return false;

  LLVM_DEBUG(dbgs() << "isSafeIncreasingBound with:\n");
  LLVM_DEBUG(dbgs() << "Start: " << *Start << "\n");
  LLVM_DEBUG(dbgs() << "Step: " << *Step << "\n");
  LLVM_DEBUG(dbgs() << "BoundSCEV: " << *BoundSCEV << "\n");
  LLVM_DEBUG(dbgs() << "Pred: " << Pred << "\n");
  LLVM_DEBUG(dbgs() << "LatchExitBrIdx: " << LatchBrExitIdx << "\n");

  bool IsSigned = ICmpInst::isSigned(Pred);
  // The predicate that we need to check that the induction variable lies
  // within bounds.
  ICmpInst::Predicate BoundPred =
      IsSigned ? CmpInst::ICMP_SLT : CmpInst::ICMP_ULT;

  auto StartLG = SE.applyLoopGuards(Start, L);
  auto BoundLG = SE.applyLoopGuards(BoundSCEV, L);

  if (LatchBrExitIdx == 1)
    return SE.isLoopEntryGuardedByCond(L, BoundPred, StartLG, BoundLG);

  assert(LatchBrExitIdx == 0 && "LatchBrExitIdx should be 0 or 1");

  const SCEV *StepMinusOne = SE.getMinusSCEV(Step, SE.getOne(Step->getType()));
  unsigned BitWidth = cast<IntegerType>(BoundSCEV->getType())->getBitWidth();
  APInt Max = IsSigned ? APInt::getSignedMaxValue(BitWidth)
                       : APInt::getMaxValue(BitWidth);
  const SCEV *Limit = SE.getMinusSCEV(SE.getConstant(Max), StepMinusOne);

  return (SE.isLoopEntryGuardedByCond(L, BoundPred, StartLG,
                                      SE.getAddExpr(BoundLG, Step)) &&
          SE.isLoopEntryGuardedByCond(L, BoundPred, BoundLG, Limit));
}

/// Returns estimate for max latch taken count of the loop of the narrowest
/// available type. If the latch block has such estimate, it is returned.
/// Otherwise, we use max exit count of whole loop (that is potentially of wider
/// type than latch check itself), which is still better than no estimate.
static const SCEV *getNarrowestLatchMaxTakenCountEstimate(ScalarEvolution &SE,
                                                          const Loop &L) {
  const SCEV *FromBlock =
      SE.getExitCount(&L, L.getLoopLatch(), ScalarEvolution::SymbolicMaximum);
  if (isa<SCEVCouldNotCompute>(FromBlock))
    return SE.getSymbolicMaxBackedgeTakenCount(&L);
  return FromBlock;
}

std::optional<LoopStructure>
LoopStructure::parseLoopStructure(ScalarEvolution &SE, Loop &L,
                                  bool AllowUnsignedLatchCond,
                                  const char *&FailureReason) {
  if (!L.isLoopSimplifyForm()) {
    FailureReason = "loop not in LoopSimplify form";
    return std::nullopt;
  }

  BasicBlock *Latch = L.getLoopLatch();
  assert(Latch && "Simplified loops only have one latch!");

  if (Latch->getTerminator()->getMetadata(ClonedLoopTag)) {
    FailureReason = "loop has already been cloned";
    return std::nullopt;
  }

  if (!L.isLoopExiting(Latch)) {
    FailureReason = "no loop latch";
    return std::nullopt;
  }

  BasicBlock *Header = L.getHeader();
  BasicBlock *Preheader = L.getLoopPreheader();
  if (!Preheader) {
    FailureReason = "no preheader";
    return std::nullopt;
  }

  BranchInst *LatchBr = dyn_cast<BranchInst>(Latch->getTerminator());
  if (!LatchBr || LatchBr->isUnconditional()) {
    FailureReason = "latch terminator not conditional branch";
    return std::nullopt;
  }

  unsigned LatchBrExitIdx = LatchBr->getSuccessor(0) == Header ? 1 : 0;

  ICmpInst *ICI = dyn_cast<ICmpInst>(LatchBr->getCondition());
  if (!ICI || !isa<IntegerType>(ICI->getOperand(0)->getType())) {
    FailureReason = "latch terminator branch not conditional on integral icmp";
    return std::nullopt;
  }

  const SCEV *MaxBETakenCount = getNarrowestLatchMaxTakenCountEstimate(SE, L);
  if (isa<SCEVCouldNotCompute>(MaxBETakenCount)) {
    FailureReason = "could not compute latch count";
    return std::nullopt;
  }
  assert(SE.getLoopDisposition(MaxBETakenCount, &L) ==
             ScalarEvolution::LoopInvariant &&
         "loop variant exit count doesn't make sense!");

  ICmpInst::Predicate Pred = ICI->getPredicate();
  Value *LeftValue = ICI->getOperand(0);
  const SCEV *LeftSCEV = SE.getSCEV(LeftValue);
  IntegerType *IndVarTy = cast<IntegerType>(LeftValue->getType());

  Value *RightValue = ICI->getOperand(1);
  const SCEV *RightSCEV = SE.getSCEV(RightValue);

  // We canonicalize `ICI` such that `LeftSCEV` is an add recurrence.
  if (!isa<SCEVAddRecExpr>(LeftSCEV)) {
    if (isa<SCEVAddRecExpr>(RightSCEV)) {
      std::swap(LeftSCEV, RightSCEV);
      std::swap(LeftValue, RightValue);
      Pred = ICmpInst::getSwappedPredicate(Pred);
    } else {
      FailureReason = "no add recurrences in the icmp";
      return std::nullopt;
    }
  }

  auto HasNoSignedWrap = [&](const SCEVAddRecExpr *AR) {
    if (AR->getNoWrapFlags(SCEV::FlagNSW))
      return true;

    IntegerType *Ty = cast<IntegerType>(AR->getType());
    IntegerType *WideTy =
        IntegerType::get(Ty->getContext(), Ty->getBitWidth() * 2);

    const SCEVAddRecExpr *ExtendAfterOp =
        dyn_cast<SCEVAddRecExpr>(SE.getSignExtendExpr(AR, WideTy));
    if (ExtendAfterOp) {
      const SCEV *ExtendedStart = SE.getSignExtendExpr(AR->getStart(), WideTy);
      const SCEV *ExtendedStep =
          SE.getSignExtendExpr(AR->getStepRecurrence(SE), WideTy);

      bool NoSignedWrap = ExtendAfterOp->getStart() == ExtendedStart &&
                          ExtendAfterOp->getStepRecurrence(SE) == ExtendedStep;

      if (NoSignedWrap)
        return true;
    }

    // We may have proved this when computing the sign extension above.
    return AR->getNoWrapFlags(SCEV::FlagNSW) != SCEV::FlagAnyWrap;
  };

  // `ICI` is interpreted as taking the backedge if the *next* value of the
  // induction variable satisfies some constraint.

  const SCEVAddRecExpr *IndVarBase = cast<SCEVAddRecExpr>(LeftSCEV);
  if (IndVarBase->getLoop() != &L) {
    FailureReason = "LHS in cmp is not an AddRec for this loop";
    return std::nullopt;
  }
  if (!IndVarBase->isAffine()) {
    FailureReason = "LHS in icmp not induction variable";
    return std::nullopt;
  }
  const SCEV *StepRec = IndVarBase->getStepRecurrence(SE);
  if (!isa<SCEVConstant>(StepRec)) {
    FailureReason = "LHS in icmp not induction variable";
    return std::nullopt;
  }
  ConstantInt *StepCI = cast<SCEVConstant>(StepRec)->getValue();

  if (ICI->isEquality() && !HasNoSignedWrap(IndVarBase)) {
    FailureReason = "LHS in icmp needs nsw for equality predicates";
    return std::nullopt;
  }

  assert(!StepCI->isZero() && "Zero step?");
  bool IsIncreasing = !StepCI->isNegative();
  bool IsSignedPredicate;
  const SCEV *StartNext = IndVarBase->getStart();
  const SCEV *Addend = SE.getNegativeSCEV(IndVarBase->getStepRecurrence(SE));
  const SCEV *IndVarStart = SE.getAddExpr(StartNext, Addend);
  const SCEV *Step = SE.getSCEV(StepCI);

  const SCEV *FixedRightSCEV = nullptr;

  // If RightValue resides within loop (but still being loop invariant),
  // regenerate it as preheader.
  if (auto *I = dyn_cast<Instruction>(RightValue))
    if (L.contains(I->getParent()))
      FixedRightSCEV = RightSCEV;

  if (IsIncreasing) {
    bool DecreasedRightValueByOne = false;
    if (StepCI->isOne()) {
      // Try to turn eq/ne predicates to those we can work with.
      if (Pred == ICmpInst::ICMP_NE && LatchBrExitIdx == 1)
        // while (++i != len) {         while (++i < len) {
        //   ...                 --->     ...
        // }                            }
        // If both parts are known non-negative, it is profitable to use
        // unsigned comparison in increasing loop. This allows us to make the
        // comparison check against "RightSCEV + 1" more optimistic.
        if (isKnownNonNegativeInLoop(IndVarStart, &L, SE) &&
            isKnownNonNegativeInLoop(RightSCEV, &L, SE))
          Pred = ICmpInst::ICMP_ULT;
        else
          Pred = ICmpInst::ICMP_SLT;
      else if (Pred == ICmpInst::ICMP_EQ && LatchBrExitIdx == 0) {
        // while (true) {               while (true) {
        //   if (++i == len)     --->     if (++i > len - 1)
        //     break;                       break;
        //   ...                          ...
        // }                            }
        if (IndVarBase->getNoWrapFlags(SCEV::FlagNUW) &&
            cannotBeMinInLoop(RightSCEV, &L, SE, /*Signed*/ false)) {
          Pred = ICmpInst::ICMP_UGT;
          RightSCEV =
              SE.getMinusSCEV(RightSCEV, SE.getOne(RightSCEV->getType()));
          DecreasedRightValueByOne = true;
        } else if (cannotBeMinInLoop(RightSCEV, &L, SE, /*Signed*/ true)) {
          Pred = ICmpInst::ICMP_SGT;
          RightSCEV =
              SE.getMinusSCEV(RightSCEV, SE.getOne(RightSCEV->getType()));
          DecreasedRightValueByOne = true;
        }
      }
    }

    bool LTPred = (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_ULT);
    bool GTPred = (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_UGT);
    bool FoundExpectedPred =
        (LTPred && LatchBrExitIdx == 1) || (GTPred && LatchBrExitIdx == 0);

    if (!FoundExpectedPred) {
      FailureReason = "expected icmp slt semantically, found something else";
      return std::nullopt;
    }

    IsSignedPredicate = ICmpInst::isSigned(Pred);
    if (!IsSignedPredicate && !AllowUnsignedLatchCond) {
      FailureReason = "unsigned latch conditions are explicitly prohibited";
      return std::nullopt;
    }

    if (!isSafeIncreasingBound(IndVarStart, RightSCEV, Step, Pred,
                               LatchBrExitIdx, &L, SE)) {
      FailureReason = "Unsafe loop bounds";
      return std::nullopt;
    }
    if (LatchBrExitIdx == 0) {
      // We need to increase the right value unless we have already decreased
      // it virtually when we replaced EQ with SGT.
      if (!DecreasedRightValueByOne)
        FixedRightSCEV =
            SE.getAddExpr(RightSCEV, SE.getOne(RightSCEV->getType()));
    } else {
      assert(!DecreasedRightValueByOne &&
             "Right value can be decreased only for LatchBrExitIdx == 0!");
    }
  } else {
    bool IncreasedRightValueByOne = false;
    if (StepCI->isMinusOne()) {
      // Try to turn eq/ne predicates to those we can work with.
      if (Pred == ICmpInst::ICMP_NE && LatchBrExitIdx == 1)
        // while (--i != len) {         while (--i > len) {
        //   ...                 --->     ...
        // }                            }
        // We intentionally don't turn the predicate into UGT even if we know
        // that both operands are non-negative, because it will only pessimize
        // our check against "RightSCEV - 1".
        Pred = ICmpInst::ICMP_SGT;
      else if (Pred == ICmpInst::ICMP_EQ && LatchBrExitIdx == 0) {
        // while (true) {               while (true) {
        //   if (--i == len)     --->     if (--i < len + 1)
        //     break;                       break;
        //   ...                          ...
        // }                            }
        if (IndVarBase->getNoWrapFlags(SCEV::FlagNUW) &&
            cannotBeMaxInLoop(RightSCEV, &L, SE, /* Signed */ false)) {
          Pred = ICmpInst::ICMP_ULT;
          RightSCEV = SE.getAddExpr(RightSCEV, SE.getOne(RightSCEV->getType()));
          IncreasedRightValueByOne = true;
        } else if (cannotBeMaxInLoop(RightSCEV, &L, SE, /* Signed */ true)) {
          Pred = ICmpInst::ICMP_SLT;
          RightSCEV = SE.getAddExpr(RightSCEV, SE.getOne(RightSCEV->getType()));
          IncreasedRightValueByOne = true;
        }
      }
    }

    bool LTPred = (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_ULT);
    bool GTPred = (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_UGT);

    bool FoundExpectedPred =
        (GTPred && LatchBrExitIdx == 1) || (LTPred && LatchBrExitIdx == 0);

    if (!FoundExpectedPred) {
      FailureReason = "expected icmp sgt semantically, found something else";
      return std::nullopt;
    }

    IsSignedPredicate =
        Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SGT;

    if (!IsSignedPredicate && !AllowUnsignedLatchCond) {
      FailureReason = "unsigned latch conditions are explicitly prohibited";
      return std::nullopt;
    }

    if (!isSafeDecreasingBound(IndVarStart, RightSCEV, Step, Pred,
                               LatchBrExitIdx, &L, SE)) {
      FailureReason = "Unsafe bounds";
      return std::nullopt;
    }

    if (LatchBrExitIdx == 0) {
      // We need to decrease the right value unless we have already increased
      // it virtually when we replaced EQ with SLT.
      if (!IncreasedRightValueByOne)
        FixedRightSCEV =
            SE.getMinusSCEV(RightSCEV, SE.getOne(RightSCEV->getType()));
    } else {
      assert(!IncreasedRightValueByOne &&
             "Right value can be increased only for LatchBrExitIdx == 0!");
    }
  }
  BasicBlock *LatchExit = LatchBr->getSuccessor(LatchBrExitIdx);

  assert(!L.contains(LatchExit) && "expected an exit block!");
  const DataLayout &DL = Preheader->getDataLayout();
  SCEVExpander Expander(SE, DL, "loop-constrainer");
  Instruction *Ins = Preheader->getTerminator();

  if (FixedRightSCEV)
    RightValue =
        Expander.expandCodeFor(FixedRightSCEV, FixedRightSCEV->getType(), Ins);

  Value *IndVarStartV = Expander.expandCodeFor(IndVarStart, IndVarTy, Ins);
  IndVarStartV->setName("indvar.start");

  LoopStructure Result;

  Result.Tag = "main";
  Result.Header = Header;
  Result.Latch = Latch;
  Result.LatchBr = LatchBr;
  Result.LatchExit = LatchExit;
  Result.LatchBrExitIdx = LatchBrExitIdx;
  Result.IndVarStart = IndVarStartV;
  Result.IndVarStep = StepCI;
  Result.IndVarBase = LeftValue;
  Result.IndVarIncreasing = IsIncreasing;
  Result.LoopExitAt = RightValue;
  Result.IsSignedPredicate = IsSignedPredicate;
  Result.ExitCountTy = cast<IntegerType>(MaxBETakenCount->getType());

  FailureReason = nullptr;

  return Result;
}

// Add metadata to the loop L to disable loop optimizations. Callers need to
// confirm that optimizing loop L is not beneficial.
static void DisableAllLoopOptsOnLoop(Loop &L) {
  // We do not care about any existing loopID related metadata for L, since we
  // are setting all loop metadata to false.
  LLVMContext &Context = L.getHeader()->getContext();
  // Reserve first location for self reference to the LoopID metadata node.
  MDNode *Dummy = MDNode::get(Context, {});
  MDNode *DisableUnroll = MDNode::get(
      Context, {MDString::get(Context, "llvm.loop.unroll.disable")});
  Metadata *FalseVal =
      ConstantAsMetadata::get(ConstantInt::get(Type::getInt1Ty(Context), 0));
  MDNode *DisableVectorize = MDNode::get(
      Context,
      {MDString::get(Context, "llvm.loop.vectorize.enable"), FalseVal});
  MDNode *DisableLICMVersioning = MDNode::get(
      Context, {MDString::get(Context, "llvm.loop.licm_versioning.disable")});
  MDNode *DisableDistribution = MDNode::get(
      Context,
      {MDString::get(Context, "llvm.loop.distribute.enable"), FalseVal});
  MDNode *NewLoopID =
      MDNode::get(Context, {Dummy, DisableUnroll, DisableVectorize,
                            DisableLICMVersioning, DisableDistribution});
  // Set operand 0 to refer to the loop id itself.
  NewLoopID->replaceOperandWith(0, NewLoopID);
  L.setLoopID(NewLoopID);
}

LoopConstrainer::LoopConstrainer(Loop &L, LoopInfo &LI,
                                 function_ref<void(Loop *, bool)> LPMAddNewLoop,
                                 const LoopStructure &LS, ScalarEvolution &SE,
                                 DominatorTree &DT, Type *T, SubRanges SR)
    : F(*L.getHeader()->getParent()), Ctx(L.getHeader()->getContext()), SE(SE),
      DT(DT), LI(LI), LPMAddNewLoop(LPMAddNewLoop), OriginalLoop(L), RangeTy(T),
      MainLoopStructure(LS), SR(SR) {}

void LoopConstrainer::cloneLoop(LoopConstrainer::ClonedLoop &Result,
                                const char *Tag) const {
  for (BasicBlock *BB : OriginalLoop.getBlocks()) {
    BasicBlock *Clone = CloneBasicBlock(BB, Result.Map, Twine(".") + Tag, &F);
    Result.Blocks.push_back(Clone);
    Result.Map[BB] = Clone;
  }

  auto GetClonedValue = [&Result](Value *V) {
    assert(V && "null values not in domain!");
    auto It = Result.Map.find(V);
    if (It == Result.Map.end())
      return V;
    return static_cast<Value *>(It->second);
  };

  auto *ClonedLatch =
      cast<BasicBlock>(GetClonedValue(OriginalLoop.getLoopLatch()));
  ClonedLatch->getTerminator()->setMetadata(ClonedLoopTag,
                                            MDNode::get(Ctx, {}));

  Result.Structure = MainLoopStructure.map(GetClonedValue);
  Result.Structure.Tag = Tag;

  for (unsigned i = 0, e = Result.Blocks.size(); i != e; ++i) {
    BasicBlock *ClonedBB = Result.Blocks[i];
    BasicBlock *OriginalBB = OriginalLoop.getBlocks()[i];

    assert(Result.Map[OriginalBB] == ClonedBB && "invariant!");

    for (Instruction &I : *ClonedBB)
      RemapInstruction(&I, Result.Map,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);

    // Exit blocks will now have one more predecessor and their PHI nodes need
    // to be edited to reflect that.  No phi nodes need to be introduced because
    // the loop is in LCSSA.

    for (auto *SBB : successors(OriginalBB)) {
      if (OriginalLoop.contains(SBB))
        continue; // not an exit block

      for (PHINode &PN : SBB->phis()) {
        Value *OldIncoming = PN.getIncomingValueForBlock(OriginalBB);
        PN.addIncoming(GetClonedValue(OldIncoming), ClonedBB);
        SE.forgetValue(&PN);
      }
    }
  }
}

LoopConstrainer::RewrittenRangeInfo LoopConstrainer::changeIterationSpaceEnd(
    const LoopStructure &LS, BasicBlock *Preheader, Value *ExitSubloopAt,
    BasicBlock *ContinuationBlock) const {
  // We start with a loop with a single latch:
  //
  //    +--------------------+
  //    |                    |
  //    |     preheader      |
  //    |                    |
  //    +--------+-----------+
  //             |      ----------------\
  //             |     /                |
  //    +--------v----v------+          |
  //    |                    |          |
  //    |      header        |          |
  //    |                    |          |
  //    +--------------------+          |
  //                                    |
  //            .....                   |
  //                                    |
  //    +--------------------+          |
  //    |                    |          |
  //    |       latch        >----------/
  //    |                    |
  //    +-------v------------+
  //            |
  //            |
  //            |   +--------------------+
  //            |   |                    |
  //            +--->   original exit    |
  //                |                    |
  //                +--------------------+
  //
  // We change the control flow to look like
  //
  //
  //    +--------------------+
  //    |                    |
  //    |     preheader      >-------------------------+
  //    |                    |                         |
  //    +--------v-----------+                         |
  //             |    /-------------+                  |
  //             |   /              |                  |
  //    +--------v--v--------+      |                  |
  //    |                    |      |                  |
  //    |      header        |      |   +--------+     |
  //    |                    |      |   |        |     |
  //    +--------------------+      |   |  +-----v-----v-----------+
  //                                |   |  |                       |
  //                                |   |  |     .pseudo.exit      |
  //                                |   |  |                       |
  //                                |   |  +-----------v-----------+
  //                                |   |              |
  //            .....               |   |              |
  //                                |   |     +--------v-------------+
  //    +--------------------+      |   |     |                      |
  //    |                    |      |   |     |   ContinuationBlock  |
  //    |       latch        >------+   |     |                      |
  //    |                    |          |     +----------------------+
  //    +---------v----------+          |
  //              |                     |
  //              |                     |
  //              |     +---------------^-----+
  //              |     |                     |
  //              +----->    .exit.selector   |
  //                    |                     |
  //                    +----------v----------+
  //                               |
  //     +--------------------+    |
  //     |                    |    |
  //     |   original exit    <----+
  //     |                    |
  //     +--------------------+

  RewrittenRangeInfo RRI;

  BasicBlock *BBInsertLocation = LS.Latch->getNextNode();
  RRI.ExitSelector = BasicBlock::Create(Ctx, Twine(LS.Tag) + ".exit.selector",
                                        &F, BBInsertLocation);
  RRI.PseudoExit = BasicBlock::Create(Ctx, Twine(LS.Tag) + ".pseudo.exit", &F,
                                      BBInsertLocation);

  BranchInst *PreheaderJump = cast<BranchInst>(Preheader->getTerminator());
  bool Increasing = LS.IndVarIncreasing;
  bool IsSignedPredicate = LS.IsSignedPredicate;

  IRBuilder<> B(PreheaderJump);
  auto NoopOrExt = [&](Value *V) {
    if (V->getType() == RangeTy)
      return V;
    return IsSignedPredicate ? B.CreateSExt(V, RangeTy, "wide." + V->getName())
                             : B.CreateZExt(V, RangeTy, "wide." + V->getName());
  };

  // EnterLoopCond - is it okay to start executing this `LS'?
  Value *EnterLoopCond = nullptr;
  auto Pred =
      Increasing
          ? (IsSignedPredicate ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT)
          : (IsSignedPredicate ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT);
  Value *IndVarStart = NoopOrExt(LS.IndVarStart);
  EnterLoopCond = B.CreateICmp(Pred, IndVarStart, ExitSubloopAt);

  B.CreateCondBr(EnterLoopCond, LS.Header, RRI.PseudoExit);
  PreheaderJump->eraseFromParent();

  LS.LatchBr->setSuccessor(LS.LatchBrExitIdx, RRI.ExitSelector);
  B.SetInsertPoint(LS.LatchBr);
  Value *IndVarBase = NoopOrExt(LS.IndVarBase);
  Value *TakeBackedgeLoopCond = B.CreateICmp(Pred, IndVarBase, ExitSubloopAt);

  Value *CondForBranch = LS.LatchBrExitIdx == 1
                             ? TakeBackedgeLoopCond
                             : B.CreateNot(TakeBackedgeLoopCond);

  LS.LatchBr->setCondition(CondForBranch);

  B.SetInsertPoint(RRI.ExitSelector);

  // IterationsLeft - are there any more iterations left, given the original
  // upper bound on the induction variable?  If not, we branch to the "real"
  // exit.
  Value *LoopExitAt = NoopOrExt(LS.LoopExitAt);
  Value *IterationsLeft = B.CreateICmp(Pred, IndVarBase, LoopExitAt);
  B.CreateCondBr(IterationsLeft, RRI.PseudoExit, LS.LatchExit);

  BranchInst *BranchToContinuation =
      BranchInst::Create(ContinuationBlock, RRI.PseudoExit);

  // We emit PHI nodes into `RRI.PseudoExit' that compute the "latest" value of
  // each of the PHI nodes in the loop header.  This feeds into the initial
  // value of the same PHI nodes if/when we continue execution.
  for (PHINode &PN : LS.Header->phis()) {
    PHINode *NewPHI = PHINode::Create(PN.getType(), 2, PN.getName() + ".copy",
                                      BranchToContinuation->getIterator());

    NewPHI->addIncoming(PN.getIncomingValueForBlock(Preheader), Preheader);
    NewPHI->addIncoming(PN.getIncomingValueForBlock(LS.Latch),
                        RRI.ExitSelector);
    RRI.PHIValuesAtPseudoExit.push_back(NewPHI);
  }

  RRI.IndVarEnd = PHINode::Create(IndVarBase->getType(), 2, "indvar.end",
                                  BranchToContinuation->getIterator());
  RRI.IndVarEnd->addIncoming(IndVarStart, Preheader);
  RRI.IndVarEnd->addIncoming(IndVarBase, RRI.ExitSelector);

  // The latch exit now has a branch from `RRI.ExitSelector' instead of
  // `LS.Latch'.  The PHI nodes need to be updated to reflect that.
  LS.LatchExit->replacePhiUsesWith(LS.Latch, RRI.ExitSelector);

  return RRI;
}

void LoopConstrainer::rewriteIncomingValuesForPHIs(
    LoopStructure &LS, BasicBlock *ContinuationBlock,
    const LoopConstrainer::RewrittenRangeInfo &RRI) const {
  unsigned PHIIndex = 0;
  for (PHINode &PN : LS.Header->phis())
    PN.setIncomingValueForBlock(ContinuationBlock,
                                RRI.PHIValuesAtPseudoExit[PHIIndex++]);

  LS.IndVarStart = RRI.IndVarEnd;
}

BasicBlock *LoopConstrainer::createPreheader(const LoopStructure &LS,
                                             BasicBlock *OldPreheader,
                                             const char *Tag) const {
  BasicBlock *Preheader = BasicBlock::Create(Ctx, Tag, &F, LS.Header);
  BranchInst::Create(LS.Header, Preheader);

  LS.Header->replacePhiUsesWith(OldPreheader, Preheader);

  return Preheader;
}

void LoopConstrainer::addToParentLoopIfNeeded(ArrayRef<BasicBlock *> BBs) {
  Loop *ParentLoop = OriginalLoop.getParentLoop();
  if (!ParentLoop)
    return;

  for (BasicBlock *BB : BBs)
    ParentLoop->addBasicBlockToLoop(BB, LI);
}

Loop *LoopConstrainer::createClonedLoopStructure(Loop *Original, Loop *Parent,
                                                 ValueToValueMapTy &VM,
                                                 bool IsSubloop) {
  Loop &New = *LI.AllocateLoop();
  if (Parent)
    Parent->addChildLoop(&New);
  else
    LI.addTopLevelLoop(&New);
  LPMAddNewLoop(&New, IsSubloop);

  // Add all of the blocks in Original to the new loop.
  for (auto *BB : Original->blocks())
    if (LI.getLoopFor(BB) == Original)
      New.addBasicBlockToLoop(cast<BasicBlock>(VM[BB]), LI);

  // Add all of the subloops to the new loop.
  for (Loop *SubLoop : *Original)
    createClonedLoopStructure(SubLoop, &New, VM, /* IsSubloop */ true);

  return &New;
}

bool LoopConstrainer::run() {
  BasicBlock *Preheader = OriginalLoop.getLoopPreheader();
  assert(Preheader != nullptr && "precondition!");

  OriginalPreheader = Preheader;
  MainLoopPreheader = Preheader;
  bool IsSignedPredicate = MainLoopStructure.IsSignedPredicate;
  bool Increasing = MainLoopStructure.IndVarIncreasing;
  IntegerType *IVTy = cast<IntegerType>(RangeTy);

  SCEVExpander Expander(SE, F.getDataLayout(), "loop-constrainer");
  Instruction *InsertPt = OriginalPreheader->getTerminator();

  // It would have been better to make `PreLoop' and `PostLoop'
  // `std::optional<ClonedLoop>'s, but `ValueToValueMapTy' does not have a copy
  // constructor.
  ClonedLoop PreLoop, PostLoop;
  bool NeedsPreLoop =
      Increasing ? SR.LowLimit.has_value() : SR.HighLimit.has_value();
  bool NeedsPostLoop =
      Increasing ? SR.HighLimit.has_value() : SR.LowLimit.has_value();

  Value *ExitPreLoopAt = nullptr;
  Value *ExitMainLoopAt = nullptr;
  const SCEVConstant *MinusOneS =
      cast<SCEVConstant>(SE.getConstant(IVTy, -1, true /* isSigned */));

  if (NeedsPreLoop) {
    const SCEV *ExitPreLoopAtSCEV = nullptr;

    if (Increasing)
      ExitPreLoopAtSCEV = *SR.LowLimit;
    else if (cannotBeMinInLoop(*SR.HighLimit, &OriginalLoop, SE,
                               IsSignedPredicate))
      ExitPreLoopAtSCEV = SE.getAddExpr(*SR.HighLimit, MinusOneS);
    else {
      LLVM_DEBUG(dbgs() << "could not prove no-overflow when computing "
                        << "preloop exit limit.  HighLimit = "
                        << *(*SR.HighLimit) << "\n");
      return false;
    }

    if (!Expander.isSafeToExpandAt(ExitPreLoopAtSCEV, InsertPt)) {
      LLVM_DEBUG(dbgs() << "could not prove that it is safe to expand the"
                        << " preloop exit limit " << *ExitPreLoopAtSCEV
                        << " at block " << InsertPt->getParent()->getName()
                        << "\n");
      return false;
    }

    ExitPreLoopAt = Expander.expandCodeFor(ExitPreLoopAtSCEV, IVTy, InsertPt);
    ExitPreLoopAt->setName("exit.preloop.at");
  }

  if (NeedsPostLoop) {
    const SCEV *ExitMainLoopAtSCEV = nullptr;

    if (Increasing)
      ExitMainLoopAtSCEV = *SR.HighLimit;
    else if (cannotBeMinInLoop(*SR.LowLimit, &OriginalLoop, SE,
                               IsSignedPredicate))
      ExitMainLoopAtSCEV = SE.getAddExpr(*SR.LowLimit, MinusOneS);
    else {
      LLVM_DEBUG(dbgs() << "could not prove no-overflow when computing "
                        << "mainloop exit limit.  LowLimit = "
                        << *(*SR.LowLimit) << "\n");
      return false;
    }

    if (!Expander.isSafeToExpandAt(ExitMainLoopAtSCEV, InsertPt)) {
      LLVM_DEBUG(dbgs() << "could not prove that it is safe to expand the"
                        << " main loop exit limit " << *ExitMainLoopAtSCEV
                        << " at block " << InsertPt->getParent()->getName()
                        << "\n");
      return false;
    }

    ExitMainLoopAt = Expander.expandCodeFor(ExitMainLoopAtSCEV, IVTy, InsertPt);
    ExitMainLoopAt->setName("exit.mainloop.at");
  }

  // We clone these ahead of time so that we don't have to deal with changing
  // and temporarily invalid IR as we transform the loops.
  if (NeedsPreLoop)
    cloneLoop(PreLoop, "preloop");
  if (NeedsPostLoop)
    cloneLoop(PostLoop, "postloop");

  RewrittenRangeInfo PreLoopRRI;

  if (NeedsPreLoop) {
    Preheader->getTerminator()->replaceUsesOfWith(MainLoopStructure.Header,
                                                  PreLoop.Structure.Header);

    MainLoopPreheader =
        createPreheader(MainLoopStructure, Preheader, "mainloop");
    PreLoopRRI = changeIterationSpaceEnd(PreLoop.Structure, Preheader,
                                         ExitPreLoopAt, MainLoopPreheader);
    rewriteIncomingValuesForPHIs(MainLoopStructure, MainLoopPreheader,
                                 PreLoopRRI);
  }

  BasicBlock *PostLoopPreheader = nullptr;
  RewrittenRangeInfo PostLoopRRI;

  if (NeedsPostLoop) {
    PostLoopPreheader =
        createPreheader(PostLoop.Structure, Preheader, "postloop");
    PostLoopRRI = changeIterationSpaceEnd(MainLoopStructure, MainLoopPreheader,
                                          ExitMainLoopAt, PostLoopPreheader);
    rewriteIncomingValuesForPHIs(PostLoop.Structure, PostLoopPreheader,
                                 PostLoopRRI);
  }

  BasicBlock *NewMainLoopPreheader =
      MainLoopPreheader != Preheader ? MainLoopPreheader : nullptr;
  BasicBlock *NewBlocks[] = {PostLoopPreheader,        PreLoopRRI.PseudoExit,
                             PreLoopRRI.ExitSelector,  PostLoopRRI.PseudoExit,
                             PostLoopRRI.ExitSelector, NewMainLoopPreheader};

  // Some of the above may be nullptr, filter them out before passing to
  // addToParentLoopIfNeeded.
  auto NewBlocksEnd =
      std::remove(std::begin(NewBlocks), std::end(NewBlocks), nullptr);

  addToParentLoopIfNeeded(ArrayRef(std::begin(NewBlocks), NewBlocksEnd));

  DT.recalculate(F);

  // We need to first add all the pre and post loop blocks into the loop
  // structures (as part of createClonedLoopStructure), and then update the
  // LCSSA form and LoopSimplifyForm. This is necessary for correctly updating
  // LI when LoopSimplifyForm is generated.
  Loop *PreL = nullptr, *PostL = nullptr;
  if (!PreLoop.Blocks.empty()) {
    PreL = createClonedLoopStructure(&OriginalLoop,
                                     OriginalLoop.getParentLoop(), PreLoop.Map,
                                     /* IsSubLoop */ false);
  }

  if (!PostLoop.Blocks.empty()) {
    PostL =
        createClonedLoopStructure(&OriginalLoop, OriginalLoop.getParentLoop(),
                                  PostLoop.Map, /* IsSubLoop */ false);
  }

  // This function canonicalizes the loop into Loop-Simplify and LCSSA forms.
  auto CanonicalizeLoop = [&](Loop *L, bool IsOriginalLoop) {
    formLCSSARecursively(*L, DT, &LI, &SE);
    simplifyLoop(L, &DT, &LI, &SE, nullptr, nullptr, true);
    // Pre/post loops are slow paths, we do not need to perform any loop
    // optimizations on them.
    if (!IsOriginalLoop)
      DisableAllLoopOptsOnLoop(*L);
  };
  if (PreL)
    CanonicalizeLoop(PreL, false);
  if (PostL)
    CanonicalizeLoop(PostL, false);
  CanonicalizeLoop(&OriginalLoop, true);

  /// At this point:
  /// - We've broken a "main loop" out of the loop in a way that the "main loop"
  /// runs with the induction variable in a subset of [Begin, End).
  /// - There is no overflow when computing "main loop" exit limit.
  /// - Max latch taken count of the loop is limited.
  /// It guarantees that induction variable will not overflow iterating in the
  /// "main loop".
  if (isa<OverflowingBinaryOperator>(MainLoopStructure.IndVarBase))
    if (IsSignedPredicate)
      cast<BinaryOperator>(MainLoopStructure.IndVarBase)
          ->setHasNoSignedWrap(true);
  /// TODO: support unsigned predicate.
  /// To add NUW flag we need to prove that both operands of BO are
  /// non-negative. E.g:
  /// ...
  /// %iv.next = add nsw i32 %iv, -1
  /// %cmp = icmp ult i32 %iv.next, %n
  /// br i1 %cmp, label %loopexit, label %loop
  ///
  /// -1 is MAX_UINT in terms of unsigned int. Adding anything but zero will
  /// overflow, therefore NUW flag is not legal here.

  return true;
}
