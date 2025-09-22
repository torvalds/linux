//===- IndVarSimplify.cpp - Induction Variable Elimination ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This transformation analyzes and transforms the induction variables (and
// computations derived from them) into simpler forms suitable for subsequent
// analysis and transformation.
//
// If the trip count of a loop is computable, this pass also makes the following
// changes:
//   1. The exit condition for the loop is canonicalized to compare the
//      induction value against the exit value.  This turns loops like:
//        'for (i = 7; i*i < 1000; ++i)' into 'for (i = 0; i != 25; ++i)'
//   2. Any use outside of the loop of an expression derived from the indvar
//      is changed to compute the derived value outside of the loop, eliminating
//      the dependence on the exit value of the induction variable.  If the only
//      purpose of the loop is to compute the exit value of some derived
//      expression, this transformation will make the loop dead.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/Transforms/Utils/SimplifyIndVar.h"
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "indvars"

STATISTIC(NumWidened     , "Number of indvars widened");
STATISTIC(NumReplaced    , "Number of exit values replaced");
STATISTIC(NumLFTR        , "Number of loop exit tests replaced");
STATISTIC(NumElimExt     , "Number of IV sign/zero extends eliminated");
STATISTIC(NumElimIV      , "Number of congruent IVs eliminated");

static cl::opt<ReplaceExitVal> ReplaceExitValue(
    "replexitval", cl::Hidden, cl::init(OnlyCheapRepl),
    cl::desc("Choose the strategy to replace exit value in IndVarSimplify"),
    cl::values(
        clEnumValN(NeverRepl, "never", "never replace exit value"),
        clEnumValN(OnlyCheapRepl, "cheap",
                   "only replace exit value when the cost is cheap"),
        clEnumValN(
            UnusedIndVarInLoop, "unusedindvarinloop",
            "only replace exit value when it is an unused "
            "induction variable in the loop and has cheap replacement cost"),
        clEnumValN(NoHardUse, "noharduse",
                   "only replace exit values when loop def likely dead"),
        clEnumValN(AlwaysRepl, "always",
                   "always replace exit value whenever possible")));

static cl::opt<bool> UsePostIncrementRanges(
  "indvars-post-increment-ranges", cl::Hidden,
  cl::desc("Use post increment control-dependent ranges in IndVarSimplify"),
  cl::init(true));

static cl::opt<bool>
DisableLFTR("disable-lftr", cl::Hidden, cl::init(false),
            cl::desc("Disable Linear Function Test Replace optimization"));

static cl::opt<bool>
LoopPredication("indvars-predicate-loops", cl::Hidden, cl::init(true),
                cl::desc("Predicate conditions in read only loops"));

static cl::opt<bool>
AllowIVWidening("indvars-widen-indvars", cl::Hidden, cl::init(true),
                cl::desc("Allow widening of indvars to eliminate s/zext"));

namespace {

class IndVarSimplify {
  LoopInfo *LI;
  ScalarEvolution *SE;
  DominatorTree *DT;
  const DataLayout &DL;
  TargetLibraryInfo *TLI;
  const TargetTransformInfo *TTI;
  std::unique_ptr<MemorySSAUpdater> MSSAU;

  SmallVector<WeakTrackingVH, 16> DeadInsts;
  bool WidenIndVars;

  bool RunUnswitching = false;

  bool handleFloatingPointIV(Loop *L, PHINode *PH);
  bool rewriteNonIntegerIVs(Loop *L);

  bool simplifyAndExtend(Loop *L, SCEVExpander &Rewriter, LoopInfo *LI);
  /// Try to improve our exit conditions by converting condition from signed
  /// to unsigned or rotating computation out of the loop.
  /// (See inline comment about why this is duplicated from simplifyAndExtend)
  bool canonicalizeExitCondition(Loop *L);
  /// Try to eliminate loop exits based on analyzeable exit counts
  bool optimizeLoopExits(Loop *L, SCEVExpander &Rewriter);
  /// Try to form loop invariant tests for loop exits by changing how many
  /// iterations of the loop run when that is unobservable.
  bool predicateLoopExits(Loop *L, SCEVExpander &Rewriter);

  bool rewriteFirstIterationLoopExitValues(Loop *L);

  bool linearFunctionTestReplace(Loop *L, BasicBlock *ExitingBB,
                                 const SCEV *ExitCount,
                                 PHINode *IndVar, SCEVExpander &Rewriter);

  bool sinkUnusedInvariants(Loop *L);

public:
  IndVarSimplify(LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT,
                 const DataLayout &DL, TargetLibraryInfo *TLI,
                 TargetTransformInfo *TTI, MemorySSA *MSSA, bool WidenIndVars)
      : LI(LI), SE(SE), DT(DT), DL(DL), TLI(TLI), TTI(TTI),
        WidenIndVars(WidenIndVars) {
    if (MSSA)
      MSSAU = std::make_unique<MemorySSAUpdater>(MSSA);
  }

  bool run(Loop *L);

  bool runUnswitching() const { return RunUnswitching; }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// rewriteNonIntegerIVs and helpers. Prefer integer IVs.
//===----------------------------------------------------------------------===//

/// Convert APF to an integer, if possible.
static bool ConvertToSInt(const APFloat &APF, int64_t &IntVal) {
  bool isExact = false;
  // See if we can convert this to an int64_t
  uint64_t UIntVal;
  if (APF.convertToInteger(MutableArrayRef(UIntVal), 64, true,
                           APFloat::rmTowardZero, &isExact) != APFloat::opOK ||
      !isExact)
    return false;
  IntVal = UIntVal;
  return true;
}

/// If the loop has floating induction variable then insert corresponding
/// integer induction variable if possible.
/// For example,
/// for(double i = 0; i < 10000; ++i)
///   bar(i)
/// is converted into
/// for(int i = 0; i < 10000; ++i)
///   bar((double)i);
bool IndVarSimplify::handleFloatingPointIV(Loop *L, PHINode *PN) {
  unsigned IncomingEdge = L->contains(PN->getIncomingBlock(0));
  unsigned BackEdge     = IncomingEdge^1;

  // Check incoming value.
  auto *InitValueVal = dyn_cast<ConstantFP>(PN->getIncomingValue(IncomingEdge));

  int64_t InitValue;
  if (!InitValueVal || !ConvertToSInt(InitValueVal->getValueAPF(), InitValue))
    return false;

  // Check IV increment. Reject this PN if increment operation is not
  // an add or increment value can not be represented by an integer.
  auto *Incr = dyn_cast<BinaryOperator>(PN->getIncomingValue(BackEdge));
  if (Incr == nullptr || Incr->getOpcode() != Instruction::FAdd) return false;

  // If this is not an add of the PHI with a constantfp, or if the constant fp
  // is not an integer, bail out.
  ConstantFP *IncValueVal = dyn_cast<ConstantFP>(Incr->getOperand(1));
  int64_t IncValue;
  if (IncValueVal == nullptr || Incr->getOperand(0) != PN ||
      !ConvertToSInt(IncValueVal->getValueAPF(), IncValue))
    return false;

  // Check Incr uses. One user is PN and the other user is an exit condition
  // used by the conditional terminator.
  Value::user_iterator IncrUse = Incr->user_begin();
  Instruction *U1 = cast<Instruction>(*IncrUse++);
  if (IncrUse == Incr->user_end()) return false;
  Instruction *U2 = cast<Instruction>(*IncrUse++);
  if (IncrUse != Incr->user_end()) return false;

  // Find exit condition, which is an fcmp.  If it doesn't exist, or if it isn't
  // only used by a branch, we can't transform it.
  FCmpInst *Compare = dyn_cast<FCmpInst>(U1);
  if (!Compare)
    Compare = dyn_cast<FCmpInst>(U2);
  if (!Compare || !Compare->hasOneUse() ||
      !isa<BranchInst>(Compare->user_back()))
    return false;

  BranchInst *TheBr = cast<BranchInst>(Compare->user_back());

  // We need to verify that the branch actually controls the iteration count
  // of the loop.  If not, the new IV can overflow and no one will notice.
  // The branch block must be in the loop and one of the successors must be out
  // of the loop.
  assert(TheBr->isConditional() && "Can't use fcmp if not conditional");
  if (!L->contains(TheBr->getParent()) ||
      (L->contains(TheBr->getSuccessor(0)) &&
       L->contains(TheBr->getSuccessor(1))))
    return false;

  // If it isn't a comparison with an integer-as-fp (the exit value), we can't
  // transform it.
  ConstantFP *ExitValueVal = dyn_cast<ConstantFP>(Compare->getOperand(1));
  int64_t ExitValue;
  if (ExitValueVal == nullptr ||
      !ConvertToSInt(ExitValueVal->getValueAPF(), ExitValue))
    return false;

  // Find new predicate for integer comparison.
  CmpInst::Predicate NewPred = CmpInst::BAD_ICMP_PREDICATE;
  switch (Compare->getPredicate()) {
  default: return false;  // Unknown comparison.
  case CmpInst::FCMP_OEQ:
  case CmpInst::FCMP_UEQ: NewPred = CmpInst::ICMP_EQ; break;
  case CmpInst::FCMP_ONE:
  case CmpInst::FCMP_UNE: NewPred = CmpInst::ICMP_NE; break;
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_UGT: NewPred = CmpInst::ICMP_SGT; break;
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_UGE: NewPred = CmpInst::ICMP_SGE; break;
  case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_ULT: NewPred = CmpInst::ICMP_SLT; break;
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_ULE: NewPred = CmpInst::ICMP_SLE; break;
  }

  // We convert the floating point induction variable to a signed i32 value if
  // we can.  This is only safe if the comparison will not overflow in a way
  // that won't be trapped by the integer equivalent operations.  Check for this
  // now.
  // TODO: We could use i64 if it is native and the range requires it.

  // The start/stride/exit values must all fit in signed i32.
  if (!isInt<32>(InitValue) || !isInt<32>(IncValue) || !isInt<32>(ExitValue))
    return false;

  // If not actually striding (add x, 0.0), avoid touching the code.
  if (IncValue == 0)
    return false;

  // Positive and negative strides have different safety conditions.
  if (IncValue > 0) {
    // If we have a positive stride, we require the init to be less than the
    // exit value.
    if (InitValue >= ExitValue)
      return false;

    uint32_t Range = uint32_t(ExitValue-InitValue);
    // Check for infinite loop, either:
    // while (i <= Exit) or until (i > Exit)
    if (NewPred == CmpInst::ICMP_SLE || NewPred == CmpInst::ICMP_SGT) {
      if (++Range == 0) return false;  // Range overflows.
    }

    unsigned Leftover = Range % uint32_t(IncValue);

    // If this is an equality comparison, we require that the strided value
    // exactly land on the exit value, otherwise the IV condition will wrap
    // around and do things the fp IV wouldn't.
    if ((NewPred == CmpInst::ICMP_EQ || NewPred == CmpInst::ICMP_NE) &&
        Leftover != 0)
      return false;

    // If the stride would wrap around the i32 before exiting, we can't
    // transform the IV.
    if (Leftover != 0 && int32_t(ExitValue+IncValue) < ExitValue)
      return false;
  } else {
    // If we have a negative stride, we require the init to be greater than the
    // exit value.
    if (InitValue <= ExitValue)
      return false;

    uint32_t Range = uint32_t(InitValue-ExitValue);
    // Check for infinite loop, either:
    // while (i >= Exit) or until (i < Exit)
    if (NewPred == CmpInst::ICMP_SGE || NewPred == CmpInst::ICMP_SLT) {
      if (++Range == 0) return false;  // Range overflows.
    }

    unsigned Leftover = Range % uint32_t(-IncValue);

    // If this is an equality comparison, we require that the strided value
    // exactly land on the exit value, otherwise the IV condition will wrap
    // around and do things the fp IV wouldn't.
    if ((NewPred == CmpInst::ICMP_EQ || NewPred == CmpInst::ICMP_NE) &&
        Leftover != 0)
      return false;

    // If the stride would wrap around the i32 before exiting, we can't
    // transform the IV.
    if (Leftover != 0 && int32_t(ExitValue+IncValue) > ExitValue)
      return false;
  }

  IntegerType *Int32Ty = Type::getInt32Ty(PN->getContext());

  // Insert new integer induction variable.
  PHINode *NewPHI =
      PHINode::Create(Int32Ty, 2, PN->getName() + ".int", PN->getIterator());
  NewPHI->addIncoming(ConstantInt::get(Int32Ty, InitValue),
                      PN->getIncomingBlock(IncomingEdge));
  NewPHI->setDebugLoc(PN->getDebugLoc());

  Instruction *NewAdd =
      BinaryOperator::CreateAdd(NewPHI, ConstantInt::get(Int32Ty, IncValue),
                                Incr->getName() + ".int", Incr->getIterator());
  NewAdd->setDebugLoc(Incr->getDebugLoc());
  NewPHI->addIncoming(NewAdd, PN->getIncomingBlock(BackEdge));

  ICmpInst *NewCompare =
      new ICmpInst(TheBr->getIterator(), NewPred, NewAdd,
                   ConstantInt::get(Int32Ty, ExitValue), Compare->getName());
  NewCompare->setDebugLoc(Compare->getDebugLoc());

  // In the following deletions, PN may become dead and may be deleted.
  // Use a WeakTrackingVH to observe whether this happens.
  WeakTrackingVH WeakPH = PN;

  // Delete the old floating point exit comparison.  The branch starts using the
  // new comparison.
  NewCompare->takeName(Compare);
  Compare->replaceAllUsesWith(NewCompare);
  RecursivelyDeleteTriviallyDeadInstructions(Compare, TLI, MSSAU.get());

  // Delete the old floating point increment.
  Incr->replaceAllUsesWith(PoisonValue::get(Incr->getType()));
  RecursivelyDeleteTriviallyDeadInstructions(Incr, TLI, MSSAU.get());

  // If the FP induction variable still has uses, this is because something else
  // in the loop uses its value.  In order to canonicalize the induction
  // variable, we chose to eliminate the IV and rewrite it in terms of an
  // int->fp cast.
  //
  // We give preference to sitofp over uitofp because it is faster on most
  // platforms.
  if (WeakPH) {
    Instruction *Conv = new SIToFPInst(NewPHI, PN->getType(), "indvar.conv",
                                       PN->getParent()->getFirstInsertionPt());
    Conv->setDebugLoc(PN->getDebugLoc());
    PN->replaceAllUsesWith(Conv);
    RecursivelyDeleteTriviallyDeadInstructions(PN, TLI, MSSAU.get());
  }
  return true;
}

bool IndVarSimplify::rewriteNonIntegerIVs(Loop *L) {
  // First step.  Check to see if there are any floating-point recurrences.
  // If there are, change them into integer recurrences, permitting analysis by
  // the SCEV routines.
  BasicBlock *Header = L->getHeader();

  SmallVector<WeakTrackingVH, 8> PHIs;
  for (PHINode &PN : Header->phis())
    PHIs.push_back(&PN);

  bool Changed = false;
  for (WeakTrackingVH &PHI : PHIs)
    if (PHINode *PN = dyn_cast_or_null<PHINode>(&*PHI))
      Changed |= handleFloatingPointIV(L, PN);

  // If the loop previously had floating-point IV, ScalarEvolution
  // may not have been able to compute a trip count. Now that we've done some
  // re-writing, the trip count may be computable.
  if (Changed)
    SE->forgetLoop(L);
  return Changed;
}

//===---------------------------------------------------------------------===//
// rewriteFirstIterationLoopExitValues: Rewrite loop exit values if we know
// they will exit at the first iteration.
//===---------------------------------------------------------------------===//

/// Check to see if this loop has loop invariant conditions which lead to loop
/// exits. If so, we know that if the exit path is taken, it is at the first
/// loop iteration. This lets us predict exit values of PHI nodes that live in
/// loop header.
bool IndVarSimplify::rewriteFirstIterationLoopExitValues(Loop *L) {
  // Verify the input to the pass is already in LCSSA form.
  assert(L->isLCSSAForm(*DT));

  SmallVector<BasicBlock *, 8> ExitBlocks;
  L->getUniqueExitBlocks(ExitBlocks);

  bool MadeAnyChanges = false;
  for (auto *ExitBB : ExitBlocks) {
    // If there are no more PHI nodes in this exit block, then no more
    // values defined inside the loop are used on this path.
    for (PHINode &PN : ExitBB->phis()) {
      for (unsigned IncomingValIdx = 0, E = PN.getNumIncomingValues();
           IncomingValIdx != E; ++IncomingValIdx) {
        auto *IncomingBB = PN.getIncomingBlock(IncomingValIdx);

        // Can we prove that the exit must run on the first iteration if it
        // runs at all?  (i.e. early exits are fine for our purposes, but
        // traces which lead to this exit being taken on the 2nd iteration
        // aren't.)  Note that this is about whether the exit branch is
        // executed, not about whether it is taken.
        if (!L->getLoopLatch() ||
            !DT->dominates(IncomingBB, L->getLoopLatch()))
          continue;

        // Get condition that leads to the exit path.
        auto *TermInst = IncomingBB->getTerminator();

        Value *Cond = nullptr;
        if (auto *BI = dyn_cast<BranchInst>(TermInst)) {
          // Must be a conditional branch, otherwise the block
          // should not be in the loop.
          Cond = BI->getCondition();
        } else if (auto *SI = dyn_cast<SwitchInst>(TermInst))
          Cond = SI->getCondition();
        else
          continue;

        if (!L->isLoopInvariant(Cond))
          continue;

        auto *ExitVal = dyn_cast<PHINode>(PN.getIncomingValue(IncomingValIdx));

        // Only deal with PHIs in the loop header.
        if (!ExitVal || ExitVal->getParent() != L->getHeader())
          continue;

        // If ExitVal is a PHI on the loop header, then we know its
        // value along this exit because the exit can only be taken
        // on the first iteration.
        auto *LoopPreheader = L->getLoopPreheader();
        assert(LoopPreheader && "Invalid loop");
        int PreheaderIdx = ExitVal->getBasicBlockIndex(LoopPreheader);
        if (PreheaderIdx != -1) {
          assert(ExitVal->getParent() == L->getHeader() &&
                 "ExitVal must be in loop header");
          MadeAnyChanges = true;
          PN.setIncomingValue(IncomingValIdx,
                              ExitVal->getIncomingValue(PreheaderIdx));
          SE->forgetValue(&PN);
        }
      }
    }
  }
  return MadeAnyChanges;
}

//===----------------------------------------------------------------------===//
//  IV Widening - Extend the width of an IV to cover its widest uses.
//===----------------------------------------------------------------------===//

/// Update information about the induction variable that is extended by this
/// sign or zero extend operation. This is used to determine the final width of
/// the IV before actually widening it.
static void visitIVCast(CastInst *Cast, WideIVInfo &WI,
                        ScalarEvolution *SE,
                        const TargetTransformInfo *TTI) {
  bool IsSigned = Cast->getOpcode() == Instruction::SExt;
  if (!IsSigned && Cast->getOpcode() != Instruction::ZExt)
    return;

  Type *Ty = Cast->getType();
  uint64_t Width = SE->getTypeSizeInBits(Ty);
  if (!Cast->getDataLayout().isLegalInteger(Width))
    return;

  // Check that `Cast` actually extends the induction variable (we rely on this
  // later).  This takes care of cases where `Cast` is extending a truncation of
  // the narrow induction variable, and thus can end up being narrower than the
  // "narrow" induction variable.
  uint64_t NarrowIVWidth = SE->getTypeSizeInBits(WI.NarrowIV->getType());
  if (NarrowIVWidth >= Width)
    return;

  // Cast is either an sext or zext up to this point.
  // We should not widen an indvar if arithmetics on the wider indvar are more
  // expensive than those on the narrower indvar. We check only the cost of ADD
  // because at least an ADD is required to increment the induction variable. We
  // could compute more comprehensively the cost of all instructions on the
  // induction variable when necessary.
  if (TTI &&
      TTI->getArithmeticInstrCost(Instruction::Add, Ty) >
          TTI->getArithmeticInstrCost(Instruction::Add,
                                      Cast->getOperand(0)->getType())) {
    return;
  }

  if (!WI.WidestNativeType ||
      Width > SE->getTypeSizeInBits(WI.WidestNativeType)) {
    WI.WidestNativeType = SE->getEffectiveSCEVType(Ty);
    WI.IsSigned = IsSigned;
    return;
  }

  // We extend the IV to satisfy the sign of its user(s), or 'signed'
  // if there are multiple users with both sign- and zero extensions,
  // in order not to introduce nondeterministic behaviour based on the
  // unspecified order of a PHI nodes' users-iterator.
  WI.IsSigned |= IsSigned;
}

//===----------------------------------------------------------------------===//
//  Live IV Reduction - Minimize IVs live across the loop.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Simplification of IV users based on SCEV evaluation.
//===----------------------------------------------------------------------===//

namespace {

class IndVarSimplifyVisitor : public IVVisitor {
  ScalarEvolution *SE;
  const TargetTransformInfo *TTI;
  PHINode *IVPhi;

public:
  WideIVInfo WI;

  IndVarSimplifyVisitor(PHINode *IV, ScalarEvolution *SCEV,
                        const TargetTransformInfo *TTI,
                        const DominatorTree *DTree)
    : SE(SCEV), TTI(TTI), IVPhi(IV) {
    DT = DTree;
    WI.NarrowIV = IVPhi;
  }

  // Implement the interface used by simplifyUsersOfIV.
  void visitCast(CastInst *Cast) override { visitIVCast(Cast, WI, SE, TTI); }
};

} // end anonymous namespace

/// Iteratively perform simplification on a worklist of IV users. Each
/// successive simplification may push more users which may themselves be
/// candidates for simplification.
///
/// Sign/Zero extend elimination is interleaved with IV simplification.
bool IndVarSimplify::simplifyAndExtend(Loop *L,
                                       SCEVExpander &Rewriter,
                                       LoopInfo *LI) {
  SmallVector<WideIVInfo, 8> WideIVs;

  auto *GuardDecl = L->getBlocks()[0]->getModule()->getFunction(
          Intrinsic::getName(Intrinsic::experimental_guard));
  bool HasGuards = GuardDecl && !GuardDecl->use_empty();

  SmallVector<PHINode *, 8> LoopPhis;
  for (PHINode &PN : L->getHeader()->phis())
    LoopPhis.push_back(&PN);

  // Each round of simplification iterates through the SimplifyIVUsers worklist
  // for all current phis, then determines whether any IVs can be
  // widened. Widening adds new phis to LoopPhis, inducing another round of
  // simplification on the wide IVs.
  bool Changed = false;
  while (!LoopPhis.empty()) {
    // Evaluate as many IV expressions as possible before widening any IVs. This
    // forces SCEV to set no-wrap flags before evaluating sign/zero
    // extension. The first time SCEV attempts to normalize sign/zero extension,
    // the result becomes final. So for the most predictable results, we delay
    // evaluation of sign/zero extend evaluation until needed, and avoid running
    // other SCEV based analysis prior to simplifyAndExtend.
    do {
      PHINode *CurrIV = LoopPhis.pop_back_val();

      // Information about sign/zero extensions of CurrIV.
      IndVarSimplifyVisitor Visitor(CurrIV, SE, TTI, DT);

      const auto &[C, U] = simplifyUsersOfIV(CurrIV, SE, DT, LI, TTI, DeadInsts,
                                             Rewriter, &Visitor);

      Changed |= C;
      RunUnswitching |= U;
      if (Visitor.WI.WidestNativeType) {
        WideIVs.push_back(Visitor.WI);
      }
    } while(!LoopPhis.empty());

    // Continue if we disallowed widening.
    if (!WidenIndVars)
      continue;

    for (; !WideIVs.empty(); WideIVs.pop_back()) {
      unsigned ElimExt;
      unsigned Widened;
      if (PHINode *WidePhi = createWideIV(WideIVs.back(), LI, SE, Rewriter,
                                          DT, DeadInsts, ElimExt, Widened,
                                          HasGuards, UsePostIncrementRanges)) {
        NumElimExt += ElimExt;
        NumWidened += Widened;
        Changed = true;
        LoopPhis.push_back(WidePhi);
      }
    }
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
//  linearFunctionTestReplace and its kin. Rewrite the loop exit condition.
//===----------------------------------------------------------------------===//

/// Given an Value which is hoped to be part of an add recurance in the given
/// loop, return the associated Phi node if so.  Otherwise, return null.  Note
/// that this is less general than SCEVs AddRec checking.
static PHINode *getLoopPhiForCounter(Value *IncV, Loop *L) {
  Instruction *IncI = dyn_cast<Instruction>(IncV);
  if (!IncI)
    return nullptr;

  switch (IncI->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
    break;
  case Instruction::GetElementPtr:
    // An IV counter must preserve its type.
    if (IncI->getNumOperands() == 2)
      break;
    [[fallthrough]];
  default:
    return nullptr;
  }

  PHINode *Phi = dyn_cast<PHINode>(IncI->getOperand(0));
  if (Phi && Phi->getParent() == L->getHeader()) {
    if (L->isLoopInvariant(IncI->getOperand(1)))
      return Phi;
    return nullptr;
  }
  if (IncI->getOpcode() == Instruction::GetElementPtr)
    return nullptr;

  // Allow add/sub to be commuted.
  Phi = dyn_cast<PHINode>(IncI->getOperand(1));
  if (Phi && Phi->getParent() == L->getHeader()) {
    if (L->isLoopInvariant(IncI->getOperand(0)))
      return Phi;
  }
  return nullptr;
}

/// Whether the current loop exit test is based on this value.  Currently this
/// is limited to a direct use in the loop condition.
static bool isLoopExitTestBasedOn(Value *V, BasicBlock *ExitingBB) {
  BranchInst *BI = cast<BranchInst>(ExitingBB->getTerminator());
  ICmpInst *ICmp = dyn_cast<ICmpInst>(BI->getCondition());
  // TODO: Allow non-icmp loop test.
  if (!ICmp)
    return false;

  // TODO: Allow indirect use.
  return ICmp->getOperand(0) == V || ICmp->getOperand(1) == V;
}

/// linearFunctionTestReplace policy. Return true unless we can show that the
/// current exit test is already sufficiently canonical.
static bool needsLFTR(Loop *L, BasicBlock *ExitingBB) {
  assert(L->getLoopLatch() && "Must be in simplified form");

  // Avoid converting a constant or loop invariant test back to a runtime
  // test.  This is critical for when SCEV's cached ExitCount is less precise
  // than the current IR (such as after we've proven a particular exit is
  // actually dead and thus the BE count never reaches our ExitCount.)
  BranchInst *BI = cast<BranchInst>(ExitingBB->getTerminator());
  if (L->isLoopInvariant(BI->getCondition()))
    return false;

  // Do LFTR to simplify the exit condition to an ICMP.
  ICmpInst *Cond = dyn_cast<ICmpInst>(BI->getCondition());
  if (!Cond)
    return true;

  // Do LFTR to simplify the exit ICMP to EQ/NE
  ICmpInst::Predicate Pred = Cond->getPredicate();
  if (Pred != ICmpInst::ICMP_NE && Pred != ICmpInst::ICMP_EQ)
    return true;

  // Look for a loop invariant RHS
  Value *LHS = Cond->getOperand(0);
  Value *RHS = Cond->getOperand(1);
  if (!L->isLoopInvariant(RHS)) {
    if (!L->isLoopInvariant(LHS))
      return true;
    std::swap(LHS, RHS);
  }
  // Look for a simple IV counter LHS
  PHINode *Phi = dyn_cast<PHINode>(LHS);
  if (!Phi)
    Phi = getLoopPhiForCounter(LHS, L);

  if (!Phi)
    return true;

  // Do LFTR if PHI node is defined in the loop, but is *not* a counter.
  int Idx = Phi->getBasicBlockIndex(L->getLoopLatch());
  if (Idx < 0)
    return true;

  // Do LFTR if the exit condition's IV is *not* a simple counter.
  Value *IncV = Phi->getIncomingValue(Idx);
  return Phi != getLoopPhiForCounter(IncV, L);
}

/// Recursive helper for hasConcreteDef(). Unfortunately, this currently boils
/// down to checking that all operands are constant and listing instructions
/// that may hide undef.
static bool hasConcreteDefImpl(Value *V, SmallPtrSetImpl<Value*> &Visited,
                               unsigned Depth) {
  if (isa<Constant>(V))
    return !isa<UndefValue>(V);

  if (Depth >= 6)
    return false;

  // Conservatively handle non-constant non-instructions. For example, Arguments
  // may be undef.
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  // Load and return values may be undef.
  if(I->mayReadFromMemory() || isa<CallInst>(I) || isa<InvokeInst>(I))
    return false;

  // Optimistically handle other instructions.
  for (Value *Op : I->operands()) {
    if (!Visited.insert(Op).second)
      continue;
    if (!hasConcreteDefImpl(Op, Visited, Depth+1))
      return false;
  }
  return true;
}

/// Return true if the given value is concrete. We must prove that undef can
/// never reach it.
///
/// TODO: If we decide that this is a good approach to checking for undef, we
/// may factor it into a common location.
static bool hasConcreteDef(Value *V) {
  SmallPtrSet<Value*, 8> Visited;
  Visited.insert(V);
  return hasConcreteDefImpl(V, Visited, 0);
}

/// Return true if the given phi is a "counter" in L.  A counter is an
/// add recurance (of integer or pointer type) with an arbitrary start, and a
/// step of 1.  Note that L must have exactly one latch.
static bool isLoopCounter(PHINode* Phi, Loop *L,
                          ScalarEvolution *SE) {
  assert(Phi->getParent() == L->getHeader());
  assert(L->getLoopLatch());

  if (!SE->isSCEVable(Phi->getType()))
    return false;

  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(Phi));
  if (!AR || AR->getLoop() != L || !AR->isAffine())
    return false;

  const SCEV *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(*SE));
  if (!Step || !Step->isOne())
    return false;

  int LatchIdx = Phi->getBasicBlockIndex(L->getLoopLatch());
  Value *IncV = Phi->getIncomingValue(LatchIdx);
  return (getLoopPhiForCounter(IncV, L) == Phi &&
          isa<SCEVAddRecExpr>(SE->getSCEV(IncV)));
}

/// Search the loop header for a loop counter (anadd rec w/step of one)
/// suitable for use by LFTR.  If multiple counters are available, select the
/// "best" one based profitable heuristics.
///
/// BECount may be an i8* pointer type. The pointer difference is already
/// valid count without scaling the address stride, so it remains a pointer
/// expression as far as SCEV is concerned.
static PHINode *FindLoopCounter(Loop *L, BasicBlock *ExitingBB,
                                const SCEV *BECount,
                                ScalarEvolution *SE, DominatorTree *DT) {
  uint64_t BCWidth = SE->getTypeSizeInBits(BECount->getType());

  Value *Cond = cast<BranchInst>(ExitingBB->getTerminator())->getCondition();

  // Loop over all of the PHI nodes, looking for a simple counter.
  PHINode *BestPhi = nullptr;
  const SCEV *BestInit = nullptr;
  BasicBlock *LatchBlock = L->getLoopLatch();
  assert(LatchBlock && "Must be in simplified form");
  const DataLayout &DL = L->getHeader()->getDataLayout();

  for (BasicBlock::iterator I = L->getHeader()->begin(); isa<PHINode>(I); ++I) {
    PHINode *Phi = cast<PHINode>(I);
    if (!isLoopCounter(Phi, L, SE))
      continue;

    const auto *AR = cast<SCEVAddRecExpr>(SE->getSCEV(Phi));

    // AR may be a pointer type, while BECount is an integer type.
    // AR may be wider than BECount. With eq/ne tests overflow is immaterial.
    // AR may not be a narrower type, or we may never exit.
    uint64_t PhiWidth = SE->getTypeSizeInBits(AR->getType());
    if (PhiWidth < BCWidth || !DL.isLegalInteger(PhiWidth))
      continue;

    // Avoid reusing a potentially undef value to compute other values that may
    // have originally had a concrete definition.
    if (!hasConcreteDef(Phi)) {
      // We explicitly allow unknown phis as long as they are already used by
      // the loop exit test.  This is legal since performing LFTR could not
      // increase the number of undef users.
      Value *IncPhi = Phi->getIncomingValueForBlock(LatchBlock);
      if (!isLoopExitTestBasedOn(Phi, ExitingBB) &&
          !isLoopExitTestBasedOn(IncPhi, ExitingBB))
        continue;
    }

    // Avoid introducing undefined behavior due to poison which didn't exist in
    // the original program.  (Annoyingly, the rules for poison and undef
    // propagation are distinct, so this does NOT cover the undef case above.)
    // We have to ensure that we don't introduce UB by introducing a use on an
    // iteration where said IV produces poison.  Our strategy here differs for
    // pointers and integer IVs.  For integers, we strip and reinfer as needed,
    // see code in linearFunctionTestReplace.  For pointers, we restrict
    // transforms as there is no good way to reinfer inbounds once lost.
    if (!Phi->getType()->isIntegerTy() &&
        !mustExecuteUBIfPoisonOnPathTo(Phi, ExitingBB->getTerminator(), DT))
      continue;

    const SCEV *Init = AR->getStart();

    if (BestPhi && !isAlmostDeadIV(BestPhi, LatchBlock, Cond)) {
      // Don't force a live loop counter if another IV can be used.
      if (isAlmostDeadIV(Phi, LatchBlock, Cond))
        continue;

      // Prefer to count-from-zero. This is a more "canonical" counter form. It
      // also prefers integer to pointer IVs.
      if (BestInit->isZero() != Init->isZero()) {
        if (BestInit->isZero())
          continue;
      }
      // If two IVs both count from zero or both count from nonzero then the
      // narrower is likely a dead phi that has been widened. Use the wider phi
      // to allow the other to be eliminated.
      else if (PhiWidth <= SE->getTypeSizeInBits(BestPhi->getType()))
        continue;
    }
    BestPhi = Phi;
    BestInit = Init;
  }
  return BestPhi;
}

/// Insert an IR expression which computes the value held by the IV IndVar
/// (which must be an loop counter w/unit stride) after the backedge of loop L
/// is taken ExitCount times.
static Value *genLoopLimit(PHINode *IndVar, BasicBlock *ExitingBB,
                           const SCEV *ExitCount, bool UsePostInc, Loop *L,
                           SCEVExpander &Rewriter, ScalarEvolution *SE) {
  assert(isLoopCounter(IndVar, L, SE));
  assert(ExitCount->getType()->isIntegerTy() && "exit count must be integer");
  const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(SE->getSCEV(IndVar));
  assert(AR->getStepRecurrence(*SE)->isOne() && "only handles unit stride");

  // For integer IVs, truncate the IV before computing the limit unless we
  // know apriori that the limit must be a constant when evaluated in the
  // bitwidth of the IV.  We prefer (potentially) keeping a truncate of the
  // IV in the loop over a (potentially) expensive expansion of the widened
  // exit count add(zext(add)) expression.
  if (IndVar->getType()->isIntegerTy() &&
      SE->getTypeSizeInBits(AR->getType()) >
      SE->getTypeSizeInBits(ExitCount->getType())) {
    const SCEV *IVInit = AR->getStart();
    if (!isa<SCEVConstant>(IVInit) || !isa<SCEVConstant>(ExitCount))
      AR = cast<SCEVAddRecExpr>(SE->getTruncateExpr(AR, ExitCount->getType()));
  }

  const SCEVAddRecExpr *ARBase = UsePostInc ? AR->getPostIncExpr(*SE) : AR;
  const SCEV *IVLimit = ARBase->evaluateAtIteration(ExitCount, *SE);
  assert(SE->isLoopInvariant(IVLimit, L) &&
         "Computed iteration count is not loop invariant!");
  return Rewriter.expandCodeFor(IVLimit, ARBase->getType(),
                                ExitingBB->getTerminator());
}

/// This method rewrites the exit condition of the loop to be a canonical !=
/// comparison against the incremented loop induction variable.  This pass is
/// able to rewrite the exit tests of any loop where the SCEV analysis can
/// determine a loop-invariant trip count of the loop, which is actually a much
/// broader range than just linear tests.
bool IndVarSimplify::
linearFunctionTestReplace(Loop *L, BasicBlock *ExitingBB,
                          const SCEV *ExitCount,
                          PHINode *IndVar, SCEVExpander &Rewriter) {
  assert(L->getLoopLatch() && "Loop no longer in simplified form?");
  assert(isLoopCounter(IndVar, L, SE));
  Instruction * const IncVar =
    cast<Instruction>(IndVar->getIncomingValueForBlock(L->getLoopLatch()));

  // Initialize CmpIndVar to the preincremented IV.
  Value *CmpIndVar = IndVar;
  bool UsePostInc = false;

  // If the exiting block is the same as the backedge block, we prefer to
  // compare against the post-incremented value, otherwise we must compare
  // against the preincremented value.
  if (ExitingBB == L->getLoopLatch()) {
    // For pointer IVs, we chose to not strip inbounds which requires us not
    // to add a potentially UB introducing use.  We need to either a) show
    // the loop test we're modifying is already in post-inc form, or b) show
    // that adding a use must not introduce UB.
    bool SafeToPostInc =
        IndVar->getType()->isIntegerTy() ||
        isLoopExitTestBasedOn(IncVar, ExitingBB) ||
        mustExecuteUBIfPoisonOnPathTo(IncVar, ExitingBB->getTerminator(), DT);
    if (SafeToPostInc) {
      UsePostInc = true;
      CmpIndVar = IncVar;
    }
  }

  // It may be necessary to drop nowrap flags on the incrementing instruction
  // if either LFTR moves from a pre-inc check to a post-inc check (in which
  // case the increment might have previously been poison on the last iteration
  // only) or if LFTR switches to a different IV that was previously dynamically
  // dead (and as such may be arbitrarily poison). We remove any nowrap flags
  // that SCEV didn't infer for the post-inc addrec (even if we use a pre-inc
  // check), because the pre-inc addrec flags may be adopted from the original
  // instruction, while SCEV has to explicitly prove the post-inc nowrap flags.
  // TODO: This handling is inaccurate for one case: If we switch to a
  // dynamically dead IV that wraps on the first loop iteration only, which is
  // not covered by the post-inc addrec. (If the new IV was not dynamically
  // dead, it could not be poison on the first iteration in the first place.)
  if (auto *BO = dyn_cast<BinaryOperator>(IncVar)) {
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(SE->getSCEV(IncVar));
    if (BO->hasNoUnsignedWrap())
      BO->setHasNoUnsignedWrap(AR->hasNoUnsignedWrap());
    if (BO->hasNoSignedWrap())
      BO->setHasNoSignedWrap(AR->hasNoSignedWrap());
  }

  Value *ExitCnt = genLoopLimit(
      IndVar, ExitingBB, ExitCount, UsePostInc, L, Rewriter, SE);
  assert(ExitCnt->getType()->isPointerTy() ==
             IndVar->getType()->isPointerTy() &&
         "genLoopLimit missed a cast");

  // Insert a new icmp_ne or icmp_eq instruction before the branch.
  BranchInst *BI = cast<BranchInst>(ExitingBB->getTerminator());
  ICmpInst::Predicate P;
  if (L->contains(BI->getSuccessor(0)))
    P = ICmpInst::ICMP_NE;
  else
    P = ICmpInst::ICMP_EQ;

  IRBuilder<> Builder(BI);

  // The new loop exit condition should reuse the debug location of the
  // original loop exit condition.
  if (auto *Cond = dyn_cast<Instruction>(BI->getCondition()))
    Builder.SetCurrentDebugLocation(Cond->getDebugLoc());

  // For integer IVs, if we evaluated the limit in the narrower bitwidth to
  // avoid the expensive expansion of the limit expression in the wider type,
  // emit a truncate to narrow the IV to the ExitCount type.  This is safe
  // since we know (from the exit count bitwidth), that we can't self-wrap in
  // the narrower type.
  unsigned CmpIndVarSize = SE->getTypeSizeInBits(CmpIndVar->getType());
  unsigned ExitCntSize = SE->getTypeSizeInBits(ExitCnt->getType());
  if (CmpIndVarSize > ExitCntSize) {
    assert(!CmpIndVar->getType()->isPointerTy() &&
           !ExitCnt->getType()->isPointerTy());

    // Before resorting to actually inserting the truncate, use the same
    // reasoning as from SimplifyIndvar::eliminateTrunc to see if we can extend
    // the other side of the comparison instead.  We still evaluate the limit
    // in the narrower bitwidth, we just prefer a zext/sext outside the loop to
    // a truncate within in.
    bool Extended = false;
    const SCEV *IV = SE->getSCEV(CmpIndVar);
    const SCEV *TruncatedIV = SE->getTruncateExpr(IV, ExitCnt->getType());
    const SCEV *ZExtTrunc =
      SE->getZeroExtendExpr(TruncatedIV, CmpIndVar->getType());

    if (ZExtTrunc == IV) {
      Extended = true;
      ExitCnt = Builder.CreateZExt(ExitCnt, IndVar->getType(),
                                   "wide.trip.count");
    } else {
      const SCEV *SExtTrunc =
        SE->getSignExtendExpr(TruncatedIV, CmpIndVar->getType());
      if (SExtTrunc == IV) {
        Extended = true;
        ExitCnt = Builder.CreateSExt(ExitCnt, IndVar->getType(),
                                     "wide.trip.count");
      }
    }

    if (Extended) {
      bool Discard;
      L->makeLoopInvariant(ExitCnt, Discard);
    } else
      CmpIndVar = Builder.CreateTrunc(CmpIndVar, ExitCnt->getType(),
                                      "lftr.wideiv");
  }
  LLVM_DEBUG(dbgs() << "INDVARS: Rewriting loop exit condition to:\n"
                    << "      LHS:" << *CmpIndVar << '\n'
                    << "       op:\t" << (P == ICmpInst::ICMP_NE ? "!=" : "==")
                    << "\n"
                    << "      RHS:\t" << *ExitCnt << "\n"
                    << "ExitCount:\t" << *ExitCount << "\n"
                    << "  was: " << *BI->getCondition() << "\n");

  Value *Cond = Builder.CreateICmp(P, CmpIndVar, ExitCnt, "exitcond");
  Value *OrigCond = BI->getCondition();
  // It's tempting to use replaceAllUsesWith here to fully replace the old
  // comparison, but that's not immediately safe, since users of the old
  // comparison may not be dominated by the new comparison. Instead, just
  // update the branch to use the new comparison; in the common case this
  // will make old comparison dead.
  BI->setCondition(Cond);
  DeadInsts.emplace_back(OrigCond);

  ++NumLFTR;
  return true;
}

//===----------------------------------------------------------------------===//
//  sinkUnusedInvariants. A late subpass to cleanup loop preheaders.
//===----------------------------------------------------------------------===//

/// If there's a single exit block, sink any loop-invariant values that
/// were defined in the preheader but not used inside the loop into the
/// exit block to reduce register pressure in the loop.
bool IndVarSimplify::sinkUnusedInvariants(Loop *L) {
  BasicBlock *ExitBlock = L->getExitBlock();
  if (!ExitBlock) return false;

  BasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader) return false;

  bool MadeAnyChanges = false;
  BasicBlock::iterator InsertPt = ExitBlock->getFirstInsertionPt();
  BasicBlock::iterator I(Preheader->getTerminator());
  while (I != Preheader->begin()) {
    --I;
    // New instructions were inserted at the end of the preheader.
    if (isa<PHINode>(I))
      break;

    // Don't move instructions which might have side effects, since the side
    // effects need to complete before instructions inside the loop.  Also don't
    // move instructions which might read memory, since the loop may modify
    // memory. Note that it's okay if the instruction might have undefined
    // behavior: LoopSimplify guarantees that the preheader dominates the exit
    // block.
    if (I->mayHaveSideEffects() || I->mayReadFromMemory())
      continue;

    // Skip debug info intrinsics.
    if (isa<DbgInfoIntrinsic>(I))
      continue;

    // Skip eh pad instructions.
    if (I->isEHPad())
      continue;

    // Don't sink alloca: we never want to sink static alloca's out of the
    // entry block, and correctly sinking dynamic alloca's requires
    // checks for stacksave/stackrestore intrinsics.
    // FIXME: Refactor this check somehow?
    if (isa<AllocaInst>(I))
      continue;

    // Determine if there is a use in or before the loop (direct or
    // otherwise).
    bool UsedInLoop = false;
    for (Use &U : I->uses()) {
      Instruction *User = cast<Instruction>(U.getUser());
      BasicBlock *UseBB = User->getParent();
      if (PHINode *P = dyn_cast<PHINode>(User)) {
        unsigned i =
          PHINode::getIncomingValueNumForOperand(U.getOperandNo());
        UseBB = P->getIncomingBlock(i);
      }
      if (UseBB == Preheader || L->contains(UseBB)) {
        UsedInLoop = true;
        break;
      }
    }

    // If there is, the def must remain in the preheader.
    if (UsedInLoop)
      continue;

    // Otherwise, sink it to the exit block.
    Instruction *ToMove = &*I;
    bool Done = false;

    if (I != Preheader->begin()) {
      // Skip debug info intrinsics.
      do {
        --I;
      } while (I->isDebugOrPseudoInst() && I != Preheader->begin());

      if (I->isDebugOrPseudoInst() && I == Preheader->begin())
        Done = true;
    } else {
      Done = true;
    }

    MadeAnyChanges = true;
    ToMove->moveBefore(*ExitBlock, InsertPt);
    SE->forgetValue(ToMove);
    if (Done) break;
    InsertPt = ToMove->getIterator();
  }

  return MadeAnyChanges;
}

static void replaceExitCond(BranchInst *BI, Value *NewCond,
                            SmallVectorImpl<WeakTrackingVH> &DeadInsts) {
  auto *OldCond = BI->getCondition();
  LLVM_DEBUG(dbgs() << "Replacing condition of loop-exiting branch " << *BI
                    << " with " << *NewCond << "\n");
  BI->setCondition(NewCond);
  if (OldCond->use_empty())
    DeadInsts.emplace_back(OldCond);
}

static Constant *createFoldedExitCond(const Loop *L, BasicBlock *ExitingBB,
                                      bool IsTaken) {
  BranchInst *BI = cast<BranchInst>(ExitingBB->getTerminator());
  bool ExitIfTrue = !L->contains(*succ_begin(ExitingBB));
  auto *OldCond = BI->getCondition();
  return ConstantInt::get(OldCond->getType(),
                          IsTaken ? ExitIfTrue : !ExitIfTrue);
}

static void foldExit(const Loop *L, BasicBlock *ExitingBB, bool IsTaken,
                     SmallVectorImpl<WeakTrackingVH> &DeadInsts) {
  BranchInst *BI = cast<BranchInst>(ExitingBB->getTerminator());
  auto *NewCond = createFoldedExitCond(L, ExitingBB, IsTaken);
  replaceExitCond(BI, NewCond, DeadInsts);
}

static void replaceLoopPHINodesWithPreheaderValues(
    LoopInfo *LI, Loop *L, SmallVectorImpl<WeakTrackingVH> &DeadInsts,
    ScalarEvolution &SE) {
  assert(L->isLoopSimplifyForm() && "Should only do it in simplify form!");
  auto *LoopPreheader = L->getLoopPreheader();
  auto *LoopHeader = L->getHeader();
  SmallVector<Instruction *> Worklist;
  for (auto &PN : LoopHeader->phis()) {
    auto *PreheaderIncoming = PN.getIncomingValueForBlock(LoopPreheader);
    for (User *U : PN.users())
      Worklist.push_back(cast<Instruction>(U));
    SE.forgetValue(&PN);
    PN.replaceAllUsesWith(PreheaderIncoming);
    DeadInsts.emplace_back(&PN);
  }

  // Replacing with the preheader value will often allow IV users to simplify
  // (especially if the preheader value is a constant).
  SmallPtrSet<Instruction *, 16> Visited;
  while (!Worklist.empty()) {
    auto *I = cast<Instruction>(Worklist.pop_back_val());
    if (!Visited.insert(I).second)
      continue;

    // Don't simplify instructions outside the loop.
    if (!L->contains(I))
      continue;

    Value *Res = simplifyInstruction(I, I->getDataLayout());
    if (Res && LI->replacementPreservesLCSSAForm(I, Res)) {
      for (User *U : I->users())
        Worklist.push_back(cast<Instruction>(U));
      I->replaceAllUsesWith(Res);
      DeadInsts.emplace_back(I);
    }
  }
}

static Value *
createInvariantCond(const Loop *L, BasicBlock *ExitingBB,
                    const ScalarEvolution::LoopInvariantPredicate &LIP,
                    SCEVExpander &Rewriter) {
  ICmpInst::Predicate InvariantPred = LIP.Pred;
  BasicBlock *Preheader = L->getLoopPreheader();
  assert(Preheader && "Preheader doesn't exist");
  Rewriter.setInsertPoint(Preheader->getTerminator());
  auto *LHSV = Rewriter.expandCodeFor(LIP.LHS);
  auto *RHSV = Rewriter.expandCodeFor(LIP.RHS);
  bool ExitIfTrue = !L->contains(*succ_begin(ExitingBB));
  if (ExitIfTrue)
    InvariantPred = ICmpInst::getInversePredicate(InvariantPred);
  IRBuilder<> Builder(Preheader->getTerminator());
  BranchInst *BI = cast<BranchInst>(ExitingBB->getTerminator());
  return Builder.CreateICmp(InvariantPred, LHSV, RHSV,
                            BI->getCondition()->getName());
}

static std::optional<Value *>
createReplacement(ICmpInst *ICmp, const Loop *L, BasicBlock *ExitingBB,
                  const SCEV *MaxIter, bool Inverted, bool SkipLastIter,
                  ScalarEvolution *SE, SCEVExpander &Rewriter) {
  ICmpInst::Predicate Pred = ICmp->getPredicate();
  Value *LHS = ICmp->getOperand(0);
  Value *RHS = ICmp->getOperand(1);

  // 'LHS pred RHS' should now mean that we stay in loop.
  auto *BI = cast<BranchInst>(ExitingBB->getTerminator());
  if (Inverted)
    Pred = CmpInst::getInversePredicate(Pred);

  const SCEV *LHSS = SE->getSCEVAtScope(LHS, L);
  const SCEV *RHSS = SE->getSCEVAtScope(RHS, L);
  // Can we prove it to be trivially true or false?
  if (auto EV = SE->evaluatePredicateAt(Pred, LHSS, RHSS, BI))
    return createFoldedExitCond(L, ExitingBB, /*IsTaken*/ !*EV);

  auto *ARTy = LHSS->getType();
  auto *MaxIterTy = MaxIter->getType();
  // If possible, adjust types.
  if (SE->getTypeSizeInBits(ARTy) > SE->getTypeSizeInBits(MaxIterTy))
    MaxIter = SE->getZeroExtendExpr(MaxIter, ARTy);
  else if (SE->getTypeSizeInBits(ARTy) < SE->getTypeSizeInBits(MaxIterTy)) {
    const SCEV *MinusOne = SE->getMinusOne(ARTy);
    auto *MaxAllowedIter = SE->getZeroExtendExpr(MinusOne, MaxIterTy);
    if (SE->isKnownPredicateAt(ICmpInst::ICMP_ULE, MaxIter, MaxAllowedIter, BI))
      MaxIter = SE->getTruncateExpr(MaxIter, ARTy);
  }

  if (SkipLastIter) {
    // Semantically skip last iter is "subtract 1, do not bother about unsigned
    // wrap". getLoopInvariantExitCondDuringFirstIterations knows how to deal
    // with umin in a smart way, but umin(a, b) - 1 will likely not simplify.
    // So we manually construct umin(a - 1, b - 1).
    SmallVector<const SCEV *, 4> Elements;
    if (auto *UMin = dyn_cast<SCEVUMinExpr>(MaxIter)) {
      for (auto *Op : UMin->operands())
        Elements.push_back(SE->getMinusSCEV(Op, SE->getOne(Op->getType())));
      MaxIter = SE->getUMinFromMismatchedTypes(Elements);
    } else
      MaxIter = SE->getMinusSCEV(MaxIter, SE->getOne(MaxIter->getType()));
  }

  // Check if there is a loop-invariant predicate equivalent to our check.
  auto LIP = SE->getLoopInvariantExitCondDuringFirstIterations(Pred, LHSS, RHSS,
                                                               L, BI, MaxIter);
  if (!LIP)
    return std::nullopt;

  // Can we prove it to be trivially true?
  if (SE->isKnownPredicateAt(LIP->Pred, LIP->LHS, LIP->RHS, BI))
    return createFoldedExitCond(L, ExitingBB, /*IsTaken*/ false);
  else
    return createInvariantCond(L, ExitingBB, *LIP, Rewriter);
}

static bool optimizeLoopExitWithUnknownExitCount(
    const Loop *L, BranchInst *BI, BasicBlock *ExitingBB, const SCEV *MaxIter,
    bool SkipLastIter, ScalarEvolution *SE, SCEVExpander &Rewriter,
    SmallVectorImpl<WeakTrackingVH> &DeadInsts) {
  assert(
      (L->contains(BI->getSuccessor(0)) != L->contains(BI->getSuccessor(1))) &&
      "Not a loop exit!");

  // For branch that stays in loop by TRUE condition, go through AND. For branch
  // that stays in loop by FALSE condition, go through OR. Both gives the
  // similar logic: "stay in loop iff all conditions are true(false)".
  bool Inverted = L->contains(BI->getSuccessor(1));
  SmallVector<ICmpInst *, 4> LeafConditions;
  SmallVector<Value *, 4> Worklist;
  SmallPtrSet<Value *, 4> Visited;
  Value *OldCond = BI->getCondition();
  Visited.insert(OldCond);
  Worklist.push_back(OldCond);

  auto GoThrough = [&](Value *V) {
    Value *LHS = nullptr, *RHS = nullptr;
    if (Inverted) {
      if (!match(V, m_LogicalOr(m_Value(LHS), m_Value(RHS))))
        return false;
    } else {
      if (!match(V, m_LogicalAnd(m_Value(LHS), m_Value(RHS))))
        return false;
    }
    if (Visited.insert(LHS).second)
      Worklist.push_back(LHS);
    if (Visited.insert(RHS).second)
      Worklist.push_back(RHS);
    return true;
  };

  do {
    Value *Curr = Worklist.pop_back_val();
    // Go through AND/OR conditions. Collect leaf ICMPs. We only care about
    // those with one use, to avoid instruction duplication.
    if (Curr->hasOneUse())
      if (!GoThrough(Curr))
        if (auto *ICmp = dyn_cast<ICmpInst>(Curr))
          LeafConditions.push_back(ICmp);
  } while (!Worklist.empty());

  // If the current basic block has the same exit count as the whole loop, and
  // it consists of multiple icmp's, try to collect all icmp's that give exact
  // same exit count. For all other icmp's, we could use one less iteration,
  // because their value on the last iteration doesn't really matter.
  SmallPtrSet<ICmpInst *, 4> ICmpsFailingOnLastIter;
  if (!SkipLastIter && LeafConditions.size() > 1 &&
      SE->getExitCount(L, ExitingBB,
                       ScalarEvolution::ExitCountKind::SymbolicMaximum) ==
          MaxIter)
    for (auto *ICmp : LeafConditions) {
      auto EL = SE->computeExitLimitFromCond(L, ICmp, Inverted,
                                             /*ControlsExit*/ false);
      auto *ExitMax = EL.SymbolicMaxNotTaken;
      if (isa<SCEVCouldNotCompute>(ExitMax))
        continue;
      // They could be of different types (specifically this happens after
      // IV widening).
      auto *WiderType =
          SE->getWiderType(ExitMax->getType(), MaxIter->getType());
      auto *WideExitMax = SE->getNoopOrZeroExtend(ExitMax, WiderType);
      auto *WideMaxIter = SE->getNoopOrZeroExtend(MaxIter, WiderType);
      if (WideExitMax == WideMaxIter)
        ICmpsFailingOnLastIter.insert(ICmp);
    }

  bool Changed = false;
  for (auto *OldCond : LeafConditions) {
    // Skip last iteration for this icmp under one of two conditions:
    // - We do it for all conditions;
    // - There is another ICmp that would fail on last iter, so this one doesn't
    // really matter.
    bool OptimisticSkipLastIter = SkipLastIter;
    if (!OptimisticSkipLastIter) {
      if (ICmpsFailingOnLastIter.size() > 1)
        OptimisticSkipLastIter = true;
      else if (ICmpsFailingOnLastIter.size() == 1)
        OptimisticSkipLastIter = !ICmpsFailingOnLastIter.count(OldCond);
    }
    if (auto Replaced =
            createReplacement(OldCond, L, ExitingBB, MaxIter, Inverted,
                              OptimisticSkipLastIter, SE, Rewriter)) {
      Changed = true;
      auto *NewCond = *Replaced;
      if (auto *NCI = dyn_cast<Instruction>(NewCond)) {
        NCI->setName(OldCond->getName() + ".first_iter");
      }
      LLVM_DEBUG(dbgs() << "Unknown exit count: Replacing " << *OldCond
                        << " with " << *NewCond << "\n");
      assert(OldCond->hasOneUse() && "Must be!");
      OldCond->replaceAllUsesWith(NewCond);
      DeadInsts.push_back(OldCond);
      // Make sure we no longer consider this condition as failing on last
      // iteration.
      ICmpsFailingOnLastIter.erase(OldCond);
    }
  }
  return Changed;
}

bool IndVarSimplify::canonicalizeExitCondition(Loop *L) {
  // Note: This is duplicating a particular part on SimplifyIndVars reasoning.
  // We need to duplicate it because given icmp zext(small-iv), C, IVUsers
  // never reaches the icmp since the zext doesn't fold to an AddRec unless
  // it already has flags.  The alternative to this would be to extending the
  // set of "interesting" IV users to include the icmp, but doing that
  // regresses results in practice by querying SCEVs before trip counts which
  // rely on them which results in SCEV caching sub-optimal answers.  The
  // concern about caching sub-optimal results is why we only query SCEVs of
  // the loop invariant RHS here.
  SmallVector<BasicBlock*, 16> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  bool Changed = false;
  for (auto *ExitingBB : ExitingBlocks) {
    auto *BI = dyn_cast<BranchInst>(ExitingBB->getTerminator());
    if (!BI)
      continue;
    assert(BI->isConditional() && "exit branch must be conditional");

    auto *ICmp = dyn_cast<ICmpInst>(BI->getCondition());
    if (!ICmp || !ICmp->hasOneUse())
      continue;

    auto *LHS = ICmp->getOperand(0);
    auto *RHS = ICmp->getOperand(1);
    // For the range reasoning, avoid computing SCEVs in the loop to avoid
    // poisoning cache with sub-optimal results.  For the must-execute case,
    // this is a neccessary precondition for correctness.
    if (!L->isLoopInvariant(RHS)) {
      if (!L->isLoopInvariant(LHS))
        continue;
      // Same logic applies for the inverse case
      std::swap(LHS, RHS);
    }

    // Match (icmp signed-cond zext, RHS)
    Value *LHSOp = nullptr;
    if (!match(LHS, m_ZExt(m_Value(LHSOp))) || !ICmp->isSigned())
      continue;

    const DataLayout &DL = ExitingBB->getDataLayout();
    const unsigned InnerBitWidth = DL.getTypeSizeInBits(LHSOp->getType());
    const unsigned OuterBitWidth = DL.getTypeSizeInBits(RHS->getType());
    auto FullCR = ConstantRange::getFull(InnerBitWidth);
    FullCR = FullCR.zeroExtend(OuterBitWidth);
    auto RHSCR = SE->getUnsignedRange(SE->applyLoopGuards(SE->getSCEV(RHS), L));
    if (FullCR.contains(RHSCR)) {
      // We have now matched icmp signed-cond zext(X), zext(Y'), and can thus
      // replace the signed condition with the unsigned version.
      ICmp->setPredicate(ICmp->getUnsignedPredicate());
      Changed = true;
      // Note: No SCEV invalidation needed.  We've changed the predicate, but
      // have not changed exit counts, or the values produced by the compare.
      continue;
    }
  }

  // Now that we've canonicalized the condition to match the extend,
  // see if we can rotate the extend out of the loop.
  for (auto *ExitingBB : ExitingBlocks) {
    auto *BI = dyn_cast<BranchInst>(ExitingBB->getTerminator());
    if (!BI)
      continue;
    assert(BI->isConditional() && "exit branch must be conditional");

    auto *ICmp = dyn_cast<ICmpInst>(BI->getCondition());
    if (!ICmp || !ICmp->hasOneUse() || !ICmp->isUnsigned())
      continue;

    bool Swapped = false;
    auto *LHS = ICmp->getOperand(0);
    auto *RHS = ICmp->getOperand(1);
    if (L->isLoopInvariant(LHS) == L->isLoopInvariant(RHS))
      // Nothing to rotate
      continue;
    if (L->isLoopInvariant(LHS)) {
      // Same logic applies for the inverse case until we actually pick
      // which operand of the compare to update.
      Swapped = true;
      std::swap(LHS, RHS);
    }
    assert(!L->isLoopInvariant(LHS) && L->isLoopInvariant(RHS));

    // Match (icmp unsigned-cond zext, RHS)
    // TODO: Extend to handle corresponding sext/signed-cmp case
    // TODO: Extend to other invertible functions
    Value *LHSOp = nullptr;
    if (!match(LHS, m_ZExt(m_Value(LHSOp))))
      continue;

    // In general, we only rotate if we can do so without increasing the number
    // of instructions.  The exception is when we have an zext(add-rec).  The
    // reason for allowing this exception is that we know we need to get rid
    // of the zext for SCEV to be able to compute a trip count for said loops;
    // we consider the new trip count valuable enough to increase instruction
    // count by one.
    if (!LHS->hasOneUse() && !isa<SCEVAddRecExpr>(SE->getSCEV(LHSOp)))
      continue;

    // Given a icmp unsigned-cond zext(Op) where zext(trunc(RHS)) == RHS
    // replace with an icmp of the form icmp unsigned-cond Op, trunc(RHS)
    // when zext is loop varying and RHS is loop invariant.  This converts
    // loop varying work to loop-invariant work.
    auto doRotateTransform = [&]() {
      assert(ICmp->isUnsigned() && "must have proven unsigned already");
      auto *NewRHS = CastInst::Create(
          Instruction::Trunc, RHS, LHSOp->getType(), "",
          L->getLoopPreheader()->getTerminator()->getIterator());
      ICmp->setOperand(Swapped ? 1 : 0, LHSOp);
      ICmp->setOperand(Swapped ? 0 : 1, NewRHS);
      if (LHS->use_empty())
        DeadInsts.push_back(LHS);
    };


    const DataLayout &DL = ExitingBB->getDataLayout();
    const unsigned InnerBitWidth = DL.getTypeSizeInBits(LHSOp->getType());
    const unsigned OuterBitWidth = DL.getTypeSizeInBits(RHS->getType());
    auto FullCR = ConstantRange::getFull(InnerBitWidth);
    FullCR = FullCR.zeroExtend(OuterBitWidth);
    auto RHSCR = SE->getUnsignedRange(SE->applyLoopGuards(SE->getSCEV(RHS), L));
    if (FullCR.contains(RHSCR)) {
      doRotateTransform();
      Changed = true;
      // Note, we are leaving SCEV in an unfortunately imprecise case here
      // as rotation tends to reveal information about trip counts not
      // previously visible.
      continue;
    }
  }

  return Changed;
}

bool IndVarSimplify::optimizeLoopExits(Loop *L, SCEVExpander &Rewriter) {
  SmallVector<BasicBlock*, 16> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  // Remove all exits which aren't both rewriteable and execute on every
  // iteration.
  llvm::erase_if(ExitingBlocks, [&](BasicBlock *ExitingBB) {
    // If our exitting block exits multiple loops, we can only rewrite the
    // innermost one.  Otherwise, we're changing how many times the innermost
    // loop runs before it exits.
    if (LI->getLoopFor(ExitingBB) != L)
      return true;

    // Can't rewrite non-branch yet.
    BranchInst *BI = dyn_cast<BranchInst>(ExitingBB->getTerminator());
    if (!BI)
      return true;

    // Likewise, the loop latch must be dominated by the exiting BB.
    if (!DT->dominates(ExitingBB, L->getLoopLatch()))
      return true;

    if (auto *CI = dyn_cast<ConstantInt>(BI->getCondition())) {
      // If already constant, nothing to do. However, if this is an
      // unconditional exit, we can still replace header phis with their
      // preheader value.
      if (!L->contains(BI->getSuccessor(CI->isNullValue())))
        replaceLoopPHINodesWithPreheaderValues(LI, L, DeadInsts, *SE);
      return true;
    }

    return false;
  });

  if (ExitingBlocks.empty())
    return false;

  // Get a symbolic upper bound on the loop backedge taken count.
  const SCEV *MaxBECount = SE->getSymbolicMaxBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(MaxBECount))
    return false;

  // Visit our exit blocks in order of dominance. We know from the fact that
  // all exits must dominate the latch, so there is a total dominance order
  // between them.
  llvm::sort(ExitingBlocks, [&](BasicBlock *A, BasicBlock *B) {
               // std::sort sorts in ascending order, so we want the inverse of
               // the normal dominance relation.
               if (A == B) return false;
               if (DT->properlyDominates(A, B))
                 return true;
               else {
                 assert(DT->properlyDominates(B, A) &&
                        "expected total dominance order!");
                 return false;
               }
  });
#ifdef ASSERT
  for (unsigned i = 1; i < ExitingBlocks.size(); i++) {
    assert(DT->dominates(ExitingBlocks[i-1], ExitingBlocks[i]));
  }
#endif

  bool Changed = false;
  bool SkipLastIter = false;
  const SCEV *CurrMaxExit = SE->getCouldNotCompute();
  auto UpdateSkipLastIter = [&](const SCEV *MaxExitCount) {
    if (SkipLastIter || isa<SCEVCouldNotCompute>(MaxExitCount))
      return;
    if (isa<SCEVCouldNotCompute>(CurrMaxExit))
      CurrMaxExit = MaxExitCount;
    else
      CurrMaxExit = SE->getUMinFromMismatchedTypes(CurrMaxExit, MaxExitCount);
    // If the loop has more than 1 iteration, all further checks will be
    // executed 1 iteration less.
    if (CurrMaxExit == MaxBECount)
      SkipLastIter = true;
  };
  SmallSet<const SCEV *, 8> DominatingExactExitCounts;
  for (BasicBlock *ExitingBB : ExitingBlocks) {
    const SCEV *ExactExitCount = SE->getExitCount(L, ExitingBB);
    const SCEV *MaxExitCount = SE->getExitCount(
        L, ExitingBB, ScalarEvolution::ExitCountKind::SymbolicMaximum);
    if (isa<SCEVCouldNotCompute>(ExactExitCount)) {
      // Okay, we do not know the exit count here. Can we at least prove that it
      // will remain the same within iteration space?
      auto *BI = cast<BranchInst>(ExitingBB->getTerminator());
      auto OptimizeCond = [&](bool SkipLastIter) {
        return optimizeLoopExitWithUnknownExitCount(L, BI, ExitingBB,
                                                    MaxBECount, SkipLastIter,
                                                    SE, Rewriter, DeadInsts);
      };

      // TODO: We might have proved that we can skip the last iteration for
      // this check. In this case, we only want to check the condition on the
      // pre-last iteration (MaxBECount - 1). However, there is a nasty
      // corner case:
      //
      //   for (i = len; i != 0; i--) { ... check (i ult X) ... }
      //
      // If we could not prove that len != 0, then we also could not prove that
      // (len - 1) is not a UINT_MAX. If we simply query (len - 1), then
      // OptimizeCond will likely not prove anything for it, even if it could
      // prove the same fact for len.
      //
      // As a temporary solution, we query both last and pre-last iterations in
      // hope that we will be able to prove triviality for at least one of
      // them. We can stop querying MaxBECount for this case once SCEV
      // understands that (MaxBECount - 1) will not overflow here.
      if (OptimizeCond(false))
        Changed = true;
      else if (SkipLastIter && OptimizeCond(true))
        Changed = true;
      UpdateSkipLastIter(MaxExitCount);
      continue;
    }

    UpdateSkipLastIter(ExactExitCount);

    // If we know we'd exit on the first iteration, rewrite the exit to
    // reflect this.  This does not imply the loop must exit through this
    // exit; there may be an earlier one taken on the first iteration.
    // We know that the backedge can't be taken, so we replace all
    // the header PHIs with values coming from the preheader.
    if (ExactExitCount->isZero()) {
      foldExit(L, ExitingBB, true, DeadInsts);
      replaceLoopPHINodesWithPreheaderValues(LI, L, DeadInsts, *SE);
      Changed = true;
      continue;
    }

    assert(ExactExitCount->getType()->isIntegerTy() &&
           MaxBECount->getType()->isIntegerTy() &&
           "Exit counts must be integers");

    Type *WiderType =
        SE->getWiderType(MaxBECount->getType(), ExactExitCount->getType());
    ExactExitCount = SE->getNoopOrZeroExtend(ExactExitCount, WiderType);
    MaxBECount = SE->getNoopOrZeroExtend(MaxBECount, WiderType);
    assert(MaxBECount->getType() == ExactExitCount->getType());

    // Can we prove that some other exit must be taken strictly before this
    // one?
    if (SE->isLoopEntryGuardedByCond(L, CmpInst::ICMP_ULT, MaxBECount,
                                     ExactExitCount)) {
      foldExit(L, ExitingBB, false, DeadInsts);
      Changed = true;
      continue;
    }

    // As we run, keep track of which exit counts we've encountered.  If we
    // find a duplicate, we've found an exit which would have exited on the
    // exiting iteration, but (from the visit order) strictly follows another
    // which does the same and is thus dead.
    if (!DominatingExactExitCounts.insert(ExactExitCount).second) {
      foldExit(L, ExitingBB, false, DeadInsts);
      Changed = true;
      continue;
    }

    // TODO: There might be another oppurtunity to leverage SCEV's reasoning
    // here.  If we kept track of the min of dominanting exits so far, we could
    // discharge exits with EC >= MDEC. This is less powerful than the existing
    // transform (since later exits aren't considered), but potentially more
    // powerful for any case where SCEV can prove a >=u b, but neither a == b
    // or a >u b.  Such a case is not currently known.
  }
  return Changed;
}

bool IndVarSimplify::predicateLoopExits(Loop *L, SCEVExpander &Rewriter) {
  SmallVector<BasicBlock*, 16> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  // Finally, see if we can rewrite our exit conditions into a loop invariant
  // form. If we have a read-only loop, and we can tell that we must exit down
  // a path which does not need any of the values computed within the loop, we
  // can rewrite the loop to exit on the first iteration.  Note that this
  // doesn't either a) tell us the loop exits on the first iteration (unless
  // *all* exits are predicateable) or b) tell us *which* exit might be taken.
  // This transformation looks a lot like a restricted form of dead loop
  // elimination, but restricted to read-only loops and without neccesssarily
  // needing to kill the loop entirely.
  if (!LoopPredication)
    return false;

  // Note: ExactBTC is the exact backedge taken count *iff* the loop exits
  // through *explicit* control flow.  We have to eliminate the possibility of
  // implicit exits (see below) before we know it's truly exact.
  const SCEV *ExactBTC = SE->getBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(ExactBTC) || !Rewriter.isSafeToExpand(ExactBTC))
    return false;

  assert(SE->isLoopInvariant(ExactBTC, L) && "BTC must be loop invariant");
  assert(ExactBTC->getType()->isIntegerTy() && "BTC must be integer");

  auto BadExit = [&](BasicBlock *ExitingBB) {
    // If our exiting block exits multiple loops, we can only rewrite the
    // innermost one.  Otherwise, we're changing how many times the innermost
    // loop runs before it exits.
    if (LI->getLoopFor(ExitingBB) != L)
      return true;

    // Can't rewrite non-branch yet.
    BranchInst *BI = dyn_cast<BranchInst>(ExitingBB->getTerminator());
    if (!BI)
      return true;

    // If already constant, nothing to do.
    if (isa<Constant>(BI->getCondition()))
      return true;

    // If the exit block has phis, we need to be able to compute the values
    // within the loop which contains them.  This assumes trivially lcssa phis
    // have already been removed; TODO: generalize
    BasicBlock *ExitBlock =
    BI->getSuccessor(L->contains(BI->getSuccessor(0)) ? 1 : 0);
    if (!ExitBlock->phis().empty())
      return true;

    const SCEV *ExitCount = SE->getExitCount(L, ExitingBB);
    if (isa<SCEVCouldNotCompute>(ExitCount) ||
        !Rewriter.isSafeToExpand(ExitCount))
      return true;

    assert(SE->isLoopInvariant(ExitCount, L) &&
           "Exit count must be loop invariant");
    assert(ExitCount->getType()->isIntegerTy() && "Exit count must be integer");
    return false;
  };

  // If we have any exits which can't be predicated themselves, than we can't
  // predicate any exit which isn't guaranteed to execute before it.  Consider
  // two exits (a) and (b) which would both exit on the same iteration.  If we
  // can predicate (b), but not (a), and (a) preceeds (b) along some path, then
  // we could convert a loop from exiting through (a) to one exiting through
  // (b).  Note that this problem exists only for exits with the same exit
  // count, and we could be more aggressive when exit counts are known inequal.
  llvm::sort(ExitingBlocks,
            [&](BasicBlock *A, BasicBlock *B) {
              // std::sort sorts in ascending order, so we want the inverse of
              // the normal dominance relation, plus a tie breaker for blocks
              // unordered by dominance.
              if (DT->properlyDominates(A, B)) return true;
              if (DT->properlyDominates(B, A)) return false;
              return A->getName() < B->getName();
            });
  // Check to see if our exit blocks are a total order (i.e. a linear chain of
  // exits before the backedge).  If they aren't, reasoning about reachability
  // is complicated and we choose not to for now.
  for (unsigned i = 1; i < ExitingBlocks.size(); i++)
    if (!DT->dominates(ExitingBlocks[i-1], ExitingBlocks[i]))
      return false;

  // Given our sorted total order, we know that exit[j] must be evaluated
  // after all exit[i] such j > i.
  for (unsigned i = 0, e = ExitingBlocks.size(); i < e; i++)
    if (BadExit(ExitingBlocks[i])) {
      ExitingBlocks.resize(i);
      break;
    }

  if (ExitingBlocks.empty())
    return false;

  // We rely on not being able to reach an exiting block on a later iteration
  // then it's statically compute exit count.  The implementaton of
  // getExitCount currently has this invariant, but assert it here so that
  // breakage is obvious if this ever changes..
  assert(llvm::all_of(ExitingBlocks, [&](BasicBlock *ExitingBB) {
        return DT->dominates(ExitingBB, L->getLoopLatch());
      }));

  // At this point, ExitingBlocks consists of only those blocks which are
  // predicatable.  Given that, we know we have at least one exit we can
  // predicate if the loop is doesn't have side effects and doesn't have any
  // implicit exits (because then our exact BTC isn't actually exact).
  // @Reviewers - As structured, this is O(I^2) for loop nests.  Any
  // suggestions on how to improve this?  I can obviously bail out for outer
  // loops, but that seems less than ideal.  MemorySSA can find memory writes,
  // is that enough for *all* side effects?
  for (BasicBlock *BB : L->blocks())
    for (auto &I : *BB)
      // TODO:isGuaranteedToTransfer
      if (I.mayHaveSideEffects())
        return false;

  bool Changed = false;
  // Finally, do the actual predication for all predicatable blocks.  A couple
  // of notes here:
  // 1) We don't bother to constant fold dominated exits with identical exit
  //    counts; that's simply a form of CSE/equality propagation and we leave
  //    it for dedicated passes.
  // 2) We insert the comparison at the branch.  Hoisting introduces additional
  //    legality constraints and we leave that to dedicated logic.  We want to
  //    predicate even if we can't insert a loop invariant expression as
  //    peeling or unrolling will likely reduce the cost of the otherwise loop
  //    varying check.
  Rewriter.setInsertPoint(L->getLoopPreheader()->getTerminator());
  IRBuilder<> B(L->getLoopPreheader()->getTerminator());
  Value *ExactBTCV = nullptr; // Lazily generated if needed.
  for (BasicBlock *ExitingBB : ExitingBlocks) {
    const SCEV *ExitCount = SE->getExitCount(L, ExitingBB);

    auto *BI = cast<BranchInst>(ExitingBB->getTerminator());
    Value *NewCond;
    if (ExitCount == ExactBTC) {
      NewCond = L->contains(BI->getSuccessor(0)) ?
        B.getFalse() : B.getTrue();
    } else {
      Value *ECV = Rewriter.expandCodeFor(ExitCount);
      if (!ExactBTCV)
        ExactBTCV = Rewriter.expandCodeFor(ExactBTC);
      Value *RHS = ExactBTCV;
      if (ECV->getType() != RHS->getType()) {
        Type *WiderTy = SE->getWiderType(ECV->getType(), RHS->getType());
        ECV = B.CreateZExt(ECV, WiderTy);
        RHS = B.CreateZExt(RHS, WiderTy);
      }
      auto Pred = L->contains(BI->getSuccessor(0)) ?
        ICmpInst::ICMP_NE : ICmpInst::ICMP_EQ;
      NewCond = B.CreateICmp(Pred, ECV, RHS);
    }
    Value *OldCond = BI->getCondition();
    BI->setCondition(NewCond);
    if (OldCond->use_empty())
      DeadInsts.emplace_back(OldCond);
    Changed = true;
    RunUnswitching = true;
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
//  IndVarSimplify driver. Manage several subpasses of IV simplification.
//===----------------------------------------------------------------------===//

bool IndVarSimplify::run(Loop *L) {
  // We need (and expect!) the incoming loop to be in LCSSA.
  assert(L->isRecursivelyLCSSAForm(*DT, *LI) &&
         "LCSSA required to run indvars!");

  // If LoopSimplify form is not available, stay out of trouble. Some notes:
  //  - LSR currently only supports LoopSimplify-form loops. Indvars'
  //    canonicalization can be a pessimization without LSR to "clean up"
  //    afterwards.
  //  - We depend on having a preheader; in particular,
  //    Loop::getCanonicalInductionVariable only supports loops with preheaders,
  //    and we're in trouble if we can't find the induction variable even when
  //    we've manually inserted one.
  //  - LFTR relies on having a single backedge.
  if (!L->isLoopSimplifyForm())
    return false;

  bool Changed = false;
  // If there are any floating-point recurrences, attempt to
  // transform them to use integer recurrences.
  Changed |= rewriteNonIntegerIVs(L);

  // Create a rewriter object which we'll use to transform the code with.
  SCEVExpander Rewriter(*SE, DL, "indvars");
#ifndef NDEBUG
  Rewriter.setDebugType(DEBUG_TYPE);
#endif

  // Eliminate redundant IV users.
  //
  // Simplification works best when run before other consumers of SCEV. We
  // attempt to avoid evaluating SCEVs for sign/zero extend operations until
  // other expressions involving loop IVs have been evaluated. This helps SCEV
  // set no-wrap flags before normalizing sign/zero extension.
  Rewriter.disableCanonicalMode();
  Changed |= simplifyAndExtend(L, Rewriter, LI);

  // Check to see if we can compute the final value of any expressions
  // that are recurrent in the loop, and substitute the exit values from the
  // loop into any instructions outside of the loop that use the final values
  // of the current expressions.
  if (ReplaceExitValue != NeverRepl) {
    if (int Rewrites = rewriteLoopExitValues(L, LI, TLI, SE, TTI, Rewriter, DT,
                                             ReplaceExitValue, DeadInsts)) {
      NumReplaced += Rewrites;
      Changed = true;
    }
  }

  // Eliminate redundant IV cycles.
  NumElimIV += Rewriter.replaceCongruentIVs(L, DT, DeadInsts, TTI);

  // Try to convert exit conditions to unsigned and rotate computation
  // out of the loop.  Note: Handles invalidation internally if needed.
  Changed |= canonicalizeExitCondition(L);

  // Try to eliminate loop exits based on analyzeable exit counts
  if (optimizeLoopExits(L, Rewriter))  {
    Changed = true;
    // Given we've changed exit counts, notify SCEV
    // Some nested loops may share same folded exit basic block,
    // thus we need to notify top most loop.
    SE->forgetTopmostLoop(L);
  }

  // Try to form loop invariant tests for loop exits by changing how many
  // iterations of the loop run when that is unobservable.
  if (predicateLoopExits(L, Rewriter)) {
    Changed = true;
    // Given we've changed exit counts, notify SCEV
    SE->forgetLoop(L);
  }

  // If we have a trip count expression, rewrite the loop's exit condition
  // using it.
  if (!DisableLFTR) {
    BasicBlock *PreHeader = L->getLoopPreheader();

    SmallVector<BasicBlock*, 16> ExitingBlocks;
    L->getExitingBlocks(ExitingBlocks);
    for (BasicBlock *ExitingBB : ExitingBlocks) {
      // Can't rewrite non-branch yet.
      if (!isa<BranchInst>(ExitingBB->getTerminator()))
        continue;

      // If our exitting block exits multiple loops, we can only rewrite the
      // innermost one.  Otherwise, we're changing how many times the innermost
      // loop runs before it exits.
      if (LI->getLoopFor(ExitingBB) != L)
        continue;

      if (!needsLFTR(L, ExitingBB))
        continue;

      const SCEV *ExitCount = SE->getExitCount(L, ExitingBB);
      if (isa<SCEVCouldNotCompute>(ExitCount))
        continue;

      // This was handled above, but as we form SCEVs, we can sometimes refine
      // existing ones; this allows exit counts to be folded to zero which
      // weren't when optimizeLoopExits saw them.  Arguably, we should iterate
      // until stable to handle cases like this better.
      if (ExitCount->isZero())
        continue;

      PHINode *IndVar = FindLoopCounter(L, ExitingBB, ExitCount, SE, DT);
      if (!IndVar)
        continue;

      // Avoid high cost expansions.  Note: This heuristic is questionable in
      // that our definition of "high cost" is not exactly principled.
      if (Rewriter.isHighCostExpansion(ExitCount, L, SCEVCheapExpansionBudget,
                                       TTI, PreHeader->getTerminator()))
        continue;

      if (!Rewriter.isSafeToExpand(ExitCount))
        continue;

      Changed |= linearFunctionTestReplace(L, ExitingBB,
                                           ExitCount, IndVar,
                                           Rewriter);
    }
  }
  // Clear the rewriter cache, because values that are in the rewriter's cache
  // can be deleted in the loop below, causing the AssertingVH in the cache to
  // trigger.
  Rewriter.clear();

  // Now that we're done iterating through lists, clean up any instructions
  // which are now dead.
  while (!DeadInsts.empty()) {
    Value *V = DeadInsts.pop_back_val();

    if (PHINode *PHI = dyn_cast_or_null<PHINode>(V))
      Changed |= RecursivelyDeleteDeadPHINode(PHI, TLI, MSSAU.get());
    else if (Instruction *Inst = dyn_cast_or_null<Instruction>(V))
      Changed |=
          RecursivelyDeleteTriviallyDeadInstructions(Inst, TLI, MSSAU.get());
  }

  // The Rewriter may not be used from this point on.

  // Loop-invariant instructions in the preheader that aren't used in the
  // loop may be sunk below the loop to reduce register pressure.
  Changed |= sinkUnusedInvariants(L);

  // rewriteFirstIterationLoopExitValues does not rely on the computation of
  // trip count and therefore can further simplify exit values in addition to
  // rewriteLoopExitValues.
  Changed |= rewriteFirstIterationLoopExitValues(L);

  // Clean up dead instructions.
  Changed |= DeleteDeadPHIs(L->getHeader(), TLI, MSSAU.get());

  // Check a post-condition.
  assert(L->isRecursivelyLCSSAForm(*DT, *LI) &&
         "Indvars did not preserve LCSSA!");
  if (VerifyMemorySSA && MSSAU)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  return Changed;
}

PreservedAnalyses IndVarSimplifyPass::run(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &) {
  Function *F = L.getHeader()->getParent();
  const DataLayout &DL = F->getDataLayout();

  IndVarSimplify IVS(&AR.LI, &AR.SE, &AR.DT, DL, &AR.TLI, &AR.TTI, AR.MSSA,
                     WidenIndVars && AllowIVWidening);
  if (!IVS.run(&L))
    return PreservedAnalyses::all();

  auto PA = getLoopPassPreservedAnalyses();
  PA.preserveSet<CFGAnalyses>();
  if (IVS.runUnswitching()) {
    AM.getResult<ShouldRunExtraSimpleLoopUnswitch>(L, AR);
    PA.preserve<ShouldRunExtraSimpleLoopUnswitch>();
  }

  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}
