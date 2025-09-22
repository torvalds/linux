//===- ValueTracking.cpp - Walk computations to compute properties --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help analyze properties that chains of
// computations have.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/DomConditionCache.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/Analysis/WithCache.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsRISCV.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/RISCVTargetParser.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

using namespace llvm;
using namespace llvm::PatternMatch;

// Controls the number of uses of the value searched for possible
// dominating comparisons.
static cl::opt<unsigned> DomConditionsMaxUses("dom-conditions-max-uses",
                                              cl::Hidden, cl::init(20));


/// Returns the bitwidth of the given scalar or pointer type. For vector types,
/// returns the element type's bitwidth.
static unsigned getBitWidth(Type *Ty, const DataLayout &DL) {
  if (unsigned BitWidth = Ty->getScalarSizeInBits())
    return BitWidth;

  return DL.getPointerTypeSizeInBits(Ty);
}

// Given the provided Value and, potentially, a context instruction, return
// the preferred context instruction (if any).
static const Instruction *safeCxtI(const Value *V, const Instruction *CxtI) {
  // If we've been provided with a context instruction, then use that (provided
  // it has been inserted).
  if (CxtI && CxtI->getParent())
    return CxtI;

  // If the value is really an already-inserted instruction, then use that.
  CxtI = dyn_cast<Instruction>(V);
  if (CxtI && CxtI->getParent())
    return CxtI;

  return nullptr;
}

static const Instruction *safeCxtI(const Value *V1, const Value *V2, const Instruction *CxtI) {
  // If we've been provided with a context instruction, then use that (provided
  // it has been inserted).
  if (CxtI && CxtI->getParent())
    return CxtI;

  // If the value is really an already-inserted instruction, then use that.
  CxtI = dyn_cast<Instruction>(V1);
  if (CxtI && CxtI->getParent())
    return CxtI;

  CxtI = dyn_cast<Instruction>(V2);
  if (CxtI && CxtI->getParent())
    return CxtI;

  return nullptr;
}

static bool getShuffleDemandedElts(const ShuffleVectorInst *Shuf,
                                   const APInt &DemandedElts,
                                   APInt &DemandedLHS, APInt &DemandedRHS) {
  if (isa<ScalableVectorType>(Shuf->getType())) {
    assert(DemandedElts == APInt(1,1));
    DemandedLHS = DemandedRHS = DemandedElts;
    return true;
  }

  int NumElts =
      cast<FixedVectorType>(Shuf->getOperand(0)->getType())->getNumElements();
  return llvm::getShuffleDemandedElts(NumElts, Shuf->getShuffleMask(),
                                      DemandedElts, DemandedLHS, DemandedRHS);
}

static void computeKnownBits(const Value *V, const APInt &DemandedElts,
                             KnownBits &Known, unsigned Depth,
                             const SimplifyQuery &Q);

void llvm::computeKnownBits(const Value *V, KnownBits &Known, unsigned Depth,
                            const SimplifyQuery &Q) {
  // Since the number of lanes in a scalable vector is unknown at compile time,
  // we track one bit which is implicitly broadcast to all lanes.  This means
  // that all lanes in a scalable vector are considered demanded.
  auto *FVTy = dyn_cast<FixedVectorType>(V->getType());
  APInt DemandedElts =
      FVTy ? APInt::getAllOnes(FVTy->getNumElements()) : APInt(1, 1);
  ::computeKnownBits(V, DemandedElts, Known, Depth, Q);
}

void llvm::computeKnownBits(const Value *V, KnownBits &Known,
                            const DataLayout &DL, unsigned Depth,
                            AssumptionCache *AC, const Instruction *CxtI,
                            const DominatorTree *DT, bool UseInstrInfo) {
  computeKnownBits(
      V, Known, Depth,
      SimplifyQuery(DL, DT, AC, safeCxtI(V, CxtI), UseInstrInfo));
}

KnownBits llvm::computeKnownBits(const Value *V, const DataLayout &DL,
                                 unsigned Depth, AssumptionCache *AC,
                                 const Instruction *CxtI,
                                 const DominatorTree *DT, bool UseInstrInfo) {
  return computeKnownBits(
      V, Depth, SimplifyQuery(DL, DT, AC, safeCxtI(V, CxtI), UseInstrInfo));
}

KnownBits llvm::computeKnownBits(const Value *V, const APInt &DemandedElts,
                                 const DataLayout &DL, unsigned Depth,
                                 AssumptionCache *AC, const Instruction *CxtI,
                                 const DominatorTree *DT, bool UseInstrInfo) {
  return computeKnownBits(
      V, DemandedElts, Depth,
      SimplifyQuery(DL, DT, AC, safeCxtI(V, CxtI), UseInstrInfo));
}

static bool haveNoCommonBitsSetSpecialCases(const Value *LHS, const Value *RHS,
                                            const SimplifyQuery &SQ) {
  // Look for an inverted mask: (X & ~M) op (Y & M).
  {
    Value *M;
    if (match(LHS, m_c_And(m_Not(m_Value(M)), m_Value())) &&
        match(RHS, m_c_And(m_Specific(M), m_Value())) &&
        isGuaranteedNotToBeUndef(M, SQ.AC, SQ.CxtI, SQ.DT))
      return true;
  }

  // X op (Y & ~X)
  if (match(RHS, m_c_And(m_Not(m_Specific(LHS)), m_Value())) &&
      isGuaranteedNotToBeUndef(LHS, SQ.AC, SQ.CxtI, SQ.DT))
    return true;

  // X op ((X & Y) ^ Y) -- this is the canonical form of the previous pattern
  // for constant Y.
  Value *Y;
  if (match(RHS,
            m_c_Xor(m_c_And(m_Specific(LHS), m_Value(Y)), m_Deferred(Y))) &&
      isGuaranteedNotToBeUndef(LHS, SQ.AC, SQ.CxtI, SQ.DT) &&
      isGuaranteedNotToBeUndef(Y, SQ.AC, SQ.CxtI, SQ.DT))
    return true;

  // Peek through extends to find a 'not' of the other side:
  // (ext Y) op ext(~Y)
  if (match(LHS, m_ZExtOrSExt(m_Value(Y))) &&
      match(RHS, m_ZExtOrSExt(m_Not(m_Specific(Y)))) &&
      isGuaranteedNotToBeUndef(Y, SQ.AC, SQ.CxtI, SQ.DT))
    return true;

  // Look for: (A & B) op ~(A | B)
  {
    Value *A, *B;
    if (match(LHS, m_And(m_Value(A), m_Value(B))) &&
        match(RHS, m_Not(m_c_Or(m_Specific(A), m_Specific(B)))) &&
        isGuaranteedNotToBeUndef(A, SQ.AC, SQ.CxtI, SQ.DT) &&
        isGuaranteedNotToBeUndef(B, SQ.AC, SQ.CxtI, SQ.DT))
      return true;
  }

  return false;
}

bool llvm::haveNoCommonBitsSet(const WithCache<const Value *> &LHSCache,
                               const WithCache<const Value *> &RHSCache,
                               const SimplifyQuery &SQ) {
  const Value *LHS = LHSCache.getValue();
  const Value *RHS = RHSCache.getValue();

  assert(LHS->getType() == RHS->getType() &&
         "LHS and RHS should have the same type");
  assert(LHS->getType()->isIntOrIntVectorTy() &&
         "LHS and RHS should be integers");

  if (haveNoCommonBitsSetSpecialCases(LHS, RHS, SQ) ||
      haveNoCommonBitsSetSpecialCases(RHS, LHS, SQ))
    return true;

  return KnownBits::haveNoCommonBitsSet(LHSCache.getKnownBits(SQ),
                                        RHSCache.getKnownBits(SQ));
}

bool llvm::isOnlyUsedInZeroComparison(const Instruction *I) {
  return !I->user_empty() && all_of(I->users(), [](const User *U) {
    ICmpInst::Predicate P;
    return match(U, m_ICmp(P, m_Value(), m_Zero()));
  });
}

bool llvm::isOnlyUsedInZeroEqualityComparison(const Instruction *I) {
  return !I->user_empty() && all_of(I->users(), [](const User *U) {
    ICmpInst::Predicate P;
    return match(U, m_ICmp(P, m_Value(), m_Zero())) && ICmpInst::isEquality(P);
  });
}

static bool isKnownToBeAPowerOfTwo(const Value *V, bool OrZero, unsigned Depth,
                                   const SimplifyQuery &Q);

bool llvm::isKnownToBeAPowerOfTwo(const Value *V, const DataLayout &DL,
                                  bool OrZero, unsigned Depth,
                                  AssumptionCache *AC, const Instruction *CxtI,
                                  const DominatorTree *DT, bool UseInstrInfo) {
  return ::isKnownToBeAPowerOfTwo(
      V, OrZero, Depth,
      SimplifyQuery(DL, DT, AC, safeCxtI(V, CxtI), UseInstrInfo));
}

static bool isKnownNonZero(const Value *V, const APInt &DemandedElts,
                           const SimplifyQuery &Q, unsigned Depth);

bool llvm::isKnownNonNegative(const Value *V, const SimplifyQuery &SQ,
                              unsigned Depth) {
  return computeKnownBits(V, Depth, SQ).isNonNegative();
}

bool llvm::isKnownPositive(const Value *V, const SimplifyQuery &SQ,
                           unsigned Depth) {
  if (auto *CI = dyn_cast<ConstantInt>(V))
    return CI->getValue().isStrictlyPositive();

  // If `isKnownNonNegative` ever becomes more sophisticated, make sure to keep
  // this updated.
  KnownBits Known = computeKnownBits(V, Depth, SQ);
  return Known.isNonNegative() &&
         (Known.isNonZero() || isKnownNonZero(V, SQ, Depth));
}

bool llvm::isKnownNegative(const Value *V, const SimplifyQuery &SQ,
                           unsigned Depth) {
  return computeKnownBits(V, Depth, SQ).isNegative();
}

static bool isKnownNonEqual(const Value *V1, const Value *V2,
                            const APInt &DemandedElts, unsigned Depth,
                            const SimplifyQuery &Q);

bool llvm::isKnownNonEqual(const Value *V1, const Value *V2,
                           const DataLayout &DL, AssumptionCache *AC,
                           const Instruction *CxtI, const DominatorTree *DT,
                           bool UseInstrInfo) {
  assert(V1->getType() == V2->getType() &&
         "Testing equality of non-equal types!");
  auto *FVTy = dyn_cast<FixedVectorType>(V1->getType());
  APInt DemandedElts =
      FVTy ? APInt::getAllOnes(FVTy->getNumElements()) : APInt(1, 1);
  return ::isKnownNonEqual(
      V1, V2, DemandedElts, 0,
      SimplifyQuery(DL, DT, AC, safeCxtI(V2, V1, CxtI), UseInstrInfo));
}

bool llvm::MaskedValueIsZero(const Value *V, const APInt &Mask,
                             const SimplifyQuery &SQ, unsigned Depth) {
  KnownBits Known(Mask.getBitWidth());
  computeKnownBits(V, Known, Depth, SQ);
  return Mask.isSubsetOf(Known.Zero);
}

static unsigned ComputeNumSignBits(const Value *V, const APInt &DemandedElts,
                                   unsigned Depth, const SimplifyQuery &Q);

static unsigned ComputeNumSignBits(const Value *V, unsigned Depth,
                                   const SimplifyQuery &Q) {
  auto *FVTy = dyn_cast<FixedVectorType>(V->getType());
  APInt DemandedElts =
      FVTy ? APInt::getAllOnes(FVTy->getNumElements()) : APInt(1, 1);
  return ComputeNumSignBits(V, DemandedElts, Depth, Q);
}

unsigned llvm::ComputeNumSignBits(const Value *V, const DataLayout &DL,
                                  unsigned Depth, AssumptionCache *AC,
                                  const Instruction *CxtI,
                                  const DominatorTree *DT, bool UseInstrInfo) {
  return ::ComputeNumSignBits(
      V, Depth, SimplifyQuery(DL, DT, AC, safeCxtI(V, CxtI), UseInstrInfo));
}

unsigned llvm::ComputeMaxSignificantBits(const Value *V, const DataLayout &DL,
                                         unsigned Depth, AssumptionCache *AC,
                                         const Instruction *CxtI,
                                         const DominatorTree *DT) {
  unsigned SignBits = ComputeNumSignBits(V, DL, Depth, AC, CxtI, DT);
  return V->getType()->getScalarSizeInBits() - SignBits + 1;
}

static void computeKnownBitsAddSub(bool Add, const Value *Op0, const Value *Op1,
                                   bool NSW, bool NUW,
                                   const APInt &DemandedElts,
                                   KnownBits &KnownOut, KnownBits &Known2,
                                   unsigned Depth, const SimplifyQuery &Q) {
  computeKnownBits(Op1, DemandedElts, KnownOut, Depth + 1, Q);

  // If one operand is unknown and we have no nowrap information,
  // the result will be unknown independently of the second operand.
  if (KnownOut.isUnknown() && !NSW && !NUW)
    return;

  computeKnownBits(Op0, DemandedElts, Known2, Depth + 1, Q);
  KnownOut = KnownBits::computeForAddSub(Add, NSW, NUW, Known2, KnownOut);
}

static void computeKnownBitsMul(const Value *Op0, const Value *Op1, bool NSW,
                                const APInt &DemandedElts, KnownBits &Known,
                                KnownBits &Known2, unsigned Depth,
                                const SimplifyQuery &Q) {
  computeKnownBits(Op1, DemandedElts, Known, Depth + 1, Q);
  computeKnownBits(Op0, DemandedElts, Known2, Depth + 1, Q);

  bool isKnownNegative = false;
  bool isKnownNonNegative = false;
  // If the multiplication is known not to overflow, compute the sign bit.
  if (NSW) {
    if (Op0 == Op1) {
      // The product of a number with itself is non-negative.
      isKnownNonNegative = true;
    } else {
      bool isKnownNonNegativeOp1 = Known.isNonNegative();
      bool isKnownNonNegativeOp0 = Known2.isNonNegative();
      bool isKnownNegativeOp1 = Known.isNegative();
      bool isKnownNegativeOp0 = Known2.isNegative();
      // The product of two numbers with the same sign is non-negative.
      isKnownNonNegative = (isKnownNegativeOp1 && isKnownNegativeOp0) ||
                           (isKnownNonNegativeOp1 && isKnownNonNegativeOp0);
      // The product of a negative number and a non-negative number is either
      // negative or zero.
      if (!isKnownNonNegative)
        isKnownNegative =
            (isKnownNegativeOp1 && isKnownNonNegativeOp0 &&
             Known2.isNonZero()) ||
            (isKnownNegativeOp0 && isKnownNonNegativeOp1 && Known.isNonZero());
    }
  }

  bool SelfMultiply = Op0 == Op1;
  if (SelfMultiply)
    SelfMultiply &=
        isGuaranteedNotToBeUndef(Op0, Q.AC, Q.CxtI, Q.DT, Depth + 1);
  Known = KnownBits::mul(Known, Known2, SelfMultiply);

  // Only make use of no-wrap flags if we failed to compute the sign bit
  // directly.  This matters if the multiplication always overflows, in
  // which case we prefer to follow the result of the direct computation,
  // though as the program is invoking undefined behaviour we can choose
  // whatever we like here.
  if (isKnownNonNegative && !Known.isNegative())
    Known.makeNonNegative();
  else if (isKnownNegative && !Known.isNonNegative())
    Known.makeNegative();
}

void llvm::computeKnownBitsFromRangeMetadata(const MDNode &Ranges,
                                             KnownBits &Known) {
  unsigned BitWidth = Known.getBitWidth();
  unsigned NumRanges = Ranges.getNumOperands() / 2;
  assert(NumRanges >= 1);

  Known.Zero.setAllBits();
  Known.One.setAllBits();

  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Lower =
        mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 0));
    ConstantInt *Upper =
        mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 1));
    ConstantRange Range(Lower->getValue(), Upper->getValue());

    // The first CommonPrefixBits of all values in Range are equal.
    unsigned CommonPrefixBits =
        (Range.getUnsignedMax() ^ Range.getUnsignedMin()).countl_zero();
    APInt Mask = APInt::getHighBitsSet(BitWidth, CommonPrefixBits);
    APInt UnsignedMax = Range.getUnsignedMax().zextOrTrunc(BitWidth);
    Known.One &= UnsignedMax & Mask;
    Known.Zero &= ~UnsignedMax & Mask;
  }
}

static bool isEphemeralValueOf(const Instruction *I, const Value *E) {
  SmallVector<const Value *, 16> WorkSet(1, I);
  SmallPtrSet<const Value *, 32> Visited;
  SmallPtrSet<const Value *, 16> EphValues;

  // The instruction defining an assumption's condition itself is always
  // considered ephemeral to that assumption (even if it has other
  // non-ephemeral users). See r246696's test case for an example.
  if (is_contained(I->operands(), E))
    return true;

  while (!WorkSet.empty()) {
    const Value *V = WorkSet.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    // If all uses of this value are ephemeral, then so is this value.
    if (llvm::all_of(V->users(), [&](const User *U) {
                                   return EphValues.count(U);
                                 })) {
      if (V == E)
        return true;

      if (V == I || (isa<Instruction>(V) &&
                     !cast<Instruction>(V)->mayHaveSideEffects() &&
                     !cast<Instruction>(V)->isTerminator())) {
       EphValues.insert(V);
       if (const User *U = dyn_cast<User>(V))
         append_range(WorkSet, U->operands());
      }
    }
  }

  return false;
}

// Is this an intrinsic that cannot be speculated but also cannot trap?
bool llvm::isAssumeLikeIntrinsic(const Instruction *I) {
  if (const IntrinsicInst *CI = dyn_cast<IntrinsicInst>(I))
    return CI->isAssumeLikeIntrinsic();

  return false;
}

bool llvm::isValidAssumeForContext(const Instruction *Inv,
                                   const Instruction *CxtI,
                                   const DominatorTree *DT,
                                   bool AllowEphemerals) {
  // There are two restrictions on the use of an assume:
  //  1. The assume must dominate the context (or the control flow must
  //     reach the assume whenever it reaches the context).
  //  2. The context must not be in the assume's set of ephemeral values
  //     (otherwise we will use the assume to prove that the condition
  //     feeding the assume is trivially true, thus causing the removal of
  //     the assume).

  if (Inv->getParent() == CxtI->getParent()) {
    // If Inv and CtxI are in the same block, check if the assume (Inv) is first
    // in the BB.
    if (Inv->comesBefore(CxtI))
      return true;

    // Don't let an assume affect itself - this would cause the problems
    // `isEphemeralValueOf` is trying to prevent, and it would also make
    // the loop below go out of bounds.
    if (!AllowEphemerals && Inv == CxtI)
      return false;

    // The context comes first, but they're both in the same block.
    // Make sure there is nothing in between that might interrupt
    // the control flow, not even CxtI itself.
    // We limit the scan distance between the assume and its context instruction
    // to avoid a compile-time explosion. This limit is chosen arbitrarily, so
    // it can be adjusted if needed (could be turned into a cl::opt).
    auto Range = make_range(CxtI->getIterator(), Inv->getIterator());
    if (!isGuaranteedToTransferExecutionToSuccessor(Range, 15))
      return false;

    return AllowEphemerals || !isEphemeralValueOf(Inv, CxtI);
  }

  // Inv and CxtI are in different blocks.
  if (DT) {
    if (DT->dominates(Inv, CxtI))
      return true;
  } else if (Inv->getParent() == CxtI->getParent()->getSinglePredecessor()) {
    // We don't have a DT, but this trivially dominates.
    return true;
  }

  return false;
}

// TODO: cmpExcludesZero misses many cases where `RHS` is non-constant but
// we still have enough information about `RHS` to conclude non-zero. For
// example Pred=EQ, RHS=isKnownNonZero. cmpExcludesZero is called in loops
// so the extra compile time may not be worth it, but possibly a second API
// should be created for use outside of loops.
static bool cmpExcludesZero(CmpInst::Predicate Pred, const Value *RHS) {
  // v u> y implies v != 0.
  if (Pred == ICmpInst::ICMP_UGT)
    return true;

  // Special-case v != 0 to also handle v != null.
  if (Pred == ICmpInst::ICMP_NE)
    return match(RHS, m_Zero());

  // All other predicates - rely on generic ConstantRange handling.
  const APInt *C;
  auto Zero = APInt::getZero(RHS->getType()->getScalarSizeInBits());
  if (match(RHS, m_APInt(C))) {
    ConstantRange TrueValues = ConstantRange::makeExactICmpRegion(Pred, *C);
    return !TrueValues.contains(Zero);
  }

  auto *VC = dyn_cast<ConstantDataVector>(RHS);
  if (VC == nullptr)
    return false;

  for (unsigned ElemIdx = 0, NElem = VC->getNumElements(); ElemIdx < NElem;
       ++ElemIdx) {
    ConstantRange TrueValues = ConstantRange::makeExactICmpRegion(
        Pred, VC->getElementAsAPInt(ElemIdx));
    if (TrueValues.contains(Zero))
      return false;
  }
  return true;
}

static bool isKnownNonZeroFromAssume(const Value *V, const SimplifyQuery &Q) {
  // Use of assumptions is context-sensitive. If we don't have a context, we
  // cannot use them!
  if (!Q.AC || !Q.CxtI)
    return false;

  for (AssumptionCache::ResultElem &Elem : Q.AC->assumptionsFor(V)) {
    if (!Elem.Assume)
      continue;

    AssumeInst *I = cast<AssumeInst>(Elem.Assume);
    assert(I->getFunction() == Q.CxtI->getFunction() &&
           "Got assumption for the wrong function!");

    if (Elem.Index != AssumptionCache::ExprResultIdx) {
      if (!V->getType()->isPointerTy())
        continue;
      if (RetainedKnowledge RK = getKnowledgeFromBundle(
              *I, I->bundle_op_info_begin()[Elem.Index])) {
        if (RK.WasOn == V &&
            (RK.AttrKind == Attribute::NonNull ||
             (RK.AttrKind == Attribute::Dereferenceable &&
              !NullPointerIsDefined(Q.CxtI->getFunction(),
                                    V->getType()->getPointerAddressSpace()))) &&
            isValidAssumeForContext(I, Q.CxtI, Q.DT))
          return true;
      }
      continue;
    }

    // Warning: This loop can end up being somewhat performance sensitive.
    // We're running this loop for once for each value queried resulting in a
    // runtime of ~O(#assumes * #values).

    Value *RHS;
    CmpInst::Predicate Pred;
    auto m_V = m_CombineOr(m_Specific(V), m_PtrToInt(m_Specific(V)));
    if (!match(I->getArgOperand(0), m_c_ICmp(Pred, m_V, m_Value(RHS))))
      return false;

    if (cmpExcludesZero(Pred, RHS) && isValidAssumeForContext(I, Q.CxtI, Q.DT))
      return true;
  }

  return false;
}

static void computeKnownBitsFromCmp(const Value *V, CmpInst::Predicate Pred,
                                    Value *LHS, Value *RHS, KnownBits &Known,
                                    const SimplifyQuery &Q) {
  if (RHS->getType()->isPointerTy()) {
    // Handle comparison of pointer to null explicitly, as it will not be
    // covered by the m_APInt() logic below.
    if (LHS == V && match(RHS, m_Zero())) {
      switch (Pred) {
      case ICmpInst::ICMP_EQ:
        Known.setAllZero();
        break;
      case ICmpInst::ICMP_SGE:
      case ICmpInst::ICMP_SGT:
        Known.makeNonNegative();
        break;
      case ICmpInst::ICMP_SLT:
        Known.makeNegative();
        break;
      default:
        break;
      }
    }
    return;
  }

  unsigned BitWidth = Known.getBitWidth();
  auto m_V =
      m_CombineOr(m_Specific(V), m_PtrToIntSameSize(Q.DL, m_Specific(V)));

  Value *Y;
  const APInt *Mask, *C;
  uint64_t ShAmt;
  switch (Pred) {
  case ICmpInst::ICMP_EQ:
    // assume(V = C)
    if (match(LHS, m_V) && match(RHS, m_APInt(C))) {
      Known = Known.unionWith(KnownBits::makeConstant(*C));
      // assume(V & Mask = C)
    } else if (match(LHS, m_c_And(m_V, m_Value(Y))) &&
               match(RHS, m_APInt(C))) {
      // For one bits in Mask, we can propagate bits from C to V.
      Known.One |= *C;
      if (match(Y, m_APInt(Mask)))
        Known.Zero |= ~*C & *Mask;
      // assume(V | Mask = C)
    } else if (match(LHS, m_c_Or(m_V, m_Value(Y))) && match(RHS, m_APInt(C))) {
      // For zero bits in Mask, we can propagate bits from C to V.
      Known.Zero |= ~*C;
      if (match(Y, m_APInt(Mask)))
        Known.One |= *C & ~*Mask;
      // assume(V ^ Mask = C)
    } else if (match(LHS, m_Xor(m_V, m_APInt(Mask))) &&
               match(RHS, m_APInt(C))) {
      // Equivalent to assume(V == Mask ^ C)
      Known = Known.unionWith(KnownBits::makeConstant(*C ^ *Mask));
      // assume(V << ShAmt = C)
    } else if (match(LHS, m_Shl(m_V, m_ConstantInt(ShAmt))) &&
               match(RHS, m_APInt(C)) && ShAmt < BitWidth) {
      // For those bits in C that are known, we can propagate them to known
      // bits in V shifted to the right by ShAmt.
      KnownBits RHSKnown = KnownBits::makeConstant(*C);
      RHSKnown.Zero.lshrInPlace(ShAmt);
      RHSKnown.One.lshrInPlace(ShAmt);
      Known = Known.unionWith(RHSKnown);
      // assume(V >> ShAmt = C)
    } else if (match(LHS, m_Shr(m_V, m_ConstantInt(ShAmt))) &&
               match(RHS, m_APInt(C)) && ShAmt < BitWidth) {
      KnownBits RHSKnown = KnownBits::makeConstant(*C);
      // For those bits in RHS that are known, we can propagate them to known
      // bits in V shifted to the right by C.
      Known.Zero |= RHSKnown.Zero << ShAmt;
      Known.One |= RHSKnown.One << ShAmt;
    }
    break;
  case ICmpInst::ICMP_NE: {
    // assume (V & B != 0) where B is a power of 2
    const APInt *BPow2;
    if (match(LHS, m_And(m_V, m_Power2(BPow2))) && match(RHS, m_Zero()))
      Known.One |= *BPow2;
    break;
  }
  default:
    if (match(RHS, m_APInt(C))) {
      const APInt *Offset = nullptr;
      if (match(LHS, m_CombineOr(m_V, m_AddLike(m_V, m_APInt(Offset))))) {
        ConstantRange LHSRange = ConstantRange::makeAllowedICmpRegion(Pred, *C);
        if (Offset)
          LHSRange = LHSRange.sub(*Offset);
        Known = Known.unionWith(LHSRange.toKnownBits());
      }
      if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE) {
        // X & Y u> C     -> X u> C && Y u> C
        // X nuw- Y u> C  -> X u> C
        if (match(LHS, m_c_And(m_V, m_Value())) ||
            match(LHS, m_NUWSub(m_V, m_Value())))
          Known.One.setHighBits(
              (*C + (Pred == ICmpInst::ICMP_UGT)).countLeadingOnes());
      }
      if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE) {
        // X | Y u< C    -> X u< C && Y u< C
        // X nuw+ Y u< C -> X u< C && Y u< C
        if (match(LHS, m_c_Or(m_V, m_Value())) ||
            match(LHS, m_c_NUWAdd(m_V, m_Value()))) {
          Known.Zero.setHighBits(
              (*C - (Pred == ICmpInst::ICMP_ULT)).countLeadingZeros());
        }
      }
    }
    break;
  }
}

static void computeKnownBitsFromICmpCond(const Value *V, ICmpInst *Cmp,
                                         KnownBits &Known,
                                         const SimplifyQuery &SQ, bool Invert) {
  ICmpInst::Predicate Pred =
      Invert ? Cmp->getInversePredicate() : Cmp->getPredicate();
  Value *LHS = Cmp->getOperand(0);
  Value *RHS = Cmp->getOperand(1);

  // Handle icmp pred (trunc V), C
  if (match(LHS, m_Trunc(m_Specific(V)))) {
    KnownBits DstKnown(LHS->getType()->getScalarSizeInBits());
    computeKnownBitsFromCmp(LHS, Pred, LHS, RHS, DstKnown, SQ);
    Known = Known.unionWith(DstKnown.anyext(Known.getBitWidth()));
    return;
  }

  computeKnownBitsFromCmp(V, Pred, LHS, RHS, Known, SQ);
}

static void computeKnownBitsFromCond(const Value *V, Value *Cond,
                                     KnownBits &Known, unsigned Depth,
                                     const SimplifyQuery &SQ, bool Invert) {
  Value *A, *B;
  if (Depth < MaxAnalysisRecursionDepth &&
      match(Cond, m_LogicalOp(m_Value(A), m_Value(B)))) {
    KnownBits Known2(Known.getBitWidth());
    KnownBits Known3(Known.getBitWidth());
    computeKnownBitsFromCond(V, A, Known2, Depth + 1, SQ, Invert);
    computeKnownBitsFromCond(V, B, Known3, Depth + 1, SQ, Invert);
    if (Invert ? match(Cond, m_LogicalOr(m_Value(), m_Value()))
               : match(Cond, m_LogicalAnd(m_Value(), m_Value())))
      Known2 = Known2.unionWith(Known3);
    else
      Known2 = Known2.intersectWith(Known3);
    Known = Known.unionWith(Known2);
  }

  if (auto *Cmp = dyn_cast<ICmpInst>(Cond))
    computeKnownBitsFromICmpCond(V, Cmp, Known, SQ, Invert);
}

void llvm::computeKnownBitsFromContext(const Value *V, KnownBits &Known,
                                       unsigned Depth, const SimplifyQuery &Q) {
  // Handle injected condition.
  if (Q.CC && Q.CC->AffectedValues.contains(V))
    computeKnownBitsFromCond(V, Q.CC->Cond, Known, Depth, Q, Q.CC->Invert);

  if (!Q.CxtI)
    return;

  if (Q.DC && Q.DT) {
    // Handle dominating conditions.
    for (BranchInst *BI : Q.DC->conditionsFor(V)) {
      BasicBlockEdge Edge0(BI->getParent(), BI->getSuccessor(0));
      if (Q.DT->dominates(Edge0, Q.CxtI->getParent()))
        computeKnownBitsFromCond(V, BI->getCondition(), Known, Depth, Q,
                                 /*Invert*/ false);

      BasicBlockEdge Edge1(BI->getParent(), BI->getSuccessor(1));
      if (Q.DT->dominates(Edge1, Q.CxtI->getParent()))
        computeKnownBitsFromCond(V, BI->getCondition(), Known, Depth, Q,
                                 /*Invert*/ true);
    }

    if (Known.hasConflict())
      Known.resetAll();
  }

  if (!Q.AC)
    return;

  unsigned BitWidth = Known.getBitWidth();

  // Note that the patterns below need to be kept in sync with the code
  // in AssumptionCache::updateAffectedValues.

  for (AssumptionCache::ResultElem &Elem : Q.AC->assumptionsFor(V)) {
    if (!Elem.Assume)
      continue;

    AssumeInst *I = cast<AssumeInst>(Elem.Assume);
    assert(I->getParent()->getParent() == Q.CxtI->getParent()->getParent() &&
           "Got assumption for the wrong function!");

    if (Elem.Index != AssumptionCache::ExprResultIdx) {
      if (!V->getType()->isPointerTy())
        continue;
      if (RetainedKnowledge RK = getKnowledgeFromBundle(
              *I, I->bundle_op_info_begin()[Elem.Index])) {
        if (RK.WasOn == V && RK.AttrKind == Attribute::Alignment &&
            isPowerOf2_64(RK.ArgValue) &&
            isValidAssumeForContext(I, Q.CxtI, Q.DT))
          Known.Zero.setLowBits(Log2_64(RK.ArgValue));
      }
      continue;
    }

    // Warning: This loop can end up being somewhat performance sensitive.
    // We're running this loop for once for each value queried resulting in a
    // runtime of ~O(#assumes * #values).

    Value *Arg = I->getArgOperand(0);

    if (Arg == V && isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      assert(BitWidth == 1 && "assume operand is not i1?");
      (void)BitWidth;
      Known.setAllOnes();
      return;
    }
    if (match(Arg, m_Not(m_Specific(V))) &&
        isValidAssumeForContext(I, Q.CxtI, Q.DT)) {
      assert(BitWidth == 1 && "assume operand is not i1?");
      (void)BitWidth;
      Known.setAllZero();
      return;
    }

    // The remaining tests are all recursive, so bail out if we hit the limit.
    if (Depth == MaxAnalysisRecursionDepth)
      continue;

    ICmpInst *Cmp = dyn_cast<ICmpInst>(Arg);
    if (!Cmp)
      continue;

    if (!isValidAssumeForContext(I, Q.CxtI, Q.DT))
      continue;

    computeKnownBitsFromICmpCond(V, Cmp, Known, Q, /*Invert=*/false);
  }

  // Conflicting assumption: Undefined behavior will occur on this execution
  // path.
  if (Known.hasConflict())
    Known.resetAll();
}

/// Compute known bits from a shift operator, including those with a
/// non-constant shift amount. Known is the output of this function. Known2 is a
/// pre-allocated temporary with the same bit width as Known and on return
/// contains the known bit of the shift value source. KF is an
/// operator-specific function that, given the known-bits and a shift amount,
/// compute the implied known-bits of the shift operator's result respectively
/// for that shift amount. The results from calling KF are conservatively
/// combined for all permitted shift amounts.
static void computeKnownBitsFromShiftOperator(
    const Operator *I, const APInt &DemandedElts, KnownBits &Known,
    KnownBits &Known2, unsigned Depth, const SimplifyQuery &Q,
    function_ref<KnownBits(const KnownBits &, const KnownBits &, bool)> KF) {
  computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
  computeKnownBits(I->getOperand(1), DemandedElts, Known, Depth + 1, Q);
  // To limit compile-time impact, only query isKnownNonZero() if we know at
  // least something about the shift amount.
  bool ShAmtNonZero =
      Known.isNonZero() ||
      (Known.getMaxValue().ult(Known.getBitWidth()) &&
       isKnownNonZero(I->getOperand(1), DemandedElts, Q, Depth + 1));
  Known = KF(Known2, Known, ShAmtNonZero);
}

static KnownBits
getKnownBitsFromAndXorOr(const Operator *I, const APInt &DemandedElts,
                         const KnownBits &KnownLHS, const KnownBits &KnownRHS,
                         unsigned Depth, const SimplifyQuery &Q) {
  unsigned BitWidth = KnownLHS.getBitWidth();
  KnownBits KnownOut(BitWidth);
  bool IsAnd = false;
  bool HasKnownOne = !KnownLHS.One.isZero() || !KnownRHS.One.isZero();
  Value *X = nullptr, *Y = nullptr;

  switch (I->getOpcode()) {
  case Instruction::And:
    KnownOut = KnownLHS & KnownRHS;
    IsAnd = true;
    // and(x, -x) is common idioms that will clear all but lowest set
    // bit. If we have a single known bit in x, we can clear all bits
    // above it.
    // TODO: instcombine often reassociates independent `and` which can hide
    // this pattern. Try to match and(x, and(-x, y)) / and(and(x, y), -x).
    if (HasKnownOne && match(I, m_c_And(m_Value(X), m_Neg(m_Deferred(X))))) {
      // -(-x) == x so using whichever (LHS/RHS) gets us a better result.
      if (KnownLHS.countMaxTrailingZeros() <= KnownRHS.countMaxTrailingZeros())
        KnownOut = KnownLHS.blsi();
      else
        KnownOut = KnownRHS.blsi();
    }
    break;
  case Instruction::Or:
    KnownOut = KnownLHS | KnownRHS;
    break;
  case Instruction::Xor:
    KnownOut = KnownLHS ^ KnownRHS;
    // xor(x, x-1) is common idioms that will clear all but lowest set
    // bit. If we have a single known bit in x, we can clear all bits
    // above it.
    // TODO: xor(x, x-1) is often rewritting as xor(x, x-C) where C !=
    // -1 but for the purpose of demanded bits (xor(x, x-C) &
    // Demanded) == (xor(x, x-1) & Demanded). Extend the xor pattern
    // to use arbitrary C if xor(x, x-C) as the same as xor(x, x-1).
    if (HasKnownOne &&
        match(I, m_c_Xor(m_Value(X), m_Add(m_Deferred(X), m_AllOnes())))) {
      const KnownBits &XBits = I->getOperand(0) == X ? KnownLHS : KnownRHS;
      KnownOut = XBits.blsmsk();
    }
    break;
  default:
    llvm_unreachable("Invalid Op used in 'analyzeKnownBitsFromAndXorOr'");
  }

  // and(x, add (x, -1)) is a common idiom that always clears the low bit;
  // xor/or(x, add (x, -1)) is an idiom that will always set the low bit.
  // here we handle the more general case of adding any odd number by
  // matching the form and/xor/or(x, add(x, y)) where y is odd.
  // TODO: This could be generalized to clearing any bit set in y where the
  // following bit is known to be unset in y.
  if (!KnownOut.Zero[0] && !KnownOut.One[0] &&
      (match(I, m_c_BinOp(m_Value(X), m_c_Add(m_Deferred(X), m_Value(Y)))) ||
       match(I, m_c_BinOp(m_Value(X), m_Sub(m_Deferred(X), m_Value(Y)))) ||
       match(I, m_c_BinOp(m_Value(X), m_Sub(m_Value(Y), m_Deferred(X)))))) {
    KnownBits KnownY(BitWidth);
    computeKnownBits(Y, DemandedElts, KnownY, Depth + 1, Q);
    if (KnownY.countMinTrailingOnes() > 0) {
      if (IsAnd)
        KnownOut.Zero.setBit(0);
      else
        KnownOut.One.setBit(0);
    }
  }
  return KnownOut;
}

static KnownBits computeKnownBitsForHorizontalOperation(
    const Operator *I, const APInt &DemandedElts, unsigned Depth,
    const SimplifyQuery &Q,
    const function_ref<KnownBits(const KnownBits &, const KnownBits &)>
        KnownBitsFunc) {
  APInt DemandedEltsLHS, DemandedEltsRHS;
  getHorizDemandedEltsForFirstOperand(Q.DL.getTypeSizeInBits(I->getType()),
                                      DemandedElts, DemandedEltsLHS,
                                      DemandedEltsRHS);

  const auto ComputeForSingleOpFunc =
      [Depth, &Q, KnownBitsFunc](const Value *Op, APInt &DemandedEltsOp) {
        return KnownBitsFunc(
            computeKnownBits(Op, DemandedEltsOp, Depth + 1, Q),
            computeKnownBits(Op, DemandedEltsOp << 1, Depth + 1, Q));
      };

  if (DemandedEltsRHS.isZero())
    return ComputeForSingleOpFunc(I->getOperand(0), DemandedEltsLHS);
  if (DemandedEltsLHS.isZero())
    return ComputeForSingleOpFunc(I->getOperand(1), DemandedEltsRHS);

  return ComputeForSingleOpFunc(I->getOperand(0), DemandedEltsLHS)
      .intersectWith(ComputeForSingleOpFunc(I->getOperand(1), DemandedEltsRHS));
}

// Public so this can be used in `SimplifyDemandedUseBits`.
KnownBits llvm::analyzeKnownBitsFromAndXorOr(const Operator *I,
                                             const KnownBits &KnownLHS,
                                             const KnownBits &KnownRHS,
                                             unsigned Depth,
                                             const SimplifyQuery &SQ) {
  auto *FVTy = dyn_cast<FixedVectorType>(I->getType());
  APInt DemandedElts =
      FVTy ? APInt::getAllOnes(FVTy->getNumElements()) : APInt(1, 1);

  return getKnownBitsFromAndXorOr(I, DemandedElts, KnownLHS, KnownRHS, Depth,
                                  SQ);
}

ConstantRange llvm::getVScaleRange(const Function *F, unsigned BitWidth) {
  Attribute Attr = F->getFnAttribute(Attribute::VScaleRange);
  // Without vscale_range, we only know that vscale is non-zero.
  if (!Attr.isValid())
    return ConstantRange(APInt(BitWidth, 1), APInt::getZero(BitWidth));

  unsigned AttrMin = Attr.getVScaleRangeMin();
  // Minimum is larger than vscale width, result is always poison.
  if ((unsigned)llvm::bit_width(AttrMin) > BitWidth)
    return ConstantRange::getEmpty(BitWidth);

  APInt Min(BitWidth, AttrMin);
  std::optional<unsigned> AttrMax = Attr.getVScaleRangeMax();
  if (!AttrMax || (unsigned)llvm::bit_width(*AttrMax) > BitWidth)
    return ConstantRange(Min, APInt::getZero(BitWidth));

  return ConstantRange(Min, APInt(BitWidth, *AttrMax) + 1);
}

void llvm::adjustKnownBitsForSelectArm(KnownBits &Known, Value *Cond,
                                       Value *Arm, bool Invert, unsigned Depth,
                                       const SimplifyQuery &Q) {
  // If we have a constant arm, we are done.
  if (Known.isConstant())
    return;

  // See what condition implies about the bits of the select arm.
  KnownBits CondRes(Known.getBitWidth());
  computeKnownBitsFromCond(Arm, Cond, CondRes, Depth + 1, Q, Invert);
  // If we don't get any information from the condition, no reason to
  // proceed.
  if (CondRes.isUnknown())
    return;

  // We can have conflict if the condition is dead. I.e if we have
  // (x | 64) < 32 ? (x | 64) : y
  // we will have conflict at bit 6 from the condition/the `or`.
  // In that case just return. Its not particularly important
  // what we do, as this select is going to be simplified soon.
  CondRes = CondRes.unionWith(Known);
  if (CondRes.hasConflict())
    return;

  // Finally make sure the information we found is valid. This is relatively
  // expensive so it's left for the very end.
  if (!isGuaranteedNotToBeUndef(Arm, Q.AC, Q.CxtI, Q.DT, Depth + 1))
    return;

  // Finally, we know we get information from the condition and its valid,
  // so return it.
  Known = CondRes;
}

static void computeKnownBitsFromOperator(const Operator *I,
                                         const APInt &DemandedElts,
                                         KnownBits &Known, unsigned Depth,
                                         const SimplifyQuery &Q) {
  unsigned BitWidth = Known.getBitWidth();

  KnownBits Known2(BitWidth);
  switch (I->getOpcode()) {
  default: break;
  case Instruction::Load:
    if (MDNode *MD =
            Q.IIQ.getMetadata(cast<LoadInst>(I), LLVMContext::MD_range))
      computeKnownBitsFromRangeMetadata(*MD, Known);
    break;
  case Instruction::And:
    computeKnownBits(I->getOperand(1), DemandedElts, Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);

    Known = getKnownBitsFromAndXorOr(I, DemandedElts, Known2, Known, Depth, Q);
    break;
  case Instruction::Or:
    computeKnownBits(I->getOperand(1), DemandedElts, Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);

    Known = getKnownBitsFromAndXorOr(I, DemandedElts, Known2, Known, Depth, Q);
    break;
  case Instruction::Xor:
    computeKnownBits(I->getOperand(1), DemandedElts, Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);

    Known = getKnownBitsFromAndXorOr(I, DemandedElts, Known2, Known, Depth, Q);
    break;
  case Instruction::Mul: {
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    computeKnownBitsMul(I->getOperand(0), I->getOperand(1), NSW, DemandedElts,
                        Known, Known2, Depth, Q);
    break;
  }
  case Instruction::UDiv: {
    computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
    Known =
        KnownBits::udiv(Known, Known2, Q.IIQ.isExact(cast<BinaryOperator>(I)));
    break;
  }
  case Instruction::SDiv: {
    computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
    Known =
        KnownBits::sdiv(Known, Known2, Q.IIQ.isExact(cast<BinaryOperator>(I)));
    break;
  }
  case Instruction::Select: {
    auto ComputeForArm = [&](Value *Arm, bool Invert) {
      KnownBits Res(Known.getBitWidth());
      computeKnownBits(Arm, DemandedElts, Res, Depth + 1, Q);
      adjustKnownBitsForSelectArm(Res, I->getOperand(0), Arm, Invert, Depth, Q);
      return Res;
    };
    // Only known if known in both the LHS and RHS.
    Known =
        ComputeForArm(I->getOperand(1), /*Invert=*/false)
            .intersectWith(ComputeForArm(I->getOperand(2), /*Invert=*/true));
    break;
  }
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
    break; // Can't work with floating point.
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
    // Fall through and handle them the same as zext/trunc.
    [[fallthrough]];
  case Instruction::ZExt:
  case Instruction::Trunc: {
    Type *SrcTy = I->getOperand(0)->getType();

    unsigned SrcBitWidth;
    // Note that we handle pointer operands here because of inttoptr/ptrtoint
    // which fall through here.
    Type *ScalarTy = SrcTy->getScalarType();
    SrcBitWidth = ScalarTy->isPointerTy() ?
      Q.DL.getPointerTypeSizeInBits(ScalarTy) :
      Q.DL.getTypeSizeInBits(ScalarTy);

    assert(SrcBitWidth && "SrcBitWidth can't be zero");
    Known = Known.anyextOrTrunc(SrcBitWidth);
    computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
    if (auto *Inst = dyn_cast<PossiblyNonNegInst>(I);
        Inst && Inst->hasNonNeg() && !Known.isNegative())
      Known.makeNonNegative();
    Known = Known.zextOrTrunc(BitWidth);
    break;
  }
  case Instruction::BitCast: {
    Type *SrcTy = I->getOperand(0)->getType();
    if (SrcTy->isIntOrPtrTy() &&
        // TODO: For now, not handling conversions like:
        // (bitcast i64 %x to <2 x i32>)
        !I->getType()->isVectorTy()) {
      computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
      break;
    }

    const Value *V;
    // Handle bitcast from floating point to integer.
    if (match(I, m_ElementWiseBitCast(m_Value(V))) &&
        V->getType()->isFPOrFPVectorTy()) {
      Type *FPType = V->getType()->getScalarType();
      KnownFPClass Result =
          computeKnownFPClass(V, DemandedElts, fcAllFlags, Depth + 1, Q);
      FPClassTest FPClasses = Result.KnownFPClasses;

      // TODO: Treat it as zero/poison if the use of I is unreachable.
      if (FPClasses == fcNone)
        break;

      if (Result.isKnownNever(fcNormal | fcSubnormal | fcNan)) {
        Known.Zero.setAllBits();
        Known.One.setAllBits();

        if (FPClasses & fcInf)
          Known = Known.intersectWith(KnownBits::makeConstant(
              APFloat::getInf(FPType->getFltSemantics()).bitcastToAPInt()));

        if (FPClasses & fcZero)
          Known = Known.intersectWith(KnownBits::makeConstant(
              APInt::getZero(FPType->getScalarSizeInBits())));

        Known.Zero.clearSignBit();
        Known.One.clearSignBit();
      }

      if (Result.SignBit) {
        if (*Result.SignBit)
          Known.makeNegative();
        else
          Known.makeNonNegative();
      }

      break;
    }

    // Handle cast from vector integer type to scalar or vector integer.
    auto *SrcVecTy = dyn_cast<FixedVectorType>(SrcTy);
    if (!SrcVecTy || !SrcVecTy->getElementType()->isIntegerTy() ||
        !I->getType()->isIntOrIntVectorTy() ||
        isa<ScalableVectorType>(I->getType()))
      break;

    // Look through a cast from narrow vector elements to wider type.
    // Examples: v4i32 -> v2i64, v3i8 -> v24
    unsigned SubBitWidth = SrcVecTy->getScalarSizeInBits();
    if (BitWidth % SubBitWidth == 0) {
      // Known bits are automatically intersected across demanded elements of a
      // vector. So for example, if a bit is computed as known zero, it must be
      // zero across all demanded elements of the vector.
      //
      // For this bitcast, each demanded element of the output is sub-divided
      // across a set of smaller vector elements in the source vector. To get
      // the known bits for an entire element of the output, compute the known
      // bits for each sub-element sequentially. This is done by shifting the
      // one-set-bit demanded elements parameter across the sub-elements for
      // consecutive calls to computeKnownBits. We are using the demanded
      // elements parameter as a mask operator.
      //
      // The known bits of each sub-element are then inserted into place
      // (dependent on endian) to form the full result of known bits.
      unsigned NumElts = DemandedElts.getBitWidth();
      unsigned SubScale = BitWidth / SubBitWidth;
      APInt SubDemandedElts = APInt::getZero(NumElts * SubScale);
      for (unsigned i = 0; i != NumElts; ++i) {
        if (DemandedElts[i])
          SubDemandedElts.setBit(i * SubScale);
      }

      KnownBits KnownSrc(SubBitWidth);
      for (unsigned i = 0; i != SubScale; ++i) {
        computeKnownBits(I->getOperand(0), SubDemandedElts.shl(i), KnownSrc,
                         Depth + 1, Q);
        unsigned ShiftElt = Q.DL.isLittleEndian() ? i : SubScale - 1 - i;
        Known.insertBits(KnownSrc, ShiftElt * SubBitWidth);
      }
    }
    break;
  }
  case Instruction::SExt: {
    // Compute the bits in the result that are not present in the input.
    unsigned SrcBitWidth = I->getOperand(0)->getType()->getScalarSizeInBits();

    Known = Known.trunc(SrcBitWidth);
    computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
    // If the sign bit of the input is known set or clear, then we know the
    // top bits of the result.
    Known = Known.sext(BitWidth);
    break;
  }
  case Instruction::Shl: {
    bool NUW = Q.IIQ.hasNoUnsignedWrap(cast<OverflowingBinaryOperator>(I));
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    auto KF = [NUW, NSW](const KnownBits &KnownVal, const KnownBits &KnownAmt,
                         bool ShAmtNonZero) {
      return KnownBits::shl(KnownVal, KnownAmt, NUW, NSW, ShAmtNonZero);
    };
    computeKnownBitsFromShiftOperator(I, DemandedElts, Known, Known2, Depth, Q,
                                      KF);
    // Trailing zeros of a right-shifted constant never decrease.
    const APInt *C;
    if (match(I->getOperand(0), m_APInt(C)))
      Known.Zero.setLowBits(C->countr_zero());
    break;
  }
  case Instruction::LShr: {
    bool Exact = Q.IIQ.isExact(cast<BinaryOperator>(I));
    auto KF = [Exact](const KnownBits &KnownVal, const KnownBits &KnownAmt,
                      bool ShAmtNonZero) {
      return KnownBits::lshr(KnownVal, KnownAmt, ShAmtNonZero, Exact);
    };
    computeKnownBitsFromShiftOperator(I, DemandedElts, Known, Known2, Depth, Q,
                                      KF);
    // Leading zeros of a left-shifted constant never decrease.
    const APInt *C;
    if (match(I->getOperand(0), m_APInt(C)))
      Known.Zero.setHighBits(C->countl_zero());
    break;
  }
  case Instruction::AShr: {
    bool Exact = Q.IIQ.isExact(cast<BinaryOperator>(I));
    auto KF = [Exact](const KnownBits &KnownVal, const KnownBits &KnownAmt,
                      bool ShAmtNonZero) {
      return KnownBits::ashr(KnownVal, KnownAmt, ShAmtNonZero, Exact);
    };
    computeKnownBitsFromShiftOperator(I, DemandedElts, Known, Known2, Depth, Q,
                                      KF);
    break;
  }
  case Instruction::Sub: {
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    bool NUW = Q.IIQ.hasNoUnsignedWrap(cast<OverflowingBinaryOperator>(I));
    computeKnownBitsAddSub(false, I->getOperand(0), I->getOperand(1), NSW, NUW,
                           DemandedElts, Known, Known2, Depth, Q);
    break;
  }
  case Instruction::Add: {
    bool NSW = Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(I));
    bool NUW = Q.IIQ.hasNoUnsignedWrap(cast<OverflowingBinaryOperator>(I));
    computeKnownBitsAddSub(true, I->getOperand(0), I->getOperand(1), NSW, NUW,
                           DemandedElts, Known, Known2, Depth, Q);
    break;
  }
  case Instruction::SRem:
    computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
    Known = KnownBits::srem(Known, Known2);
    break;

  case Instruction::URem:
    computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
    computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
    Known = KnownBits::urem(Known, Known2);
    break;
  case Instruction::Alloca:
    Known.Zero.setLowBits(Log2(cast<AllocaInst>(I)->getAlign()));
    break;
  case Instruction::GetElementPtr: {
    // Analyze all of the subscripts of this getelementptr instruction
    // to determine if we can prove known low zero bits.
    computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
    // Accumulate the constant indices in a separate variable
    // to minimize the number of calls to computeForAddSub.
    APInt AccConstIndices(BitWidth, 0, /*IsSigned*/ true);

    gep_type_iterator GTI = gep_type_begin(I);
    for (unsigned i = 1, e = I->getNumOperands(); i != e; ++i, ++GTI) {
      // TrailZ can only become smaller, short-circuit if we hit zero.
      if (Known.isUnknown())
        break;

      Value *Index = I->getOperand(i);

      // Handle case when index is zero.
      Constant *CIndex = dyn_cast<Constant>(Index);
      if (CIndex && CIndex->isZeroValue())
        continue;

      if (StructType *STy = GTI.getStructTypeOrNull()) {
        // Handle struct member offset arithmetic.

        assert(CIndex &&
               "Access to structure field must be known at compile time");

        if (CIndex->getType()->isVectorTy())
          Index = CIndex->getSplatValue();

        unsigned Idx = cast<ConstantInt>(Index)->getZExtValue();
        const StructLayout *SL = Q.DL.getStructLayout(STy);
        uint64_t Offset = SL->getElementOffset(Idx);
        AccConstIndices += Offset;
        continue;
      }

      // Handle array index arithmetic.
      Type *IndexedTy = GTI.getIndexedType();
      if (!IndexedTy->isSized()) {
        Known.resetAll();
        break;
      }

      unsigned IndexBitWidth = Index->getType()->getScalarSizeInBits();
      KnownBits IndexBits(IndexBitWidth);
      computeKnownBits(Index, IndexBits, Depth + 1, Q);
      TypeSize IndexTypeSize = GTI.getSequentialElementStride(Q.DL);
      uint64_t TypeSizeInBytes = IndexTypeSize.getKnownMinValue();
      KnownBits ScalingFactor(IndexBitWidth);
      // Multiply by current sizeof type.
      // &A[i] == A + i * sizeof(*A[i]).
      if (IndexTypeSize.isScalable()) {
        // For scalable types the only thing we know about sizeof is
        // that this is a multiple of the minimum size.
        ScalingFactor.Zero.setLowBits(llvm::countr_zero(TypeSizeInBytes));
      } else if (IndexBits.isConstant()) {
        APInt IndexConst = IndexBits.getConstant();
        APInt ScalingFactor(IndexBitWidth, TypeSizeInBytes);
        IndexConst *= ScalingFactor;
        AccConstIndices += IndexConst.sextOrTrunc(BitWidth);
        continue;
      } else {
        ScalingFactor =
            KnownBits::makeConstant(APInt(IndexBitWidth, TypeSizeInBytes));
      }
      IndexBits = KnownBits::mul(IndexBits, ScalingFactor);

      // If the offsets have a different width from the pointer, according
      // to the language reference we need to sign-extend or truncate them
      // to the width of the pointer.
      IndexBits = IndexBits.sextOrTrunc(BitWidth);

      // Note that inbounds does *not* guarantee nsw for the addition, as only
      // the offset is signed, while the base address is unsigned.
      Known = KnownBits::computeForAddSub(
          /*Add=*/true, /*NSW=*/false, /* NUW=*/false, Known, IndexBits);
    }
    if (!Known.isUnknown() && !AccConstIndices.isZero()) {
      KnownBits Index = KnownBits::makeConstant(AccConstIndices);
      Known = KnownBits::computeForAddSub(
          /*Add=*/true, /*NSW=*/false, /* NUW=*/false, Known, Index);
    }
    break;
  }
  case Instruction::PHI: {
    const PHINode *P = cast<PHINode>(I);
    BinaryOperator *BO = nullptr;
    Value *R = nullptr, *L = nullptr;
    if (matchSimpleRecurrence(P, BO, R, L)) {
      // Handle the case of a simple two-predecessor recurrence PHI.
      // There's a lot more that could theoretically be done here, but
      // this is sufficient to catch some interesting cases.
      unsigned Opcode = BO->getOpcode();

      // If this is a shift recurrence, we know the bits being shifted in.
      // We can combine that with information about the start value of the
      // recurrence to conclude facts about the result.
      if ((Opcode == Instruction::LShr || Opcode == Instruction::AShr ||
           Opcode == Instruction::Shl) &&
          BO->getOperand(0) == I) {

        // We have matched a recurrence of the form:
        // %iv = [R, %entry], [%iv.next, %backedge]
        // %iv.next = shift_op %iv, L

        // Recurse with the phi context to avoid concern about whether facts
        // inferred hold at original context instruction.  TODO: It may be
        // correct to use the original context.  IF warranted, explore and
        // add sufficient tests to cover.
        SimplifyQuery RecQ = Q.getWithoutCondContext();
        RecQ.CxtI = P;
        computeKnownBits(R, DemandedElts, Known2, Depth + 1, RecQ);
        switch (Opcode) {
        case Instruction::Shl:
          // A shl recurrence will only increase the tailing zeros
          Known.Zero.setLowBits(Known2.countMinTrailingZeros());
          break;
        case Instruction::LShr:
          // A lshr recurrence will preserve the leading zeros of the
          // start value
          Known.Zero.setHighBits(Known2.countMinLeadingZeros());
          break;
        case Instruction::AShr:
          // An ashr recurrence will extend the initial sign bit
          Known.Zero.setHighBits(Known2.countMinLeadingZeros());
          Known.One.setHighBits(Known2.countMinLeadingOnes());
          break;
        };
      }

      // Check for operations that have the property that if
      // both their operands have low zero bits, the result
      // will have low zero bits.
      if (Opcode == Instruction::Add ||
          Opcode == Instruction::Sub ||
          Opcode == Instruction::And ||
          Opcode == Instruction::Or ||
          Opcode == Instruction::Mul) {
        // Change the context instruction to the "edge" that flows into the
        // phi. This is important because that is where the value is actually
        // "evaluated" even though it is used later somewhere else. (see also
        // D69571).
        SimplifyQuery RecQ = Q.getWithoutCondContext();

        unsigned OpNum = P->getOperand(0) == R ? 0 : 1;
        Instruction *RInst = P->getIncomingBlock(OpNum)->getTerminator();
        Instruction *LInst = P->getIncomingBlock(1 - OpNum)->getTerminator();

        // Ok, we have a PHI of the form L op= R. Check for low
        // zero bits.
        RecQ.CxtI = RInst;
        computeKnownBits(R, DemandedElts, Known2, Depth + 1, RecQ);

        // We need to take the minimum number of known bits
        KnownBits Known3(BitWidth);
        RecQ.CxtI = LInst;
        computeKnownBits(L, DemandedElts, Known3, Depth + 1, RecQ);

        Known.Zero.setLowBits(std::min(Known2.countMinTrailingZeros(),
                                       Known3.countMinTrailingZeros()));

        auto *OverflowOp = dyn_cast<OverflowingBinaryOperator>(BO);
        if (OverflowOp && Q.IIQ.hasNoSignedWrap(OverflowOp)) {
          // If initial value of recurrence is nonnegative, and we are adding
          // a nonnegative number with nsw, the result can only be nonnegative
          // or poison value regardless of the number of times we execute the
          // add in phi recurrence. If initial value is negative and we are
          // adding a negative number with nsw, the result can only be
          // negative or poison value. Similar arguments apply to sub and mul.
          //
          // (add non-negative, non-negative) --> non-negative
          // (add negative, negative) --> negative
          if (Opcode == Instruction::Add) {
            if (Known2.isNonNegative() && Known3.isNonNegative())
              Known.makeNonNegative();
            else if (Known2.isNegative() && Known3.isNegative())
              Known.makeNegative();
          }

          // (sub nsw non-negative, negative) --> non-negative
          // (sub nsw negative, non-negative) --> negative
          else if (Opcode == Instruction::Sub && BO->getOperand(0) == I) {
            if (Known2.isNonNegative() && Known3.isNegative())
              Known.makeNonNegative();
            else if (Known2.isNegative() && Known3.isNonNegative())
              Known.makeNegative();
          }

          // (mul nsw non-negative, non-negative) --> non-negative
          else if (Opcode == Instruction::Mul && Known2.isNonNegative() &&
                   Known3.isNonNegative())
            Known.makeNonNegative();
        }

        break;
      }
    }

    // Unreachable blocks may have zero-operand PHI nodes.
    if (P->getNumIncomingValues() == 0)
      break;

    // Otherwise take the unions of the known bit sets of the operands,
    // taking conservative care to avoid excessive recursion.
    if (Depth < MaxAnalysisRecursionDepth - 1 && Known.isUnknown()) {
      // Skip if every incoming value references to ourself.
      if (isa_and_nonnull<UndefValue>(P->hasConstantValue()))
        break;

      Known.Zero.setAllBits();
      Known.One.setAllBits();
      for (unsigned u = 0, e = P->getNumIncomingValues(); u < e; ++u) {
        Value *IncValue = P->getIncomingValue(u);
        // Skip direct self references.
        if (IncValue == P) continue;

        // Change the context instruction to the "edge" that flows into the
        // phi. This is important because that is where the value is actually
        // "evaluated" even though it is used later somewhere else. (see also
        // D69571).
        SimplifyQuery RecQ = Q.getWithoutCondContext();
        RecQ.CxtI = P->getIncomingBlock(u)->getTerminator();

        Known2 = KnownBits(BitWidth);

        // Recurse, but cap the recursion to one level, because we don't
        // want to waste time spinning around in loops.
        // TODO: See if we can base recursion limiter on number of incoming phi
        // edges so we don't overly clamp analysis.
        computeKnownBits(IncValue, DemandedElts, Known2,
                         MaxAnalysisRecursionDepth - 1, RecQ);

        // See if we can further use a conditional branch into the phi
        // to help us determine the range of the value.
        if (!Known2.isConstant()) {
          ICmpInst::Predicate Pred;
          const APInt *RHSC;
          BasicBlock *TrueSucc, *FalseSucc;
          // TODO: Use RHS Value and compute range from its known bits.
          if (match(RecQ.CxtI,
                    m_Br(m_c_ICmp(Pred, m_Specific(IncValue), m_APInt(RHSC)),
                         m_BasicBlock(TrueSucc), m_BasicBlock(FalseSucc)))) {
            // Check for cases of duplicate successors.
            if ((TrueSucc == P->getParent()) != (FalseSucc == P->getParent())) {
              // If we're using the false successor, invert the predicate.
              if (FalseSucc == P->getParent())
                Pred = CmpInst::getInversePredicate(Pred);
              // Get the knownbits implied by the incoming phi condition.
              auto CR = ConstantRange::makeExactICmpRegion(Pred, *RHSC);
              KnownBits KnownUnion = Known2.unionWith(CR.toKnownBits());
              // We can have conflicts here if we are analyzing deadcode (its
              // impossible for us reach this BB based the icmp).
              if (KnownUnion.hasConflict()) {
                // No reason to continue analyzing in a known dead region, so
                // just resetAll and break. This will cause us to also exit the
                // outer loop.
                Known.resetAll();
                break;
              }
              Known2 = KnownUnion;
            }
          }
        }

        Known = Known.intersectWith(Known2);
        // If all bits have been ruled out, there's no need to check
        // more operands.
        if (Known.isUnknown())
          break;
      }
    }
    break;
  }
  case Instruction::Call:
  case Instruction::Invoke: {
    // If range metadata is attached to this call, set known bits from that,
    // and then intersect with known bits based on other properties of the
    // function.
    if (MDNode *MD =
            Q.IIQ.getMetadata(cast<Instruction>(I), LLVMContext::MD_range))
      computeKnownBitsFromRangeMetadata(*MD, Known);

    const auto *CB = cast<CallBase>(I);

    if (std::optional<ConstantRange> Range = CB->getRange())
      Known = Known.unionWith(Range->toKnownBits());

    if (const Value *RV = CB->getReturnedArgOperand()) {
      if (RV->getType() == I->getType()) {
        computeKnownBits(RV, Known2, Depth + 1, Q);
        Known = Known.unionWith(Known2);
        // If the function doesn't return properly for all input values
        // (e.g. unreachable exits) then there might be conflicts between the
        // argument value and the range metadata. Simply discard the known bits
        // in case of conflicts.
        if (Known.hasConflict())
          Known.resetAll();
      }
    }
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      default:
        break;
      case Intrinsic::abs: {
        computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
        bool IntMinIsPoison = match(II->getArgOperand(1), m_One());
        Known = Known2.abs(IntMinIsPoison);
        break;
      }
      case Intrinsic::bitreverse:
        computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
        Known.Zero |= Known2.Zero.reverseBits();
        Known.One |= Known2.One.reverseBits();
        break;
      case Intrinsic::bswap:
        computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
        Known.Zero |= Known2.Zero.byteSwap();
        Known.One |= Known2.One.byteSwap();
        break;
      case Intrinsic::ctlz: {
        computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
        // If we have a known 1, its position is our upper bound.
        unsigned PossibleLZ = Known2.countMaxLeadingZeros();
        // If this call is poison for 0 input, the result will be less than 2^n.
        if (II->getArgOperand(1) == ConstantInt::getTrue(II->getContext()))
          PossibleLZ = std::min(PossibleLZ, BitWidth - 1);
        unsigned LowBits = llvm::bit_width(PossibleLZ);
        Known.Zero.setBitsFrom(LowBits);
        break;
      }
      case Intrinsic::cttz: {
        computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
        // If we have a known 1, its position is our upper bound.
        unsigned PossibleTZ = Known2.countMaxTrailingZeros();
        // If this call is poison for 0 input, the result will be less than 2^n.
        if (II->getArgOperand(1) == ConstantInt::getTrue(II->getContext()))
          PossibleTZ = std::min(PossibleTZ, BitWidth - 1);
        unsigned LowBits = llvm::bit_width(PossibleTZ);
        Known.Zero.setBitsFrom(LowBits);
        break;
      }
      case Intrinsic::ctpop: {
        computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
        // We can bound the space the count needs.  Also, bits known to be zero
        // can't contribute to the population.
        unsigned BitsPossiblySet = Known2.countMaxPopulation();
        unsigned LowBits = llvm::bit_width(BitsPossiblySet);
        Known.Zero.setBitsFrom(LowBits);
        // TODO: we could bound KnownOne using the lower bound on the number
        // of bits which might be set provided by popcnt KnownOne2.
        break;
      }
      case Intrinsic::fshr:
      case Intrinsic::fshl: {
        const APInt *SA;
        if (!match(I->getOperand(2), m_APInt(SA)))
          break;

        // Normalize to funnel shift left.
        uint64_t ShiftAmt = SA->urem(BitWidth);
        if (II->getIntrinsicID() == Intrinsic::fshr)
          ShiftAmt = BitWidth - ShiftAmt;

        KnownBits Known3(BitWidth);
        computeKnownBits(I->getOperand(0), DemandedElts, Known2, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known3, Depth + 1, Q);

        Known.Zero =
            Known2.Zero.shl(ShiftAmt) | Known3.Zero.lshr(BitWidth - ShiftAmt);
        Known.One =
            Known2.One.shl(ShiftAmt) | Known3.One.lshr(BitWidth - ShiftAmt);
        break;
      }
      case Intrinsic::uadd_sat:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::uadd_sat(Known, Known2);
        break;
      case Intrinsic::usub_sat:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::usub_sat(Known, Known2);
        break;
      case Intrinsic::sadd_sat:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::sadd_sat(Known, Known2);
        break;
      case Intrinsic::ssub_sat:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::ssub_sat(Known, Known2);
        break;
        // Vec reverse preserves bits from input vec.
      case Intrinsic::vector_reverse:
        computeKnownBits(I->getOperand(0), DemandedElts.reverseBits(), Known,
                         Depth + 1, Q);
        break;
        // for min/max/and/or reduce, any bit common to each element in the
        // input vec is set in the output.
      case Intrinsic::vector_reduce_and:
      case Intrinsic::vector_reduce_or:
      case Intrinsic::vector_reduce_umax:
      case Intrinsic::vector_reduce_umin:
      case Intrinsic::vector_reduce_smax:
      case Intrinsic::vector_reduce_smin:
        computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
        break;
      case Intrinsic::vector_reduce_xor: {
        computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
        // The zeros common to all vecs are zero in the output.
        // If the number of elements is odd, then the common ones remain. If the
        // number of elements is even, then the common ones becomes zeros.
        auto *VecTy = cast<VectorType>(I->getOperand(0)->getType());
        // Even, so the ones become zeros.
        bool EvenCnt = VecTy->getElementCount().isKnownEven();
        if (EvenCnt)
          Known.Zero |= Known.One;
        // Maybe even element count so need to clear ones.
        if (VecTy->isScalableTy() || EvenCnt)
          Known.One.clearAllBits();
        break;
      }
      case Intrinsic::umin:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::umin(Known, Known2);
        break;
      case Intrinsic::umax:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::umax(Known, Known2);
        break;
      case Intrinsic::smin:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::smin(Known, Known2);
        break;
      case Intrinsic::smax:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::smax(Known, Known2);
        break;
      case Intrinsic::ptrmask: {
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);

        const Value *Mask = I->getOperand(1);
        Known2 = KnownBits(Mask->getType()->getScalarSizeInBits());
        computeKnownBits(Mask, DemandedElts, Known2, Depth + 1, Q);
        // TODO: 1-extend would be more precise.
        Known &= Known2.anyextOrTrunc(BitWidth);
        break;
      }
      case Intrinsic::x86_sse2_pmulh_w:
      case Intrinsic::x86_avx2_pmulh_w:
      case Intrinsic::x86_avx512_pmulh_w_512:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::mulhs(Known, Known2);
        break;
      case Intrinsic::x86_sse2_pmulhu_w:
      case Intrinsic::x86_avx2_pmulhu_w:
      case Intrinsic::x86_avx512_pmulhu_w_512:
        computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth + 1, Q);
        computeKnownBits(I->getOperand(1), DemandedElts, Known2, Depth + 1, Q);
        Known = KnownBits::mulhu(Known, Known2);
        break;
      case Intrinsic::x86_sse42_crc32_64_64:
        Known.Zero.setBitsFrom(32);
        break;
      case Intrinsic::x86_ssse3_phadd_d_128:
      case Intrinsic::x86_ssse3_phadd_w_128:
      case Intrinsic::x86_avx2_phadd_d:
      case Intrinsic::x86_avx2_phadd_w: {
        Known = computeKnownBitsForHorizontalOperation(
            I, DemandedElts, Depth, Q,
            [](const KnownBits &KnownLHS, const KnownBits &KnownRHS) {
              return KnownBits::computeForAddSub(/*Add=*/true, /*NSW=*/false,
                                                 /*NUW=*/false, KnownLHS,
                                                 KnownRHS);
            });
        break;
      }
      case Intrinsic::x86_ssse3_phadd_sw_128:
      case Intrinsic::x86_avx2_phadd_sw: {
        Known = computeKnownBitsForHorizontalOperation(I, DemandedElts, Depth,
                                                       Q, KnownBits::sadd_sat);
        break;
      }
      case Intrinsic::x86_ssse3_phsub_d_128:
      case Intrinsic::x86_ssse3_phsub_w_128:
      case Intrinsic::x86_avx2_phsub_d:
      case Intrinsic::x86_avx2_phsub_w: {
        Known = computeKnownBitsForHorizontalOperation(
            I, DemandedElts, Depth, Q,
            [](const KnownBits &KnownLHS, const KnownBits &KnownRHS) {
              return KnownBits::computeForAddSub(/*Add=*/false, /*NSW=*/false,
                                                 /*NUW=*/false, KnownLHS,
                                                 KnownRHS);
            });
        break;
      }
      case Intrinsic::x86_ssse3_phsub_sw_128:
      case Intrinsic::x86_avx2_phsub_sw: {
        Known = computeKnownBitsForHorizontalOperation(I, DemandedElts, Depth,
                                                       Q, KnownBits::ssub_sat);
        break;
      }
      case Intrinsic::riscv_vsetvli:
      case Intrinsic::riscv_vsetvlimax: {
        bool HasAVL = II->getIntrinsicID() == Intrinsic::riscv_vsetvli;
        const ConstantRange Range = getVScaleRange(II->getFunction(), BitWidth);
        uint64_t SEW = RISCVVType::decodeVSEW(
            cast<ConstantInt>(II->getArgOperand(HasAVL))->getZExtValue());
        RISCVII::VLMUL VLMUL = static_cast<RISCVII::VLMUL>(
            cast<ConstantInt>(II->getArgOperand(1 + HasAVL))->getZExtValue());
        uint64_t MaxVLEN =
            Range.getUnsignedMax().getZExtValue() * RISCV::RVVBitsPerBlock;
        uint64_t MaxVL = MaxVLEN / RISCVVType::getSEWLMULRatio(SEW, VLMUL);

        // Result of vsetvli must be not larger than AVL.
        if (HasAVL)
          if (auto *CI = dyn_cast<ConstantInt>(II->getArgOperand(0)))
            MaxVL = std::min(MaxVL, CI->getZExtValue());

        unsigned KnownZeroFirstBit = Log2_32(MaxVL) + 1;
        if (BitWidth > KnownZeroFirstBit)
          Known.Zero.setBitsFrom(KnownZeroFirstBit);
        break;
      }
      case Intrinsic::vscale: {
        if (!II->getParent() || !II->getFunction())
          break;

        Known = getVScaleRange(II->getFunction(), BitWidth).toKnownBits();
        break;
      }
      }
    }
    break;
  }
  case Instruction::ShuffleVector: {
    auto *Shuf = dyn_cast<ShuffleVectorInst>(I);
    // FIXME: Do we need to handle ConstantExpr involving shufflevectors?
    if (!Shuf) {
      Known.resetAll();
      return;
    }
    // For undef elements, we don't know anything about the common state of
    // the shuffle result.
    APInt DemandedLHS, DemandedRHS;
    if (!getShuffleDemandedElts(Shuf, DemandedElts, DemandedLHS, DemandedRHS)) {
      Known.resetAll();
      return;
    }
    Known.One.setAllBits();
    Known.Zero.setAllBits();
    if (!!DemandedLHS) {
      const Value *LHS = Shuf->getOperand(0);
      computeKnownBits(LHS, DemandedLHS, Known, Depth + 1, Q);
      // If we don't know any bits, early out.
      if (Known.isUnknown())
        break;
    }
    if (!!DemandedRHS) {
      const Value *RHS = Shuf->getOperand(1);
      computeKnownBits(RHS, DemandedRHS, Known2, Depth + 1, Q);
      Known = Known.intersectWith(Known2);
    }
    break;
  }
  case Instruction::InsertElement: {
    if (isa<ScalableVectorType>(I->getType())) {
      Known.resetAll();
      return;
    }
    const Value *Vec = I->getOperand(0);
    const Value *Elt = I->getOperand(1);
    auto *CIdx = dyn_cast<ConstantInt>(I->getOperand(2));
    unsigned NumElts = DemandedElts.getBitWidth();
    APInt DemandedVecElts = DemandedElts;
    bool NeedsElt = true;
    // If we know the index we are inserting too, clear it from Vec check.
    if (CIdx && CIdx->getValue().ult(NumElts)) {
      DemandedVecElts.clearBit(CIdx->getZExtValue());
      NeedsElt = DemandedElts[CIdx->getZExtValue()];
    }

    Known.One.setAllBits();
    Known.Zero.setAllBits();
    if (NeedsElt) {
      computeKnownBits(Elt, Known, Depth + 1, Q);
      // If we don't know any bits, early out.
      if (Known.isUnknown())
        break;
    }

    if (!DemandedVecElts.isZero()) {
      computeKnownBits(Vec, DemandedVecElts, Known2, Depth + 1, Q);
      Known = Known.intersectWith(Known2);
    }
    break;
  }
  case Instruction::ExtractElement: {
    // Look through extract element. If the index is non-constant or
    // out-of-range demand all elements, otherwise just the extracted element.
    const Value *Vec = I->getOperand(0);
    const Value *Idx = I->getOperand(1);
    auto *CIdx = dyn_cast<ConstantInt>(Idx);
    if (isa<ScalableVectorType>(Vec->getType())) {
      // FIXME: there's probably *something* we can do with scalable vectors
      Known.resetAll();
      break;
    }
    unsigned NumElts = cast<FixedVectorType>(Vec->getType())->getNumElements();
    APInt DemandedVecElts = APInt::getAllOnes(NumElts);
    if (CIdx && CIdx->getValue().ult(NumElts))
      DemandedVecElts = APInt::getOneBitSet(NumElts, CIdx->getZExtValue());
    computeKnownBits(Vec, DemandedVecElts, Known, Depth + 1, Q);
    break;
  }
  case Instruction::ExtractValue:
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I->getOperand(0))) {
      const ExtractValueInst *EVI = cast<ExtractValueInst>(I);
      if (EVI->getNumIndices() != 1) break;
      if (EVI->getIndices()[0] == 0) {
        switch (II->getIntrinsicID()) {
        default: break;
        case Intrinsic::uadd_with_overflow:
        case Intrinsic::sadd_with_overflow:
          computeKnownBitsAddSub(
              true, II->getArgOperand(0), II->getArgOperand(1), /*NSW=*/false,
              /* NUW=*/false, DemandedElts, Known, Known2, Depth, Q);
          break;
        case Intrinsic::usub_with_overflow:
        case Intrinsic::ssub_with_overflow:
          computeKnownBitsAddSub(
              false, II->getArgOperand(0), II->getArgOperand(1), /*NSW=*/false,
              /* NUW=*/false, DemandedElts, Known, Known2, Depth, Q);
          break;
        case Intrinsic::umul_with_overflow:
        case Intrinsic::smul_with_overflow:
          computeKnownBitsMul(II->getArgOperand(0), II->getArgOperand(1), false,
                              DemandedElts, Known, Known2, Depth, Q);
          break;
        }
      }
    }
    break;
  case Instruction::Freeze:
    if (isGuaranteedNotToBePoison(I->getOperand(0), Q.AC, Q.CxtI, Q.DT,
                                  Depth + 1))
      computeKnownBits(I->getOperand(0), Known, Depth + 1, Q);
    break;
  }
}

/// Determine which bits of V are known to be either zero or one and return
/// them.
KnownBits llvm::computeKnownBits(const Value *V, const APInt &DemandedElts,
                                 unsigned Depth, const SimplifyQuery &Q) {
  KnownBits Known(getBitWidth(V->getType(), Q.DL));
  ::computeKnownBits(V, DemandedElts, Known, Depth, Q);
  return Known;
}

/// Determine which bits of V are known to be either zero or one and return
/// them.
KnownBits llvm::computeKnownBits(const Value *V, unsigned Depth,
                                 const SimplifyQuery &Q) {
  KnownBits Known(getBitWidth(V->getType(), Q.DL));
  computeKnownBits(V, Known, Depth, Q);
  return Known;
}

/// Determine which bits of V are known to be either zero or one and return
/// them in the Known bit set.
///
/// NOTE: we cannot consider 'undef' to be "IsZero" here.  The problem is that
/// we cannot optimize based on the assumption that it is zero without changing
/// it to be an explicit zero.  If we don't change it to zero, other code could
/// optimized based on the contradictory assumption that it is non-zero.
/// Because instcombine aggressively folds operations with undef args anyway,
/// this won't lose us code quality.
///
/// This function is defined on values with integer type, values with pointer
/// type, and vectors of integers.  In the case
/// where V is a vector, known zero, and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the demanded elements in the vector specified by DemandedElts.
void computeKnownBits(const Value *V, const APInt &DemandedElts,
                      KnownBits &Known, unsigned Depth,
                      const SimplifyQuery &Q) {
  if (!DemandedElts) {
    // No demanded elts, better to assume we don't know anything.
    Known.resetAll();
    return;
  }

  assert(V && "No Value?");
  assert(Depth <= MaxAnalysisRecursionDepth && "Limit Search Depth");

#ifndef NDEBUG
  Type *Ty = V->getType();
  unsigned BitWidth = Known.getBitWidth();

  assert((Ty->isIntOrIntVectorTy(BitWidth) || Ty->isPtrOrPtrVectorTy()) &&
         "Not integer or pointer type!");

  if (auto *FVTy = dyn_cast<FixedVectorType>(Ty)) {
    assert(
        FVTy->getNumElements() == DemandedElts.getBitWidth() &&
        "DemandedElt width should equal the fixed vector number of elements");
  } else {
    assert(DemandedElts == APInt(1, 1) &&
           "DemandedElt width should be 1 for scalars or scalable vectors");
  }

  Type *ScalarTy = Ty->getScalarType();
  if (ScalarTy->isPointerTy()) {
    assert(BitWidth == Q.DL.getPointerTypeSizeInBits(ScalarTy) &&
           "V and Known should have same BitWidth");
  } else {
    assert(BitWidth == Q.DL.getTypeSizeInBits(ScalarTy) &&
           "V and Known should have same BitWidth");
  }
#endif

  const APInt *C;
  if (match(V, m_APInt(C))) {
    // We know all of the bits for a scalar constant or a splat vector constant!
    Known = KnownBits::makeConstant(*C);
    return;
  }
  // Null and aggregate-zero are all-zeros.
  if (isa<ConstantPointerNull>(V) || isa<ConstantAggregateZero>(V)) {
    Known.setAllZero();
    return;
  }
  // Handle a constant vector by taking the intersection of the known bits of
  // each element.
  if (const ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(V)) {
    assert(!isa<ScalableVectorType>(V->getType()));
    // We know that CDV must be a vector of integers. Take the intersection of
    // each element.
    Known.Zero.setAllBits(); Known.One.setAllBits();
    for (unsigned i = 0, e = CDV->getNumElements(); i != e; ++i) {
      if (!DemandedElts[i])
        continue;
      APInt Elt = CDV->getElementAsAPInt(i);
      Known.Zero &= ~Elt;
      Known.One &= Elt;
    }
    if (Known.hasConflict())
      Known.resetAll();
    return;
  }

  if (const auto *CV = dyn_cast<ConstantVector>(V)) {
    assert(!isa<ScalableVectorType>(V->getType()));
    // We know that CV must be a vector of integers. Take the intersection of
    // each element.
    Known.Zero.setAllBits(); Known.One.setAllBits();
    for (unsigned i = 0, e = CV->getNumOperands(); i != e; ++i) {
      if (!DemandedElts[i])
        continue;
      Constant *Element = CV->getAggregateElement(i);
      if (isa<PoisonValue>(Element))
        continue;
      auto *ElementCI = dyn_cast_or_null<ConstantInt>(Element);
      if (!ElementCI) {
        Known.resetAll();
        return;
      }
      const APInt &Elt = ElementCI->getValue();
      Known.Zero &= ~Elt;
      Known.One &= Elt;
    }
    if (Known.hasConflict())
      Known.resetAll();
    return;
  }

  // Start out not knowing anything.
  Known.resetAll();

  // We can't imply anything about undefs.
  if (isa<UndefValue>(V))
    return;

  // There's no point in looking through other users of ConstantData for
  // assumptions.  Confirm that we've handled them all.
  assert(!isa<ConstantData>(V) && "Unhandled constant data!");

  if (const auto *A = dyn_cast<Argument>(V))
    if (std::optional<ConstantRange> Range = A->getRange())
      Known = Range->toKnownBits();

  // All recursive calls that increase depth must come after this.
  if (Depth == MaxAnalysisRecursionDepth)
    return;

  // A weak GlobalAlias is totally unknown. A non-weak GlobalAlias has
  // the bits of its aliasee.
  if (const GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
    if (!GA->isInterposable())
      computeKnownBits(GA->getAliasee(), Known, Depth + 1, Q);
    return;
  }

  if (const Operator *I = dyn_cast<Operator>(V))
    computeKnownBitsFromOperator(I, DemandedElts, Known, Depth, Q);
  else if (const GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    if (std::optional<ConstantRange> CR = GV->getAbsoluteSymbolRange())
      Known = CR->toKnownBits();
  }

  // Aligned pointers have trailing zeros - refine Known.Zero set
  if (isa<PointerType>(V->getType())) {
    Align Alignment = V->getPointerAlignment(Q.DL);
    Known.Zero.setLowBits(Log2(Alignment));
  }

  // computeKnownBitsFromContext strictly refines Known.
  // Therefore, we run them after computeKnownBitsFromOperator.

  // Check whether we can determine known bits from context such as assumes.
  computeKnownBitsFromContext(V, Known, Depth, Q);
}

/// Try to detect a recurrence that the value of the induction variable is
/// always a power of two (or zero).
static bool isPowerOfTwoRecurrence(const PHINode *PN, bool OrZero,
                                   unsigned Depth, SimplifyQuery &Q) {
  BinaryOperator *BO = nullptr;
  Value *Start = nullptr, *Step = nullptr;
  if (!matchSimpleRecurrence(PN, BO, Start, Step))
    return false;

  // Initial value must be a power of two.
  for (const Use &U : PN->operands()) {
    if (U.get() == Start) {
      // Initial value comes from a different BB, need to adjust context
      // instruction for analysis.
      Q.CxtI = PN->getIncomingBlock(U)->getTerminator();
      if (!isKnownToBeAPowerOfTwo(Start, OrZero, Depth, Q))
        return false;
    }
  }

  // Except for Mul, the induction variable must be on the left side of the
  // increment expression, otherwise its value can be arbitrary.
  if (BO->getOpcode() != Instruction::Mul && BO->getOperand(1) != Step)
    return false;

  Q.CxtI = BO->getParent()->getTerminator();
  switch (BO->getOpcode()) {
  case Instruction::Mul:
    // Power of two is closed under multiplication.
    return (OrZero || Q.IIQ.hasNoUnsignedWrap(BO) ||
            Q.IIQ.hasNoSignedWrap(BO)) &&
           isKnownToBeAPowerOfTwo(Step, OrZero, Depth, Q);
  case Instruction::SDiv:
    // Start value must not be signmask for signed division, so simply being a
    // power of two is not sufficient, and it has to be a constant.
    if (!match(Start, m_Power2()) || match(Start, m_SignMask()))
      return false;
    [[fallthrough]];
  case Instruction::UDiv:
    // Divisor must be a power of two.
    // If OrZero is false, cannot guarantee induction variable is non-zero after
    // division, same for Shr, unless it is exact division.
    return (OrZero || Q.IIQ.isExact(BO)) &&
           isKnownToBeAPowerOfTwo(Step, false, Depth, Q);
  case Instruction::Shl:
    return OrZero || Q.IIQ.hasNoUnsignedWrap(BO) || Q.IIQ.hasNoSignedWrap(BO);
  case Instruction::AShr:
    if (!match(Start, m_Power2()) || match(Start, m_SignMask()))
      return false;
    [[fallthrough]];
  case Instruction::LShr:
    return OrZero || Q.IIQ.isExact(BO);
  default:
    return false;
  }
}

/// Return true if the given value is known to have exactly one
/// bit set when defined. For vectors return true if every element is known to
/// be a power of two when defined. Supports values with integer or pointer
/// types and vectors of integers.
bool isKnownToBeAPowerOfTwo(const Value *V, bool OrZero, unsigned Depth,
                            const SimplifyQuery &Q) {
  assert(Depth <= MaxAnalysisRecursionDepth && "Limit Search Depth");

  if (isa<Constant>(V))
    return OrZero ? match(V, m_Power2OrZero()) : match(V, m_Power2());

  // i1 is by definition a power of 2 or zero.
  if (OrZero && V->getType()->getScalarSizeInBits() == 1)
    return true;

  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  if (Q.CxtI && match(V, m_VScale())) {
    const Function *F = Q.CxtI->getFunction();
    // The vscale_range indicates vscale is a power-of-two.
    return F->hasFnAttribute(Attribute::VScaleRange);
  }

  // 1 << X is clearly a power of two if the one is not shifted off the end.  If
  // it is shifted off the end then the result is undefined.
  if (match(I, m_Shl(m_One(), m_Value())))
    return true;

  // (signmask) >>l X is clearly a power of two if the one is not shifted off
  // the bottom.  If it is shifted off the bottom then the result is undefined.
  if (match(I, m_LShr(m_SignMask(), m_Value())))
    return true;

  // The remaining tests are all recursive, so bail out if we hit the limit.
  if (Depth++ == MaxAnalysisRecursionDepth)
    return false;

  switch (I->getOpcode()) {
  case Instruction::ZExt:
    return isKnownToBeAPowerOfTwo(I->getOperand(0), OrZero, Depth, Q);
  case Instruction::Trunc:
    return OrZero && isKnownToBeAPowerOfTwo(I->getOperand(0), OrZero, Depth, Q);
  case Instruction::Shl:
    if (OrZero || Q.IIQ.hasNoUnsignedWrap(I) || Q.IIQ.hasNoSignedWrap(I))
      return isKnownToBeAPowerOfTwo(I->getOperand(0), OrZero, Depth, Q);
    return false;
  case Instruction::LShr:
    if (OrZero || Q.IIQ.isExact(cast<BinaryOperator>(I)))
      return isKnownToBeAPowerOfTwo(I->getOperand(0), OrZero, Depth, Q);
    return false;
  case Instruction::UDiv:
    if (Q.IIQ.isExact(cast<BinaryOperator>(I)))
      return isKnownToBeAPowerOfTwo(I->getOperand(0), OrZero, Depth, Q);
    return false;
  case Instruction::Mul:
    return isKnownToBeAPowerOfTwo(I->getOperand(1), OrZero, Depth, Q) &&
           isKnownToBeAPowerOfTwo(I->getOperand(0), OrZero, Depth, Q) &&
           (OrZero || isKnownNonZero(I, Q, Depth));
  case Instruction::And:
    // A power of two and'd with anything is a power of two or zero.
    if (OrZero &&
        (isKnownToBeAPowerOfTwo(I->getOperand(1), /*OrZero*/ true, Depth, Q) ||
         isKnownToBeAPowerOfTwo(I->getOperand(0), /*OrZero*/ true, Depth, Q)))
      return true;
    // X & (-X) is always a power of two or zero.
    if (match(I->getOperand(0), m_Neg(m_Specific(I->getOperand(1)))) ||
        match(I->getOperand(1), m_Neg(m_Specific(I->getOperand(0)))))
      return OrZero || isKnownNonZero(I->getOperand(0), Q, Depth);
    return false;
  case Instruction::Add: {
    // Adding a power-of-two or zero to the same power-of-two or zero yields
    // either the original power-of-two, a larger power-of-two or zero.
    const OverflowingBinaryOperator *VOBO = cast<OverflowingBinaryOperator>(V);
    if (OrZero || Q.IIQ.hasNoUnsignedWrap(VOBO) ||
        Q.IIQ.hasNoSignedWrap(VOBO)) {
      if (match(I->getOperand(0),
                m_c_And(m_Specific(I->getOperand(1)), m_Value())) &&
          isKnownToBeAPowerOfTwo(I->getOperand(1), OrZero, Depth, Q))
        return true;
      if (match(I->getOperand(1),
                m_c_And(m_Specific(I->getOperand(0)), m_Value())) &&
          isKnownToBeAPowerOfTwo(I->getOperand(0), OrZero, Depth, Q))
        return true;

      unsigned BitWidth = V->getType()->getScalarSizeInBits();
      KnownBits LHSBits(BitWidth);
      computeKnownBits(I->getOperand(0), LHSBits, Depth, Q);

      KnownBits RHSBits(BitWidth);
      computeKnownBits(I->getOperand(1), RHSBits, Depth, Q);
      // If i8 V is a power of two or zero:
      //  ZeroBits: 1 1 1 0 1 1 1 1
      // ~ZeroBits: 0 0 0 1 0 0 0 0
      if ((~(LHSBits.Zero & RHSBits.Zero)).isPowerOf2())
        // If OrZero isn't set, we cannot give back a zero result.
        // Make sure either the LHS or RHS has a bit set.
        if (OrZero || RHSBits.One.getBoolValue() || LHSBits.One.getBoolValue())
          return true;
    }

    // LShr(UINT_MAX, Y) + 1 is a power of two (if add is nuw) or zero.
    if (OrZero || Q.IIQ.hasNoUnsignedWrap(VOBO))
      if (match(I, m_Add(m_LShr(m_AllOnes(), m_Value()), m_One())))
        return true;
    return false;
  }
  case Instruction::Select:
    return isKnownToBeAPowerOfTwo(I->getOperand(1), OrZero, Depth, Q) &&
           isKnownToBeAPowerOfTwo(I->getOperand(2), OrZero, Depth, Q);
  case Instruction::PHI: {
    // A PHI node is power of two if all incoming values are power of two, or if
    // it is an induction variable where in each step its value is a power of
    // two.
    auto *PN = cast<PHINode>(I);
    SimplifyQuery RecQ = Q.getWithoutCondContext();

    // Check if it is an induction variable and always power of two.
    if (isPowerOfTwoRecurrence(PN, OrZero, Depth, RecQ))
      return true;

    // Recursively check all incoming values. Limit recursion to 2 levels, so
    // that search complexity is limited to number of operands^2.
    unsigned NewDepth = std::max(Depth, MaxAnalysisRecursionDepth - 1);
    return llvm::all_of(PN->operands(), [&](const Use &U) {
      // Value is power of 2 if it is coming from PHI node itself by induction.
      if (U.get() == PN)
        return true;

      // Change the context instruction to the incoming block where it is
      // evaluated.
      RecQ.CxtI = PN->getIncomingBlock(U)->getTerminator();
      return isKnownToBeAPowerOfTwo(U.get(), OrZero, NewDepth, RecQ);
    });
  }
  case Instruction::Invoke:
  case Instruction::Call: {
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::umax:
      case Intrinsic::smax:
      case Intrinsic::umin:
      case Intrinsic::smin:
        return isKnownToBeAPowerOfTwo(II->getArgOperand(1), OrZero, Depth, Q) &&
               isKnownToBeAPowerOfTwo(II->getArgOperand(0), OrZero, Depth, Q);
      // bswap/bitreverse just move around bits, but don't change any 1s/0s
      // thus dont change pow2/non-pow2 status.
      case Intrinsic::bitreverse:
      case Intrinsic::bswap:
        return isKnownToBeAPowerOfTwo(II->getArgOperand(0), OrZero, Depth, Q);
      case Intrinsic::fshr:
      case Intrinsic::fshl:
        // If Op0 == Op1, this is a rotate. is_pow2(rotate(x, y)) == is_pow2(x)
        if (II->getArgOperand(0) == II->getArgOperand(1))
          return isKnownToBeAPowerOfTwo(II->getArgOperand(0), OrZero, Depth, Q);
        break;
      default:
        break;
      }
    }
    return false;
  }
  default:
    return false;
  }
}

/// Test whether a GEP's result is known to be non-null.
///
/// Uses properties inherent in a GEP to try to determine whether it is known
/// to be non-null.
///
/// Currently this routine does not support vector GEPs.
static bool isGEPKnownNonNull(const GEPOperator *GEP, unsigned Depth,
                              const SimplifyQuery &Q) {
  const Function *F = nullptr;
  if (const Instruction *I = dyn_cast<Instruction>(GEP))
    F = I->getFunction();

  // If the gep is nuw or inbounds with invalid null pointer, then the GEP
  // may be null iff the base pointer is null and the offset is zero.
  if (!GEP->hasNoUnsignedWrap() &&
      !(GEP->isInBounds() &&
        !NullPointerIsDefined(F, GEP->getPointerAddressSpace())))
    return false;

  // FIXME: Support vector-GEPs.
  assert(GEP->getType()->isPointerTy() && "We only support plain pointer GEP");

  // If the base pointer is non-null, we cannot walk to a null address with an
  // inbounds GEP in address space zero.
  if (isKnownNonZero(GEP->getPointerOperand(), Q, Depth))
    return true;

  // Walk the GEP operands and see if any operand introduces a non-zero offset.
  // If so, then the GEP cannot produce a null pointer, as doing so would
  // inherently violate the inbounds contract within address space zero.
  for (gep_type_iterator GTI = gep_type_begin(GEP), GTE = gep_type_end(GEP);
       GTI != GTE; ++GTI) {
    // Struct types are easy -- they must always be indexed by a constant.
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      ConstantInt *OpC = cast<ConstantInt>(GTI.getOperand());
      unsigned ElementIdx = OpC->getZExtValue();
      const StructLayout *SL = Q.DL.getStructLayout(STy);
      uint64_t ElementOffset = SL->getElementOffset(ElementIdx);
      if (ElementOffset > 0)
        return true;
      continue;
    }

    // If we have a zero-sized type, the index doesn't matter. Keep looping.
    if (GTI.getSequentialElementStride(Q.DL).isZero())
      continue;

    // Fast path the constant operand case both for efficiency and so we don't
    // increment Depth when just zipping down an all-constant GEP.
    if (ConstantInt *OpC = dyn_cast<ConstantInt>(GTI.getOperand())) {
      if (!OpC->isZero())
        return true;
      continue;
    }

    // We post-increment Depth here because while isKnownNonZero increments it
    // as well, when we pop back up that increment won't persist. We don't want
    // to recurse 10k times just because we have 10k GEP operands. We don't
    // bail completely out because we want to handle constant GEPs regardless
    // of depth.
    if (Depth++ >= MaxAnalysisRecursionDepth)
      continue;

    if (isKnownNonZero(GTI.getOperand(), Q, Depth))
      return true;
  }

  return false;
}

static bool isKnownNonNullFromDominatingCondition(const Value *V,
                                                  const Instruction *CtxI,
                                                  const DominatorTree *DT) {
  assert(!isa<Constant>(V) && "Called for constant?");

  if (!CtxI || !DT)
    return false;

  unsigned NumUsesExplored = 0;
  for (const auto *U : V->users()) {
    // Avoid massive lists
    if (NumUsesExplored >= DomConditionsMaxUses)
      break;
    NumUsesExplored++;

    // If the value is used as an argument to a call or invoke, then argument
    // attributes may provide an answer about null-ness.
    if (const auto *CB = dyn_cast<CallBase>(U))
      if (auto *CalledFunc = CB->getCalledFunction())
        for (const Argument &Arg : CalledFunc->args())
          if (CB->getArgOperand(Arg.getArgNo()) == V &&
              Arg.hasNonNullAttr(/* AllowUndefOrPoison */ false) &&
              DT->dominates(CB, CtxI))
            return true;

    // If the value is used as a load/store, then the pointer must be non null.
    if (V == getLoadStorePointerOperand(U)) {
      const Instruction *I = cast<Instruction>(U);
      if (!NullPointerIsDefined(I->getFunction(),
                                V->getType()->getPointerAddressSpace()) &&
          DT->dominates(I, CtxI))
        return true;
    }

    if ((match(U, m_IDiv(m_Value(), m_Specific(V))) ||
         match(U, m_IRem(m_Value(), m_Specific(V)))) &&
        isValidAssumeForContext(cast<Instruction>(U), CtxI, DT))
      return true;

    // Consider only compare instructions uniquely controlling a branch
    Value *RHS;
    CmpInst::Predicate Pred;
    if (!match(U, m_c_ICmp(Pred, m_Specific(V), m_Value(RHS))))
      continue;

    bool NonNullIfTrue;
    if (cmpExcludesZero(Pred, RHS))
      NonNullIfTrue = true;
    else if (cmpExcludesZero(CmpInst::getInversePredicate(Pred), RHS))
      NonNullIfTrue = false;
    else
      continue;

    SmallVector<const User *, 4> WorkList;
    SmallPtrSet<const User *, 4> Visited;
    for (const auto *CmpU : U->users()) {
      assert(WorkList.empty() && "Should be!");
      if (Visited.insert(CmpU).second)
        WorkList.push_back(CmpU);

      while (!WorkList.empty()) {
        auto *Curr = WorkList.pop_back_val();

        // If a user is an AND, add all its users to the work list. We only
        // propagate "pred != null" condition through AND because it is only
        // correct to assume that all conditions of AND are met in true branch.
        // TODO: Support similar logic of OR and EQ predicate?
        if (NonNullIfTrue)
          if (match(Curr, m_LogicalAnd(m_Value(), m_Value()))) {
            for (const auto *CurrU : Curr->users())
              if (Visited.insert(CurrU).second)
                WorkList.push_back(CurrU);
            continue;
          }

        if (const BranchInst *BI = dyn_cast<BranchInst>(Curr)) {
          assert(BI->isConditional() && "uses a comparison!");

          BasicBlock *NonNullSuccessor =
              BI->getSuccessor(NonNullIfTrue ? 0 : 1);
          BasicBlockEdge Edge(BI->getParent(), NonNullSuccessor);
          if (Edge.isSingleEdge() && DT->dominates(Edge, CtxI->getParent()))
            return true;
        } else if (NonNullIfTrue && isGuard(Curr) &&
                   DT->dominates(cast<Instruction>(Curr), CtxI)) {
          return true;
        }
      }
    }
  }

  return false;
}

/// Does the 'Range' metadata (which must be a valid MD_range operand list)
/// ensure that the value it's attached to is never Value?  'RangeType' is
/// is the type of the value described by the range.
static bool rangeMetadataExcludesValue(const MDNode* Ranges, const APInt& Value) {
  const unsigned NumRanges = Ranges->getNumOperands() / 2;
  assert(NumRanges >= 1);
  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Lower =
        mdconst::extract<ConstantInt>(Ranges->getOperand(2 * i + 0));
    ConstantInt *Upper =
        mdconst::extract<ConstantInt>(Ranges->getOperand(2 * i + 1));
    ConstantRange Range(Lower->getValue(), Upper->getValue());
    if (Range.contains(Value))
      return false;
  }
  return true;
}

/// Try to detect a recurrence that monotonically increases/decreases from a
/// non-zero starting value. These are common as induction variables.
static bool isNonZeroRecurrence(const PHINode *PN) {
  BinaryOperator *BO = nullptr;
  Value *Start = nullptr, *Step = nullptr;
  const APInt *StartC, *StepC;
  if (!matchSimpleRecurrence(PN, BO, Start, Step) ||
      !match(Start, m_APInt(StartC)) || StartC->isZero())
    return false;

  switch (BO->getOpcode()) {
  case Instruction::Add:
    // Starting from non-zero and stepping away from zero can never wrap back
    // to zero.
    return BO->hasNoUnsignedWrap() ||
           (BO->hasNoSignedWrap() && match(Step, m_APInt(StepC)) &&
            StartC->isNegative() == StepC->isNegative());
  case Instruction::Mul:
    return (BO->hasNoUnsignedWrap() || BO->hasNoSignedWrap()) &&
           match(Step, m_APInt(StepC)) && !StepC->isZero();
  case Instruction::Shl:
    return BO->hasNoUnsignedWrap() || BO->hasNoSignedWrap();
  case Instruction::AShr:
  case Instruction::LShr:
    return BO->isExact();
  default:
    return false;
  }
}

static bool matchOpWithOpEqZero(Value *Op0, Value *Op1) {
  ICmpInst::Predicate Pred;
  return (match(Op0, m_ZExtOrSExt(m_ICmp(Pred, m_Specific(Op1), m_Zero()))) ||
          match(Op1, m_ZExtOrSExt(m_ICmp(Pred, m_Specific(Op0), m_Zero())))) &&
         Pred == ICmpInst::ICMP_EQ;
}

static bool isNonZeroAdd(const APInt &DemandedElts, unsigned Depth,
                         const SimplifyQuery &Q, unsigned BitWidth, Value *X,
                         Value *Y, bool NSW, bool NUW) {
  // (X + (X != 0)) is non zero
  if (matchOpWithOpEqZero(X, Y))
    return true;

  if (NUW)
    return isKnownNonZero(Y, DemandedElts, Q, Depth) ||
           isKnownNonZero(X, DemandedElts, Q, Depth);

  KnownBits XKnown = computeKnownBits(X, DemandedElts, Depth, Q);
  KnownBits YKnown = computeKnownBits(Y, DemandedElts, Depth, Q);

  // If X and Y are both non-negative (as signed values) then their sum is not
  // zero unless both X and Y are zero.
  if (XKnown.isNonNegative() && YKnown.isNonNegative())
    if (isKnownNonZero(Y, DemandedElts, Q, Depth) ||
        isKnownNonZero(X, DemandedElts, Q, Depth))
      return true;

  // If X and Y are both negative (as signed values) then their sum is not
  // zero unless both X and Y equal INT_MIN.
  if (XKnown.isNegative() && YKnown.isNegative()) {
    APInt Mask = APInt::getSignedMaxValue(BitWidth);
    // The sign bit of X is set.  If some other bit is set then X is not equal
    // to INT_MIN.
    if (XKnown.One.intersects(Mask))
      return true;
    // The sign bit of Y is set.  If some other bit is set then Y is not equal
    // to INT_MIN.
    if (YKnown.One.intersects(Mask))
      return true;
  }

  // The sum of a non-negative number and a power of two is not zero.
  if (XKnown.isNonNegative() &&
      isKnownToBeAPowerOfTwo(Y, /*OrZero*/ false, Depth, Q))
    return true;
  if (YKnown.isNonNegative() &&
      isKnownToBeAPowerOfTwo(X, /*OrZero*/ false, Depth, Q))
    return true;

  return KnownBits::computeForAddSub(/*Add=*/true, NSW, NUW, XKnown, YKnown)
      .isNonZero();
}

static bool isNonZeroSub(const APInt &DemandedElts, unsigned Depth,
                         const SimplifyQuery &Q, unsigned BitWidth, Value *X,
                         Value *Y) {
  // (X - (X != 0)) is non zero
  // ((X != 0) - X) is non zero
  if (matchOpWithOpEqZero(X, Y))
    return true;

  // TODO: Move this case into isKnownNonEqual().
  if (auto *C = dyn_cast<Constant>(X))
    if (C->isNullValue() && isKnownNonZero(Y, DemandedElts, Q, Depth))
      return true;

  return ::isKnownNonEqual(X, Y, DemandedElts, Depth, Q);
}

static bool isNonZeroMul(const APInt &DemandedElts, unsigned Depth,
                         const SimplifyQuery &Q, unsigned BitWidth, Value *X,
                         Value *Y, bool NSW, bool NUW) {
  // If X and Y are non-zero then so is X * Y as long as the multiplication
  // does not overflow.
  if (NSW || NUW)
    return isKnownNonZero(X, DemandedElts, Q, Depth) &&
           isKnownNonZero(Y, DemandedElts, Q, Depth);

  // If either X or Y is odd, then if the other is non-zero the result can't
  // be zero.
  KnownBits XKnown = computeKnownBits(X, DemandedElts, Depth, Q);
  if (XKnown.One[0])
    return isKnownNonZero(Y, DemandedElts, Q, Depth);

  KnownBits YKnown = computeKnownBits(Y, DemandedElts, Depth, Q);
  if (YKnown.One[0])
    return XKnown.isNonZero() || isKnownNonZero(X, DemandedElts, Q, Depth);

  // If there exists any subset of X (sX) and subset of Y (sY) s.t sX * sY is
  // non-zero, then X * Y is non-zero. We can find sX and sY by just taking
  // the lowest known One of X and Y. If they are non-zero, the result
  // must be non-zero. We can check if LSB(X) * LSB(Y) != 0 by doing
  // X.CountLeadingZeros + Y.CountLeadingZeros < BitWidth.
  return (XKnown.countMaxTrailingZeros() + YKnown.countMaxTrailingZeros()) <
         BitWidth;
}

static bool isNonZeroShift(const Operator *I, const APInt &DemandedElts,
                           unsigned Depth, const SimplifyQuery &Q,
                           const KnownBits &KnownVal) {
  auto ShiftOp = [&](const APInt &Lhs, const APInt &Rhs) {
    switch (I->getOpcode()) {
    case Instruction::Shl:
      return Lhs.shl(Rhs);
    case Instruction::LShr:
      return Lhs.lshr(Rhs);
    case Instruction::AShr:
      return Lhs.ashr(Rhs);
    default:
      llvm_unreachable("Unknown Shift Opcode");
    }
  };

  auto InvShiftOp = [&](const APInt &Lhs, const APInt &Rhs) {
    switch (I->getOpcode()) {
    case Instruction::Shl:
      return Lhs.lshr(Rhs);
    case Instruction::LShr:
    case Instruction::AShr:
      return Lhs.shl(Rhs);
    default:
      llvm_unreachable("Unknown Shift Opcode");
    }
  };

  if (KnownVal.isUnknown())
    return false;

  KnownBits KnownCnt =
      computeKnownBits(I->getOperand(1), DemandedElts, Depth, Q);
  APInt MaxShift = KnownCnt.getMaxValue();
  unsigned NumBits = KnownVal.getBitWidth();
  if (MaxShift.uge(NumBits))
    return false;

  if (!ShiftOp(KnownVal.One, MaxShift).isZero())
    return true;

  // If all of the bits shifted out are known to be zero, and Val is known
  // non-zero then at least one non-zero bit must remain.
  if (InvShiftOp(KnownVal.Zero, NumBits - MaxShift)
          .eq(InvShiftOp(APInt::getAllOnes(NumBits), NumBits - MaxShift)) &&
      isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth))
    return true;

  return false;
}

static bool isKnownNonZeroFromOperator(const Operator *I,
                                       const APInt &DemandedElts,
                                       unsigned Depth, const SimplifyQuery &Q) {
  unsigned BitWidth = getBitWidth(I->getType()->getScalarType(), Q.DL);
  switch (I->getOpcode()) {
  case Instruction::Alloca:
    // Alloca never returns null, malloc might.
    return I->getType()->getPointerAddressSpace() == 0;
  case Instruction::GetElementPtr:
    if (I->getType()->isPointerTy())
      return isGEPKnownNonNull(cast<GEPOperator>(I), Depth, Q);
    break;
  case Instruction::BitCast: {
    // We need to be a bit careful here. We can only peek through the bitcast
    // if the scalar size of elements in the operand are smaller than and a
    // multiple of the size they are casting too. Take three cases:
    //
    // 1) Unsafe:
    //        bitcast <2 x i16> %NonZero to <4 x i8>
    //
    //    %NonZero can have 2 non-zero i16 elements, but isKnownNonZero on a
    //    <4 x i8> requires that all 4 i8 elements be non-zero which isn't
    //    guranteed (imagine just sign bit set in the 2 i16 elements).
    //
    // 2) Unsafe:
    //        bitcast <4 x i3> %NonZero to <3 x i4>
    //
    //    Even though the scalar size of the src (`i3`) is smaller than the
    //    scalar size of the dst `i4`, because `i3` is not a multiple of `i4`
    //    its possible for the `3 x i4` elements to be zero because there are
    //    some elements in the destination that don't contain any full src
    //    element.
    //
    // 3) Safe:
    //        bitcast <4 x i8> %NonZero to <2 x i16>
    //
    //    This is always safe as non-zero in the 4 i8 elements implies
    //    non-zero in the combination of any two adjacent ones. Since i8 is a
    //    multiple of i16, each i16 is guranteed to have 2 full i8 elements.
    //    This all implies the 2 i16 elements are non-zero.
    Type *FromTy = I->getOperand(0)->getType();
    if ((FromTy->isIntOrIntVectorTy() || FromTy->isPtrOrPtrVectorTy()) &&
        (BitWidth % getBitWidth(FromTy->getScalarType(), Q.DL)) == 0)
      return isKnownNonZero(I->getOperand(0), Q, Depth);
  } break;
  case Instruction::IntToPtr:
    // Note that we have to take special care to avoid looking through
    // truncating casts, e.g., int2ptr/ptr2int with appropriate sizes, as well
    // as casts that can alter the value, e.g., AddrSpaceCasts.
    if (!isa<ScalableVectorType>(I->getType()) &&
        Q.DL.getTypeSizeInBits(I->getOperand(0)->getType()).getFixedValue() <=
            Q.DL.getTypeSizeInBits(I->getType()).getFixedValue())
      return isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth);
    break;
  case Instruction::PtrToInt:
    // Similar to int2ptr above, we can look through ptr2int here if the cast
    // is a no-op or an extend and not a truncate.
    if (!isa<ScalableVectorType>(I->getType()) &&
        Q.DL.getTypeSizeInBits(I->getOperand(0)->getType()).getFixedValue() <=
            Q.DL.getTypeSizeInBits(I->getType()).getFixedValue())
      return isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth);
    break;
  case Instruction::Trunc:
    // nuw/nsw trunc preserves zero/non-zero status of input.
    if (auto *TI = dyn_cast<TruncInst>(I))
      if (TI->hasNoSignedWrap() || TI->hasNoUnsignedWrap())
        return isKnownNonZero(TI->getOperand(0), DemandedElts, Q, Depth);
    break;

  case Instruction::Sub:
    return isNonZeroSub(DemandedElts, Depth, Q, BitWidth, I->getOperand(0),
                        I->getOperand(1));
  case Instruction::Xor:
    // (X ^ (X != 0)) is non zero
    if (matchOpWithOpEqZero(I->getOperand(0), I->getOperand(1)))
      return true;
    break;
  case Instruction::Or:
    // (X | (X != 0)) is non zero
    if (matchOpWithOpEqZero(I->getOperand(0), I->getOperand(1)))
      return true;
    // X | Y != 0 if X != 0 or Y != 0.
    return isKnownNonZero(I->getOperand(1), DemandedElts, Q, Depth) ||
           isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth);
  case Instruction::SExt:
  case Instruction::ZExt:
    // ext X != 0 if X != 0.
    return isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth);

  case Instruction::Shl: {
    // shl nsw/nuw can't remove any non-zero bits.
    const OverflowingBinaryOperator *BO = cast<OverflowingBinaryOperator>(I);
    if (Q.IIQ.hasNoUnsignedWrap(BO) || Q.IIQ.hasNoSignedWrap(BO))
      return isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth);

    // shl X, Y != 0 if X is odd.  Note that the value of the shift is undefined
    // if the lowest bit is shifted off the end.
    KnownBits Known(BitWidth);
    computeKnownBits(I->getOperand(0), DemandedElts, Known, Depth, Q);
    if (Known.One[0])
      return true;

    return isNonZeroShift(I, DemandedElts, Depth, Q, Known);
  }
  case Instruction::LShr:
  case Instruction::AShr: {
    // shr exact can only shift out zero bits.
    const PossiblyExactOperator *BO = cast<PossiblyExactOperator>(I);
    if (BO->isExact())
      return isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth);

    // shr X, Y != 0 if X is negative.  Note that the value of the shift is not
    // defined if the sign bit is shifted off the end.
    KnownBits Known =
        computeKnownBits(I->getOperand(0), DemandedElts, Depth, Q);
    if (Known.isNegative())
      return true;

    return isNonZeroShift(I, DemandedElts, Depth, Q, Known);
  }
  case Instruction::UDiv:
  case Instruction::SDiv: {
    // X / Y
    // div exact can only produce a zero if the dividend is zero.
    if (cast<PossiblyExactOperator>(I)->isExact())
      return isKnownNonZero(I->getOperand(0), DemandedElts, Q, Depth);

    KnownBits XKnown =
        computeKnownBits(I->getOperand(0), DemandedElts, Depth, Q);
    // If X is fully unknown we won't be able to figure anything out so don't
    // both computing knownbits for Y.
    if (XKnown.isUnknown())
      return false;

    KnownBits YKnown =
        computeKnownBits(I->getOperand(1), DemandedElts, Depth, Q);
    if (I->getOpcode() == Instruction::SDiv) {
      // For signed division need to compare abs value of the operands.
      XKnown = XKnown.abs(/*IntMinIsPoison*/ false);
      YKnown = YKnown.abs(/*IntMinIsPoison*/ false);
    }
    // If X u>= Y then div is non zero (0/0 is UB).
    std::optional<bool> XUgeY = KnownBits::uge(XKnown, YKnown);
    // If X is total unknown or X u< Y we won't be able to prove non-zero
    // with compute known bits so just return early.
    return XUgeY && *XUgeY;
  }
  case Instruction::Add: {
    // X + Y.

    // If Add has nuw wrap flag, then if either X or Y is non-zero the result is
    // non-zero.
    auto *BO = cast<OverflowingBinaryOperator>(I);
    return isNonZeroAdd(DemandedElts, Depth, Q, BitWidth, I->getOperand(0),
                        I->getOperand(1), Q.IIQ.hasNoSignedWrap(BO),
                        Q.IIQ.hasNoUnsignedWrap(BO));
  }
  case Instruction::Mul: {
    const OverflowingBinaryOperator *BO = cast<OverflowingBinaryOperator>(I);
    return isNonZeroMul(DemandedElts, Depth, Q, BitWidth, I->getOperand(0),
                        I->getOperand(1), Q.IIQ.hasNoSignedWrap(BO),
                        Q.IIQ.hasNoUnsignedWrap(BO));
  }
  case Instruction::Select: {
    // (C ? X : Y) != 0 if X != 0 and Y != 0.

    // First check if the arm is non-zero using `isKnownNonZero`. If that fails,
    // then see if the select condition implies the arm is non-zero. For example
    // (X != 0 ? X : Y), we know the true arm is non-zero as the `X` "return" is
    // dominated by `X != 0`.
    auto SelectArmIsNonZero = [&](bool IsTrueArm) {
      Value *Op;
      Op = IsTrueArm ? I->getOperand(1) : I->getOperand(2);
      // Op is trivially non-zero.
      if (isKnownNonZero(Op, DemandedElts, Q, Depth))
        return true;

      // The condition of the select dominates the true/false arm. Check if the
      // condition implies that a given arm is non-zero.
      Value *X;
      CmpInst::Predicate Pred;
      if (!match(I->getOperand(0), m_c_ICmp(Pred, m_Specific(Op), m_Value(X))))
        return false;

      if (!IsTrueArm)
        Pred = ICmpInst::getInversePredicate(Pred);

      return cmpExcludesZero(Pred, X);
    };

    if (SelectArmIsNonZero(/* IsTrueArm */ true) &&
        SelectArmIsNonZero(/* IsTrueArm */ false))
      return true;
    break;
  }
  case Instruction::PHI: {
    auto *PN = cast<PHINode>(I);
    if (Q.IIQ.UseInstrInfo && isNonZeroRecurrence(PN))
      return true;

    // Check if all incoming values are non-zero using recursion.
    SimplifyQuery RecQ = Q.getWithoutCondContext();
    unsigned NewDepth = std::max(Depth, MaxAnalysisRecursionDepth - 1);
    return llvm::all_of(PN->operands(), [&](const Use &U) {
      if (U.get() == PN)
        return true;
      RecQ.CxtI = PN->getIncomingBlock(U)->getTerminator();
      // Check if the branch on the phi excludes zero.
      ICmpInst::Predicate Pred;
      Value *X;
      BasicBlock *TrueSucc, *FalseSucc;
      if (match(RecQ.CxtI,
                m_Br(m_c_ICmp(Pred, m_Specific(U.get()), m_Value(X)),
                     m_BasicBlock(TrueSucc), m_BasicBlock(FalseSucc)))) {
        // Check for cases of duplicate successors.
        if ((TrueSucc == PN->getParent()) != (FalseSucc == PN->getParent())) {
          // If we're using the false successor, invert the predicate.
          if (FalseSucc == PN->getParent())
            Pred = CmpInst::getInversePredicate(Pred);
          if (cmpExcludesZero(Pred, X))
            return true;
        }
      }
      // Finally recurse on the edge and check it directly.
      return isKnownNonZero(U.get(), DemandedElts, RecQ, NewDepth);
    });
  }
  case Instruction::InsertElement: {
    if (isa<ScalableVectorType>(I->getType()))
      break;

    const Value *Vec = I->getOperand(0);
    const Value *Elt = I->getOperand(1);
    auto *CIdx = dyn_cast<ConstantInt>(I->getOperand(2));

    unsigned NumElts = DemandedElts.getBitWidth();
    APInt DemandedVecElts = DemandedElts;
    bool SkipElt = false;
    // If we know the index we are inserting too, clear it from Vec check.
    if (CIdx && CIdx->getValue().ult(NumElts)) {
      DemandedVecElts.clearBit(CIdx->getZExtValue());
      SkipElt = !DemandedElts[CIdx->getZExtValue()];
    }

    // Result is zero if Elt is non-zero and rest of the demanded elts in Vec
    // are non-zero.
    return (SkipElt || isKnownNonZero(Elt, Q, Depth)) &&
           (DemandedVecElts.isZero() ||
            isKnownNonZero(Vec, DemandedVecElts, Q, Depth));
  }
  case Instruction::ExtractElement:
    if (const auto *EEI = dyn_cast<ExtractElementInst>(I)) {
      const Value *Vec = EEI->getVectorOperand();
      const Value *Idx = EEI->getIndexOperand();
      auto *CIdx = dyn_cast<ConstantInt>(Idx);
      if (auto *VecTy = dyn_cast<FixedVectorType>(Vec->getType())) {
        unsigned NumElts = VecTy->getNumElements();
        APInt DemandedVecElts = APInt::getAllOnes(NumElts);
        if (CIdx && CIdx->getValue().ult(NumElts))
          DemandedVecElts = APInt::getOneBitSet(NumElts, CIdx->getZExtValue());
        return isKnownNonZero(Vec, DemandedVecElts, Q, Depth);
      }
    }
    break;
  case Instruction::ShuffleVector: {
    auto *Shuf = dyn_cast<ShuffleVectorInst>(I);
    if (!Shuf)
      break;
    APInt DemandedLHS, DemandedRHS;
    // For undef elements, we don't know anything about the common state of
    // the shuffle result.
    if (!getShuffleDemandedElts(Shuf, DemandedElts, DemandedLHS, DemandedRHS))
      break;
    // If demanded elements for both vecs are non-zero, the shuffle is non-zero.
    return (DemandedRHS.isZero() ||
            isKnownNonZero(Shuf->getOperand(1), DemandedRHS, Q, Depth)) &&
           (DemandedLHS.isZero() ||
            isKnownNonZero(Shuf->getOperand(0), DemandedLHS, Q, Depth));
  }
  case Instruction::Freeze:
    return isKnownNonZero(I->getOperand(0), Q, Depth) &&
           isGuaranteedNotToBePoison(I->getOperand(0), Q.AC, Q.CxtI, Q.DT,
                                     Depth);
  case Instruction::Load: {
    auto *LI = cast<LoadInst>(I);
    // A Load tagged with nonnull or dereferenceable with null pointer undefined
    // is never null.
    if (auto *PtrT = dyn_cast<PointerType>(I->getType())) {
      if (Q.IIQ.getMetadata(LI, LLVMContext::MD_nonnull) ||
          (Q.IIQ.getMetadata(LI, LLVMContext::MD_dereferenceable) &&
           !NullPointerIsDefined(LI->getFunction(), PtrT->getAddressSpace())))
        return true;
    } else if (MDNode *Ranges = Q.IIQ.getMetadata(LI, LLVMContext::MD_range)) {
      return rangeMetadataExcludesValue(Ranges, APInt::getZero(BitWidth));
    }

    // No need to fall through to computeKnownBits as range metadata is already
    // handled in isKnownNonZero.
    return false;
  }
  case Instruction::ExtractValue: {
    const WithOverflowInst *WO;
    if (match(I, m_ExtractValue<0>(m_WithOverflowInst(WO)))) {
      switch (WO->getBinaryOp()) {
      default:
        break;
      case Instruction::Add:
        return isNonZeroAdd(DemandedElts, Depth, Q, BitWidth,
                            WO->getArgOperand(0), WO->getArgOperand(1),
                            /*NSW=*/false,
                            /*NUW=*/false);
      case Instruction::Sub:
        return isNonZeroSub(DemandedElts, Depth, Q, BitWidth,
                            WO->getArgOperand(0), WO->getArgOperand(1));
      case Instruction::Mul:
        return isNonZeroMul(DemandedElts, Depth, Q, BitWidth,
                            WO->getArgOperand(0), WO->getArgOperand(1),
                            /*NSW=*/false, /*NUW=*/false);
        break;
      }
    }
    break;
  }
  case Instruction::Call:
  case Instruction::Invoke: {
    const auto *Call = cast<CallBase>(I);
    if (I->getType()->isPointerTy()) {
      if (Call->isReturnNonNull())
        return true;
      if (const auto *RP = getArgumentAliasingToReturnedPointer(Call, true))
        return isKnownNonZero(RP, Q, Depth);
    } else {
      if (MDNode *Ranges = Q.IIQ.getMetadata(Call, LLVMContext::MD_range))
        return rangeMetadataExcludesValue(Ranges, APInt::getZero(BitWidth));
      if (std::optional<ConstantRange> Range = Call->getRange()) {
        const APInt ZeroValue(Range->getBitWidth(), 0);
        if (!Range->contains(ZeroValue))
          return true;
      }
      if (const Value *RV = Call->getReturnedArgOperand())
        if (RV->getType() == I->getType() && isKnownNonZero(RV, Q, Depth))
          return true;
    }

    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::sshl_sat:
      case Intrinsic::ushl_sat:
      case Intrinsic::abs:
      case Intrinsic::bitreverse:
      case Intrinsic::bswap:
      case Intrinsic::ctpop:
        return isKnownNonZero(II->getArgOperand(0), DemandedElts, Q, Depth);
        // NB: We don't do usub_sat here as in any case we can prove its
        // non-zero, we will fold it to `sub nuw` in InstCombine.
      case Intrinsic::ssub_sat:
        return isNonZeroSub(DemandedElts, Depth, Q, BitWidth,
                            II->getArgOperand(0), II->getArgOperand(1));
      case Intrinsic::sadd_sat:
        return isNonZeroAdd(DemandedElts, Depth, Q, BitWidth,
                            II->getArgOperand(0), II->getArgOperand(1),
                            /*NSW=*/true, /* NUW=*/false);
        // Vec reverse preserves zero/non-zero status from input vec.
      case Intrinsic::vector_reverse:
        return isKnownNonZero(II->getArgOperand(0), DemandedElts.reverseBits(),
                              Q, Depth);
        // umin/smin/smax/smin/or of all non-zero elements is always non-zero.
      case Intrinsic::vector_reduce_or:
      case Intrinsic::vector_reduce_umax:
      case Intrinsic::vector_reduce_umin:
      case Intrinsic::vector_reduce_smax:
      case Intrinsic::vector_reduce_smin:
        return isKnownNonZero(II->getArgOperand(0), Q, Depth);
      case Intrinsic::umax:
      case Intrinsic::uadd_sat:
        // umax(X, (X != 0)) is non zero
        // X +usat (X != 0) is non zero
        if (matchOpWithOpEqZero(II->getArgOperand(0), II->getArgOperand(1)))
          return true;

        return isKnownNonZero(II->getArgOperand(1), DemandedElts, Q, Depth) ||
               isKnownNonZero(II->getArgOperand(0), DemandedElts, Q, Depth);
      case Intrinsic::smax: {
        // If either arg is strictly positive the result is non-zero. Otherwise
        // the result is non-zero if both ops are non-zero.
        auto IsNonZero = [&](Value *Op, std::optional<bool> &OpNonZero,
                             const KnownBits &OpKnown) {
          if (!OpNonZero.has_value())
            OpNonZero = OpKnown.isNonZero() ||
                        isKnownNonZero(Op, DemandedElts, Q, Depth);
          return *OpNonZero;
        };
        // Avoid re-computing isKnownNonZero.
        std::optional<bool> Op0NonZero, Op1NonZero;
        KnownBits Op1Known =
            computeKnownBits(II->getArgOperand(1), DemandedElts, Depth, Q);
        if (Op1Known.isNonNegative() &&
            IsNonZero(II->getArgOperand(1), Op1NonZero, Op1Known))
          return true;
        KnownBits Op0Known =
            computeKnownBits(II->getArgOperand(0), DemandedElts, Depth, Q);
        if (Op0Known.isNonNegative() &&
            IsNonZero(II->getArgOperand(0), Op0NonZero, Op0Known))
          return true;
        return IsNonZero(II->getArgOperand(1), Op1NonZero, Op1Known) &&
               IsNonZero(II->getArgOperand(0), Op0NonZero, Op0Known);
      }
      case Intrinsic::smin: {
        // If either arg is negative the result is non-zero. Otherwise
        // the result is non-zero if both ops are non-zero.
        KnownBits Op1Known =
            computeKnownBits(II->getArgOperand(1), DemandedElts, Depth, Q);
        if (Op1Known.isNegative())
          return true;
        KnownBits Op0Known =
            computeKnownBits(II->getArgOperand(0), DemandedElts, Depth, Q);
        if (Op0Known.isNegative())
          return true;

        if (Op1Known.isNonZero() && Op0Known.isNonZero())
          return true;
      }
        [[fallthrough]];
      case Intrinsic::umin:
        return isKnownNonZero(II->getArgOperand(0), DemandedElts, Q, Depth) &&
               isKnownNonZero(II->getArgOperand(1), DemandedElts, Q, Depth);
      case Intrinsic::cttz:
        return computeKnownBits(II->getArgOperand(0), DemandedElts, Depth, Q)
            .Zero[0];
      case Intrinsic::ctlz:
        return computeKnownBits(II->getArgOperand(0), DemandedElts, Depth, Q)
            .isNonNegative();
      case Intrinsic::fshr:
      case Intrinsic::fshl:
        // If Op0 == Op1, this is a rotate. rotate(x, y) != 0 iff x != 0.
        if (II->getArgOperand(0) == II->getArgOperand(1))
          return isKnownNonZero(II->getArgOperand(0), DemandedElts, Q, Depth);
        break;
      case Intrinsic::vscale:
        return true;
      case Intrinsic::experimental_get_vector_length:
        return isKnownNonZero(I->getOperand(0), Q, Depth);
      default:
        break;
      }
      break;
    }

    return false;
  }
  }

  KnownBits Known(BitWidth);
  computeKnownBits(I, DemandedElts, Known, Depth, Q);
  return Known.One != 0;
}

/// Return true if the given value is known to be non-zero when defined. For
/// vectors, return true if every demanded element is known to be non-zero when
/// defined. For pointers, if the context instruction and dominator tree are
/// specified, perform context-sensitive analysis and return true if the
/// pointer couldn't possibly be null at the specified instruction.
/// Supports values with integer or pointer type and vectors of integers.
bool isKnownNonZero(const Value *V, const APInt &DemandedElts,
                    const SimplifyQuery &Q, unsigned Depth) {
  Type *Ty = V->getType();

#ifndef NDEBUG
  assert(Depth <= MaxAnalysisRecursionDepth && "Limit Search Depth");

  if (auto *FVTy = dyn_cast<FixedVectorType>(Ty)) {
    assert(
        FVTy->getNumElements() == DemandedElts.getBitWidth() &&
        "DemandedElt width should equal the fixed vector number of elements");
  } else {
    assert(DemandedElts == APInt(1, 1) &&
           "DemandedElt width should be 1 for scalars");
  }
#endif

  if (auto *C = dyn_cast<Constant>(V)) {
    if (C->isNullValue())
      return false;
    if (isa<ConstantInt>(C))
      // Must be non-zero due to null test above.
      return true;

    // For constant vectors, check that all elements are poison or known
    // non-zero to determine that the whole vector is known non-zero.
    if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
      for (unsigned i = 0, e = VecTy->getNumElements(); i != e; ++i) {
        if (!DemandedElts[i])
          continue;
        Constant *Elt = C->getAggregateElement(i);
        if (!Elt || Elt->isNullValue())
          return false;
        if (!isa<PoisonValue>(Elt) && !isa<ConstantInt>(Elt))
          return false;
      }
      return true;
    }

    // Constant ptrauth can be null, iff the base pointer can be.
    if (auto *CPA = dyn_cast<ConstantPtrAuth>(V))
      return isKnownNonZero(CPA->getPointer(), DemandedElts, Q, Depth);

    // A global variable in address space 0 is non null unless extern weak
    // or an absolute symbol reference. Other address spaces may have null as a
    // valid address for a global, so we can't assume anything.
    if (const GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
      if (!GV->isAbsoluteSymbolRef() && !GV->hasExternalWeakLinkage() &&
          GV->getType()->getAddressSpace() == 0)
        return true;
    }

    // For constant expressions, fall through to the Operator code below.
    if (!isa<ConstantExpr>(V))
      return false;
  }

  if (const auto *A = dyn_cast<Argument>(V))
    if (std::optional<ConstantRange> Range = A->getRange()) {
      const APInt ZeroValue(Range->getBitWidth(), 0);
      if (!Range->contains(ZeroValue))
        return true;
    }

  if (!isa<Constant>(V) && isKnownNonZeroFromAssume(V, Q))
    return true;

  // Some of the tests below are recursive, so bail out if we hit the limit.
  if (Depth++ >= MaxAnalysisRecursionDepth)
    return false;

  // Check for pointer simplifications.

  if (PointerType *PtrTy = dyn_cast<PointerType>(Ty)) {
    // A byval, inalloca may not be null in a non-default addres space. A
    // nonnull argument is assumed never 0.
    if (const Argument *A = dyn_cast<Argument>(V)) {
      if (((A->hasPassPointeeByValueCopyAttr() &&
            !NullPointerIsDefined(A->getParent(), PtrTy->getAddressSpace())) ||
           A->hasNonNullAttr()))
        return true;
    }
  }

  if (const auto *I = dyn_cast<Operator>(V))
    if (isKnownNonZeroFromOperator(I, DemandedElts, Depth, Q))
      return true;

  if (!isa<Constant>(V) &&
      isKnownNonNullFromDominatingCondition(V, Q.CxtI, Q.DT))
    return true;

  return false;
}

bool llvm::isKnownNonZero(const Value *V, const SimplifyQuery &Q,
                          unsigned Depth) {
  auto *FVTy = dyn_cast<FixedVectorType>(V->getType());
  APInt DemandedElts =
      FVTy ? APInt::getAllOnes(FVTy->getNumElements()) : APInt(1, 1);
  return ::isKnownNonZero(V, DemandedElts, Q, Depth);
}

/// If the pair of operators are the same invertible function, return the
/// the operands of the function corresponding to each input. Otherwise,
/// return std::nullopt.  An invertible function is one that is 1-to-1 and maps
/// every input value to exactly one output value.  This is equivalent to
/// saying that Op1 and Op2 are equal exactly when the specified pair of
/// operands are equal, (except that Op1 and Op2 may be poison more often.)
static std::optional<std::pair<Value*, Value*>>
getInvertibleOperands(const Operator *Op1,
                      const Operator *Op2) {
  if (Op1->getOpcode() != Op2->getOpcode())
    return std::nullopt;

  auto getOperands = [&](unsigned OpNum) -> auto {
    return std::make_pair(Op1->getOperand(OpNum), Op2->getOperand(OpNum));
  };

  switch (Op1->getOpcode()) {
  default:
    break;
  case Instruction::Or:
    if (!cast<PossiblyDisjointInst>(Op1)->isDisjoint() ||
        !cast<PossiblyDisjointInst>(Op2)->isDisjoint())
      break;
    [[fallthrough]];
  case Instruction::Xor:
  case Instruction::Add: {
    Value *Other;
    if (match(Op2, m_c_BinOp(m_Specific(Op1->getOperand(0)), m_Value(Other))))
      return std::make_pair(Op1->getOperand(1), Other);
    if (match(Op2, m_c_BinOp(m_Specific(Op1->getOperand(1)), m_Value(Other))))
      return std::make_pair(Op1->getOperand(0), Other);
    break;
  }
  case Instruction::Sub:
    if (Op1->getOperand(0) == Op2->getOperand(0))
      return getOperands(1);
    if (Op1->getOperand(1) == Op2->getOperand(1))
      return getOperands(0);
    break;
  case Instruction::Mul: {
    // invertible if A * B == (A * B) mod 2^N where A, and B are integers
    // and N is the bitwdith.  The nsw case is non-obvious, but proven by
    // alive2: https://alive2.llvm.org/ce/z/Z6D5qK
    auto *OBO1 = cast<OverflowingBinaryOperator>(Op1);
    auto *OBO2 = cast<OverflowingBinaryOperator>(Op2);
    if ((!OBO1->hasNoUnsignedWrap() || !OBO2->hasNoUnsignedWrap()) &&
        (!OBO1->hasNoSignedWrap() || !OBO2->hasNoSignedWrap()))
      break;

    // Assume operand order has been canonicalized
    if (Op1->getOperand(1) == Op2->getOperand(1) &&
        isa<ConstantInt>(Op1->getOperand(1)) &&
        !cast<ConstantInt>(Op1->getOperand(1))->isZero())
      return getOperands(0);
    break;
  }
  case Instruction::Shl: {
    // Same as multiplies, with the difference that we don't need to check
    // for a non-zero multiply. Shifts always multiply by non-zero.
    auto *OBO1 = cast<OverflowingBinaryOperator>(Op1);
    auto *OBO2 = cast<OverflowingBinaryOperator>(Op2);
    if ((!OBO1->hasNoUnsignedWrap() || !OBO2->hasNoUnsignedWrap()) &&
        (!OBO1->hasNoSignedWrap() || !OBO2->hasNoSignedWrap()))
      break;

    if (Op1->getOperand(1) == Op2->getOperand(1))
      return getOperands(0);
    break;
  }
  case Instruction::AShr:
  case Instruction::LShr: {
    auto *PEO1 = cast<PossiblyExactOperator>(Op1);
    auto *PEO2 = cast<PossiblyExactOperator>(Op2);
    if (!PEO1->isExact() || !PEO2->isExact())
      break;

    if (Op1->getOperand(1) == Op2->getOperand(1))
      return getOperands(0);
    break;
  }
  case Instruction::SExt:
  case Instruction::ZExt:
    if (Op1->getOperand(0)->getType() == Op2->getOperand(0)->getType())
      return getOperands(0);
    break;
  case Instruction::PHI: {
    const PHINode *PN1 = cast<PHINode>(Op1);
    const PHINode *PN2 = cast<PHINode>(Op2);

    // If PN1 and PN2 are both recurrences, can we prove the entire recurrences
    // are a single invertible function of the start values? Note that repeated
    // application of an invertible function is also invertible
    BinaryOperator *BO1 = nullptr;
    Value *Start1 = nullptr, *Step1 = nullptr;
    BinaryOperator *BO2 = nullptr;
    Value *Start2 = nullptr, *Step2 = nullptr;
    if (PN1->getParent() != PN2->getParent() ||
        !matchSimpleRecurrence(PN1, BO1, Start1, Step1) ||
        !matchSimpleRecurrence(PN2, BO2, Start2, Step2))
      break;

    auto Values = getInvertibleOperands(cast<Operator>(BO1),
                                        cast<Operator>(BO2));
    if (!Values)
       break;

    // We have to be careful of mutually defined recurrences here.  Ex:
    // * X_i = X_(i-1) OP Y_(i-1), and Y_i = X_(i-1) OP V
    // * X_i = Y_i = X_(i-1) OP Y_(i-1)
    // The invertibility of these is complicated, and not worth reasoning
    // about (yet?).
    if (Values->first != PN1 || Values->second != PN2)
      break;

    return std::make_pair(Start1, Start2);
  }
  }
  return std::nullopt;
}

/// Return true if V1 == (binop V2, X), where X is known non-zero.
/// Only handle a small subset of binops where (binop V2, X) with non-zero X
/// implies V2 != V1.
static bool isModifyingBinopOfNonZero(const Value *V1, const Value *V2,
                                      const APInt &DemandedElts, unsigned Depth,
                                      const SimplifyQuery &Q) {
  const BinaryOperator *BO = dyn_cast<BinaryOperator>(V1);
  if (!BO)
    return false;
  switch (BO->getOpcode()) {
  default:
    break;
  case Instruction::Or:
    if (!cast<PossiblyDisjointInst>(V1)->isDisjoint())
      break;
    [[fallthrough]];
  case Instruction::Xor:
  case Instruction::Add:
    Value *Op = nullptr;
    if (V2 == BO->getOperand(0))
      Op = BO->getOperand(1);
    else if (V2 == BO->getOperand(1))
      Op = BO->getOperand(0);
    else
      return false;
    return isKnownNonZero(Op, DemandedElts, Q, Depth + 1);
  }
  return false;
}

/// Return true if V2 == V1 * C, where V1 is known non-zero, C is not 0/1 and
/// the multiplication is nuw or nsw.
static bool isNonEqualMul(const Value *V1, const Value *V2,
                          const APInt &DemandedElts, unsigned Depth,
                          const SimplifyQuery &Q) {
  if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(V2)) {
    const APInt *C;
    return match(OBO, m_Mul(m_Specific(V1), m_APInt(C))) &&
           (OBO->hasNoUnsignedWrap() || OBO->hasNoSignedWrap()) &&
           !C->isZero() && !C->isOne() &&
           isKnownNonZero(V1, DemandedElts, Q, Depth + 1);
  }
  return false;
}

/// Return true if V2 == V1 << C, where V1 is known non-zero, C is not 0 and
/// the shift is nuw or nsw.
static bool isNonEqualShl(const Value *V1, const Value *V2,
                          const APInt &DemandedElts, unsigned Depth,
                          const SimplifyQuery &Q) {
  if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(V2)) {
    const APInt *C;
    return match(OBO, m_Shl(m_Specific(V1), m_APInt(C))) &&
           (OBO->hasNoUnsignedWrap() || OBO->hasNoSignedWrap()) &&
           !C->isZero() && isKnownNonZero(V1, DemandedElts, Q, Depth + 1);
  }
  return false;
}

static bool isNonEqualPHIs(const PHINode *PN1, const PHINode *PN2,
                           const APInt &DemandedElts, unsigned Depth,
                           const SimplifyQuery &Q) {
  // Check two PHIs are in same block.
  if (PN1->getParent() != PN2->getParent())
    return false;

  SmallPtrSet<const BasicBlock *, 8> VisitedBBs;
  bool UsedFullRecursion = false;
  for (const BasicBlock *IncomBB : PN1->blocks()) {
    if (!VisitedBBs.insert(IncomBB).second)
      continue; // Don't reprocess blocks that we have dealt with already.
    const Value *IV1 = PN1->getIncomingValueForBlock(IncomBB);
    const Value *IV2 = PN2->getIncomingValueForBlock(IncomBB);
    const APInt *C1, *C2;
    if (match(IV1, m_APInt(C1)) && match(IV2, m_APInt(C2)) && *C1 != *C2)
      continue;

    // Only one pair of phi operands is allowed for full recursion.
    if (UsedFullRecursion)
      return false;

    SimplifyQuery RecQ = Q.getWithoutCondContext();
    RecQ.CxtI = IncomBB->getTerminator();
    if (!isKnownNonEqual(IV1, IV2, DemandedElts, Depth + 1, RecQ))
      return false;
    UsedFullRecursion = true;
  }
  return true;
}

static bool isNonEqualSelect(const Value *V1, const Value *V2,
                             const APInt &DemandedElts, unsigned Depth,
                             const SimplifyQuery &Q) {
  const SelectInst *SI1 = dyn_cast<SelectInst>(V1);
  if (!SI1)
    return false;

  if (const SelectInst *SI2 = dyn_cast<SelectInst>(V2)) {
    const Value *Cond1 = SI1->getCondition();
    const Value *Cond2 = SI2->getCondition();
    if (Cond1 == Cond2)
      return isKnownNonEqual(SI1->getTrueValue(), SI2->getTrueValue(),
                             DemandedElts, Depth + 1, Q) &&
             isKnownNonEqual(SI1->getFalseValue(), SI2->getFalseValue(),
                             DemandedElts, Depth + 1, Q);
  }
  return isKnownNonEqual(SI1->getTrueValue(), V2, DemandedElts, Depth + 1, Q) &&
         isKnownNonEqual(SI1->getFalseValue(), V2, DemandedElts, Depth + 1, Q);
}

// Check to see if A is both a GEP and is the incoming value for a PHI in the
// loop, and B is either a ptr or another GEP. If the PHI has 2 incoming values,
// one of them being the recursive GEP A and the other a ptr at same base and at
// the same/higher offset than B we are only incrementing the pointer further in
// loop if offset of recursive GEP is greater than 0.
static bool isNonEqualPointersWithRecursiveGEP(const Value *A, const Value *B,
                                               const SimplifyQuery &Q) {
  if (!A->getType()->isPointerTy() || !B->getType()->isPointerTy())
    return false;

  auto *GEPA = dyn_cast<GEPOperator>(A);
  if (!GEPA || GEPA->getNumIndices() != 1 || !isa<Constant>(GEPA->idx_begin()))
    return false;

  // Handle 2 incoming PHI values with one being a recursive GEP.
  auto *PN = dyn_cast<PHINode>(GEPA->getPointerOperand());
  if (!PN || PN->getNumIncomingValues() != 2)
    return false;

  // Search for the recursive GEP as an incoming operand, and record that as
  // Step.
  Value *Start = nullptr;
  Value *Step = const_cast<Value *>(A);
  if (PN->getIncomingValue(0) == Step)
    Start = PN->getIncomingValue(1);
  else if (PN->getIncomingValue(1) == Step)
    Start = PN->getIncomingValue(0);
  else
    return false;

  // Other incoming node base should match the B base.
  // StartOffset >= OffsetB && StepOffset > 0?
  // StartOffset <= OffsetB && StepOffset < 0?
  // Is non-equal if above are true.
  // We use stripAndAccumulateInBoundsConstantOffsets to restrict the
  // optimisation to inbounds GEPs only.
  unsigned IndexWidth = Q.DL.getIndexTypeSizeInBits(Start->getType());
  APInt StartOffset(IndexWidth, 0);
  Start = Start->stripAndAccumulateInBoundsConstantOffsets(Q.DL, StartOffset);
  APInt StepOffset(IndexWidth, 0);
  Step = Step->stripAndAccumulateInBoundsConstantOffsets(Q.DL, StepOffset);

  // Check if Base Pointer of Step matches the PHI.
  if (Step != PN)
    return false;
  APInt OffsetB(IndexWidth, 0);
  B = B->stripAndAccumulateInBoundsConstantOffsets(Q.DL, OffsetB);
  return Start == B &&
         ((StartOffset.sge(OffsetB) && StepOffset.isStrictlyPositive()) ||
          (StartOffset.sle(OffsetB) && StepOffset.isNegative()));
}

/// Return true if it is known that V1 != V2.
static bool isKnownNonEqual(const Value *V1, const Value *V2,
                            const APInt &DemandedElts, unsigned Depth,
                            const SimplifyQuery &Q) {
  if (V1 == V2)
    return false;
  if (V1->getType() != V2->getType())
    // We can't look through casts yet.
    return false;

  if (Depth >= MaxAnalysisRecursionDepth)
    return false;

  // See if we can recurse through (exactly one of) our operands.  This
  // requires our operation be 1-to-1 and map every input value to exactly
  // one output value.  Such an operation is invertible.
  auto *O1 = dyn_cast<Operator>(V1);
  auto *O2 = dyn_cast<Operator>(V2);
  if (O1 && O2 && O1->getOpcode() == O2->getOpcode()) {
    if (auto Values = getInvertibleOperands(O1, O2))
      return isKnownNonEqual(Values->first, Values->second, DemandedElts,
                             Depth + 1, Q);

    if (const PHINode *PN1 = dyn_cast<PHINode>(V1)) {
      const PHINode *PN2 = cast<PHINode>(V2);
      // FIXME: This is missing a generalization to handle the case where one is
      // a PHI and another one isn't.
      if (isNonEqualPHIs(PN1, PN2, DemandedElts, Depth, Q))
        return true;
    };
  }

  if (isModifyingBinopOfNonZero(V1, V2, DemandedElts, Depth, Q) ||
      isModifyingBinopOfNonZero(V2, V1, DemandedElts, Depth, Q))
    return true;

  if (isNonEqualMul(V1, V2, DemandedElts, Depth, Q) ||
      isNonEqualMul(V2, V1, DemandedElts, Depth, Q))
    return true;

  if (isNonEqualShl(V1, V2, DemandedElts, Depth, Q) ||
      isNonEqualShl(V2, V1, DemandedElts, Depth, Q))
    return true;

  if (V1->getType()->isIntOrIntVectorTy()) {
    // Are any known bits in V1 contradictory to known bits in V2? If V1
    // has a known zero where V2 has a known one, they must not be equal.
    KnownBits Known1 = computeKnownBits(V1, DemandedElts, Depth, Q);
    if (!Known1.isUnknown()) {
      KnownBits Known2 = computeKnownBits(V2, DemandedElts, Depth, Q);
      if (Known1.Zero.intersects(Known2.One) ||
          Known2.Zero.intersects(Known1.One))
        return true;
    }
  }

  if (isNonEqualSelect(V1, V2, DemandedElts, Depth, Q) ||
      isNonEqualSelect(V2, V1, DemandedElts, Depth, Q))
    return true;

  if (isNonEqualPointersWithRecursiveGEP(V1, V2, Q) ||
      isNonEqualPointersWithRecursiveGEP(V2, V1, Q))
    return true;

  Value *A, *B;
  // PtrToInts are NonEqual if their Ptrs are NonEqual.
  // Check PtrToInt type matches the pointer size.
  if (match(V1, m_PtrToIntSameSize(Q.DL, m_Value(A))) &&
      match(V2, m_PtrToIntSameSize(Q.DL, m_Value(B))))
    return isKnownNonEqual(A, B, DemandedElts, Depth + 1, Q);

  return false;
}

// Match a signed min+max clamp pattern like smax(smin(In, CHigh), CLow).
// Returns the input and lower/upper bounds.
static bool isSignedMinMaxClamp(const Value *Select, const Value *&In,
                                const APInt *&CLow, const APInt *&CHigh) {
  assert(isa<Operator>(Select) &&
         cast<Operator>(Select)->getOpcode() == Instruction::Select &&
         "Input should be a Select!");

  const Value *LHS = nullptr, *RHS = nullptr;
  SelectPatternFlavor SPF = matchSelectPattern(Select, LHS, RHS).Flavor;
  if (SPF != SPF_SMAX && SPF != SPF_SMIN)
    return false;

  if (!match(RHS, m_APInt(CLow)))
    return false;

  const Value *LHS2 = nullptr, *RHS2 = nullptr;
  SelectPatternFlavor SPF2 = matchSelectPattern(LHS, LHS2, RHS2).Flavor;
  if (getInverseMinMaxFlavor(SPF) != SPF2)
    return false;

  if (!match(RHS2, m_APInt(CHigh)))
    return false;

  if (SPF == SPF_SMIN)
    std::swap(CLow, CHigh);

  In = LHS2;
  return CLow->sle(*CHigh);
}

static bool isSignedMinMaxIntrinsicClamp(const IntrinsicInst *II,
                                         const APInt *&CLow,
                                         const APInt *&CHigh) {
  assert((II->getIntrinsicID() == Intrinsic::smin ||
          II->getIntrinsicID() == Intrinsic::smax) && "Must be smin/smax");

  Intrinsic::ID InverseID = getInverseMinMaxIntrinsic(II->getIntrinsicID());
  auto *InnerII = dyn_cast<IntrinsicInst>(II->getArgOperand(0));
  if (!InnerII || InnerII->getIntrinsicID() != InverseID ||
      !match(II->getArgOperand(1), m_APInt(CLow)) ||
      !match(InnerII->getArgOperand(1), m_APInt(CHigh)))
    return false;

  if (II->getIntrinsicID() == Intrinsic::smin)
    std::swap(CLow, CHigh);
  return CLow->sle(*CHigh);
}

/// For vector constants, loop over the elements and find the constant with the
/// minimum number of sign bits. Return 0 if the value is not a vector constant
/// or if any element was not analyzed; otherwise, return the count for the
/// element with the minimum number of sign bits.
static unsigned computeNumSignBitsVectorConstant(const Value *V,
                                                 const APInt &DemandedElts,
                                                 unsigned TyBits) {
  const auto *CV = dyn_cast<Constant>(V);
  if (!CV || !isa<FixedVectorType>(CV->getType()))
    return 0;

  unsigned MinSignBits = TyBits;
  unsigned NumElts = cast<FixedVectorType>(CV->getType())->getNumElements();
  for (unsigned i = 0; i != NumElts; ++i) {
    if (!DemandedElts[i])
      continue;
    // If we find a non-ConstantInt, bail out.
    auto *Elt = dyn_cast_or_null<ConstantInt>(CV->getAggregateElement(i));
    if (!Elt)
      return 0;

    MinSignBits = std::min(MinSignBits, Elt->getValue().getNumSignBits());
  }

  return MinSignBits;
}

static unsigned ComputeNumSignBitsImpl(const Value *V,
                                       const APInt &DemandedElts,
                                       unsigned Depth, const SimplifyQuery &Q);

static unsigned ComputeNumSignBits(const Value *V, const APInt &DemandedElts,
                                   unsigned Depth, const SimplifyQuery &Q) {
  unsigned Result = ComputeNumSignBitsImpl(V, DemandedElts, Depth, Q);
  assert(Result > 0 && "At least one sign bit needs to be present!");
  return Result;
}

/// Return the number of times the sign bit of the register is replicated into
/// the other bits. We know that at least 1 bit is always equal to the sign bit
/// (itself), but other cases can give us information. For example, immediately
/// after an "ashr X, 2", we know that the top 3 bits are all equal to each
/// other, so we return 3. For vectors, return the number of sign bits for the
/// vector element with the minimum number of known sign bits of the demanded
/// elements in the vector specified by DemandedElts.
static unsigned ComputeNumSignBitsImpl(const Value *V,
                                       const APInt &DemandedElts,
                                       unsigned Depth, const SimplifyQuery &Q) {
  Type *Ty = V->getType();
#ifndef NDEBUG
  assert(Depth <= MaxAnalysisRecursionDepth && "Limit Search Depth");

  if (auto *FVTy = dyn_cast<FixedVectorType>(Ty)) {
    assert(
        FVTy->getNumElements() == DemandedElts.getBitWidth() &&
        "DemandedElt width should equal the fixed vector number of elements");
  } else {
    assert(DemandedElts == APInt(1, 1) &&
           "DemandedElt width should be 1 for scalars");
  }
#endif

  // We return the minimum number of sign bits that are guaranteed to be present
  // in V, so for undef we have to conservatively return 1.  We don't have the
  // same behavior for poison though -- that's a FIXME today.

  Type *ScalarTy = Ty->getScalarType();
  unsigned TyBits = ScalarTy->isPointerTy() ?
    Q.DL.getPointerTypeSizeInBits(ScalarTy) :
    Q.DL.getTypeSizeInBits(ScalarTy);

  unsigned Tmp, Tmp2;
  unsigned FirstAnswer = 1;

  // Note that ConstantInt is handled by the general computeKnownBits case
  // below.

  if (Depth == MaxAnalysisRecursionDepth)
    return 1;

  if (auto *U = dyn_cast<Operator>(V)) {
    switch (Operator::getOpcode(V)) {
    default: break;
    case Instruction::SExt:
      Tmp = TyBits - U->getOperand(0)->getType()->getScalarSizeInBits();
      return ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q) +
             Tmp;

    case Instruction::SDiv: {
      const APInt *Denominator;
      // sdiv X, C -> adds log(C) sign bits.
      if (match(U->getOperand(1), m_APInt(Denominator))) {

        // Ignore non-positive denominator.
        if (!Denominator->isStrictlyPositive())
          break;

        // Calculate the incoming numerator bits.
        unsigned NumBits =
            ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);

        // Add floor(log(C)) bits to the numerator bits.
        return std::min(TyBits, NumBits + Denominator->logBase2());
      }
      break;
    }

    case Instruction::SRem: {
      Tmp = ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);

      const APInt *Denominator;
      // srem X, C -> we know that the result is within [-C+1,C) when C is a
      // positive constant.  This let us put a lower bound on the number of sign
      // bits.
      if (match(U->getOperand(1), m_APInt(Denominator))) {

        // Ignore non-positive denominator.
        if (Denominator->isStrictlyPositive()) {
          // Calculate the leading sign bit constraints by examining the
          // denominator.  Given that the denominator is positive, there are two
          // cases:
          //
          //  1. The numerator is positive. The result range is [0,C) and
          //     [0,C) u< (1 << ceilLogBase2(C)).
          //
          //  2. The numerator is negative. Then the result range is (-C,0] and
          //     integers in (-C,0] are either 0 or >u (-1 << ceilLogBase2(C)).
          //
          // Thus a lower bound on the number of sign bits is `TyBits -
          // ceilLogBase2(C)`.

          unsigned ResBits = TyBits - Denominator->ceilLogBase2();
          Tmp = std::max(Tmp, ResBits);
        }
      }
      return Tmp;
    }

    case Instruction::AShr: {
      Tmp = ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);
      // ashr X, C   -> adds C sign bits.  Vectors too.
      const APInt *ShAmt;
      if (match(U->getOperand(1), m_APInt(ShAmt))) {
        if (ShAmt->uge(TyBits))
          break; // Bad shift.
        unsigned ShAmtLimited = ShAmt->getZExtValue();
        Tmp += ShAmtLimited;
        if (Tmp > TyBits) Tmp = TyBits;
      }
      return Tmp;
    }
    case Instruction::Shl: {
      const APInt *ShAmt;
      Value *X = nullptr;
      if (match(U->getOperand(1), m_APInt(ShAmt))) {
        // shl destroys sign bits.
        if (ShAmt->uge(TyBits))
          break; // Bad shift.
        // We can look through a zext (more or less treating it as a sext) if
        // all extended bits are shifted out.
        if (match(U->getOperand(0), m_ZExt(m_Value(X))) &&
            ShAmt->uge(TyBits - X->getType()->getScalarSizeInBits())) {
          Tmp = ComputeNumSignBits(X, DemandedElts, Depth + 1, Q);
          Tmp += TyBits - X->getType()->getScalarSizeInBits();
        } else
          Tmp =
              ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);
        if (ShAmt->uge(Tmp))
          break; // Shifted all sign bits out.
        Tmp2 = ShAmt->getZExtValue();
        return Tmp - Tmp2;
      }
      break;
    }
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: // NOT is handled here.
      // Logical binary ops preserve the number of sign bits at the worst.
      Tmp = ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);
      if (Tmp != 1) {
        Tmp2 = ComputeNumSignBits(U->getOperand(1), DemandedElts, Depth + 1, Q);
        FirstAnswer = std::min(Tmp, Tmp2);
        // We computed what we know about the sign bits as our first
        // answer. Now proceed to the generic code that uses
        // computeKnownBits, and pick whichever answer is better.
      }
      break;

    case Instruction::Select: {
      // If we have a clamp pattern, we know that the number of sign bits will
      // be the minimum of the clamp min/max range.
      const Value *X;
      const APInt *CLow, *CHigh;
      if (isSignedMinMaxClamp(U, X, CLow, CHigh))
        return std::min(CLow->getNumSignBits(), CHigh->getNumSignBits());

      Tmp = ComputeNumSignBits(U->getOperand(1), DemandedElts, Depth + 1, Q);
      if (Tmp == 1)
        break;
      Tmp2 = ComputeNumSignBits(U->getOperand(2), DemandedElts, Depth + 1, Q);
      return std::min(Tmp, Tmp2);
    }

    case Instruction::Add:
      // Add can have at most one carry bit.  Thus we know that the output
      // is, at worst, one more bit than the inputs.
      Tmp = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
      if (Tmp == 1) break;

      // Special case decrementing a value (ADD X, -1):
      if (const auto *CRHS = dyn_cast<Constant>(U->getOperand(1)))
        if (CRHS->isAllOnesValue()) {
          KnownBits Known(TyBits);
          computeKnownBits(U->getOperand(0), DemandedElts, Known, Depth + 1, Q);

          // If the input is known to be 0 or 1, the output is 0/-1, which is
          // all sign bits set.
          if ((Known.Zero | 1).isAllOnes())
            return TyBits;

          // If we are subtracting one from a positive number, there is no carry
          // out of the result.
          if (Known.isNonNegative())
            return Tmp;
        }

      Tmp2 = ComputeNumSignBits(U->getOperand(1), DemandedElts, Depth + 1, Q);
      if (Tmp2 == 1)
        break;
      return std::min(Tmp, Tmp2) - 1;

    case Instruction::Sub:
      Tmp2 = ComputeNumSignBits(U->getOperand(1), DemandedElts, Depth + 1, Q);
      if (Tmp2 == 1)
        break;

      // Handle NEG.
      if (const auto *CLHS = dyn_cast<Constant>(U->getOperand(0)))
        if (CLHS->isNullValue()) {
          KnownBits Known(TyBits);
          computeKnownBits(U->getOperand(1), DemandedElts, Known, Depth + 1, Q);
          // If the input is known to be 0 or 1, the output is 0/-1, which is
          // all sign bits set.
          if ((Known.Zero | 1).isAllOnes())
            return TyBits;

          // If the input is known to be positive (the sign bit is known clear),
          // the output of the NEG has the same number of sign bits as the
          // input.
          if (Known.isNonNegative())
            return Tmp2;

          // Otherwise, we treat this like a SUB.
        }

      // Sub can have at most one carry bit.  Thus we know that the output
      // is, at worst, one more bit than the inputs.
      Tmp = ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);
      if (Tmp == 1)
        break;
      return std::min(Tmp, Tmp2) - 1;

    case Instruction::Mul: {
      // The output of the Mul can be at most twice the valid bits in the
      // inputs.
      unsigned SignBitsOp0 =
          ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);
      if (SignBitsOp0 == 1)
        break;
      unsigned SignBitsOp1 =
          ComputeNumSignBits(U->getOperand(1), DemandedElts, Depth + 1, Q);
      if (SignBitsOp1 == 1)
        break;
      unsigned OutValidBits =
          (TyBits - SignBitsOp0 + 1) + (TyBits - SignBitsOp1 + 1);
      return OutValidBits > TyBits ? 1 : TyBits - OutValidBits + 1;
    }

    case Instruction::PHI: {
      const PHINode *PN = cast<PHINode>(U);
      unsigned NumIncomingValues = PN->getNumIncomingValues();
      // Don't analyze large in-degree PHIs.
      if (NumIncomingValues > 4) break;
      // Unreachable blocks may have zero-operand PHI nodes.
      if (NumIncomingValues == 0) break;

      // Take the minimum of all incoming values.  This can't infinitely loop
      // because of our depth threshold.
      SimplifyQuery RecQ = Q.getWithoutCondContext();
      Tmp = TyBits;
      for (unsigned i = 0, e = NumIncomingValues; i != e; ++i) {
        if (Tmp == 1) return Tmp;
        RecQ.CxtI = PN->getIncomingBlock(i)->getTerminator();
        Tmp = std::min(Tmp, ComputeNumSignBits(PN->getIncomingValue(i),
                                               DemandedElts, Depth + 1, RecQ));
      }
      return Tmp;
    }

    case Instruction::Trunc: {
      // If the input contained enough sign bits that some remain after the
      // truncation, then we can make use of that. Otherwise we don't know
      // anything.
      Tmp = ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);
      unsigned OperandTyBits = U->getOperand(0)->getType()->getScalarSizeInBits();
      if (Tmp > (OperandTyBits - TyBits))
        return Tmp - (OperandTyBits - TyBits);

      return 1;
    }

    case Instruction::ExtractElement:
      // Look through extract element. At the moment we keep this simple and
      // skip tracking the specific element. But at least we might find
      // information valid for all elements of the vector (for example if vector
      // is sign extended, shifted, etc).
      return ComputeNumSignBits(U->getOperand(0), Depth + 1, Q);

    case Instruction::ShuffleVector: {
      // Collect the minimum number of sign bits that are shared by every vector
      // element referenced by the shuffle.
      auto *Shuf = dyn_cast<ShuffleVectorInst>(U);
      if (!Shuf) {
        // FIXME: Add support for shufflevector constant expressions.
        return 1;
      }
      APInt DemandedLHS, DemandedRHS;
      // For undef elements, we don't know anything about the common state of
      // the shuffle result.
      if (!getShuffleDemandedElts(Shuf, DemandedElts, DemandedLHS, DemandedRHS))
        return 1;
      Tmp = std::numeric_limits<unsigned>::max();
      if (!!DemandedLHS) {
        const Value *LHS = Shuf->getOperand(0);
        Tmp = ComputeNumSignBits(LHS, DemandedLHS, Depth + 1, Q);
      }
      // If we don't know anything, early out and try computeKnownBits
      // fall-back.
      if (Tmp == 1)
        break;
      if (!!DemandedRHS) {
        const Value *RHS = Shuf->getOperand(1);
        Tmp2 = ComputeNumSignBits(RHS, DemandedRHS, Depth + 1, Q);
        Tmp = std::min(Tmp, Tmp2);
      }
      // If we don't know anything, early out and try computeKnownBits
      // fall-back.
      if (Tmp == 1)
        break;
      assert(Tmp <= TyBits && "Failed to determine minimum sign bits");
      return Tmp;
    }
    case Instruction::Call: {
      if (const auto *II = dyn_cast<IntrinsicInst>(U)) {
        switch (II->getIntrinsicID()) {
        default:
          break;
        case Intrinsic::abs:
          Tmp =
              ComputeNumSignBits(U->getOperand(0), DemandedElts, Depth + 1, Q);
          if (Tmp == 1)
            break;

          // Absolute value reduces number of sign bits by at most 1.
          return Tmp - 1;
        case Intrinsic::smin:
        case Intrinsic::smax: {
          const APInt *CLow, *CHigh;
          if (isSignedMinMaxIntrinsicClamp(II, CLow, CHigh))
            return std::min(CLow->getNumSignBits(), CHigh->getNumSignBits());
        }
        }
      }
    }
    }
  }

  // Finally, if we can prove that the top bits of the result are 0's or 1's,
  // use this information.

  // If we can examine all elements of a vector constant successfully, we're
  // done (we can't do any better than that). If not, keep trying.
  if (unsigned VecSignBits =
          computeNumSignBitsVectorConstant(V, DemandedElts, TyBits))
    return VecSignBits;

  KnownBits Known(TyBits);
  computeKnownBits(V, DemandedElts, Known, Depth, Q);

  // If we know that the sign bit is either zero or one, determine the number of
  // identical bits in the top of the input value.
  return std::max(FirstAnswer, Known.countMinSignBits());
}

Intrinsic::ID llvm::getIntrinsicForCallSite(const CallBase &CB,
                                            const TargetLibraryInfo *TLI) {
  const Function *F = CB.getCalledFunction();
  if (!F)
    return Intrinsic::not_intrinsic;

  if (F->isIntrinsic())
    return F->getIntrinsicID();

  // We are going to infer semantics of a library function based on mapping it
  // to an LLVM intrinsic. Check that the library function is available from
  // this callbase and in this environment.
  LibFunc Func;
  if (F->hasLocalLinkage() || !TLI || !TLI->getLibFunc(CB, Func) ||
      !CB.onlyReadsMemory())
    return Intrinsic::not_intrinsic;

  switch (Func) {
  default:
    break;
  case LibFunc_sin:
  case LibFunc_sinf:
  case LibFunc_sinl:
    return Intrinsic::sin;
  case LibFunc_cos:
  case LibFunc_cosf:
  case LibFunc_cosl:
    return Intrinsic::cos;
  case LibFunc_tan:
  case LibFunc_tanf:
  case LibFunc_tanl:
    return Intrinsic::tan;
  case LibFunc_exp:
  case LibFunc_expf:
  case LibFunc_expl:
    return Intrinsic::exp;
  case LibFunc_exp2:
  case LibFunc_exp2f:
  case LibFunc_exp2l:
    return Intrinsic::exp2;
  case LibFunc_log:
  case LibFunc_logf:
  case LibFunc_logl:
    return Intrinsic::log;
  case LibFunc_log10:
  case LibFunc_log10f:
  case LibFunc_log10l:
    return Intrinsic::log10;
  case LibFunc_log2:
  case LibFunc_log2f:
  case LibFunc_log2l:
    return Intrinsic::log2;
  case LibFunc_fabs:
  case LibFunc_fabsf:
  case LibFunc_fabsl:
    return Intrinsic::fabs;
  case LibFunc_fmin:
  case LibFunc_fminf:
  case LibFunc_fminl:
    return Intrinsic::minnum;
  case LibFunc_fmax:
  case LibFunc_fmaxf:
  case LibFunc_fmaxl:
    return Intrinsic::maxnum;
  case LibFunc_copysign:
  case LibFunc_copysignf:
  case LibFunc_copysignl:
    return Intrinsic::copysign;
  case LibFunc_floor:
  case LibFunc_floorf:
  case LibFunc_floorl:
    return Intrinsic::floor;
  case LibFunc_ceil:
  case LibFunc_ceilf:
  case LibFunc_ceill:
    return Intrinsic::ceil;
  case LibFunc_trunc:
  case LibFunc_truncf:
  case LibFunc_truncl:
    return Intrinsic::trunc;
  case LibFunc_rint:
  case LibFunc_rintf:
  case LibFunc_rintl:
    return Intrinsic::rint;
  case LibFunc_nearbyint:
  case LibFunc_nearbyintf:
  case LibFunc_nearbyintl:
    return Intrinsic::nearbyint;
  case LibFunc_round:
  case LibFunc_roundf:
  case LibFunc_roundl:
    return Intrinsic::round;
  case LibFunc_roundeven:
  case LibFunc_roundevenf:
  case LibFunc_roundevenl:
    return Intrinsic::roundeven;
  case LibFunc_pow:
  case LibFunc_powf:
  case LibFunc_powl:
    return Intrinsic::pow;
  case LibFunc_sqrt:
  case LibFunc_sqrtf:
  case LibFunc_sqrtl:
    return Intrinsic::sqrt;
  }

  return Intrinsic::not_intrinsic;
}

/// Return true if it's possible to assume IEEE treatment of input denormals in
/// \p F for \p Val.
static bool inputDenormalIsIEEE(const Function &F, const Type *Ty) {
  Ty = Ty->getScalarType();
  return F.getDenormalMode(Ty->getFltSemantics()).Input == DenormalMode::IEEE;
}

static bool inputDenormalIsIEEEOrPosZero(const Function &F, const Type *Ty) {
  Ty = Ty->getScalarType();
  DenormalMode Mode = F.getDenormalMode(Ty->getFltSemantics());
  return Mode.Input == DenormalMode::IEEE ||
         Mode.Input == DenormalMode::PositiveZero;
}

static bool outputDenormalIsIEEEOrPosZero(const Function &F, const Type *Ty) {
  Ty = Ty->getScalarType();
  DenormalMode Mode = F.getDenormalMode(Ty->getFltSemantics());
  return Mode.Output == DenormalMode::IEEE ||
         Mode.Output == DenormalMode::PositiveZero;
}

bool KnownFPClass::isKnownNeverLogicalZero(const Function &F, Type *Ty) const {
  return isKnownNeverZero() &&
         (isKnownNeverSubnormal() || inputDenormalIsIEEE(F, Ty));
}

bool KnownFPClass::isKnownNeverLogicalNegZero(const Function &F,
                                              Type *Ty) const {
  return isKnownNeverNegZero() &&
         (isKnownNeverNegSubnormal() || inputDenormalIsIEEEOrPosZero(F, Ty));
}

bool KnownFPClass::isKnownNeverLogicalPosZero(const Function &F,
                                              Type *Ty) const {
  if (!isKnownNeverPosZero())
    return false;

  // If we know there are no denormals, nothing can be flushed to zero.
  if (isKnownNeverSubnormal())
    return true;

  DenormalMode Mode = F.getDenormalMode(Ty->getScalarType()->getFltSemantics());
  switch (Mode.Input) {
  case DenormalMode::IEEE:
    return true;
  case DenormalMode::PreserveSign:
    // Negative subnormal won't flush to +0
    return isKnownNeverPosSubnormal();
  case DenormalMode::PositiveZero:
  default:
    // Both positive and negative subnormal could flush to +0
    return false;
  }

  llvm_unreachable("covered switch over denormal mode");
}

void KnownFPClass::propagateDenormal(const KnownFPClass &Src, const Function &F,
                                     Type *Ty) {
  KnownFPClasses = Src.KnownFPClasses;
  // If we aren't assuming the source can't be a zero, we don't have to check if
  // a denormal input could be flushed.
  if (!Src.isKnownNeverPosZero() && !Src.isKnownNeverNegZero())
    return;

  // If we know the input can't be a denormal, it can't be flushed to 0.
  if (Src.isKnownNeverSubnormal())
    return;

  DenormalMode Mode = F.getDenormalMode(Ty->getScalarType()->getFltSemantics());

  if (!Src.isKnownNeverPosSubnormal() && Mode != DenormalMode::getIEEE())
    KnownFPClasses |= fcPosZero;

  if (!Src.isKnownNeverNegSubnormal() && Mode != DenormalMode::getIEEE()) {
    if (Mode != DenormalMode::getPositiveZero())
      KnownFPClasses |= fcNegZero;

    if (Mode.Input == DenormalMode::PositiveZero ||
        Mode.Output == DenormalMode::PositiveZero ||
        Mode.Input == DenormalMode::Dynamic ||
        Mode.Output == DenormalMode::Dynamic)
      KnownFPClasses |= fcPosZero;
  }
}

void KnownFPClass::propagateCanonicalizingSrc(const KnownFPClass &Src,
                                              const Function &F, Type *Ty) {
  propagateDenormal(Src, F, Ty);
  propagateNaN(Src, /*PreserveSign=*/true);
}

/// Given an exploded icmp instruction, return true if the comparison only
/// checks the sign bit. If it only checks the sign bit, set TrueIfSigned if
/// the result of the comparison is true when the input value is signed.
bool llvm::isSignBitCheck(ICmpInst::Predicate Pred, const APInt &RHS,
                          bool &TrueIfSigned) {
  switch (Pred) {
  case ICmpInst::ICMP_SLT: // True if LHS s< 0
    TrueIfSigned = true;
    return RHS.isZero();
  case ICmpInst::ICMP_SLE: // True if LHS s<= -1
    TrueIfSigned = true;
    return RHS.isAllOnes();
  case ICmpInst::ICMP_SGT: // True if LHS s> -1
    TrueIfSigned = false;
    return RHS.isAllOnes();
  case ICmpInst::ICMP_SGE: // True if LHS s>= 0
    TrueIfSigned = false;
    return RHS.isZero();
  case ICmpInst::ICMP_UGT:
    // True if LHS u> RHS and RHS == sign-bit-mask - 1
    TrueIfSigned = true;
    return RHS.isMaxSignedValue();
  case ICmpInst::ICMP_UGE:
    // True if LHS u>= RHS and RHS == sign-bit-mask (2^7, 2^15, 2^31, etc)
    TrueIfSigned = true;
    return RHS.isMinSignedValue();
  case ICmpInst::ICMP_ULT:
    // True if LHS u< RHS and RHS == sign-bit-mask (2^7, 2^15, 2^31, etc)
    TrueIfSigned = false;
    return RHS.isMinSignedValue();
  case ICmpInst::ICMP_ULE:
    // True if LHS u<= RHS and RHS == sign-bit-mask - 1
    TrueIfSigned = false;
    return RHS.isMaxSignedValue();
  default:
    return false;
  }
}

/// Returns a pair of values, which if passed to llvm.is.fpclass, returns the
/// same result as an fcmp with the given operands.
std::pair<Value *, FPClassTest> llvm::fcmpToClassTest(FCmpInst::Predicate Pred,
                                                      const Function &F,
                                                      Value *LHS, Value *RHS,
                                                      bool LookThroughSrc) {
  const APFloat *ConstRHS;
  if (!match(RHS, m_APFloatAllowPoison(ConstRHS)))
    return {nullptr, fcAllFlags};

  return fcmpToClassTest(Pred, F, LHS, ConstRHS, LookThroughSrc);
}

std::pair<Value *, FPClassTest>
llvm::fcmpToClassTest(FCmpInst::Predicate Pred, const Function &F, Value *LHS,
                      const APFloat *ConstRHS, bool LookThroughSrc) {

  auto [Src, ClassIfTrue, ClassIfFalse] =
      fcmpImpliesClass(Pred, F, LHS, *ConstRHS, LookThroughSrc);
  if (Src && ClassIfTrue == ~ClassIfFalse)
    return {Src, ClassIfTrue};
  return {nullptr, fcAllFlags};
}

/// Return the return value for fcmpImpliesClass for a compare that produces an
/// exact class test.
static std::tuple<Value *, FPClassTest, FPClassTest> exactClass(Value *V,
                                                                FPClassTest M) {
  return {V, M, ~M};
}

std::tuple<Value *, FPClassTest, FPClassTest>
llvm::fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                       FPClassTest RHSClass, bool LookThroughSrc) {
  assert(RHSClass != fcNone);
  Value *Src = LHS;

  if (Pred == FCmpInst::FCMP_TRUE)
    return exactClass(Src, fcAllFlags);

  if (Pred == FCmpInst::FCMP_FALSE)
    return exactClass(Src, fcNone);

  const FPClassTest OrigClass = RHSClass;

  const bool IsNegativeRHS = (RHSClass & fcNegative) == RHSClass;
  const bool IsPositiveRHS = (RHSClass & fcPositive) == RHSClass;
  const bool IsNaN = (RHSClass & ~fcNan) == fcNone;

  if (IsNaN) {
    // fcmp o__ x, nan -> false
    // fcmp u__ x, nan -> true
    return exactClass(Src, CmpInst::isOrdered(Pred) ? fcNone : fcAllFlags);
  }

  // fcmp ord x, zero|normal|subnormal|inf -> ~fcNan
  if (Pred == FCmpInst::FCMP_ORD)
    return exactClass(Src, ~fcNan);

  // fcmp uno x, zero|normal|subnormal|inf -> fcNan
  if (Pred == FCmpInst::FCMP_UNO)
    return exactClass(Src, fcNan);

  const bool IsFabs = LookThroughSrc && match(LHS, m_FAbs(m_Value(Src)));
  if (IsFabs)
    RHSClass = llvm::inverse_fabs(RHSClass);

  const bool IsZero = (OrigClass & fcZero) == OrigClass;
  if (IsZero) {
    assert(Pred != FCmpInst::FCMP_ORD && Pred != FCmpInst::FCMP_UNO);
    // Compares with fcNone are only exactly equal to fcZero if input denormals
    // are not flushed.
    // TODO: Handle DAZ by expanding masks to cover subnormal cases.
    if (!inputDenormalIsIEEE(F, LHS->getType()))
      return {nullptr, fcAllFlags, fcAllFlags};

    switch (Pred) {
    case FCmpInst::FCMP_OEQ: // Match x == 0.0
      return exactClass(Src, fcZero);
    case FCmpInst::FCMP_UEQ: // Match isnan(x) || (x == 0.0)
      return exactClass(Src, fcZero | fcNan);
    case FCmpInst::FCMP_UNE: // Match (x != 0.0)
      return exactClass(Src, ~fcZero);
    case FCmpInst::FCMP_ONE: // Match !isnan(x) && x != 0.0
      return exactClass(Src, ~fcNan & ~fcZero);
    case FCmpInst::FCMP_ORD:
      // Canonical form of ord/uno is with a zero. We could also handle
      // non-canonical other non-NaN constants or LHS == RHS.
      return exactClass(Src, ~fcNan);
    case FCmpInst::FCMP_UNO:
      return exactClass(Src, fcNan);
    case FCmpInst::FCMP_OGT: // x > 0
      return exactClass(Src, fcPosSubnormal | fcPosNormal | fcPosInf);
    case FCmpInst::FCMP_UGT: // isnan(x) || x > 0
      return exactClass(Src, fcPosSubnormal | fcPosNormal | fcPosInf | fcNan);
    case FCmpInst::FCMP_OGE: // x >= 0
      return exactClass(Src, fcPositive | fcNegZero);
    case FCmpInst::FCMP_UGE: // isnan(x) || x >= 0
      return exactClass(Src, fcPositive | fcNegZero | fcNan);
    case FCmpInst::FCMP_OLT: // x < 0
      return exactClass(Src, fcNegSubnormal | fcNegNormal | fcNegInf);
    case FCmpInst::FCMP_ULT: // isnan(x) || x < 0
      return exactClass(Src, fcNegSubnormal | fcNegNormal | fcNegInf | fcNan);
    case FCmpInst::FCMP_OLE: // x <= 0
      return exactClass(Src, fcNegative | fcPosZero);
    case FCmpInst::FCMP_ULE: // isnan(x) || x <= 0
      return exactClass(Src, fcNegative | fcPosZero | fcNan);
    default:
      llvm_unreachable("all compare types are handled");
    }

    return {nullptr, fcAllFlags, fcAllFlags};
  }

  const bool IsDenormalRHS = (OrigClass & fcSubnormal) == OrigClass;

  const bool IsInf = (OrigClass & fcInf) == OrigClass;
  if (IsInf) {
    FPClassTest Mask = fcAllFlags;

    switch (Pred) {
    case FCmpInst::FCMP_OEQ:
    case FCmpInst::FCMP_UNE: {
      // Match __builtin_isinf patterns
      //
      //   fcmp oeq x, +inf -> is_fpclass x, fcPosInf
      //   fcmp oeq fabs(x), +inf -> is_fpclass x, fcInf
      //   fcmp oeq x, -inf -> is_fpclass x, fcNegInf
      //   fcmp oeq fabs(x), -inf -> is_fpclass x, 0 -> false
      //
      //   fcmp une x, +inf -> is_fpclass x, ~fcPosInf
      //   fcmp une fabs(x), +inf -> is_fpclass x, ~fcInf
      //   fcmp une x, -inf -> is_fpclass x, ~fcNegInf
      //   fcmp une fabs(x), -inf -> is_fpclass x, fcAllFlags -> true
      if (IsNegativeRHS) {
        Mask = fcNegInf;
        if (IsFabs)
          Mask = fcNone;
      } else {
        Mask = fcPosInf;
        if (IsFabs)
          Mask |= fcNegInf;
      }
      break;
    }
    case FCmpInst::FCMP_ONE:
    case FCmpInst::FCMP_UEQ: {
      // Match __builtin_isinf patterns
      //   fcmp one x, -inf -> is_fpclass x, fcNegInf
      //   fcmp one fabs(x), -inf -> is_fpclass x, ~fcNegInf & ~fcNan
      //   fcmp one x, +inf -> is_fpclass x, ~fcNegInf & ~fcNan
      //   fcmp one fabs(x), +inf -> is_fpclass x, ~fcInf & fcNan
      //
      //   fcmp ueq x, +inf -> is_fpclass x, fcPosInf|fcNan
      //   fcmp ueq (fabs x), +inf -> is_fpclass x, fcInf|fcNan
      //   fcmp ueq x, -inf -> is_fpclass x, fcNegInf|fcNan
      //   fcmp ueq fabs(x), -inf -> is_fpclass x, fcNan
      if (IsNegativeRHS) {
        Mask = ~fcNegInf & ~fcNan;
        if (IsFabs)
          Mask = ~fcNan;
      } else {
        Mask = ~fcPosInf & ~fcNan;
        if (IsFabs)
          Mask &= ~fcNegInf;
      }

      break;
    }
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_UGE: {
      if (IsNegativeRHS) {
        // No value is ordered and less than negative infinity.
        // All values are unordered with or at least negative infinity.
        // fcmp olt x, -inf -> false
        // fcmp uge x, -inf -> true
        Mask = fcNone;
        break;
      }

      // fcmp olt fabs(x), +inf -> fcFinite
      // fcmp uge fabs(x), +inf -> ~fcFinite
      // fcmp olt x, +inf -> fcFinite|fcNegInf
      // fcmp uge x, +inf -> ~(fcFinite|fcNegInf)
      Mask = fcFinite;
      if (!IsFabs)
        Mask |= fcNegInf;
      break;
    }
    case FCmpInst::FCMP_OGE:
    case FCmpInst::FCMP_ULT: {
      if (IsNegativeRHS) {
        // fcmp oge x, -inf -> ~fcNan
        // fcmp oge fabs(x), -inf -> ~fcNan
        // fcmp ult x, -inf -> fcNan
        // fcmp ult fabs(x), -inf -> fcNan
        Mask = ~fcNan;
        break;
      }

      // fcmp oge fabs(x), +inf -> fcInf
      // fcmp oge x, +inf -> fcPosInf
      // fcmp ult fabs(x), +inf -> ~fcInf
      // fcmp ult x, +inf -> ~fcPosInf
      Mask = fcPosInf;
      if (IsFabs)
        Mask |= fcNegInf;
      break;
    }
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_ULE: {
      if (IsNegativeRHS) {
        // fcmp ogt x, -inf -> fcmp one x, -inf
        // fcmp ogt fabs(x), -inf -> fcmp ord x, x
        // fcmp ule x, -inf -> fcmp ueq x, -inf
        // fcmp ule fabs(x), -inf -> fcmp uno x, x
        Mask = IsFabs ? ~fcNan : ~(fcNegInf | fcNan);
        break;
      }

      // No value is ordered and greater than infinity.
      Mask = fcNone;
      break;
    }
    case FCmpInst::FCMP_OLE:
    case FCmpInst::FCMP_UGT: {
      if (IsNegativeRHS) {
        Mask = IsFabs ? fcNone : fcNegInf;
        break;
      }

      // fcmp ole x, +inf -> fcmp ord x, x
      // fcmp ole fabs(x), +inf -> fcmp ord x, x
      // fcmp ole x, -inf -> fcmp oeq x, -inf
      // fcmp ole fabs(x), -inf -> false
      Mask = ~fcNan;
      break;
    }
    default:
      llvm_unreachable("all compare types are handled");
    }

    // Invert the comparison for the unordered cases.
    if (FCmpInst::isUnordered(Pred))
      Mask = ~Mask;

    return exactClass(Src, Mask);
  }

  if (Pred == FCmpInst::FCMP_OEQ)
    return {Src, RHSClass, fcAllFlags};

  if (Pred == FCmpInst::FCMP_UEQ) {
    FPClassTest Class = RHSClass | fcNan;
    return {Src, Class, ~fcNan};
  }

  if (Pred == FCmpInst::FCMP_ONE)
    return {Src, ~fcNan, RHSClass | fcNan};

  if (Pred == FCmpInst::FCMP_UNE)
    return {Src, fcAllFlags, RHSClass};

  assert((RHSClass == fcNone || RHSClass == fcPosNormal ||
          RHSClass == fcNegNormal || RHSClass == fcNormal ||
          RHSClass == fcPosSubnormal || RHSClass == fcNegSubnormal ||
          RHSClass == fcSubnormal) &&
         "should have been recognized as an exact class test");

  if (IsNegativeRHS) {
    // TODO: Handle fneg(fabs)
    if (IsFabs) {
      // fabs(x) o> -k -> fcmp ord x, x
      // fabs(x) u> -k -> true
      // fabs(x) o< -k -> false
      // fabs(x) u< -k -> fcmp uno x, x
      switch (Pred) {
      case FCmpInst::FCMP_OGT:
      case FCmpInst::FCMP_OGE:
        return {Src, ~fcNan, fcNan};
      case FCmpInst::FCMP_UGT:
      case FCmpInst::FCMP_UGE:
        return {Src, fcAllFlags, fcNone};
      case FCmpInst::FCMP_OLT:
      case FCmpInst::FCMP_OLE:
        return {Src, fcNone, fcAllFlags};
      case FCmpInst::FCMP_ULT:
      case FCmpInst::FCMP_ULE:
        return {Src, fcNan, ~fcNan};
      default:
        break;
      }

      return {nullptr, fcAllFlags, fcAllFlags};
    }

    FPClassTest ClassesLE = fcNegInf | fcNegNormal;
    FPClassTest ClassesGE = fcPositive | fcNegZero | fcNegSubnormal;

    if (IsDenormalRHS)
      ClassesLE |= fcNegSubnormal;
    else
      ClassesGE |= fcNegNormal;

    switch (Pred) {
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_OGE:
      return {Src, ClassesGE, ~ClassesGE | RHSClass};
    case FCmpInst::FCMP_UGT:
    case FCmpInst::FCMP_UGE:
      return {Src, ClassesGE | fcNan, ~(ClassesGE | fcNan) | RHSClass};
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_OLE:
      return {Src, ClassesLE, ~ClassesLE | RHSClass};
    case FCmpInst::FCMP_ULT:
    case FCmpInst::FCMP_ULE:
      return {Src, ClassesLE | fcNan, ~(ClassesLE | fcNan) | RHSClass};
    default:
      break;
    }
  } else if (IsPositiveRHS) {
    FPClassTest ClassesGE = fcPosNormal | fcPosInf;
    FPClassTest ClassesLE = fcNegative | fcPosZero | fcPosSubnormal;
    if (IsDenormalRHS)
      ClassesGE |= fcPosSubnormal;
    else
      ClassesLE |= fcPosNormal;

    if (IsFabs) {
      ClassesGE = llvm::inverse_fabs(ClassesGE);
      ClassesLE = llvm::inverse_fabs(ClassesLE);
    }

    switch (Pred) {
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_OGE:
      return {Src, ClassesGE, ~ClassesGE | RHSClass};
    case FCmpInst::FCMP_UGT:
    case FCmpInst::FCMP_UGE:
      return {Src, ClassesGE | fcNan, ~(ClassesGE | fcNan) | RHSClass};
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_OLE:
      return {Src, ClassesLE, ~ClassesLE | RHSClass};
    case FCmpInst::FCMP_ULT:
    case FCmpInst::FCMP_ULE:
      return {Src, ClassesLE | fcNan, ~(ClassesLE | fcNan) | RHSClass};
    default:
      break;
    }
  }

  return {nullptr, fcAllFlags, fcAllFlags};
}

std::tuple<Value *, FPClassTest, FPClassTest>
llvm::fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                       const APFloat &ConstRHS, bool LookThroughSrc) {
  // We can refine checks against smallest normal / largest denormal to an
  // exact class test.
  if (!ConstRHS.isNegative() && ConstRHS.isSmallestNormalized()) {
    Value *Src = LHS;
    const bool IsFabs = LookThroughSrc && match(LHS, m_FAbs(m_Value(Src)));

    FPClassTest Mask;
    // Match pattern that's used in __builtin_isnormal.
    switch (Pred) {
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_UGE: {
      // fcmp olt x, smallest_normal -> fcNegInf|fcNegNormal|fcSubnormal|fcZero
      // fcmp olt fabs(x), smallest_normal -> fcSubnormal|fcZero
      // fcmp uge x, smallest_normal -> fcNan|fcPosNormal|fcPosInf
      // fcmp uge fabs(x), smallest_normal -> ~(fcSubnormal|fcZero)
      Mask = fcZero | fcSubnormal;
      if (!IsFabs)
        Mask |= fcNegNormal | fcNegInf;

      break;
    }
    case FCmpInst::FCMP_OGE:
    case FCmpInst::FCMP_ULT: {
      // fcmp oge x, smallest_normal -> fcPosNormal | fcPosInf
      // fcmp oge fabs(x), smallest_normal -> fcInf | fcNormal
      // fcmp ult x, smallest_normal -> ~(fcPosNormal | fcPosInf)
      // fcmp ult fabs(x), smallest_normal -> ~(fcInf | fcNormal)
      Mask = fcPosInf | fcPosNormal;
      if (IsFabs)
        Mask |= fcNegInf | fcNegNormal;
      break;
    }
    default:
      return fcmpImpliesClass(Pred, F, LHS, ConstRHS.classify(),
                              LookThroughSrc);
    }

    // Invert the comparison for the unordered cases.
    if (FCmpInst::isUnordered(Pred))
      Mask = ~Mask;

    return exactClass(Src, Mask);
  }

  return fcmpImpliesClass(Pred, F, LHS, ConstRHS.classify(), LookThroughSrc);
}

std::tuple<Value *, FPClassTest, FPClassTest>
llvm::fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                       Value *RHS, bool LookThroughSrc) {
  const APFloat *ConstRHS;
  if (!match(RHS, m_APFloatAllowPoison(ConstRHS)))
    return {nullptr, fcAllFlags, fcAllFlags};

  // TODO: Just call computeKnownFPClass for RHS to handle non-constants.
  return fcmpImpliesClass(Pred, F, LHS, *ConstRHS, LookThroughSrc);
}

static void computeKnownFPClassFromCond(const Value *V, Value *Cond,
                                        bool CondIsTrue,
                                        const Instruction *CxtI,
                                        KnownFPClass &KnownFromContext) {
  CmpInst::Predicate Pred;
  Value *LHS;
  uint64_t ClassVal = 0;
  const APFloat *CRHS;
  const APInt *RHS;
  if (match(Cond, m_FCmp(Pred, m_Value(LHS), m_APFloat(CRHS)))) {
    auto [CmpVal, MaskIfTrue, MaskIfFalse] = fcmpImpliesClass(
        Pred, *CxtI->getParent()->getParent(), LHS, *CRHS, LHS != V);
    if (CmpVal == V)
      KnownFromContext.knownNot(~(CondIsTrue ? MaskIfTrue : MaskIfFalse));
  } else if (match(Cond, m_Intrinsic<Intrinsic::is_fpclass>(
                             m_Value(LHS), m_ConstantInt(ClassVal)))) {
    FPClassTest Mask = static_cast<FPClassTest>(ClassVal);
    KnownFromContext.knownNot(CondIsTrue ? ~Mask : Mask);
  } else if (match(Cond, m_ICmp(Pred, m_ElementWiseBitCast(m_Value(LHS)),
                                m_APInt(RHS)))) {
    bool TrueIfSigned;
    if (!isSignBitCheck(Pred, *RHS, TrueIfSigned))
      return;
    if (TrueIfSigned == CondIsTrue)
      KnownFromContext.signBitMustBeOne();
    else
      KnownFromContext.signBitMustBeZero();
  }
}

static KnownFPClass computeKnownFPClassFromContext(const Value *V,
                                                   const SimplifyQuery &Q) {
  KnownFPClass KnownFromContext;

  if (!Q.CxtI)
    return KnownFromContext;

  if (Q.DC && Q.DT) {
    // Handle dominating conditions.
    for (BranchInst *BI : Q.DC->conditionsFor(V)) {
      Value *Cond = BI->getCondition();

      BasicBlockEdge Edge0(BI->getParent(), BI->getSuccessor(0));
      if (Q.DT->dominates(Edge0, Q.CxtI->getParent()))
        computeKnownFPClassFromCond(V, Cond, /*CondIsTrue=*/true, Q.CxtI,
                                    KnownFromContext);

      BasicBlockEdge Edge1(BI->getParent(), BI->getSuccessor(1));
      if (Q.DT->dominates(Edge1, Q.CxtI->getParent()))
        computeKnownFPClassFromCond(V, Cond, /*CondIsTrue=*/false, Q.CxtI,
                                    KnownFromContext);
    }
  }

  if (!Q.AC)
    return KnownFromContext;

  // Try to restrict the floating-point classes based on information from
  // assumptions.
  for (auto &AssumeVH : Q.AC->assumptionsFor(V)) {
    if (!AssumeVH)
      continue;
    CallInst *I = cast<CallInst>(AssumeVH);

    assert(I->getFunction() == Q.CxtI->getParent()->getParent() &&
           "Got assumption for the wrong function!");
    assert(I->getIntrinsicID() == Intrinsic::assume &&
           "must be an assume intrinsic");

    if (!isValidAssumeForContext(I, Q.CxtI, Q.DT))
      continue;

    computeKnownFPClassFromCond(V, I->getArgOperand(0), /*CondIsTrue=*/true,
                                Q.CxtI, KnownFromContext);
  }

  return KnownFromContext;
}

void computeKnownFPClass(const Value *V, const APInt &DemandedElts,
                         FPClassTest InterestedClasses, KnownFPClass &Known,
                         unsigned Depth, const SimplifyQuery &Q);

static void computeKnownFPClass(const Value *V, KnownFPClass &Known,
                                FPClassTest InterestedClasses, unsigned Depth,
                                const SimplifyQuery &Q) {
  auto *FVTy = dyn_cast<FixedVectorType>(V->getType());
  APInt DemandedElts =
      FVTy ? APInt::getAllOnes(FVTy->getNumElements()) : APInt(1, 1);
  computeKnownFPClass(V, DemandedElts, InterestedClasses, Known, Depth, Q);
}

static void computeKnownFPClassForFPTrunc(const Operator *Op,
                                          const APInt &DemandedElts,
                                          FPClassTest InterestedClasses,
                                          KnownFPClass &Known, unsigned Depth,
                                          const SimplifyQuery &Q) {
  if ((InterestedClasses &
       (KnownFPClass::OrderedLessThanZeroMask | fcNan)) == fcNone)
    return;

  KnownFPClass KnownSrc;
  computeKnownFPClass(Op->getOperand(0), DemandedElts, InterestedClasses,
                      KnownSrc, Depth + 1, Q);

  // Sign should be preserved
  // TODO: Handle cannot be ordered greater than zero
  if (KnownSrc.cannotBeOrderedLessThanZero())
    Known.knownNot(KnownFPClass::OrderedLessThanZeroMask);

  Known.propagateNaN(KnownSrc, true);

  // Infinity needs a range check.
}

void computeKnownFPClass(const Value *V, const APInt &DemandedElts,
                         FPClassTest InterestedClasses, KnownFPClass &Known,
                         unsigned Depth, const SimplifyQuery &Q) {
  assert(Known.isUnknown() && "should not be called with known information");

  if (!DemandedElts) {
    // No demanded elts, better to assume we don't know anything.
    Known.resetAll();
    return;
  }

  assert(Depth <= MaxAnalysisRecursionDepth && "Limit Search Depth");

  if (auto *CFP = dyn_cast<ConstantFP>(V)) {
    Known.KnownFPClasses = CFP->getValueAPF().classify();
    Known.SignBit = CFP->isNegative();
    return;
  }

  if (isa<ConstantAggregateZero>(V)) {
    Known.KnownFPClasses = fcPosZero;
    Known.SignBit = false;
    return;
  }

  if (isa<PoisonValue>(V)) {
    Known.KnownFPClasses = fcNone;
    Known.SignBit = false;
    return;
  }

  // Try to handle fixed width vector constants
  auto *VFVTy = dyn_cast<FixedVectorType>(V->getType());
  const Constant *CV = dyn_cast<Constant>(V);
  if (VFVTy && CV) {
    Known.KnownFPClasses = fcNone;
    bool SignBitAllZero = true;
    bool SignBitAllOne = true;

    // For vectors, verify that each element is not NaN.
    unsigned NumElts = VFVTy->getNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      if (!DemandedElts[i])
        continue;

      Constant *Elt = CV->getAggregateElement(i);
      if (!Elt) {
        Known = KnownFPClass();
        return;
      }
      if (isa<PoisonValue>(Elt))
        continue;
      auto *CElt = dyn_cast<ConstantFP>(Elt);
      if (!CElt) {
        Known = KnownFPClass();
        return;
      }

      const APFloat &C = CElt->getValueAPF();
      Known.KnownFPClasses |= C.classify();
      if (C.isNegative())
        SignBitAllZero = false;
      else
        SignBitAllOne = false;
    }
    if (SignBitAllOne != SignBitAllZero)
      Known.SignBit = SignBitAllOne;
    return;
  }

  FPClassTest KnownNotFromFlags = fcNone;
  if (const auto *CB = dyn_cast<CallBase>(V))
    KnownNotFromFlags |= CB->getRetNoFPClass();
  else if (const auto *Arg = dyn_cast<Argument>(V))
    KnownNotFromFlags |= Arg->getNoFPClass();

  const Operator *Op = dyn_cast<Operator>(V);
  if (const FPMathOperator *FPOp = dyn_cast_or_null<FPMathOperator>(Op)) {
    if (FPOp->hasNoNaNs())
      KnownNotFromFlags |= fcNan;
    if (FPOp->hasNoInfs())
      KnownNotFromFlags |= fcInf;
  }

  KnownFPClass AssumedClasses = computeKnownFPClassFromContext(V, Q);
  KnownNotFromFlags |= ~AssumedClasses.KnownFPClasses;

  // We no longer need to find out about these bits from inputs if we can
  // assume this from flags/attributes.
  InterestedClasses &= ~KnownNotFromFlags;

  auto ClearClassesFromFlags = make_scope_exit([=, &Known] {
    Known.knownNot(KnownNotFromFlags);
    if (!Known.SignBit && AssumedClasses.SignBit) {
      if (*AssumedClasses.SignBit)
        Known.signBitMustBeOne();
      else
        Known.signBitMustBeZero();
    }
  });

  if (!Op)
    return;

  // All recursive calls that increase depth must come after this.
  if (Depth == MaxAnalysisRecursionDepth)
    return;

  const unsigned Opc = Op->getOpcode();
  switch (Opc) {
  case Instruction::FNeg: {
    computeKnownFPClass(Op->getOperand(0), DemandedElts, InterestedClasses,
                        Known, Depth + 1, Q);
    Known.fneg();
    break;
  }
  case Instruction::Select: {
    Value *Cond = Op->getOperand(0);
    Value *LHS = Op->getOperand(1);
    Value *RHS = Op->getOperand(2);

    FPClassTest FilterLHS = fcAllFlags;
    FPClassTest FilterRHS = fcAllFlags;

    Value *TestedValue = nullptr;
    FPClassTest MaskIfTrue = fcAllFlags;
    FPClassTest MaskIfFalse = fcAllFlags;
    uint64_t ClassVal = 0;
    const Function *F = cast<Instruction>(Op)->getFunction();
    CmpInst::Predicate Pred;
    Value *CmpLHS, *CmpRHS;
    if (F && match(Cond, m_FCmp(Pred, m_Value(CmpLHS), m_Value(CmpRHS)))) {
      // If the select filters out a value based on the class, it no longer
      // participates in the class of the result

      // TODO: In some degenerate cases we can infer something if we try again
      // without looking through sign operations.
      bool LookThroughFAbsFNeg = CmpLHS != LHS && CmpLHS != RHS;
      std::tie(TestedValue, MaskIfTrue, MaskIfFalse) =
          fcmpImpliesClass(Pred, *F, CmpLHS, CmpRHS, LookThroughFAbsFNeg);
    } else if (match(Cond,
                     m_Intrinsic<Intrinsic::is_fpclass>(
                         m_Value(TestedValue), m_ConstantInt(ClassVal)))) {
      FPClassTest TestedMask = static_cast<FPClassTest>(ClassVal);
      MaskIfTrue = TestedMask;
      MaskIfFalse = ~TestedMask;
    }

    if (TestedValue == LHS) {
      // match !isnan(x) ? x : y
      FilterLHS = MaskIfTrue;
    } else if (TestedValue == RHS) { // && IsExactClass
      // match !isnan(x) ? y : x
      FilterRHS = MaskIfFalse;
    }

    KnownFPClass Known2;
    computeKnownFPClass(LHS, DemandedElts, InterestedClasses & FilterLHS, Known,
                        Depth + 1, Q);
    Known.KnownFPClasses &= FilterLHS;

    computeKnownFPClass(RHS, DemandedElts, InterestedClasses & FilterRHS,
                        Known2, Depth + 1, Q);
    Known2.KnownFPClasses &= FilterRHS;

    Known |= Known2;
    break;
  }
  case Instruction::Call: {
    const CallInst *II = cast<CallInst>(Op);
    const Intrinsic::ID IID = II->getIntrinsicID();
    switch (IID) {
    case Intrinsic::fabs: {
      if ((InterestedClasses & (fcNan | fcPositive)) != fcNone) {
        // If we only care about the sign bit we don't need to inspect the
        // operand.
        computeKnownFPClass(II->getArgOperand(0), DemandedElts,
                            InterestedClasses, Known, Depth + 1, Q);
      }

      Known.fabs();
      break;
    }
    case Intrinsic::copysign: {
      KnownFPClass KnownSign;

      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedClasses,
                          Known, Depth + 1, Q);
      computeKnownFPClass(II->getArgOperand(1), DemandedElts, InterestedClasses,
                          KnownSign, Depth + 1, Q);
      Known.copysign(KnownSign);
      break;
    }
    case Intrinsic::fma:
    case Intrinsic::fmuladd: {
      if ((InterestedClasses & fcNegative) == fcNone)
        break;

      if (II->getArgOperand(0) != II->getArgOperand(1))
        break;

      // The multiply cannot be -0 and therefore the add can't be -0
      Known.knownNot(fcNegZero);

      // x * x + y is non-negative if y is non-negative.
      KnownFPClass KnownAddend;
      computeKnownFPClass(II->getArgOperand(2), DemandedElts, InterestedClasses,
                          KnownAddend, Depth + 1, Q);

      if (KnownAddend.cannotBeOrderedLessThanZero())
        Known.knownNot(fcNegative);
      break;
    }
    case Intrinsic::sqrt:
    case Intrinsic::experimental_constrained_sqrt: {
      KnownFPClass KnownSrc;
      FPClassTest InterestedSrcs = InterestedClasses;
      if (InterestedClasses & fcNan)
        InterestedSrcs |= KnownFPClass::OrderedLessThanZeroMask;

      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedSrcs,
                          KnownSrc, Depth + 1, Q);

      if (KnownSrc.isKnownNeverPosInfinity())
        Known.knownNot(fcPosInf);
      if (KnownSrc.isKnownNever(fcSNan))
        Known.knownNot(fcSNan);

      // Any negative value besides -0 returns a nan.
      if (KnownSrc.isKnownNeverNaN() && KnownSrc.cannotBeOrderedLessThanZero())
        Known.knownNot(fcNan);

      // The only negative value that can be returned is -0 for -0 inputs.
      Known.knownNot(fcNegInf | fcNegSubnormal | fcNegNormal);

      // If the input denormal mode could be PreserveSign, a negative
      // subnormal input could produce a negative zero output.
      const Function *F = II->getFunction();
      if (Q.IIQ.hasNoSignedZeros(II) ||
          (F && KnownSrc.isKnownNeverLogicalNegZero(*F, II->getType())))
        Known.knownNot(fcNegZero);

      break;
    }
    case Intrinsic::sin:
    case Intrinsic::cos: {
      // Return NaN on infinite inputs.
      KnownFPClass KnownSrc;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedClasses,
                          KnownSrc, Depth + 1, Q);
      Known.knownNot(fcInf);
      if (KnownSrc.isKnownNeverNaN() && KnownSrc.isKnownNeverInfinity())
        Known.knownNot(fcNan);
      break;
    }
    case Intrinsic::maxnum:
    case Intrinsic::minnum:
    case Intrinsic::minimum:
    case Intrinsic::maximum: {
      KnownFPClass KnownLHS, KnownRHS;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedClasses,
                          KnownLHS, Depth + 1, Q);
      computeKnownFPClass(II->getArgOperand(1), DemandedElts, InterestedClasses,
                          KnownRHS, Depth + 1, Q);

      bool NeverNaN = KnownLHS.isKnownNeverNaN() || KnownRHS.isKnownNeverNaN();
      Known = KnownLHS | KnownRHS;

      // If either operand is not NaN, the result is not NaN.
      if (NeverNaN && (IID == Intrinsic::minnum || IID == Intrinsic::maxnum))
        Known.knownNot(fcNan);

      if (IID == Intrinsic::maxnum) {
        // If at least one operand is known to be positive, the result must be
        // positive.
        if ((KnownLHS.cannotBeOrderedLessThanZero() &&
             KnownLHS.isKnownNeverNaN()) ||
            (KnownRHS.cannotBeOrderedLessThanZero() &&
             KnownRHS.isKnownNeverNaN()))
          Known.knownNot(KnownFPClass::OrderedLessThanZeroMask);
      } else if (IID == Intrinsic::maximum) {
        // If at least one operand is known to be positive, the result must be
        // positive.
        if (KnownLHS.cannotBeOrderedLessThanZero() ||
            KnownRHS.cannotBeOrderedLessThanZero())
          Known.knownNot(KnownFPClass::OrderedLessThanZeroMask);
      } else if (IID == Intrinsic::minnum) {
        // If at least one operand is known to be negative, the result must be
        // negative.
        if ((KnownLHS.cannotBeOrderedGreaterThanZero() &&
             KnownLHS.isKnownNeverNaN()) ||
            (KnownRHS.cannotBeOrderedGreaterThanZero() &&
             KnownRHS.isKnownNeverNaN()))
          Known.knownNot(KnownFPClass::OrderedGreaterThanZeroMask);
      } else {
        // If at least one operand is known to be negative, the result must be
        // negative.
        if (KnownLHS.cannotBeOrderedGreaterThanZero() ||
            KnownRHS.cannotBeOrderedGreaterThanZero())
          Known.knownNot(KnownFPClass::OrderedGreaterThanZeroMask);
      }

      // Fixup zero handling if denormals could be returned as a zero.
      //
      // As there's no spec for denormal flushing, be conservative with the
      // treatment of denormals that could be flushed to zero. For older
      // subtargets on AMDGPU the min/max instructions would not flush the
      // output and return the original value.
      //
      if ((Known.KnownFPClasses & fcZero) != fcNone &&
          !Known.isKnownNeverSubnormal()) {
        const Function *Parent = II->getFunction();
        if (!Parent)
          break;

        DenormalMode Mode = Parent->getDenormalMode(
            II->getType()->getScalarType()->getFltSemantics());
        if (Mode != DenormalMode::getIEEE())
          Known.KnownFPClasses |= fcZero;
      }

      if (Known.isKnownNeverNaN()) {
        if (KnownLHS.SignBit && KnownRHS.SignBit &&
            *KnownLHS.SignBit == *KnownRHS.SignBit) {
          if (*KnownLHS.SignBit)
            Known.signBitMustBeOne();
          else
            Known.signBitMustBeZero();
        } else if ((IID == Intrinsic::maximum || IID == Intrinsic::minimum) ||
                   ((KnownLHS.isKnownNeverNegZero() ||
                     KnownRHS.isKnownNeverPosZero()) &&
                    (KnownLHS.isKnownNeverPosZero() ||
                     KnownRHS.isKnownNeverNegZero()))) {
          if ((IID == Intrinsic::maximum || IID == Intrinsic::maxnum) &&
              (KnownLHS.SignBit == false || KnownRHS.SignBit == false))
            Known.signBitMustBeZero();
          else if ((IID == Intrinsic::minimum || IID == Intrinsic::minnum) &&
                   (KnownLHS.SignBit == true || KnownRHS.SignBit == true))
            Known.signBitMustBeOne();
        }
      }
      break;
    }
    case Intrinsic::canonicalize: {
      KnownFPClass KnownSrc;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedClasses,
                          KnownSrc, Depth + 1, Q);

      // This is essentially a stronger form of
      // propagateCanonicalizingSrc. Other "canonicalizing" operations don't
      // actually have an IR canonicalization guarantee.

      // Canonicalize may flush denormals to zero, so we have to consider the
      // denormal mode to preserve known-not-0 knowledge.
      Known.KnownFPClasses = KnownSrc.KnownFPClasses | fcZero | fcQNan;

      // Stronger version of propagateNaN
      // Canonicalize is guaranteed to quiet signaling nans.
      if (KnownSrc.isKnownNeverNaN())
        Known.knownNot(fcNan);
      else
        Known.knownNot(fcSNan);

      const Function *F = II->getFunction();
      if (!F)
        break;

      // If the parent function flushes denormals, the canonical output cannot
      // be a denormal.
      const fltSemantics &FPType =
          II->getType()->getScalarType()->getFltSemantics();
      DenormalMode DenormMode = F->getDenormalMode(FPType);
      if (DenormMode == DenormalMode::getIEEE()) {
        if (KnownSrc.isKnownNever(fcPosZero))
          Known.knownNot(fcPosZero);
        if (KnownSrc.isKnownNever(fcNegZero))
          Known.knownNot(fcNegZero);
        break;
      }

      if (DenormMode.inputsAreZero() || DenormMode.outputsAreZero())
        Known.knownNot(fcSubnormal);

      if (DenormMode.Input == DenormalMode::PositiveZero ||
          (DenormMode.Output == DenormalMode::PositiveZero &&
           DenormMode.Input == DenormalMode::IEEE))
        Known.knownNot(fcNegZero);

      break;
    }
    case Intrinsic::vector_reduce_fmax:
    case Intrinsic::vector_reduce_fmin:
    case Intrinsic::vector_reduce_fmaximum:
    case Intrinsic::vector_reduce_fminimum: {
      // reduce min/max will choose an element from one of the vector elements,
      // so we can infer and class information that is common to all elements.
      Known = computeKnownFPClass(II->getArgOperand(0), II->getFastMathFlags(),
                                  InterestedClasses, Depth + 1, Q);
      // Can only propagate sign if output is never NaN.
      if (!Known.isKnownNeverNaN())
        Known.SignBit.reset();
      break;
    }
      // reverse preserves all characteristics of the input vec's element.
    case Intrinsic::vector_reverse:
      Known = computeKnownFPClass(
          II->getArgOperand(0), DemandedElts.reverseBits(),
          II->getFastMathFlags(), InterestedClasses, Depth + 1, Q);
      break;
    case Intrinsic::trunc:
    case Intrinsic::floor:
    case Intrinsic::ceil:
    case Intrinsic::rint:
    case Intrinsic::nearbyint:
    case Intrinsic::round:
    case Intrinsic::roundeven: {
      KnownFPClass KnownSrc;
      FPClassTest InterestedSrcs = InterestedClasses;
      if (InterestedSrcs & fcPosFinite)
        InterestedSrcs |= fcPosFinite;
      if (InterestedSrcs & fcNegFinite)
        InterestedSrcs |= fcNegFinite;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedSrcs,
                          KnownSrc, Depth + 1, Q);

      // Integer results cannot be subnormal.
      Known.knownNot(fcSubnormal);

      Known.propagateNaN(KnownSrc, true);

      // Pass through infinities, except PPC_FP128 is a special case for
      // intrinsics other than trunc.
      if (IID == Intrinsic::trunc || !V->getType()->isMultiUnitFPType()) {
        if (KnownSrc.isKnownNeverPosInfinity())
          Known.knownNot(fcPosInf);
        if (KnownSrc.isKnownNeverNegInfinity())
          Known.knownNot(fcNegInf);
      }

      // Negative round ups to 0 produce -0
      if (KnownSrc.isKnownNever(fcPosFinite))
        Known.knownNot(fcPosFinite);
      if (KnownSrc.isKnownNever(fcNegFinite))
        Known.knownNot(fcNegFinite);

      break;
    }
    case Intrinsic::exp:
    case Intrinsic::exp2:
    case Intrinsic::exp10: {
      Known.knownNot(fcNegative);
      if ((InterestedClasses & fcNan) == fcNone)
        break;

      KnownFPClass KnownSrc;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedClasses,
                          KnownSrc, Depth + 1, Q);
      if (KnownSrc.isKnownNeverNaN()) {
        Known.knownNot(fcNan);
        Known.signBitMustBeZero();
      }

      break;
    }
    case Intrinsic::fptrunc_round: {
      computeKnownFPClassForFPTrunc(Op, DemandedElts, InterestedClasses, Known,
                                    Depth, Q);
      break;
    }
    case Intrinsic::log:
    case Intrinsic::log10:
    case Intrinsic::log2:
    case Intrinsic::experimental_constrained_log:
    case Intrinsic::experimental_constrained_log10:
    case Intrinsic::experimental_constrained_log2: {
      // log(+inf) -> +inf
      // log([+-]0.0) -> -inf
      // log(-inf) -> nan
      // log(-x) -> nan
      if ((InterestedClasses & (fcNan | fcInf)) == fcNone)
        break;

      FPClassTest InterestedSrcs = InterestedClasses;
      if ((InterestedClasses & fcNegInf) != fcNone)
        InterestedSrcs |= fcZero | fcSubnormal;
      if ((InterestedClasses & fcNan) != fcNone)
        InterestedSrcs |= fcNan | (fcNegative & ~fcNan);

      KnownFPClass KnownSrc;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedSrcs,
                          KnownSrc, Depth + 1, Q);

      if (KnownSrc.isKnownNeverPosInfinity())
        Known.knownNot(fcPosInf);

      if (KnownSrc.isKnownNeverNaN() && KnownSrc.cannotBeOrderedLessThanZero())
        Known.knownNot(fcNan);

      const Function *F = II->getFunction();
      if (F && KnownSrc.isKnownNeverLogicalZero(*F, II->getType()))
        Known.knownNot(fcNegInf);

      break;
    }
    case Intrinsic::powi: {
      if ((InterestedClasses & fcNegative) == fcNone)
        break;

      const Value *Exp = II->getArgOperand(1);
      Type *ExpTy = Exp->getType();
      unsigned BitWidth = ExpTy->getScalarType()->getIntegerBitWidth();
      KnownBits ExponentKnownBits(BitWidth);
      computeKnownBits(Exp, isa<VectorType>(ExpTy) ? DemandedElts : APInt(1, 1),
                       ExponentKnownBits, Depth + 1, Q);

      if (ExponentKnownBits.Zero[0]) { // Is even
        Known.knownNot(fcNegative);
        break;
      }

      // Given that exp is an integer, here are the
      // ways that pow can return a negative value:
      //
      //   pow(-x, exp)   --> negative if exp is odd and x is negative.
      //   pow(-0, exp)   --> -inf if exp is negative odd.
      //   pow(-0, exp)   --> -0 if exp is positive odd.
      //   pow(-inf, exp) --> -0 if exp is negative odd.
      //   pow(-inf, exp) --> -inf if exp is positive odd.
      KnownFPClass KnownSrc;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, fcNegative,
                          KnownSrc, Depth + 1, Q);
      if (KnownSrc.isKnownNever(fcNegative))
        Known.knownNot(fcNegative);
      break;
    }
    case Intrinsic::ldexp: {
      KnownFPClass KnownSrc;
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedClasses,
                          KnownSrc, Depth + 1, Q);
      Known.propagateNaN(KnownSrc, /*PropagateSign=*/true);

      // Sign is preserved, but underflows may produce zeroes.
      if (KnownSrc.isKnownNever(fcNegative))
        Known.knownNot(fcNegative);
      else if (KnownSrc.cannotBeOrderedLessThanZero())
        Known.knownNot(KnownFPClass::OrderedLessThanZeroMask);

      if (KnownSrc.isKnownNever(fcPositive))
        Known.knownNot(fcPositive);
      else if (KnownSrc.cannotBeOrderedGreaterThanZero())
        Known.knownNot(KnownFPClass::OrderedGreaterThanZeroMask);

      // Can refine inf/zero handling based on the exponent operand.
      const FPClassTest ExpInfoMask = fcZero | fcSubnormal | fcInf;
      if ((InterestedClasses & ExpInfoMask) == fcNone)
        break;
      if ((KnownSrc.KnownFPClasses & ExpInfoMask) == fcNone)
        break;

      const fltSemantics &Flt =
          II->getType()->getScalarType()->getFltSemantics();
      unsigned Precision = APFloat::semanticsPrecision(Flt);
      const Value *ExpArg = II->getArgOperand(1);
      ConstantRange ExpRange = computeConstantRange(
          ExpArg, true, Q.IIQ.UseInstrInfo, Q.AC, Q.CxtI, Q.DT, Depth + 1);

      const int MantissaBits = Precision - 1;
      if (ExpRange.getSignedMin().sge(static_cast<int64_t>(MantissaBits)))
        Known.knownNot(fcSubnormal);

      const Function *F = II->getFunction();
      const APInt *ConstVal = ExpRange.getSingleElement();
      if (ConstVal && ConstVal->isZero()) {
        // ldexp(x, 0) -> x, so propagate everything.
        Known.propagateCanonicalizingSrc(KnownSrc, *F, II->getType());
      } else if (ExpRange.isAllNegative()) {
        // If we know the power is <= 0, can't introduce inf
        if (KnownSrc.isKnownNeverPosInfinity())
          Known.knownNot(fcPosInf);
        if (KnownSrc.isKnownNeverNegInfinity())
          Known.knownNot(fcNegInf);
      } else if (ExpRange.isAllNonNegative()) {
        // If we know the power is >= 0, can't introduce subnormal or zero
        if (KnownSrc.isKnownNeverPosSubnormal())
          Known.knownNot(fcPosSubnormal);
        if (KnownSrc.isKnownNeverNegSubnormal())
          Known.knownNot(fcNegSubnormal);
        if (F && KnownSrc.isKnownNeverLogicalPosZero(*F, II->getType()))
          Known.knownNot(fcPosZero);
        if (F && KnownSrc.isKnownNeverLogicalNegZero(*F, II->getType()))
          Known.knownNot(fcNegZero);
      }

      break;
    }
    case Intrinsic::arithmetic_fence: {
      computeKnownFPClass(II->getArgOperand(0), DemandedElts, InterestedClasses,
                          Known, Depth + 1, Q);
      break;
    }
    case Intrinsic::experimental_constrained_sitofp:
    case Intrinsic::experimental_constrained_uitofp:
      // Cannot produce nan
      Known.knownNot(fcNan);

      // sitofp and uitofp turn into +0.0 for zero.
      Known.knownNot(fcNegZero);

      // Integers cannot be subnormal
      Known.knownNot(fcSubnormal);

      if (IID == Intrinsic::experimental_constrained_uitofp)
        Known.signBitMustBeZero();

      // TODO: Copy inf handling from instructions
      break;
    default:
      break;
    }

    break;
  }
  case Instruction::FAdd:
  case Instruction::FSub: {
    KnownFPClass KnownLHS, KnownRHS;
    bool WantNegative =
        Op->getOpcode() == Instruction::FAdd &&
        (InterestedClasses & KnownFPClass::OrderedLessThanZeroMask) != fcNone;
    bool WantNaN = (InterestedClasses & fcNan) != fcNone;
    bool WantNegZero = (InterestedClasses & fcNegZero) != fcNone;

    if (!WantNaN && !WantNegative && !WantNegZero)
      break;

    FPClassTest InterestedSrcs = InterestedClasses;
    if (WantNegative)
      InterestedSrcs |= KnownFPClass::OrderedLessThanZeroMask;
    if (InterestedClasses & fcNan)
      InterestedSrcs |= fcInf;
    computeKnownFPClass(Op->getOperand(1), DemandedElts, InterestedSrcs,
                        KnownRHS, Depth + 1, Q);

    if ((WantNaN && KnownRHS.isKnownNeverNaN()) ||
        (WantNegative && KnownRHS.cannotBeOrderedLessThanZero()) ||
        WantNegZero || Opc == Instruction::FSub) {

      // RHS is canonically cheaper to compute. Skip inspecting the LHS if
      // there's no point.
      computeKnownFPClass(Op->getOperand(0), DemandedElts, InterestedSrcs,
                          KnownLHS, Depth + 1, Q);
      // Adding positive and negative infinity produces NaN.
      // TODO: Check sign of infinities.
      if (KnownLHS.isKnownNeverNaN() && KnownRHS.isKnownNeverNaN() &&
          (KnownLHS.isKnownNeverInfinity() || KnownRHS.isKnownNeverInfinity()))
        Known.knownNot(fcNan);

      // FIXME: Context function should always be passed in separately
      const Function *F = cast<Instruction>(Op)->getFunction();

      if (Op->getOpcode() == Instruction::FAdd) {
        if (KnownLHS.cannotBeOrderedLessThanZero() &&
            KnownRHS.cannotBeOrderedLessThanZero())
          Known.knownNot(KnownFPClass::OrderedLessThanZeroMask);
        if (!F)
          break;

        // (fadd x, 0.0) is guaranteed to return +0.0, not -0.0.
        if ((KnownLHS.isKnownNeverLogicalNegZero(*F, Op->getType()) ||
             KnownRHS.isKnownNeverLogicalNegZero(*F, Op->getType())) &&
            // Make sure output negative denormal can't flush to -0
            outputDenormalIsIEEEOrPosZero(*F, Op->getType()))
          Known.knownNot(fcNegZero);
      } else {
        if (!F)
          break;

        // Only fsub -0, +0 can return -0
        if ((KnownLHS.isKnownNeverLogicalNegZero(*F, Op->getType()) ||
             KnownRHS.isKnownNeverLogicalPosZero(*F, Op->getType())) &&
            // Make sure output negative denormal can't flush to -0
            outputDenormalIsIEEEOrPosZero(*F, Op->getType()))
          Known.knownNot(fcNegZero);
      }
    }

    break;
  }
  case Instruction::FMul: {
    // X * X is always non-negative or a NaN.
    if (Op->getOperand(0) == Op->getOperand(1))
      Known.knownNot(fcNegative);

    if ((InterestedClasses & fcNan) != fcNan)
      break;

    // fcSubnormal is only needed in case of DAZ.
    const FPClassTest NeedForNan = fcNan | fcInf | fcZero | fcSubnormal;

    KnownFPClass KnownLHS, KnownRHS;
    computeKnownFPClass(Op->getOperand(1), DemandedElts, NeedForNan, KnownRHS,
                        Depth + 1, Q);
    if (!KnownRHS.isKnownNeverNaN())
      break;

    computeKnownFPClass(Op->getOperand(0), DemandedElts, NeedForNan, KnownLHS,
                        Depth + 1, Q);
    if (!KnownLHS.isKnownNeverNaN())
      break;

    if (KnownLHS.SignBit && KnownRHS.SignBit) {
      if (*KnownLHS.SignBit == *KnownRHS.SignBit)
        Known.signBitMustBeZero();
      else
        Known.signBitMustBeOne();
    }

    // If 0 * +/-inf produces NaN.
    if (KnownLHS.isKnownNeverInfinity() && KnownRHS.isKnownNeverInfinity()) {
      Known.knownNot(fcNan);
      break;
    }

    const Function *F = cast<Instruction>(Op)->getFunction();
    if (!F)
      break;

    if ((KnownRHS.isKnownNeverInfinity() ||
         KnownLHS.isKnownNeverLogicalZero(*F, Op->getType())) &&
        (KnownLHS.isKnownNeverInfinity() ||
         KnownRHS.isKnownNeverLogicalZero(*F, Op->getType())))
      Known.knownNot(fcNan);

    break;
  }
  case Instruction::FDiv:
  case Instruction::FRem: {
    if (Op->getOperand(0) == Op->getOperand(1)) {
      // TODO: Could filter out snan if we inspect the operand
      if (Op->getOpcode() == Instruction::FDiv) {
        // X / X is always exactly 1.0 or a NaN.
        Known.KnownFPClasses = fcNan | fcPosNormal;
      } else {
        // X % X is always exactly [+-]0.0 or a NaN.
        Known.KnownFPClasses = fcNan | fcZero;
      }

      break;
    }

    const bool WantNan = (InterestedClasses & fcNan) != fcNone;
    const bool WantNegative = (InterestedClasses & fcNegative) != fcNone;
    const bool WantPositive =
        Opc == Instruction::FRem && (InterestedClasses & fcPositive) != fcNone;
    if (!WantNan && !WantNegative && !WantPositive)
      break;

    KnownFPClass KnownLHS, KnownRHS;

    computeKnownFPClass(Op->getOperand(1), DemandedElts,
                        fcNan | fcInf | fcZero | fcNegative, KnownRHS,
                        Depth + 1, Q);

    bool KnowSomethingUseful =
        KnownRHS.isKnownNeverNaN() || KnownRHS.isKnownNever(fcNegative);

    if (KnowSomethingUseful || WantPositive) {
      const FPClassTest InterestedLHS =
          WantPositive ? fcAllFlags
                       : fcNan | fcInf | fcZero | fcSubnormal | fcNegative;

      computeKnownFPClass(Op->getOperand(0), DemandedElts,
                          InterestedClasses & InterestedLHS, KnownLHS,
                          Depth + 1, Q);
    }

    const Function *F = cast<Instruction>(Op)->getFunction();

    if (Op->getOpcode() == Instruction::FDiv) {
      // Only 0/0, Inf/Inf produce NaN.
      if (KnownLHS.isKnownNeverNaN() && KnownRHS.isKnownNeverNaN() &&
          (KnownLHS.isKnownNeverInfinity() ||
           KnownRHS.isKnownNeverInfinity()) &&
          ((F && KnownLHS.isKnownNeverLogicalZero(*F, Op->getType())) ||
           (F && KnownRHS.isKnownNeverLogicalZero(*F, Op->getType())))) {
        Known.knownNot(fcNan);
      }

      // X / -0.0 is -Inf (or NaN).
      // +X / +X is +X
      if (KnownLHS.isKnownNever(fcNegative) && KnownRHS.isKnownNever(fcNegative))
        Known.knownNot(fcNegative);
    } else {
      // Inf REM x and x REM 0 produce NaN.
      if (KnownLHS.isKnownNeverNaN() && KnownRHS.isKnownNeverNaN() &&
          KnownLHS.isKnownNeverInfinity() && F &&
          KnownRHS.isKnownNeverLogicalZero(*F, Op->getType())) {
        Known.knownNot(fcNan);
      }

      // The sign for frem is the same as the first operand.
      if (KnownLHS.cannotBeOrderedLessThanZero())
        Known.knownNot(KnownFPClass::OrderedLessThanZeroMask);
      if (KnownLHS.cannotBeOrderedGreaterThanZero())
        Known.knownNot(KnownFPClass::OrderedGreaterThanZeroMask);

      // See if we can be more aggressive about the sign of 0.
      if (KnownLHS.isKnownNever(fcNegative))
        Known.knownNot(fcNegative);
      if (KnownLHS.isKnownNever(fcPositive))
        Known.knownNot(fcPositive);
    }

    break;
  }
  case Instruction::FPExt: {
    // Infinity, nan and zero propagate from source.
    computeKnownFPClass(Op->getOperand(0), DemandedElts, InterestedClasses,
                        Known, Depth + 1, Q);

    const fltSemantics &DstTy =
        Op->getType()->getScalarType()->getFltSemantics();
    const fltSemantics &SrcTy =
        Op->getOperand(0)->getType()->getScalarType()->getFltSemantics();

    // All subnormal inputs should be in the normal range in the result type.
    if (APFloat::isRepresentableAsNormalIn(SrcTy, DstTy)) {
      if (Known.KnownFPClasses & fcPosSubnormal)
        Known.KnownFPClasses |= fcPosNormal;
      if (Known.KnownFPClasses & fcNegSubnormal)
        Known.KnownFPClasses |= fcNegNormal;
      Known.knownNot(fcSubnormal);
    }

    // Sign bit of a nan isn't guaranteed.
    if (!Known.isKnownNeverNaN())
      Known.SignBit = std::nullopt;
    break;
  }
  case Instruction::FPTrunc: {
    computeKnownFPClassForFPTrunc(Op, DemandedElts, InterestedClasses, Known,
                                  Depth, Q);
    break;
  }
  case Instruction::SIToFP:
  case Instruction::UIToFP: {
    // Cannot produce nan
    Known.knownNot(fcNan);

    // Integers cannot be subnormal
    Known.knownNot(fcSubnormal);

    // sitofp and uitofp turn into +0.0 for zero.
    Known.knownNot(fcNegZero);
    if (Op->getOpcode() == Instruction::UIToFP)
      Known.signBitMustBeZero();

    if (InterestedClasses & fcInf) {
      // Get width of largest magnitude integer (remove a bit if signed).
      // This still works for a signed minimum value because the largest FP
      // value is scaled by some fraction close to 2.0 (1.0 + 0.xxxx).
      int IntSize = Op->getOperand(0)->getType()->getScalarSizeInBits();
      if (Op->getOpcode() == Instruction::SIToFP)
        --IntSize;

      // If the exponent of the largest finite FP value can hold the largest
      // integer, the result of the cast must be finite.
      Type *FPTy = Op->getType()->getScalarType();
      if (ilogb(APFloat::getLargest(FPTy->getFltSemantics())) >= IntSize)
        Known.knownNot(fcInf);
    }

    break;
  }
  case Instruction::ExtractElement: {
    // Look through extract element. If the index is non-constant or
    // out-of-range demand all elements, otherwise just the extracted element.
    const Value *Vec = Op->getOperand(0);
    const Value *Idx = Op->getOperand(1);
    auto *CIdx = dyn_cast<ConstantInt>(Idx);

    if (auto *VecTy = dyn_cast<FixedVectorType>(Vec->getType())) {
      unsigned NumElts = VecTy->getNumElements();
      APInt DemandedVecElts = APInt::getAllOnes(NumElts);
      if (CIdx && CIdx->getValue().ult(NumElts))
        DemandedVecElts = APInt::getOneBitSet(NumElts, CIdx->getZExtValue());
      return computeKnownFPClass(Vec, DemandedVecElts, InterestedClasses, Known,
                                 Depth + 1, Q);
    }

    break;
  }
  case Instruction::InsertElement: {
    if (isa<ScalableVectorType>(Op->getType()))
      return;

    const Value *Vec = Op->getOperand(0);
    const Value *Elt = Op->getOperand(1);
    auto *CIdx = dyn_cast<ConstantInt>(Op->getOperand(2));
    unsigned NumElts = DemandedElts.getBitWidth();
    APInt DemandedVecElts = DemandedElts;
    bool NeedsElt = true;
    // If we know the index we are inserting to, clear it from Vec check.
    if (CIdx && CIdx->getValue().ult(NumElts)) {
      DemandedVecElts.clearBit(CIdx->getZExtValue());
      NeedsElt = DemandedElts[CIdx->getZExtValue()];
    }

    // Do we demand the inserted element?
    if (NeedsElt) {
      computeKnownFPClass(Elt, Known, InterestedClasses, Depth + 1, Q);
      // If we don't know any bits, early out.
      if (Known.isUnknown())
        break;
    } else {
      Known.KnownFPClasses = fcNone;
    }

    // Do we need anymore elements from Vec?
    if (!DemandedVecElts.isZero()) {
      KnownFPClass Known2;
      computeKnownFPClass(Vec, DemandedVecElts, InterestedClasses, Known2,
                          Depth + 1, Q);
      Known |= Known2;
    }

    break;
  }
  case Instruction::ShuffleVector: {
    // For undef elements, we don't know anything about the common state of
    // the shuffle result.
    APInt DemandedLHS, DemandedRHS;
    auto *Shuf = dyn_cast<ShuffleVectorInst>(Op);
    if (!Shuf || !getShuffleDemandedElts(Shuf, DemandedElts, DemandedLHS, DemandedRHS))
      return;

    if (!!DemandedLHS) {
      const Value *LHS = Shuf->getOperand(0);
      computeKnownFPClass(LHS, DemandedLHS, InterestedClasses, Known,
                          Depth + 1, Q);

      // If we don't know any bits, early out.
      if (Known.isUnknown())
        break;
    } else {
      Known.KnownFPClasses = fcNone;
    }

    if (!!DemandedRHS) {
      KnownFPClass Known2;
      const Value *RHS = Shuf->getOperand(1);
      computeKnownFPClass(RHS, DemandedRHS, InterestedClasses, Known2,
                          Depth + 1, Q);
      Known |= Known2;
    }

    break;
  }
  case Instruction::ExtractValue: {
    const ExtractValueInst *Extract = cast<ExtractValueInst>(Op);
    ArrayRef<unsigned> Indices = Extract->getIndices();
    const Value *Src = Extract->getAggregateOperand();
    if (isa<StructType>(Src->getType()) && Indices.size() == 1 &&
        Indices[0] == 0) {
      if (const auto *II = dyn_cast<IntrinsicInst>(Src)) {
        switch (II->getIntrinsicID()) {
        case Intrinsic::frexp: {
          Known.knownNot(fcSubnormal);

          KnownFPClass KnownSrc;
          computeKnownFPClass(II->getArgOperand(0), DemandedElts,
                              InterestedClasses, KnownSrc, Depth + 1, Q);

          const Function *F = cast<Instruction>(Op)->getFunction();

          if (KnownSrc.isKnownNever(fcNegative))
            Known.knownNot(fcNegative);
          else {
            if (F && KnownSrc.isKnownNeverLogicalNegZero(*F, Op->getType()))
              Known.knownNot(fcNegZero);
            if (KnownSrc.isKnownNever(fcNegInf))
              Known.knownNot(fcNegInf);
          }

          if (KnownSrc.isKnownNever(fcPositive))
            Known.knownNot(fcPositive);
          else {
            if (F && KnownSrc.isKnownNeverLogicalPosZero(*F, Op->getType()))
              Known.knownNot(fcPosZero);
            if (KnownSrc.isKnownNever(fcPosInf))
              Known.knownNot(fcPosInf);
          }

          Known.propagateNaN(KnownSrc);
          return;
        }
        default:
          break;
        }
      }
    }

    computeKnownFPClass(Src, DemandedElts, InterestedClasses, Known, Depth + 1,
                        Q);
    break;
  }
  case Instruction::PHI: {
    const PHINode *P = cast<PHINode>(Op);
    // Unreachable blocks may have zero-operand PHI nodes.
    if (P->getNumIncomingValues() == 0)
      break;

    // Otherwise take the unions of the known bit sets of the operands,
    // taking conservative care to avoid excessive recursion.
    const unsigned PhiRecursionLimit = MaxAnalysisRecursionDepth - 2;

    if (Depth < PhiRecursionLimit) {
      // Skip if every incoming value references to ourself.
      if (isa_and_nonnull<UndefValue>(P->hasConstantValue()))
        break;

      bool First = true;

      for (const Use &U : P->operands()) {
        Value *IncValue = U.get();
        // Skip direct self references.
        if (IncValue == P)
          continue;

        KnownFPClass KnownSrc;
        // Recurse, but cap the recursion to two levels, because we don't want
        // to waste time spinning around in loops. We need at least depth 2 to
        // detect known sign bits.
        computeKnownFPClass(IncValue, DemandedElts, InterestedClasses, KnownSrc,
                            PhiRecursionLimit,
                            Q.getWithoutCondContext().getWithInstruction(
                                P->getIncomingBlock(U)->getTerminator()));

        if (First) {
          Known = KnownSrc;
          First = false;
        } else {
          Known |= KnownSrc;
        }

        if (Known.KnownFPClasses == fcAllFlags)
          break;
      }
    }

    break;
  }
  default:
    break;
  }
}

KnownFPClass llvm::computeKnownFPClass(const Value *V,
                                       const APInt &DemandedElts,
                                       FPClassTest InterestedClasses,
                                       unsigned Depth,
                                       const SimplifyQuery &SQ) {
  KnownFPClass KnownClasses;
  ::computeKnownFPClass(V, DemandedElts, InterestedClasses, KnownClasses, Depth,
                        SQ);
  return KnownClasses;
}

KnownFPClass llvm::computeKnownFPClass(const Value *V,
                                       FPClassTest InterestedClasses,
                                       unsigned Depth,
                                       const SimplifyQuery &SQ) {
  KnownFPClass Known;
  ::computeKnownFPClass(V, Known, InterestedClasses, Depth, SQ);
  return Known;
}

Value *llvm::isBytewiseValue(Value *V, const DataLayout &DL) {

  // All byte-wide stores are splatable, even of arbitrary variables.
  if (V->getType()->isIntegerTy(8))
    return V;

  LLVMContext &Ctx = V->getContext();

  // Undef don't care.
  auto *UndefInt8 = UndefValue::get(Type::getInt8Ty(Ctx));
  if (isa<UndefValue>(V))
    return UndefInt8;

  // Return Undef for zero-sized type.
  if (DL.getTypeStoreSize(V->getType()).isZero())
    return UndefInt8;

  Constant *C = dyn_cast<Constant>(V);
  if (!C) {
    // Conceptually, we could handle things like:
    //   %a = zext i8 %X to i16
    //   %b = shl i16 %a, 8
    //   %c = or i16 %a, %b
    // but until there is an example that actually needs this, it doesn't seem
    // worth worrying about.
    return nullptr;
  }

  // Handle 'null' ConstantArrayZero etc.
  if (C->isNullValue())
    return Constant::getNullValue(Type::getInt8Ty(Ctx));

  // Constant floating-point values can be handled as integer values if the
  // corresponding integer value is "byteable".  An important case is 0.0.
  if (ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
    Type *Ty = nullptr;
    if (CFP->getType()->isHalfTy())
      Ty = Type::getInt16Ty(Ctx);
    else if (CFP->getType()->isFloatTy())
      Ty = Type::getInt32Ty(Ctx);
    else if (CFP->getType()->isDoubleTy())
      Ty = Type::getInt64Ty(Ctx);
    // Don't handle long double formats, which have strange constraints.
    return Ty ? isBytewiseValue(ConstantExpr::getBitCast(CFP, Ty), DL)
              : nullptr;
  }

  // We can handle constant integers that are multiple of 8 bits.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(C)) {
    if (CI->getBitWidth() % 8 == 0) {
      assert(CI->getBitWidth() > 8 && "8 bits should be handled above!");
      if (!CI->getValue().isSplat(8))
        return nullptr;
      return ConstantInt::get(Ctx, CI->getValue().trunc(8));
    }
  }

  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    if (CE->getOpcode() == Instruction::IntToPtr) {
      if (auto *PtrTy = dyn_cast<PointerType>(CE->getType())) {
        unsigned BitWidth = DL.getPointerSizeInBits(PtrTy->getAddressSpace());
        if (Constant *Op = ConstantFoldIntegerCast(
                CE->getOperand(0), Type::getIntNTy(Ctx, BitWidth), false, DL))
          return isBytewiseValue(Op, DL);
      }
    }
  }

  auto Merge = [&](Value *LHS, Value *RHS) -> Value * {
    if (LHS == RHS)
      return LHS;
    if (!LHS || !RHS)
      return nullptr;
    if (LHS == UndefInt8)
      return RHS;
    if (RHS == UndefInt8)
      return LHS;
    return nullptr;
  };

  if (ConstantDataSequential *CA = dyn_cast<ConstantDataSequential>(C)) {
    Value *Val = UndefInt8;
    for (unsigned I = 0, E = CA->getNumElements(); I != E; ++I)
      if (!(Val = Merge(Val, isBytewiseValue(CA->getElementAsConstant(I), DL))))
        return nullptr;
    return Val;
  }

  if (isa<ConstantAggregate>(C)) {
    Value *Val = UndefInt8;
    for (unsigned I = 0, E = C->getNumOperands(); I != E; ++I)
      if (!(Val = Merge(Val, isBytewiseValue(C->getOperand(I), DL))))
        return nullptr;
    return Val;
  }

  // Don't try to handle the handful of other constants.
  return nullptr;
}

// This is the recursive version of BuildSubAggregate. It takes a few different
// arguments. Idxs is the index within the nested struct From that we are
// looking at now (which is of type IndexedType). IdxSkip is the number of
// indices from Idxs that should be left out when inserting into the resulting
// struct. To is the result struct built so far, new insertvalue instructions
// build on that.
static Value *BuildSubAggregate(Value *From, Value *To, Type *IndexedType,
                                SmallVectorImpl<unsigned> &Idxs,
                                unsigned IdxSkip,
                                BasicBlock::iterator InsertBefore) {
  StructType *STy = dyn_cast<StructType>(IndexedType);
  if (STy) {
    // Save the original To argument so we can modify it
    Value *OrigTo = To;
    // General case, the type indexed by Idxs is a struct
    for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
      // Process each struct element recursively
      Idxs.push_back(i);
      Value *PrevTo = To;
      To = BuildSubAggregate(From, To, STy->getElementType(i), Idxs, IdxSkip,
                             InsertBefore);
      Idxs.pop_back();
      if (!To) {
        // Couldn't find any inserted value for this index? Cleanup
        while (PrevTo != OrigTo) {
          InsertValueInst* Del = cast<InsertValueInst>(PrevTo);
          PrevTo = Del->getAggregateOperand();
          Del->eraseFromParent();
        }
        // Stop processing elements
        break;
      }
    }
    // If we successfully found a value for each of our subaggregates
    if (To)
      return To;
  }
  // Base case, the type indexed by SourceIdxs is not a struct, or not all of
  // the struct's elements had a value that was inserted directly. In the latter
  // case, perhaps we can't determine each of the subelements individually, but
  // we might be able to find the complete struct somewhere.

  // Find the value that is at that particular spot
  Value *V = FindInsertedValue(From, Idxs);

  if (!V)
    return nullptr;

  // Insert the value in the new (sub) aggregate
  return InsertValueInst::Create(To, V, ArrayRef(Idxs).slice(IdxSkip), "tmp",
                                 InsertBefore);
}

// This helper takes a nested struct and extracts a part of it (which is again a
// struct) into a new value. For example, given the struct:
// { a, { b, { c, d }, e } }
// and the indices "1, 1" this returns
// { c, d }.
//
// It does this by inserting an insertvalue for each element in the resulting
// struct, as opposed to just inserting a single struct. This will only work if
// each of the elements of the substruct are known (ie, inserted into From by an
// insertvalue instruction somewhere).
//
// All inserted insertvalue instructions are inserted before InsertBefore
static Value *BuildSubAggregate(Value *From, ArrayRef<unsigned> idx_range,
                                BasicBlock::iterator InsertBefore) {
  Type *IndexedType = ExtractValueInst::getIndexedType(From->getType(),
                                                             idx_range);
  Value *To = PoisonValue::get(IndexedType);
  SmallVector<unsigned, 10> Idxs(idx_range.begin(), idx_range.end());
  unsigned IdxSkip = Idxs.size();

  return BuildSubAggregate(From, To, IndexedType, Idxs, IdxSkip, InsertBefore);
}

/// Given an aggregate and a sequence of indices, see if the scalar value
/// indexed is already around as a register, for example if it was inserted
/// directly into the aggregate.
///
/// If InsertBefore is not null, this function will duplicate (modified)
/// insertvalues when a part of a nested struct is extracted.
Value *
llvm::FindInsertedValue(Value *V, ArrayRef<unsigned> idx_range,
                        std::optional<BasicBlock::iterator> InsertBefore) {
  // Nothing to index? Just return V then (this is useful at the end of our
  // recursion).
  if (idx_range.empty())
    return V;
  // We have indices, so V should have an indexable type.
  assert((V->getType()->isStructTy() || V->getType()->isArrayTy()) &&
         "Not looking at a struct or array?");
  assert(ExtractValueInst::getIndexedType(V->getType(), idx_range) &&
         "Invalid indices for type?");

  if (Constant *C = dyn_cast<Constant>(V)) {
    C = C->getAggregateElement(idx_range[0]);
    if (!C) return nullptr;
    return FindInsertedValue(C, idx_range.slice(1), InsertBefore);
  }

  if (InsertValueInst *I = dyn_cast<InsertValueInst>(V)) {
    // Loop the indices for the insertvalue instruction in parallel with the
    // requested indices
    const unsigned *req_idx = idx_range.begin();
    for (const unsigned *i = I->idx_begin(), *e = I->idx_end();
         i != e; ++i, ++req_idx) {
      if (req_idx == idx_range.end()) {
        // We can't handle this without inserting insertvalues
        if (!InsertBefore)
          return nullptr;

        // The requested index identifies a part of a nested aggregate. Handle
        // this specially. For example,
        // %A = insertvalue { i32, {i32, i32 } } undef, i32 10, 1, 0
        // %B = insertvalue { i32, {i32, i32 } } %A, i32 11, 1, 1
        // %C = extractvalue {i32, { i32, i32 } } %B, 1
        // This can be changed into
        // %A = insertvalue {i32, i32 } undef, i32 10, 0
        // %C = insertvalue {i32, i32 } %A, i32 11, 1
        // which allows the unused 0,0 element from the nested struct to be
        // removed.
        return BuildSubAggregate(V, ArrayRef(idx_range.begin(), req_idx),
                                 *InsertBefore);
      }

      // This insert value inserts something else than what we are looking for.
      // See if the (aggregate) value inserted into has the value we are
      // looking for, then.
      if (*req_idx != *i)
        return FindInsertedValue(I->getAggregateOperand(), idx_range,
                                 InsertBefore);
    }
    // If we end up here, the indices of the insertvalue match with those
    // requested (though possibly only partially). Now we recursively look at
    // the inserted value, passing any remaining indices.
    return FindInsertedValue(I->getInsertedValueOperand(),
                             ArrayRef(req_idx, idx_range.end()), InsertBefore);
  }

  if (ExtractValueInst *I = dyn_cast<ExtractValueInst>(V)) {
    // If we're extracting a value from an aggregate that was extracted from
    // something else, we can extract from that something else directly instead.
    // However, we will need to chain I's indices with the requested indices.

    // Calculate the number of indices required
    unsigned size = I->getNumIndices() + idx_range.size();
    // Allocate some space to put the new indices in
    SmallVector<unsigned, 5> Idxs;
    Idxs.reserve(size);
    // Add indices from the extract value instruction
    Idxs.append(I->idx_begin(), I->idx_end());

    // Add requested indices
    Idxs.append(idx_range.begin(), idx_range.end());

    assert(Idxs.size() == size
           && "Number of indices added not correct?");

    return FindInsertedValue(I->getAggregateOperand(), Idxs, InsertBefore);
  }
  // Otherwise, we don't know (such as, extracting from a function return value
  // or load instruction)
  return nullptr;
}

bool llvm::isGEPBasedOnPointerToString(const GEPOperator *GEP,
                                       unsigned CharSize) {
  // Make sure the GEP has exactly three arguments.
  if (GEP->getNumOperands() != 3)
    return false;

  // Make sure the index-ee is a pointer to array of \p CharSize integers.
  // CharSize.
  ArrayType *AT = dyn_cast<ArrayType>(GEP->getSourceElementType());
  if (!AT || !AT->getElementType()->isIntegerTy(CharSize))
    return false;

  // Check to make sure that the first operand of the GEP is an integer and
  // has value 0 so that we are sure we're indexing into the initializer.
  const ConstantInt *FirstIdx = dyn_cast<ConstantInt>(GEP->getOperand(1));
  if (!FirstIdx || !FirstIdx->isZero())
    return false;

  return true;
}

// If V refers to an initialized global constant, set Slice either to
// its initializer if the size of its elements equals ElementSize, or,
// for ElementSize == 8, to its representation as an array of unsiged
// char. Return true on success.
// Offset is in the unit "nr of ElementSize sized elements".
bool llvm::getConstantDataArrayInfo(const Value *V,
                                    ConstantDataArraySlice &Slice,
                                    unsigned ElementSize, uint64_t Offset) {
  assert(V && "V should not be null.");
  assert((ElementSize % 8) == 0 &&
         "ElementSize expected to be a multiple of the size of a byte.");
  unsigned ElementSizeInBytes = ElementSize / 8;

  // Drill down into the pointer expression V, ignoring any intervening
  // casts, and determine the identity of the object it references along
  // with the cumulative byte offset into it.
  const GlobalVariable *GV =
    dyn_cast<GlobalVariable>(getUnderlyingObject(V));
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
    // Fail if V is not based on constant global object.
    return false;

  const DataLayout &DL = GV->getDataLayout();
  APInt Off(DL.getIndexTypeSizeInBits(V->getType()), 0);

  if (GV != V->stripAndAccumulateConstantOffsets(DL, Off,
                                                 /*AllowNonInbounds*/ true))
    // Fail if a constant offset could not be determined.
    return false;

  uint64_t StartIdx = Off.getLimitedValue();
  if (StartIdx == UINT64_MAX)
    // Fail if the constant offset is excessive.
    return false;

  // Off/StartIdx is in the unit of bytes. So we need to convert to number of
  // elements. Simply bail out if that isn't possible.
  if ((StartIdx % ElementSizeInBytes) != 0)
    return false;

  Offset += StartIdx / ElementSizeInBytes;
  ConstantDataArray *Array = nullptr;
  ArrayType *ArrayTy = nullptr;

  if (GV->getInitializer()->isNullValue()) {
    Type *GVTy = GV->getValueType();
    uint64_t SizeInBytes = DL.getTypeStoreSize(GVTy).getFixedValue();
    uint64_t Length = SizeInBytes / ElementSizeInBytes;

    Slice.Array = nullptr;
    Slice.Offset = 0;
    // Return an empty Slice for undersized constants to let callers
    // transform even undefined library calls into simpler, well-defined
    // expressions.  This is preferable to making the calls although it
    // prevents sanitizers from detecting such calls.
    Slice.Length = Length < Offset ? 0 : Length - Offset;
    return true;
  }

  auto *Init = const_cast<Constant *>(GV->getInitializer());
  if (auto *ArrayInit = dyn_cast<ConstantDataArray>(Init)) {
    Type *InitElTy = ArrayInit->getElementType();
    if (InitElTy->isIntegerTy(ElementSize)) {
      // If Init is an initializer for an array of the expected type
      // and size, use it as is.
      Array = ArrayInit;
      ArrayTy = ArrayInit->getType();
    }
  }

  if (!Array) {
    if (ElementSize != 8)
      // TODO: Handle conversions to larger integral types.
      return false;

    // Otherwise extract the portion of the initializer starting
    // at Offset as an array of bytes, and reset Offset.
    Init = ReadByteArrayFromGlobal(GV, Offset);
    if (!Init)
      return false;

    Offset = 0;
    Array = dyn_cast<ConstantDataArray>(Init);
    ArrayTy = dyn_cast<ArrayType>(Init->getType());
  }

  uint64_t NumElts = ArrayTy->getArrayNumElements();
  if (Offset > NumElts)
    return false;

  Slice.Array = Array;
  Slice.Offset = Offset;
  Slice.Length = NumElts - Offset;
  return true;
}

/// Extract bytes from the initializer of the constant array V, which need
/// not be a nul-terminated string.  On success, store the bytes in Str and
/// return true.  When TrimAtNul is set, Str will contain only the bytes up
/// to but not including the first nul.  Return false on failure.
bool llvm::getConstantStringInfo(const Value *V, StringRef &Str,
                                 bool TrimAtNul) {
  ConstantDataArraySlice Slice;
  if (!getConstantDataArrayInfo(V, Slice, 8))
    return false;

  if (Slice.Array == nullptr) {
    if (TrimAtNul) {
      // Return a nul-terminated string even for an empty Slice.  This is
      // safe because all existing SimplifyLibcalls callers require string
      // arguments and the behavior of the functions they fold is undefined
      // otherwise.  Folding the calls this way is preferable to making
      // the undefined library calls, even though it prevents sanitizers
      // from reporting such calls.
      Str = StringRef();
      return true;
    }
    if (Slice.Length == 1) {
      Str = StringRef("", 1);
      return true;
    }
    // We cannot instantiate a StringRef as we do not have an appropriate string
    // of 0s at hand.
    return false;
  }

  // Start out with the entire array in the StringRef.
  Str = Slice.Array->getAsString();
  // Skip over 'offset' bytes.
  Str = Str.substr(Slice.Offset);

  if (TrimAtNul) {
    // Trim off the \0 and anything after it.  If the array is not nul
    // terminated, we just return the whole end of string.  The client may know
    // some other way that the string is length-bound.
    Str = Str.substr(0, Str.find('\0'));
  }
  return true;
}

// These next two are very similar to the above, but also look through PHI
// nodes.
// TODO: See if we can integrate these two together.

/// If we can compute the length of the string pointed to by
/// the specified pointer, return 'len+1'.  If we can't, return 0.
static uint64_t GetStringLengthH(const Value *V,
                                 SmallPtrSetImpl<const PHINode*> &PHIs,
                                 unsigned CharSize) {
  // Look through noop bitcast instructions.
  V = V->stripPointerCasts();

  // If this is a PHI node, there are two cases: either we have already seen it
  // or we haven't.
  if (const PHINode *PN = dyn_cast<PHINode>(V)) {
    if (!PHIs.insert(PN).second)
      return ~0ULL;  // already in the set.

    // If it was new, see if all the input strings are the same length.
    uint64_t LenSoFar = ~0ULL;
    for (Value *IncValue : PN->incoming_values()) {
      uint64_t Len = GetStringLengthH(IncValue, PHIs, CharSize);
      if (Len == 0) return 0; // Unknown length -> unknown.

      if (Len == ~0ULL) continue;

      if (Len != LenSoFar && LenSoFar != ~0ULL)
        return 0;    // Disagree -> unknown.
      LenSoFar = Len;
    }

    // Success, all agree.
    return LenSoFar;
  }

  // strlen(select(c,x,y)) -> strlen(x) ^ strlen(y)
  if (const SelectInst *SI = dyn_cast<SelectInst>(V)) {
    uint64_t Len1 = GetStringLengthH(SI->getTrueValue(), PHIs, CharSize);
    if (Len1 == 0) return 0;
    uint64_t Len2 = GetStringLengthH(SI->getFalseValue(), PHIs, CharSize);
    if (Len2 == 0) return 0;
    if (Len1 == ~0ULL) return Len2;
    if (Len2 == ~0ULL) return Len1;
    if (Len1 != Len2) return 0;
    return Len1;
  }

  // Otherwise, see if we can read the string.
  ConstantDataArraySlice Slice;
  if (!getConstantDataArrayInfo(V, Slice, CharSize))
    return 0;

  if (Slice.Array == nullptr)
    // Zeroinitializer (including an empty one).
    return 1;

  // Search for the first nul character.  Return a conservative result even
  // when there is no nul.  This is safe since otherwise the string function
  // being folded such as strlen is undefined, and can be preferable to
  // making the undefined library call.
  unsigned NullIndex = 0;
  for (unsigned E = Slice.Length; NullIndex < E; ++NullIndex) {
    if (Slice.Array->getElementAsInteger(Slice.Offset + NullIndex) == 0)
      break;
  }

  return NullIndex + 1;
}

/// If we can compute the length of the string pointed to by
/// the specified pointer, return 'len+1'.  If we can't, return 0.
uint64_t llvm::GetStringLength(const Value *V, unsigned CharSize) {
  if (!V->getType()->isPointerTy())
    return 0;

  SmallPtrSet<const PHINode*, 32> PHIs;
  uint64_t Len = GetStringLengthH(V, PHIs, CharSize);
  // If Len is ~0ULL, we had an infinite phi cycle: this is dead code, so return
  // an empty string as a length.
  return Len == ~0ULL ? 1 : Len;
}

const Value *
llvm::getArgumentAliasingToReturnedPointer(const CallBase *Call,
                                           bool MustPreserveNullness) {
  assert(Call &&
         "getArgumentAliasingToReturnedPointer only works on nonnull calls");
  if (const Value *RV = Call->getReturnedArgOperand())
    return RV;
  // This can be used only as a aliasing property.
  if (isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(
          Call, MustPreserveNullness))
    return Call->getArgOperand(0);
  return nullptr;
}

bool llvm::isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(
    const CallBase *Call, bool MustPreserveNullness) {
  switch (Call->getIntrinsicID()) {
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
  case Intrinsic::aarch64_irg:
  case Intrinsic::aarch64_tagp:
  // The amdgcn_make_buffer_rsrc function does not alter the address of the
  // input pointer (and thus preserve null-ness for the purposes of escape
  // analysis, which is where the MustPreserveNullness flag comes in to play).
  // However, it will not necessarily map ptr addrspace(N) null to ptr
  // addrspace(8) null, aka the "null descriptor", which has "all loads return
  // 0, all stores are dropped" semantics. Given the context of this intrinsic
  // list, no one should be relying on such a strict interpretation of
  // MustPreserveNullness (and, at time of writing, they are not), but we
  // document this fact out of an abundance of caution.
  case Intrinsic::amdgcn_make_buffer_rsrc:
    return true;
  case Intrinsic::ptrmask:
    return !MustPreserveNullness;
  case Intrinsic::threadlocal_address:
    // The underlying variable changes with thread ID. The Thread ID may change
    // at coroutine suspend points.
    return !Call->getParent()->getParent()->isPresplitCoroutine();
  default:
    return false;
  }
}

/// \p PN defines a loop-variant pointer to an object.  Check if the
/// previous iteration of the loop was referring to the same object as \p PN.
static bool isSameUnderlyingObjectInLoop(const PHINode *PN,
                                         const LoopInfo *LI) {
  // Find the loop-defined value.
  Loop *L = LI->getLoopFor(PN->getParent());
  if (PN->getNumIncomingValues() != 2)
    return true;

  // Find the value from previous iteration.
  auto *PrevValue = dyn_cast<Instruction>(PN->getIncomingValue(0));
  if (!PrevValue || LI->getLoopFor(PrevValue->getParent()) != L)
    PrevValue = dyn_cast<Instruction>(PN->getIncomingValue(1));
  if (!PrevValue || LI->getLoopFor(PrevValue->getParent()) != L)
    return true;

  // If a new pointer is loaded in the loop, the pointer references a different
  // object in every iteration.  E.g.:
  //    for (i)
  //       int *p = a[i];
  //       ...
  if (auto *Load = dyn_cast<LoadInst>(PrevValue))
    if (!L->isLoopInvariant(Load->getPointerOperand()))
      return false;
  return true;
}

const Value *llvm::getUnderlyingObject(const Value *V, unsigned MaxLookup) {
  if (!V->getType()->isPointerTy())
    return V;
  for (unsigned Count = 0; MaxLookup == 0 || Count < MaxLookup; ++Count) {
    if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast ||
               Operator::getOpcode(V) == Instruction::AddrSpaceCast) {
      Value *NewV = cast<Operator>(V)->getOperand(0);
      if (!NewV->getType()->isPointerTy())
        return V;
      V = NewV;
    } else if (auto *GA = dyn_cast<GlobalAlias>(V)) {
      if (GA->isInterposable())
        return V;
      V = GA->getAliasee();
    } else {
      if (auto *PHI = dyn_cast<PHINode>(V)) {
        // Look through single-arg phi nodes created by LCSSA.
        if (PHI->getNumIncomingValues() == 1) {
          V = PHI->getIncomingValue(0);
          continue;
        }
      } else if (auto *Call = dyn_cast<CallBase>(V)) {
        // CaptureTracking can know about special capturing properties of some
        // intrinsics like launder.invariant.group, that can't be expressed with
        // the attributes, but have properties like returning aliasing pointer.
        // Because some analysis may assume that nocaptured pointer is not
        // returned from some special intrinsic (because function would have to
        // be marked with returns attribute), it is crucial to use this function
        // because it should be in sync with CaptureTracking. Not using it may
        // cause weird miscompilations where 2 aliasing pointers are assumed to
        // noalias.
        if (auto *RP = getArgumentAliasingToReturnedPointer(Call, false)) {
          V = RP;
          continue;
        }
      }

      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  }
  return V;
}

void llvm::getUnderlyingObjects(const Value *V,
                                SmallVectorImpl<const Value *> &Objects,
                                LoopInfo *LI, unsigned MaxLookup) {
  SmallPtrSet<const Value *, 4> Visited;
  SmallVector<const Value *, 4> Worklist;
  Worklist.push_back(V);
  do {
    const Value *P = Worklist.pop_back_val();
    P = getUnderlyingObject(P, MaxLookup);

    if (!Visited.insert(P).second)
      continue;

    if (auto *SI = dyn_cast<SelectInst>(P)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }

    if (auto *PN = dyn_cast<PHINode>(P)) {
      // If this PHI changes the underlying object in every iteration of the
      // loop, don't look through it.  Consider:
      //   int **A;
      //   for (i) {
      //     Prev = Curr;     // Prev = PHI (Prev_0, Curr)
      //     Curr = A[i];
      //     *Prev, *Curr;
      //
      // Prev is tracking Curr one iteration behind so they refer to different
      // underlying objects.
      if (!LI || !LI->isLoopHeader(PN->getParent()) ||
          isSameUnderlyingObjectInLoop(PN, LI))
        append_range(Worklist, PN->incoming_values());
      else
        Objects.push_back(P);
      continue;
    }

    Objects.push_back(P);
  } while (!Worklist.empty());
}

const Value *llvm::getUnderlyingObjectAggressive(const Value *V) {
  const unsigned MaxVisited = 8;

  SmallPtrSet<const Value *, 8> Visited;
  SmallVector<const Value *, 8> Worklist;
  Worklist.push_back(V);
  const Value *Object = nullptr;
  // Used as fallback if we can't find a common underlying object through
  // recursion.
  bool First = true;
  const Value *FirstObject = getUnderlyingObject(V);
  do {
    const Value *P = Worklist.pop_back_val();
    P = First ? FirstObject : getUnderlyingObject(P);
    First = false;

    if (!Visited.insert(P).second)
      continue;

    if (Visited.size() == MaxVisited)
      return FirstObject;

    if (auto *SI = dyn_cast<SelectInst>(P)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }

    if (auto *PN = dyn_cast<PHINode>(P)) {
      append_range(Worklist, PN->incoming_values());
      continue;
    }

    if (!Object)
      Object = P;
    else if (Object != P)
      return FirstObject;
  } while (!Worklist.empty());

  return Object;
}

/// This is the function that does the work of looking through basic
/// ptrtoint+arithmetic+inttoptr sequences.
static const Value *getUnderlyingObjectFromInt(const Value *V) {
  do {
    if (const Operator *U = dyn_cast<Operator>(V)) {
      // If we find a ptrtoint, we can transfer control back to the
      // regular getUnderlyingObjectFromInt.
      if (U->getOpcode() == Instruction::PtrToInt)
        return U->getOperand(0);
      // If we find an add of a constant, a multiplied value, or a phi, it's
      // likely that the other operand will lead us to the base
      // object. We don't have to worry about the case where the
      // object address is somehow being computed by the multiply,
      // because our callers only care when the result is an
      // identifiable object.
      if (U->getOpcode() != Instruction::Add ||
          (!isa<ConstantInt>(U->getOperand(1)) &&
           Operator::getOpcode(U->getOperand(1)) != Instruction::Mul &&
           !isa<PHINode>(U->getOperand(1))))
        return V;
      V = U->getOperand(0);
    } else {
      return V;
    }
    assert(V->getType()->isIntegerTy() && "Unexpected operand type!");
  } while (true);
}

/// This is a wrapper around getUnderlyingObjects and adds support for basic
/// ptrtoint+arithmetic+inttoptr sequences.
/// It returns false if unidentified object is found in getUnderlyingObjects.
bool llvm::getUnderlyingObjectsForCodeGen(const Value *V,
                                          SmallVectorImpl<Value *> &Objects) {
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 4> Working(1, V);
  do {
    V = Working.pop_back_val();

    SmallVector<const Value *, 4> Objs;
    getUnderlyingObjects(V, Objs);

    for (const Value *V : Objs) {
      if (!Visited.insert(V).second)
        continue;
      if (Operator::getOpcode(V) == Instruction::IntToPtr) {
        const Value *O =
          getUnderlyingObjectFromInt(cast<User>(V)->getOperand(0));
        if (O->getType()->isPointerTy()) {
          Working.push_back(O);
          continue;
        }
      }
      // If getUnderlyingObjects fails to find an identifiable object,
      // getUnderlyingObjectsForCodeGen also fails for safety.
      if (!isIdentifiedObject(V)) {
        Objects.clear();
        return false;
      }
      Objects.push_back(const_cast<Value *>(V));
    }
  } while (!Working.empty());
  return true;
}

AllocaInst *llvm::findAllocaForValue(Value *V, bool OffsetZero) {
  AllocaInst *Result = nullptr;
  SmallPtrSet<Value *, 4> Visited;
  SmallVector<Value *, 4> Worklist;

  auto AddWork = [&](Value *V) {
    if (Visited.insert(V).second)
      Worklist.push_back(V);
  };

  AddWork(V);
  do {
    V = Worklist.pop_back_val();
    assert(Visited.count(V));

    if (AllocaInst *AI = dyn_cast<AllocaInst>(V)) {
      if (Result && Result != AI)
        return nullptr;
      Result = AI;
    } else if (CastInst *CI = dyn_cast<CastInst>(V)) {
      AddWork(CI->getOperand(0));
    } else if (PHINode *PN = dyn_cast<PHINode>(V)) {
      for (Value *IncValue : PN->incoming_values())
        AddWork(IncValue);
    } else if (auto *SI = dyn_cast<SelectInst>(V)) {
      AddWork(SI->getTrueValue());
      AddWork(SI->getFalseValue());
    } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(V)) {
      if (OffsetZero && !GEP->hasAllZeroIndices())
        return nullptr;
      AddWork(GEP->getPointerOperand());
    } else if (CallBase *CB = dyn_cast<CallBase>(V)) {
      Value *Returned = CB->getReturnedArgOperand();
      if (Returned)
        AddWork(Returned);
      else
        return nullptr;
    } else {
      return nullptr;
    }
  } while (!Worklist.empty());

  return Result;
}

static bool onlyUsedByLifetimeMarkersOrDroppableInstsHelper(
    const Value *V, bool AllowLifetime, bool AllowDroppable) {
  for (const User *U : V->users()) {
    const IntrinsicInst *II = dyn_cast<IntrinsicInst>(U);
    if (!II)
      return false;

    if (AllowLifetime && II->isLifetimeStartOrEnd())
      continue;

    if (AllowDroppable && II->isDroppable())
      continue;

    return false;
  }
  return true;
}

bool llvm::onlyUsedByLifetimeMarkers(const Value *V) {
  return onlyUsedByLifetimeMarkersOrDroppableInstsHelper(
      V, /* AllowLifetime */ true, /* AllowDroppable */ false);
}
bool llvm::onlyUsedByLifetimeMarkersOrDroppableInsts(const Value *V) {
  return onlyUsedByLifetimeMarkersOrDroppableInstsHelper(
      V, /* AllowLifetime */ true, /* AllowDroppable */ true);
}

bool llvm::mustSuppressSpeculation(const LoadInst &LI) {
  if (!LI.isUnordered())
    return true;
  const Function &F = *LI.getFunction();
  // Speculative load may create a race that did not exist in the source.
  return F.hasFnAttribute(Attribute::SanitizeThread) ||
    // Speculative load may load data from dirty regions.
    F.hasFnAttribute(Attribute::SanitizeAddress) ||
    F.hasFnAttribute(Attribute::SanitizeHWAddress);
}

bool llvm::isSafeToSpeculativelyExecute(const Instruction *Inst,
                                        const Instruction *CtxI,
                                        AssumptionCache *AC,
                                        const DominatorTree *DT,
                                        const TargetLibraryInfo *TLI,
                                        bool UseVariableInfo) {
  return isSafeToSpeculativelyExecuteWithOpcode(Inst->getOpcode(), Inst, CtxI,
                                                AC, DT, TLI, UseVariableInfo);
}

bool llvm::isSafeToSpeculativelyExecuteWithOpcode(
    unsigned Opcode, const Instruction *Inst, const Instruction *CtxI,
    AssumptionCache *AC, const DominatorTree *DT, const TargetLibraryInfo *TLI,
    bool UseVariableInfo) {
#ifndef NDEBUG
  if (Inst->getOpcode() != Opcode) {
    // Check that the operands are actually compatible with the Opcode override.
    auto hasEqualReturnAndLeadingOperandTypes =
        [](const Instruction *Inst, unsigned NumLeadingOperands) {
          if (Inst->getNumOperands() < NumLeadingOperands)
            return false;
          const Type *ExpectedType = Inst->getType();
          for (unsigned ItOp = 0; ItOp < NumLeadingOperands; ++ItOp)
            if (Inst->getOperand(ItOp)->getType() != ExpectedType)
              return false;
          return true;
        };
    assert(!Instruction::isBinaryOp(Opcode) ||
           hasEqualReturnAndLeadingOperandTypes(Inst, 2));
    assert(!Instruction::isUnaryOp(Opcode) ||
           hasEqualReturnAndLeadingOperandTypes(Inst, 1));
  }
#endif

  switch (Opcode) {
  default:
    return true;
  case Instruction::UDiv:
  case Instruction::URem: {
    // x / y is undefined if y == 0.
    const APInt *V;
    if (match(Inst->getOperand(1), m_APInt(V)))
      return *V != 0;
    return false;
  }
  case Instruction::SDiv:
  case Instruction::SRem: {
    // x / y is undefined if y == 0 or x == INT_MIN and y == -1
    const APInt *Numerator, *Denominator;
    if (!match(Inst->getOperand(1), m_APInt(Denominator)))
      return false;
    // We cannot hoist this division if the denominator is 0.
    if (*Denominator == 0)
      return false;
    // It's safe to hoist if the denominator is not 0 or -1.
    if (!Denominator->isAllOnes())
      return true;
    // At this point we know that the denominator is -1.  It is safe to hoist as
    // long we know that the numerator is not INT_MIN.
    if (match(Inst->getOperand(0), m_APInt(Numerator)))
      return !Numerator->isMinSignedValue();
    // The numerator *might* be MinSignedValue.
    return false;
  }
  case Instruction::Load: {
    if (!UseVariableInfo)
      return false;

    const LoadInst *LI = dyn_cast<LoadInst>(Inst);
    if (!LI)
      return false;
    if (mustSuppressSpeculation(*LI))
      return false;
    const DataLayout &DL = LI->getDataLayout();
    return isDereferenceableAndAlignedPointer(LI->getPointerOperand(),
                                              LI->getType(), LI->getAlign(), DL,
                                              CtxI, AC, DT, TLI);
  }
  case Instruction::Call: {
    auto *CI = dyn_cast<const CallInst>(Inst);
    if (!CI)
      return false;
    const Function *Callee = CI->getCalledFunction();

    // The called function could have undefined behavior or side-effects, even
    // if marked readnone nounwind.
    return Callee && Callee->isSpeculatable();
  }
  case Instruction::VAArg:
  case Instruction::Alloca:
  case Instruction::Invoke:
  case Instruction::CallBr:
  case Instruction::PHI:
  case Instruction::Store:
  case Instruction::Ret:
  case Instruction::Br:
  case Instruction::IndirectBr:
  case Instruction::Switch:
  case Instruction::Unreachable:
  case Instruction::Fence:
  case Instruction::AtomicRMW:
  case Instruction::AtomicCmpXchg:
  case Instruction::LandingPad:
  case Instruction::Resume:
  case Instruction::CatchSwitch:
  case Instruction::CatchPad:
  case Instruction::CatchRet:
  case Instruction::CleanupPad:
  case Instruction::CleanupRet:
    return false; // Misc instructions which have effects
  }
}

bool llvm::mayHaveNonDefUseDependency(const Instruction &I) {
  if (I.mayReadOrWriteMemory())
    // Memory dependency possible
    return true;
  if (!isSafeToSpeculativelyExecute(&I))
    // Can't move above a maythrow call or infinite loop.  Or if an
    // inalloca alloca, above a stacksave call.
    return true;
  if (!isGuaranteedToTransferExecutionToSuccessor(&I))
    // 1) Can't reorder two inf-loop calls, even if readonly
    // 2) Also can't reorder an inf-loop call below a instruction which isn't
    //    safe to speculative execute.  (Inverse of above)
    return true;
  return false;
}

/// Convert ConstantRange OverflowResult into ValueTracking OverflowResult.
static OverflowResult mapOverflowResult(ConstantRange::OverflowResult OR) {
  switch (OR) {
    case ConstantRange::OverflowResult::MayOverflow:
      return OverflowResult::MayOverflow;
    case ConstantRange::OverflowResult::AlwaysOverflowsLow:
      return OverflowResult::AlwaysOverflowsLow;
    case ConstantRange::OverflowResult::AlwaysOverflowsHigh:
      return OverflowResult::AlwaysOverflowsHigh;
    case ConstantRange::OverflowResult::NeverOverflows:
      return OverflowResult::NeverOverflows;
  }
  llvm_unreachable("Unknown OverflowResult");
}

/// Combine constant ranges from computeConstantRange() and computeKnownBits().
ConstantRange
llvm::computeConstantRangeIncludingKnownBits(const WithCache<const Value *> &V,
                                             bool ForSigned,
                                             const SimplifyQuery &SQ) {
  ConstantRange CR1 =
      ConstantRange::fromKnownBits(V.getKnownBits(SQ), ForSigned);
  ConstantRange CR2 = computeConstantRange(V, ForSigned, SQ.IIQ.UseInstrInfo);
  ConstantRange::PreferredRangeType RangeType =
      ForSigned ? ConstantRange::Signed : ConstantRange::Unsigned;
  return CR1.intersectWith(CR2, RangeType);
}

OverflowResult llvm::computeOverflowForUnsignedMul(const Value *LHS,
                                                   const Value *RHS,
                                                   const SimplifyQuery &SQ,
                                                   bool IsNSW) {
  KnownBits LHSKnown = computeKnownBits(LHS, /*Depth=*/0, SQ);
  KnownBits RHSKnown = computeKnownBits(RHS, /*Depth=*/0, SQ);

  // mul nsw of two non-negative numbers is also nuw.
  if (IsNSW && LHSKnown.isNonNegative() && RHSKnown.isNonNegative())
    return OverflowResult::NeverOverflows;

  ConstantRange LHSRange = ConstantRange::fromKnownBits(LHSKnown, false);
  ConstantRange RHSRange = ConstantRange::fromKnownBits(RHSKnown, false);
  return mapOverflowResult(LHSRange.unsignedMulMayOverflow(RHSRange));
}

OverflowResult llvm::computeOverflowForSignedMul(const Value *LHS,
                                                 const Value *RHS,
                                                 const SimplifyQuery &SQ) {
  // Multiplying n * m significant bits yields a result of n + m significant
  // bits. If the total number of significant bits does not exceed the
  // result bit width (minus 1), there is no overflow.
  // This means if we have enough leading sign bits in the operands
  // we can guarantee that the result does not overflow.
  // Ref: "Hacker's Delight" by Henry Warren
  unsigned BitWidth = LHS->getType()->getScalarSizeInBits();

  // Note that underestimating the number of sign bits gives a more
  // conservative answer.
  unsigned SignBits =
      ::ComputeNumSignBits(LHS, 0, SQ) + ::ComputeNumSignBits(RHS, 0, SQ);

  // First handle the easy case: if we have enough sign bits there's
  // definitely no overflow.
  if (SignBits > BitWidth + 1)
    return OverflowResult::NeverOverflows;

  // There are two ambiguous cases where there can be no overflow:
  //   SignBits == BitWidth + 1    and
  //   SignBits == BitWidth
  // The second case is difficult to check, therefore we only handle the
  // first case.
  if (SignBits == BitWidth + 1) {
    // It overflows only when both arguments are negative and the true
    // product is exactly the minimum negative number.
    // E.g. mul i16 with 17 sign bits: 0xff00 * 0xff80 = 0x8000
    // For simplicity we just check if at least one side is not negative.
    KnownBits LHSKnown = computeKnownBits(LHS, /*Depth=*/0, SQ);
    KnownBits RHSKnown = computeKnownBits(RHS, /*Depth=*/0, SQ);
    if (LHSKnown.isNonNegative() || RHSKnown.isNonNegative())
      return OverflowResult::NeverOverflows;
  }
  return OverflowResult::MayOverflow;
}

OverflowResult
llvm::computeOverflowForUnsignedAdd(const WithCache<const Value *> &LHS,
                                    const WithCache<const Value *> &RHS,
                                    const SimplifyQuery &SQ) {
  ConstantRange LHSRange =
      computeConstantRangeIncludingKnownBits(LHS, /*ForSigned=*/false, SQ);
  ConstantRange RHSRange =
      computeConstantRangeIncludingKnownBits(RHS, /*ForSigned=*/false, SQ);
  return mapOverflowResult(LHSRange.unsignedAddMayOverflow(RHSRange));
}

static OverflowResult
computeOverflowForSignedAdd(const WithCache<const Value *> &LHS,
                            const WithCache<const Value *> &RHS,
                            const AddOperator *Add, const SimplifyQuery &SQ) {
  if (Add && Add->hasNoSignedWrap()) {
    return OverflowResult::NeverOverflows;
  }

  // If LHS and RHS each have at least two sign bits, the addition will look
  // like
  //
  // XX..... +
  // YY.....
  //
  // If the carry into the most significant position is 0, X and Y can't both
  // be 1 and therefore the carry out of the addition is also 0.
  //
  // If the carry into the most significant position is 1, X and Y can't both
  // be 0 and therefore the carry out of the addition is also 1.
  //
  // Since the carry into the most significant position is always equal to
  // the carry out of the addition, there is no signed overflow.
  if (::ComputeNumSignBits(LHS, 0, SQ) > 1 &&
      ::ComputeNumSignBits(RHS, 0, SQ) > 1)
    return OverflowResult::NeverOverflows;

  ConstantRange LHSRange =
      computeConstantRangeIncludingKnownBits(LHS, /*ForSigned=*/true, SQ);
  ConstantRange RHSRange =
      computeConstantRangeIncludingKnownBits(RHS, /*ForSigned=*/true, SQ);
  OverflowResult OR =
      mapOverflowResult(LHSRange.signedAddMayOverflow(RHSRange));
  if (OR != OverflowResult::MayOverflow)
    return OR;

  // The remaining code needs Add to be available. Early returns if not so.
  if (!Add)
    return OverflowResult::MayOverflow;

  // If the sign of Add is the same as at least one of the operands, this add
  // CANNOT overflow. If this can be determined from the known bits of the
  // operands the above signedAddMayOverflow() check will have already done so.
  // The only other way to improve on the known bits is from an assumption, so
  // call computeKnownBitsFromContext() directly.
  bool LHSOrRHSKnownNonNegative =
      (LHSRange.isAllNonNegative() || RHSRange.isAllNonNegative());
  bool LHSOrRHSKnownNegative =
      (LHSRange.isAllNegative() || RHSRange.isAllNegative());
  if (LHSOrRHSKnownNonNegative || LHSOrRHSKnownNegative) {
    KnownBits AddKnown(LHSRange.getBitWidth());
    computeKnownBitsFromContext(Add, AddKnown, /*Depth=*/0, SQ);
    if ((AddKnown.isNonNegative() && LHSOrRHSKnownNonNegative) ||
        (AddKnown.isNegative() && LHSOrRHSKnownNegative))
      return OverflowResult::NeverOverflows;
  }

  return OverflowResult::MayOverflow;
}

OverflowResult llvm::computeOverflowForUnsignedSub(const Value *LHS,
                                                   const Value *RHS,
                                                   const SimplifyQuery &SQ) {
  // X - (X % ?)
  // The remainder of a value can't have greater magnitude than itself,
  // so the subtraction can't overflow.

  // X - (X -nuw ?)
  // In the minimal case, this would simplify to "?", so there's no subtract
  // at all. But if this analysis is used to peek through casts, for example,
  // then determining no-overflow may allow other transforms.

  // TODO: There are other patterns like this.
  //       See simplifyICmpWithBinOpOnLHS() for candidates.
  if (match(RHS, m_URem(m_Specific(LHS), m_Value())) ||
      match(RHS, m_NUWSub(m_Specific(LHS), m_Value())))
    if (isGuaranteedNotToBeUndef(LHS, SQ.AC, SQ.CxtI, SQ.DT))
      return OverflowResult::NeverOverflows;

  // Checking for conditions implied by dominating conditions may be expensive.
  // Limit it to usub_with_overflow calls for now.
  if (match(SQ.CxtI,
            m_Intrinsic<Intrinsic::usub_with_overflow>(m_Value(), m_Value())))
    if (auto C = isImpliedByDomCondition(CmpInst::ICMP_UGE, LHS, RHS, SQ.CxtI,
                                         SQ.DL)) {
      if (*C)
        return OverflowResult::NeverOverflows;
      return OverflowResult::AlwaysOverflowsLow;
    }
  ConstantRange LHSRange =
      computeConstantRangeIncludingKnownBits(LHS, /*ForSigned=*/false, SQ);
  ConstantRange RHSRange =
      computeConstantRangeIncludingKnownBits(RHS, /*ForSigned=*/false, SQ);
  return mapOverflowResult(LHSRange.unsignedSubMayOverflow(RHSRange));
}

OverflowResult llvm::computeOverflowForSignedSub(const Value *LHS,
                                                 const Value *RHS,
                                                 const SimplifyQuery &SQ) {
  // X - (X % ?)
  // The remainder of a value can't have greater magnitude than itself,
  // so the subtraction can't overflow.

  // X - (X -nsw ?)
  // In the minimal case, this would simplify to "?", so there's no subtract
  // at all. But if this analysis is used to peek through casts, for example,
  // then determining no-overflow may allow other transforms.
  if (match(RHS, m_SRem(m_Specific(LHS), m_Value())) ||
      match(RHS, m_NSWSub(m_Specific(LHS), m_Value())))
    if (isGuaranteedNotToBeUndef(LHS, SQ.AC, SQ.CxtI, SQ.DT))
      return OverflowResult::NeverOverflows;

  // If LHS and RHS each have at least two sign bits, the subtraction
  // cannot overflow.
  if (::ComputeNumSignBits(LHS, 0, SQ) > 1 &&
      ::ComputeNumSignBits(RHS, 0, SQ) > 1)
    return OverflowResult::NeverOverflows;

  ConstantRange LHSRange =
      computeConstantRangeIncludingKnownBits(LHS, /*ForSigned=*/true, SQ);
  ConstantRange RHSRange =
      computeConstantRangeIncludingKnownBits(RHS, /*ForSigned=*/true, SQ);
  return mapOverflowResult(LHSRange.signedSubMayOverflow(RHSRange));
}

bool llvm::isOverflowIntrinsicNoWrap(const WithOverflowInst *WO,
                                     const DominatorTree &DT) {
  SmallVector<const BranchInst *, 2> GuardingBranches;
  SmallVector<const ExtractValueInst *, 2> Results;

  for (const User *U : WO->users()) {
    if (const auto *EVI = dyn_cast<ExtractValueInst>(U)) {
      assert(EVI->getNumIndices() == 1 && "Obvious from CI's type");

      if (EVI->getIndices()[0] == 0)
        Results.push_back(EVI);
      else {
        assert(EVI->getIndices()[0] == 1 && "Obvious from CI's type");

        for (const auto *U : EVI->users())
          if (const auto *B = dyn_cast<BranchInst>(U)) {
            assert(B->isConditional() && "How else is it using an i1?");
            GuardingBranches.push_back(B);
          }
      }
    } else {
      // We are using the aggregate directly in a way we don't want to analyze
      // here (storing it to a global, say).
      return false;
    }
  }

  auto AllUsesGuardedByBranch = [&](const BranchInst *BI) {
    BasicBlockEdge NoWrapEdge(BI->getParent(), BI->getSuccessor(1));
    if (!NoWrapEdge.isSingleEdge())
      return false;

    // Check if all users of the add are provably no-wrap.
    for (const auto *Result : Results) {
      // If the extractvalue itself is not executed on overflow, the we don't
      // need to check each use separately, since domination is transitive.
      if (DT.dominates(NoWrapEdge, Result->getParent()))
        continue;

      for (const auto &RU : Result->uses())
        if (!DT.dominates(NoWrapEdge, RU))
          return false;
    }

    return true;
  };

  return llvm::any_of(GuardingBranches, AllUsesGuardedByBranch);
}

/// Shifts return poison if shiftwidth is larger than the bitwidth.
static bool shiftAmountKnownInRange(const Value *ShiftAmount) {
  auto *C = dyn_cast<Constant>(ShiftAmount);
  if (!C)
    return false;

  // Shifts return poison if shiftwidth is larger than the bitwidth.
  SmallVector<const Constant *, 4> ShiftAmounts;
  if (auto *FVTy = dyn_cast<FixedVectorType>(C->getType())) {
    unsigned NumElts = FVTy->getNumElements();
    for (unsigned i = 0; i < NumElts; ++i)
      ShiftAmounts.push_back(C->getAggregateElement(i));
  } else if (isa<ScalableVectorType>(C->getType()))
    return false; // Can't tell, just return false to be safe
  else
    ShiftAmounts.push_back(C);

  bool Safe = llvm::all_of(ShiftAmounts, [](const Constant *C) {
    auto *CI = dyn_cast_or_null<ConstantInt>(C);
    return CI && CI->getValue().ult(C->getType()->getIntegerBitWidth());
  });

  return Safe;
}

enum class UndefPoisonKind {
  PoisonOnly = (1 << 0),
  UndefOnly = (1 << 1),
  UndefOrPoison = PoisonOnly | UndefOnly,
};

static bool includesPoison(UndefPoisonKind Kind) {
  return (unsigned(Kind) & unsigned(UndefPoisonKind::PoisonOnly)) != 0;
}

static bool includesUndef(UndefPoisonKind Kind) {
  return (unsigned(Kind) & unsigned(UndefPoisonKind::UndefOnly)) != 0;
}

static bool canCreateUndefOrPoison(const Operator *Op, UndefPoisonKind Kind,
                                   bool ConsiderFlagsAndMetadata) {

  if (ConsiderFlagsAndMetadata && includesPoison(Kind) &&
      Op->hasPoisonGeneratingAnnotations())
    return true;

  unsigned Opcode = Op->getOpcode();

  // Check whether opcode is a poison/undef-generating operation
  switch (Opcode) {
  case Instruction::Shl:
  case Instruction::AShr:
  case Instruction::LShr:
    return includesPoison(Kind) && !shiftAmountKnownInRange(Op->getOperand(1));
  case Instruction::FPToSI:
  case Instruction::FPToUI:
    // fptosi/ui yields poison if the resulting value does not fit in the
    // destination type.
    return true;
  case Instruction::Call:
    if (auto *II = dyn_cast<IntrinsicInst>(Op)) {
      switch (II->getIntrinsicID()) {
      // TODO: Add more intrinsics.
      case Intrinsic::ctlz:
      case Intrinsic::cttz:
      case Intrinsic::abs:
        if (cast<ConstantInt>(II->getArgOperand(1))->isNullValue())
          return false;
        break;
      case Intrinsic::ctpop:
      case Intrinsic::bswap:
      case Intrinsic::bitreverse:
      case Intrinsic::fshl:
      case Intrinsic::fshr:
      case Intrinsic::smax:
      case Intrinsic::smin:
      case Intrinsic::umax:
      case Intrinsic::umin:
      case Intrinsic::ptrmask:
      case Intrinsic::fptoui_sat:
      case Intrinsic::fptosi_sat:
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::smul_with_overflow:
      case Intrinsic::uadd_with_overflow:
      case Intrinsic::usub_with_overflow:
      case Intrinsic::umul_with_overflow:
      case Intrinsic::sadd_sat:
      case Intrinsic::uadd_sat:
      case Intrinsic::ssub_sat:
      case Intrinsic::usub_sat:
        return false;
      case Intrinsic::sshl_sat:
      case Intrinsic::ushl_sat:
        return includesPoison(Kind) &&
               !shiftAmountKnownInRange(II->getArgOperand(1));
      case Intrinsic::fma:
      case Intrinsic::fmuladd:
      case Intrinsic::sqrt:
      case Intrinsic::powi:
      case Intrinsic::sin:
      case Intrinsic::cos:
      case Intrinsic::pow:
      case Intrinsic::log:
      case Intrinsic::log10:
      case Intrinsic::log2:
      case Intrinsic::exp:
      case Intrinsic::exp2:
      case Intrinsic::exp10:
      case Intrinsic::fabs:
      case Intrinsic::copysign:
      case Intrinsic::floor:
      case Intrinsic::ceil:
      case Intrinsic::trunc:
      case Intrinsic::rint:
      case Intrinsic::nearbyint:
      case Intrinsic::round:
      case Intrinsic::roundeven:
      case Intrinsic::fptrunc_round:
      case Intrinsic::canonicalize:
      case Intrinsic::arithmetic_fence:
      case Intrinsic::minnum:
      case Intrinsic::maxnum:
      case Intrinsic::minimum:
      case Intrinsic::maximum:
      case Intrinsic::is_fpclass:
      case Intrinsic::ldexp:
      case Intrinsic::frexp:
        return false;
      case Intrinsic::lround:
      case Intrinsic::llround:
      case Intrinsic::lrint:
      case Intrinsic::llrint:
        // If the value doesn't fit an unspecified value is returned (but this
        // is not poison).
        return false;
      }
    }
    [[fallthrough]];
  case Instruction::CallBr:
  case Instruction::Invoke: {
    const auto *CB = cast<CallBase>(Op);
    return !CB->hasRetAttr(Attribute::NoUndef);
  }
  case Instruction::InsertElement:
  case Instruction::ExtractElement: {
    // If index exceeds the length of the vector, it returns poison
    auto *VTy = cast<VectorType>(Op->getOperand(0)->getType());
    unsigned IdxOp = Op->getOpcode() == Instruction::InsertElement ? 2 : 1;
    auto *Idx = dyn_cast<ConstantInt>(Op->getOperand(IdxOp));
    if (includesPoison(Kind))
      return !Idx ||
             Idx->getValue().uge(VTy->getElementCount().getKnownMinValue());
    return false;
  }
  case Instruction::ShuffleVector: {
    ArrayRef<int> Mask = isa<ConstantExpr>(Op)
                             ? cast<ConstantExpr>(Op)->getShuffleMask()
                             : cast<ShuffleVectorInst>(Op)->getShuffleMask();
    return includesPoison(Kind) && is_contained(Mask, PoisonMaskElem);
  }
  case Instruction::FNeg:
  case Instruction::PHI:
  case Instruction::Select:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::ExtractValue:
  case Instruction::InsertValue:
  case Instruction::Freeze:
  case Instruction::ICmp:
  case Instruction::FCmp:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
    return false;
  case Instruction::GetElementPtr:
    // inbounds is handled above
    // TODO: what about inrange on constexpr?
    return false;
  default: {
    const auto *CE = dyn_cast<ConstantExpr>(Op);
    if (isa<CastInst>(Op) || (CE && CE->isCast()))
      return false;
    else if (Instruction::isBinaryOp(Opcode))
      return false;
    // Be conservative and return true.
    return true;
  }
  }
}

bool llvm::canCreateUndefOrPoison(const Operator *Op,
                                  bool ConsiderFlagsAndMetadata) {
  return ::canCreateUndefOrPoison(Op, UndefPoisonKind::UndefOrPoison,
                                  ConsiderFlagsAndMetadata);
}

bool llvm::canCreatePoison(const Operator *Op, bool ConsiderFlagsAndMetadata) {
  return ::canCreateUndefOrPoison(Op, UndefPoisonKind::PoisonOnly,
                                  ConsiderFlagsAndMetadata);
}

static bool directlyImpliesPoison(const Value *ValAssumedPoison, const Value *V,
                                  unsigned Depth) {
  if (ValAssumedPoison == V)
    return true;

  const unsigned MaxDepth = 2;
  if (Depth >= MaxDepth)
    return false;

  if (const auto *I = dyn_cast<Instruction>(V)) {
    if (any_of(I->operands(), [=](const Use &Op) {
          return propagatesPoison(Op) &&
                 directlyImpliesPoison(ValAssumedPoison, Op, Depth + 1);
        }))
      return true;

    // V  = extractvalue V0, idx
    // V2 = extractvalue V0, idx2
    // V0's elements are all poison or not. (e.g., add_with_overflow)
    const WithOverflowInst *II;
    if (match(I, m_ExtractValue(m_WithOverflowInst(II))) &&
        (match(ValAssumedPoison, m_ExtractValue(m_Specific(II))) ||
         llvm::is_contained(II->args(), ValAssumedPoison)))
      return true;
  }
  return false;
}

static bool impliesPoison(const Value *ValAssumedPoison, const Value *V,
                          unsigned Depth) {
  if (isGuaranteedNotToBePoison(ValAssumedPoison))
    return true;

  if (directlyImpliesPoison(ValAssumedPoison, V, /* Depth */ 0))
    return true;

  const unsigned MaxDepth = 2;
  if (Depth >= MaxDepth)
    return false;

  const auto *I = dyn_cast<Instruction>(ValAssumedPoison);
  if (I && !canCreatePoison(cast<Operator>(I))) {
    return all_of(I->operands(), [=](const Value *Op) {
      return impliesPoison(Op, V, Depth + 1);
    });
  }
  return false;
}

bool llvm::impliesPoison(const Value *ValAssumedPoison, const Value *V) {
  return ::impliesPoison(ValAssumedPoison, V, /* Depth */ 0);
}

static bool programUndefinedIfUndefOrPoison(const Value *V, bool PoisonOnly);

static bool isGuaranteedNotToBeUndefOrPoison(
    const Value *V, AssumptionCache *AC, const Instruction *CtxI,
    const DominatorTree *DT, unsigned Depth, UndefPoisonKind Kind) {
  if (Depth >= MaxAnalysisRecursionDepth)
    return false;

  if (isa<MetadataAsValue>(V))
    return false;

  if (const auto *A = dyn_cast<Argument>(V)) {
    if (A->hasAttribute(Attribute::NoUndef) ||
        A->hasAttribute(Attribute::Dereferenceable) ||
        A->hasAttribute(Attribute::DereferenceableOrNull))
      return true;
  }

  if (auto *C = dyn_cast<Constant>(V)) {
    if (isa<PoisonValue>(C))
      return !includesPoison(Kind);

    if (isa<UndefValue>(C))
      return !includesUndef(Kind);

    if (isa<ConstantInt>(C) || isa<GlobalVariable>(C) || isa<ConstantFP>(V) ||
        isa<ConstantPointerNull>(C) || isa<Function>(C))
      return true;

    if (C->getType()->isVectorTy() && !isa<ConstantExpr>(C)) {
      if (includesUndef(Kind) && C->containsUndefElement())
        return false;
      if (includesPoison(Kind) && C->containsPoisonElement())
        return false;
      return !C->containsConstantExpression();
    }
  }

  // Strip cast operations from a pointer value.
  // Note that stripPointerCastsSameRepresentation can strip off getelementptr
  // inbounds with zero offset. To guarantee that the result isn't poison, the
  // stripped pointer is checked as it has to be pointing into an allocated
  // object or be null `null` to ensure `inbounds` getelement pointers with a
  // zero offset could not produce poison.
  // It can strip off addrspacecast that do not change bit representation as
  // well. We believe that such addrspacecast is equivalent to no-op.
  auto *StrippedV = V->stripPointerCastsSameRepresentation();
  if (isa<AllocaInst>(StrippedV) || isa<GlobalVariable>(StrippedV) ||
      isa<Function>(StrippedV) || isa<ConstantPointerNull>(StrippedV))
    return true;

  auto OpCheck = [&](const Value *V) {
    return isGuaranteedNotToBeUndefOrPoison(V, AC, CtxI, DT, Depth + 1, Kind);
  };

  if (auto *Opr = dyn_cast<Operator>(V)) {
    // If the value is a freeze instruction, then it can never
    // be undef or poison.
    if (isa<FreezeInst>(V))
      return true;

    if (const auto *CB = dyn_cast<CallBase>(V)) {
      if (CB->hasRetAttr(Attribute::NoUndef) ||
          CB->hasRetAttr(Attribute::Dereferenceable) ||
          CB->hasRetAttr(Attribute::DereferenceableOrNull))
        return true;
    }

    if (const auto *PN = dyn_cast<PHINode>(V)) {
      unsigned Num = PN->getNumIncomingValues();
      bool IsWellDefined = true;
      for (unsigned i = 0; i < Num; ++i) {
        auto *TI = PN->getIncomingBlock(i)->getTerminator();
        if (!isGuaranteedNotToBeUndefOrPoison(PN->getIncomingValue(i), AC, TI,
                                              DT, Depth + 1, Kind)) {
          IsWellDefined = false;
          break;
        }
      }
      if (IsWellDefined)
        return true;
    } else if (!::canCreateUndefOrPoison(Opr, Kind,
                                         /*ConsiderFlagsAndMetadata*/ true) &&
               all_of(Opr->operands(), OpCheck))
      return true;
  }

  if (auto *I = dyn_cast<LoadInst>(V))
    if (I->hasMetadata(LLVMContext::MD_noundef) ||
        I->hasMetadata(LLVMContext::MD_dereferenceable) ||
        I->hasMetadata(LLVMContext::MD_dereferenceable_or_null))
      return true;

  if (programUndefinedIfUndefOrPoison(V, !includesUndef(Kind)))
    return true;

  // CxtI may be null or a cloned instruction.
  if (!CtxI || !CtxI->getParent() || !DT)
    return false;

  auto *DNode = DT->getNode(CtxI->getParent());
  if (!DNode)
    // Unreachable block
    return false;

  // If V is used as a branch condition before reaching CtxI, V cannot be
  // undef or poison.
  //   br V, BB1, BB2
  // BB1:
  //   CtxI ; V cannot be undef or poison here
  auto *Dominator = DNode->getIDom();
  // This check is purely for compile time reasons: we can skip the IDom walk
  // if what we are checking for includes undef and the value is not an integer.
  if (!includesUndef(Kind) || V->getType()->isIntegerTy())
    while (Dominator) {
      auto *TI = Dominator->getBlock()->getTerminator();

      Value *Cond = nullptr;
      if (auto BI = dyn_cast_or_null<BranchInst>(TI)) {
        if (BI->isConditional())
          Cond = BI->getCondition();
      } else if (auto SI = dyn_cast_or_null<SwitchInst>(TI)) {
        Cond = SI->getCondition();
      }

      if (Cond) {
        if (Cond == V)
          return true;
        else if (!includesUndef(Kind) && isa<Operator>(Cond)) {
          // For poison, we can analyze further
          auto *Opr = cast<Operator>(Cond);
          if (any_of(Opr->operands(), [V](const Use &U) {
                return V == U && propagatesPoison(U);
              }))
            return true;
        }
      }

      Dominator = Dominator->getIDom();
    }

  if (getKnowledgeValidInContext(V, {Attribute::NoUndef}, CtxI, DT, AC))
    return true;

  return false;
}

bool llvm::isGuaranteedNotToBeUndefOrPoison(const Value *V, AssumptionCache *AC,
                                            const Instruction *CtxI,
                                            const DominatorTree *DT,
                                            unsigned Depth) {
  return ::isGuaranteedNotToBeUndefOrPoison(V, AC, CtxI, DT, Depth,
                                            UndefPoisonKind::UndefOrPoison);
}

bool llvm::isGuaranteedNotToBePoison(const Value *V, AssumptionCache *AC,
                                     const Instruction *CtxI,
                                     const DominatorTree *DT, unsigned Depth) {
  return ::isGuaranteedNotToBeUndefOrPoison(V, AC, CtxI, DT, Depth,
                                            UndefPoisonKind::PoisonOnly);
}

bool llvm::isGuaranteedNotToBeUndef(const Value *V, AssumptionCache *AC,
                                    const Instruction *CtxI,
                                    const DominatorTree *DT, unsigned Depth) {
  return ::isGuaranteedNotToBeUndefOrPoison(V, AC, CtxI, DT, Depth,
                                            UndefPoisonKind::UndefOnly);
}

/// Return true if undefined behavior would provably be executed on the path to
/// OnPathTo if Root produced a posion result.  Note that this doesn't say
/// anything about whether OnPathTo is actually executed or whether Root is
/// actually poison.  This can be used to assess whether a new use of Root can
/// be added at a location which is control equivalent with OnPathTo (such as
/// immediately before it) without introducing UB which didn't previously
/// exist.  Note that a false result conveys no information.
bool llvm::mustExecuteUBIfPoisonOnPathTo(Instruction *Root,
                                         Instruction *OnPathTo,
                                         DominatorTree *DT) {
  // Basic approach is to assume Root is poison, propagate poison forward
  // through all users we can easily track, and then check whether any of those
  // users are provable UB and must execute before out exiting block might
  // exit.

  // The set of all recursive users we've visited (which are assumed to all be
  // poison because of said visit)
  SmallSet<const Value *, 16> KnownPoison;
  SmallVector<const Instruction*, 16> Worklist;
  Worklist.push_back(Root);
  while (!Worklist.empty()) {
    const Instruction *I = Worklist.pop_back_val();

    // If we know this must trigger UB on a path leading our target.
    if (mustTriggerUB(I, KnownPoison) && DT->dominates(I, OnPathTo))
      return true;

    // If we can't analyze propagation through this instruction, just skip it
    // and transitive users.  Safe as false is a conservative result.
    if (I != Root && !any_of(I->operands(), [&KnownPoison](const Use &U) {
          return KnownPoison.contains(U) && propagatesPoison(U);
        }))
      continue;

    if (KnownPoison.insert(I).second)
      for (const User *User : I->users())
        Worklist.push_back(cast<Instruction>(User));
  }

  // Might be non-UB, or might have a path we couldn't prove must execute on
  // way to exiting bb.
  return false;
}

OverflowResult llvm::computeOverflowForSignedAdd(const AddOperator *Add,
                                                 const SimplifyQuery &SQ) {
  return ::computeOverflowForSignedAdd(Add->getOperand(0), Add->getOperand(1),
                                       Add, SQ);
}

OverflowResult
llvm::computeOverflowForSignedAdd(const WithCache<const Value *> &LHS,
                                  const WithCache<const Value *> &RHS,
                                  const SimplifyQuery &SQ) {
  return ::computeOverflowForSignedAdd(LHS, RHS, nullptr, SQ);
}

bool llvm::isGuaranteedToTransferExecutionToSuccessor(const Instruction *I) {
  // Note: An atomic operation isn't guaranteed to return in a reasonable amount
  // of time because it's possible for another thread to interfere with it for an
  // arbitrary length of time, but programs aren't allowed to rely on that.

  // If there is no successor, then execution can't transfer to it.
  if (isa<ReturnInst>(I))
    return false;
  if (isa<UnreachableInst>(I))
    return false;

  // Note: Do not add new checks here; instead, change Instruction::mayThrow or
  // Instruction::willReturn.
  //
  // FIXME: Move this check into Instruction::willReturn.
  if (isa<CatchPadInst>(I)) {
    switch (classifyEHPersonality(I->getFunction()->getPersonalityFn())) {
    default:
      // A catchpad may invoke exception object constructors and such, which
      // in some languages can be arbitrary code, so be conservative by default.
      return false;
    case EHPersonality::CoreCLR:
      // For CoreCLR, it just involves a type test.
      return true;
    }
  }

  // An instruction that returns without throwing must transfer control flow
  // to a successor.
  return !I->mayThrow() && I->willReturn();
}

bool llvm::isGuaranteedToTransferExecutionToSuccessor(const BasicBlock *BB) {
  // TODO: This is slightly conservative for invoke instruction since exiting
  // via an exception *is* normal control for them.
  for (const Instruction &I : *BB)
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      return false;
  return true;
}

bool llvm::isGuaranteedToTransferExecutionToSuccessor(
   BasicBlock::const_iterator Begin, BasicBlock::const_iterator End,
   unsigned ScanLimit) {
  return isGuaranteedToTransferExecutionToSuccessor(make_range(Begin, End),
                                                    ScanLimit);
}

bool llvm::isGuaranteedToTransferExecutionToSuccessor(
   iterator_range<BasicBlock::const_iterator> Range, unsigned ScanLimit) {
  assert(ScanLimit && "scan limit must be non-zero");
  for (const Instruction &I : Range) {
    if (isa<DbgInfoIntrinsic>(I))
        continue;
    if (--ScanLimit == 0)
      return false;
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      return false;
  }
  return true;
}

bool llvm::isGuaranteedToExecuteForEveryIteration(const Instruction *I,
                                                  const Loop *L) {
  // The loop header is guaranteed to be executed for every iteration.
  //
  // FIXME: Relax this constraint to cover all basic blocks that are
  // guaranteed to be executed at every iteration.
  if (I->getParent() != L->getHeader()) return false;

  for (const Instruction &LI : *L->getHeader()) {
    if (&LI == I) return true;
    if (!isGuaranteedToTransferExecutionToSuccessor(&LI)) return false;
  }
  llvm_unreachable("Instruction not contained in its own parent basic block.");
}

bool llvm::propagatesPoison(const Use &PoisonOp) {
  const Operator *I = cast<Operator>(PoisonOp.getUser());
  switch (I->getOpcode()) {
  case Instruction::Freeze:
  case Instruction::PHI:
  case Instruction::Invoke:
    return false;
  case Instruction::Select:
    return PoisonOp.getOperandNo() == 0;
  case Instruction::Call:
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      // TODO: Add more intrinsics.
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::smul_with_overflow:
      case Intrinsic::uadd_with_overflow:
      case Intrinsic::usub_with_overflow:
      case Intrinsic::umul_with_overflow:
        // If an input is a vector containing a poison element, the
        // two output vectors (calculated results, overflow bits)'
        // corresponding lanes are poison.
        return true;
      case Intrinsic::ctpop:
      case Intrinsic::ctlz:
      case Intrinsic::cttz:
      case Intrinsic::abs:
      case Intrinsic::smax:
      case Intrinsic::smin:
      case Intrinsic::umax:
      case Intrinsic::umin:
      case Intrinsic::bitreverse:
      case Intrinsic::bswap:
      case Intrinsic::sadd_sat:
      case Intrinsic::ssub_sat:
      case Intrinsic::sshl_sat:
      case Intrinsic::uadd_sat:
      case Intrinsic::usub_sat:
      case Intrinsic::ushl_sat:
        return true;
      }
    }
    return false;
  case Instruction::ICmp:
  case Instruction::FCmp:
  case Instruction::GetElementPtr:
    return true;
  default:
    if (isa<BinaryOperator>(I) || isa<UnaryOperator>(I) || isa<CastInst>(I))
      return true;

    // Be conservative and return false.
    return false;
  }
}

/// Enumerates all operands of \p I that are guaranteed to not be undef or
/// poison. If the callback \p Handle returns true, stop processing and return
/// true. Otherwise, return false.
template <typename CallableT>
static bool handleGuaranteedWellDefinedOps(const Instruction *I,
                                           const CallableT &Handle) {
  switch (I->getOpcode()) {
    case Instruction::Store:
      if (Handle(cast<StoreInst>(I)->getPointerOperand()))
        return true;
      break;

    case Instruction::Load:
      if (Handle(cast<LoadInst>(I)->getPointerOperand()))
        return true;
      break;

    // Since dereferenceable attribute imply noundef, atomic operations
    // also implicitly have noundef pointers too
    case Instruction::AtomicCmpXchg:
      if (Handle(cast<AtomicCmpXchgInst>(I)->getPointerOperand()))
        return true;
      break;

    case Instruction::AtomicRMW:
      if (Handle(cast<AtomicRMWInst>(I)->getPointerOperand()))
        return true;
      break;

    case Instruction::Call:
    case Instruction::Invoke: {
      const CallBase *CB = cast<CallBase>(I);
      if (CB->isIndirectCall() && Handle(CB->getCalledOperand()))
        return true;
      for (unsigned i = 0; i < CB->arg_size(); ++i)
        if ((CB->paramHasAttr(i, Attribute::NoUndef) ||
             CB->paramHasAttr(i, Attribute::Dereferenceable) ||
             CB->paramHasAttr(i, Attribute::DereferenceableOrNull)) &&
            Handle(CB->getArgOperand(i)))
          return true;
      break;
    }
    case Instruction::Ret:
      if (I->getFunction()->hasRetAttribute(Attribute::NoUndef) &&
          Handle(I->getOperand(0)))
        return true;
      break;
    case Instruction::Switch:
      if (Handle(cast<SwitchInst>(I)->getCondition()))
        return true;
      break;
    case Instruction::Br: {
      auto *BR = cast<BranchInst>(I);
      if (BR->isConditional() && Handle(BR->getCondition()))
        return true;
      break;
    }
    default:
      break;
  }

  return false;
}

void llvm::getGuaranteedWellDefinedOps(
    const Instruction *I, SmallVectorImpl<const Value *> &Operands) {
  handleGuaranteedWellDefinedOps(I, [&](const Value *V) {
    Operands.push_back(V);
    return false;
  });
}

/// Enumerates all operands of \p I that are guaranteed to not be poison.
template <typename CallableT>
static bool handleGuaranteedNonPoisonOps(const Instruction *I,
                                         const CallableT &Handle) {
  if (handleGuaranteedWellDefinedOps(I, Handle))
    return true;
  switch (I->getOpcode()) {
  // Divisors of these operations are allowed to be partially undef.
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    return Handle(I->getOperand(1));
  default:
    return false;
  }
}

void llvm::getGuaranteedNonPoisonOps(const Instruction *I,
                                     SmallVectorImpl<const Value *> &Operands) {
  handleGuaranteedNonPoisonOps(I, [&](const Value *V) {
    Operands.push_back(V);
    return false;
  });
}

bool llvm::mustTriggerUB(const Instruction *I,
                         const SmallPtrSetImpl<const Value *> &KnownPoison) {
  return handleGuaranteedNonPoisonOps(
      I, [&](const Value *V) { return KnownPoison.count(V); });
}

static bool programUndefinedIfUndefOrPoison(const Value *V,
                                            bool PoisonOnly) {
  // We currently only look for uses of values within the same basic
  // block, as that makes it easier to guarantee that the uses will be
  // executed given that Inst is executed.
  //
  // FIXME: Expand this to consider uses beyond the same basic block. To do
  // this, look out for the distinction between post-dominance and strong
  // post-dominance.
  const BasicBlock *BB = nullptr;
  BasicBlock::const_iterator Begin;
  if (const auto *Inst = dyn_cast<Instruction>(V)) {
    BB = Inst->getParent();
    Begin = Inst->getIterator();
    Begin++;
  } else if (const auto *Arg = dyn_cast<Argument>(V)) {
    if (Arg->getParent()->isDeclaration())
      return false;
    BB = &Arg->getParent()->getEntryBlock();
    Begin = BB->begin();
  } else {
    return false;
  }

  // Limit number of instructions we look at, to avoid scanning through large
  // blocks. The current limit is chosen arbitrarily.
  unsigned ScanLimit = 32;
  BasicBlock::const_iterator End = BB->end();

  if (!PoisonOnly) {
    // Since undef does not propagate eagerly, be conservative & just check
    // whether a value is directly passed to an instruction that must take
    // well-defined operands.

    for (const auto &I : make_range(Begin, End)) {
      if (isa<DbgInfoIntrinsic>(I))
        continue;
      if (--ScanLimit == 0)
        break;

      if (handleGuaranteedWellDefinedOps(&I, [V](const Value *WellDefinedOp) {
            return WellDefinedOp == V;
          }))
        return true;

      if (!isGuaranteedToTransferExecutionToSuccessor(&I))
        break;
    }
    return false;
  }

  // Set of instructions that we have proved will yield poison if Inst
  // does.
  SmallSet<const Value *, 16> YieldsPoison;
  SmallSet<const BasicBlock *, 4> Visited;

  YieldsPoison.insert(V);
  Visited.insert(BB);

  while (true) {
    for (const auto &I : make_range(Begin, End)) {
      if (isa<DbgInfoIntrinsic>(I))
        continue;
      if (--ScanLimit == 0)
        return false;
      if (mustTriggerUB(&I, YieldsPoison))
        return true;
      if (!isGuaranteedToTransferExecutionToSuccessor(&I))
        return false;

      // If an operand is poison and propagates it, mark I as yielding poison.
      for (const Use &Op : I.operands()) {
        if (YieldsPoison.count(Op) && propagatesPoison(Op)) {
          YieldsPoison.insert(&I);
          break;
        }
      }

      // Special handling for select, which returns poison if its operand 0 is
      // poison (handled in the loop above) *or* if both its true/false operands
      // are poison (handled here).
      if (I.getOpcode() == Instruction::Select &&
          YieldsPoison.count(I.getOperand(1)) &&
          YieldsPoison.count(I.getOperand(2))) {
        YieldsPoison.insert(&I);
      }
    }

    BB = BB->getSingleSuccessor();
    if (!BB || !Visited.insert(BB).second)
      break;

    Begin = BB->getFirstNonPHI()->getIterator();
    End = BB->end();
  }
  return false;
}

bool llvm::programUndefinedIfUndefOrPoison(const Instruction *Inst) {
  return ::programUndefinedIfUndefOrPoison(Inst, false);
}

bool llvm::programUndefinedIfPoison(const Instruction *Inst) {
  return ::programUndefinedIfUndefOrPoison(Inst, true);
}

static bool isKnownNonNaN(const Value *V, FastMathFlags FMF) {
  if (FMF.noNaNs())
    return true;

  if (auto *C = dyn_cast<ConstantFP>(V))
    return !C->isNaN();

  if (auto *C = dyn_cast<ConstantDataVector>(V)) {
    if (!C->getElementType()->isFloatingPointTy())
      return false;
    for (unsigned I = 0, E = C->getNumElements(); I < E; ++I) {
      if (C->getElementAsAPFloat(I).isNaN())
        return false;
    }
    return true;
  }

  if (isa<ConstantAggregateZero>(V))
    return true;

  return false;
}

static bool isKnownNonZero(const Value *V) {
  if (auto *C = dyn_cast<ConstantFP>(V))
    return !C->isZero();

  if (auto *C = dyn_cast<ConstantDataVector>(V)) {
    if (!C->getElementType()->isFloatingPointTy())
      return false;
    for (unsigned I = 0, E = C->getNumElements(); I < E; ++I) {
      if (C->getElementAsAPFloat(I).isZero())
        return false;
    }
    return true;
  }

  return false;
}

/// Match clamp pattern for float types without care about NaNs or signed zeros.
/// Given non-min/max outer cmp/select from the clamp pattern this
/// function recognizes if it can be substitued by a "canonical" min/max
/// pattern.
static SelectPatternResult matchFastFloatClamp(CmpInst::Predicate Pred,
                                               Value *CmpLHS, Value *CmpRHS,
                                               Value *TrueVal, Value *FalseVal,
                                               Value *&LHS, Value *&RHS) {
  // Try to match
  //   X < C1 ? C1 : Min(X, C2) --> Max(C1, Min(X, C2))
  //   X > C1 ? C1 : Max(X, C2) --> Min(C1, Max(X, C2))
  // and return description of the outer Max/Min.

  // First, check if select has inverse order:
  if (CmpRHS == FalseVal) {
    std::swap(TrueVal, FalseVal);
    Pred = CmpInst::getInversePredicate(Pred);
  }

  // Assume success now. If there's no match, callers should not use these anyway.
  LHS = TrueVal;
  RHS = FalseVal;

  const APFloat *FC1;
  if (CmpRHS != TrueVal || !match(CmpRHS, m_APFloat(FC1)) || !FC1->isFinite())
    return {SPF_UNKNOWN, SPNB_NA, false};

  const APFloat *FC2;
  switch (Pred) {
  case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_ULT:
  case CmpInst::FCMP_ULE:
    if (match(FalseVal,
              m_CombineOr(m_OrdFMin(m_Specific(CmpLHS), m_APFloat(FC2)),
                          m_UnordFMin(m_Specific(CmpLHS), m_APFloat(FC2)))) &&
        *FC1 < *FC2)
      return {SPF_FMAXNUM, SPNB_RETURNS_ANY, false};
    break;
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_UGT:
  case CmpInst::FCMP_UGE:
    if (match(FalseVal,
              m_CombineOr(m_OrdFMax(m_Specific(CmpLHS), m_APFloat(FC2)),
                          m_UnordFMax(m_Specific(CmpLHS), m_APFloat(FC2)))) &&
        *FC1 > *FC2)
      return {SPF_FMINNUM, SPNB_RETURNS_ANY, false};
    break;
  default:
    break;
  }

  return {SPF_UNKNOWN, SPNB_NA, false};
}

/// Recognize variations of:
///   CLAMP(v,l,h) ==> ((v) < (l) ? (l) : ((v) > (h) ? (h) : (v)))
static SelectPatternResult matchClamp(CmpInst::Predicate Pred,
                                      Value *CmpLHS, Value *CmpRHS,
                                      Value *TrueVal, Value *FalseVal) {
  // Swap the select operands and predicate to match the patterns below.
  if (CmpRHS != TrueVal) {
    Pred = ICmpInst::getSwappedPredicate(Pred);
    std::swap(TrueVal, FalseVal);
  }
  const APInt *C1;
  if (CmpRHS == TrueVal && match(CmpRHS, m_APInt(C1))) {
    const APInt *C2;
    // (X <s C1) ? C1 : SMIN(X, C2) ==> SMAX(SMIN(X, C2), C1)
    if (match(FalseVal, m_SMin(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->slt(*C2) && Pred == CmpInst::ICMP_SLT)
      return {SPF_SMAX, SPNB_NA, false};

    // (X >s C1) ? C1 : SMAX(X, C2) ==> SMIN(SMAX(X, C2), C1)
    if (match(FalseVal, m_SMax(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->sgt(*C2) && Pred == CmpInst::ICMP_SGT)
      return {SPF_SMIN, SPNB_NA, false};

    // (X <u C1) ? C1 : UMIN(X, C2) ==> UMAX(UMIN(X, C2), C1)
    if (match(FalseVal, m_UMin(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->ult(*C2) && Pred == CmpInst::ICMP_ULT)
      return {SPF_UMAX, SPNB_NA, false};

    // (X >u C1) ? C1 : UMAX(X, C2) ==> UMIN(UMAX(X, C2), C1)
    if (match(FalseVal, m_UMax(m_Specific(CmpLHS), m_APInt(C2))) &&
        C1->ugt(*C2) && Pred == CmpInst::ICMP_UGT)
      return {SPF_UMIN, SPNB_NA, false};
  }
  return {SPF_UNKNOWN, SPNB_NA, false};
}

/// Recognize variations of:
///   a < c ? min(a,b) : min(b,c) ==> min(min(a,b),min(b,c))
static SelectPatternResult matchMinMaxOfMinMax(CmpInst::Predicate Pred,
                                               Value *CmpLHS, Value *CmpRHS,
                                               Value *TVal, Value *FVal,
                                               unsigned Depth) {
  // TODO: Allow FP min/max with nnan/nsz.
  assert(CmpInst::isIntPredicate(Pred) && "Expected integer comparison");

  Value *A = nullptr, *B = nullptr;
  SelectPatternResult L = matchSelectPattern(TVal, A, B, nullptr, Depth + 1);
  if (!SelectPatternResult::isMinOrMax(L.Flavor))
    return {SPF_UNKNOWN, SPNB_NA, false};

  Value *C = nullptr, *D = nullptr;
  SelectPatternResult R = matchSelectPattern(FVal, C, D, nullptr, Depth + 1);
  if (L.Flavor != R.Flavor)
    return {SPF_UNKNOWN, SPNB_NA, false};

  // We have something like: x Pred y ? min(a, b) : min(c, d).
  // Try to match the compare to the min/max operations of the select operands.
  // First, make sure we have the right compare predicate.
  switch (L.Flavor) {
  case SPF_SMIN:
    if (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  case SPF_SMAX:
    if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  case SPF_UMIN:
    if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  case SPF_UMAX:
    if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE) {
      Pred = ICmpInst::getSwappedPredicate(Pred);
      std::swap(CmpLHS, CmpRHS);
    }
    if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE)
      break;
    return {SPF_UNKNOWN, SPNB_NA, false};
  default:
    return {SPF_UNKNOWN, SPNB_NA, false};
  }

  // If there is a common operand in the already matched min/max and the other
  // min/max operands match the compare operands (either directly or inverted),
  // then this is min/max of the same flavor.

  // a pred c ? m(a, b) : m(c, b) --> m(m(a, b), m(c, b))
  // ~c pred ~a ? m(a, b) : m(c, b) --> m(m(a, b), m(c, b))
  if (D == B) {
    if ((CmpLHS == A && CmpRHS == C) || (match(C, m_Not(m_Specific(CmpLHS))) &&
                                         match(A, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }
  // a pred d ? m(a, b) : m(b, d) --> m(m(a, b), m(b, d))
  // ~d pred ~a ? m(a, b) : m(b, d) --> m(m(a, b), m(b, d))
  if (C == B) {
    if ((CmpLHS == A && CmpRHS == D) || (match(D, m_Not(m_Specific(CmpLHS))) &&
                                         match(A, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }
  // b pred c ? m(a, b) : m(c, a) --> m(m(a, b), m(c, a))
  // ~c pred ~b ? m(a, b) : m(c, a) --> m(m(a, b), m(c, a))
  if (D == A) {
    if ((CmpLHS == B && CmpRHS == C) || (match(C, m_Not(m_Specific(CmpLHS))) &&
                                         match(B, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }
  // b pred d ? m(a, b) : m(a, d) --> m(m(a, b), m(a, d))
  // ~d pred ~b ? m(a, b) : m(a, d) --> m(m(a, b), m(a, d))
  if (C == A) {
    if ((CmpLHS == B && CmpRHS == D) || (match(D, m_Not(m_Specific(CmpLHS))) &&
                                         match(B, m_Not(m_Specific(CmpRHS)))))
      return {L.Flavor, SPNB_NA, false};
  }

  return {SPF_UNKNOWN, SPNB_NA, false};
}

/// If the input value is the result of a 'not' op, constant integer, or vector
/// splat of a constant integer, return the bitwise-not source value.
/// TODO: This could be extended to handle non-splat vector integer constants.
static Value *getNotValue(Value *V) {
  Value *NotV;
  if (match(V, m_Not(m_Value(NotV))))
    return NotV;

  const APInt *C;
  if (match(V, m_APInt(C)))
    return ConstantInt::get(V->getType(), ~(*C));

  return nullptr;
}

/// Match non-obvious integer minimum and maximum sequences.
static SelectPatternResult matchMinMax(CmpInst::Predicate Pred,
                                       Value *CmpLHS, Value *CmpRHS,
                                       Value *TrueVal, Value *FalseVal,
                                       Value *&LHS, Value *&RHS,
                                       unsigned Depth) {
  // Assume success. If there's no match, callers should not use these anyway.
  LHS = TrueVal;
  RHS = FalseVal;

  SelectPatternResult SPR = matchClamp(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal);
  if (SPR.Flavor != SelectPatternFlavor::SPF_UNKNOWN)
    return SPR;

  SPR = matchMinMaxOfMinMax(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal, Depth);
  if (SPR.Flavor != SelectPatternFlavor::SPF_UNKNOWN)
    return SPR;

  // Look through 'not' ops to find disguised min/max.
  // (X > Y) ? ~X : ~Y ==> (~X < ~Y) ? ~X : ~Y ==> MIN(~X, ~Y)
  // (X < Y) ? ~X : ~Y ==> (~X > ~Y) ? ~X : ~Y ==> MAX(~X, ~Y)
  if (CmpLHS == getNotValue(TrueVal) && CmpRHS == getNotValue(FalseVal)) {
    switch (Pred) {
    case CmpInst::ICMP_SGT: return {SPF_SMIN, SPNB_NA, false};
    case CmpInst::ICMP_SLT: return {SPF_SMAX, SPNB_NA, false};
    case CmpInst::ICMP_UGT: return {SPF_UMIN, SPNB_NA, false};
    case CmpInst::ICMP_ULT: return {SPF_UMAX, SPNB_NA, false};
    default: break;
    }
  }

  // (X > Y) ? ~Y : ~X ==> (~X < ~Y) ? ~Y : ~X ==> MAX(~Y, ~X)
  // (X < Y) ? ~Y : ~X ==> (~X > ~Y) ? ~Y : ~X ==> MIN(~Y, ~X)
  if (CmpLHS == getNotValue(FalseVal) && CmpRHS == getNotValue(TrueVal)) {
    switch (Pred) {
    case CmpInst::ICMP_SGT: return {SPF_SMAX, SPNB_NA, false};
    case CmpInst::ICMP_SLT: return {SPF_SMIN, SPNB_NA, false};
    case CmpInst::ICMP_UGT: return {SPF_UMAX, SPNB_NA, false};
    case CmpInst::ICMP_ULT: return {SPF_UMIN, SPNB_NA, false};
    default: break;
    }
  }

  if (Pred != CmpInst::ICMP_SGT && Pred != CmpInst::ICMP_SLT)
    return {SPF_UNKNOWN, SPNB_NA, false};

  const APInt *C1;
  if (!match(CmpRHS, m_APInt(C1)))
    return {SPF_UNKNOWN, SPNB_NA, false};

  // An unsigned min/max can be written with a signed compare.
  const APInt *C2;
  if ((CmpLHS == TrueVal && match(FalseVal, m_APInt(C2))) ||
      (CmpLHS == FalseVal && match(TrueVal, m_APInt(C2)))) {
    // Is the sign bit set?
    // (X <s 0) ? X : MAXVAL ==> (X >u MAXVAL) ? X : MAXVAL ==> UMAX
    // (X <s 0) ? MAXVAL : X ==> (X >u MAXVAL) ? MAXVAL : X ==> UMIN
    if (Pred == CmpInst::ICMP_SLT && C1->isZero() && C2->isMaxSignedValue())
      return {CmpLHS == TrueVal ? SPF_UMAX : SPF_UMIN, SPNB_NA, false};

    // Is the sign bit clear?
    // (X >s -1) ? MINVAL : X ==> (X <u MINVAL) ? MINVAL : X ==> UMAX
    // (X >s -1) ? X : MINVAL ==> (X <u MINVAL) ? X : MINVAL ==> UMIN
    if (Pred == CmpInst::ICMP_SGT && C1->isAllOnes() && C2->isMinSignedValue())
      return {CmpLHS == FalseVal ? SPF_UMAX : SPF_UMIN, SPNB_NA, false};
  }

  return {SPF_UNKNOWN, SPNB_NA, false};
}

bool llvm::isKnownNegation(const Value *X, const Value *Y, bool NeedNSW,
                           bool AllowPoison) {
  assert(X && Y && "Invalid operand");

  auto IsNegationOf = [&](const Value *X, const Value *Y) {
    if (!match(X, m_Neg(m_Specific(Y))))
      return false;

    auto *BO = cast<BinaryOperator>(X);
    if (NeedNSW && !BO->hasNoSignedWrap())
      return false;

    auto *Zero = cast<Constant>(BO->getOperand(0));
    if (!AllowPoison && !Zero->isNullValue())
      return false;

    return true;
  };

  // X = -Y or Y = -X
  if (IsNegationOf(X, Y) || IsNegationOf(Y, X))
    return true;

  // X = sub (A, B), Y = sub (B, A) || X = sub nsw (A, B), Y = sub nsw (B, A)
  Value *A, *B;
  return (!NeedNSW && (match(X, m_Sub(m_Value(A), m_Value(B))) &&
                        match(Y, m_Sub(m_Specific(B), m_Specific(A))))) ||
         (NeedNSW && (match(X, m_NSWSub(m_Value(A), m_Value(B))) &&
                       match(Y, m_NSWSub(m_Specific(B), m_Specific(A)))));
}

bool llvm::isKnownInversion(const Value *X, const Value *Y) {
  // Handle X = icmp pred A, B, Y = icmp pred A, C.
  Value *A, *B, *C;
  ICmpInst::Predicate Pred1, Pred2;
  if (!match(X, m_ICmp(Pred1, m_Value(A), m_Value(B))) ||
      !match(Y, m_c_ICmp(Pred2, m_Specific(A), m_Value(C))))
    return false;

  if (B == C)
    return Pred1 == ICmpInst::getInversePredicate(Pred2);

  // Try to infer the relationship from constant ranges.
  const APInt *RHSC1, *RHSC2;
  if (!match(B, m_APInt(RHSC1)) || !match(C, m_APInt(RHSC2)))
    return false;

  const auto CR1 = ConstantRange::makeExactICmpRegion(Pred1, *RHSC1);
  const auto CR2 = ConstantRange::makeExactICmpRegion(Pred2, *RHSC2);

  return CR1.inverse() == CR2;
}

static SelectPatternResult matchSelectPattern(CmpInst::Predicate Pred,
                                              FastMathFlags FMF,
                                              Value *CmpLHS, Value *CmpRHS,
                                              Value *TrueVal, Value *FalseVal,
                                              Value *&LHS, Value *&RHS,
                                              unsigned Depth) {
  bool HasMismatchedZeros = false;
  if (CmpInst::isFPPredicate(Pred)) {
    // IEEE-754 ignores the sign of 0.0 in comparisons. So if the select has one
    // 0.0 operand, set the compare's 0.0 operands to that same value for the
    // purpose of identifying min/max. Disregard vector constants with undefined
    // elements because those can not be back-propagated for analysis.
    Value *OutputZeroVal = nullptr;
    if (match(TrueVal, m_AnyZeroFP()) && !match(FalseVal, m_AnyZeroFP()) &&
        !cast<Constant>(TrueVal)->containsUndefOrPoisonElement())
      OutputZeroVal = TrueVal;
    else if (match(FalseVal, m_AnyZeroFP()) && !match(TrueVal, m_AnyZeroFP()) &&
             !cast<Constant>(FalseVal)->containsUndefOrPoisonElement())
      OutputZeroVal = FalseVal;

    if (OutputZeroVal) {
      if (match(CmpLHS, m_AnyZeroFP()) && CmpLHS != OutputZeroVal) {
        HasMismatchedZeros = true;
        CmpLHS = OutputZeroVal;
      }
      if (match(CmpRHS, m_AnyZeroFP()) && CmpRHS != OutputZeroVal) {
        HasMismatchedZeros = true;
        CmpRHS = OutputZeroVal;
      }
    }
  }

  LHS = CmpLHS;
  RHS = CmpRHS;

  // Signed zero may return inconsistent results between implementations.
  //  (0.0 <= -0.0) ? 0.0 : -0.0 // Returns 0.0
  //  minNum(0.0, -0.0)          // May return -0.0 or 0.0 (IEEE 754-2008 5.3.1)
  // Therefore, we behave conservatively and only proceed if at least one of the
  // operands is known to not be zero or if we don't care about signed zero.
  switch (Pred) {
  default: break;
  case CmpInst::FCMP_OGT: case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_UGT: case CmpInst::FCMP_ULT:
    if (!HasMismatchedZeros)
      break;
    [[fallthrough]];
  case CmpInst::FCMP_OGE: case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_UGE: case CmpInst::FCMP_ULE:
    if (!FMF.noSignedZeros() && !isKnownNonZero(CmpLHS) &&
        !isKnownNonZero(CmpRHS))
      return {SPF_UNKNOWN, SPNB_NA, false};
  }

  SelectPatternNaNBehavior NaNBehavior = SPNB_NA;
  bool Ordered = false;

  // When given one NaN and one non-NaN input:
  //   - maxnum/minnum (C99 fmaxf()/fminf()) return the non-NaN input.
  //   - A simple C99 (a < b ? a : b) construction will return 'b' (as the
  //     ordered comparison fails), which could be NaN or non-NaN.
  // so here we discover exactly what NaN behavior is required/accepted.
  if (CmpInst::isFPPredicate(Pred)) {
    bool LHSSafe = isKnownNonNaN(CmpLHS, FMF);
    bool RHSSafe = isKnownNonNaN(CmpRHS, FMF);

    if (LHSSafe && RHSSafe) {
      // Both operands are known non-NaN.
      NaNBehavior = SPNB_RETURNS_ANY;
    } else if (CmpInst::isOrdered(Pred)) {
      // An ordered comparison will return false when given a NaN, so it
      // returns the RHS.
      Ordered = true;
      if (LHSSafe)
        // LHS is non-NaN, so if RHS is NaN then NaN will be returned.
        NaNBehavior = SPNB_RETURNS_NAN;
      else if (RHSSafe)
        NaNBehavior = SPNB_RETURNS_OTHER;
      else
        // Completely unsafe.
        return {SPF_UNKNOWN, SPNB_NA, false};
    } else {
      Ordered = false;
      // An unordered comparison will return true when given a NaN, so it
      // returns the LHS.
      if (LHSSafe)
        // LHS is non-NaN, so if RHS is NaN then non-NaN will be returned.
        NaNBehavior = SPNB_RETURNS_OTHER;
      else if (RHSSafe)
        NaNBehavior = SPNB_RETURNS_NAN;
      else
        // Completely unsafe.
        return {SPF_UNKNOWN, SPNB_NA, false};
    }
  }

  if (TrueVal == CmpRHS && FalseVal == CmpLHS) {
    std::swap(CmpLHS, CmpRHS);
    Pred = CmpInst::getSwappedPredicate(Pred);
    if (NaNBehavior == SPNB_RETURNS_NAN)
      NaNBehavior = SPNB_RETURNS_OTHER;
    else if (NaNBehavior == SPNB_RETURNS_OTHER)
      NaNBehavior = SPNB_RETURNS_NAN;
    Ordered = !Ordered;
  }

  // ([if]cmp X, Y) ? X : Y
  if (TrueVal == CmpLHS && FalseVal == CmpRHS) {
    switch (Pred) {
    default: return {SPF_UNKNOWN, SPNB_NA, false}; // Equality.
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE: return {SPF_UMAX, SPNB_NA, false};
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE: return {SPF_SMAX, SPNB_NA, false};
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_ULE: return {SPF_UMIN, SPNB_NA, false};
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE: return {SPF_SMIN, SPNB_NA, false};
    case FCmpInst::FCMP_UGT:
    case FCmpInst::FCMP_UGE:
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_OGE: return {SPF_FMAXNUM, NaNBehavior, Ordered};
    case FCmpInst::FCMP_ULT:
    case FCmpInst::FCMP_ULE:
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_OLE: return {SPF_FMINNUM, NaNBehavior, Ordered};
    }
  }

  if (isKnownNegation(TrueVal, FalseVal)) {
    // Sign-extending LHS does not change its sign, so TrueVal/FalseVal can
    // match against either LHS or sext(LHS).
    auto MaybeSExtCmpLHS =
        m_CombineOr(m_Specific(CmpLHS), m_SExt(m_Specific(CmpLHS)));
    auto ZeroOrAllOnes = m_CombineOr(m_ZeroInt(), m_AllOnes());
    auto ZeroOrOne = m_CombineOr(m_ZeroInt(), m_One());
    if (match(TrueVal, MaybeSExtCmpLHS)) {
      // Set the return values. If the compare uses the negated value (-X >s 0),
      // swap the return values because the negated value is always 'RHS'.
      LHS = TrueVal;
      RHS = FalseVal;
      if (match(CmpLHS, m_Neg(m_Specific(FalseVal))))
        std::swap(LHS, RHS);

      // (X >s 0) ? X : -X or (X >s -1) ? X : -X --> ABS(X)
      // (-X >s 0) ? -X : X or (-X >s -1) ? -X : X --> ABS(X)
      if (Pred == ICmpInst::ICMP_SGT && match(CmpRHS, ZeroOrAllOnes))
        return {SPF_ABS, SPNB_NA, false};

      // (X >=s 0) ? X : -X or (X >=s 1) ? X : -X --> ABS(X)
      if (Pred == ICmpInst::ICMP_SGE && match(CmpRHS, ZeroOrOne))
        return {SPF_ABS, SPNB_NA, false};

      // (X <s 0) ? X : -X or (X <s 1) ? X : -X --> NABS(X)
      // (-X <s 0) ? -X : X or (-X <s 1) ? -X : X --> NABS(X)
      if (Pred == ICmpInst::ICMP_SLT && match(CmpRHS, ZeroOrOne))
        return {SPF_NABS, SPNB_NA, false};
    }
    else if (match(FalseVal, MaybeSExtCmpLHS)) {
      // Set the return values. If the compare uses the negated value (-X >s 0),
      // swap the return values because the negated value is always 'RHS'.
      LHS = FalseVal;
      RHS = TrueVal;
      if (match(CmpLHS, m_Neg(m_Specific(TrueVal))))
        std::swap(LHS, RHS);

      // (X >s 0) ? -X : X or (X >s -1) ? -X : X --> NABS(X)
      // (-X >s 0) ? X : -X or (-X >s -1) ? X : -X --> NABS(X)
      if (Pred == ICmpInst::ICMP_SGT && match(CmpRHS, ZeroOrAllOnes))
        return {SPF_NABS, SPNB_NA, false};

      // (X <s 0) ? -X : X or (X <s 1) ? -X : X --> ABS(X)
      // (-X <s 0) ? X : -X or (-X <s 1) ? X : -X --> ABS(X)
      if (Pred == ICmpInst::ICMP_SLT && match(CmpRHS, ZeroOrOne))
        return {SPF_ABS, SPNB_NA, false};
    }
  }

  if (CmpInst::isIntPredicate(Pred))
    return matchMinMax(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal, LHS, RHS, Depth);

  // According to (IEEE 754-2008 5.3.1), minNum(0.0, -0.0) and similar
  // may return either -0.0 or 0.0, so fcmp/select pair has stricter
  // semantics than minNum. Be conservative in such case.
  if (NaNBehavior != SPNB_RETURNS_ANY ||
      (!FMF.noSignedZeros() && !isKnownNonZero(CmpLHS) &&
       !isKnownNonZero(CmpRHS)))
    return {SPF_UNKNOWN, SPNB_NA, false};

  return matchFastFloatClamp(Pred, CmpLHS, CmpRHS, TrueVal, FalseVal, LHS, RHS);
}

/// Helps to match a select pattern in case of a type mismatch.
///
/// The function processes the case when type of true and false values of a
/// select instruction differs from type of the cmp instruction operands because
/// of a cast instruction. The function checks if it is legal to move the cast
/// operation after "select". If yes, it returns the new second value of
/// "select" (with the assumption that cast is moved):
/// 1. As operand of cast instruction when both values of "select" are same cast
/// instructions.
/// 2. As restored constant (by applying reverse cast operation) when the first
/// value of the "select" is a cast operation and the second value is a
/// constant.
/// NOTE: We return only the new second value because the first value could be
/// accessed as operand of cast instruction.
static Value *lookThroughCast(CmpInst *CmpI, Value *V1, Value *V2,
                              Instruction::CastOps *CastOp) {
  auto *Cast1 = dyn_cast<CastInst>(V1);
  if (!Cast1)
    return nullptr;

  *CastOp = Cast1->getOpcode();
  Type *SrcTy = Cast1->getSrcTy();
  if (auto *Cast2 = dyn_cast<CastInst>(V2)) {
    // If V1 and V2 are both the same cast from the same type, look through V1.
    if (*CastOp == Cast2->getOpcode() && SrcTy == Cast2->getSrcTy())
      return Cast2->getOperand(0);
    return nullptr;
  }

  auto *C = dyn_cast<Constant>(V2);
  if (!C)
    return nullptr;

  const DataLayout &DL = CmpI->getDataLayout();
  Constant *CastedTo = nullptr;
  switch (*CastOp) {
  case Instruction::ZExt:
    if (CmpI->isUnsigned())
      CastedTo = ConstantExpr::getTrunc(C, SrcTy);
    break;
  case Instruction::SExt:
    if (CmpI->isSigned())
      CastedTo = ConstantExpr::getTrunc(C, SrcTy, true);
    break;
  case Instruction::Trunc:
    Constant *CmpConst;
    if (match(CmpI->getOperand(1), m_Constant(CmpConst)) &&
        CmpConst->getType() == SrcTy) {
      // Here we have the following case:
      //
      //   %cond = cmp iN %x, CmpConst
      //   %tr = trunc iN %x to iK
      //   %narrowsel = select i1 %cond, iK %t, iK C
      //
      // We can always move trunc after select operation:
      //
      //   %cond = cmp iN %x, CmpConst
      //   %widesel = select i1 %cond, iN %x, iN CmpConst
      //   %tr = trunc iN %widesel to iK
      //
      // Note that C could be extended in any way because we don't care about
      // upper bits after truncation. It can't be abs pattern, because it would
      // look like:
      //
      //   select i1 %cond, x, -x.
      //
      // So only min/max pattern could be matched. Such match requires widened C
      // == CmpConst. That is why set widened C = CmpConst, condition trunc
      // CmpConst == C is checked below.
      CastedTo = CmpConst;
    } else {
      unsigned ExtOp = CmpI->isSigned() ? Instruction::SExt : Instruction::ZExt;
      CastedTo = ConstantFoldCastOperand(ExtOp, C, SrcTy, DL);
    }
    break;
  case Instruction::FPTrunc:
    CastedTo = ConstantFoldCastOperand(Instruction::FPExt, C, SrcTy, DL);
    break;
  case Instruction::FPExt:
    CastedTo = ConstantFoldCastOperand(Instruction::FPTrunc, C, SrcTy, DL);
    break;
  case Instruction::FPToUI:
    CastedTo = ConstantFoldCastOperand(Instruction::UIToFP, C, SrcTy, DL);
    break;
  case Instruction::FPToSI:
    CastedTo = ConstantFoldCastOperand(Instruction::SIToFP, C, SrcTy, DL);
    break;
  case Instruction::UIToFP:
    CastedTo = ConstantFoldCastOperand(Instruction::FPToUI, C, SrcTy, DL);
    break;
  case Instruction::SIToFP:
    CastedTo = ConstantFoldCastOperand(Instruction::FPToSI, C, SrcTy, DL);
    break;
  default:
    break;
  }

  if (!CastedTo)
    return nullptr;

  // Make sure the cast doesn't lose any information.
  Constant *CastedBack =
      ConstantFoldCastOperand(*CastOp, CastedTo, C->getType(), DL);
  if (CastedBack && CastedBack != C)
    return nullptr;

  return CastedTo;
}

SelectPatternResult llvm::matchSelectPattern(Value *V, Value *&LHS, Value *&RHS,
                                             Instruction::CastOps *CastOp,
                                             unsigned Depth) {
  if (Depth >= MaxAnalysisRecursionDepth)
    return {SPF_UNKNOWN, SPNB_NA, false};

  SelectInst *SI = dyn_cast<SelectInst>(V);
  if (!SI) return {SPF_UNKNOWN, SPNB_NA, false};

  CmpInst *CmpI = dyn_cast<CmpInst>(SI->getCondition());
  if (!CmpI) return {SPF_UNKNOWN, SPNB_NA, false};

  Value *TrueVal = SI->getTrueValue();
  Value *FalseVal = SI->getFalseValue();

  return llvm::matchDecomposedSelectPattern(CmpI, TrueVal, FalseVal, LHS, RHS,
                                            CastOp, Depth);
}

SelectPatternResult llvm::matchDecomposedSelectPattern(
    CmpInst *CmpI, Value *TrueVal, Value *FalseVal, Value *&LHS, Value *&RHS,
    Instruction::CastOps *CastOp, unsigned Depth) {
  CmpInst::Predicate Pred = CmpI->getPredicate();
  Value *CmpLHS = CmpI->getOperand(0);
  Value *CmpRHS = CmpI->getOperand(1);
  FastMathFlags FMF;
  if (isa<FPMathOperator>(CmpI))
    FMF = CmpI->getFastMathFlags();

  // Bail out early.
  if (CmpI->isEquality())
    return {SPF_UNKNOWN, SPNB_NA, false};

  // Deal with type mismatches.
  if (CastOp && CmpLHS->getType() != TrueVal->getType()) {
    if (Value *C = lookThroughCast(CmpI, TrueVal, FalseVal, CastOp)) {
      // If this is a potential fmin/fmax with a cast to integer, then ignore
      // -0.0 because there is no corresponding integer value.
      if (*CastOp == Instruction::FPToSI || *CastOp == Instruction::FPToUI)
        FMF.setNoSignedZeros();
      return ::matchSelectPattern(Pred, FMF, CmpLHS, CmpRHS,
                                  cast<CastInst>(TrueVal)->getOperand(0), C,
                                  LHS, RHS, Depth);
    }
    if (Value *C = lookThroughCast(CmpI, FalseVal, TrueVal, CastOp)) {
      // If this is a potential fmin/fmax with a cast to integer, then ignore
      // -0.0 because there is no corresponding integer value.
      if (*CastOp == Instruction::FPToSI || *CastOp == Instruction::FPToUI)
        FMF.setNoSignedZeros();
      return ::matchSelectPattern(Pred, FMF, CmpLHS, CmpRHS,
                                  C, cast<CastInst>(FalseVal)->getOperand(0),
                                  LHS, RHS, Depth);
    }
  }
  return ::matchSelectPattern(Pred, FMF, CmpLHS, CmpRHS, TrueVal, FalseVal,
                              LHS, RHS, Depth);
}

CmpInst::Predicate llvm::getMinMaxPred(SelectPatternFlavor SPF, bool Ordered) {
  if (SPF == SPF_SMIN) return ICmpInst::ICMP_SLT;
  if (SPF == SPF_UMIN) return ICmpInst::ICMP_ULT;
  if (SPF == SPF_SMAX) return ICmpInst::ICMP_SGT;
  if (SPF == SPF_UMAX) return ICmpInst::ICMP_UGT;
  if (SPF == SPF_FMINNUM)
    return Ordered ? FCmpInst::FCMP_OLT : FCmpInst::FCMP_ULT;
  if (SPF == SPF_FMAXNUM)
    return Ordered ? FCmpInst::FCMP_OGT : FCmpInst::FCMP_UGT;
  llvm_unreachable("unhandled!");
}

SelectPatternFlavor llvm::getInverseMinMaxFlavor(SelectPatternFlavor SPF) {
  if (SPF == SPF_SMIN) return SPF_SMAX;
  if (SPF == SPF_UMIN) return SPF_UMAX;
  if (SPF == SPF_SMAX) return SPF_SMIN;
  if (SPF == SPF_UMAX) return SPF_UMIN;
  llvm_unreachable("unhandled!");
}

Intrinsic::ID llvm::getInverseMinMaxIntrinsic(Intrinsic::ID MinMaxID) {
  switch (MinMaxID) {
  case Intrinsic::smax: return Intrinsic::smin;
  case Intrinsic::smin: return Intrinsic::smax;
  case Intrinsic::umax: return Intrinsic::umin;
  case Intrinsic::umin: return Intrinsic::umax;
  // Please note that next four intrinsics may produce the same result for
  // original and inverted case even if X != Y due to NaN is handled specially.
  case Intrinsic::maximum: return Intrinsic::minimum;
  case Intrinsic::minimum: return Intrinsic::maximum;
  case Intrinsic::maxnum: return Intrinsic::minnum;
  case Intrinsic::minnum: return Intrinsic::maxnum;
  default: llvm_unreachable("Unexpected intrinsic");
  }
}

APInt llvm::getMinMaxLimit(SelectPatternFlavor SPF, unsigned BitWidth) {
  switch (SPF) {
  case SPF_SMAX: return APInt::getSignedMaxValue(BitWidth);
  case SPF_SMIN: return APInt::getSignedMinValue(BitWidth);
  case SPF_UMAX: return APInt::getMaxValue(BitWidth);
  case SPF_UMIN: return APInt::getMinValue(BitWidth);
  default: llvm_unreachable("Unexpected flavor");
  }
}

std::pair<Intrinsic::ID, bool>
llvm::canConvertToMinOrMaxIntrinsic(ArrayRef<Value *> VL) {
  // Check if VL contains select instructions that can be folded into a min/max
  // vector intrinsic and return the intrinsic if it is possible.
  // TODO: Support floating point min/max.
  bool AllCmpSingleUse = true;
  SelectPatternResult SelectPattern;
  SelectPattern.Flavor = SPF_UNKNOWN;
  if (all_of(VL, [&SelectPattern, &AllCmpSingleUse](Value *I) {
        Value *LHS, *RHS;
        auto CurrentPattern = matchSelectPattern(I, LHS, RHS);
        if (!SelectPatternResult::isMinOrMax(CurrentPattern.Flavor))
          return false;
        if (SelectPattern.Flavor != SPF_UNKNOWN &&
            SelectPattern.Flavor != CurrentPattern.Flavor)
          return false;
        SelectPattern = CurrentPattern;
        AllCmpSingleUse &=
            match(I, m_Select(m_OneUse(m_Value()), m_Value(), m_Value()));
        return true;
      })) {
    switch (SelectPattern.Flavor) {
    case SPF_SMIN:
      return {Intrinsic::smin, AllCmpSingleUse};
    case SPF_UMIN:
      return {Intrinsic::umin, AllCmpSingleUse};
    case SPF_SMAX:
      return {Intrinsic::smax, AllCmpSingleUse};
    case SPF_UMAX:
      return {Intrinsic::umax, AllCmpSingleUse};
    case SPF_FMAXNUM:
      return {Intrinsic::maxnum, AllCmpSingleUse};
    case SPF_FMINNUM:
      return {Intrinsic::minnum, AllCmpSingleUse};
    default:
      llvm_unreachable("unexpected select pattern flavor");
    }
  }
  return {Intrinsic::not_intrinsic, false};
}

bool llvm::matchSimpleRecurrence(const PHINode *P, BinaryOperator *&BO,
                                 Value *&Start, Value *&Step) {
  // Handle the case of a simple two-predecessor recurrence PHI.
  // There's a lot more that could theoretically be done here, but
  // this is sufficient to catch some interesting cases.
  if (P->getNumIncomingValues() != 2)
    return false;

  for (unsigned i = 0; i != 2; ++i) {
    Value *L = P->getIncomingValue(i);
    Value *R = P->getIncomingValue(!i);
    auto *LU = dyn_cast<BinaryOperator>(L);
    if (!LU)
      continue;
    unsigned Opcode = LU->getOpcode();

    switch (Opcode) {
    default:
      continue;
    // TODO: Expand list -- xor, div, gep, uaddo, etc..
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::Shl:
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Mul:
    case Instruction::FMul: {
      Value *LL = LU->getOperand(0);
      Value *LR = LU->getOperand(1);
      // Find a recurrence.
      if (LL == P)
        L = LR;
      else if (LR == P)
        L = LL;
      else
        continue; // Check for recurrence with L and R flipped.

      break; // Match!
    }
    };

    // We have matched a recurrence of the form:
    //   %iv = [R, %entry], [%iv.next, %backedge]
    //   %iv.next = binop %iv, L
    // OR
    //   %iv = [R, %entry], [%iv.next, %backedge]
    //   %iv.next = binop L, %iv
    BO = LU;
    Start = R;
    Step = L;
    return true;
  }
  return false;
}

bool llvm::matchSimpleRecurrence(const BinaryOperator *I, PHINode *&P,
                                 Value *&Start, Value *&Step) {
  BinaryOperator *BO = nullptr;
  P = dyn_cast<PHINode>(I->getOperand(0));
  if (!P)
    P = dyn_cast<PHINode>(I->getOperand(1));
  return P && matchSimpleRecurrence(P, BO, Start, Step) && BO == I;
}

/// Return true if "icmp Pred LHS RHS" is always true.
static bool isTruePredicate(CmpInst::Predicate Pred, const Value *LHS,
                            const Value *RHS) {
  if (ICmpInst::isTrueWhenEqual(Pred) && LHS == RHS)
    return true;

  switch (Pred) {
  default:
    return false;

  case CmpInst::ICMP_SLE: {
    const APInt *C;

    // LHS s<= LHS +_{nsw} C   if C >= 0
    // LHS s<= LHS | C         if C >= 0
    if (match(RHS, m_NSWAdd(m_Specific(LHS), m_APInt(C))) ||
        match(RHS, m_Or(m_Specific(LHS), m_APInt(C))))
      return !C->isNegative();

    // LHS s<= smax(LHS, V) for any V
    if (match(RHS, m_c_SMax(m_Specific(LHS), m_Value())))
      return true;

    // smin(RHS, V) s<= RHS for any V
    if (match(LHS, m_c_SMin(m_Specific(RHS), m_Value())))
      return true;

    // Match A to (X +_{nsw} CA) and B to (X +_{nsw} CB)
    const Value *X;
    const APInt *CLHS, *CRHS;
    if (match(LHS, m_NSWAddLike(m_Value(X), m_APInt(CLHS))) &&
        match(RHS, m_NSWAddLike(m_Specific(X), m_APInt(CRHS))))
      return CLHS->sle(*CRHS);

    return false;
  }

  case CmpInst::ICMP_ULE: {
    // LHS u<= LHS +_{nuw} V for any V
    if (match(RHS, m_c_Add(m_Specific(LHS), m_Value())) &&
        cast<OverflowingBinaryOperator>(RHS)->hasNoUnsignedWrap())
      return true;

    // LHS u<= LHS | V for any V
    if (match(RHS, m_c_Or(m_Specific(LHS), m_Value())))
      return true;

    // LHS u<= umax(LHS, V) for any V
    if (match(RHS, m_c_UMax(m_Specific(LHS), m_Value())))
      return true;

    // RHS >> V u<= RHS for any V
    if (match(LHS, m_LShr(m_Specific(RHS), m_Value())))
      return true;

    // RHS u/ C_ugt_1 u<= RHS
    const APInt *C;
    if (match(LHS, m_UDiv(m_Specific(RHS), m_APInt(C))) && C->ugt(1))
      return true;

    // RHS & V u<= RHS for any V
    if (match(LHS, m_c_And(m_Specific(RHS), m_Value())))
      return true;

    // umin(RHS, V) u<= RHS for any V
    if (match(LHS, m_c_UMin(m_Specific(RHS), m_Value())))
      return true;

    // Match A to (X +_{nuw} CA) and B to (X +_{nuw} CB)
    const Value *X;
    const APInt *CLHS, *CRHS;
    if (match(LHS, m_NUWAddLike(m_Value(X), m_APInt(CLHS))) &&
        match(RHS, m_NUWAddLike(m_Specific(X), m_APInt(CRHS))))
      return CLHS->ule(*CRHS);

    return false;
  }
  }
}

/// Return true if "icmp Pred BLHS BRHS" is true whenever "icmp Pred
/// ALHS ARHS" is true.  Otherwise, return std::nullopt.
static std::optional<bool>
isImpliedCondOperands(CmpInst::Predicate Pred, const Value *ALHS,
                      const Value *ARHS, const Value *BLHS, const Value *BRHS) {
  switch (Pred) {
  default:
    return std::nullopt;

  case CmpInst::ICMP_SLT:
  case CmpInst::ICMP_SLE:
    if (isTruePredicate(CmpInst::ICMP_SLE, BLHS, ALHS) &&
        isTruePredicate(CmpInst::ICMP_SLE, ARHS, BRHS))
      return true;
    return std::nullopt;

  case CmpInst::ICMP_SGT:
  case CmpInst::ICMP_SGE:
    if (isTruePredicate(CmpInst::ICMP_SLE, ALHS, BLHS) &&
        isTruePredicate(CmpInst::ICMP_SLE, BRHS, ARHS))
      return true;
    return std::nullopt;

  case CmpInst::ICMP_ULT:
  case CmpInst::ICMP_ULE:
    if (isTruePredicate(CmpInst::ICMP_ULE, BLHS, ALHS) &&
        isTruePredicate(CmpInst::ICMP_ULE, ARHS, BRHS))
      return true;
    return std::nullopt;

  case CmpInst::ICMP_UGT:
  case CmpInst::ICMP_UGE:
    if (isTruePredicate(CmpInst::ICMP_ULE, ALHS, BLHS) &&
        isTruePredicate(CmpInst::ICMP_ULE, BRHS, ARHS))
      return true;
    return std::nullopt;
  }
}

/// Return true if "icmp1 LPred X, Y" implies "icmp2 RPred X, Y" is true.
/// Return false if "icmp1 LPred X, Y" implies "icmp2 RPred X, Y" is false.
/// Otherwise, return std::nullopt if we can't infer anything.
static std::optional<bool>
isImpliedCondMatchingOperands(CmpInst::Predicate LPred,
                              CmpInst::Predicate RPred) {
  if (CmpInst::isImpliedTrueByMatchingCmp(LPred, RPred))
    return true;
  if (CmpInst::isImpliedFalseByMatchingCmp(LPred, RPred))
    return false;

  return std::nullopt;
}

/// Return true if "icmp LPred X, LCR" implies "icmp RPred X, RCR" is true.
/// Return false if "icmp LPred X, LCR" implies "icmp RPred X, RCR" is false.
/// Otherwise, return std::nullopt if we can't infer anything.
static std::optional<bool> isImpliedCondCommonOperandWithCR(
    CmpInst::Predicate LPred, const ConstantRange &LCR,
    CmpInst::Predicate RPred, const ConstantRange &RCR) {
  ConstantRange DomCR = ConstantRange::makeAllowedICmpRegion(LPred, LCR);
  // If all true values for lhs and true for rhs, lhs implies rhs
  if (DomCR.icmp(RPred, RCR))
    return true;

  // If there is no overlap, lhs implies not rhs
  if (DomCR.icmp(CmpInst::getInversePredicate(RPred), RCR))
    return false;
  return std::nullopt;
}

/// Return true if LHS implies RHS (expanded to its components as "R0 RPred R1")
/// is true.  Return false if LHS implies RHS is false. Otherwise, return
/// std::nullopt if we can't infer anything.
static std::optional<bool> isImpliedCondICmps(const ICmpInst *LHS,
                                              CmpInst::Predicate RPred,
                                              const Value *R0, const Value *R1,
                                              const DataLayout &DL,
                                              bool LHSIsTrue) {
  Value *L0 = LHS->getOperand(0);
  Value *L1 = LHS->getOperand(1);

  // The rest of the logic assumes the LHS condition is true.  If that's not the
  // case, invert the predicate to make it so.
  CmpInst::Predicate LPred =
      LHSIsTrue ? LHS->getPredicate() : LHS->getInversePredicate();

  // We can have non-canonical operands, so try to normalize any common operand
  // to L0/R0.
  if (L0 == R1) {
    std::swap(R0, R1);
    RPred = ICmpInst::getSwappedPredicate(RPred);
  }
  if (R0 == L1) {
    std::swap(L0, L1);
    LPred = ICmpInst::getSwappedPredicate(LPred);
  }
  if (L1 == R1) {
    // If we have L0 == R0 and L1 == R1, then make L1/R1 the constants.
    if (L0 != R0 || match(L0, m_ImmConstant())) {
      std::swap(L0, L1);
      LPred = ICmpInst::getSwappedPredicate(LPred);
      std::swap(R0, R1);
      RPred = ICmpInst::getSwappedPredicate(RPred);
    }
  }

  // See if we can infer anything if operand-0 matches and we have at least one
  // constant.
  const APInt *Unused;
  if (L0 == R0 && (match(L1, m_APInt(Unused)) || match(R1, m_APInt(Unused)))) {
    // Potential TODO: We could also further use the constant range of L0/R0 to
    // further constraint the constant ranges. At the moment this leads to
    // several regressions related to not transforming `multi_use(A + C0) eq/ne
    // C1` (see discussion: D58633).
    ConstantRange LCR = computeConstantRange(
        L1, ICmpInst::isSigned(LPred), /* UseInstrInfo=*/true, /*AC=*/nullptr,
        /*CxtI=*/nullptr, /*DT=*/nullptr, MaxAnalysisRecursionDepth - 1);
    ConstantRange RCR = computeConstantRange(
        R1, ICmpInst::isSigned(RPred), /* UseInstrInfo=*/true, /*AC=*/nullptr,
        /*CxtI=*/nullptr, /*DT=*/nullptr, MaxAnalysisRecursionDepth - 1);
    // Even if L1/R1 are not both constant, we can still sometimes deduce
    // relationship from a single constant. For example X u> Y implies X != 0.
    if (auto R = isImpliedCondCommonOperandWithCR(LPred, LCR, RPred, RCR))
      return R;
    // If both L1/R1 were exact constant ranges and we didn't get anything
    // here, we won't be able to deduce this.
    if (match(L1, m_APInt(Unused)) && match(R1, m_APInt(Unused)))
      return std::nullopt;
  }

  // Can we infer anything when the two compares have matching operands?
  if (L0 == R0 && L1 == R1)
    return isImpliedCondMatchingOperands(LPred, RPred);

  // L0 = R0 = L1 + R1, L0 >=u L1 implies R0 >=u R1, L0 <u L1 implies R0 <u R1
  if (L0 == R0 &&
      (LPred == ICmpInst::ICMP_ULT || LPred == ICmpInst::ICMP_UGE) &&
      (RPred == ICmpInst::ICMP_ULT || RPred == ICmpInst::ICMP_UGE) &&
      match(L0, m_c_Add(m_Specific(L1), m_Specific(R1))))
    return LPred == RPred;

  if (LPred == RPred)
    return isImpliedCondOperands(LPred, L0, L1, R0, R1);

  return std::nullopt;
}

/// Return true if LHS implies RHS is true.  Return false if LHS implies RHS is
/// false.  Otherwise, return std::nullopt if we can't infer anything.  We
/// expect the RHS to be an icmp and the LHS to be an 'and', 'or', or a 'select'
/// instruction.
static std::optional<bool>
isImpliedCondAndOr(const Instruction *LHS, CmpInst::Predicate RHSPred,
                   const Value *RHSOp0, const Value *RHSOp1,
                   const DataLayout &DL, bool LHSIsTrue, unsigned Depth) {
  // The LHS must be an 'or', 'and', or a 'select' instruction.
  assert((LHS->getOpcode() == Instruction::And ||
          LHS->getOpcode() == Instruction::Or ||
          LHS->getOpcode() == Instruction::Select) &&
         "Expected LHS to be 'and', 'or', or 'select'.");

  assert(Depth <= MaxAnalysisRecursionDepth && "Hit recursion limit");

  // If the result of an 'or' is false, then we know both legs of the 'or' are
  // false.  Similarly, if the result of an 'and' is true, then we know both
  // legs of the 'and' are true.
  const Value *ALHS, *ARHS;
  if ((!LHSIsTrue && match(LHS, m_LogicalOr(m_Value(ALHS), m_Value(ARHS)))) ||
      (LHSIsTrue && match(LHS, m_LogicalAnd(m_Value(ALHS), m_Value(ARHS))))) {
    // FIXME: Make this non-recursion.
    if (std::optional<bool> Implication = isImpliedCondition(
            ALHS, RHSPred, RHSOp0, RHSOp1, DL, LHSIsTrue, Depth + 1))
      return Implication;
    if (std::optional<bool> Implication = isImpliedCondition(
            ARHS, RHSPred, RHSOp0, RHSOp1, DL, LHSIsTrue, Depth + 1))
      return Implication;
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<bool>
llvm::isImpliedCondition(const Value *LHS, CmpInst::Predicate RHSPred,
                         const Value *RHSOp0, const Value *RHSOp1,
                         const DataLayout &DL, bool LHSIsTrue, unsigned Depth) {
  // Bail out when we hit the limit.
  if (Depth == MaxAnalysisRecursionDepth)
    return std::nullopt;

  // A mismatch occurs when we compare a scalar cmp to a vector cmp, for
  // example.
  if (RHSOp0->getType()->isVectorTy() != LHS->getType()->isVectorTy())
    return std::nullopt;

  assert(LHS->getType()->isIntOrIntVectorTy(1) &&
         "Expected integer type only!");

  // Match not
  if (match(LHS, m_Not(m_Value(LHS))))
    LHSIsTrue = !LHSIsTrue;

  // Both LHS and RHS are icmps.
  const ICmpInst *LHSCmp = dyn_cast<ICmpInst>(LHS);
  if (LHSCmp)
    return isImpliedCondICmps(LHSCmp, RHSPred, RHSOp0, RHSOp1, DL, LHSIsTrue);

  /// The LHS should be an 'or', 'and', or a 'select' instruction.  We expect
  /// the RHS to be an icmp.
  /// FIXME: Add support for and/or/select on the RHS.
  if (const Instruction *LHSI = dyn_cast<Instruction>(LHS)) {
    if ((LHSI->getOpcode() == Instruction::And ||
         LHSI->getOpcode() == Instruction::Or ||
         LHSI->getOpcode() == Instruction::Select))
      return isImpliedCondAndOr(LHSI, RHSPred, RHSOp0, RHSOp1, DL, LHSIsTrue,
                                Depth);
  }
  return std::nullopt;
}

std::optional<bool> llvm::isImpliedCondition(const Value *LHS, const Value *RHS,
                                             const DataLayout &DL,
                                             bool LHSIsTrue, unsigned Depth) {
  // LHS ==> RHS by definition
  if (LHS == RHS)
    return LHSIsTrue;

  // Match not
  bool InvertRHS = false;
  if (match(RHS, m_Not(m_Value(RHS)))) {
    if (LHS == RHS)
      return !LHSIsTrue;
    InvertRHS = true;
  }

  if (const ICmpInst *RHSCmp = dyn_cast<ICmpInst>(RHS)) {
    if (auto Implied = isImpliedCondition(
            LHS, RHSCmp->getPredicate(), RHSCmp->getOperand(0),
            RHSCmp->getOperand(1), DL, LHSIsTrue, Depth))
      return InvertRHS ? !*Implied : *Implied;
    return std::nullopt;
  }

  if (Depth == MaxAnalysisRecursionDepth)
    return std::nullopt;

  // LHS ==> (RHS1 || RHS2) if LHS ==> RHS1 or LHS ==> RHS2
  // LHS ==> !(RHS1 && RHS2) if LHS ==> !RHS1 or LHS ==> !RHS2
  const Value *RHS1, *RHS2;
  if (match(RHS, m_LogicalOr(m_Value(RHS1), m_Value(RHS2)))) {
    if (std::optional<bool> Imp =
            isImpliedCondition(LHS, RHS1, DL, LHSIsTrue, Depth + 1))
      if (*Imp == true)
        return !InvertRHS;
    if (std::optional<bool> Imp =
            isImpliedCondition(LHS, RHS2, DL, LHSIsTrue, Depth + 1))
      if (*Imp == true)
        return !InvertRHS;
  }
  if (match(RHS, m_LogicalAnd(m_Value(RHS1), m_Value(RHS2)))) {
    if (std::optional<bool> Imp =
            isImpliedCondition(LHS, RHS1, DL, LHSIsTrue, Depth + 1))
      if (*Imp == false)
        return InvertRHS;
    if (std::optional<bool> Imp =
            isImpliedCondition(LHS, RHS2, DL, LHSIsTrue, Depth + 1))
      if (*Imp == false)
        return InvertRHS;
  }

  return std::nullopt;
}

// Returns a pair (Condition, ConditionIsTrue), where Condition is a branch
// condition dominating ContextI or nullptr, if no condition is found.
static std::pair<Value *, bool>
getDomPredecessorCondition(const Instruction *ContextI) {
  if (!ContextI || !ContextI->getParent())
    return {nullptr, false};

  // TODO: This is a poor/cheap way to determine dominance. Should we use a
  // dominator tree (eg, from a SimplifyQuery) instead?
  const BasicBlock *ContextBB = ContextI->getParent();
  const BasicBlock *PredBB = ContextBB->getSinglePredecessor();
  if (!PredBB)
    return {nullptr, false};

  // We need a conditional branch in the predecessor.
  Value *PredCond;
  BasicBlock *TrueBB, *FalseBB;
  if (!match(PredBB->getTerminator(), m_Br(m_Value(PredCond), TrueBB, FalseBB)))
    return {nullptr, false};

  // The branch should get simplified. Don't bother simplifying this condition.
  if (TrueBB == FalseBB)
    return {nullptr, false};

  assert((TrueBB == ContextBB || FalseBB == ContextBB) &&
         "Predecessor block does not point to successor?");

  // Is this condition implied by the predecessor condition?
  return {PredCond, TrueBB == ContextBB};
}

std::optional<bool> llvm::isImpliedByDomCondition(const Value *Cond,
                                                  const Instruction *ContextI,
                                                  const DataLayout &DL) {
  assert(Cond->getType()->isIntOrIntVectorTy(1) && "Condition must be bool");
  auto PredCond = getDomPredecessorCondition(ContextI);
  if (PredCond.first)
    return isImpliedCondition(PredCond.first, Cond, DL, PredCond.second);
  return std::nullopt;
}

std::optional<bool> llvm::isImpliedByDomCondition(CmpInst::Predicate Pred,
                                                  const Value *LHS,
                                                  const Value *RHS,
                                                  const Instruction *ContextI,
                                                  const DataLayout &DL) {
  auto PredCond = getDomPredecessorCondition(ContextI);
  if (PredCond.first)
    return isImpliedCondition(PredCond.first, Pred, LHS, RHS, DL,
                              PredCond.second);
  return std::nullopt;
}

static void setLimitsForBinOp(const BinaryOperator &BO, APInt &Lower,
                              APInt &Upper, const InstrInfoQuery &IIQ,
                              bool PreferSignedRange) {
  unsigned Width = Lower.getBitWidth();
  const APInt *C;
  switch (BO.getOpcode()) {
  case Instruction::Add:
    if (match(BO.getOperand(1), m_APInt(C)) && !C->isZero()) {
      bool HasNSW = IIQ.hasNoSignedWrap(&BO);
      bool HasNUW = IIQ.hasNoUnsignedWrap(&BO);

      // If the caller expects a signed compare, then try to use a signed range.
      // Otherwise if both no-wraps are set, use the unsigned range because it
      // is never larger than the signed range. Example:
      // "add nuw nsw i8 X, -2" is unsigned [254,255] vs. signed [-128, 125].
      if (PreferSignedRange && HasNSW && HasNUW)
        HasNUW = false;

      if (HasNUW) {
        // 'add nuw x, C' produces [C, UINT_MAX].
        Lower = *C;
      } else if (HasNSW) {
        if (C->isNegative()) {
          // 'add nsw x, -C' produces [SINT_MIN, SINT_MAX - C].
          Lower = APInt::getSignedMinValue(Width);
          Upper = APInt::getSignedMaxValue(Width) + *C + 1;
        } else {
          // 'add nsw x, +C' produces [SINT_MIN + C, SINT_MAX].
          Lower = APInt::getSignedMinValue(Width) + *C;
          Upper = APInt::getSignedMaxValue(Width) + 1;
        }
      }
    }
    break;

  case Instruction::And:
    if (match(BO.getOperand(1), m_APInt(C)))
      // 'and x, C' produces [0, C].
      Upper = *C + 1;
    // X & -X is a power of two or zero. So we can cap the value at max power of
    // two.
    if (match(BO.getOperand(0), m_Neg(m_Specific(BO.getOperand(1)))) ||
        match(BO.getOperand(1), m_Neg(m_Specific(BO.getOperand(0)))))
      Upper = APInt::getSignedMinValue(Width) + 1;
    break;

  case Instruction::Or:
    if (match(BO.getOperand(1), m_APInt(C)))
      // 'or x, C' produces [C, UINT_MAX].
      Lower = *C;
    break;

  case Instruction::AShr:
    if (match(BO.getOperand(1), m_APInt(C)) && C->ult(Width)) {
      // 'ashr x, C' produces [INT_MIN >> C, INT_MAX >> C].
      Lower = APInt::getSignedMinValue(Width).ashr(*C);
      Upper = APInt::getSignedMaxValue(Width).ashr(*C) + 1;
    } else if (match(BO.getOperand(0), m_APInt(C))) {
      unsigned ShiftAmount = Width - 1;
      if (!C->isZero() && IIQ.isExact(&BO))
        ShiftAmount = C->countr_zero();
      if (C->isNegative()) {
        // 'ashr C, x' produces [C, C >> (Width-1)]
        Lower = *C;
        Upper = C->ashr(ShiftAmount) + 1;
      } else {
        // 'ashr C, x' produces [C >> (Width-1), C]
        Lower = C->ashr(ShiftAmount);
        Upper = *C + 1;
      }
    }
    break;

  case Instruction::LShr:
    if (match(BO.getOperand(1), m_APInt(C)) && C->ult(Width)) {
      // 'lshr x, C' produces [0, UINT_MAX >> C].
      Upper = APInt::getAllOnes(Width).lshr(*C) + 1;
    } else if (match(BO.getOperand(0), m_APInt(C))) {
      // 'lshr C, x' produces [C >> (Width-1), C].
      unsigned ShiftAmount = Width - 1;
      if (!C->isZero() && IIQ.isExact(&BO))
        ShiftAmount = C->countr_zero();
      Lower = C->lshr(ShiftAmount);
      Upper = *C + 1;
    }
    break;

  case Instruction::Shl:
    if (match(BO.getOperand(0), m_APInt(C))) {
      if (IIQ.hasNoUnsignedWrap(&BO)) {
        // 'shl nuw C, x' produces [C, C << CLZ(C)]
        Lower = *C;
        Upper = Lower.shl(Lower.countl_zero()) + 1;
      } else if (BO.hasNoSignedWrap()) { // TODO: What if both nuw+nsw?
        if (C->isNegative()) {
          // 'shl nsw C, x' produces [C << CLO(C)-1, C]
          unsigned ShiftAmount = C->countl_one() - 1;
          Lower = C->shl(ShiftAmount);
          Upper = *C + 1;
        } else {
          // 'shl nsw C, x' produces [C, C << CLZ(C)-1]
          unsigned ShiftAmount = C->countl_zero() - 1;
          Lower = *C;
          Upper = C->shl(ShiftAmount) + 1;
        }
      } else {
        // If lowbit is set, value can never be zero.
        if ((*C)[0])
          Lower = APInt::getOneBitSet(Width, 0);
        // If we are shifting a constant the largest it can be is if the longest
        // sequence of consecutive ones is shifted to the highbits (breaking
        // ties for which sequence is higher). At the moment we take a liberal
        // upper bound on this by just popcounting the constant.
        // TODO: There may be a bitwise trick for it longest/highest
        // consecutative sequence of ones (naive method is O(Width) loop).
        Upper = APInt::getHighBitsSet(Width, C->popcount()) + 1;
      }
    } else if (match(BO.getOperand(1), m_APInt(C)) && C->ult(Width)) {
      Upper = APInt::getBitsSetFrom(Width, C->getZExtValue()) + 1;
    }
    break;

  case Instruction::SDiv:
    if (match(BO.getOperand(1), m_APInt(C))) {
      APInt IntMin = APInt::getSignedMinValue(Width);
      APInt IntMax = APInt::getSignedMaxValue(Width);
      if (C->isAllOnes()) {
        // 'sdiv x, -1' produces [INT_MIN + 1, INT_MAX]
        //    where C != -1 and C != 0 and C != 1
        Lower = IntMin + 1;
        Upper = IntMax + 1;
      } else if (C->countl_zero() < Width - 1) {
        // 'sdiv x, C' produces [INT_MIN / C, INT_MAX / C]
        //    where C != -1 and C != 0 and C != 1
        Lower = IntMin.sdiv(*C);
        Upper = IntMax.sdiv(*C);
        if (Lower.sgt(Upper))
          std::swap(Lower, Upper);
        Upper = Upper + 1;
        assert(Upper != Lower && "Upper part of range has wrapped!");
      }
    } else if (match(BO.getOperand(0), m_APInt(C))) {
      if (C->isMinSignedValue()) {
        // 'sdiv INT_MIN, x' produces [INT_MIN, INT_MIN / -2].
        Lower = *C;
        Upper = Lower.lshr(1) + 1;
      } else {
        // 'sdiv C, x' produces [-|C|, |C|].
        Upper = C->abs() + 1;
        Lower = (-Upper) + 1;
      }
    }
    break;

  case Instruction::UDiv:
    if (match(BO.getOperand(1), m_APInt(C)) && !C->isZero()) {
      // 'udiv x, C' produces [0, UINT_MAX / C].
      Upper = APInt::getMaxValue(Width).udiv(*C) + 1;
    } else if (match(BO.getOperand(0), m_APInt(C))) {
      // 'udiv C, x' produces [0, C].
      Upper = *C + 1;
    }
    break;

  case Instruction::SRem:
    if (match(BO.getOperand(1), m_APInt(C))) {
      // 'srem x, C' produces (-|C|, |C|).
      Upper = C->abs();
      Lower = (-Upper) + 1;
    } else if (match(BO.getOperand(0), m_APInt(C))) {
      if (C->isNegative()) {
        // 'srem -|C|, x' produces [-|C|, 0].
        Upper = 1;
        Lower = *C;
      } else {
        // 'srem |C|, x' produces [0, |C|].
        Upper = *C + 1;
      }
    }
    break;

  case Instruction::URem:
    if (match(BO.getOperand(1), m_APInt(C)))
      // 'urem x, C' produces [0, C).
      Upper = *C;
    else if (match(BO.getOperand(0), m_APInt(C)))
      // 'urem C, x' produces [0, C].
      Upper = *C + 1;
    break;

  default:
    break;
  }
}

static ConstantRange getRangeForIntrinsic(const IntrinsicInst &II) {
  unsigned Width = II.getType()->getScalarSizeInBits();
  const APInt *C;
  switch (II.getIntrinsicID()) {
  case Intrinsic::ctpop:
  case Intrinsic::ctlz:
  case Intrinsic::cttz:
    // Maximum of set/clear bits is the bit width.
    return ConstantRange::getNonEmpty(APInt::getZero(Width),
                                      APInt(Width, Width + 1));
  case Intrinsic::uadd_sat:
    // uadd.sat(x, C) produces [C, UINT_MAX].
    if (match(II.getOperand(0), m_APInt(C)) ||
        match(II.getOperand(1), m_APInt(C)))
      return ConstantRange::getNonEmpty(*C, APInt::getZero(Width));
    break;
  case Intrinsic::sadd_sat:
    if (match(II.getOperand(0), m_APInt(C)) ||
        match(II.getOperand(1), m_APInt(C))) {
      if (C->isNegative())
        // sadd.sat(x, -C) produces [SINT_MIN, SINT_MAX + (-C)].
        return ConstantRange::getNonEmpty(APInt::getSignedMinValue(Width),
                                          APInt::getSignedMaxValue(Width) + *C +
                                              1);

      // sadd.sat(x, +C) produces [SINT_MIN + C, SINT_MAX].
      return ConstantRange::getNonEmpty(APInt::getSignedMinValue(Width) + *C,
                                        APInt::getSignedMaxValue(Width) + 1);
    }
    break;
  case Intrinsic::usub_sat:
    // usub.sat(C, x) produces [0, C].
    if (match(II.getOperand(0), m_APInt(C)))
      return ConstantRange::getNonEmpty(APInt::getZero(Width), *C + 1);

    // usub.sat(x, C) produces [0, UINT_MAX - C].
    if (match(II.getOperand(1), m_APInt(C)))
      return ConstantRange::getNonEmpty(APInt::getZero(Width),
                                        APInt::getMaxValue(Width) - *C + 1);
    break;
  case Intrinsic::ssub_sat:
    if (match(II.getOperand(0), m_APInt(C))) {
      if (C->isNegative())
        // ssub.sat(-C, x) produces [SINT_MIN, -SINT_MIN + (-C)].
        return ConstantRange::getNonEmpty(APInt::getSignedMinValue(Width),
                                          *C - APInt::getSignedMinValue(Width) +
                                              1);

      // ssub.sat(+C, x) produces [-SINT_MAX + C, SINT_MAX].
      return ConstantRange::getNonEmpty(*C - APInt::getSignedMaxValue(Width),
                                        APInt::getSignedMaxValue(Width) + 1);
    } else if (match(II.getOperand(1), m_APInt(C))) {
      if (C->isNegative())
        // ssub.sat(x, -C) produces [SINT_MIN - (-C), SINT_MAX]:
        return ConstantRange::getNonEmpty(APInt::getSignedMinValue(Width) - *C,
                                          APInt::getSignedMaxValue(Width) + 1);

      // ssub.sat(x, +C) produces [SINT_MIN, SINT_MAX - C].
      return ConstantRange::getNonEmpty(APInt::getSignedMinValue(Width),
                                        APInt::getSignedMaxValue(Width) - *C +
                                            1);
    }
    break;
  case Intrinsic::umin:
  case Intrinsic::umax:
  case Intrinsic::smin:
  case Intrinsic::smax:
    if (!match(II.getOperand(0), m_APInt(C)) &&
        !match(II.getOperand(1), m_APInt(C)))
      break;

    switch (II.getIntrinsicID()) {
    case Intrinsic::umin:
      return ConstantRange::getNonEmpty(APInt::getZero(Width), *C + 1);
    case Intrinsic::umax:
      return ConstantRange::getNonEmpty(*C, APInt::getZero(Width));
    case Intrinsic::smin:
      return ConstantRange::getNonEmpty(APInt::getSignedMinValue(Width),
                                        *C + 1);
    case Intrinsic::smax:
      return ConstantRange::getNonEmpty(*C,
                                        APInt::getSignedMaxValue(Width) + 1);
    default:
      llvm_unreachable("Must be min/max intrinsic");
    }
    break;
  case Intrinsic::abs:
    // If abs of SIGNED_MIN is poison, then the result is [0..SIGNED_MAX],
    // otherwise it is [0..SIGNED_MIN], as -SIGNED_MIN == SIGNED_MIN.
    if (match(II.getOperand(1), m_One()))
      return ConstantRange::getNonEmpty(APInt::getZero(Width),
                                        APInt::getSignedMaxValue(Width) + 1);

    return ConstantRange::getNonEmpty(APInt::getZero(Width),
                                      APInt::getSignedMinValue(Width) + 1);
  case Intrinsic::vscale:
    if (!II.getParent() || !II.getFunction())
      break;
    return getVScaleRange(II.getFunction(), Width);
  case Intrinsic::scmp:
  case Intrinsic::ucmp:
    return ConstantRange::getNonEmpty(APInt::getAllOnes(Width),
                                      APInt(Width, 2));
  default:
    break;
  }

  return ConstantRange::getFull(Width);
}

static ConstantRange getRangeForSelectPattern(const SelectInst &SI,
                                              const InstrInfoQuery &IIQ) {
  unsigned BitWidth = SI.getType()->getScalarSizeInBits();
  const Value *LHS = nullptr, *RHS = nullptr;
  SelectPatternResult R = matchSelectPattern(&SI, LHS, RHS);
  if (R.Flavor == SPF_UNKNOWN)
    return ConstantRange::getFull(BitWidth);

  if (R.Flavor == SelectPatternFlavor::SPF_ABS) {
    // If the negation part of the abs (in RHS) has the NSW flag,
    // then the result of abs(X) is [0..SIGNED_MAX],
    // otherwise it is [0..SIGNED_MIN], as -SIGNED_MIN == SIGNED_MIN.
    if (match(RHS, m_Neg(m_Specific(LHS))) &&
        IIQ.hasNoSignedWrap(cast<Instruction>(RHS)))
      return ConstantRange::getNonEmpty(APInt::getZero(BitWidth),
                                        APInt::getSignedMaxValue(BitWidth) + 1);

    return ConstantRange::getNonEmpty(APInt::getZero(BitWidth),
                                      APInt::getSignedMinValue(BitWidth) + 1);
  }

  if (R.Flavor == SelectPatternFlavor::SPF_NABS) {
    // The result of -abs(X) is <= 0.
    return ConstantRange::getNonEmpty(APInt::getSignedMinValue(BitWidth),
                                      APInt(BitWidth, 1));
  }

  const APInt *C;
  if (!match(LHS, m_APInt(C)) && !match(RHS, m_APInt(C)))
    return ConstantRange::getFull(BitWidth);

  switch (R.Flavor) {
  case SPF_UMIN:
    return ConstantRange::getNonEmpty(APInt::getZero(BitWidth), *C + 1);
  case SPF_UMAX:
    return ConstantRange::getNonEmpty(*C, APInt::getZero(BitWidth));
  case SPF_SMIN:
    return ConstantRange::getNonEmpty(APInt::getSignedMinValue(BitWidth),
                                      *C + 1);
  case SPF_SMAX:
    return ConstantRange::getNonEmpty(*C,
                                      APInt::getSignedMaxValue(BitWidth) + 1);
  default:
    return ConstantRange::getFull(BitWidth);
  }
}

static void setLimitForFPToI(const Instruction *I, APInt &Lower, APInt &Upper) {
  // The maximum representable value of a half is 65504. For floats the maximum
  // value is 3.4e38 which requires roughly 129 bits.
  unsigned BitWidth = I->getType()->getScalarSizeInBits();
  if (!I->getOperand(0)->getType()->getScalarType()->isHalfTy())
    return;
  if (isa<FPToSIInst>(I) && BitWidth >= 17) {
    Lower = APInt(BitWidth, -65504);
    Upper = APInt(BitWidth, 65505);
  }

  if (isa<FPToUIInst>(I) && BitWidth >= 16) {
    // For a fptoui the lower limit is left as 0.
    Upper = APInt(BitWidth, 65505);
  }
}

ConstantRange llvm::computeConstantRange(const Value *V, bool ForSigned,
                                         bool UseInstrInfo, AssumptionCache *AC,
                                         const Instruction *CtxI,
                                         const DominatorTree *DT,
                                         unsigned Depth) {
  assert(V->getType()->isIntOrIntVectorTy() && "Expected integer instruction");

  if (Depth == MaxAnalysisRecursionDepth)
    return ConstantRange::getFull(V->getType()->getScalarSizeInBits());

  if (auto *C = dyn_cast<Constant>(V))
    return C->toConstantRange();

  unsigned BitWidth = V->getType()->getScalarSizeInBits();
  InstrInfoQuery IIQ(UseInstrInfo);
  ConstantRange CR = ConstantRange::getFull(BitWidth);
  if (auto *BO = dyn_cast<BinaryOperator>(V)) {
    APInt Lower = APInt(BitWidth, 0);
    APInt Upper = APInt(BitWidth, 0);
    // TODO: Return ConstantRange.
    setLimitsForBinOp(*BO, Lower, Upper, IIQ, ForSigned);
    CR = ConstantRange::getNonEmpty(Lower, Upper);
  } else if (auto *II = dyn_cast<IntrinsicInst>(V))
    CR = getRangeForIntrinsic(*II);
  else if (auto *SI = dyn_cast<SelectInst>(V)) {
    ConstantRange CRTrue = computeConstantRange(
        SI->getTrueValue(), ForSigned, UseInstrInfo, AC, CtxI, DT, Depth + 1);
    ConstantRange CRFalse = computeConstantRange(
        SI->getFalseValue(), ForSigned, UseInstrInfo, AC, CtxI, DT, Depth + 1);
    CR = CRTrue.unionWith(CRFalse);
    CR = CR.intersectWith(getRangeForSelectPattern(*SI, IIQ));
  } else if (isa<FPToUIInst>(V) || isa<FPToSIInst>(V)) {
    APInt Lower = APInt(BitWidth, 0);
    APInt Upper = APInt(BitWidth, 0);
    // TODO: Return ConstantRange.
    setLimitForFPToI(cast<Instruction>(V), Lower, Upper);
    CR = ConstantRange::getNonEmpty(Lower, Upper);
  } else if (const auto *A = dyn_cast<Argument>(V))
    if (std::optional<ConstantRange> Range = A->getRange())
      CR = *Range;

  if (auto *I = dyn_cast<Instruction>(V)) {
    if (auto *Range = IIQ.getMetadata(I, LLVMContext::MD_range))
      CR = CR.intersectWith(getConstantRangeFromMetadata(*Range));

    if (const auto *CB = dyn_cast<CallBase>(V))
      if (std::optional<ConstantRange> Range = CB->getRange())
        CR = CR.intersectWith(*Range);
  }

  if (CtxI && AC) {
    // Try to restrict the range based on information from assumptions.
    for (auto &AssumeVH : AC->assumptionsFor(V)) {
      if (!AssumeVH)
        continue;
      CallInst *I = cast<CallInst>(AssumeVH);
      assert(I->getParent()->getParent() == CtxI->getParent()->getParent() &&
             "Got assumption for the wrong function!");
      assert(I->getIntrinsicID() == Intrinsic::assume &&
             "must be an assume intrinsic");

      if (!isValidAssumeForContext(I, CtxI, DT))
        continue;
      Value *Arg = I->getArgOperand(0);
      ICmpInst *Cmp = dyn_cast<ICmpInst>(Arg);
      // Currently we just use information from comparisons.
      if (!Cmp || Cmp->getOperand(0) != V)
        continue;
      // TODO: Set "ForSigned" parameter via Cmp->isSigned()?
      ConstantRange RHS =
          computeConstantRange(Cmp->getOperand(1), /* ForSigned */ false,
                               UseInstrInfo, AC, I, DT, Depth + 1);
      CR = CR.intersectWith(
          ConstantRange::makeAllowedICmpRegion(Cmp->getPredicate(), RHS));
    }
  }

  return CR;
}

static void
addValueAffectedByCondition(Value *V,
                            function_ref<void(Value *)> InsertAffected) {
  assert(V != nullptr);
  if (isa<Argument>(V) || isa<GlobalValue>(V)) {
    InsertAffected(V);
  } else if (auto *I = dyn_cast<Instruction>(V)) {
    InsertAffected(V);

    // Peek through unary operators to find the source of the condition.
    Value *Op;
    if (match(I, m_CombineOr(m_PtrToInt(m_Value(Op)), m_Trunc(m_Value(Op))))) {
      if (isa<Instruction>(Op) || isa<Argument>(Op))
        InsertAffected(Op);
    }
  }
}

void llvm::findValuesAffectedByCondition(
    Value *Cond, bool IsAssume, function_ref<void(Value *)> InsertAffected) {
  auto AddAffected = [&InsertAffected](Value *V) {
    addValueAffectedByCondition(V, InsertAffected);
  };

  auto AddCmpOperands = [&AddAffected, IsAssume](Value *LHS, Value *RHS) {
    if (IsAssume) {
      AddAffected(LHS);
      AddAffected(RHS);
    } else if (match(RHS, m_Constant()))
      AddAffected(LHS);
  };

  SmallVector<Value *, 8> Worklist;
  SmallPtrSet<Value *, 8> Visited;
  Worklist.push_back(Cond);
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    CmpInst::Predicate Pred;
    Value *A, *B, *X;

    if (IsAssume) {
      AddAffected(V);
      if (match(V, m_Not(m_Value(X))))
        AddAffected(X);
    }

    if (match(V, m_LogicalOp(m_Value(A), m_Value(B)))) {
      // assume(A && B) is split to -> assume(A); assume(B);
      // assume(!(A || B)) is split to -> assume(!A); assume(!B);
      // Finally, assume(A || B) / assume(!(A && B)) generally don't provide
      // enough information to be worth handling (intersection of information as
      // opposed to union).
      if (!IsAssume) {
        Worklist.push_back(A);
        Worklist.push_back(B);
      }
    } else if (match(V, m_ICmp(Pred, m_Value(A), m_Value(B)))) {
      AddCmpOperands(A, B);

      if (ICmpInst::isEquality(Pred)) {
        if (match(B, m_ConstantInt())) {
          Value *Y;
          // (X & C) or (X | C) or (X ^ C).
          // (X << C) or (X >>_s C) or (X >>_u C).
          if (match(A, m_BitwiseLogic(m_Value(X), m_ConstantInt())) ||
              match(A, m_Shift(m_Value(X), m_ConstantInt())))
            AddAffected(X);
          else if (match(A, m_And(m_Value(X), m_Value(Y))) ||
                   match(A, m_Or(m_Value(X), m_Value(Y)))) {
            AddAffected(X);
            AddAffected(Y);
          }
        }
      } else {
        if (match(B, m_ConstantInt())) {
          // Handle (A + C1) u< C2, which is the canonical form of
          // A > C3 && A < C4.
          if (match(A, m_AddLike(m_Value(X), m_ConstantInt())))
            AddAffected(X);

          if (ICmpInst::isUnsigned(Pred)) {
            Value *Y;
            // X & Y u> C    -> X >u C && Y >u C
            // X | Y u< C    -> X u< C && Y u< C
            // X nuw+ Y u< C -> X u< C && Y u< C
            if (match(A, m_And(m_Value(X), m_Value(Y))) ||
                match(A, m_Or(m_Value(X), m_Value(Y))) ||
                match(A, m_NUWAdd(m_Value(X), m_Value(Y)))) {
              AddAffected(X);
              AddAffected(Y);
            }
            // X nuw- Y u> C -> X u> C
            if (match(A, m_NUWSub(m_Value(X), m_Value())))
              AddAffected(X);
          }
        }

        // Handle icmp slt/sgt (bitcast X to int), 0/-1, which is supported
        // by computeKnownFPClass().
        if (match(A, m_ElementWiseBitCast(m_Value(X)))) {
          if (Pred == ICmpInst::ICMP_SLT && match(B, m_Zero()))
            InsertAffected(X);
          else if (Pred == ICmpInst::ICMP_SGT && match(B, m_AllOnes()))
            InsertAffected(X);
        }
      }
    } else if (match(Cond, m_FCmp(Pred, m_Value(A), m_Value(B)))) {
      AddCmpOperands(A, B);

      // fcmp fneg(x), y
      // fcmp fabs(x), y
      // fcmp fneg(fabs(x)), y
      if (match(A, m_FNeg(m_Value(A))))
        AddAffected(A);
      if (match(A, m_FAbs(m_Value(A))))
        AddAffected(A);

    } else if (match(V, m_Intrinsic<Intrinsic::is_fpclass>(m_Value(A),
                                                           m_Value()))) {
      // Handle patterns that computeKnownFPClass() support.
      AddAffected(A);
    }
  }
}
