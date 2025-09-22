//===- ScalarEvolutionExpander.cpp - Scalar Evolution Analysis ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the scalar evolution expander,
// which is used to generate the code corresponding to a given scalar evolution
// expression.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
#define SCEV_DEBUG_WITH_TYPE(TYPE, X) DEBUG_WITH_TYPE(TYPE, X)
#else
#define SCEV_DEBUG_WITH_TYPE(TYPE, X)
#endif

using namespace llvm;

cl::opt<unsigned> llvm::SCEVCheapExpansionBudget(
    "scev-cheap-expansion-budget", cl::Hidden, cl::init(4),
    cl::desc("When performing SCEV expansion only if it is cheap to do, this "
             "controls the budget that is considered cheap (default = 4)"));

using namespace PatternMatch;

PoisonFlags::PoisonFlags(const Instruction *I) {
  NUW = false;
  NSW = false;
  Exact = false;
  Disjoint = false;
  NNeg = false;
  if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(I)) {
    NUW = OBO->hasNoUnsignedWrap();
    NSW = OBO->hasNoSignedWrap();
  }
  if (auto *PEO = dyn_cast<PossiblyExactOperator>(I))
    Exact = PEO->isExact();
  if (auto *PDI = dyn_cast<PossiblyDisjointInst>(I))
    Disjoint = PDI->isDisjoint();
  if (auto *PNI = dyn_cast<PossiblyNonNegInst>(I))
    NNeg = PNI->hasNonNeg();
  if (auto *TI = dyn_cast<TruncInst>(I)) {
    NUW = TI->hasNoUnsignedWrap();
    NSW = TI->hasNoSignedWrap();
  }
}

void PoisonFlags::apply(Instruction *I) {
  if (isa<OverflowingBinaryOperator>(I)) {
    I->setHasNoUnsignedWrap(NUW);
    I->setHasNoSignedWrap(NSW);
  }
  if (isa<PossiblyExactOperator>(I))
    I->setIsExact(Exact);
  if (auto *PDI = dyn_cast<PossiblyDisjointInst>(I))
    PDI->setIsDisjoint(Disjoint);
  if (auto *PNI = dyn_cast<PossiblyNonNegInst>(I))
    PNI->setNonNeg(NNeg);
  if (isa<TruncInst>(I)) {
    I->setHasNoUnsignedWrap(NUW);
    I->setHasNoSignedWrap(NSW);
  }
}

/// ReuseOrCreateCast - Arrange for there to be a cast of V to Ty at IP,
/// reusing an existing cast if a suitable one (= dominating IP) exists, or
/// creating a new one.
Value *SCEVExpander::ReuseOrCreateCast(Value *V, Type *Ty,
                                       Instruction::CastOps Op,
                                       BasicBlock::iterator IP) {
  // This function must be called with the builder having a valid insertion
  // point. It doesn't need to be the actual IP where the uses of the returned
  // cast will be added, but it must dominate such IP.
  // We use this precondition to produce a cast that will dominate all its
  // uses. In particular, this is crucial for the case where the builder's
  // insertion point *is* the point where we were asked to put the cast.
  // Since we don't know the builder's insertion point is actually
  // where the uses will be added (only that it dominates it), we are
  // not allowed to move it.
  BasicBlock::iterator BIP = Builder.GetInsertPoint();

  Value *Ret = nullptr;

  // Check to see if there is already a cast!
  for (User *U : V->users()) {
    if (U->getType() != Ty)
      continue;
    CastInst *CI = dyn_cast<CastInst>(U);
    if (!CI || CI->getOpcode() != Op)
      continue;

    // Found a suitable cast that is at IP or comes before IP. Use it. Note that
    // the cast must also properly dominate the Builder's insertion point.
    if (IP->getParent() == CI->getParent() && &*BIP != CI &&
        (&*IP == CI || CI->comesBefore(&*IP))) {
      Ret = CI;
      break;
    }
  }

  // Create a new cast.
  if (!Ret) {
    SCEVInsertPointGuard Guard(Builder, this);
    Builder.SetInsertPoint(&*IP);
    Ret = Builder.CreateCast(Op, V, Ty, V->getName());
  }

  // We assert at the end of the function since IP might point to an
  // instruction with different dominance properties than a cast
  // (an invoke for example) and not dominate BIP (but the cast does).
  assert(!isa<Instruction>(Ret) ||
         SE.DT.dominates(cast<Instruction>(Ret), &*BIP));

  return Ret;
}

BasicBlock::iterator
SCEVExpander::findInsertPointAfter(Instruction *I,
                                   Instruction *MustDominate) const {
  BasicBlock::iterator IP = ++I->getIterator();
  if (auto *II = dyn_cast<InvokeInst>(I))
    IP = II->getNormalDest()->begin();

  while (isa<PHINode>(IP))
    ++IP;

  if (isa<FuncletPadInst>(IP) || isa<LandingPadInst>(IP)) {
    ++IP;
  } else if (isa<CatchSwitchInst>(IP)) {
    IP = MustDominate->getParent()->getFirstInsertionPt();
  } else {
    assert(!IP->isEHPad() && "unexpected eh pad!");
  }

  // Adjust insert point to be after instructions inserted by the expander, so
  // we can re-use already inserted instructions. Avoid skipping past the
  // original \p MustDominate, in case it is an inserted instruction.
  while (isInsertedInstruction(&*IP) && &*IP != MustDominate)
    ++IP;

  return IP;
}

BasicBlock::iterator
SCEVExpander::GetOptimalInsertionPointForCastOf(Value *V) const {
  // Cast the argument at the beginning of the entry block, after
  // any bitcasts of other arguments.
  if (Argument *A = dyn_cast<Argument>(V)) {
    BasicBlock::iterator IP = A->getParent()->getEntryBlock().begin();
    while ((isa<BitCastInst>(IP) &&
            isa<Argument>(cast<BitCastInst>(IP)->getOperand(0)) &&
            cast<BitCastInst>(IP)->getOperand(0) != A) ||
           isa<DbgInfoIntrinsic>(IP))
      ++IP;
    return IP;
  }

  // Cast the instruction immediately after the instruction.
  if (Instruction *I = dyn_cast<Instruction>(V))
    return findInsertPointAfter(I, &*Builder.GetInsertPoint());

  // Otherwise, this must be some kind of a constant,
  // so let's plop this cast into the function's entry block.
  assert(isa<Constant>(V) &&
         "Expected the cast argument to be a global/constant");
  return Builder.GetInsertBlock()
      ->getParent()
      ->getEntryBlock()
      .getFirstInsertionPt();
}

/// InsertNoopCastOfTo - Insert a cast of V to the specified type,
/// which must be possible with a noop cast, doing what we can to share
/// the casts.
Value *SCEVExpander::InsertNoopCastOfTo(Value *V, Type *Ty) {
  Instruction::CastOps Op = CastInst::getCastOpcode(V, false, Ty, false);
  assert((Op == Instruction::BitCast ||
          Op == Instruction::PtrToInt ||
          Op == Instruction::IntToPtr) &&
         "InsertNoopCastOfTo cannot perform non-noop casts!");
  assert(SE.getTypeSizeInBits(V->getType()) == SE.getTypeSizeInBits(Ty) &&
         "InsertNoopCastOfTo cannot change sizes!");

  // inttoptr only works for integral pointers. For non-integral pointers, we
  // can create a GEP on null with the integral value as index. Note that
  // it is safe to use GEP of null instead of inttoptr here, because only
  // expressions already based on a GEP of null should be converted to pointers
  // during expansion.
  if (Op == Instruction::IntToPtr) {
    auto *PtrTy = cast<PointerType>(Ty);
    if (DL.isNonIntegralPointerType(PtrTy))
      return Builder.CreatePtrAdd(Constant::getNullValue(PtrTy), V, "scevgep");
  }
  // Short-circuit unnecessary bitcasts.
  if (Op == Instruction::BitCast) {
    if (V->getType() == Ty)
      return V;
    if (CastInst *CI = dyn_cast<CastInst>(V)) {
      if (CI->getOperand(0)->getType() == Ty)
        return CI->getOperand(0);
    }
  }
  // Short-circuit unnecessary inttoptr<->ptrtoint casts.
  if ((Op == Instruction::PtrToInt || Op == Instruction::IntToPtr) &&
      SE.getTypeSizeInBits(Ty) == SE.getTypeSizeInBits(V->getType())) {
    if (CastInst *CI = dyn_cast<CastInst>(V))
      if ((CI->getOpcode() == Instruction::PtrToInt ||
           CI->getOpcode() == Instruction::IntToPtr) &&
          SE.getTypeSizeInBits(CI->getType()) ==
          SE.getTypeSizeInBits(CI->getOperand(0)->getType()))
        return CI->getOperand(0);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
      if ((CE->getOpcode() == Instruction::PtrToInt ||
           CE->getOpcode() == Instruction::IntToPtr) &&
          SE.getTypeSizeInBits(CE->getType()) ==
          SE.getTypeSizeInBits(CE->getOperand(0)->getType()))
        return CE->getOperand(0);
  }

  // Fold a cast of a constant.
  if (Constant *C = dyn_cast<Constant>(V))
    return ConstantExpr::getCast(Op, C, Ty);

  // Try to reuse existing cast, or insert one.
  return ReuseOrCreateCast(V, Ty, Op, GetOptimalInsertionPointForCastOf(V));
}

/// InsertBinop - Insert the specified binary operator, doing a small amount
/// of work to avoid inserting an obviously redundant operation, and hoisting
/// to an outer loop when the opportunity is there and it is safe.
Value *SCEVExpander::InsertBinop(Instruction::BinaryOps Opcode,
                                 Value *LHS, Value *RHS,
                                 SCEV::NoWrapFlags Flags, bool IsSafeToHoist) {
  // Fold a binop with constant operands.
  if (Constant *CLHS = dyn_cast<Constant>(LHS))
    if (Constant *CRHS = dyn_cast<Constant>(RHS))
      if (Constant *Res = ConstantFoldBinaryOpOperands(Opcode, CLHS, CRHS, DL))
        return Res;

  // Do a quick scan to see if we have this binop nearby.  If so, reuse it.
  unsigned ScanLimit = 6;
  BasicBlock::iterator BlockBegin = Builder.GetInsertBlock()->begin();
  // Scanning starts from the last instruction before the insertion point.
  BasicBlock::iterator IP = Builder.GetInsertPoint();
  if (IP != BlockBegin) {
    --IP;
    for (; ScanLimit; --IP, --ScanLimit) {
      // Don't count dbg.value against the ScanLimit, to avoid perturbing the
      // generated code.
      if (isa<DbgInfoIntrinsic>(IP))
        ScanLimit++;

      auto canGenerateIncompatiblePoison = [&Flags](Instruction *I) {
        // Ensure that no-wrap flags match.
        if (isa<OverflowingBinaryOperator>(I)) {
          if (I->hasNoSignedWrap() != (Flags & SCEV::FlagNSW))
            return true;
          if (I->hasNoUnsignedWrap() != (Flags & SCEV::FlagNUW))
            return true;
        }
        // Conservatively, do not use any instruction which has any of exact
        // flags installed.
        if (isa<PossiblyExactOperator>(I) && I->isExact())
          return true;
        return false;
      };
      if (IP->getOpcode() == (unsigned)Opcode && IP->getOperand(0) == LHS &&
          IP->getOperand(1) == RHS && !canGenerateIncompatiblePoison(&*IP))
        return &*IP;
      if (IP == BlockBegin) break;
    }
  }

  // Save the original insertion point so we can restore it when we're done.
  DebugLoc Loc = Builder.GetInsertPoint()->getDebugLoc();
  SCEVInsertPointGuard Guard(Builder, this);

  if (IsSafeToHoist) {
    // Move the insertion point out of as many loops as we can.
    while (const Loop *L = SE.LI.getLoopFor(Builder.GetInsertBlock())) {
      if (!L->isLoopInvariant(LHS) || !L->isLoopInvariant(RHS)) break;
      BasicBlock *Preheader = L->getLoopPreheader();
      if (!Preheader) break;

      // Ok, move up a level.
      Builder.SetInsertPoint(Preheader->getTerminator());
    }
  }

  // If we haven't found this binop, insert it.
  // TODO: Use the Builder, which will make CreateBinOp below fold with
  // InstSimplifyFolder.
  Instruction *BO = Builder.Insert(BinaryOperator::Create(Opcode, LHS, RHS));
  BO->setDebugLoc(Loc);
  if (Flags & SCEV::FlagNUW)
    BO->setHasNoUnsignedWrap();
  if (Flags & SCEV::FlagNSW)
    BO->setHasNoSignedWrap();

  return BO;
}

/// expandAddToGEP - Expand an addition expression with a pointer type into
/// a GEP instead of using ptrtoint+arithmetic+inttoptr. This helps
/// BasicAliasAnalysis and other passes analyze the result. See the rules
/// for getelementptr vs. inttoptr in
/// http://llvm.org/docs/LangRef.html#pointeraliasing
/// for details.
///
/// Design note: The correctness of using getelementptr here depends on
/// ScalarEvolution not recognizing inttoptr and ptrtoint operators, as
/// they may introduce pointer arithmetic which may not be safely converted
/// into getelementptr.
///
/// Design note: It might seem desirable for this function to be more
/// loop-aware. If some of the indices are loop-invariant while others
/// aren't, it might seem desirable to emit multiple GEPs, keeping the
/// loop-invariant portions of the overall computation outside the loop.
/// However, there are a few reasons this is not done here. Hoisting simple
/// arithmetic is a low-level optimization that often isn't very
/// important until late in the optimization process. In fact, passes
/// like InstructionCombining will combine GEPs, even if it means
/// pushing loop-invariant computation down into loops, so even if the
/// GEPs were split here, the work would quickly be undone. The
/// LoopStrengthReduction pass, which is usually run quite late (and
/// after the last InstructionCombining pass), takes care of hoisting
/// loop-invariant portions of expressions, after considering what
/// can be folded using target addressing modes.
///
Value *SCEVExpander::expandAddToGEP(const SCEV *Offset, Value *V) {
  assert(!isa<Instruction>(V) ||
         SE.DT.dominates(cast<Instruction>(V), &*Builder.GetInsertPoint()));

  Value *Idx = expand(Offset);

  // Fold a GEP with constant operands.
  if (Constant *CLHS = dyn_cast<Constant>(V))
    if (Constant *CRHS = dyn_cast<Constant>(Idx))
      return Builder.CreatePtrAdd(CLHS, CRHS);

  // Do a quick scan to see if we have this GEP nearby.  If so, reuse it.
  unsigned ScanLimit = 6;
  BasicBlock::iterator BlockBegin = Builder.GetInsertBlock()->begin();
  // Scanning starts from the last instruction before the insertion point.
  BasicBlock::iterator IP = Builder.GetInsertPoint();
  if (IP != BlockBegin) {
    --IP;
    for (; ScanLimit; --IP, --ScanLimit) {
      // Don't count dbg.value against the ScanLimit, to avoid perturbing the
      // generated code.
      if (isa<DbgInfoIntrinsic>(IP))
        ScanLimit++;
      if (IP->getOpcode() == Instruction::GetElementPtr &&
          IP->getOperand(0) == V && IP->getOperand(1) == Idx &&
          cast<GEPOperator>(&*IP)->getSourceElementType() ==
              Builder.getInt8Ty())
        return &*IP;
      if (IP == BlockBegin) break;
    }
  }

  // Save the original insertion point so we can restore it when we're done.
  SCEVInsertPointGuard Guard(Builder, this);

  // Move the insertion point out of as many loops as we can.
  while (const Loop *L = SE.LI.getLoopFor(Builder.GetInsertBlock())) {
    if (!L->isLoopInvariant(V) || !L->isLoopInvariant(Idx)) break;
    BasicBlock *Preheader = L->getLoopPreheader();
    if (!Preheader) break;

    // Ok, move up a level.
    Builder.SetInsertPoint(Preheader->getTerminator());
  }

  // Emit a GEP.
  return Builder.CreatePtrAdd(V, Idx, "scevgep");
}

/// PickMostRelevantLoop - Given two loops pick the one that's most relevant for
/// SCEV expansion. If they are nested, this is the most nested. If they are
/// neighboring, pick the later.
static const Loop *PickMostRelevantLoop(const Loop *A, const Loop *B,
                                        DominatorTree &DT) {
  if (!A) return B;
  if (!B) return A;
  if (A->contains(B)) return B;
  if (B->contains(A)) return A;
  if (DT.dominates(A->getHeader(), B->getHeader())) return B;
  if (DT.dominates(B->getHeader(), A->getHeader())) return A;
  return A; // Arbitrarily break the tie.
}

/// getRelevantLoop - Get the most relevant loop associated with the given
/// expression, according to PickMostRelevantLoop.
const Loop *SCEVExpander::getRelevantLoop(const SCEV *S) {
  // Test whether we've already computed the most relevant loop for this SCEV.
  auto Pair = RelevantLoops.insert(std::make_pair(S, nullptr));
  if (!Pair.second)
    return Pair.first->second;

  switch (S->getSCEVType()) {
  case scConstant:
  case scVScale:
    return nullptr; // A constant has no relevant loops.
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
  case scPtrToInt:
  case scAddExpr:
  case scMulExpr:
  case scUDivExpr:
  case scAddRecExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr: {
    const Loop *L = nullptr;
    if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S))
      L = AR->getLoop();
    for (const SCEV *Op : S->operands())
      L = PickMostRelevantLoop(L, getRelevantLoop(Op), SE.DT);
    return RelevantLoops[S] = L;
  }
  case scUnknown: {
    const SCEVUnknown *U = cast<SCEVUnknown>(S);
    if (const Instruction *I = dyn_cast<Instruction>(U->getValue()))
      return Pair.first->second = SE.LI.getLoopFor(I->getParent());
    // A non-instruction has no relevant loops.
    return nullptr;
  }
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unexpected SCEV type!");
}

namespace {

/// LoopCompare - Compare loops by PickMostRelevantLoop.
class LoopCompare {
  DominatorTree &DT;
public:
  explicit LoopCompare(DominatorTree &dt) : DT(dt) {}

  bool operator()(std::pair<const Loop *, const SCEV *> LHS,
                  std::pair<const Loop *, const SCEV *> RHS) const {
    // Keep pointer operands sorted at the end.
    if (LHS.second->getType()->isPointerTy() !=
        RHS.second->getType()->isPointerTy())
      return LHS.second->getType()->isPointerTy();

    // Compare loops with PickMostRelevantLoop.
    if (LHS.first != RHS.first)
      return PickMostRelevantLoop(LHS.first, RHS.first, DT) != LHS.first;

    // If one operand is a non-constant negative and the other is not,
    // put the non-constant negative on the right so that a sub can
    // be used instead of a negate and add.
    if (LHS.second->isNonConstantNegative()) {
      if (!RHS.second->isNonConstantNegative())
        return false;
    } else if (RHS.second->isNonConstantNegative())
      return true;

    // Otherwise they are equivalent according to this comparison.
    return false;
  }
};

}

Value *SCEVExpander::visitAddExpr(const SCEVAddExpr *S) {
  // Recognize the canonical representation of an unsimplifed urem.
  const SCEV *URemLHS = nullptr;
  const SCEV *URemRHS = nullptr;
  if (SE.matchURem(S, URemLHS, URemRHS)) {
    Value *LHS = expand(URemLHS);
    Value *RHS = expand(URemRHS);
    return InsertBinop(Instruction::URem, LHS, RHS, SCEV::FlagAnyWrap,
                      /*IsSafeToHoist*/ false);
  }

  // Collect all the add operands in a loop, along with their associated loops.
  // Iterate in reverse so that constants are emitted last, all else equal, and
  // so that pointer operands are inserted first, which the code below relies on
  // to form more involved GEPs.
  SmallVector<std::pair<const Loop *, const SCEV *>, 8> OpsAndLoops;
  for (const SCEV *Op : reverse(S->operands()))
    OpsAndLoops.push_back(std::make_pair(getRelevantLoop(Op), Op));

  // Sort by loop. Use a stable sort so that constants follow non-constants and
  // pointer operands precede non-pointer operands.
  llvm::stable_sort(OpsAndLoops, LoopCompare(SE.DT));

  // Emit instructions to add all the operands. Hoist as much as possible
  // out of loops, and form meaningful getelementptrs where possible.
  Value *Sum = nullptr;
  for (auto I = OpsAndLoops.begin(), E = OpsAndLoops.end(); I != E;) {
    const Loop *CurLoop = I->first;
    const SCEV *Op = I->second;
    if (!Sum) {
      // This is the first operand. Just expand it.
      Sum = expand(Op);
      ++I;
      continue;
    }

    assert(!Op->getType()->isPointerTy() && "Only first op can be pointer");
    if (isa<PointerType>(Sum->getType())) {
      // The running sum expression is a pointer. Try to form a getelementptr
      // at this level with that as the base.
      SmallVector<const SCEV *, 4> NewOps;
      for (; I != E && I->first == CurLoop; ++I) {
        // If the operand is SCEVUnknown and not instructions, peek through
        // it, to enable more of it to be folded into the GEP.
        const SCEV *X = I->second;
        if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(X))
          if (!isa<Instruction>(U->getValue()))
            X = SE.getSCEV(U->getValue());
        NewOps.push_back(X);
      }
      Sum = expandAddToGEP(SE.getAddExpr(NewOps), Sum);
    } else if (Op->isNonConstantNegative()) {
      // Instead of doing a negate and add, just do a subtract.
      Value *W = expand(SE.getNegativeSCEV(Op));
      Sum = InsertBinop(Instruction::Sub, Sum, W, SCEV::FlagAnyWrap,
                        /*IsSafeToHoist*/ true);
      ++I;
    } else {
      // A simple add.
      Value *W = expand(Op);
      // Canonicalize a constant to the RHS.
      if (isa<Constant>(Sum))
        std::swap(Sum, W);
      Sum = InsertBinop(Instruction::Add, Sum, W, S->getNoWrapFlags(),
                        /*IsSafeToHoist*/ true);
      ++I;
    }
  }

  return Sum;
}

Value *SCEVExpander::visitMulExpr(const SCEVMulExpr *S) {
  Type *Ty = S->getType();

  // Collect all the mul operands in a loop, along with their associated loops.
  // Iterate in reverse so that constants are emitted last, all else equal.
  SmallVector<std::pair<const Loop *, const SCEV *>, 8> OpsAndLoops;
  for (const SCEV *Op : reverse(S->operands()))
    OpsAndLoops.push_back(std::make_pair(getRelevantLoop(Op), Op));

  // Sort by loop. Use a stable sort so that constants follow non-constants.
  llvm::stable_sort(OpsAndLoops, LoopCompare(SE.DT));

  // Emit instructions to mul all the operands. Hoist as much as possible
  // out of loops.
  Value *Prod = nullptr;
  auto I = OpsAndLoops.begin();

  // Expand the calculation of X pow N in the following manner:
  // Let N = P1 + P2 + ... + PK, where all P are powers of 2. Then:
  // X pow N = (X pow P1) * (X pow P2) * ... * (X pow PK).
  const auto ExpandOpBinPowN = [this, &I, &OpsAndLoops]() {
    auto E = I;
    // Calculate how many times the same operand from the same loop is included
    // into this power.
    uint64_t Exponent = 0;
    const uint64_t MaxExponent = UINT64_MAX >> 1;
    // No one sane will ever try to calculate such huge exponents, but if we
    // need this, we stop on UINT64_MAX / 2 because we need to exit the loop
    // below when the power of 2 exceeds our Exponent, and we want it to be
    // 1u << 31 at most to not deal with unsigned overflow.
    while (E != OpsAndLoops.end() && *I == *E && Exponent != MaxExponent) {
      ++Exponent;
      ++E;
    }
    assert(Exponent > 0 && "Trying to calculate a zeroth exponent of operand?");

    // Calculate powers with exponents 1, 2, 4, 8 etc. and include those of them
    // that are needed into the result.
    Value *P = expand(I->second);
    Value *Result = nullptr;
    if (Exponent & 1)
      Result = P;
    for (uint64_t BinExp = 2; BinExp <= Exponent; BinExp <<= 1) {
      P = InsertBinop(Instruction::Mul, P, P, SCEV::FlagAnyWrap,
                      /*IsSafeToHoist*/ true);
      if (Exponent & BinExp)
        Result = Result ? InsertBinop(Instruction::Mul, Result, P,
                                      SCEV::FlagAnyWrap,
                                      /*IsSafeToHoist*/ true)
                        : P;
    }

    I = E;
    assert(Result && "Nothing was expanded?");
    return Result;
  };

  while (I != OpsAndLoops.end()) {
    if (!Prod) {
      // This is the first operand. Just expand it.
      Prod = ExpandOpBinPowN();
    } else if (I->second->isAllOnesValue()) {
      // Instead of doing a multiply by negative one, just do a negate.
      Prod = InsertBinop(Instruction::Sub, Constant::getNullValue(Ty), Prod,
                         SCEV::FlagAnyWrap, /*IsSafeToHoist*/ true);
      ++I;
    } else {
      // A simple mul.
      Value *W = ExpandOpBinPowN();
      // Canonicalize a constant to the RHS.
      if (isa<Constant>(Prod)) std::swap(Prod, W);
      const APInt *RHS;
      if (match(W, m_Power2(RHS))) {
        // Canonicalize Prod*(1<<C) to Prod<<C.
        assert(!Ty->isVectorTy() && "vector types are not SCEVable");
        auto NWFlags = S->getNoWrapFlags();
        // clear nsw flag if shl will produce poison value.
        if (RHS->logBase2() == RHS->getBitWidth() - 1)
          NWFlags = ScalarEvolution::clearFlags(NWFlags, SCEV::FlagNSW);
        Prod = InsertBinop(Instruction::Shl, Prod,
                           ConstantInt::get(Ty, RHS->logBase2()), NWFlags,
                           /*IsSafeToHoist*/ true);
      } else {
        Prod = InsertBinop(Instruction::Mul, Prod, W, S->getNoWrapFlags(),
                           /*IsSafeToHoist*/ true);
      }
    }
  }

  return Prod;
}

Value *SCEVExpander::visitUDivExpr(const SCEVUDivExpr *S) {
  Value *LHS = expand(S->getLHS());
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(S->getRHS())) {
    const APInt &RHS = SC->getAPInt();
    if (RHS.isPowerOf2())
      return InsertBinop(Instruction::LShr, LHS,
                         ConstantInt::get(SC->getType(), RHS.logBase2()),
                         SCEV::FlagAnyWrap, /*IsSafeToHoist*/ true);
  }

  Value *RHS = expand(S->getRHS());
  return InsertBinop(Instruction::UDiv, LHS, RHS, SCEV::FlagAnyWrap,
                     /*IsSafeToHoist*/ SE.isKnownNonZero(S->getRHS()));
}

/// Determine if this is a well-behaved chain of instructions leading back to
/// the PHI. If so, it may be reused by expanded expressions.
bool SCEVExpander::isNormalAddRecExprPHI(PHINode *PN, Instruction *IncV,
                                         const Loop *L) {
  if (IncV->getNumOperands() == 0 || isa<PHINode>(IncV) ||
      (isa<CastInst>(IncV) && !isa<BitCastInst>(IncV)))
    return false;
  // If any of the operands don't dominate the insert position, bail.
  // Addrec operands are always loop-invariant, so this can only happen
  // if there are instructions which haven't been hoisted.
  if (L == IVIncInsertLoop) {
    for (Use &Op : llvm::drop_begin(IncV->operands()))
      if (Instruction *OInst = dyn_cast<Instruction>(Op))
        if (!SE.DT.dominates(OInst, IVIncInsertPos))
          return false;
  }
  // Advance to the next instruction.
  IncV = dyn_cast<Instruction>(IncV->getOperand(0));
  if (!IncV)
    return false;

  if (IncV->mayHaveSideEffects())
    return false;

  if (IncV == PN)
    return true;

  return isNormalAddRecExprPHI(PN, IncV, L);
}

/// getIVIncOperand returns an induction variable increment's induction
/// variable operand.
///
/// If allowScale is set, any type of GEP is allowed as long as the nonIV
/// operands dominate InsertPos.
///
/// If allowScale is not set, ensure that a GEP increment conforms to one of the
/// simple patterns generated by getAddRecExprPHILiterally and
/// expandAddtoGEP. If the pattern isn't recognized, return NULL.
Instruction *SCEVExpander::getIVIncOperand(Instruction *IncV,
                                           Instruction *InsertPos,
                                           bool allowScale) {
  if (IncV == InsertPos)
    return nullptr;

  switch (IncV->getOpcode()) {
  default:
    return nullptr;
  // Check for a simple Add/Sub or GEP of a loop invariant step.
  case Instruction::Add:
  case Instruction::Sub: {
    Instruction *OInst = dyn_cast<Instruction>(IncV->getOperand(1));
    if (!OInst || SE.DT.dominates(OInst, InsertPos))
      return dyn_cast<Instruction>(IncV->getOperand(0));
    return nullptr;
  }
  case Instruction::BitCast:
    return dyn_cast<Instruction>(IncV->getOperand(0));
  case Instruction::GetElementPtr:
    for (Use &U : llvm::drop_begin(IncV->operands())) {
      if (isa<Constant>(U))
        continue;
      if (Instruction *OInst = dyn_cast<Instruction>(U)) {
        if (!SE.DT.dominates(OInst, InsertPos))
          return nullptr;
      }
      if (allowScale) {
        // allow any kind of GEP as long as it can be hoisted.
        continue;
      }
      // GEPs produced by SCEVExpander use i8 element type.
      if (!cast<GEPOperator>(IncV)->getSourceElementType()->isIntegerTy(8))
        return nullptr;
      break;
    }
    return dyn_cast<Instruction>(IncV->getOperand(0));
  }
}

/// If the insert point of the current builder or any of the builders on the
/// stack of saved builders has 'I' as its insert point, update it to point to
/// the instruction after 'I'.  This is intended to be used when the instruction
/// 'I' is being moved.  If this fixup is not done and 'I' is moved to a
/// different block, the inconsistent insert point (with a mismatched
/// Instruction and Block) can lead to an instruction being inserted in a block
/// other than its parent.
void SCEVExpander::fixupInsertPoints(Instruction *I) {
  BasicBlock::iterator It(*I);
  BasicBlock::iterator NewInsertPt = std::next(It);
  if (Builder.GetInsertPoint() == It)
    Builder.SetInsertPoint(&*NewInsertPt);
  for (auto *InsertPtGuard : InsertPointGuards)
    if (InsertPtGuard->GetInsertPoint() == It)
      InsertPtGuard->SetInsertPoint(NewInsertPt);
}

/// hoistStep - Attempt to hoist a simple IV increment above InsertPos to make
/// it available to other uses in this loop. Recursively hoist any operands,
/// until we reach a value that dominates InsertPos.
bool SCEVExpander::hoistIVInc(Instruction *IncV, Instruction *InsertPos,
                              bool RecomputePoisonFlags) {
  auto FixupPoisonFlags = [this](Instruction *I) {
    // Drop flags that are potentially inferred from old context and infer flags
    // in new context.
    rememberFlags(I);
    I->dropPoisonGeneratingFlags();
    if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(I))
      if (auto Flags = SE.getStrengthenedNoWrapFlagsFromBinOp(OBO)) {
        auto *BO = cast<BinaryOperator>(I);
        BO->setHasNoUnsignedWrap(
            ScalarEvolution::maskFlags(*Flags, SCEV::FlagNUW) == SCEV::FlagNUW);
        BO->setHasNoSignedWrap(
            ScalarEvolution::maskFlags(*Flags, SCEV::FlagNSW) == SCEV::FlagNSW);
      }
  };

  if (SE.DT.dominates(IncV, InsertPos)) {
    if (RecomputePoisonFlags)
      FixupPoisonFlags(IncV);
    return true;
  }

  // InsertPos must itself dominate IncV so that IncV's new position satisfies
  // its existing users.
  if (isa<PHINode>(InsertPos) ||
      !SE.DT.dominates(InsertPos->getParent(), IncV->getParent()))
    return false;

  if (!SE.LI.movementPreservesLCSSAForm(IncV, InsertPos))
    return false;

  // Check that the chain of IV operands leading back to Phi can be hoisted.
  SmallVector<Instruction*, 4> IVIncs;
  for(;;) {
    Instruction *Oper = getIVIncOperand(IncV, InsertPos, /*allowScale*/true);
    if (!Oper)
      return false;
    // IncV is safe to hoist.
    IVIncs.push_back(IncV);
    IncV = Oper;
    if (SE.DT.dominates(IncV, InsertPos))
      break;
  }
  for (Instruction *I : llvm::reverse(IVIncs)) {
    fixupInsertPoints(I);
    I->moveBefore(InsertPos);
    if (RecomputePoisonFlags)
      FixupPoisonFlags(I);
  }
  return true;
}

bool SCEVExpander::canReuseFlagsFromOriginalIVInc(PHINode *OrigPhi,
                                                  PHINode *WidePhi,
                                                  Instruction *OrigInc,
                                                  Instruction *WideInc) {
  return match(OrigInc, m_c_BinOp(m_Specific(OrigPhi), m_Value())) &&
         match(WideInc, m_c_BinOp(m_Specific(WidePhi), m_Value())) &&
         OrigInc->getOpcode() == WideInc->getOpcode();
}

/// Determine if this cyclic phi is in a form that would have been generated by
/// LSR. We don't care if the phi was actually expanded in this pass, as long
/// as it is in a low-cost form, for example, no implied multiplication. This
/// should match any patterns generated by getAddRecExprPHILiterally and
/// expandAddtoGEP.
bool SCEVExpander::isExpandedAddRecExprPHI(PHINode *PN, Instruction *IncV,
                                           const Loop *L) {
  for(Instruction *IVOper = IncV;
      (IVOper = getIVIncOperand(IVOper, L->getLoopPreheader()->getTerminator(),
                                /*allowScale=*/false));) {
    if (IVOper == PN)
      return true;
  }
  return false;
}

/// expandIVInc - Expand an IV increment at Builder's current InsertPos.
/// Typically this is the LatchBlock terminator or IVIncInsertPos, but we may
/// need to materialize IV increments elsewhere to handle difficult situations.
Value *SCEVExpander::expandIVInc(PHINode *PN, Value *StepV, const Loop *L,
                                 bool useSubtract) {
  Value *IncV;
  // If the PHI is a pointer, use a GEP, otherwise use an add or sub.
  if (PN->getType()->isPointerTy()) {
    // TODO: Change name to IVName.iv.next.
    IncV = Builder.CreatePtrAdd(PN, StepV, "scevgep");
  } else {
    IncV = useSubtract ?
      Builder.CreateSub(PN, StepV, Twine(IVName) + ".iv.next") :
      Builder.CreateAdd(PN, StepV, Twine(IVName) + ".iv.next");
  }
  return IncV;
}

/// Check whether we can cheaply express the requested SCEV in terms of
/// the available PHI SCEV by truncation and/or inversion of the step.
static bool canBeCheaplyTransformed(ScalarEvolution &SE,
                                    const SCEVAddRecExpr *Phi,
                                    const SCEVAddRecExpr *Requested,
                                    bool &InvertStep) {
  // We can't transform to match a pointer PHI.
  Type *PhiTy = Phi->getType();
  Type *RequestedTy = Requested->getType();
  if (PhiTy->isPointerTy() || RequestedTy->isPointerTy())
    return false;

  if (RequestedTy->getIntegerBitWidth() > PhiTy->getIntegerBitWidth())
    return false;

  // Try truncate it if necessary.
  Phi = dyn_cast<SCEVAddRecExpr>(SE.getTruncateOrNoop(Phi, RequestedTy));
  if (!Phi)
    return false;

  // Check whether truncation will help.
  if (Phi == Requested) {
    InvertStep = false;
    return true;
  }

  // Check whether inverting will help: {R,+,-1} == R - {0,+,1}.
  if (SE.getMinusSCEV(Requested->getStart(), Requested) == Phi) {
    InvertStep = true;
    return true;
  }

  return false;
}

static bool IsIncrementNSW(ScalarEvolution &SE, const SCEVAddRecExpr *AR) {
  if (!isa<IntegerType>(AR->getType()))
    return false;

  unsigned BitWidth = cast<IntegerType>(AR->getType())->getBitWidth();
  Type *WideTy = IntegerType::get(AR->getType()->getContext(), BitWidth * 2);
  const SCEV *Step = AR->getStepRecurrence(SE);
  const SCEV *OpAfterExtend = SE.getAddExpr(SE.getSignExtendExpr(Step, WideTy),
                                            SE.getSignExtendExpr(AR, WideTy));
  const SCEV *ExtendAfterOp =
    SE.getSignExtendExpr(SE.getAddExpr(AR, Step), WideTy);
  return ExtendAfterOp == OpAfterExtend;
}

static bool IsIncrementNUW(ScalarEvolution &SE, const SCEVAddRecExpr *AR) {
  if (!isa<IntegerType>(AR->getType()))
    return false;

  unsigned BitWidth = cast<IntegerType>(AR->getType())->getBitWidth();
  Type *WideTy = IntegerType::get(AR->getType()->getContext(), BitWidth * 2);
  const SCEV *Step = AR->getStepRecurrence(SE);
  const SCEV *OpAfterExtend = SE.getAddExpr(SE.getZeroExtendExpr(Step, WideTy),
                                            SE.getZeroExtendExpr(AR, WideTy));
  const SCEV *ExtendAfterOp =
    SE.getZeroExtendExpr(SE.getAddExpr(AR, Step), WideTy);
  return ExtendAfterOp == OpAfterExtend;
}

/// getAddRecExprPHILiterally - Helper for expandAddRecExprLiterally. Expand
/// the base addrec, which is the addrec without any non-loop-dominating
/// values, and return the PHI.
PHINode *
SCEVExpander::getAddRecExprPHILiterally(const SCEVAddRecExpr *Normalized,
                                        const Loop *L, Type *&TruncTy,
                                        bool &InvertStep) {
  assert((!IVIncInsertLoop || IVIncInsertPos) &&
         "Uninitialized insert position");

  // Reuse a previously-inserted PHI, if present.
  BasicBlock *LatchBlock = L->getLoopLatch();
  if (LatchBlock) {
    PHINode *AddRecPhiMatch = nullptr;
    Instruction *IncV = nullptr;
    TruncTy = nullptr;
    InvertStep = false;

    // Only try partially matching scevs that need truncation and/or
    // step-inversion if we know this loop is outside the current loop.
    bool TryNonMatchingSCEV =
        IVIncInsertLoop &&
        SE.DT.properlyDominates(LatchBlock, IVIncInsertLoop->getHeader());

    for (PHINode &PN : L->getHeader()->phis()) {
      if (!SE.isSCEVable(PN.getType()))
        continue;

      // We should not look for a incomplete PHI. Getting SCEV for a incomplete
      // PHI has no meaning at all.
      if (!PN.isComplete()) {
        SCEV_DEBUG_WITH_TYPE(
            DebugType, dbgs() << "One incomplete PHI is found: " << PN << "\n");
        continue;
      }

      const SCEVAddRecExpr *PhiSCEV = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(&PN));
      if (!PhiSCEV)
        continue;

      bool IsMatchingSCEV = PhiSCEV == Normalized;
      // We only handle truncation and inversion of phi recurrences for the
      // expanded expression if the expanded expression's loop dominates the
      // loop we insert to. Check now, so we can bail out early.
      if (!IsMatchingSCEV && !TryNonMatchingSCEV)
          continue;

      // TODO: this possibly can be reworked to avoid this cast at all.
      Instruction *TempIncV =
          dyn_cast<Instruction>(PN.getIncomingValueForBlock(LatchBlock));
      if (!TempIncV)
        continue;

      // Check whether we can reuse this PHI node.
      if (LSRMode) {
        if (!isExpandedAddRecExprPHI(&PN, TempIncV, L))
          continue;
      } else {
        if (!isNormalAddRecExprPHI(&PN, TempIncV, L))
          continue;
      }

      // Stop if we have found an exact match SCEV.
      if (IsMatchingSCEV) {
        IncV = TempIncV;
        TruncTy = nullptr;
        InvertStep = false;
        AddRecPhiMatch = &PN;
        break;
      }

      // Try whether the phi can be translated into the requested form
      // (truncated and/or offset by a constant).
      if ((!TruncTy || InvertStep) &&
          canBeCheaplyTransformed(SE, PhiSCEV, Normalized, InvertStep)) {
        // Record the phi node. But don't stop we might find an exact match
        // later.
        AddRecPhiMatch = &PN;
        IncV = TempIncV;
        TruncTy = Normalized->getType();
      }
    }

    if (AddRecPhiMatch) {
      // Ok, the add recurrence looks usable.
      // Remember this PHI, even in post-inc mode.
      InsertedValues.insert(AddRecPhiMatch);
      // Remember the increment.
      rememberInstruction(IncV);
      // Those values were not actually inserted but re-used.
      ReusedValues.insert(AddRecPhiMatch);
      ReusedValues.insert(IncV);
      return AddRecPhiMatch;
    }
  }

  // Save the original insertion point so we can restore it when we're done.
  SCEVInsertPointGuard Guard(Builder, this);

  // Another AddRec may need to be recursively expanded below. For example, if
  // this AddRec is quadratic, the StepV may itself be an AddRec in this
  // loop. Remove this loop from the PostIncLoops set before expanding such
  // AddRecs. Otherwise, we cannot find a valid position for the step
  // (i.e. StepV can never dominate its loop header).  Ideally, we could do
  // SavedIncLoops.swap(PostIncLoops), but we generally have a single element,
  // so it's not worth implementing SmallPtrSet::swap.
  PostIncLoopSet SavedPostIncLoops = PostIncLoops;
  PostIncLoops.clear();

  // Expand code for the start value into the loop preheader.
  assert(L->getLoopPreheader() &&
         "Can't expand add recurrences without a loop preheader!");
  Value *StartV =
      expand(Normalized->getStart(), L->getLoopPreheader()->getTerminator());

  // StartV must have been be inserted into L's preheader to dominate the new
  // phi.
  assert(!isa<Instruction>(StartV) ||
         SE.DT.properlyDominates(cast<Instruction>(StartV)->getParent(),
                                 L->getHeader()));

  // Expand code for the step value. Do this before creating the PHI so that PHI
  // reuse code doesn't see an incomplete PHI.
  const SCEV *Step = Normalized->getStepRecurrence(SE);
  Type *ExpandTy = Normalized->getType();
  // If the stride is negative, insert a sub instead of an add for the increment
  // (unless it's a constant, because subtracts of constants are canonicalized
  // to adds).
  bool useSubtract = !ExpandTy->isPointerTy() && Step->isNonConstantNegative();
  if (useSubtract)
    Step = SE.getNegativeSCEV(Step);
  // Expand the step somewhere that dominates the loop header.
  Value *StepV = expand(Step, L->getHeader()->getFirstInsertionPt());

  // The no-wrap behavior proved by IsIncrement(NUW|NSW) is only applicable if
  // we actually do emit an addition.  It does not apply if we emit a
  // subtraction.
  bool IncrementIsNUW = !useSubtract && IsIncrementNUW(SE, Normalized);
  bool IncrementIsNSW = !useSubtract && IsIncrementNSW(SE, Normalized);

  // Create the PHI.
  BasicBlock *Header = L->getHeader();
  Builder.SetInsertPoint(Header, Header->begin());
  PHINode *PN =
      Builder.CreatePHI(ExpandTy, pred_size(Header), Twine(IVName) + ".iv");

  // Create the step instructions and populate the PHI.
  for (BasicBlock *Pred : predecessors(Header)) {
    // Add a start value.
    if (!L->contains(Pred)) {
      PN->addIncoming(StartV, Pred);
      continue;
    }

    // Create a step value and add it to the PHI.
    // If IVIncInsertLoop is non-null and equal to the addrec's loop, insert the
    // instructions at IVIncInsertPos.
    Instruction *InsertPos = L == IVIncInsertLoop ?
      IVIncInsertPos : Pred->getTerminator();
    Builder.SetInsertPoint(InsertPos);
    Value *IncV = expandIVInc(PN, StepV, L, useSubtract);

    if (isa<OverflowingBinaryOperator>(IncV)) {
      if (IncrementIsNUW)
        cast<BinaryOperator>(IncV)->setHasNoUnsignedWrap();
      if (IncrementIsNSW)
        cast<BinaryOperator>(IncV)->setHasNoSignedWrap();
    }
    PN->addIncoming(IncV, Pred);
  }

  // After expanding subexpressions, restore the PostIncLoops set so the caller
  // can ensure that IVIncrement dominates the current uses.
  PostIncLoops = SavedPostIncLoops;

  // Remember this PHI, even in post-inc mode. LSR SCEV-based salvaging is most
  // effective when we are able to use an IV inserted here, so record it.
  InsertedValues.insert(PN);
  InsertedIVs.push_back(PN);
  return PN;
}

Value *SCEVExpander::expandAddRecExprLiterally(const SCEVAddRecExpr *S) {
  const Loop *L = S->getLoop();

  // Determine a normalized form of this expression, which is the expression
  // before any post-inc adjustment is made.
  const SCEVAddRecExpr *Normalized = S;
  if (PostIncLoops.count(L)) {
    PostIncLoopSet Loops;
    Loops.insert(L);
    Normalized = cast<SCEVAddRecExpr>(
        normalizeForPostIncUse(S, Loops, SE, /*CheckInvertible=*/false));
  }

  [[maybe_unused]] const SCEV *Start = Normalized->getStart();
  const SCEV *Step = Normalized->getStepRecurrence(SE);
  assert(SE.properlyDominates(Start, L->getHeader()) &&
         "Start does not properly dominate loop header");
  assert(SE.dominates(Step, L->getHeader()) && "Step not dominate loop header");

  // In some cases, we decide to reuse an existing phi node but need to truncate
  // it and/or invert the step.
  Type *TruncTy = nullptr;
  bool InvertStep = false;
  PHINode *PN = getAddRecExprPHILiterally(Normalized, L, TruncTy, InvertStep);

  // Accommodate post-inc mode, if necessary.
  Value *Result;
  if (!PostIncLoops.count(L))
    Result = PN;
  else {
    // In PostInc mode, use the post-incremented value.
    BasicBlock *LatchBlock = L->getLoopLatch();
    assert(LatchBlock && "PostInc mode requires a unique loop latch!");
    Result = PN->getIncomingValueForBlock(LatchBlock);

    // We might be introducing a new use of the post-inc IV that is not poison
    // safe, in which case we should drop poison generating flags. Only keep
    // those flags for which SCEV has proven that they always hold.
    if (isa<OverflowingBinaryOperator>(Result)) {
      auto *I = cast<Instruction>(Result);
      if (!S->hasNoUnsignedWrap())
        I->setHasNoUnsignedWrap(false);
      if (!S->hasNoSignedWrap())
        I->setHasNoSignedWrap(false);
    }

    // For an expansion to use the postinc form, the client must call
    // expandCodeFor with an InsertPoint that is either outside the PostIncLoop
    // or dominated by IVIncInsertPos.
    if (isa<Instruction>(Result) &&
        !SE.DT.dominates(cast<Instruction>(Result),
                         &*Builder.GetInsertPoint())) {
      // The induction variable's postinc expansion does not dominate this use.
      // IVUsers tries to prevent this case, so it is rare. However, it can
      // happen when an IVUser outside the loop is not dominated by the latch
      // block. Adjusting IVIncInsertPos before expansion begins cannot handle
      // all cases. Consider a phi outside whose operand is replaced during
      // expansion with the value of the postinc user. Without fundamentally
      // changing the way postinc users are tracked, the only remedy is
      // inserting an extra IV increment. StepV might fold into PostLoopOffset,
      // but hopefully expandCodeFor handles that.
      bool useSubtract =
          !S->getType()->isPointerTy() && Step->isNonConstantNegative();
      if (useSubtract)
        Step = SE.getNegativeSCEV(Step);
      Value *StepV;
      {
        // Expand the step somewhere that dominates the loop header.
        SCEVInsertPointGuard Guard(Builder, this);
        StepV = expand(Step, L->getHeader()->getFirstInsertionPt());
      }
      Result = expandIVInc(PN, StepV, L, useSubtract);
    }
  }

  // We have decided to reuse an induction variable of a dominating loop. Apply
  // truncation and/or inversion of the step.
  if (TruncTy) {
    // Truncate the result.
    if (TruncTy != Result->getType())
      Result = Builder.CreateTrunc(Result, TruncTy);

    // Invert the result.
    if (InvertStep)
      Result = Builder.CreateSub(expand(Normalized->getStart()), Result);
  }

  return Result;
}

Value *SCEVExpander::visitAddRecExpr(const SCEVAddRecExpr *S) {
  // In canonical mode we compute the addrec as an expression of a canonical IV
  // using evaluateAtIteration and expand the resulting SCEV expression. This
  // way we avoid introducing new IVs to carry on the computation of the addrec
  // throughout the loop.
  //
  // For nested addrecs evaluateAtIteration might need a canonical IV of a
  // type wider than the addrec itself. Emitting a canonical IV of the
  // proper type might produce non-legal types, for example expanding an i64
  // {0,+,2,+,1} addrec would need an i65 canonical IV. To avoid this just fall
  // back to non-canonical mode for nested addrecs.
  if (!CanonicalMode || (S->getNumOperands() > 2))
    return expandAddRecExprLiterally(S);

  Type *Ty = SE.getEffectiveSCEVType(S->getType());
  const Loop *L = S->getLoop();

  // First check for an existing canonical IV in a suitable type.
  PHINode *CanonicalIV = nullptr;
  if (PHINode *PN = L->getCanonicalInductionVariable())
    if (SE.getTypeSizeInBits(PN->getType()) >= SE.getTypeSizeInBits(Ty))
      CanonicalIV = PN;

  // Rewrite an AddRec in terms of the canonical induction variable, if
  // its type is more narrow.
  if (CanonicalIV &&
      SE.getTypeSizeInBits(CanonicalIV->getType()) > SE.getTypeSizeInBits(Ty) &&
      !S->getType()->isPointerTy()) {
    SmallVector<const SCEV *, 4> NewOps(S->getNumOperands());
    for (unsigned i = 0, e = S->getNumOperands(); i != e; ++i)
      NewOps[i] = SE.getAnyExtendExpr(S->getOperand(i), CanonicalIV->getType());
    Value *V = expand(SE.getAddRecExpr(NewOps, S->getLoop(),
                                       S->getNoWrapFlags(SCEV::FlagNW)));
    BasicBlock::iterator NewInsertPt =
        findInsertPointAfter(cast<Instruction>(V), &*Builder.GetInsertPoint());
    V = expand(SE.getTruncateExpr(SE.getUnknown(V), Ty), NewInsertPt);
    return V;
  }

  // {X,+,F} --> X + {0,+,F}
  if (!S->getStart()->isZero()) {
    if (isa<PointerType>(S->getType())) {
      Value *StartV = expand(SE.getPointerBase(S));
      return expandAddToGEP(SE.removePointerBase(S), StartV);
    }

    SmallVector<const SCEV *, 4> NewOps(S->operands());
    NewOps[0] = SE.getConstant(Ty, 0);
    const SCEV *Rest = SE.getAddRecExpr(NewOps, L,
                                        S->getNoWrapFlags(SCEV::FlagNW));

    // Just do a normal add. Pre-expand the operands to suppress folding.
    //
    // The LHS and RHS values are factored out of the expand call to make the
    // output independent of the argument evaluation order.
    const SCEV *AddExprLHS = SE.getUnknown(expand(S->getStart()));
    const SCEV *AddExprRHS = SE.getUnknown(expand(Rest));
    return expand(SE.getAddExpr(AddExprLHS, AddExprRHS));
  }

  // If we don't yet have a canonical IV, create one.
  if (!CanonicalIV) {
    // Create and insert the PHI node for the induction variable in the
    // specified loop.
    BasicBlock *Header = L->getHeader();
    pred_iterator HPB = pred_begin(Header), HPE = pred_end(Header);
    CanonicalIV = PHINode::Create(Ty, std::distance(HPB, HPE), "indvar");
    CanonicalIV->insertBefore(Header->begin());
    rememberInstruction(CanonicalIV);

    SmallSet<BasicBlock *, 4> PredSeen;
    Constant *One = ConstantInt::get(Ty, 1);
    for (pred_iterator HPI = HPB; HPI != HPE; ++HPI) {
      BasicBlock *HP = *HPI;
      if (!PredSeen.insert(HP).second) {
        // There must be an incoming value for each predecessor, even the
        // duplicates!
        CanonicalIV->addIncoming(CanonicalIV->getIncomingValueForBlock(HP), HP);
        continue;
      }

      if (L->contains(HP)) {
        // Insert a unit add instruction right before the terminator
        // corresponding to the back-edge.
        Instruction *Add = BinaryOperator::CreateAdd(CanonicalIV, One,
                                                     "indvar.next",
                                                     HP->getTerminator()->getIterator());
        Add->setDebugLoc(HP->getTerminator()->getDebugLoc());
        rememberInstruction(Add);
        CanonicalIV->addIncoming(Add, HP);
      } else {
        CanonicalIV->addIncoming(Constant::getNullValue(Ty), HP);
      }
    }
  }

  // {0,+,1} --> Insert a canonical induction variable into the loop!
  if (S->isAffine() && S->getOperand(1)->isOne()) {
    assert(Ty == SE.getEffectiveSCEVType(CanonicalIV->getType()) &&
           "IVs with types different from the canonical IV should "
           "already have been handled!");
    return CanonicalIV;
  }

  // {0,+,F} --> {0,+,1} * F

  // If this is a simple linear addrec, emit it now as a special case.
  if (S->isAffine())    // {0,+,F} --> i*F
    return
      expand(SE.getTruncateOrNoop(
        SE.getMulExpr(SE.getUnknown(CanonicalIV),
                      SE.getNoopOrAnyExtend(S->getOperand(1),
                                            CanonicalIV->getType())),
        Ty));

  // If this is a chain of recurrences, turn it into a closed form, using the
  // folders, then expandCodeFor the closed form.  This allows the folders to
  // simplify the expression without having to build a bunch of special code
  // into this folder.
  const SCEV *IH = SE.getUnknown(CanonicalIV);   // Get I as a "symbolic" SCEV.

  // Promote S up to the canonical IV type, if the cast is foldable.
  const SCEV *NewS = S;
  const SCEV *Ext = SE.getNoopOrAnyExtend(S, CanonicalIV->getType());
  if (isa<SCEVAddRecExpr>(Ext))
    NewS = Ext;

  const SCEV *V = cast<SCEVAddRecExpr>(NewS)->evaluateAtIteration(IH, SE);

  // Truncate the result down to the original type, if needed.
  const SCEV *T = SE.getTruncateOrNoop(V, Ty);
  return expand(T);
}

Value *SCEVExpander::visitPtrToIntExpr(const SCEVPtrToIntExpr *S) {
  Value *V = expand(S->getOperand());
  return ReuseOrCreateCast(V, S->getType(), CastInst::PtrToInt,
                           GetOptimalInsertionPointForCastOf(V));
}

Value *SCEVExpander::visitTruncateExpr(const SCEVTruncateExpr *S) {
  Value *V = expand(S->getOperand());
  return Builder.CreateTrunc(V, S->getType());
}

Value *SCEVExpander::visitZeroExtendExpr(const SCEVZeroExtendExpr *S) {
  Value *V = expand(S->getOperand());
  return Builder.CreateZExt(V, S->getType(), "",
                            SE.isKnownNonNegative(S->getOperand()));
}

Value *SCEVExpander::visitSignExtendExpr(const SCEVSignExtendExpr *S) {
  Value *V = expand(S->getOperand());
  return Builder.CreateSExt(V, S->getType());
}

Value *SCEVExpander::expandMinMaxExpr(const SCEVNAryExpr *S,
                                      Intrinsic::ID IntrinID, Twine Name,
                                      bool IsSequential) {
  Value *LHS = expand(S->getOperand(S->getNumOperands() - 1));
  Type *Ty = LHS->getType();
  if (IsSequential)
    LHS = Builder.CreateFreeze(LHS);
  for (int i = S->getNumOperands() - 2; i >= 0; --i) {
    Value *RHS = expand(S->getOperand(i));
    if (IsSequential && i != 0)
      RHS = Builder.CreateFreeze(RHS);
    Value *Sel;
    if (Ty->isIntegerTy())
      Sel = Builder.CreateIntrinsic(IntrinID, {Ty}, {LHS, RHS},
                                    /*FMFSource=*/nullptr, Name);
    else {
      Value *ICmp =
          Builder.CreateICmp(MinMaxIntrinsic::getPredicate(IntrinID), LHS, RHS);
      Sel = Builder.CreateSelect(ICmp, LHS, RHS, Name);
    }
    LHS = Sel;
  }
  return LHS;
}

Value *SCEVExpander::visitSMaxExpr(const SCEVSMaxExpr *S) {
  return expandMinMaxExpr(S, Intrinsic::smax, "smax");
}

Value *SCEVExpander::visitUMaxExpr(const SCEVUMaxExpr *S) {
  return expandMinMaxExpr(S, Intrinsic::umax, "umax");
}

Value *SCEVExpander::visitSMinExpr(const SCEVSMinExpr *S) {
  return expandMinMaxExpr(S, Intrinsic::smin, "smin");
}

Value *SCEVExpander::visitUMinExpr(const SCEVUMinExpr *S) {
  return expandMinMaxExpr(S, Intrinsic::umin, "umin");
}

Value *SCEVExpander::visitSequentialUMinExpr(const SCEVSequentialUMinExpr *S) {
  return expandMinMaxExpr(S, Intrinsic::umin, "umin", /*IsSequential*/true);
}

Value *SCEVExpander::visitVScale(const SCEVVScale *S) {
  return Builder.CreateVScale(ConstantInt::get(S->getType(), 1));
}

Value *SCEVExpander::expandCodeFor(const SCEV *SH, Type *Ty,
                                   BasicBlock::iterator IP) {
  setInsertPoint(IP);
  Value *V = expandCodeFor(SH, Ty);
  return V;
}

Value *SCEVExpander::expandCodeFor(const SCEV *SH, Type *Ty) {
  // Expand the code for this SCEV.
  Value *V = expand(SH);

  if (Ty) {
    assert(SE.getTypeSizeInBits(Ty) == SE.getTypeSizeInBits(SH->getType()) &&
           "non-trivial casts should be done with the SCEVs directly!");
    V = InsertNoopCastOfTo(V, Ty);
  }
  return V;
}

Value *SCEVExpander::FindValueInExprValueMap(
    const SCEV *S, const Instruction *InsertPt,
    SmallVectorImpl<Instruction *> &DropPoisonGeneratingInsts) {
  // If the expansion is not in CanonicalMode, and the SCEV contains any
  // sub scAddRecExpr type SCEV, it is required to expand the SCEV literally.
  if (!CanonicalMode && SE.containsAddRecurrence(S))
    return nullptr;

  // If S is a constant, it may be worse to reuse an existing Value.
  if (isa<SCEVConstant>(S))
    return nullptr;

  for (Value *V : SE.getSCEVValues(S)) {
    Instruction *EntInst = dyn_cast<Instruction>(V);
    if (!EntInst)
      continue;

    // Choose a Value from the set which dominates the InsertPt.
    // InsertPt should be inside the Value's parent loop so as not to break
    // the LCSSA form.
    assert(EntInst->getFunction() == InsertPt->getFunction());
    if (S->getType() != V->getType() || !SE.DT.dominates(EntInst, InsertPt) ||
        !(SE.LI.getLoopFor(EntInst->getParent()) == nullptr ||
          SE.LI.getLoopFor(EntInst->getParent())->contains(InsertPt)))
      continue;

    // Make sure reusing the instruction is poison-safe.
    if (SE.canReuseInstruction(S, EntInst, DropPoisonGeneratingInsts))
      return V;
    DropPoisonGeneratingInsts.clear();
  }
  return nullptr;
}

// The expansion of SCEV will either reuse a previous Value in ExprValueMap,
// or expand the SCEV literally. Specifically, if the expansion is in LSRMode,
// and the SCEV contains any sub scAddRecExpr type SCEV, it will be expanded
// literally, to prevent LSR's transformed SCEV from being reverted. Otherwise,
// the expansion will try to reuse Value from ExprValueMap, and only when it
// fails, expand the SCEV literally.
Value *SCEVExpander::expand(const SCEV *S) {
  // Compute an insertion point for this SCEV object. Hoist the instructions
  // as far out in the loop nest as possible.
  BasicBlock::iterator InsertPt = Builder.GetInsertPoint();

  // We can move insertion point only if there is no div or rem operations
  // otherwise we are risky to move it over the check for zero denominator.
  auto SafeToHoist = [](const SCEV *S) {
    return !SCEVExprContains(S, [](const SCEV *S) {
              if (const auto *D = dyn_cast<SCEVUDivExpr>(S)) {
                if (const auto *SC = dyn_cast<SCEVConstant>(D->getRHS()))
                  // Division by non-zero constants can be hoisted.
                  return SC->getValue()->isZero();
                // All other divisions should not be moved as they may be
                // divisions by zero and should be kept within the
                // conditions of the surrounding loops that guard their
                // execution (see PR35406).
                return true;
              }
              return false;
            });
  };
  if (SafeToHoist(S)) {
    for (Loop *L = SE.LI.getLoopFor(Builder.GetInsertBlock());;
         L = L->getParentLoop()) {
      if (SE.isLoopInvariant(S, L)) {
        if (!L) break;
        if (BasicBlock *Preheader = L->getLoopPreheader()) {
          InsertPt = Preheader->getTerminator()->getIterator();
        } else {
          // LSR sets the insertion point for AddRec start/step values to the
          // block start to simplify value reuse, even though it's an invalid
          // position. SCEVExpander must correct for this in all cases.
          InsertPt = L->getHeader()->getFirstInsertionPt();
        }
      } else {
        // If the SCEV is computable at this level, insert it into the header
        // after the PHIs (and after any other instructions that we've inserted
        // there) so that it is guaranteed to dominate any user inside the loop.
        if (L && SE.hasComputableLoopEvolution(S, L) && !PostIncLoops.count(L))
          InsertPt = L->getHeader()->getFirstInsertionPt();

        while (InsertPt != Builder.GetInsertPoint() &&
               (isInsertedInstruction(&*InsertPt) ||
                isa<DbgInfoIntrinsic>(&*InsertPt))) {
          InsertPt = std::next(InsertPt);
        }
        break;
      }
    }
  }

  // Check to see if we already expanded this here.
  auto I = InsertedExpressions.find(std::make_pair(S, &*InsertPt));
  if (I != InsertedExpressions.end())
    return I->second;

  SCEVInsertPointGuard Guard(Builder, this);
  Builder.SetInsertPoint(InsertPt->getParent(), InsertPt);

  // Expand the expression into instructions.
  SmallVector<Instruction *> DropPoisonGeneratingInsts;
  Value *V = FindValueInExprValueMap(S, &*InsertPt, DropPoisonGeneratingInsts);
  if (!V) {
    V = visit(S);
    V = fixupLCSSAFormFor(V);
  } else {
    for (Instruction *I : DropPoisonGeneratingInsts) {
      rememberFlags(I);
      I->dropPoisonGeneratingAnnotations();
      // See if we can re-infer from first principles any of the flags we just
      // dropped.
      if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(I))
        if (auto Flags = SE.getStrengthenedNoWrapFlagsFromBinOp(OBO)) {
          auto *BO = cast<BinaryOperator>(I);
          BO->setHasNoUnsignedWrap(
            ScalarEvolution::maskFlags(*Flags, SCEV::FlagNUW) == SCEV::FlagNUW);
          BO->setHasNoSignedWrap(
            ScalarEvolution::maskFlags(*Flags, SCEV::FlagNSW) == SCEV::FlagNSW);
        }
      if (auto *NNI = dyn_cast<PossiblyNonNegInst>(I)) {
        auto *Src = NNI->getOperand(0);
        if (isImpliedByDomCondition(ICmpInst::ICMP_SGE, Src,
                                    Constant::getNullValue(Src->getType()), I,
                                    DL).value_or(false))
          NNI->setNonNeg(true);
      }
    }
  }
  // Remember the expanded value for this SCEV at this location.
  //
  // This is independent of PostIncLoops. The mapped value simply materializes
  // the expression at this insertion point. If the mapped value happened to be
  // a postinc expansion, it could be reused by a non-postinc user, but only if
  // its insertion point was already at the head of the loop.
  InsertedExpressions[std::make_pair(S, &*InsertPt)] = V;
  return V;
}

void SCEVExpander::rememberInstruction(Value *I) {
  auto DoInsert = [this](Value *V) {
    if (!PostIncLoops.empty())
      InsertedPostIncValues.insert(V);
    else
      InsertedValues.insert(V);
  };
  DoInsert(I);
}

void SCEVExpander::rememberFlags(Instruction *I) {
  // If we already have flags for the instruction, keep the existing ones.
  OrigFlags.try_emplace(I, PoisonFlags(I));
}

void SCEVExpander::replaceCongruentIVInc(
    PHINode *&Phi, PHINode *&OrigPhi, Loop *L, const DominatorTree *DT,
    SmallVectorImpl<WeakTrackingVH> &DeadInsts) {
  BasicBlock *LatchBlock = L->getLoopLatch();
  if (!LatchBlock)
    return;

  Instruction *OrigInc =
      dyn_cast<Instruction>(OrigPhi->getIncomingValueForBlock(LatchBlock));
  Instruction *IsomorphicInc =
      dyn_cast<Instruction>(Phi->getIncomingValueForBlock(LatchBlock));
  if (!OrigInc || !IsomorphicInc)
    return;

  // If this phi has the same width but is more canonical, replace the
  // original with it. As part of the "more canonical" determination,
  // respect a prior decision to use an IV chain.
  if (OrigPhi->getType() == Phi->getType() &&
      !(ChainedPhis.count(Phi) ||
        isExpandedAddRecExprPHI(OrigPhi, OrigInc, L)) &&
      (ChainedPhis.count(Phi) ||
       isExpandedAddRecExprPHI(Phi, IsomorphicInc, L))) {
    std::swap(OrigPhi, Phi);
    std::swap(OrigInc, IsomorphicInc);
  }

  // Replacing the congruent phi is sufficient because acyclic
  // redundancy elimination, CSE/GVN, should handle the
  // rest. However, once SCEV proves that a phi is congruent,
  // it's often the head of an IV user cycle that is isomorphic
  // with the original phi. It's worth eagerly cleaning up the
  // common case of a single IV increment so that DeleteDeadPHIs
  // can remove cycles that had postinc uses.
  // Because we may potentially introduce a new use of OrigIV that didn't
  // exist before at this point, its poison flags need readjustment.
  const SCEV *TruncExpr =
      SE.getTruncateOrNoop(SE.getSCEV(OrigInc), IsomorphicInc->getType());
  if (OrigInc == IsomorphicInc || TruncExpr != SE.getSCEV(IsomorphicInc) ||
      !SE.LI.replacementPreservesLCSSAForm(IsomorphicInc, OrigInc))
    return;

  bool BothHaveNUW = false;
  bool BothHaveNSW = false;
  auto *OBOIncV = dyn_cast<OverflowingBinaryOperator>(OrigInc);
  auto *OBOIsomorphic = dyn_cast<OverflowingBinaryOperator>(IsomorphicInc);
  if (OBOIncV && OBOIsomorphic) {
    BothHaveNUW =
        OBOIncV->hasNoUnsignedWrap() && OBOIsomorphic->hasNoUnsignedWrap();
    BothHaveNSW =
        OBOIncV->hasNoSignedWrap() && OBOIsomorphic->hasNoSignedWrap();
  }

  if (!hoistIVInc(OrigInc, IsomorphicInc,
                  /*RecomputePoisonFlags*/ true))
    return;

  // We are replacing with a wider increment. If both OrigInc and IsomorphicInc
  // are NUW/NSW, then we can preserve them on the wider increment; the narrower
  // IsomorphicInc would wrap before the wider OrigInc, so the replacement won't
  // make IsomorphicInc's uses more poisonous.
  assert(OrigInc->getType()->getScalarSizeInBits() >=
             IsomorphicInc->getType()->getScalarSizeInBits() &&
         "Should only replace an increment with a wider one.");
  if (BothHaveNUW || BothHaveNSW) {
    OrigInc->setHasNoUnsignedWrap(OBOIncV->hasNoUnsignedWrap() || BothHaveNUW);
    OrigInc->setHasNoSignedWrap(OBOIncV->hasNoSignedWrap() || BothHaveNSW);
  }

  SCEV_DEBUG_WITH_TYPE(DebugType,
                       dbgs() << "INDVARS: Eliminated congruent iv.inc: "
                              << *IsomorphicInc << '\n');
  Value *NewInc = OrigInc;
  if (OrigInc->getType() != IsomorphicInc->getType()) {
    BasicBlock::iterator IP;
    if (PHINode *PN = dyn_cast<PHINode>(OrigInc))
      IP = PN->getParent()->getFirstInsertionPt();
    else
      IP = OrigInc->getNextNonDebugInstruction()->getIterator();

    IRBuilder<> Builder(IP->getParent(), IP);
    Builder.SetCurrentDebugLocation(IsomorphicInc->getDebugLoc());
    NewInc =
        Builder.CreateTruncOrBitCast(OrigInc, IsomorphicInc->getType(), IVName);
  }
  IsomorphicInc->replaceAllUsesWith(NewInc);
  DeadInsts.emplace_back(IsomorphicInc);
}

/// replaceCongruentIVs - Check for congruent phis in this loop header and
/// replace them with their most canonical representative. Return the number of
/// phis eliminated.
///
/// This does not depend on any SCEVExpander state but should be used in
/// the same context that SCEVExpander is used.
unsigned
SCEVExpander::replaceCongruentIVs(Loop *L, const DominatorTree *DT,
                                  SmallVectorImpl<WeakTrackingVH> &DeadInsts,
                                  const TargetTransformInfo *TTI) {
  // Find integer phis in order of increasing width.
  SmallVector<PHINode*, 8> Phis;
  for (PHINode &PN : L->getHeader()->phis())
    Phis.push_back(&PN);

  if (TTI)
    // Use stable_sort to preserve order of equivalent PHIs, so the order
    // of the sorted Phis is the same from run to run on the same loop.
    llvm::stable_sort(Phis, [](Value *LHS, Value *RHS) {
      // Put pointers at the back and make sure pointer < pointer = false.
      if (!LHS->getType()->isIntegerTy() || !RHS->getType()->isIntegerTy())
        return RHS->getType()->isIntegerTy() && !LHS->getType()->isIntegerTy();
      return RHS->getType()->getPrimitiveSizeInBits().getFixedValue() <
             LHS->getType()->getPrimitiveSizeInBits().getFixedValue();
    });

  unsigned NumElim = 0;
  DenseMap<const SCEV *, PHINode *> ExprToIVMap;
  // Process phis from wide to narrow. Map wide phis to their truncation
  // so narrow phis can reuse them.
  for (PHINode *Phi : Phis) {
    auto SimplifyPHINode = [&](PHINode *PN) -> Value * {
      if (Value *V = simplifyInstruction(PN, {DL, &SE.TLI, &SE.DT, &SE.AC}))
        return V;
      if (!SE.isSCEVable(PN->getType()))
        return nullptr;
      auto *Const = dyn_cast<SCEVConstant>(SE.getSCEV(PN));
      if (!Const)
        return nullptr;
      return Const->getValue();
    };

    // Fold constant phis. They may be congruent to other constant phis and
    // would confuse the logic below that expects proper IVs.
    if (Value *V = SimplifyPHINode(Phi)) {
      if (V->getType() != Phi->getType())
        continue;
      SE.forgetValue(Phi);
      Phi->replaceAllUsesWith(V);
      DeadInsts.emplace_back(Phi);
      ++NumElim;
      SCEV_DEBUG_WITH_TYPE(DebugType,
                           dbgs() << "INDVARS: Eliminated constant iv: " << *Phi
                                  << '\n');
      continue;
    }

    if (!SE.isSCEVable(Phi->getType()))
      continue;

    PHINode *&OrigPhiRef = ExprToIVMap[SE.getSCEV(Phi)];
    if (!OrigPhiRef) {
      OrigPhiRef = Phi;
      if (Phi->getType()->isIntegerTy() && TTI &&
          TTI->isTruncateFree(Phi->getType(), Phis.back()->getType())) {
        // Make sure we only rewrite using simple induction variables;
        // otherwise, we can make the trip count of a loop unanalyzable
        // to SCEV.
        const SCEV *PhiExpr = SE.getSCEV(Phi);
        if (isa<SCEVAddRecExpr>(PhiExpr)) {
          // This phi can be freely truncated to the narrowest phi type. Map the
          // truncated expression to it so it will be reused for narrow types.
          const SCEV *TruncExpr =
              SE.getTruncateExpr(PhiExpr, Phis.back()->getType());
          ExprToIVMap[TruncExpr] = Phi;
        }
      }
      continue;
    }

    // Replacing a pointer phi with an integer phi or vice-versa doesn't make
    // sense.
    if (OrigPhiRef->getType()->isPointerTy() != Phi->getType()->isPointerTy())
      continue;

    replaceCongruentIVInc(Phi, OrigPhiRef, L, DT, DeadInsts);
    SCEV_DEBUG_WITH_TYPE(DebugType,
                         dbgs() << "INDVARS: Eliminated congruent iv: " << *Phi
                                << '\n');
    SCEV_DEBUG_WITH_TYPE(
        DebugType, dbgs() << "INDVARS: Original iv: " << *OrigPhiRef << '\n');
    ++NumElim;
    Value *NewIV = OrigPhiRef;
    if (OrigPhiRef->getType() != Phi->getType()) {
      IRBuilder<> Builder(L->getHeader(),
                          L->getHeader()->getFirstInsertionPt());
      Builder.SetCurrentDebugLocation(Phi->getDebugLoc());
      NewIV = Builder.CreateTruncOrBitCast(OrigPhiRef, Phi->getType(), IVName);
    }
    Phi->replaceAllUsesWith(NewIV);
    DeadInsts.emplace_back(Phi);
  }
  return NumElim;
}

bool SCEVExpander::hasRelatedExistingExpansion(const SCEV *S,
                                               const Instruction *At,
                                               Loop *L) {
  using namespace llvm::PatternMatch;

  SmallVector<BasicBlock *, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  // Look for suitable value in simple conditions at the loop exits.
  for (BasicBlock *BB : ExitingBlocks) {
    ICmpInst::Predicate Pred;
    Instruction *LHS, *RHS;

    if (!match(BB->getTerminator(),
               m_Br(m_ICmp(Pred, m_Instruction(LHS), m_Instruction(RHS)),
                    m_BasicBlock(), m_BasicBlock())))
      continue;

    if (SE.getSCEV(LHS) == S && SE.DT.dominates(LHS, At))
      return true;

    if (SE.getSCEV(RHS) == S && SE.DT.dominates(RHS, At))
      return true;
  }

  // Use expand's logic which is used for reusing a previous Value in
  // ExprValueMap.  Note that we don't currently model the cost of
  // needing to drop poison generating flags on the instruction if we
  // want to reuse it.  We effectively assume that has zero cost.
  SmallVector<Instruction *> DropPoisonGeneratingInsts;
  return FindValueInExprValueMap(S, At, DropPoisonGeneratingInsts) != nullptr;
}

template<typename T> static InstructionCost costAndCollectOperands(
  const SCEVOperand &WorkItem, const TargetTransformInfo &TTI,
  TargetTransformInfo::TargetCostKind CostKind,
  SmallVectorImpl<SCEVOperand> &Worklist) {

  const T *S = cast<T>(WorkItem.S);
  InstructionCost Cost = 0;
  // Object to help map SCEV operands to expanded IR instructions.
  struct OperationIndices {
    OperationIndices(unsigned Opc, size_t min, size_t max) :
      Opcode(Opc), MinIdx(min), MaxIdx(max) { }
    unsigned Opcode;
    size_t MinIdx;
    size_t MaxIdx;
  };

  // Collect the operations of all the instructions that will be needed to
  // expand the SCEVExpr. This is so that when we come to cost the operands,
  // we know what the generated user(s) will be.
  SmallVector<OperationIndices, 2> Operations;

  auto CastCost = [&](unsigned Opcode) -> InstructionCost {
    Operations.emplace_back(Opcode, 0, 0);
    return TTI.getCastInstrCost(Opcode, S->getType(),
                                S->getOperand(0)->getType(),
                                TTI::CastContextHint::None, CostKind);
  };

  auto ArithCost = [&](unsigned Opcode, unsigned NumRequired,
                       unsigned MinIdx = 0,
                       unsigned MaxIdx = 1) -> InstructionCost {
    Operations.emplace_back(Opcode, MinIdx, MaxIdx);
    return NumRequired *
      TTI.getArithmeticInstrCost(Opcode, S->getType(), CostKind);
  };

  auto CmpSelCost = [&](unsigned Opcode, unsigned NumRequired, unsigned MinIdx,
                        unsigned MaxIdx) -> InstructionCost {
    Operations.emplace_back(Opcode, MinIdx, MaxIdx);
    Type *OpType = S->getType();
    return NumRequired * TTI.getCmpSelInstrCost(
                             Opcode, OpType, CmpInst::makeCmpResultType(OpType),
                             CmpInst::BAD_ICMP_PREDICATE, CostKind);
  };

  switch (S->getSCEVType()) {
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  case scUnknown:
  case scConstant:
  case scVScale:
    return 0;
  case scPtrToInt:
    Cost = CastCost(Instruction::PtrToInt);
    break;
  case scTruncate:
    Cost = CastCost(Instruction::Trunc);
    break;
  case scZeroExtend:
    Cost = CastCost(Instruction::ZExt);
    break;
  case scSignExtend:
    Cost = CastCost(Instruction::SExt);
    break;
  case scUDivExpr: {
    unsigned Opcode = Instruction::UDiv;
    if (auto *SC = dyn_cast<SCEVConstant>(S->getOperand(1)))
      if (SC->getAPInt().isPowerOf2())
        Opcode = Instruction::LShr;
    Cost = ArithCost(Opcode, 1);
    break;
  }
  case scAddExpr:
    Cost = ArithCost(Instruction::Add, S->getNumOperands() - 1);
    break;
  case scMulExpr:
    // TODO: this is a very pessimistic cost modelling for Mul,
    // because of Bin Pow algorithm actually used by the expander,
    // see SCEVExpander::visitMulExpr(), ExpandOpBinPowN().
    Cost = ArithCost(Instruction::Mul, S->getNumOperands() - 1);
    break;
  case scSMaxExpr:
  case scUMaxExpr:
  case scSMinExpr:
  case scUMinExpr:
  case scSequentialUMinExpr: {
    // FIXME: should this ask the cost for Intrinsic's?
    // The reduction tree.
    Cost += CmpSelCost(Instruction::ICmp, S->getNumOperands() - 1, 0, 1);
    Cost += CmpSelCost(Instruction::Select, S->getNumOperands() - 1, 0, 2);
    switch (S->getSCEVType()) {
    case scSequentialUMinExpr: {
      // The safety net against poison.
      // FIXME: this is broken.
      Cost += CmpSelCost(Instruction::ICmp, S->getNumOperands() - 1, 0, 0);
      Cost += ArithCost(Instruction::Or,
                        S->getNumOperands() > 2 ? S->getNumOperands() - 2 : 0);
      Cost += CmpSelCost(Instruction::Select, 1, 0, 1);
      break;
    }
    default:
      assert(!isa<SCEVSequentialMinMaxExpr>(S) &&
             "Unhandled SCEV expression type?");
      break;
    }
    break;
  }
  case scAddRecExpr: {
    // In this polynominal, we may have some zero operands, and we shouldn't
    // really charge for those. So how many non-zero coefficients are there?
    int NumTerms = llvm::count_if(S->operands(), [](const SCEV *Op) {
                                    return !Op->isZero();
                                  });

    assert(NumTerms >= 1 && "Polynominal should have at least one term.");
    assert(!(*std::prev(S->operands().end()))->isZero() &&
           "Last operand should not be zero");

    // Ignoring constant term (operand 0), how many of the coefficients are u> 1?
    int NumNonZeroDegreeNonOneTerms =
      llvm::count_if(S->operands(), [](const SCEV *Op) {
                      auto *SConst = dyn_cast<SCEVConstant>(Op);
                      return !SConst || SConst->getAPInt().ugt(1);
                    });

    // Much like with normal add expr, the polynominal will require
    // one less addition than the number of it's terms.
    InstructionCost AddCost = ArithCost(Instruction::Add, NumTerms - 1,
                                        /*MinIdx*/ 1, /*MaxIdx*/ 1);
    // Here, *each* one of those will require a multiplication.
    InstructionCost MulCost =
        ArithCost(Instruction::Mul, NumNonZeroDegreeNonOneTerms);
    Cost = AddCost + MulCost;

    // What is the degree of this polynominal?
    int PolyDegree = S->getNumOperands() - 1;
    assert(PolyDegree >= 1 && "Should be at least affine.");

    // The final term will be:
    //   Op_{PolyDegree} * x ^ {PolyDegree}
    // Where  x ^ {PolyDegree}  will again require PolyDegree-1 mul operations.
    // Note that  x ^ {PolyDegree} = x * x ^ {PolyDegree-1}  so charging for
    // x ^ {PolyDegree}  will give us  x ^ {2} .. x ^ {PolyDegree-1}  for free.
    // FIXME: this is conservatively correct, but might be overly pessimistic.
    Cost += MulCost * (PolyDegree - 1);
    break;
  }
  }

  for (auto &CostOp : Operations) {
    for (auto SCEVOp : enumerate(S->operands())) {
      // Clamp the index to account for multiple IR operations being chained.
      size_t MinIdx = std::max(SCEVOp.index(), CostOp.MinIdx);
      size_t OpIdx = std::min(MinIdx, CostOp.MaxIdx);
      Worklist.emplace_back(CostOp.Opcode, OpIdx, SCEVOp.value());
    }
  }
  return Cost;
}

bool SCEVExpander::isHighCostExpansionHelper(
    const SCEVOperand &WorkItem, Loop *L, const Instruction &At,
    InstructionCost &Cost, unsigned Budget, const TargetTransformInfo &TTI,
    SmallPtrSetImpl<const SCEV *> &Processed,
    SmallVectorImpl<SCEVOperand> &Worklist) {
  if (Cost > Budget)
    return true; // Already run out of budget, give up.

  const SCEV *S = WorkItem.S;
  // Was the cost of expansion of this expression already accounted for?
  if (!isa<SCEVConstant>(S) && !Processed.insert(S).second)
    return false; // We have already accounted for this expression.

  // If we can find an existing value for this scev available at the point "At"
  // then consider the expression cheap.
  if (hasRelatedExistingExpansion(S, &At, L))
    return false; // Consider the expression to be free.

  TargetTransformInfo::TargetCostKind CostKind =
      L->getHeader()->getParent()->hasMinSize()
          ? TargetTransformInfo::TCK_CodeSize
          : TargetTransformInfo::TCK_RecipThroughput;

  switch (S->getSCEVType()) {
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  case scUnknown:
  case scVScale:
    // Assume to be zero-cost.
    return false;
  case scConstant: {
    // Only evalulate the costs of constants when optimizing for size.
    if (CostKind != TargetTransformInfo::TCK_CodeSize)
      return false;
    const APInt &Imm = cast<SCEVConstant>(S)->getAPInt();
    Type *Ty = S->getType();
    Cost += TTI.getIntImmCostInst(
        WorkItem.ParentOpcode, WorkItem.OperandIdx, Imm, Ty, CostKind);
    return Cost > Budget;
  }
  case scTruncate:
  case scPtrToInt:
  case scZeroExtend:
  case scSignExtend: {
    Cost +=
        costAndCollectOperands<SCEVCastExpr>(WorkItem, TTI, CostKind, Worklist);
    return false; // Will answer upon next entry into this function.
  }
  case scUDivExpr: {
    // UDivExpr is very likely a UDiv that ScalarEvolution's HowFarToZero or
    // HowManyLessThans produced to compute a precise expression, rather than a
    // UDiv from the user's code. If we can't find a UDiv in the code with some
    // simple searching, we need to account for it's cost.

    // At the beginning of this function we already tried to find existing
    // value for plain 'S'. Now try to lookup 'S + 1' since it is common
    // pattern involving division. This is just a simple search heuristic.
    if (hasRelatedExistingExpansion(
            SE.getAddExpr(S, SE.getConstant(S->getType(), 1)), &At, L))
      return false; // Consider it to be free.

    Cost +=
        costAndCollectOperands<SCEVUDivExpr>(WorkItem, TTI, CostKind, Worklist);
    return false; // Will answer upon next entry into this function.
  }
  case scAddExpr:
  case scMulExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr: {
    assert(cast<SCEVNAryExpr>(S)->getNumOperands() > 1 &&
           "Nary expr should have more than 1 operand.");
    // The simple nary expr will require one less op (or pair of ops)
    // than the number of it's terms.
    Cost +=
        costAndCollectOperands<SCEVNAryExpr>(WorkItem, TTI, CostKind, Worklist);
    return Cost > Budget;
  }
  case scAddRecExpr: {
    assert(cast<SCEVAddRecExpr>(S)->getNumOperands() >= 2 &&
           "Polynomial should be at least linear");
    Cost += costAndCollectOperands<SCEVAddRecExpr>(
        WorkItem, TTI, CostKind, Worklist);
    return Cost > Budget;
  }
  }
  llvm_unreachable("Unknown SCEV kind!");
}

Value *SCEVExpander::expandCodeForPredicate(const SCEVPredicate *Pred,
                                            Instruction *IP) {
  assert(IP);
  switch (Pred->getKind()) {
  case SCEVPredicate::P_Union:
    return expandUnionPredicate(cast<SCEVUnionPredicate>(Pred), IP);
  case SCEVPredicate::P_Compare:
    return expandComparePredicate(cast<SCEVComparePredicate>(Pred), IP);
  case SCEVPredicate::P_Wrap: {
    auto *AddRecPred = cast<SCEVWrapPredicate>(Pred);
    return expandWrapPredicate(AddRecPred, IP);
  }
  }
  llvm_unreachable("Unknown SCEV predicate type");
}

Value *SCEVExpander::expandComparePredicate(const SCEVComparePredicate *Pred,
                                            Instruction *IP) {
  Value *Expr0 = expand(Pred->getLHS(), IP);
  Value *Expr1 = expand(Pred->getRHS(), IP);

  Builder.SetInsertPoint(IP);
  auto InvPred = ICmpInst::getInversePredicate(Pred->getPredicate());
  auto *I = Builder.CreateICmp(InvPred, Expr0, Expr1, "ident.check");
  return I;
}

Value *SCEVExpander::generateOverflowCheck(const SCEVAddRecExpr *AR,
                                           Instruction *Loc, bool Signed) {
  assert(AR->isAffine() && "Cannot generate RT check for "
                           "non-affine expression");

  // FIXME: It is highly suspicious that we're ignoring the predicates here.
  SmallVector<const SCEVPredicate *, 4> Pred;
  const SCEV *ExitCount =
      SE.getPredicatedSymbolicMaxBackedgeTakenCount(AR->getLoop(), Pred);

  assert(!isa<SCEVCouldNotCompute>(ExitCount) && "Invalid loop count");

  const SCEV *Step = AR->getStepRecurrence(SE);
  const SCEV *Start = AR->getStart();

  Type *ARTy = AR->getType();
  unsigned SrcBits = SE.getTypeSizeInBits(ExitCount->getType());
  unsigned DstBits = SE.getTypeSizeInBits(ARTy);

  // The expression {Start,+,Step} has nusw/nssw if
  //   Step < 0, Start - |Step| * Backedge <= Start
  //   Step >= 0, Start + |Step| * Backedge > Start
  // and |Step| * Backedge doesn't unsigned overflow.

  Builder.SetInsertPoint(Loc);
  Value *TripCountVal = expand(ExitCount, Loc);

  IntegerType *Ty =
      IntegerType::get(Loc->getContext(), SE.getTypeSizeInBits(ARTy));

  Value *StepValue = expand(Step, Loc);
  Value *NegStepValue = expand(SE.getNegativeSCEV(Step), Loc);
  Value *StartValue = expand(Start, Loc);

  ConstantInt *Zero =
      ConstantInt::get(Loc->getContext(), APInt::getZero(DstBits));

  Builder.SetInsertPoint(Loc);
  // Compute |Step|
  Value *StepCompare = Builder.CreateICmp(ICmpInst::ICMP_SLT, StepValue, Zero);
  Value *AbsStep = Builder.CreateSelect(StepCompare, NegStepValue, StepValue);

  // Compute |Step| * Backedge
  // Compute:
  //   1. Start + |Step| * Backedge < Start
  //   2. Start - |Step| * Backedge > Start
  //
  // And select either 1. or 2. depending on whether step is positive or
  // negative. If Step is known to be positive or negative, only create
  // either 1. or 2.
  auto ComputeEndCheck = [&]() -> Value * {
    // Checking <u 0 is always false.
    if (!Signed && Start->isZero() && SE.isKnownPositive(Step))
      return ConstantInt::getFalse(Loc->getContext());

    // Get the backedge taken count and truncate or extended to the AR type.
    Value *TruncTripCount = Builder.CreateZExtOrTrunc(TripCountVal, Ty);

    Value *MulV, *OfMul;
    if (Step->isOne()) {
      // Special-case Step of one. Potentially-costly `umul_with_overflow` isn't
      // needed, there is never an overflow, so to avoid artificially inflating
      // the cost of the check, directly emit the optimized IR.
      MulV = TruncTripCount;
      OfMul = ConstantInt::getFalse(MulV->getContext());
    } else {
      auto *MulF = Intrinsic::getDeclaration(Loc->getModule(),
                                             Intrinsic::umul_with_overflow, Ty);
      CallInst *Mul =
          Builder.CreateCall(MulF, {AbsStep, TruncTripCount}, "mul");
      MulV = Builder.CreateExtractValue(Mul, 0, "mul.result");
      OfMul = Builder.CreateExtractValue(Mul, 1, "mul.overflow");
    }

    Value *Add = nullptr, *Sub = nullptr;
    bool NeedPosCheck = !SE.isKnownNegative(Step);
    bool NeedNegCheck = !SE.isKnownPositive(Step);

    if (isa<PointerType>(ARTy)) {
      Value *NegMulV = Builder.CreateNeg(MulV);
      if (NeedPosCheck)
        Add = Builder.CreatePtrAdd(StartValue, MulV);
      if (NeedNegCheck)
        Sub = Builder.CreatePtrAdd(StartValue, NegMulV);
    } else {
      if (NeedPosCheck)
        Add = Builder.CreateAdd(StartValue, MulV);
      if (NeedNegCheck)
        Sub = Builder.CreateSub(StartValue, MulV);
    }

    Value *EndCompareLT = nullptr;
    Value *EndCompareGT = nullptr;
    Value *EndCheck = nullptr;
    if (NeedPosCheck)
      EndCheck = EndCompareLT = Builder.CreateICmp(
          Signed ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT, Add, StartValue);
    if (NeedNegCheck)
      EndCheck = EndCompareGT = Builder.CreateICmp(
          Signed ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT, Sub, StartValue);
    if (NeedPosCheck && NeedNegCheck) {
      // Select the answer based on the sign of Step.
      EndCheck = Builder.CreateSelect(StepCompare, EndCompareGT, EndCompareLT);
    }
    return Builder.CreateOr(EndCheck, OfMul);
  };
  Value *EndCheck = ComputeEndCheck();

  // If the backedge taken count type is larger than the AR type,
  // check that we don't drop any bits by truncating it. If we are
  // dropping bits, then we have overflow (unless the step is zero).
  if (SrcBits > DstBits) {
    auto MaxVal = APInt::getMaxValue(DstBits).zext(SrcBits);
    auto *BackedgeCheck =
        Builder.CreateICmp(ICmpInst::ICMP_UGT, TripCountVal,
                           ConstantInt::get(Loc->getContext(), MaxVal));
    BackedgeCheck = Builder.CreateAnd(
        BackedgeCheck, Builder.CreateICmp(ICmpInst::ICMP_NE, StepValue, Zero));

    EndCheck = Builder.CreateOr(EndCheck, BackedgeCheck);
  }

  return EndCheck;
}

Value *SCEVExpander::expandWrapPredicate(const SCEVWrapPredicate *Pred,
                                         Instruction *IP) {
  const auto *A = cast<SCEVAddRecExpr>(Pred->getExpr());
  Value *NSSWCheck = nullptr, *NUSWCheck = nullptr;

  // Add a check for NUSW
  if (Pred->getFlags() & SCEVWrapPredicate::IncrementNUSW)
    NUSWCheck = generateOverflowCheck(A, IP, false);

  // Add a check for NSSW
  if (Pred->getFlags() & SCEVWrapPredicate::IncrementNSSW)
    NSSWCheck = generateOverflowCheck(A, IP, true);

  if (NUSWCheck && NSSWCheck)
    return Builder.CreateOr(NUSWCheck, NSSWCheck);

  if (NUSWCheck)
    return NUSWCheck;

  if (NSSWCheck)
    return NSSWCheck;

  return ConstantInt::getFalse(IP->getContext());
}

Value *SCEVExpander::expandUnionPredicate(const SCEVUnionPredicate *Union,
                                          Instruction *IP) {
  // Loop over all checks in this set.
  SmallVector<Value *> Checks;
  for (const auto *Pred : Union->getPredicates()) {
    Checks.push_back(expandCodeForPredicate(Pred, IP));
    Builder.SetInsertPoint(IP);
  }

  if (Checks.empty())
    return ConstantInt::getFalse(IP->getContext());
  return Builder.CreateOr(Checks);
}

Value *SCEVExpander::fixupLCSSAFormFor(Value *V) {
  auto *DefI = dyn_cast<Instruction>(V);
  if (!PreserveLCSSA || !DefI)
    return V;

  BasicBlock::iterator InsertPt = Builder.GetInsertPoint();
  Loop *DefLoop = SE.LI.getLoopFor(DefI->getParent());
  Loop *UseLoop = SE.LI.getLoopFor(InsertPt->getParent());
  if (!DefLoop || UseLoop == DefLoop || DefLoop->contains(UseLoop))
    return V;

  // Create a temporary instruction to at the current insertion point, so we
  // can hand it off to the helper to create LCSSA PHIs if required for the
  // new use.
  // FIXME: Ideally formLCSSAForInstructions (used in fixupLCSSAFormFor)
  // would accept a insertion point and return an LCSSA phi for that
  // insertion point, so there is no need to insert & remove the temporary
  // instruction.
  Type *ToTy;
  if (DefI->getType()->isIntegerTy())
    ToTy = PointerType::get(DefI->getContext(), 0);
  else
    ToTy = Type::getInt32Ty(DefI->getContext());
  Instruction *User =
      CastInst::CreateBitOrPointerCast(DefI, ToTy, "tmp.lcssa.user", InsertPt);
  auto RemoveUserOnExit =
      make_scope_exit([User]() { User->eraseFromParent(); });

  SmallVector<Instruction *, 1> ToUpdate;
  ToUpdate.push_back(DefI);
  SmallVector<PHINode *, 16> PHIsToRemove;
  SmallVector<PHINode *, 16> InsertedPHIs;
  formLCSSAForInstructions(ToUpdate, SE.DT, SE.LI, &SE, &PHIsToRemove,
                           &InsertedPHIs);
  for (PHINode *PN : InsertedPHIs)
    rememberInstruction(PN);
  for (PHINode *PN : PHIsToRemove) {
    if (!PN->use_empty())
      continue;
    InsertedValues.erase(PN);
    InsertedPostIncValues.erase(PN);
    PN->eraseFromParent();
  }

  return User->getOperand(0);
}

namespace {
// Search for a SCEV subexpression that is not safe to expand.  Any expression
// that may expand to a !isSafeToSpeculativelyExecute value is unsafe, namely
// UDiv expressions. We don't know if the UDiv is derived from an IR divide
// instruction, but the important thing is that we prove the denominator is
// nonzero before expansion.
//
// IVUsers already checks that IV-derived expressions are safe. So this check is
// only needed when the expression includes some subexpression that is not IV
// derived.
//
// Currently, we only allow division by a value provably non-zero here.
//
// We cannot generally expand recurrences unless the step dominates the loop
// header. The expander handles the special case of affine recurrences by
// scaling the recurrence outside the loop, but this technique isn't generally
// applicable. Expanding a nested recurrence outside a loop requires computing
// binomial coefficients. This could be done, but the recurrence has to be in a
// perfectly reduced form, which can't be guaranteed.
struct SCEVFindUnsafe {
  ScalarEvolution &SE;
  bool CanonicalMode;
  bool IsUnsafe = false;

  SCEVFindUnsafe(ScalarEvolution &SE, bool CanonicalMode)
      : SE(SE), CanonicalMode(CanonicalMode) {}

  bool follow(const SCEV *S) {
    if (const SCEVUDivExpr *D = dyn_cast<SCEVUDivExpr>(S)) {
      if (!SE.isKnownNonZero(D->getRHS())) {
        IsUnsafe = true;
        return false;
      }
    }
    if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
      // For non-affine addrecs or in non-canonical mode we need a preheader
      // to insert into.
      if (!AR->getLoop()->getLoopPreheader() &&
          (!CanonicalMode || !AR->isAffine())) {
        IsUnsafe = true;
        return false;
      }
    }
    return true;
  }
  bool isDone() const { return IsUnsafe; }
};
} // namespace

bool SCEVExpander::isSafeToExpand(const SCEV *S) const {
  SCEVFindUnsafe Search(SE, CanonicalMode);
  visitAll(S, Search);
  return !Search.IsUnsafe;
}

bool SCEVExpander::isSafeToExpandAt(const SCEV *S,
                                    const Instruction *InsertionPoint) const {
  if (!isSafeToExpand(S))
    return false;
  // We have to prove that the expanded site of S dominates InsertionPoint.
  // This is easy when not in the same block, but hard when S is an instruction
  // to be expanded somewhere inside the same block as our insertion point.
  // What we really need here is something analogous to an OrderedBasicBlock,
  // but for the moment, we paper over the problem by handling two common and
  // cheap to check cases.
  if (SE.properlyDominates(S, InsertionPoint->getParent()))
    return true;
  if (SE.dominates(S, InsertionPoint->getParent())) {
    if (InsertionPoint->getParent()->getTerminator() == InsertionPoint)
      return true;
    if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S))
      if (llvm::is_contained(InsertionPoint->operand_values(), U->getValue()))
        return true;
  }
  return false;
}

void SCEVExpanderCleaner::cleanup() {
  // Result is used, nothing to remove.
  if (ResultUsed)
    return;

  // Restore original poison flags.
  for (auto [I, Flags] : Expander.OrigFlags)
    Flags.apply(I);

  auto InsertedInstructions = Expander.getAllInsertedInstructions();
#ifndef NDEBUG
  SmallPtrSet<Instruction *, 8> InsertedSet(InsertedInstructions.begin(),
                                            InsertedInstructions.end());
  (void)InsertedSet;
#endif
  // Remove sets with value handles.
  Expander.clear();

  // Remove all inserted instructions.
  for (Instruction *I : reverse(InsertedInstructions)) {
#ifndef NDEBUG
    assert(all_of(I->users(),
                  [&InsertedSet](Value *U) {
                    return InsertedSet.contains(cast<Instruction>(U));
                  }) &&
           "removed instruction should only be used by instructions inserted "
           "during expansion");
#endif
    assert(!I->getType()->isVoidTy() &&
           "inserted instruction should have non-void types");
    I->replaceAllUsesWith(PoisonValue::get(I->getType()));
    I->eraseFromParent();
  }
}
