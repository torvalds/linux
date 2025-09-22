//===- InstCombineShifts.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitShl, visitLShr, and visitAShr functions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

bool canTryToConstantAddTwoShiftAmounts(Value *Sh0, Value *ShAmt0, Value *Sh1,
                                        Value *ShAmt1) {
  // We have two shift amounts from two different shifts. The types of those
  // shift amounts may not match. If that's the case let's bailout now..
  if (ShAmt0->getType() != ShAmt1->getType())
    return false;

  // As input, we have the following pattern:
  //   Sh0 (Sh1 X, Q), K
  // We want to rewrite that as:
  //   Sh x, (Q+K)  iff (Q+K) u< bitwidth(x)
  // While we know that originally (Q+K) would not overflow
  // (because  2 * (N-1) u<= iN -1), we have looked past extensions of
  // shift amounts. so it may now overflow in smaller bitwidth.
  // To ensure that does not happen, we need to ensure that the total maximal
  // shift amount is still representable in that smaller bit width.
  unsigned MaximalPossibleTotalShiftAmount =
      (Sh0->getType()->getScalarSizeInBits() - 1) +
      (Sh1->getType()->getScalarSizeInBits() - 1);
  APInt MaximalRepresentableShiftAmount =
      APInt::getAllOnes(ShAmt0->getType()->getScalarSizeInBits());
  return MaximalRepresentableShiftAmount.uge(MaximalPossibleTotalShiftAmount);
}

// Given pattern:
//   (x shiftopcode Q) shiftopcode K
// we should rewrite it as
//   x shiftopcode (Q+K)  iff (Q+K) u< bitwidth(x) and
//
// This is valid for any shift, but they must be identical, and we must be
// careful in case we have (zext(Q)+zext(K)) and look past extensions,
// (Q+K) must not overflow or else (Q+K) u< bitwidth(x) is bogus.
//
// AnalyzeForSignBitExtraction indicates that we will only analyze whether this
// pattern has any 2 right-shifts that sum to 1 less than original bit width.
Value *InstCombinerImpl::reassociateShiftAmtsOfTwoSameDirectionShifts(
    BinaryOperator *Sh0, const SimplifyQuery &SQ,
    bool AnalyzeForSignBitExtraction) {
  // Look for a shift of some instruction, ignore zext of shift amount if any.
  Instruction *Sh0Op0;
  Value *ShAmt0;
  if (!match(Sh0,
             m_Shift(m_Instruction(Sh0Op0), m_ZExtOrSelf(m_Value(ShAmt0)))))
    return nullptr;

  // If there is a truncation between the two shifts, we must make note of it
  // and look through it. The truncation imposes additional constraints on the
  // transform.
  Instruction *Sh1;
  Value *Trunc = nullptr;
  match(Sh0Op0,
        m_CombineOr(m_CombineAnd(m_Trunc(m_Instruction(Sh1)), m_Value(Trunc)),
                    m_Instruction(Sh1)));

  // Inner shift: (x shiftopcode ShAmt1)
  // Like with other shift, ignore zext of shift amount if any.
  Value *X, *ShAmt1;
  if (!match(Sh1, m_Shift(m_Value(X), m_ZExtOrSelf(m_Value(ShAmt1)))))
    return nullptr;

  // Verify that it would be safe to try to add those two shift amounts.
  if (!canTryToConstantAddTwoShiftAmounts(Sh0, ShAmt0, Sh1, ShAmt1))
    return nullptr;

  // We are only looking for signbit extraction if we have two right shifts.
  bool HadTwoRightShifts = match(Sh0, m_Shr(m_Value(), m_Value())) &&
                           match(Sh1, m_Shr(m_Value(), m_Value()));
  // ... and if it's not two right-shifts, we know the answer already.
  if (AnalyzeForSignBitExtraction && !HadTwoRightShifts)
    return nullptr;

  // The shift opcodes must be identical, unless we are just checking whether
  // this pattern can be interpreted as a sign-bit-extraction.
  Instruction::BinaryOps ShiftOpcode = Sh0->getOpcode();
  bool IdenticalShOpcodes = Sh0->getOpcode() == Sh1->getOpcode();
  if (!IdenticalShOpcodes && !AnalyzeForSignBitExtraction)
    return nullptr;

  // If we saw truncation, we'll need to produce extra instruction,
  // and for that one of the operands of the shift must be one-use,
  // unless of course we don't actually plan to produce any instructions here.
  if (Trunc && !AnalyzeForSignBitExtraction &&
      !match(Sh0, m_c_BinOp(m_OneUse(m_Value()), m_Value())))
    return nullptr;

  // Can we fold (ShAmt0+ShAmt1) ?
  auto *NewShAmt = dyn_cast_or_null<Constant>(
      simplifyAddInst(ShAmt0, ShAmt1, /*isNSW=*/false, /*isNUW=*/false,
                      SQ.getWithInstruction(Sh0)));
  if (!NewShAmt)
    return nullptr; // Did not simplify.
  unsigned NewShAmtBitWidth = NewShAmt->getType()->getScalarSizeInBits();
  unsigned XBitWidth = X->getType()->getScalarSizeInBits();
  // Is the new shift amount smaller than the bit width of inner/new shift?
  if (!match(NewShAmt, m_SpecificInt_ICMP(ICmpInst::Predicate::ICMP_ULT,
                                          APInt(NewShAmtBitWidth, XBitWidth))))
    return nullptr; // FIXME: could perform constant-folding.

  // If there was a truncation, and we have a right-shift, we can only fold if
  // we are left with the original sign bit. Likewise, if we were just checking
  // that this is a sighbit extraction, this is the place to check it.
  // FIXME: zero shift amount is also legal here, but we can't *easily* check
  // more than one predicate so it's not really worth it.
  if (HadTwoRightShifts && (Trunc || AnalyzeForSignBitExtraction)) {
    // If it's not a sign bit extraction, then we're done.
    if (!match(NewShAmt,
               m_SpecificInt_ICMP(ICmpInst::Predicate::ICMP_EQ,
                                  APInt(NewShAmtBitWidth, XBitWidth - 1))))
      return nullptr;
    // If it is, and that was the question, return the base value.
    if (AnalyzeForSignBitExtraction)
      return X;
  }

  assert(IdenticalShOpcodes && "Should not get here with different shifts.");

  if (NewShAmt->getType() != X->getType()) {
    NewShAmt = ConstantFoldCastOperand(Instruction::ZExt, NewShAmt,
                                       X->getType(), SQ.DL);
    if (!NewShAmt)
      return nullptr;
  }

  // All good, we can do this fold.
  BinaryOperator *NewShift = BinaryOperator::Create(ShiftOpcode, X, NewShAmt);

  // The flags can only be propagated if there wasn't a trunc.
  if (!Trunc) {
    // If the pattern did not involve trunc, and both of the original shifts
    // had the same flag set, preserve the flag.
    if (ShiftOpcode == Instruction::BinaryOps::Shl) {
      NewShift->setHasNoUnsignedWrap(Sh0->hasNoUnsignedWrap() &&
                                     Sh1->hasNoUnsignedWrap());
      NewShift->setHasNoSignedWrap(Sh0->hasNoSignedWrap() &&
                                   Sh1->hasNoSignedWrap());
    } else {
      NewShift->setIsExact(Sh0->isExact() && Sh1->isExact());
    }
  }

  Instruction *Ret = NewShift;
  if (Trunc) {
    Builder.Insert(NewShift);
    Ret = CastInst::Create(Instruction::Trunc, NewShift, Sh0->getType());
  }

  return Ret;
}

// If we have some pattern that leaves only some low bits set, and then performs
// left-shift of those bits, if none of the bits that are left after the final
// shift are modified by the mask, we can omit the mask.
//
// There are many variants to this pattern:
//   a)  (x & ((1 << MaskShAmt) - 1)) << ShiftShAmt
//   b)  (x & (~(-1 << MaskShAmt))) << ShiftShAmt
//   c)  (x & (-1 l>> MaskShAmt)) << ShiftShAmt
//   d)  (x & ((-1 << MaskShAmt) l>> MaskShAmt)) << ShiftShAmt
//   e)  ((x << MaskShAmt) l>> MaskShAmt) << ShiftShAmt
//   f)  ((x << MaskShAmt) a>> MaskShAmt) << ShiftShAmt
// All these patterns can be simplified to just:
//   x << ShiftShAmt
// iff:
//   a,b)     (MaskShAmt+ShiftShAmt) u>= bitwidth(x)
//   c,d,e,f) (ShiftShAmt-MaskShAmt) s>= 0 (i.e. ShiftShAmt u>= MaskShAmt)
static Instruction *
dropRedundantMaskingOfLeftShiftInput(BinaryOperator *OuterShift,
                                     const SimplifyQuery &Q,
                                     InstCombiner::BuilderTy &Builder) {
  assert(OuterShift->getOpcode() == Instruction::BinaryOps::Shl &&
         "The input must be 'shl'!");

  Value *Masked, *ShiftShAmt;
  match(OuterShift,
        m_Shift(m_Value(Masked), m_ZExtOrSelf(m_Value(ShiftShAmt))));

  // *If* there is a truncation between an outer shift and a possibly-mask,
  // then said truncation *must* be one-use, else we can't perform the fold.
  Value *Trunc;
  if (match(Masked, m_CombineAnd(m_Trunc(m_Value(Masked)), m_Value(Trunc))) &&
      !Trunc->hasOneUse())
    return nullptr;

  Type *NarrowestTy = OuterShift->getType();
  Type *WidestTy = Masked->getType();
  bool HadTrunc = WidestTy != NarrowestTy;

  // The mask must be computed in a type twice as wide to ensure
  // that no bits are lost if the sum-of-shifts is wider than the base type.
  Type *ExtendedTy = WidestTy->getExtendedType();

  Value *MaskShAmt;

  // ((1 << MaskShAmt) - 1)
  auto MaskA = m_Add(m_Shl(m_One(), m_Value(MaskShAmt)), m_AllOnes());
  // (~(-1 << maskNbits))
  auto MaskB = m_Not(m_Shl(m_AllOnes(), m_Value(MaskShAmt)));
  // (-1 l>> MaskShAmt)
  auto MaskC = m_LShr(m_AllOnes(), m_Value(MaskShAmt));
  // ((-1 << MaskShAmt) l>> MaskShAmt)
  auto MaskD =
      m_LShr(m_Shl(m_AllOnes(), m_Value(MaskShAmt)), m_Deferred(MaskShAmt));

  Value *X;
  Constant *NewMask;

  if (match(Masked, m_c_And(m_CombineOr(MaskA, MaskB), m_Value(X)))) {
    // Peek through an optional zext of the shift amount.
    match(MaskShAmt, m_ZExtOrSelf(m_Value(MaskShAmt)));

    // Verify that it would be safe to try to add those two shift amounts.
    if (!canTryToConstantAddTwoShiftAmounts(OuterShift, ShiftShAmt, Masked,
                                            MaskShAmt))
      return nullptr;

    // Can we simplify (MaskShAmt+ShiftShAmt) ?
    auto *SumOfShAmts = dyn_cast_or_null<Constant>(simplifyAddInst(
        MaskShAmt, ShiftShAmt, /*IsNSW=*/false, /*IsNUW=*/false, Q));
    if (!SumOfShAmts)
      return nullptr; // Did not simplify.
    // In this pattern SumOfShAmts correlates with the number of low bits
    // that shall remain in the root value (OuterShift).

    // An extend of an undef value becomes zero because the high bits are never
    // completely unknown. Replace the `undef` shift amounts with final
    // shift bitwidth to ensure that the value remains undef when creating the
    // subsequent shift op.
    SumOfShAmts = Constant::replaceUndefsWith(
        SumOfShAmts, ConstantInt::get(SumOfShAmts->getType()->getScalarType(),
                                      ExtendedTy->getScalarSizeInBits()));
    auto *ExtendedSumOfShAmts = ConstantFoldCastOperand(
        Instruction::ZExt, SumOfShAmts, ExtendedTy, Q.DL);
    if (!ExtendedSumOfShAmts)
      return nullptr;

    // And compute the mask as usual: ~(-1 << (SumOfShAmts))
    auto *ExtendedAllOnes = ConstantExpr::getAllOnesValue(ExtendedTy);
    Constant *ExtendedInvertedMask = ConstantFoldBinaryOpOperands(
        Instruction::Shl, ExtendedAllOnes, ExtendedSumOfShAmts, Q.DL);
    if (!ExtendedInvertedMask)
      return nullptr;

    NewMask = ConstantExpr::getNot(ExtendedInvertedMask);
  } else if (match(Masked, m_c_And(m_CombineOr(MaskC, MaskD), m_Value(X))) ||
             match(Masked, m_Shr(m_Shl(m_Value(X), m_Value(MaskShAmt)),
                                 m_Deferred(MaskShAmt)))) {
    // Peek through an optional zext of the shift amount.
    match(MaskShAmt, m_ZExtOrSelf(m_Value(MaskShAmt)));

    // Verify that it would be safe to try to add those two shift amounts.
    if (!canTryToConstantAddTwoShiftAmounts(OuterShift, ShiftShAmt, Masked,
                                            MaskShAmt))
      return nullptr;

    // Can we simplify (ShiftShAmt-MaskShAmt) ?
    auto *ShAmtsDiff = dyn_cast_or_null<Constant>(simplifySubInst(
        ShiftShAmt, MaskShAmt, /*IsNSW=*/false, /*IsNUW=*/false, Q));
    if (!ShAmtsDiff)
      return nullptr; // Did not simplify.
    // In this pattern ShAmtsDiff correlates with the number of high bits that
    // shall be unset in the root value (OuterShift).

    // An extend of an undef value becomes zero because the high bits are never
    // completely unknown. Replace the `undef` shift amounts with negated
    // bitwidth of innermost shift to ensure that the value remains undef when
    // creating the subsequent shift op.
    unsigned WidestTyBitWidth = WidestTy->getScalarSizeInBits();
    ShAmtsDiff = Constant::replaceUndefsWith(
        ShAmtsDiff, ConstantInt::get(ShAmtsDiff->getType()->getScalarType(),
                                     -WidestTyBitWidth));
    auto *ExtendedNumHighBitsToClear = ConstantFoldCastOperand(
        Instruction::ZExt,
        ConstantExpr::getSub(ConstantInt::get(ShAmtsDiff->getType(),
                                              WidestTyBitWidth,
                                              /*isSigned=*/false),
                             ShAmtsDiff),
        ExtendedTy, Q.DL);
    if (!ExtendedNumHighBitsToClear)
      return nullptr;

    // And compute the mask as usual: (-1 l>> (NumHighBitsToClear))
    auto *ExtendedAllOnes = ConstantExpr::getAllOnesValue(ExtendedTy);
    NewMask = ConstantFoldBinaryOpOperands(Instruction::LShr, ExtendedAllOnes,
                                           ExtendedNumHighBitsToClear, Q.DL);
    if (!NewMask)
      return nullptr;
  } else
    return nullptr; // Don't know anything about this pattern.

  NewMask = ConstantExpr::getTrunc(NewMask, NarrowestTy);

  // Does this mask has any unset bits? If not then we can just not apply it.
  bool NeedMask = !match(NewMask, m_AllOnes());

  // If we need to apply a mask, there are several more restrictions we have.
  if (NeedMask) {
    // The old masking instruction must go away.
    if (!Masked->hasOneUse())
      return nullptr;
    // The original "masking" instruction must not have been`ashr`.
    if (match(Masked, m_AShr(m_Value(), m_Value())))
      return nullptr;
  }

  // If we need to apply truncation, let's do it first, since we can.
  // We have already ensured that the old truncation will go away.
  if (HadTrunc)
    X = Builder.CreateTrunc(X, NarrowestTy);

  // No 'NUW'/'NSW'! We no longer know that we won't shift-out non-0 bits.
  // We didn't change the Type of this outermost shift, so we can just do it.
  auto *NewShift = BinaryOperator::Create(OuterShift->getOpcode(), X,
                                          OuterShift->getOperand(1));
  if (!NeedMask)
    return NewShift;

  Builder.Insert(NewShift);
  return BinaryOperator::Create(Instruction::And, NewShift, NewMask);
}

/// If we have a shift-by-constant of a bin op (bitwise logic op or add/sub w/
/// shl) that itself has a shift-by-constant operand with identical opcode, we
/// may be able to convert that into 2 independent shifts followed by the logic
/// op. This eliminates a use of an intermediate value (reduces dependency
/// chain).
static Instruction *foldShiftOfShiftedBinOp(BinaryOperator &I,
                                            InstCombiner::BuilderTy &Builder) {
  assert(I.isShift() && "Expected a shift as input");
  auto *BinInst = dyn_cast<BinaryOperator>(I.getOperand(0));
  if (!BinInst ||
      (!BinInst->isBitwiseLogicOp() &&
       BinInst->getOpcode() != Instruction::Add &&
       BinInst->getOpcode() != Instruction::Sub) ||
      !BinInst->hasOneUse())
    return nullptr;

  Constant *C0, *C1;
  if (!match(I.getOperand(1), m_Constant(C1)))
    return nullptr;

  Instruction::BinaryOps ShiftOpcode = I.getOpcode();
  // Transform for add/sub only works with shl.
  if ((BinInst->getOpcode() == Instruction::Add ||
       BinInst->getOpcode() == Instruction::Sub) &&
      ShiftOpcode != Instruction::Shl)
    return nullptr;

  Type *Ty = I.getType();

  // Find a matching shift by constant. The fold is not valid if the sum
  // of the shift values equals or exceeds bitwidth.
  Value *X, *Y;
  auto matchFirstShift = [&](Value *V, Value *W) {
    unsigned Size = Ty->getScalarSizeInBits();
    APInt Threshold(Size, Size);
    return match(V, m_BinOp(ShiftOpcode, m_Value(X), m_Constant(C0))) &&
           (V->hasOneUse() || match(W, m_ImmConstant())) &&
           match(ConstantExpr::getAdd(C0, C1),
                 m_SpecificInt_ICMP(ICmpInst::ICMP_ULT, Threshold));
  };

  // Logic ops and Add are commutative, so check each operand for a match. Sub
  // is not so we cannot reoder if we match operand(1) and need to keep the
  // operands in their original positions.
  bool FirstShiftIsOp1 = false;
  if (matchFirstShift(BinInst->getOperand(0), BinInst->getOperand(1)))
    Y = BinInst->getOperand(1);
  else if (matchFirstShift(BinInst->getOperand(1), BinInst->getOperand(0))) {
    Y = BinInst->getOperand(0);
    FirstShiftIsOp1 = BinInst->getOpcode() == Instruction::Sub;
  } else
    return nullptr;

  // shift (binop (shift X, C0), Y), C1 -> binop (shift X, C0+C1), (shift Y, C1)
  Constant *ShiftSumC = ConstantExpr::getAdd(C0, C1);
  Value *NewShift1 = Builder.CreateBinOp(ShiftOpcode, X, ShiftSumC);
  Value *NewShift2 = Builder.CreateBinOp(ShiftOpcode, Y, C1);
  Value *Op1 = FirstShiftIsOp1 ? NewShift2 : NewShift1;
  Value *Op2 = FirstShiftIsOp1 ? NewShift1 : NewShift2;
  return BinaryOperator::Create(BinInst->getOpcode(), Op1, Op2);
}

Instruction *InstCombinerImpl::commonShiftTransforms(BinaryOperator &I) {
  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  assert(Op0->getType() == Op1->getType());
  Type *Ty = I.getType();

  // If the shift amount is a one-use `sext`, we can demote it to `zext`.
  Value *Y;
  if (match(Op1, m_OneUse(m_SExt(m_Value(Y))))) {
    Value *NewExt = Builder.CreateZExt(Y, Ty, Op1->getName());
    return BinaryOperator::Create(I.getOpcode(), Op0, NewExt);
  }

  // See if we can fold away this shift.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  // Try to fold constant and into select arguments.
  if (isa<Constant>(Op0))
    if (SelectInst *SI = dyn_cast<SelectInst>(Op1))
      if (Instruction *R = FoldOpIntoSelect(I, SI))
        return R;

  if (Constant *CUI = dyn_cast<Constant>(Op1))
    if (Instruction *Res = FoldShiftByConstant(Op0, CUI, I))
      return Res;

  if (auto *NewShift = cast_or_null<Instruction>(
          reassociateShiftAmtsOfTwoSameDirectionShifts(&I, SQ)))
    return NewShift;

  // Pre-shift a constant shifted by a variable amount with constant offset:
  // C shift (A add nuw C1) --> (C shift C1) shift A
  Value *A;
  Constant *C, *C1;
  if (match(Op0, m_Constant(C)) &&
      match(Op1, m_NUWAddLike(m_Value(A), m_Constant(C1)))) {
    Value *NewC = Builder.CreateBinOp(I.getOpcode(), C, C1);
    BinaryOperator *NewShiftOp = BinaryOperator::Create(I.getOpcode(), NewC, A);
    if (I.getOpcode() == Instruction::Shl) {
      NewShiftOp->setHasNoSignedWrap(I.hasNoSignedWrap());
      NewShiftOp->setHasNoUnsignedWrap(I.hasNoUnsignedWrap());
    } else {
      NewShiftOp->setIsExact(I.isExact());
    }
    return NewShiftOp;
  }

  unsigned BitWidth = Ty->getScalarSizeInBits();

  const APInt *AC, *AddC;
  // Try to pre-shift a constant shifted by a variable amount added with a
  // negative number:
  // C << (X - AddC) --> (C >> AddC) << X
  // and
  // C >> (X - AddC) --> (C << AddC) >> X
  if (match(Op0, m_APInt(AC)) && match(Op1, m_Add(m_Value(A), m_APInt(AddC))) &&
      AddC->isNegative() && (-*AddC).ult(BitWidth)) {
    assert(!AC->isZero() && "Expected simplify of shifted zero");
    unsigned PosOffset = (-*AddC).getZExtValue();

    auto isSuitableForPreShift = [PosOffset, &I, AC]() {
      switch (I.getOpcode()) {
      default:
        return false;
      case Instruction::Shl:
        return (I.hasNoSignedWrap() || I.hasNoUnsignedWrap()) &&
               AC->eq(AC->lshr(PosOffset).shl(PosOffset));
      case Instruction::LShr:
        return I.isExact() && AC->eq(AC->shl(PosOffset).lshr(PosOffset));
      case Instruction::AShr:
        return I.isExact() && AC->eq(AC->shl(PosOffset).ashr(PosOffset));
      }
    };
    if (isSuitableForPreShift()) {
      Constant *NewC = ConstantInt::get(Ty, I.getOpcode() == Instruction::Shl
                                                ? AC->lshr(PosOffset)
                                                : AC->shl(PosOffset));
      BinaryOperator *NewShiftOp =
          BinaryOperator::Create(I.getOpcode(), NewC, A);
      if (I.getOpcode() == Instruction::Shl) {
        NewShiftOp->setHasNoUnsignedWrap(I.hasNoUnsignedWrap());
      } else {
        NewShiftOp->setIsExact();
      }
      return NewShiftOp;
    }
  }

  // X shift (A srem C) -> X shift (A and (C - 1)) iff C is a power of 2.
  // Because shifts by negative values (which could occur if A were negative)
  // are undefined.
  if (Op1->hasOneUse() && match(Op1, m_SRem(m_Value(A), m_Constant(C))) &&
      match(C, m_Power2())) {
    // FIXME: Should this get moved into SimplifyDemandedBits by saying we don't
    // demand the sign bit (and many others) here??
    Constant *Mask = ConstantExpr::getSub(C, ConstantInt::get(Ty, 1));
    Value *Rem = Builder.CreateAnd(A, Mask, Op1->getName());
    return replaceOperand(I, 1, Rem);
  }

  if (Instruction *Logic = foldShiftOfShiftedBinOp(I, Builder))
    return Logic;

  if (match(Op1, m_Or(m_Value(), m_SpecificInt(BitWidth - 1))))
    return replaceOperand(I, 1, ConstantInt::get(Ty, BitWidth - 1));

  return nullptr;
}

/// Return true if we can simplify two logical (either left or right) shifts
/// that have constant shift amounts: OuterShift (InnerShift X, C1), C2.
static bool canEvaluateShiftedShift(unsigned OuterShAmt, bool IsOuterShl,
                                    Instruction *InnerShift,
                                    InstCombinerImpl &IC, Instruction *CxtI) {
  assert(InnerShift->isLogicalShift() && "Unexpected instruction type");

  // We need constant scalar or constant splat shifts.
  const APInt *InnerShiftConst;
  if (!match(InnerShift->getOperand(1), m_APInt(InnerShiftConst)))
    return false;

  // Two logical shifts in the same direction:
  // shl (shl X, C1), C2 -->  shl X, C1 + C2
  // lshr (lshr X, C1), C2 --> lshr X, C1 + C2
  bool IsInnerShl = InnerShift->getOpcode() == Instruction::Shl;
  if (IsInnerShl == IsOuterShl)
    return true;

  // Equal shift amounts in opposite directions become bitwise 'and':
  // lshr (shl X, C), C --> and X, C'
  // shl (lshr X, C), C --> and X, C'
  if (*InnerShiftConst == OuterShAmt)
    return true;

  // If the 2nd shift is bigger than the 1st, we can fold:
  // lshr (shl X, C1), C2 -->  and (shl X, C1 - C2), C3
  // shl (lshr X, C1), C2 --> and (lshr X, C1 - C2), C3
  // but it isn't profitable unless we know the and'd out bits are already zero.
  // Also, check that the inner shift is valid (less than the type width) or
  // we'll crash trying to produce the bit mask for the 'and'.
  unsigned TypeWidth = InnerShift->getType()->getScalarSizeInBits();
  if (InnerShiftConst->ugt(OuterShAmt) && InnerShiftConst->ult(TypeWidth)) {
    unsigned InnerShAmt = InnerShiftConst->getZExtValue();
    unsigned MaskShift =
        IsInnerShl ? TypeWidth - InnerShAmt : InnerShAmt - OuterShAmt;
    APInt Mask = APInt::getLowBitsSet(TypeWidth, OuterShAmt) << MaskShift;
    if (IC.MaskedValueIsZero(InnerShift->getOperand(0), Mask, 0, CxtI))
      return true;
  }

  return false;
}

/// See if we can compute the specified value, but shifted logically to the left
/// or right by some number of bits. This should return true if the expression
/// can be computed for the same cost as the current expression tree. This is
/// used to eliminate extraneous shifting from things like:
///      %C = shl i128 %A, 64
///      %D = shl i128 %B, 96
///      %E = or i128 %C, %D
///      %F = lshr i128 %E, 64
/// where the client will ask if E can be computed shifted right by 64-bits. If
/// this succeeds, getShiftedValue() will be called to produce the value.
static bool canEvaluateShifted(Value *V, unsigned NumBits, bool IsLeftShift,
                               InstCombinerImpl &IC, Instruction *CxtI) {
  // We can always evaluate immediate constants.
  if (match(V, m_ImmConstant()))
    return true;

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return false;

  // We can't mutate something that has multiple uses: doing so would
  // require duplicating the instruction in general, which isn't profitable.
  if (!I->hasOneUse()) return false;

  switch (I->getOpcode()) {
  default: return false;
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    // Bitwise operators can all arbitrarily be arbitrarily evaluated shifted.
    return canEvaluateShifted(I->getOperand(0), NumBits, IsLeftShift, IC, I) &&
           canEvaluateShifted(I->getOperand(1), NumBits, IsLeftShift, IC, I);

  case Instruction::Shl:
  case Instruction::LShr:
    return canEvaluateShiftedShift(NumBits, IsLeftShift, I, IC, CxtI);

  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(I);
    Value *TrueVal = SI->getTrueValue();
    Value *FalseVal = SI->getFalseValue();
    return canEvaluateShifted(TrueVal, NumBits, IsLeftShift, IC, SI) &&
           canEvaluateShifted(FalseVal, NumBits, IsLeftShift, IC, SI);
  }
  case Instruction::PHI: {
    // We can change a phi if we can change all operands.  Note that we never
    // get into trouble with cyclic PHIs here because we only consider
    // instructions with a single use.
    PHINode *PN = cast<PHINode>(I);
    for (Value *IncValue : PN->incoming_values())
      if (!canEvaluateShifted(IncValue, NumBits, IsLeftShift, IC, PN))
        return false;
    return true;
  }
  case Instruction::Mul: {
    const APInt *MulConst;
    // We can fold (shr (mul X, -(1 << C)), C) -> (and (neg X), C`)
    return !IsLeftShift && match(I->getOperand(1), m_APInt(MulConst)) &&
           MulConst->isNegatedPowerOf2() && MulConst->countr_zero() == NumBits;
  }
  }
}

/// Fold OuterShift (InnerShift X, C1), C2.
/// See canEvaluateShiftedShift() for the constraints on these instructions.
static Value *foldShiftedShift(BinaryOperator *InnerShift, unsigned OuterShAmt,
                               bool IsOuterShl,
                               InstCombiner::BuilderTy &Builder) {
  bool IsInnerShl = InnerShift->getOpcode() == Instruction::Shl;
  Type *ShType = InnerShift->getType();
  unsigned TypeWidth = ShType->getScalarSizeInBits();

  // We only accept shifts-by-a-constant in canEvaluateShifted().
  const APInt *C1;
  match(InnerShift->getOperand(1), m_APInt(C1));
  unsigned InnerShAmt = C1->getZExtValue();

  // Change the shift amount and clear the appropriate IR flags.
  auto NewInnerShift = [&](unsigned ShAmt) {
    InnerShift->setOperand(1, ConstantInt::get(ShType, ShAmt));
    if (IsInnerShl) {
      InnerShift->setHasNoUnsignedWrap(false);
      InnerShift->setHasNoSignedWrap(false);
    } else {
      InnerShift->setIsExact(false);
    }
    return InnerShift;
  };

  // Two logical shifts in the same direction:
  // shl (shl X, C1), C2 -->  shl X, C1 + C2
  // lshr (lshr X, C1), C2 --> lshr X, C1 + C2
  if (IsInnerShl == IsOuterShl) {
    // If this is an oversized composite shift, then unsigned shifts get 0.
    if (InnerShAmt + OuterShAmt >= TypeWidth)
      return Constant::getNullValue(ShType);

    return NewInnerShift(InnerShAmt + OuterShAmt);
  }

  // Equal shift amounts in opposite directions become bitwise 'and':
  // lshr (shl X, C), C --> and X, C'
  // shl (lshr X, C), C --> and X, C'
  if (InnerShAmt == OuterShAmt) {
    APInt Mask = IsInnerShl
                     ? APInt::getLowBitsSet(TypeWidth, TypeWidth - OuterShAmt)
                     : APInt::getHighBitsSet(TypeWidth, TypeWidth - OuterShAmt);
    Value *And = Builder.CreateAnd(InnerShift->getOperand(0),
                                   ConstantInt::get(ShType, Mask));
    if (auto *AndI = dyn_cast<Instruction>(And)) {
      AndI->moveBefore(InnerShift);
      AndI->takeName(InnerShift);
    }
    return And;
  }

  assert(InnerShAmt > OuterShAmt &&
         "Unexpected opposite direction logical shift pair");

  // In general, we would need an 'and' for this transform, but
  // canEvaluateShiftedShift() guarantees that the masked-off bits are not used.
  // lshr (shl X, C1), C2 -->  shl X, C1 - C2
  // shl (lshr X, C1), C2 --> lshr X, C1 - C2
  return NewInnerShift(InnerShAmt - OuterShAmt);
}

/// When canEvaluateShifted() returns true for an expression, this function
/// inserts the new computation that produces the shifted value.
static Value *getShiftedValue(Value *V, unsigned NumBits, bool isLeftShift,
                              InstCombinerImpl &IC, const DataLayout &DL) {
  // We can always evaluate constants shifted.
  if (Constant *C = dyn_cast<Constant>(V)) {
    if (isLeftShift)
      return IC.Builder.CreateShl(C, NumBits);
    else
      return IC.Builder.CreateLShr(C, NumBits);
  }

  Instruction *I = cast<Instruction>(V);
  IC.addToWorklist(I);

  switch (I->getOpcode()) {
  default: llvm_unreachable("Inconsistency with CanEvaluateShifted");
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    // Bitwise operators can all arbitrarily be arbitrarily evaluated shifted.
    I->setOperand(
        0, getShiftedValue(I->getOperand(0), NumBits, isLeftShift, IC, DL));
    I->setOperand(
        1, getShiftedValue(I->getOperand(1), NumBits, isLeftShift, IC, DL));
    return I;

  case Instruction::Shl:
  case Instruction::LShr:
    return foldShiftedShift(cast<BinaryOperator>(I), NumBits, isLeftShift,
                            IC.Builder);

  case Instruction::Select:
    I->setOperand(
        1, getShiftedValue(I->getOperand(1), NumBits, isLeftShift, IC, DL));
    I->setOperand(
        2, getShiftedValue(I->getOperand(2), NumBits, isLeftShift, IC, DL));
    return I;
  case Instruction::PHI: {
    // We can change a phi if we can change all operands.  Note that we never
    // get into trouble with cyclic PHIs here because we only consider
    // instructions with a single use.
    PHINode *PN = cast<PHINode>(I);
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
      PN->setIncomingValue(i, getShiftedValue(PN->getIncomingValue(i), NumBits,
                                              isLeftShift, IC, DL));
    return PN;
  }
  case Instruction::Mul: {
    assert(!isLeftShift && "Unexpected shift direction!");
    auto *Neg = BinaryOperator::CreateNeg(I->getOperand(0));
    IC.InsertNewInstWith(Neg, I->getIterator());
    unsigned TypeWidth = I->getType()->getScalarSizeInBits();
    APInt Mask = APInt::getLowBitsSet(TypeWidth, TypeWidth - NumBits);
    auto *And = BinaryOperator::CreateAnd(Neg,
                                          ConstantInt::get(I->getType(), Mask));
    And->takeName(I);
    return IC.InsertNewInstWith(And, I->getIterator());
  }
  }
}

// If this is a bitwise operator or add with a constant RHS we might be able
// to pull it through a shift.
static bool canShiftBinOpWithConstantRHS(BinaryOperator &Shift,
                                         BinaryOperator *BO) {
  switch (BO->getOpcode()) {
  default:
    return false; // Do not perform transform!
  case Instruction::Add:
    return Shift.getOpcode() == Instruction::Shl;
  case Instruction::Or:
  case Instruction::And:
    return true;
  case Instruction::Xor:
    // Do not change a 'not' of logical shift because that would create a normal
    // 'xor'. The 'not' is likely better for analysis, SCEV, and codegen.
    return !(Shift.isLogicalShift() && match(BO, m_Not(m_Value())));
  }
}

Instruction *InstCombinerImpl::FoldShiftByConstant(Value *Op0, Constant *C1,
                                                   BinaryOperator &I) {
  // (C2 << X) << C1 --> (C2 << C1) << X
  // (C2 >> X) >> C1 --> (C2 >> C1) >> X
  Constant *C2;
  Value *X;
  bool IsLeftShift = I.getOpcode() == Instruction::Shl;
  if (match(Op0, m_BinOp(I.getOpcode(), m_ImmConstant(C2), m_Value(X)))) {
    Instruction *R = BinaryOperator::Create(
        I.getOpcode(), Builder.CreateBinOp(I.getOpcode(), C2, C1), X);
    BinaryOperator *BO0 = cast<BinaryOperator>(Op0);
    if (IsLeftShift) {
      R->setHasNoUnsignedWrap(I.hasNoUnsignedWrap() &&
                              BO0->hasNoUnsignedWrap());
      R->setHasNoSignedWrap(I.hasNoSignedWrap() && BO0->hasNoSignedWrap());
    } else
      R->setIsExact(I.isExact() && BO0->isExact());
    return R;
  }

  Type *Ty = I.getType();
  unsigned TypeBits = Ty->getScalarSizeInBits();

  // (X / +DivC) >> (Width - 1) --> ext (X <= -DivC)
  // (X / -DivC) >> (Width - 1) --> ext (X >= +DivC)
  const APInt *DivC;
  if (!IsLeftShift && match(C1, m_SpecificIntAllowPoison(TypeBits - 1)) &&
      match(Op0, m_SDiv(m_Value(X), m_APInt(DivC))) && !DivC->isZero() &&
      !DivC->isMinSignedValue()) {
    Constant *NegDivC = ConstantInt::get(Ty, -(*DivC));
    ICmpInst::Predicate Pred =
        DivC->isNegative() ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_SLE;
    Value *Cmp = Builder.CreateICmp(Pred, X, NegDivC);
    auto ExtOpcode = (I.getOpcode() == Instruction::AShr) ? Instruction::SExt
                                                          : Instruction::ZExt;
    return CastInst::Create(ExtOpcode, Cmp, Ty);
  }

  const APInt *Op1C;
  if (!match(C1, m_APInt(Op1C)))
    return nullptr;

  assert(!Op1C->uge(TypeBits) &&
         "Shift over the type width should have been removed already");

  // See if we can propagate this shift into the input, this covers the trivial
  // cast of lshr(shl(x,c1),c2) as well as other more complex cases.
  if (I.getOpcode() != Instruction::AShr &&
      canEvaluateShifted(Op0, Op1C->getZExtValue(), IsLeftShift, *this, &I)) {
    LLVM_DEBUG(
        dbgs() << "ICE: GetShiftedValue propagating shift through expression"
                  " to eliminate shift:\n  IN: "
               << *Op0 << "\n  SH: " << I << "\n");

    return replaceInstUsesWith(
        I, getShiftedValue(Op0, Op1C->getZExtValue(), IsLeftShift, *this, DL));
  }

  if (Instruction *FoldedShift = foldBinOpIntoSelectOrPhi(I))
    return FoldedShift;

  if (!Op0->hasOneUse())
    return nullptr;

  if (auto *Op0BO = dyn_cast<BinaryOperator>(Op0)) {
    // If the operand is a bitwise operator with a constant RHS, and the
    // shift is the only use, we can pull it out of the shift.
    const APInt *Op0C;
    if (match(Op0BO->getOperand(1), m_APInt(Op0C))) {
      if (canShiftBinOpWithConstantRHS(I, Op0BO)) {
        Value *NewRHS =
            Builder.CreateBinOp(I.getOpcode(), Op0BO->getOperand(1), C1);

        Value *NewShift =
            Builder.CreateBinOp(I.getOpcode(), Op0BO->getOperand(0), C1);
        NewShift->takeName(Op0BO);

        return BinaryOperator::Create(Op0BO->getOpcode(), NewShift, NewRHS);
      }
    }
  }

  // If we have a select that conditionally executes some binary operator,
  // see if we can pull it the select and operator through the shift.
  //
  // For example, turning:
  //   shl (select C, (add X, C1), X), C2
  // Into:
  //   Y = shl X, C2
  //   select C, (add Y, C1 << C2), Y
  Value *Cond;
  BinaryOperator *TBO;
  Value *FalseVal;
  if (match(Op0, m_Select(m_Value(Cond), m_OneUse(m_BinOp(TBO)),
                          m_Value(FalseVal)))) {
    const APInt *C;
    if (!isa<Constant>(FalseVal) && TBO->getOperand(0) == FalseVal &&
        match(TBO->getOperand(1), m_APInt(C)) &&
        canShiftBinOpWithConstantRHS(I, TBO)) {
      Value *NewRHS =
          Builder.CreateBinOp(I.getOpcode(), TBO->getOperand(1), C1);

      Value *NewShift = Builder.CreateBinOp(I.getOpcode(), FalseVal, C1);
      Value *NewOp = Builder.CreateBinOp(TBO->getOpcode(), NewShift, NewRHS);
      return SelectInst::Create(Cond, NewOp, NewShift);
    }
  }

  BinaryOperator *FBO;
  Value *TrueVal;
  if (match(Op0, m_Select(m_Value(Cond), m_Value(TrueVal),
                          m_OneUse(m_BinOp(FBO))))) {
    const APInt *C;
    if (!isa<Constant>(TrueVal) && FBO->getOperand(0) == TrueVal &&
        match(FBO->getOperand(1), m_APInt(C)) &&
        canShiftBinOpWithConstantRHS(I, FBO)) {
      Value *NewRHS =
          Builder.CreateBinOp(I.getOpcode(), FBO->getOperand(1), C1);

      Value *NewShift = Builder.CreateBinOp(I.getOpcode(), TrueVal, C1);
      Value *NewOp = Builder.CreateBinOp(FBO->getOpcode(), NewShift, NewRHS);
      return SelectInst::Create(Cond, NewShift, NewOp);
    }
  }

  return nullptr;
}

// Tries to perform
//    (lshr (add (zext X), (zext Y)), K)
//      -> (icmp ult (add X, Y), X)
//    where
//      - The add's operands are zexts from a K-bits integer to a bigger type.
//      - The add is only used by the shr, or by iK (or narrower) truncates.
//      - The lshr type has more than 2 bits (other types are boolean math).
//      - K > 1
//    note that
//      - The resulting add cannot have nuw/nsw, else on overflow we get a
//        poison value and the transform isn't legal anymore.
Instruction *InstCombinerImpl::foldLShrOverflowBit(BinaryOperator &I) {
  assert(I.getOpcode() == Instruction::LShr);

  Value *Add = I.getOperand(0);
  Value *ShiftAmt = I.getOperand(1);
  Type *Ty = I.getType();

  if (Ty->getScalarSizeInBits() < 3)
    return nullptr;

  const APInt *ShAmtAPInt = nullptr;
  Value *X = nullptr, *Y = nullptr;
  if (!match(ShiftAmt, m_APInt(ShAmtAPInt)) ||
      !match(Add,
             m_Add(m_OneUse(m_ZExt(m_Value(X))), m_OneUse(m_ZExt(m_Value(Y))))))
    return nullptr;

  const unsigned ShAmt = ShAmtAPInt->getZExtValue();
  if (ShAmt == 1)
    return nullptr;

  // X/Y are zexts from `ShAmt`-sized ints.
  if (X->getType()->getScalarSizeInBits() != ShAmt ||
      Y->getType()->getScalarSizeInBits() != ShAmt)
    return nullptr;

  // Make sure that `Add` is only used by `I` and `ShAmt`-truncates.
  if (!Add->hasOneUse()) {
    for (User *U : Add->users()) {
      if (U == &I)
        continue;

      TruncInst *Trunc = dyn_cast<TruncInst>(U);
      if (!Trunc || Trunc->getType()->getScalarSizeInBits() > ShAmt)
        return nullptr;
    }
  }

  // Insert at Add so that the newly created `NarrowAdd` will dominate it's
  // users (i.e. `Add`'s users).
  Instruction *AddInst = cast<Instruction>(Add);
  Builder.SetInsertPoint(AddInst);

  Value *NarrowAdd = Builder.CreateAdd(X, Y, "add.narrowed");
  Value *Overflow =
      Builder.CreateICmpULT(NarrowAdd, X, "add.narrowed.overflow");

  // Replace the uses of the original add with a zext of the
  // NarrowAdd's result. Note that all users at this stage are known to
  // be ShAmt-sized truncs, or the lshr itself.
  if (!Add->hasOneUse()) {
    replaceInstUsesWith(*AddInst, Builder.CreateZExt(NarrowAdd, Ty));
    eraseInstFromFunction(*AddInst);
  }

  // Replace the LShr with a zext of the overflow check.
  return new ZExtInst(Overflow, Ty);
}

// Try to set nuw/nsw flags on shl or exact flag on lshr/ashr using knownbits.
static bool setShiftFlags(BinaryOperator &I, const SimplifyQuery &Q) {
  assert(I.isShift() && "Expected a shift as input");
  // We already have all the flags.
  if (I.getOpcode() == Instruction::Shl) {
    if (I.hasNoUnsignedWrap() && I.hasNoSignedWrap())
      return false;
  } else {
    if (I.isExact())
      return false;

    // shr (shl X, Y), Y
    if (match(I.getOperand(0), m_Shl(m_Value(), m_Specific(I.getOperand(1))))) {
      I.setIsExact();
      return true;
    }
  }

  // Compute what we know about shift count.
  KnownBits KnownCnt = computeKnownBits(I.getOperand(1), /* Depth */ 0, Q);
  unsigned BitWidth = KnownCnt.getBitWidth();
  // Since shift produces a poison value if RHS is equal to or larger than the
  // bit width, we can safely assume that RHS is less than the bit width.
  uint64_t MaxCnt = KnownCnt.getMaxValue().getLimitedValue(BitWidth - 1);

  KnownBits KnownAmt = computeKnownBits(I.getOperand(0), /* Depth */ 0, Q);
  bool Changed = false;

  if (I.getOpcode() == Instruction::Shl) {
    // If we have as many leading zeros than maximum shift cnt we have nuw.
    if (!I.hasNoUnsignedWrap() && MaxCnt <= KnownAmt.countMinLeadingZeros()) {
      I.setHasNoUnsignedWrap();
      Changed = true;
    }
    // If we have more sign bits than maximum shift cnt we have nsw.
    if (!I.hasNoSignedWrap()) {
      if (MaxCnt < KnownAmt.countMinSignBits() ||
          MaxCnt < ComputeNumSignBits(I.getOperand(0), Q.DL, /*Depth*/ 0, Q.AC,
                                      Q.CxtI, Q.DT)) {
        I.setHasNoSignedWrap();
        Changed = true;
      }
    }
    return Changed;
  }

  // If we have at least as many trailing zeros as maximum count then we have
  // exact.
  Changed = MaxCnt <= KnownAmt.countMinTrailingZeros();
  I.setIsExact(Changed);

  return Changed;
}

Instruction *InstCombinerImpl::visitShl(BinaryOperator &I) {
  const SimplifyQuery Q = SQ.getWithInstruction(&I);

  if (Value *V = simplifyShlInst(I.getOperand(0), I.getOperand(1),
                                 I.hasNoSignedWrap(), I.hasNoUnsignedWrap(), Q))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *V = commonShiftTransforms(I))
    return V;

  if (Instruction *V = dropRedundantMaskingOfLeftShiftInput(&I, Q, Builder))
    return V;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();

  const APInt *C;
  if (match(Op1, m_APInt(C))) {
    unsigned ShAmtC = C->getZExtValue();

    // shl (zext X), C --> zext (shl X, C)
    // This is only valid if X would have zeros shifted out.
    Value *X;
    if (match(Op0, m_OneUse(m_ZExt(m_Value(X))))) {
      unsigned SrcWidth = X->getType()->getScalarSizeInBits();
      if (ShAmtC < SrcWidth &&
          MaskedValueIsZero(X, APInt::getHighBitsSet(SrcWidth, ShAmtC), 0, &I))
        return new ZExtInst(Builder.CreateShl(X, ShAmtC), Ty);
    }

    // (X >> C) << C --> X & (-1 << C)
    if (match(Op0, m_Shr(m_Value(X), m_Specific(Op1)))) {
      APInt Mask(APInt::getHighBitsSet(BitWidth, BitWidth - ShAmtC));
      return BinaryOperator::CreateAnd(X, ConstantInt::get(Ty, Mask));
    }

    const APInt *C1;
    if (match(Op0, m_Exact(m_Shr(m_Value(X), m_APInt(C1)))) &&
        C1->ult(BitWidth)) {
      unsigned ShrAmt = C1->getZExtValue();
      if (ShrAmt < ShAmtC) {
        // If C1 < C: (X >>?,exact C1) << C --> X << (C - C1)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShAmtC - ShrAmt);
        auto *NewShl = BinaryOperator::CreateShl(X, ShiftDiff);
        NewShl->setHasNoUnsignedWrap(
            I.hasNoUnsignedWrap() ||
            (ShrAmt &&
             cast<Instruction>(Op0)->getOpcode() == Instruction::LShr &&
             I.hasNoSignedWrap()));
        NewShl->setHasNoSignedWrap(I.hasNoSignedWrap());
        return NewShl;
      }
      if (ShrAmt > ShAmtC) {
        // If C1 > C: (X >>?exact C1) << C --> X >>?exact (C1 - C)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShrAmt - ShAmtC);
        auto *NewShr = BinaryOperator::Create(
            cast<BinaryOperator>(Op0)->getOpcode(), X, ShiftDiff);
        NewShr->setIsExact(true);
        return NewShr;
      }
    }

    if (match(Op0, m_OneUse(m_Shr(m_Value(X), m_APInt(C1)))) &&
        C1->ult(BitWidth)) {
      unsigned ShrAmt = C1->getZExtValue();
      if (ShrAmt < ShAmtC) {
        // If C1 < C: (X >>? C1) << C --> (X << (C - C1)) & (-1 << C)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShAmtC - ShrAmt);
        auto *NewShl = BinaryOperator::CreateShl(X, ShiftDiff);
        NewShl->setHasNoUnsignedWrap(
            I.hasNoUnsignedWrap() ||
            (ShrAmt &&
             cast<Instruction>(Op0)->getOpcode() == Instruction::LShr &&
             I.hasNoSignedWrap()));
        NewShl->setHasNoSignedWrap(I.hasNoSignedWrap());
        Builder.Insert(NewShl);
        APInt Mask(APInt::getHighBitsSet(BitWidth, BitWidth - ShAmtC));
        return BinaryOperator::CreateAnd(NewShl, ConstantInt::get(Ty, Mask));
      }
      if (ShrAmt > ShAmtC) {
        // If C1 > C: (X >>? C1) << C --> (X >>? (C1 - C)) & (-1 << C)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShrAmt - ShAmtC);
        auto *OldShr = cast<BinaryOperator>(Op0);
        auto *NewShr =
            BinaryOperator::Create(OldShr->getOpcode(), X, ShiftDiff);
        NewShr->setIsExact(OldShr->isExact());
        Builder.Insert(NewShr);
        APInt Mask(APInt::getHighBitsSet(BitWidth, BitWidth - ShAmtC));
        return BinaryOperator::CreateAnd(NewShr, ConstantInt::get(Ty, Mask));
      }
    }

    // Similar to above, but look through an intermediate trunc instruction.
    BinaryOperator *Shr;
    if (match(Op0, m_OneUse(m_Trunc(m_OneUse(m_BinOp(Shr))))) &&
        match(Shr, m_Shr(m_Value(X), m_APInt(C1)))) {
      // The larger shift direction survives through the transform.
      unsigned ShrAmtC = C1->getZExtValue();
      unsigned ShDiff = ShrAmtC > ShAmtC ? ShrAmtC - ShAmtC : ShAmtC - ShrAmtC;
      Constant *ShiftDiffC = ConstantInt::get(X->getType(), ShDiff);
      auto ShiftOpc = ShrAmtC > ShAmtC ? Shr->getOpcode() : Instruction::Shl;

      // If C1 > C:
      // (trunc (X >> C1)) << C --> (trunc (X >> (C1 - C))) && (-1 << C)
      // If C > C1:
      // (trunc (X >> C1)) << C --> (trunc (X << (C - C1))) && (-1 << C)
      Value *NewShift = Builder.CreateBinOp(ShiftOpc, X, ShiftDiffC, "sh.diff");
      Value *Trunc = Builder.CreateTrunc(NewShift, Ty, "tr.sh.diff");
      APInt Mask(APInt::getHighBitsSet(BitWidth, BitWidth - ShAmtC));
      return BinaryOperator::CreateAnd(Trunc, ConstantInt::get(Ty, Mask));
    }

    // If we have an opposite shift by the same amount, we may be able to
    // reorder binops and shifts to eliminate math/logic.
    auto isSuitableBinOpcode = [](Instruction::BinaryOps BinOpcode) {
      switch (BinOpcode) {
      default:
        return false;
      case Instruction::Add:
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor:
      case Instruction::Sub:
        // NOTE: Sub is not commutable and the tranforms below may not be valid
        //       when the shift-right is operand 1 (RHS) of the sub.
        return true;
      }
    };
    BinaryOperator *Op0BO;
    if (match(Op0, m_OneUse(m_BinOp(Op0BO))) &&
        isSuitableBinOpcode(Op0BO->getOpcode())) {
      // Commute so shift-right is on LHS of the binop.
      // (Y bop (X >> C)) << C         ->  ((X >> C) bop Y) << C
      // (Y bop ((X >> C) & CC)) << C  ->  (((X >> C) & CC) bop Y) << C
      Value *Shr = Op0BO->getOperand(0);
      Value *Y = Op0BO->getOperand(1);
      Value *X;
      const APInt *CC;
      if (Op0BO->isCommutative() && Y->hasOneUse() &&
          (match(Y, m_Shr(m_Value(), m_Specific(Op1))) ||
           match(Y, m_And(m_OneUse(m_Shr(m_Value(), m_Specific(Op1))),
                          m_APInt(CC)))))
        std::swap(Shr, Y);

      // ((X >> C) bop Y) << C  ->  (X bop (Y << C)) & (~0 << C)
      if (match(Shr, m_OneUse(m_Shr(m_Value(X), m_Specific(Op1))))) {
        // Y << C
        Value *YS = Builder.CreateShl(Y, Op1, Op0BO->getName());
        // (X bop (Y << C))
        Value *B =
            Builder.CreateBinOp(Op0BO->getOpcode(), X, YS, Shr->getName());
        unsigned Op1Val = C->getLimitedValue(BitWidth);
        APInt Bits = APInt::getHighBitsSet(BitWidth, BitWidth - Op1Val);
        Constant *Mask = ConstantInt::get(Ty, Bits);
        return BinaryOperator::CreateAnd(B, Mask);
      }

      // (((X >> C) & CC) bop Y) << C  ->  (X & (CC << C)) bop (Y << C)
      if (match(Shr,
                m_OneUse(m_And(m_OneUse(m_Shr(m_Value(X), m_Specific(Op1))),
                               m_APInt(CC))))) {
        // Y << C
        Value *YS = Builder.CreateShl(Y, Op1, Op0BO->getName());
        // X & (CC << C)
        Value *M = Builder.CreateAnd(X, ConstantInt::get(Ty, CC->shl(*C)),
                                     X->getName() + ".mask");
        auto *NewOp = BinaryOperator::Create(Op0BO->getOpcode(), M, YS);
        if (auto *Disjoint = dyn_cast<PossiblyDisjointInst>(Op0BO);
            Disjoint && Disjoint->isDisjoint())
          cast<PossiblyDisjointInst>(NewOp)->setIsDisjoint(true);
        return NewOp;
      }
    }

    // (C1 - X) << C --> (C1 << C) - (X << C)
    if (match(Op0, m_OneUse(m_Sub(m_APInt(C1), m_Value(X))))) {
      Constant *NewLHS = ConstantInt::get(Ty, C1->shl(*C));
      Value *NewShift = Builder.CreateShl(X, Op1);
      return BinaryOperator::CreateSub(NewLHS, NewShift);
    }
  }

  if (setShiftFlags(I, Q))
    return &I;

  // Transform  (x >> y) << y  to  x & (-1 << y)
  // Valid for any type of right-shift.
  Value *X;
  if (match(Op0, m_OneUse(m_Shr(m_Value(X), m_Specific(Op1))))) {
    Constant *AllOnes = ConstantInt::getAllOnesValue(Ty);
    Value *Mask = Builder.CreateShl(AllOnes, Op1);
    return BinaryOperator::CreateAnd(Mask, X);
  }

  // Transform  (-1 >> y) << y  to -1 << y
  if (match(Op0, m_LShr(m_AllOnes(), m_Specific(Op1)))) {
    Constant *AllOnes = ConstantInt::getAllOnesValue(Ty);
    return BinaryOperator::CreateShl(AllOnes, Op1);
  }

  Constant *C1;
  if (match(Op1, m_ImmConstant(C1))) {
    Constant *C2;
    Value *X;
    // (X * C2) << C1 --> X * (C2 << C1)
    if (match(Op0, m_Mul(m_Value(X), m_ImmConstant(C2))))
      return BinaryOperator::CreateMul(X, Builder.CreateShl(C2, C1));

    // shl (zext i1 X), C1 --> select (X, 1 << C1, 0)
    if (match(Op0, m_ZExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1)) {
      auto *NewC = Builder.CreateShl(ConstantInt::get(Ty, 1), C1);
      return SelectInst::Create(X, NewC, ConstantInt::getNullValue(Ty));
    }
  }

  if (match(Op0, m_One())) {
    // (1 << (C - x)) -> ((1 << C) >> x) if C is bitwidth - 1
    if (match(Op1, m_Sub(m_SpecificInt(BitWidth - 1), m_Value(X))))
      return BinaryOperator::CreateLShr(
          ConstantInt::get(Ty, APInt::getSignMask(BitWidth)), X);

    // Canonicalize "extract lowest set bit" using cttz to and-with-negate:
    // 1 << (cttz X) --> -X & X
    if (match(Op1,
              m_OneUse(m_Intrinsic<Intrinsic::cttz>(m_Value(X), m_Value())))) {
      Value *NegX = Builder.CreateNeg(X, "neg");
      return BinaryOperator::CreateAnd(NegX, X);
    }
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitLShr(BinaryOperator &I) {
  if (Value *V = simplifyLShrInst(I.getOperand(0), I.getOperand(1), I.isExact(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *R = commonShiftTransforms(I))
    return R;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  Value *X;
  const APInt *C;
  unsigned BitWidth = Ty->getScalarSizeInBits();

  // (iN (~X) u>> (N - 1)) --> zext (X > -1)
  if (match(Op0, m_OneUse(m_Not(m_Value(X)))) &&
      match(Op1, m_SpecificIntAllowPoison(BitWidth - 1)))
    return new ZExtInst(Builder.CreateIsNotNeg(X, "isnotneg"), Ty);

  // ((X << nuw Z) sub nuw Y) >>u exact Z --> X sub nuw (Y >>u exact Z)
  Value *Y;
  if (I.isExact() &&
      match(Op0, m_OneUse(m_NUWSub(m_NUWShl(m_Value(X), m_Specific(Op1)),
                                   m_Value(Y))))) {
    Value *NewLshr = Builder.CreateLShr(Y, Op1, "", /*isExact=*/true);
    auto *NewSub = BinaryOperator::CreateNUWSub(X, NewLshr);
    NewSub->setHasNoSignedWrap(
        cast<OverflowingBinaryOperator>(Op0)->hasNoSignedWrap());
    return NewSub;
  }

  // Fold (X + Y) / 2 --> (X & Y) iff (X u<= 1) && (Y u<= 1)
  if (match(Op0, m_Add(m_Value(X), m_Value(Y))) && match(Op1, m_One()) &&
      computeKnownBits(X, /*Depth=*/0, &I).countMaxActiveBits() <= 1 &&
      computeKnownBits(Y, /*Depth=*/0, &I).countMaxActiveBits() <= 1)
    return BinaryOperator::CreateAnd(X, Y);

  // (sub nuw X, (Y << nuw Z)) >>u exact Z --> (X >>u exact Z) sub nuw Y
  if (I.isExact() &&
      match(Op0, m_OneUse(m_NUWSub(m_Value(X),
                                   m_NUWShl(m_Value(Y), m_Specific(Op1)))))) {
    Value *NewLshr = Builder.CreateLShr(X, Op1, "", /*isExact=*/true);
    auto *NewSub = BinaryOperator::CreateNUWSub(NewLshr, Y);
    NewSub->setHasNoSignedWrap(
        cast<OverflowingBinaryOperator>(Op0)->hasNoSignedWrap());
    return NewSub;
  }

  auto isSuitableBinOpcode = [](Instruction::BinaryOps BinOpcode) {
    switch (BinOpcode) {
    default:
      return false;
    case Instruction::Add:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
      // Sub is handled separately.
      return true;
    }
  };

  // If both the binop and the shift are nuw, then:
  // ((X << nuw Z) binop nuw Y) >>u Z --> X binop nuw (Y >>u Z)
  if (match(Op0, m_OneUse(m_c_BinOp(m_NUWShl(m_Value(X), m_Specific(Op1)),
                                    m_Value(Y))))) {
    BinaryOperator *Op0OB = cast<BinaryOperator>(Op0);
    if (isSuitableBinOpcode(Op0OB->getOpcode())) {
      if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(Op0);
          !OBO || OBO->hasNoUnsignedWrap()) {
        Value *NewLshr = Builder.CreateLShr(
            Y, Op1, "", I.isExact() && Op0OB->getOpcode() != Instruction::And);
        auto *NewBinOp = BinaryOperator::Create(Op0OB->getOpcode(), NewLshr, X);
        if (OBO) {
          NewBinOp->setHasNoUnsignedWrap(true);
          NewBinOp->setHasNoSignedWrap(OBO->hasNoSignedWrap());
        } else if (auto *Disjoint = dyn_cast<PossiblyDisjointInst>(Op0)) {
          cast<PossiblyDisjointInst>(NewBinOp)->setIsDisjoint(
              Disjoint->isDisjoint());
        }
        return NewBinOp;
      }
    }
  }

  if (match(Op1, m_APInt(C))) {
    unsigned ShAmtC = C->getZExtValue();
    auto *II = dyn_cast<IntrinsicInst>(Op0);
    if (II && isPowerOf2_32(BitWidth) && Log2_32(BitWidth) == ShAmtC &&
        (II->getIntrinsicID() == Intrinsic::ctlz ||
         II->getIntrinsicID() == Intrinsic::cttz ||
         II->getIntrinsicID() == Intrinsic::ctpop)) {
      // ctlz.i32(x)>>5  --> zext(x == 0)
      // cttz.i32(x)>>5  --> zext(x == 0)
      // ctpop.i32(x)>>5 --> zext(x == -1)
      bool IsPop = II->getIntrinsicID() == Intrinsic::ctpop;
      Constant *RHS = ConstantInt::getSigned(Ty, IsPop ? -1 : 0);
      Value *Cmp = Builder.CreateICmpEQ(II->getArgOperand(0), RHS);
      return new ZExtInst(Cmp, Ty);
    }

    const APInt *C1;
    if (match(Op0, m_Shl(m_Value(X), m_APInt(C1))) && C1->ult(BitWidth)) {
      if (C1->ult(ShAmtC)) {
        unsigned ShlAmtC = C1->getZExtValue();
        Constant *ShiftDiff = ConstantInt::get(Ty, ShAmtC - ShlAmtC);
        if (cast<BinaryOperator>(Op0)->hasNoUnsignedWrap()) {
          // (X <<nuw C1) >>u C --> X >>u (C - C1)
          auto *NewLShr = BinaryOperator::CreateLShr(X, ShiftDiff);
          NewLShr->setIsExact(I.isExact());
          return NewLShr;
        }
        if (Op0->hasOneUse()) {
          // (X << C1) >>u C  --> (X >>u (C - C1)) & (-1 >> C)
          Value *NewLShr = Builder.CreateLShr(X, ShiftDiff, "", I.isExact());
          APInt Mask(APInt::getLowBitsSet(BitWidth, BitWidth - ShAmtC));
          return BinaryOperator::CreateAnd(NewLShr, ConstantInt::get(Ty, Mask));
        }
      } else if (C1->ugt(ShAmtC)) {
        unsigned ShlAmtC = C1->getZExtValue();
        Constant *ShiftDiff = ConstantInt::get(Ty, ShlAmtC - ShAmtC);
        if (cast<BinaryOperator>(Op0)->hasNoUnsignedWrap()) {
          // (X <<nuw C1) >>u C --> X <<nuw/nsw (C1 - C)
          auto *NewShl = BinaryOperator::CreateShl(X, ShiftDiff);
          NewShl->setHasNoUnsignedWrap(true);
          NewShl->setHasNoSignedWrap(ShAmtC > 0);
          return NewShl;
        }
        if (Op0->hasOneUse()) {
          // (X << C1) >>u C  --> X << (C1 - C) & (-1 >> C)
          Value *NewShl = Builder.CreateShl(X, ShiftDiff);
          APInt Mask(APInt::getLowBitsSet(BitWidth, BitWidth - ShAmtC));
          return BinaryOperator::CreateAnd(NewShl, ConstantInt::get(Ty, Mask));
        }
      } else {
        assert(*C1 == ShAmtC);
        // (X << C) >>u C --> X & (-1 >>u C)
        APInt Mask(APInt::getLowBitsSet(BitWidth, BitWidth - ShAmtC));
        return BinaryOperator::CreateAnd(X, ConstantInt::get(Ty, Mask));
      }
    }

    // ((X << C) + Y) >>u C --> (X + (Y >>u C)) & (-1 >>u C)
    // TODO: Consolidate with the more general transform that starts from shl
    //       (the shifts are in the opposite order).
    if (match(Op0,
              m_OneUse(m_c_Add(m_OneUse(m_Shl(m_Value(X), m_Specific(Op1))),
                               m_Value(Y))))) {
      Value *NewLshr = Builder.CreateLShr(Y, Op1);
      Value *NewAdd = Builder.CreateAdd(NewLshr, X);
      unsigned Op1Val = C->getLimitedValue(BitWidth);
      APInt Bits = APInt::getLowBitsSet(BitWidth, BitWidth - Op1Val);
      Constant *Mask = ConstantInt::get(Ty, Bits);
      return BinaryOperator::CreateAnd(NewAdd, Mask);
    }

    if (match(Op0, m_OneUse(m_ZExt(m_Value(X)))) &&
        (!Ty->isIntegerTy() || shouldChangeType(Ty, X->getType()))) {
      assert(ShAmtC < X->getType()->getScalarSizeInBits() &&
             "Big shift not simplified to zero?");
      // lshr (zext iM X to iN), C --> zext (lshr X, C) to iN
      Value *NewLShr = Builder.CreateLShr(X, ShAmtC);
      return new ZExtInst(NewLShr, Ty);
    }

    if (match(Op0, m_SExt(m_Value(X)))) {
      unsigned SrcTyBitWidth = X->getType()->getScalarSizeInBits();
      // lshr (sext i1 X to iN), C --> select (X, -1 >> C, 0)
      if (SrcTyBitWidth == 1) {
        auto *NewC = ConstantInt::get(
            Ty, APInt::getLowBitsSet(BitWidth, BitWidth - ShAmtC));
        return SelectInst::Create(X, NewC, ConstantInt::getNullValue(Ty));
      }

      if ((!Ty->isIntegerTy() || shouldChangeType(Ty, X->getType())) &&
          Op0->hasOneUse()) {
        // Are we moving the sign bit to the low bit and widening with high
        // zeros? lshr (sext iM X to iN), N-1 --> zext (lshr X, M-1) to iN
        if (ShAmtC == BitWidth - 1) {
          Value *NewLShr = Builder.CreateLShr(X, SrcTyBitWidth - 1);
          return new ZExtInst(NewLShr, Ty);
        }

        // lshr (sext iM X to iN), N-M --> zext (ashr X, min(N-M, M-1)) to iN
        if (ShAmtC == BitWidth - SrcTyBitWidth) {
          // The new shift amount can't be more than the narrow source type.
          unsigned NewShAmt = std::min(ShAmtC, SrcTyBitWidth - 1);
          Value *AShr = Builder.CreateAShr(X, NewShAmt);
          return new ZExtInst(AShr, Ty);
        }
      }
    }

    if (ShAmtC == BitWidth - 1) {
      // lshr i32 or(X,-X), 31 --> zext (X != 0)
      if (match(Op0, m_OneUse(m_c_Or(m_Neg(m_Value(X)), m_Deferred(X)))))
        return new ZExtInst(Builder.CreateIsNotNull(X), Ty);

      // lshr i32 (X -nsw Y), 31 --> zext (X < Y)
      if (match(Op0, m_OneUse(m_NSWSub(m_Value(X), m_Value(Y)))))
        return new ZExtInst(Builder.CreateICmpSLT(X, Y), Ty);

      // Check if a number is negative and odd:
      // lshr i32 (srem X, 2), 31 --> and (X >> 31), X
      if (match(Op0, m_OneUse(m_SRem(m_Value(X), m_SpecificInt(2))))) {
        Value *Signbit = Builder.CreateLShr(X, ShAmtC);
        return BinaryOperator::CreateAnd(Signbit, X);
      }
    }

    Instruction *TruncSrc;
    if (match(Op0, m_OneUse(m_Trunc(m_Instruction(TruncSrc)))) &&
        match(TruncSrc, m_LShr(m_Value(X), m_APInt(C1)))) {
      unsigned SrcWidth = X->getType()->getScalarSizeInBits();
      unsigned AmtSum = ShAmtC + C1->getZExtValue();

      // If the combined shift fits in the source width:
      // (trunc (X >>u C1)) >>u C --> and (trunc (X >>u (C1 + C)), MaskC
      //
      // If the first shift covers the number of bits truncated, then the
      // mask instruction is eliminated (and so the use check is relaxed).
      if (AmtSum < SrcWidth &&
          (TruncSrc->hasOneUse() || C1->uge(SrcWidth - BitWidth))) {
        Value *SumShift = Builder.CreateLShr(X, AmtSum, "sum.shift");
        Value *Trunc = Builder.CreateTrunc(SumShift, Ty, I.getName());

        // If the first shift does not cover the number of bits truncated, then
        // we require a mask to get rid of high bits in the result.
        APInt MaskC = APInt::getAllOnes(BitWidth).lshr(ShAmtC);
        return BinaryOperator::CreateAnd(Trunc, ConstantInt::get(Ty, MaskC));
      }
    }

    const APInt *MulC;
    if (match(Op0, m_NUWMul(m_Value(X), m_APInt(MulC)))) {
      if (BitWidth > 2 && (*MulC - 1).isPowerOf2() &&
          MulC->logBase2() == ShAmtC) {
        // Look for a "splat" mul pattern - it replicates bits across each half
        // of a value, so a right shift simplifies back to just X:
        // lshr i[2N] (mul nuw X, (2^N)+1), N --> X
        if (ShAmtC * 2 == BitWidth)
          return replaceInstUsesWith(I, X);

        // lshr (mul nuw (X, 2^N + 1)), N -> add nuw (X, lshr(X, N))
        if (Op0->hasOneUse()) {
          auto *NewAdd = BinaryOperator::CreateNUWAdd(
              X, Builder.CreateLShr(X, ConstantInt::get(Ty, ShAmtC), "",
                                    I.isExact()));
          NewAdd->setHasNoSignedWrap(
              cast<OverflowingBinaryOperator>(Op0)->hasNoSignedWrap());
          return NewAdd;
        }
      }

      // The one-use check is not strictly necessary, but codegen may not be
      // able to invert the transform and perf may suffer with an extra mul
      // instruction.
      if (Op0->hasOneUse()) {
        APInt NewMulC = MulC->lshr(ShAmtC);
        // if c is divisible by (1 << ShAmtC):
        // lshr (mul nuw x, MulC), ShAmtC -> mul nuw nsw x, (MulC >> ShAmtC)
        if (MulC->eq(NewMulC.shl(ShAmtC))) {
          auto *NewMul =
              BinaryOperator::CreateNUWMul(X, ConstantInt::get(Ty, NewMulC));
          assert(ShAmtC != 0 &&
                 "lshr X, 0 should be handled by simplifyLShrInst.");
          NewMul->setHasNoSignedWrap(true);
          return NewMul;
        }
      }
    }

    // lshr (mul nsw (X, 2^N + 1)), N -> add nsw (X, lshr(X, N))
    if (match(Op0, m_OneUse(m_NSWMul(m_Value(X), m_APInt(MulC))))) {
      if (BitWidth > 2 && (*MulC - 1).isPowerOf2() &&
          MulC->logBase2() == ShAmtC) {
        return BinaryOperator::CreateNSWAdd(
            X, Builder.CreateLShr(X, ConstantInt::get(Ty, ShAmtC), "",
                                  I.isExact()));
      }
    }

    // Try to narrow bswap.
    // In the case where the shift amount equals the bitwidth difference, the
    // shift is eliminated.
    if (match(Op0, m_OneUse(m_Intrinsic<Intrinsic::bswap>(
                       m_OneUse(m_ZExt(m_Value(X))))))) {
      unsigned SrcWidth = X->getType()->getScalarSizeInBits();
      unsigned WidthDiff = BitWidth - SrcWidth;
      if (SrcWidth % 16 == 0) {
        Value *NarrowSwap = Builder.CreateUnaryIntrinsic(Intrinsic::bswap, X);
        if (ShAmtC >= WidthDiff) {
          // (bswap (zext X)) >> C --> zext (bswap X >> C')
          Value *NewShift = Builder.CreateLShr(NarrowSwap, ShAmtC - WidthDiff);
          return new ZExtInst(NewShift, Ty);
        } else {
          // (bswap (zext X)) >> C --> (zext (bswap X)) << C'
          Value *NewZExt = Builder.CreateZExt(NarrowSwap, Ty);
          Constant *ShiftDiff = ConstantInt::get(Ty, WidthDiff - ShAmtC);
          return BinaryOperator::CreateShl(NewZExt, ShiftDiff);
        }
      }
    }

    // Reduce add-carry of bools to logic:
    // ((zext BoolX) + (zext BoolY)) >> 1 --> zext (BoolX && BoolY)
    Value *BoolX, *BoolY;
    if (ShAmtC == 1 && match(Op0, m_Add(m_Value(X), m_Value(Y))) &&
        match(X, m_ZExt(m_Value(BoolX))) && match(Y, m_ZExt(m_Value(BoolY))) &&
        BoolX->getType()->isIntOrIntVectorTy(1) &&
        BoolY->getType()->isIntOrIntVectorTy(1) &&
        (X->hasOneUse() || Y->hasOneUse() || Op0->hasOneUse())) {
      Value *And = Builder.CreateAnd(BoolX, BoolY);
      return new ZExtInst(And, Ty);
    }
  }

  const SimplifyQuery Q = SQ.getWithInstruction(&I);
  if (setShiftFlags(I, Q))
    return &I;

  // Transform  (x << y) >> y  to  x & (-1 >> y)
  if (match(Op0, m_OneUse(m_Shl(m_Value(X), m_Specific(Op1))))) {
    Constant *AllOnes = ConstantInt::getAllOnesValue(Ty);
    Value *Mask = Builder.CreateLShr(AllOnes, Op1);
    return BinaryOperator::CreateAnd(Mask, X);
  }

  // Transform  (-1 << y) >> y  to -1 >> y
  if (match(Op0, m_Shl(m_AllOnes(), m_Specific(Op1)))) {
    Constant *AllOnes = ConstantInt::getAllOnesValue(Ty);
    return BinaryOperator::CreateLShr(AllOnes, Op1);
  }

  if (Instruction *Overflow = foldLShrOverflowBit(I))
    return Overflow;

  return nullptr;
}

Instruction *
InstCombinerImpl::foldVariableSignZeroExtensionOfVariableHighBitExtract(
    BinaryOperator &OldAShr) {
  assert(OldAShr.getOpcode() == Instruction::AShr &&
         "Must be called with arithmetic right-shift instruction only.");

  // Check that constant C is a splat of the element-wise bitwidth of V.
  auto BitWidthSplat = [](Constant *C, Value *V) {
    return match(
        C, m_SpecificInt_ICMP(ICmpInst::Predicate::ICMP_EQ,
                              APInt(C->getType()->getScalarSizeInBits(),
                                    V->getType()->getScalarSizeInBits())));
  };

  // It should look like variable-length sign-extension on the outside:
  //   (Val << (bitwidth(Val)-Nbits)) a>> (bitwidth(Val)-Nbits)
  Value *NBits;
  Instruction *MaybeTrunc;
  Constant *C1, *C2;
  if (!match(&OldAShr,
             m_AShr(m_Shl(m_Instruction(MaybeTrunc),
                          m_ZExtOrSelf(m_Sub(m_Constant(C1),
                                             m_ZExtOrSelf(m_Value(NBits))))),
                    m_ZExtOrSelf(m_Sub(m_Constant(C2),
                                       m_ZExtOrSelf(m_Deferred(NBits)))))) ||
      !BitWidthSplat(C1, &OldAShr) || !BitWidthSplat(C2, &OldAShr))
    return nullptr;

  // There may or may not be a truncation after outer two shifts.
  Instruction *HighBitExtract;
  match(MaybeTrunc, m_TruncOrSelf(m_Instruction(HighBitExtract)));
  bool HadTrunc = MaybeTrunc != HighBitExtract;

  // And finally, the innermost part of the pattern must be a right-shift.
  Value *X, *NumLowBitsToSkip;
  if (!match(HighBitExtract, m_Shr(m_Value(X), m_Value(NumLowBitsToSkip))))
    return nullptr;

  // Said right-shift must extract high NBits bits - C0 must be it's bitwidth.
  Constant *C0;
  if (!match(NumLowBitsToSkip,
             m_ZExtOrSelf(
                 m_Sub(m_Constant(C0), m_ZExtOrSelf(m_Specific(NBits))))) ||
      !BitWidthSplat(C0, HighBitExtract))
    return nullptr;

  // Since the NBits is identical for all shifts, if the outermost and
  // innermost shifts are identical, then outermost shifts are redundant.
  // If we had truncation, do keep it though.
  if (HighBitExtract->getOpcode() == OldAShr.getOpcode())
    return replaceInstUsesWith(OldAShr, MaybeTrunc);

  // Else, if there was a truncation, then we need to ensure that one
  // instruction will go away.
  if (HadTrunc && !match(&OldAShr, m_c_BinOp(m_OneUse(m_Value()), m_Value())))
    return nullptr;

  // Finally, bypass two innermost shifts, and perform the outermost shift on
  // the operands of the innermost shift.
  Instruction *NewAShr =
      BinaryOperator::Create(OldAShr.getOpcode(), X, NumLowBitsToSkip);
  NewAShr->copyIRFlags(HighBitExtract); // We can preserve 'exact'-ness.
  if (!HadTrunc)
    return NewAShr;

  Builder.Insert(NewAShr);
  return TruncInst::CreateTruncOrBitCast(NewAShr, OldAShr.getType());
}

Instruction *InstCombinerImpl::visitAShr(BinaryOperator &I) {
  if (Value *V = simplifyAShrInst(I.getOperand(0), I.getOperand(1), I.isExact(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *R = commonShiftTransforms(I))
    return R;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();
  const APInt *ShAmtAPInt;
  if (match(Op1, m_APInt(ShAmtAPInt)) && ShAmtAPInt->ult(BitWidth)) {
    unsigned ShAmt = ShAmtAPInt->getZExtValue();

    // If the shift amount equals the difference in width of the destination
    // and source scalar types:
    // ashr (shl (zext X), C), C --> sext X
    Value *X;
    if (match(Op0, m_Shl(m_ZExt(m_Value(X)), m_Specific(Op1))) &&
        ShAmt == BitWidth - X->getType()->getScalarSizeInBits())
      return new SExtInst(X, Ty);

    // We can't handle (X << C1) >>s C2. It shifts arbitrary bits in. However,
    // we can handle (X <<nsw C1) >>s C2 since it only shifts in sign bits.
    const APInt *ShOp1;
    if (match(Op0, m_NSWShl(m_Value(X), m_APInt(ShOp1))) &&
        ShOp1->ult(BitWidth)) {
      unsigned ShlAmt = ShOp1->getZExtValue();
      if (ShlAmt < ShAmt) {
        // (X <<nsw C1) >>s C2 --> X >>s (C2 - C1)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShAmt - ShlAmt);
        auto *NewAShr = BinaryOperator::CreateAShr(X, ShiftDiff);
        NewAShr->setIsExact(I.isExact());
        return NewAShr;
      }
      if (ShlAmt > ShAmt) {
        // (X <<nsw C1) >>s C2 --> X <<nsw (C1 - C2)
        Constant *ShiftDiff = ConstantInt::get(Ty, ShlAmt - ShAmt);
        auto *NewShl = BinaryOperator::Create(Instruction::Shl, X, ShiftDiff);
        NewShl->setHasNoSignedWrap(true);
        return NewShl;
      }
    }

    if (match(Op0, m_AShr(m_Value(X), m_APInt(ShOp1))) &&
        ShOp1->ult(BitWidth)) {
      unsigned AmtSum = ShAmt + ShOp1->getZExtValue();
      // Oversized arithmetic shifts replicate the sign bit.
      AmtSum = std::min(AmtSum, BitWidth - 1);
      // (X >>s C1) >>s C2 --> X >>s (C1 + C2)
      return BinaryOperator::CreateAShr(X, ConstantInt::get(Ty, AmtSum));
    }

    if (match(Op0, m_OneUse(m_SExt(m_Value(X)))) &&
        (Ty->isVectorTy() || shouldChangeType(Ty, X->getType()))) {
      // ashr (sext X), C --> sext (ashr X, C')
      Type *SrcTy = X->getType();
      ShAmt = std::min(ShAmt, SrcTy->getScalarSizeInBits() - 1);
      Value *NewSh = Builder.CreateAShr(X, ConstantInt::get(SrcTy, ShAmt));
      return new SExtInst(NewSh, Ty);
    }

    if (ShAmt == BitWidth - 1) {
      // ashr i32 or(X,-X), 31 --> sext (X != 0)
      if (match(Op0, m_OneUse(m_c_Or(m_Neg(m_Value(X)), m_Deferred(X)))))
        return new SExtInst(Builder.CreateIsNotNull(X), Ty);

      // ashr i32 (X -nsw Y), 31 --> sext (X < Y)
      Value *Y;
      if (match(Op0, m_OneUse(m_NSWSub(m_Value(X), m_Value(Y)))))
        return new SExtInst(Builder.CreateICmpSLT(X, Y), Ty);
    }

    const APInt *MulC;
    if (match(Op0, m_OneUse(m_NSWMul(m_Value(X), m_APInt(MulC)))) &&
        (BitWidth > 2 && (*MulC - 1).isPowerOf2() &&
         MulC->logBase2() == ShAmt &&
         (ShAmt < BitWidth - 1))) /* Minus 1 for the sign bit */ {

      // ashr (mul nsw (X, 2^N + 1)), N -> add nsw (X, ashr(X, N))
      auto *NewAdd = BinaryOperator::CreateNSWAdd(
          X,
          Builder.CreateAShr(X, ConstantInt::get(Ty, ShAmt), "", I.isExact()));
      NewAdd->setHasNoUnsignedWrap(
          cast<OverflowingBinaryOperator>(Op0)->hasNoUnsignedWrap());
      return NewAdd;
    }
  }

  const SimplifyQuery Q = SQ.getWithInstruction(&I);
  if (setShiftFlags(I, Q))
    return &I;

  // Prefer `-(x & 1)` over `(x << (bitwidth(x)-1)) a>> (bitwidth(x)-1)`
  // as the pattern to splat the lowest bit.
  // FIXME: iff X is already masked, we don't need the one-use check.
  Value *X;
  if (match(Op1, m_SpecificIntAllowPoison(BitWidth - 1)) &&
      match(Op0, m_OneUse(m_Shl(m_Value(X),
                                m_SpecificIntAllowPoison(BitWidth - 1))))) {
    Constant *Mask = ConstantInt::get(Ty, 1);
    // Retain the knowledge about the ignored lanes.
    Mask = Constant::mergeUndefsWith(
        Constant::mergeUndefsWith(Mask, cast<Constant>(Op1)),
        cast<Constant>(cast<Instruction>(Op0)->getOperand(1)));
    X = Builder.CreateAnd(X, Mask);
    return BinaryOperator::CreateNeg(X);
  }

  if (Instruction *R = foldVariableSignZeroExtensionOfVariableHighBitExtract(I))
    return R;

  // See if we can turn a signed shr into an unsigned shr.
  if (MaskedValueIsZero(Op0, APInt::getSignMask(BitWidth), 0, &I)) {
    Instruction *Lshr = BinaryOperator::CreateLShr(Op0, Op1);
    Lshr->setIsExact(I.isExact());
    return Lshr;
  }

  // ashr (xor %x, -1), %y  -->  xor (ashr %x, %y), -1
  if (match(Op0, m_OneUse(m_Not(m_Value(X))))) {
    // Note that we must drop 'exact'-ness of the shift!
    // Note that we can't keep undef's in -1 vector constant!
    auto *NewAShr = Builder.CreateAShr(X, Op1, Op0->getName() + ".not");
    return BinaryOperator::CreateNot(NewAShr);
  }

  return nullptr;
}
