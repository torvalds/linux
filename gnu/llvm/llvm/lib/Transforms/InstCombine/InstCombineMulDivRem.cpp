//===- InstCombineMulDivRem.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visit functions for mul, fmul, sdiv, udiv, fdiv,
// srem, urem, frem.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include <cassert>

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

using namespace llvm;
using namespace PatternMatch;

/// The specific integer value is used in a context where it is known to be
/// non-zero.  If this allows us to simplify the computation, do so and return
/// the new operand, otherwise return null.
static Value *simplifyValueKnownNonZero(Value *V, InstCombinerImpl &IC,
                                        Instruction &CxtI) {
  // If V has multiple uses, then we would have to do more analysis to determine
  // if this is safe.  For example, the use could be in dynamically unreached
  // code.
  if (!V->hasOneUse()) return nullptr;

  bool MadeChange = false;

  // ((1 << A) >>u B) --> (1 << (A-B))
  // Because V cannot be zero, we know that B is less than A.
  Value *A = nullptr, *B = nullptr, *One = nullptr;
  if (match(V, m_LShr(m_OneUse(m_Shl(m_Value(One), m_Value(A))), m_Value(B))) &&
      match(One, m_One())) {
    A = IC.Builder.CreateSub(A, B);
    return IC.Builder.CreateShl(One, A);
  }

  // (PowerOfTwo >>u B) --> isExact since shifting out the result would make it
  // inexact.  Similarly for <<.
  BinaryOperator *I = dyn_cast<BinaryOperator>(V);
  if (I && I->isLogicalShift() &&
      IC.isKnownToBeAPowerOfTwo(I->getOperand(0), false, 0, &CxtI)) {
    // We know that this is an exact/nuw shift and that the input is a
    // non-zero context as well.
    if (Value *V2 = simplifyValueKnownNonZero(I->getOperand(0), IC, CxtI)) {
      IC.replaceOperand(*I, 0, V2);
      MadeChange = true;
    }

    if (I->getOpcode() == Instruction::LShr && !I->isExact()) {
      I->setIsExact();
      MadeChange = true;
    }

    if (I->getOpcode() == Instruction::Shl && !I->hasNoUnsignedWrap()) {
      I->setHasNoUnsignedWrap();
      MadeChange = true;
    }
  }

  // TODO: Lots more we could do here:
  //    If V is a phi node, we can call this on each of its operands.
  //    "select cond, X, 0" can simplify to "X".

  return MadeChange ? V : nullptr;
}

// TODO: This is a specific form of a much more general pattern.
//       We could detect a select with any binop identity constant, or we
//       could use SimplifyBinOp to see if either arm of the select reduces.
//       But that needs to be done carefully and/or while removing potential
//       reverse canonicalizations as in InstCombiner::foldSelectIntoOp().
static Value *foldMulSelectToNegate(BinaryOperator &I,
                                    InstCombiner::BuilderTy &Builder) {
  Value *Cond, *OtherOp;

  // mul (select Cond, 1, -1), OtherOp --> select Cond, OtherOp, -OtherOp
  // mul OtherOp, (select Cond, 1, -1) --> select Cond, OtherOp, -OtherOp
  if (match(&I, m_c_Mul(m_OneUse(m_Select(m_Value(Cond), m_One(), m_AllOnes())),
                        m_Value(OtherOp)))) {
    bool HasAnyNoWrap = I.hasNoSignedWrap() || I.hasNoUnsignedWrap();
    Value *Neg = Builder.CreateNeg(OtherOp, "", HasAnyNoWrap);
    return Builder.CreateSelect(Cond, OtherOp, Neg);
  }
  // mul (select Cond, -1, 1), OtherOp --> select Cond, -OtherOp, OtherOp
  // mul OtherOp, (select Cond, -1, 1) --> select Cond, -OtherOp, OtherOp
  if (match(&I, m_c_Mul(m_OneUse(m_Select(m_Value(Cond), m_AllOnes(), m_One())),
                        m_Value(OtherOp)))) {
    bool HasAnyNoWrap = I.hasNoSignedWrap() || I.hasNoUnsignedWrap();
    Value *Neg = Builder.CreateNeg(OtherOp, "", HasAnyNoWrap);
    return Builder.CreateSelect(Cond, Neg, OtherOp);
  }

  // fmul (select Cond, 1.0, -1.0), OtherOp --> select Cond, OtherOp, -OtherOp
  // fmul OtherOp, (select Cond, 1.0, -1.0) --> select Cond, OtherOp, -OtherOp
  if (match(&I, m_c_FMul(m_OneUse(m_Select(m_Value(Cond), m_SpecificFP(1.0),
                                           m_SpecificFP(-1.0))),
                         m_Value(OtherOp)))) {
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.setFastMathFlags(I.getFastMathFlags());
    return Builder.CreateSelect(Cond, OtherOp, Builder.CreateFNeg(OtherOp));
  }

  // fmul (select Cond, -1.0, 1.0), OtherOp --> select Cond, -OtherOp, OtherOp
  // fmul OtherOp, (select Cond, -1.0, 1.0) --> select Cond, -OtherOp, OtherOp
  if (match(&I, m_c_FMul(m_OneUse(m_Select(m_Value(Cond), m_SpecificFP(-1.0),
                                           m_SpecificFP(1.0))),
                         m_Value(OtherOp)))) {
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.setFastMathFlags(I.getFastMathFlags());
    return Builder.CreateSelect(Cond, Builder.CreateFNeg(OtherOp), OtherOp);
  }

  return nullptr;
}

/// Reduce integer multiplication patterns that contain a (+/-1 << Z) factor.
/// Callers are expected to call this twice to handle commuted patterns.
static Value *foldMulShl1(BinaryOperator &Mul, bool CommuteOperands,
                          InstCombiner::BuilderTy &Builder) {
  Value *X = Mul.getOperand(0), *Y = Mul.getOperand(1);
  if (CommuteOperands)
    std::swap(X, Y);

  const bool HasNSW = Mul.hasNoSignedWrap();
  const bool HasNUW = Mul.hasNoUnsignedWrap();

  // X * (1 << Z) --> X << Z
  Value *Z;
  if (match(Y, m_Shl(m_One(), m_Value(Z)))) {
    bool PropagateNSW = HasNSW && cast<ShlOperator>(Y)->hasNoSignedWrap();
    return Builder.CreateShl(X, Z, Mul.getName(), HasNUW, PropagateNSW);
  }

  // Similar to above, but an increment of the shifted value becomes an add:
  // X * ((1 << Z) + 1) --> (X * (1 << Z)) + X --> (X << Z) + X
  // This increases uses of X, so it may require a freeze, but that is still
  // expected to be an improvement because it removes the multiply.
  BinaryOperator *Shift;
  if (match(Y, m_OneUse(m_Add(m_BinOp(Shift), m_One()))) &&
      match(Shift, m_OneUse(m_Shl(m_One(), m_Value(Z))))) {
    bool PropagateNSW = HasNSW && Shift->hasNoSignedWrap();
    Value *FrX = X;
    if (!isGuaranteedNotToBeUndef(X))
      FrX = Builder.CreateFreeze(X, X->getName() + ".fr");
    Value *Shl = Builder.CreateShl(FrX, Z, "mulshl", HasNUW, PropagateNSW);
    return Builder.CreateAdd(Shl, FrX, Mul.getName(), HasNUW, PropagateNSW);
  }

  // Similar to above, but a decrement of the shifted value is disguised as
  // 'not' and becomes a sub:
  // X * (~(-1 << Z)) --> X * ((1 << Z) - 1) --> (X << Z) - X
  // This increases uses of X, so it may require a freeze, but that is still
  // expected to be an improvement because it removes the multiply.
  if (match(Y, m_OneUse(m_Not(m_OneUse(m_Shl(m_AllOnes(), m_Value(Z))))))) {
    Value *FrX = X;
    if (!isGuaranteedNotToBeUndef(X))
      FrX = Builder.CreateFreeze(X, X->getName() + ".fr");
    Value *Shl = Builder.CreateShl(FrX, Z, "mulshl");
    return Builder.CreateSub(Shl, FrX, Mul.getName());
  }

  return nullptr;
}

static Value *takeLog2(IRBuilderBase &Builder, Value *Op, unsigned Depth,
                       bool AssumeNonZero, bool DoFold);

Instruction *InstCombinerImpl::visitMul(BinaryOperator &I) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  if (Value *V =
          simplifyMulInst(Op0, Op1, I.hasNoSignedWrap(), I.hasNoUnsignedWrap(),
                          SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  if (Value *V = foldUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  Type *Ty = I.getType();
  const unsigned BitWidth = Ty->getScalarSizeInBits();
  const bool HasNSW = I.hasNoSignedWrap();
  const bool HasNUW = I.hasNoUnsignedWrap();

  // X * -1 --> 0 - X
  if (match(Op1, m_AllOnes())) {
    return HasNSW ? BinaryOperator::CreateNSWNeg(Op0)
                  : BinaryOperator::CreateNeg(Op0);
  }

  // Also allow combining multiply instructions on vectors.
  {
    Value *NewOp;
    Constant *C1, *C2;
    const APInt *IVal;
    if (match(&I, m_Mul(m_Shl(m_Value(NewOp), m_ImmConstant(C2)),
                        m_ImmConstant(C1))) &&
        match(C1, m_APInt(IVal))) {
      // ((X << C2)*C1) == (X * (C1 << C2))
      Constant *Shl =
          ConstantFoldBinaryOpOperands(Instruction::Shl, C1, C2, DL);
      assert(Shl && "Constant folding of immediate constants failed");
      BinaryOperator *Mul = cast<BinaryOperator>(I.getOperand(0));
      BinaryOperator *BO = BinaryOperator::CreateMul(NewOp, Shl);
      if (HasNUW && Mul->hasNoUnsignedWrap())
        BO->setHasNoUnsignedWrap();
      if (HasNSW && Mul->hasNoSignedWrap() && Shl->isNotMinSignedValue())
        BO->setHasNoSignedWrap();
      return BO;
    }

    if (match(&I, m_Mul(m_Value(NewOp), m_Constant(C1)))) {
      // Replace X*(2^C) with X << C, where C is either a scalar or a vector.
      if (Constant *NewCst = ConstantExpr::getExactLogBase2(C1)) {
        BinaryOperator *Shl = BinaryOperator::CreateShl(NewOp, NewCst);

        if (HasNUW)
          Shl->setHasNoUnsignedWrap();
        if (HasNSW) {
          const APInt *V;
          if (match(NewCst, m_APInt(V)) && *V != V->getBitWidth() - 1)
            Shl->setHasNoSignedWrap();
        }

        return Shl;
      }
    }
  }

  if (Op0->hasOneUse() && match(Op1, m_NegatedPower2())) {
    // Interpret  X * (-1<<C)  as  (-X) * (1<<C)  and try to sink the negation.
    // The "* (1<<C)" thus becomes a potential shifting opportunity.
    if (Value *NegOp0 =
            Negator::Negate(/*IsNegation*/ true, HasNSW, Op0, *this)) {
      auto *Op1C = cast<Constant>(Op1);
      return replaceInstUsesWith(
          I, Builder.CreateMul(NegOp0, ConstantExpr::getNeg(Op1C), "",
                               /* HasNUW */ false,
                               HasNSW && Op1C->isNotMinSignedValue()));
    }

    // Try to convert multiply of extended operand to narrow negate and shift
    // for better analysis.
    // This is valid if the shift amount (trailing zeros in the multiplier
    // constant) clears more high bits than the bitwidth difference between
    // source and destination types:
    // ({z/s}ext X) * (-1<<C) --> (zext (-X)) << C
    const APInt *NegPow2C;
    Value *X;
    if (match(Op0, m_ZExtOrSExt(m_Value(X))) &&
        match(Op1, m_APIntAllowPoison(NegPow2C))) {
      unsigned SrcWidth = X->getType()->getScalarSizeInBits();
      unsigned ShiftAmt = NegPow2C->countr_zero();
      if (ShiftAmt >= BitWidth - SrcWidth) {
        Value *N = Builder.CreateNeg(X, X->getName() + ".neg");
        Value *Z = Builder.CreateZExt(N, Ty, N->getName() + ".z");
        return BinaryOperator::CreateShl(Z, ConstantInt::get(Ty, ShiftAmt));
      }
    }
  }

  if (Instruction *FoldedMul = foldBinOpIntoSelectOrPhi(I))
    return FoldedMul;

  if (Value *FoldedMul = foldMulSelectToNegate(I, Builder))
    return replaceInstUsesWith(I, FoldedMul);

  // Simplify mul instructions with a constant RHS.
  Constant *MulC;
  if (match(Op1, m_ImmConstant(MulC))) {
    // Canonicalize (X+C1)*MulC -> X*MulC+C1*MulC.
    // Canonicalize (X|C1)*MulC -> X*MulC+C1*MulC.
    Value *X;
    Constant *C1;
    if (match(Op0, m_OneUse(m_AddLike(m_Value(X), m_ImmConstant(C1))))) {
      // C1*MulC simplifies to a tidier constant.
      Value *NewC = Builder.CreateMul(C1, MulC);
      auto *BOp0 = cast<BinaryOperator>(Op0);
      bool Op0NUW =
          (BOp0->getOpcode() == Instruction::Or || BOp0->hasNoUnsignedWrap());
      Value *NewMul = Builder.CreateMul(X, MulC);
      auto *BO = BinaryOperator::CreateAdd(NewMul, NewC);
      if (HasNUW && Op0NUW) {
        // If NewMulBO is constant we also can set BO to nuw.
        if (auto *NewMulBO = dyn_cast<BinaryOperator>(NewMul))
          NewMulBO->setHasNoUnsignedWrap();
        BO->setHasNoUnsignedWrap();
      }
      return BO;
    }
  }

  // abs(X) * abs(X) -> X * X
  Value *X;
  if (Op0 == Op1 && match(Op0, m_Intrinsic<Intrinsic::abs>(m_Value(X))))
    return BinaryOperator::CreateMul(X, X);

  {
    Value *Y;
    // abs(X) * abs(Y) -> abs(X * Y)
    if (I.hasNoSignedWrap() &&
        match(Op0,
              m_OneUse(m_Intrinsic<Intrinsic::abs>(m_Value(X), m_One()))) &&
        match(Op1, m_OneUse(m_Intrinsic<Intrinsic::abs>(m_Value(Y), m_One()))))
      return replaceInstUsesWith(
          I, Builder.CreateBinaryIntrinsic(Intrinsic::abs,
                                           Builder.CreateNSWMul(X, Y),
                                           Builder.getTrue()));
  }

  // -X * C --> X * -C
  Value *Y;
  Constant *Op1C;
  if (match(Op0, m_Neg(m_Value(X))) && match(Op1, m_Constant(Op1C)))
    return BinaryOperator::CreateMul(X, ConstantExpr::getNeg(Op1C));

  // -X * -Y --> X * Y
  if (match(Op0, m_Neg(m_Value(X))) && match(Op1, m_Neg(m_Value(Y)))) {
    auto *NewMul = BinaryOperator::CreateMul(X, Y);
    if (HasNSW && cast<OverflowingBinaryOperator>(Op0)->hasNoSignedWrap() &&
        cast<OverflowingBinaryOperator>(Op1)->hasNoSignedWrap())
      NewMul->setHasNoSignedWrap();
    return NewMul;
  }

  // -X * Y --> -(X * Y)
  // X * -Y --> -(X * Y)
  if (match(&I, m_c_Mul(m_OneUse(m_Neg(m_Value(X))), m_Value(Y))))
    return BinaryOperator::CreateNeg(Builder.CreateMul(X, Y));

  // (-X * Y) * -X --> (X * Y) * X
  // (-X << Y) * -X --> (X << Y) * X
  if (match(Op1, m_Neg(m_Value(X)))) {
    if (Value *NegOp0 = Negator::Negate(false, /*IsNSW*/ false, Op0, *this))
      return BinaryOperator::CreateMul(NegOp0, X);
  }

  if (Op0->hasOneUse()) {
    // (mul (div exact X, C0), C1)
    //    -> (div exact X, C0 / C1)
    // iff C0 % C1 == 0 and X / (C0 / C1) doesn't create UB.
    const APInt *C1;
    auto UDivCheck = [&C1](const APInt &C) { return C.urem(*C1).isZero(); };
    auto SDivCheck = [&C1](const APInt &C) {
      APInt Quot, Rem;
      APInt::sdivrem(C, *C1, Quot, Rem);
      return Rem.isZero() && !Quot.isAllOnes();
    };
    if (match(Op1, m_APInt(C1)) &&
        (match(Op0, m_Exact(m_UDiv(m_Value(X), m_CheckedInt(UDivCheck)))) ||
         match(Op0, m_Exact(m_SDiv(m_Value(X), m_CheckedInt(SDivCheck)))))) {
      auto BOpc = cast<BinaryOperator>(Op0)->getOpcode();
      return BinaryOperator::CreateExact(
          BOpc, X,
          Builder.CreateBinOp(BOpc, cast<BinaryOperator>(Op0)->getOperand(1),
                              Op1));
    }
  }

  // (X / Y) *  Y = X - (X % Y)
  // (X / Y) * -Y = (X % Y) - X
  {
    Value *Y = Op1;
    BinaryOperator *Div = dyn_cast<BinaryOperator>(Op0);
    if (!Div || (Div->getOpcode() != Instruction::UDiv &&
                 Div->getOpcode() != Instruction::SDiv)) {
      Y = Op0;
      Div = dyn_cast<BinaryOperator>(Op1);
    }
    Value *Neg = dyn_castNegVal(Y);
    if (Div && Div->hasOneUse() &&
        (Div->getOperand(1) == Y || Div->getOperand(1) == Neg) &&
        (Div->getOpcode() == Instruction::UDiv ||
         Div->getOpcode() == Instruction::SDiv)) {
      Value *X = Div->getOperand(0), *DivOp1 = Div->getOperand(1);

      // If the division is exact, X % Y is zero, so we end up with X or -X.
      if (Div->isExact()) {
        if (DivOp1 == Y)
          return replaceInstUsesWith(I, X);
        return BinaryOperator::CreateNeg(X);
      }

      auto RemOpc = Div->getOpcode() == Instruction::UDiv ? Instruction::URem
                                                          : Instruction::SRem;
      // X must be frozen because we are increasing its number of uses.
      Value *XFreeze = X;
      if (!isGuaranteedNotToBeUndef(X))
        XFreeze = Builder.CreateFreeze(X, X->getName() + ".fr");
      Value *Rem = Builder.CreateBinOp(RemOpc, XFreeze, DivOp1);
      if (DivOp1 == Y)
        return BinaryOperator::CreateSub(XFreeze, Rem);
      return BinaryOperator::CreateSub(Rem, XFreeze);
    }
  }

  // Fold the following two scenarios:
  //   1) i1 mul -> i1 and.
  //   2) X * Y --> X & Y, iff X, Y can be only {0,1}.
  // Note: We could use known bits to generalize this and related patterns with
  // shifts/truncs
  if (Ty->isIntOrIntVectorTy(1) ||
      (match(Op0, m_And(m_Value(), m_One())) &&
       match(Op1, m_And(m_Value(), m_One()))))
    return BinaryOperator::CreateAnd(Op0, Op1);

  if (Value *R = foldMulShl1(I, /* CommuteOperands */ false, Builder))
    return replaceInstUsesWith(I, R);
  if (Value *R = foldMulShl1(I, /* CommuteOperands */ true, Builder))
    return replaceInstUsesWith(I, R);

  // (zext bool X) * (zext bool Y) --> zext (and X, Y)
  // (sext bool X) * (sext bool Y) --> zext (and X, Y)
  // Note: -1 * -1 == 1 * 1 == 1 (if the extends match, the result is the same)
  if (((match(Op0, m_ZExt(m_Value(X))) && match(Op1, m_ZExt(m_Value(Y)))) ||
       (match(Op0, m_SExt(m_Value(X))) && match(Op1, m_SExt(m_Value(Y))))) &&
      X->getType()->isIntOrIntVectorTy(1) && X->getType() == Y->getType() &&
      (Op0->hasOneUse() || Op1->hasOneUse() || X == Y)) {
    Value *And = Builder.CreateAnd(X, Y, "mulbool");
    return CastInst::Create(Instruction::ZExt, And, Ty);
  }
  // (sext bool X) * (zext bool Y) --> sext (and X, Y)
  // (zext bool X) * (sext bool Y) --> sext (and X, Y)
  // Note: -1 * 1 == 1 * -1  == -1
  if (((match(Op0, m_SExt(m_Value(X))) && match(Op1, m_ZExt(m_Value(Y)))) ||
       (match(Op0, m_ZExt(m_Value(X))) && match(Op1, m_SExt(m_Value(Y))))) &&
      X->getType()->isIntOrIntVectorTy(1) && X->getType() == Y->getType() &&
      (Op0->hasOneUse() || Op1->hasOneUse())) {
    Value *And = Builder.CreateAnd(X, Y, "mulbool");
    return CastInst::Create(Instruction::SExt, And, Ty);
  }

  // (zext bool X) * Y --> X ? Y : 0
  // Y * (zext bool X) --> X ? Y : 0
  if (match(Op0, m_ZExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(X, Op1, ConstantInt::getNullValue(Ty));
  if (match(Op1, m_ZExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(X, Op0, ConstantInt::getNullValue(Ty));

  // mul (sext X), Y -> select X, -Y, 0
  // mul Y, (sext X) -> select X, -Y, 0
  if (match(&I, m_c_Mul(m_OneUse(m_SExt(m_Value(X))), m_Value(Y))) &&
      X->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(X, Builder.CreateNeg(Y, "", I.hasNoSignedWrap()),
                              ConstantInt::getNullValue(Op0->getType()));

  Constant *ImmC;
  if (match(Op1, m_ImmConstant(ImmC))) {
    // (sext bool X) * C --> X ? -C : 0
    if (match(Op0, m_SExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1)) {
      Constant *NegC = ConstantExpr::getNeg(ImmC);
      return SelectInst::Create(X, NegC, ConstantInt::getNullValue(Ty));
    }

    // (ashr i32 X, 31) * C --> (X < 0) ? -C : 0
    const APInt *C;
    if (match(Op0, m_OneUse(m_AShr(m_Value(X), m_APInt(C)))) &&
        *C == C->getBitWidth() - 1) {
      Constant *NegC = ConstantExpr::getNeg(ImmC);
      Value *IsNeg = Builder.CreateIsNeg(X, "isneg");
      return SelectInst::Create(IsNeg, NegC, ConstantInt::getNullValue(Ty));
    }
  }

  // (lshr X, 31) * Y --> (X < 0) ? Y : 0
  // TODO: We are not checking one-use because the elimination of the multiply
  //       is better for analysis?
  const APInt *C;
  if (match(&I, m_c_BinOp(m_LShr(m_Value(X), m_APInt(C)), m_Value(Y))) &&
      *C == C->getBitWidth() - 1) {
    Value *IsNeg = Builder.CreateIsNeg(X, "isneg");
    return SelectInst::Create(IsNeg, Y, ConstantInt::getNullValue(Ty));
  }

  // (and X, 1) * Y --> (trunc X) ? Y : 0
  if (match(&I, m_c_BinOp(m_OneUse(m_And(m_Value(X), m_One())), m_Value(Y)))) {
    Value *Tr = Builder.CreateTrunc(X, CmpInst::makeCmpResultType(Ty));
    return SelectInst::Create(Tr, Y, ConstantInt::getNullValue(Ty));
  }

  // ((ashr X, 31) | 1) * X --> abs(X)
  // X * ((ashr X, 31) | 1) --> abs(X)
  if (match(&I, m_c_BinOp(m_Or(m_AShr(m_Value(X),
                                      m_SpecificIntAllowPoison(BitWidth - 1)),
                               m_One()),
                          m_Deferred(X)))) {
    Value *Abs = Builder.CreateBinaryIntrinsic(
        Intrinsic::abs, X, ConstantInt::getBool(I.getContext(), HasNSW));
    Abs->takeName(&I);
    return replaceInstUsesWith(I, Abs);
  }

  if (Instruction *Ext = narrowMathIfNoOverflow(I))
    return Ext;

  if (Instruction *Res = foldBinOpOfSelectAndCastOfSelectCondition(I))
    return Res;

  // (mul Op0 Op1):
  //    if Log2(Op0) folds away ->
  //        (shl Op1, Log2(Op0))
  //    if Log2(Op1) folds away ->
  //        (shl Op0, Log2(Op1))
  if (takeLog2(Builder, Op0, /*Depth*/ 0, /*AssumeNonZero*/ false,
               /*DoFold*/ false)) {
    Value *Res = takeLog2(Builder, Op0, /*Depth*/ 0, /*AssumeNonZero*/ false,
                          /*DoFold*/ true);
    BinaryOperator *Shl = BinaryOperator::CreateShl(Op1, Res);
    // We can only propegate nuw flag.
    Shl->setHasNoUnsignedWrap(HasNUW);
    return Shl;
  }
  if (takeLog2(Builder, Op1, /*Depth*/ 0, /*AssumeNonZero*/ false,
               /*DoFold*/ false)) {
    Value *Res = takeLog2(Builder, Op1, /*Depth*/ 0, /*AssumeNonZero*/ false,
                          /*DoFold*/ true);
    BinaryOperator *Shl = BinaryOperator::CreateShl(Op0, Res);
    // We can only propegate nuw flag.
    Shl->setHasNoUnsignedWrap(HasNUW);
    return Shl;
  }

  bool Changed = false;
  if (!HasNSW && willNotOverflowSignedMul(Op0, Op1, I)) {
    Changed = true;
    I.setHasNoSignedWrap(true);
  }

  if (!HasNUW && willNotOverflowUnsignedMul(Op0, Op1, I, I.hasNoSignedWrap())) {
    Changed = true;
    I.setHasNoUnsignedWrap(true);
  }

  return Changed ? &I : nullptr;
}

Instruction *InstCombinerImpl::foldFPSignBitOps(BinaryOperator &I) {
  BinaryOperator::BinaryOps Opcode = I.getOpcode();
  assert((Opcode == Instruction::FMul || Opcode == Instruction::FDiv) &&
         "Expected fmul or fdiv");

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Value *X, *Y;

  // -X * -Y --> X * Y
  // -X / -Y --> X / Y
  if (match(Op0, m_FNeg(m_Value(X))) && match(Op1, m_FNeg(m_Value(Y))))
    return BinaryOperator::CreateWithCopiedFlags(Opcode, X, Y, &I);

  // fabs(X) * fabs(X) -> X * X
  // fabs(X) / fabs(X) -> X / X
  if (Op0 == Op1 && match(Op0, m_FAbs(m_Value(X))))
    return BinaryOperator::CreateWithCopiedFlags(Opcode, X, X, &I);

  // fabs(X) * fabs(Y) --> fabs(X * Y)
  // fabs(X) / fabs(Y) --> fabs(X / Y)
  if (match(Op0, m_FAbs(m_Value(X))) && match(Op1, m_FAbs(m_Value(Y))) &&
      (Op0->hasOneUse() || Op1->hasOneUse())) {
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.setFastMathFlags(I.getFastMathFlags());
    Value *XY = Builder.CreateBinOp(Opcode, X, Y);
    Value *Fabs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, XY);
    Fabs->takeName(&I);
    return replaceInstUsesWith(I, Fabs);
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldPowiReassoc(BinaryOperator &I) {
  auto createPowiExpr = [](BinaryOperator &I, InstCombinerImpl &IC, Value *X,
                           Value *Y, Value *Z) {
    InstCombiner::BuilderTy &Builder = IC.Builder;
    Value *YZ = Builder.CreateAdd(Y, Z);
    Instruction *NewPow = Builder.CreateIntrinsic(
        Intrinsic::powi, {X->getType(), YZ->getType()}, {X, YZ}, &I);

    return NewPow;
  };

  Value *X, *Y, *Z;
  unsigned Opcode = I.getOpcode();
  assert((Opcode == Instruction::FMul || Opcode == Instruction::FDiv) &&
         "Unexpected opcode");

  // powi(X, Y) * X --> powi(X, Y+1)
  // X * powi(X, Y) --> powi(X, Y+1)
  if (match(&I, m_c_FMul(m_OneUse(m_AllowReassoc(m_Intrinsic<Intrinsic::powi>(
                             m_Value(X), m_Value(Y)))),
                         m_Deferred(X)))) {
    Constant *One = ConstantInt::get(Y->getType(), 1);
    if (willNotOverflowSignedAdd(Y, One, I)) {
      Instruction *NewPow = createPowiExpr(I, *this, X, Y, One);
      return replaceInstUsesWith(I, NewPow);
    }
  }

  // powi(x, y) * powi(x, z) -> powi(x, y + z)
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  if (Opcode == Instruction::FMul && I.isOnlyUserOfAnyOperand() &&
      match(Op0, m_AllowReassoc(
                     m_Intrinsic<Intrinsic::powi>(m_Value(X), m_Value(Y)))) &&
      match(Op1, m_AllowReassoc(m_Intrinsic<Intrinsic::powi>(m_Specific(X),
                                                             m_Value(Z)))) &&
      Y->getType() == Z->getType()) {
    Instruction *NewPow = createPowiExpr(I, *this, X, Y, Z);
    return replaceInstUsesWith(I, NewPow);
  }

  if (Opcode == Instruction::FDiv && I.hasAllowReassoc() && I.hasNoNaNs()) {
    // powi(X, Y) / X --> powi(X, Y-1)
    // This is legal when (Y - 1) can't wraparound, in which case reassoc and
    // nnan are required.
    // TODO: Multi-use may be also better off creating Powi(x,y-1)
    if (match(Op0, m_OneUse(m_AllowReassoc(m_Intrinsic<Intrinsic::powi>(
                       m_Specific(Op1), m_Value(Y))))) &&
        willNotOverflowSignedSub(Y, ConstantInt::get(Y->getType(), 1), I)) {
      Constant *NegOne = ConstantInt::getAllOnesValue(Y->getType());
      Instruction *NewPow = createPowiExpr(I, *this, Op1, Y, NegOne);
      return replaceInstUsesWith(I, NewPow);
    }

    // powi(X, Y) / (X * Z) --> powi(X, Y-1) / Z
    // This is legal when (Y - 1) can't wraparound, in which case reassoc and
    // nnan are required.
    // TODO: Multi-use may be also better off creating Powi(x,y-1)
    if (match(Op0, m_OneUse(m_AllowReassoc(m_Intrinsic<Intrinsic::powi>(
                       m_Value(X), m_Value(Y))))) &&
        match(Op1, m_AllowReassoc(m_c_FMul(m_Specific(X), m_Value(Z)))) &&
        willNotOverflowSignedSub(Y, ConstantInt::get(Y->getType(), 1), I)) {
      Constant *NegOne = ConstantInt::getAllOnesValue(Y->getType());
      auto *NewPow = createPowiExpr(I, *this, X, Y, NegOne);
      return BinaryOperator::CreateFDivFMF(NewPow, Z, &I);
    }
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldFMulReassoc(BinaryOperator &I) {
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *X, *Y;
  Constant *C;
  BinaryOperator *Op0BinOp;

  // Reassociate constant RHS with another constant to form constant
  // expression.
  if (match(Op1, m_Constant(C)) && C->isFiniteNonZeroFP() &&
      match(Op0, m_AllowReassoc(m_BinOp(Op0BinOp)))) {
    // Everything in this scope folds I with Op0, intersecting their FMF.
    FastMathFlags FMF = I.getFastMathFlags() & Op0BinOp->getFastMathFlags();
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.setFastMathFlags(FMF);
    Constant *C1;
    if (match(Op0, m_OneUse(m_FDiv(m_Constant(C1), m_Value(X))))) {
      // (C1 / X) * C --> (C * C1) / X
      Constant *CC1 =
          ConstantFoldBinaryOpOperands(Instruction::FMul, C, C1, DL);
      if (CC1 && CC1->isNormalFP())
        return BinaryOperator::CreateFDivFMF(CC1, X, FMF);
    }
    if (match(Op0, m_FDiv(m_Value(X), m_Constant(C1)))) {
      // FIXME: This seems like it should also be checking for arcp
      // (X / C1) * C --> X * (C / C1)
      Constant *CDivC1 =
          ConstantFoldBinaryOpOperands(Instruction::FDiv, C, C1, DL);
      if (CDivC1 && CDivC1->isNormalFP())
        return BinaryOperator::CreateFMulFMF(X, CDivC1, FMF);

      // If the constant was a denormal, try reassociating differently.
      // (X / C1) * C --> X / (C1 / C)
      Constant *C1DivC =
          ConstantFoldBinaryOpOperands(Instruction::FDiv, C1, C, DL);
      if (C1DivC && Op0->hasOneUse() && C1DivC->isNormalFP())
        return BinaryOperator::CreateFDivFMF(X, C1DivC, FMF);
    }

    // We do not need to match 'fadd C, X' and 'fsub X, C' because they are
    // canonicalized to 'fadd X, C'. Distributing the multiply may allow
    // further folds and (X * C) + C2 is 'fma'.
    if (match(Op0, m_OneUse(m_FAdd(m_Value(X), m_Constant(C1))))) {
      // (X + C1) * C --> (X * C) + (C * C1)
      if (Constant *CC1 =
              ConstantFoldBinaryOpOperands(Instruction::FMul, C, C1, DL)) {
        Value *XC = Builder.CreateFMul(X, C);
        return BinaryOperator::CreateFAddFMF(XC, CC1, FMF);
      }
    }
    if (match(Op0, m_OneUse(m_FSub(m_Constant(C1), m_Value(X))))) {
      // (C1 - X) * C --> (C * C1) - (X * C)
      if (Constant *CC1 =
              ConstantFoldBinaryOpOperands(Instruction::FMul, C, C1, DL)) {
        Value *XC = Builder.CreateFMul(X, C);
        return BinaryOperator::CreateFSubFMF(CC1, XC, FMF);
      }
    }
  }

  Value *Z;
  if (match(&I,
            m_c_FMul(m_AllowReassoc(m_OneUse(m_FDiv(m_Value(X), m_Value(Y)))),
                     m_Value(Z)))) {
    BinaryOperator *DivOp = cast<BinaryOperator>(((Z == Op0) ? Op1 : Op0));
    FastMathFlags FMF = I.getFastMathFlags() & DivOp->getFastMathFlags();
    if (FMF.allowReassoc()) {
      // Sink division: (X / Y) * Z --> (X * Z) / Y
      IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
      Builder.setFastMathFlags(FMF);
      auto *NewFMul = Builder.CreateFMul(X, Z);
      return BinaryOperator::CreateFDivFMF(NewFMul, Y, FMF);
    }
  }

  // sqrt(X) * sqrt(Y) -> sqrt(X * Y)
  // nnan disallows the possibility of returning a number if both operands are
  // negative (in that case, we should return NaN).
  if (I.hasNoNaNs() && match(Op0, m_OneUse(m_Sqrt(m_Value(X)))) &&
      match(Op1, m_OneUse(m_Sqrt(m_Value(Y))))) {
    Value *XY = Builder.CreateFMulFMF(X, Y, &I);
    Value *Sqrt = Builder.CreateUnaryIntrinsic(Intrinsic::sqrt, XY, &I);
    return replaceInstUsesWith(I, Sqrt);
  }

  // The following transforms are done irrespective of the number of uses
  // for the expression "1.0/sqrt(X)".
  //  1) 1.0/sqrt(X) * X -> X/sqrt(X)
  //  2) X * 1.0/sqrt(X) -> X/sqrt(X)
  // We always expect the backend to reduce X/sqrt(X) to sqrt(X), if it
  // has the necessary (reassoc) fast-math-flags.
  if (I.hasNoSignedZeros() &&
      match(Op0, (m_FDiv(m_SpecificFP(1.0), m_Value(Y)))) &&
      match(Y, m_Sqrt(m_Value(X))) && Op1 == X)
    return BinaryOperator::CreateFDivFMF(X, Y, &I);
  if (I.hasNoSignedZeros() &&
      match(Op1, (m_FDiv(m_SpecificFP(1.0), m_Value(Y)))) &&
      match(Y, m_Sqrt(m_Value(X))) && Op0 == X)
    return BinaryOperator::CreateFDivFMF(X, Y, &I);

  // Like the similar transform in instsimplify, this requires 'nsz' because
  // sqrt(-0.0) = -0.0, and -0.0 * -0.0 does not simplify to -0.0.
  if (I.hasNoNaNs() && I.hasNoSignedZeros() && Op0 == Op1 && Op0->hasNUses(2)) {
    // Peek through fdiv to find squaring of square root:
    // (X / sqrt(Y)) * (X / sqrt(Y)) --> (X * X) / Y
    if (match(Op0, m_FDiv(m_Value(X), m_Sqrt(m_Value(Y))))) {
      Value *XX = Builder.CreateFMulFMF(X, X, &I);
      return BinaryOperator::CreateFDivFMF(XX, Y, &I);
    }
    // (sqrt(Y) / X) * (sqrt(Y) / X) --> Y / (X * X)
    if (match(Op0, m_FDiv(m_Sqrt(m_Value(Y)), m_Value(X)))) {
      Value *XX = Builder.CreateFMulFMF(X, X, &I);
      return BinaryOperator::CreateFDivFMF(Y, XX, &I);
    }
  }

  // pow(X, Y) * X --> pow(X, Y+1)
  // X * pow(X, Y) --> pow(X, Y+1)
  if (match(&I, m_c_FMul(m_OneUse(m_Intrinsic<Intrinsic::pow>(m_Value(X),
                                                              m_Value(Y))),
                         m_Deferred(X)))) {
    Value *Y1 = Builder.CreateFAddFMF(Y, ConstantFP::get(I.getType(), 1.0), &I);
    Value *Pow = Builder.CreateBinaryIntrinsic(Intrinsic::pow, X, Y1, &I);
    return replaceInstUsesWith(I, Pow);
  }

  if (Instruction *FoldedPowi = foldPowiReassoc(I))
    return FoldedPowi;

  if (I.isOnlyUserOfAnyOperand()) {
    // pow(X, Y) * pow(X, Z) -> pow(X, Y + Z)
    if (match(Op0, m_Intrinsic<Intrinsic::pow>(m_Value(X), m_Value(Y))) &&
        match(Op1, m_Intrinsic<Intrinsic::pow>(m_Specific(X), m_Value(Z)))) {
      auto *YZ = Builder.CreateFAddFMF(Y, Z, &I);
      auto *NewPow = Builder.CreateBinaryIntrinsic(Intrinsic::pow, X, YZ, &I);
      return replaceInstUsesWith(I, NewPow);
    }
    // pow(X, Y) * pow(Z, Y) -> pow(X * Z, Y)
    if (match(Op0, m_Intrinsic<Intrinsic::pow>(m_Value(X), m_Value(Y))) &&
        match(Op1, m_Intrinsic<Intrinsic::pow>(m_Value(Z), m_Specific(Y)))) {
      auto *XZ = Builder.CreateFMulFMF(X, Z, &I);
      auto *NewPow = Builder.CreateBinaryIntrinsic(Intrinsic::pow, XZ, Y, &I);
      return replaceInstUsesWith(I, NewPow);
    }

    // exp(X) * exp(Y) -> exp(X + Y)
    if (match(Op0, m_Intrinsic<Intrinsic::exp>(m_Value(X))) &&
        match(Op1, m_Intrinsic<Intrinsic::exp>(m_Value(Y)))) {
      Value *XY = Builder.CreateFAddFMF(X, Y, &I);
      Value *Exp = Builder.CreateUnaryIntrinsic(Intrinsic::exp, XY, &I);
      return replaceInstUsesWith(I, Exp);
    }

    // exp2(X) * exp2(Y) -> exp2(X + Y)
    if (match(Op0, m_Intrinsic<Intrinsic::exp2>(m_Value(X))) &&
        match(Op1, m_Intrinsic<Intrinsic::exp2>(m_Value(Y)))) {
      Value *XY = Builder.CreateFAddFMF(X, Y, &I);
      Value *Exp2 = Builder.CreateUnaryIntrinsic(Intrinsic::exp2, XY, &I);
      return replaceInstUsesWith(I, Exp2);
    }
  }

  // (X*Y) * X => (X*X) * Y where Y != X
  //  The purpose is two-fold:
  //   1) to form a power expression (of X).
  //   2) potentially shorten the critical path: After transformation, the
  //  latency of the instruction Y is amortized by the expression of X*X,
  //  and therefore Y is in a "less critical" position compared to what it
  //  was before the transformation.
  if (match(Op0, m_OneUse(m_c_FMul(m_Specific(Op1), m_Value(Y)))) && Op1 != Y) {
    Value *XX = Builder.CreateFMulFMF(Op1, Op1, &I);
    return BinaryOperator::CreateFMulFMF(XX, Y, &I);
  }
  if (match(Op1, m_OneUse(m_c_FMul(m_Specific(Op0), m_Value(Y)))) && Op0 != Y) {
    Value *XX = Builder.CreateFMulFMF(Op0, Op0, &I);
    return BinaryOperator::CreateFMulFMF(XX, Y, &I);
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitFMul(BinaryOperator &I) {
  if (Value *V = simplifyFMulInst(I.getOperand(0), I.getOperand(1),
                                  I.getFastMathFlags(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  if (Instruction *FoldedMul = foldBinOpIntoSelectOrPhi(I))
    return FoldedMul;

  if (Value *FoldedMul = foldMulSelectToNegate(I, Builder))
    return replaceInstUsesWith(I, FoldedMul);

  if (Instruction *R = foldFPSignBitOps(I))
    return R;

  if (Instruction *R = foldFBinOpOfIntCasts(I))
    return R;

  // X * -1.0 --> -X
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  if (match(Op1, m_SpecificFP(-1.0)))
    return UnaryOperator::CreateFNegFMF(Op0, &I);

  // With no-nans/no-infs:
  // X * 0.0 --> copysign(0.0, X)
  // X * -0.0 --> copysign(0.0, -X)
  const APFloat *FPC;
  if (match(Op1, m_APFloatAllowPoison(FPC)) && FPC->isZero() &&
      ((I.hasNoInfs() &&
        isKnownNeverNaN(Op0, /*Depth=*/0, SQ.getWithInstruction(&I))) ||
       isKnownNeverNaN(&I, /*Depth=*/0, SQ.getWithInstruction(&I)))) {
    if (FPC->isNegative())
      Op0 = Builder.CreateFNegFMF(Op0, &I);
    CallInst *CopySign = Builder.CreateIntrinsic(Intrinsic::copysign,
                                                 {I.getType()}, {Op1, Op0}, &I);
    return replaceInstUsesWith(I, CopySign);
  }

  // -X * C --> X * -C
  Value *X, *Y;
  Constant *C;
  if (match(Op0, m_FNeg(m_Value(X))) && match(Op1, m_Constant(C)))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
      return BinaryOperator::CreateFMulFMF(X, NegC, &I);

  if (I.hasNoNaNs() && I.hasNoSignedZeros()) {
    // (uitofp bool X) * Y --> X ? Y : 0
    // Y * (uitofp bool X) --> X ? Y : 0
    // Note INF * 0 is NaN.
    if (match(Op0, m_UIToFP(m_Value(X))) &&
        X->getType()->isIntOrIntVectorTy(1)) {
      auto *SI = SelectInst::Create(X, Op1, ConstantFP::get(I.getType(), 0.0));
      SI->copyFastMathFlags(I.getFastMathFlags());
      return SI;
    }
    if (match(Op1, m_UIToFP(m_Value(X))) &&
        X->getType()->isIntOrIntVectorTy(1)) {
      auto *SI = SelectInst::Create(X, Op0, ConstantFP::get(I.getType(), 0.0));
      SI->copyFastMathFlags(I.getFastMathFlags());
      return SI;
    }
  }

  // (select A, B, C) * (select A, D, E) --> select A, (B*D), (C*E)
  if (Value *V = SimplifySelectsFeedingBinaryOp(I, Op0, Op1))
    return replaceInstUsesWith(I, V);

  if (I.hasAllowReassoc())
    if (Instruction *FoldedMul = foldFMulReassoc(I))
      return FoldedMul;

  // log2(X * 0.5) * Y = log2(X) * Y - Y
  if (I.isFast()) {
    IntrinsicInst *Log2 = nullptr;
    if (match(Op0, m_OneUse(m_Intrinsic<Intrinsic::log2>(
            m_OneUse(m_FMul(m_Value(X), m_SpecificFP(0.5))))))) {
      Log2 = cast<IntrinsicInst>(Op0);
      Y = Op1;
    }
    if (match(Op1, m_OneUse(m_Intrinsic<Intrinsic::log2>(
            m_OneUse(m_FMul(m_Value(X), m_SpecificFP(0.5))))))) {
      Log2 = cast<IntrinsicInst>(Op1);
      Y = Op0;
    }
    if (Log2) {
      Value *Log2 = Builder.CreateUnaryIntrinsic(Intrinsic::log2, X, &I);
      Value *LogXTimesY = Builder.CreateFMulFMF(Log2, Y, &I);
      return BinaryOperator::CreateFSubFMF(LogXTimesY, Y, &I);
    }
  }

  // Simplify FMUL recurrences starting with 0.0 to 0.0 if nnan and nsz are set.
  // Given a phi node with entry value as 0 and it used in fmul operation,
  // we can replace fmul with 0 safely and eleminate loop operation.
  PHINode *PN = nullptr;
  Value *Start = nullptr, *Step = nullptr;
  if (matchSimpleRecurrence(&I, PN, Start, Step) && I.hasNoNaNs() &&
      I.hasNoSignedZeros() && match(Start, m_Zero()))
    return replaceInstUsesWith(I, Start);

  // minimum(X, Y) * maximum(X, Y) => X * Y.
  if (match(&I,
            m_c_FMul(m_Intrinsic<Intrinsic::maximum>(m_Value(X), m_Value(Y)),
                     m_c_Intrinsic<Intrinsic::minimum>(m_Deferred(X),
                                                       m_Deferred(Y))))) {
    BinaryOperator *Result = BinaryOperator::CreateFMulFMF(X, Y, &I);
    // We cannot preserve ninf if nnan flag is not set.
    // If X is NaN and Y is Inf then in original program we had NaN * NaN,
    // while in optimized version NaN * Inf and this is a poison with ninf flag.
    if (!Result->hasNoNaNs())
      Result->setHasNoInfs(false);
    return Result;
  }

  return nullptr;
}

/// Fold a divide or remainder with a select instruction divisor when one of the
/// select operands is zero. In that case, we can use the other select operand
/// because div/rem by zero is undefined.
bool InstCombinerImpl::simplifyDivRemOfSelectWithZeroOp(BinaryOperator &I) {
  SelectInst *SI = dyn_cast<SelectInst>(I.getOperand(1));
  if (!SI)
    return false;

  int NonNullOperand;
  if (match(SI->getTrueValue(), m_Zero()))
    // div/rem X, (Cond ? 0 : Y) -> div/rem X, Y
    NonNullOperand = 2;
  else if (match(SI->getFalseValue(), m_Zero()))
    // div/rem X, (Cond ? Y : 0) -> div/rem X, Y
    NonNullOperand = 1;
  else
    return false;

  // Change the div/rem to use 'Y' instead of the select.
  replaceOperand(I, 1, SI->getOperand(NonNullOperand));

  // Okay, we know we replace the operand of the div/rem with 'Y' with no
  // problem.  However, the select, or the condition of the select may have
  // multiple uses.  Based on our knowledge that the operand must be non-zero,
  // propagate the known value for the select into other uses of it, and
  // propagate a known value of the condition into its other users.

  // If the select and condition only have a single use, don't bother with this,
  // early exit.
  Value *SelectCond = SI->getCondition();
  if (SI->use_empty() && SelectCond->hasOneUse())
    return true;

  // Scan the current block backward, looking for other uses of SI.
  BasicBlock::iterator BBI = I.getIterator(), BBFront = I.getParent()->begin();
  Type *CondTy = SelectCond->getType();
  while (BBI != BBFront) {
    --BBI;
    // If we found an instruction that we can't assume will return, so
    // information from below it cannot be propagated above it.
    if (!isGuaranteedToTransferExecutionToSuccessor(&*BBI))
      break;

    // Replace uses of the select or its condition with the known values.
    for (Use &Op : BBI->operands()) {
      if (Op == SI) {
        replaceUse(Op, SI->getOperand(NonNullOperand));
        Worklist.push(&*BBI);
      } else if (Op == SelectCond) {
        replaceUse(Op, NonNullOperand == 1 ? ConstantInt::getTrue(CondTy)
                                           : ConstantInt::getFalse(CondTy));
        Worklist.push(&*BBI);
      }
    }

    // If we past the instruction, quit looking for it.
    if (&*BBI == SI)
      SI = nullptr;
    if (&*BBI == SelectCond)
      SelectCond = nullptr;

    // If we ran out of things to eliminate, break out of the loop.
    if (!SelectCond && !SI)
      break;

  }
  return true;
}

/// True if the multiply can not be expressed in an int this size.
static bool multiplyOverflows(const APInt &C1, const APInt &C2, APInt &Product,
                              bool IsSigned) {
  bool Overflow;
  Product = IsSigned ? C1.smul_ov(C2, Overflow) : C1.umul_ov(C2, Overflow);
  return Overflow;
}

/// True if C1 is a multiple of C2. Quotient contains C1/C2.
static bool isMultiple(const APInt &C1, const APInt &C2, APInt &Quotient,
                       bool IsSigned) {
  assert(C1.getBitWidth() == C2.getBitWidth() && "Constant widths not equal");

  // Bail if we will divide by zero.
  if (C2.isZero())
    return false;

  // Bail if we would divide INT_MIN by -1.
  if (IsSigned && C1.isMinSignedValue() && C2.isAllOnes())
    return false;

  APInt Remainder(C1.getBitWidth(), /*val=*/0ULL, IsSigned);
  if (IsSigned)
    APInt::sdivrem(C1, C2, Quotient, Remainder);
  else
    APInt::udivrem(C1, C2, Quotient, Remainder);

  return Remainder.isMinValue();
}

static Value *foldIDivShl(BinaryOperator &I, InstCombiner::BuilderTy &Builder) {
  assert((I.getOpcode() == Instruction::SDiv ||
          I.getOpcode() == Instruction::UDiv) &&
         "Expected integer divide");

  bool IsSigned = I.getOpcode() == Instruction::SDiv;
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();

  Value *X, *Y, *Z;

  // With appropriate no-wrap constraints, remove a common factor in the
  // dividend and divisor that is disguised as a left-shifted value.
  if (match(Op1, m_Shl(m_Value(X), m_Value(Z))) &&
      match(Op0, m_c_Mul(m_Specific(X), m_Value(Y)))) {
    // Both operands must have the matching no-wrap for this kind of division.
    auto *Mul = cast<OverflowingBinaryOperator>(Op0);
    auto *Shl = cast<OverflowingBinaryOperator>(Op1);
    bool HasNUW = Mul->hasNoUnsignedWrap() && Shl->hasNoUnsignedWrap();
    bool HasNSW = Mul->hasNoSignedWrap() && Shl->hasNoSignedWrap();

    // (X * Y) u/ (X << Z) --> Y u>> Z
    if (!IsSigned && HasNUW)
      return Builder.CreateLShr(Y, Z, "", I.isExact());

    // (X * Y) s/ (X << Z) --> Y s/ (1 << Z)
    if (IsSigned && HasNSW && (Op0->hasOneUse() || Op1->hasOneUse())) {
      Value *Shl = Builder.CreateShl(ConstantInt::get(Ty, 1), Z);
      return Builder.CreateSDiv(Y, Shl, "", I.isExact());
    }
  }

  // With appropriate no-wrap constraints, remove a common factor in the
  // dividend and divisor that is disguised as a left-shift amount.
  if (match(Op0, m_Shl(m_Value(X), m_Value(Z))) &&
      match(Op1, m_Shl(m_Value(Y), m_Specific(Z)))) {
    auto *Shl0 = cast<OverflowingBinaryOperator>(Op0);
    auto *Shl1 = cast<OverflowingBinaryOperator>(Op1);

    // For unsigned div, we need 'nuw' on both shifts or
    // 'nsw' on both shifts + 'nuw' on the dividend.
    // (X << Z) / (Y << Z) --> X / Y
    if (!IsSigned &&
        ((Shl0->hasNoUnsignedWrap() && Shl1->hasNoUnsignedWrap()) ||
         (Shl0->hasNoUnsignedWrap() && Shl0->hasNoSignedWrap() &&
          Shl1->hasNoSignedWrap())))
      return Builder.CreateUDiv(X, Y, "", I.isExact());

    // For signed div, we need 'nsw' on both shifts + 'nuw' on the divisor.
    // (X << Z) / (Y << Z) --> X / Y
    if (IsSigned && Shl0->hasNoSignedWrap() && Shl1->hasNoSignedWrap() &&
        Shl1->hasNoUnsignedWrap())
      return Builder.CreateSDiv(X, Y, "", I.isExact());
  }

  // If X << Y and X << Z does not overflow, then:
  // (X << Y) / (X << Z) -> (1 << Y) / (1 << Z) -> 1 << Y >> Z
  if (match(Op0, m_Shl(m_Value(X), m_Value(Y))) &&
      match(Op1, m_Shl(m_Specific(X), m_Value(Z)))) {
    auto *Shl0 = cast<OverflowingBinaryOperator>(Op0);
    auto *Shl1 = cast<OverflowingBinaryOperator>(Op1);

    if (IsSigned ? (Shl0->hasNoSignedWrap() && Shl1->hasNoSignedWrap())
                 : (Shl0->hasNoUnsignedWrap() && Shl1->hasNoUnsignedWrap())) {
      Constant *One = ConstantInt::get(X->getType(), 1);
      // Only preserve the nsw flag if dividend has nsw
      // or divisor has nsw and operator is sdiv.
      Value *Dividend = Builder.CreateShl(
          One, Y, "shl.dividend",
          /*HasNUW*/ true,
          /*HasNSW*/
          IsSigned ? (Shl0->hasNoUnsignedWrap() || Shl1->hasNoUnsignedWrap())
                   : Shl0->hasNoSignedWrap());
      return Builder.CreateLShr(Dividend, Z, "", I.isExact());
    }
  }

  return nullptr;
}

/// This function implements the transforms common to both integer division
/// instructions (udiv and sdiv). It is called by the visitors to those integer
/// division instructions.
/// Common integer divide transforms
Instruction *InstCombinerImpl::commonIDivTransforms(BinaryOperator &I) {
  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  bool IsSigned = I.getOpcode() == Instruction::SDiv;
  Type *Ty = I.getType();

  // The RHS is known non-zero.
  if (Value *V = simplifyValueKnownNonZero(I.getOperand(1), *this, I))
    return replaceOperand(I, 1, V);

  // Handle cases involving: [su]div X, (select Cond, Y, Z)
  // This does not apply for fdiv.
  if (simplifyDivRemOfSelectWithZeroOp(I))
    return &I;

  // If the divisor is a select-of-constants, try to constant fold all div ops:
  // C / (select Cond, TrueC, FalseC) --> select Cond, (C / TrueC), (C / FalseC)
  // TODO: Adapt simplifyDivRemOfSelectWithZeroOp to allow this and other folds.
  if (match(Op0, m_ImmConstant()) &&
      match(Op1, m_Select(m_Value(), m_ImmConstant(), m_ImmConstant()))) {
    if (Instruction *R = FoldOpIntoSelect(I, cast<SelectInst>(Op1),
                                          /*FoldWithMultiUse*/ true))
      return R;
  }

  const APInt *C2;
  if (match(Op1, m_APInt(C2))) {
    Value *X;
    const APInt *C1;

    // (X / C1) / C2  -> X / (C1*C2)
    if ((IsSigned && match(Op0, m_SDiv(m_Value(X), m_APInt(C1)))) ||
        (!IsSigned && match(Op0, m_UDiv(m_Value(X), m_APInt(C1))))) {
      APInt Product(C1->getBitWidth(), /*val=*/0ULL, IsSigned);
      if (!multiplyOverflows(*C1, *C2, Product, IsSigned))
        return BinaryOperator::Create(I.getOpcode(), X,
                                      ConstantInt::get(Ty, Product));
    }

    APInt Quotient(C2->getBitWidth(), /*val=*/0ULL, IsSigned);
    if ((IsSigned && match(Op0, m_NSWMul(m_Value(X), m_APInt(C1)))) ||
        (!IsSigned && match(Op0, m_NUWMul(m_Value(X), m_APInt(C1))))) {

      // (X * C1) / C2 -> X / (C2 / C1) if C2 is a multiple of C1.
      if (isMultiple(*C2, *C1, Quotient, IsSigned)) {
        auto *NewDiv = BinaryOperator::Create(I.getOpcode(), X,
                                              ConstantInt::get(Ty, Quotient));
        NewDiv->setIsExact(I.isExact());
        return NewDiv;
      }

      // (X * C1) / C2 -> X * (C1 / C2) if C1 is a multiple of C2.
      if (isMultiple(*C1, *C2, Quotient, IsSigned)) {
        auto *Mul = BinaryOperator::Create(Instruction::Mul, X,
                                           ConstantInt::get(Ty, Quotient));
        auto *OBO = cast<OverflowingBinaryOperator>(Op0);
        Mul->setHasNoUnsignedWrap(!IsSigned && OBO->hasNoUnsignedWrap());
        Mul->setHasNoSignedWrap(OBO->hasNoSignedWrap());
        return Mul;
      }
    }

    if ((IsSigned && match(Op0, m_NSWShl(m_Value(X), m_APInt(C1))) &&
         C1->ult(C1->getBitWidth() - 1)) ||
        (!IsSigned && match(Op0, m_NUWShl(m_Value(X), m_APInt(C1))) &&
         C1->ult(C1->getBitWidth()))) {
      APInt C1Shifted = APInt::getOneBitSet(
          C1->getBitWidth(), static_cast<unsigned>(C1->getZExtValue()));

      // (X << C1) / C2 -> X / (C2 >> C1) if C2 is a multiple of 1 << C1.
      if (isMultiple(*C2, C1Shifted, Quotient, IsSigned)) {
        auto *BO = BinaryOperator::Create(I.getOpcode(), X,
                                          ConstantInt::get(Ty, Quotient));
        BO->setIsExact(I.isExact());
        return BO;
      }

      // (X << C1) / C2 -> X * ((1 << C1) / C2) if 1 << C1 is a multiple of C2.
      if (isMultiple(C1Shifted, *C2, Quotient, IsSigned)) {
        auto *Mul = BinaryOperator::Create(Instruction::Mul, X,
                                           ConstantInt::get(Ty, Quotient));
        auto *OBO = cast<OverflowingBinaryOperator>(Op0);
        Mul->setHasNoUnsignedWrap(!IsSigned && OBO->hasNoUnsignedWrap());
        Mul->setHasNoSignedWrap(OBO->hasNoSignedWrap());
        return Mul;
      }
    }

    // Distribute div over add to eliminate a matching div/mul pair:
    // ((X * C2) + C1) / C2 --> X + C1/C2
    // We need a multiple of the divisor for a signed add constant, but
    // unsigned is fine with any constant pair.
    if (IsSigned &&
        match(Op0, m_NSWAddLike(m_NSWMul(m_Value(X), m_SpecificInt(*C2)),
                                m_APInt(C1))) &&
        isMultiple(*C1, *C2, Quotient, IsSigned)) {
      return BinaryOperator::CreateNSWAdd(X, ConstantInt::get(Ty, Quotient));
    }
    if (!IsSigned &&
        match(Op0, m_NUWAddLike(m_NUWMul(m_Value(X), m_SpecificInt(*C2)),
                                m_APInt(C1)))) {
      return BinaryOperator::CreateNUWAdd(X,
                                          ConstantInt::get(Ty, C1->udiv(*C2)));
    }

    if (!C2->isZero()) // avoid X udiv 0
      if (Instruction *FoldedDiv = foldBinOpIntoSelectOrPhi(I))
        return FoldedDiv;
  }

  if (match(Op0, m_One())) {
    assert(!Ty->isIntOrIntVectorTy(1) && "i1 divide not removed?");
    if (IsSigned) {
      // 1 / 0 --> undef ; 1 / 1 --> 1 ; 1 / -1 --> -1 ; 1 / anything else --> 0
      // (Op1 + 1) u< 3 ? Op1 : 0
      // Op1 must be frozen because we are increasing its number of uses.
      Value *F1 = Op1;
      if (!isGuaranteedNotToBeUndef(Op1))
        F1 = Builder.CreateFreeze(Op1, Op1->getName() + ".fr");
      Value *Inc = Builder.CreateAdd(F1, Op0);
      Value *Cmp = Builder.CreateICmpULT(Inc, ConstantInt::get(Ty, 3));
      return SelectInst::Create(Cmp, F1, ConstantInt::get(Ty, 0));
    } else {
      // If Op1 is 0 then it's undefined behaviour. If Op1 is 1 then the
      // result is one, otherwise it's zero.
      return new ZExtInst(Builder.CreateICmpEQ(Op1, Op0), Ty);
    }
  }

  // See if we can fold away this div instruction.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  // (X - (X rem Y)) / Y -> X / Y; usually originates as ((X / Y) * Y) / Y
  Value *X, *Z;
  if (match(Op0, m_Sub(m_Value(X), m_Value(Z)))) // (X - Z) / Y; Y = Op1
    if ((IsSigned && match(Z, m_SRem(m_Specific(X), m_Specific(Op1)))) ||
        (!IsSigned && match(Z, m_URem(m_Specific(X), m_Specific(Op1)))))
      return BinaryOperator::Create(I.getOpcode(), X, Op1);

  // (X << Y) / X -> 1 << Y
  Value *Y;
  if (IsSigned && match(Op0, m_NSWShl(m_Specific(Op1), m_Value(Y))))
    return BinaryOperator::CreateNSWShl(ConstantInt::get(Ty, 1), Y);
  if (!IsSigned && match(Op0, m_NUWShl(m_Specific(Op1), m_Value(Y))))
    return BinaryOperator::CreateNUWShl(ConstantInt::get(Ty, 1), Y);

  // X / (X * Y) -> 1 / Y if the multiplication does not overflow.
  if (match(Op1, m_c_Mul(m_Specific(Op0), m_Value(Y)))) {
    bool HasNSW = cast<OverflowingBinaryOperator>(Op1)->hasNoSignedWrap();
    bool HasNUW = cast<OverflowingBinaryOperator>(Op1)->hasNoUnsignedWrap();
    if ((IsSigned && HasNSW) || (!IsSigned && HasNUW)) {
      replaceOperand(I, 0, ConstantInt::get(Ty, 1));
      replaceOperand(I, 1, Y);
      return &I;
    }
  }

  // (X << Z) / (X * Y) -> (1 << Z) / Y
  // TODO: Handle sdiv.
  if (!IsSigned && Op1->hasOneUse() &&
      match(Op0, m_NUWShl(m_Value(X), m_Value(Z))) &&
      match(Op1, m_c_Mul(m_Specific(X), m_Value(Y))))
    if (cast<OverflowingBinaryOperator>(Op1)->hasNoUnsignedWrap()) {
      Instruction *NewDiv = BinaryOperator::CreateUDiv(
          Builder.CreateShl(ConstantInt::get(Ty, 1), Z, "", /*NUW*/ true), Y);
      NewDiv->setIsExact(I.isExact());
      return NewDiv;
    }

  if (Value *R = foldIDivShl(I, Builder))
    return replaceInstUsesWith(I, R);

  // With the appropriate no-wrap constraint, remove a multiply by the divisor
  // after peeking through another divide:
  // ((Op1 * X) / Y) / Op1 --> X / Y
  if (match(Op0, m_BinOp(I.getOpcode(), m_c_Mul(m_Specific(Op1), m_Value(X)),
                         m_Value(Y)))) {
    auto *InnerDiv = cast<PossiblyExactOperator>(Op0);
    auto *Mul = cast<OverflowingBinaryOperator>(InnerDiv->getOperand(0));
    Instruction *NewDiv = nullptr;
    if (!IsSigned && Mul->hasNoUnsignedWrap())
      NewDiv = BinaryOperator::CreateUDiv(X, Y);
    else if (IsSigned && Mul->hasNoSignedWrap())
      NewDiv = BinaryOperator::CreateSDiv(X, Y);

    // Exact propagates only if both of the original divides are exact.
    if (NewDiv) {
      NewDiv->setIsExact(I.isExact() && InnerDiv->isExact());
      return NewDiv;
    }
  }

  // (X * Y) / (X * Z) --> Y / Z (and commuted variants)
  if (match(Op0, m_Mul(m_Value(X), m_Value(Y)))) {
    auto OB0HasNSW = cast<OverflowingBinaryOperator>(Op0)->hasNoSignedWrap();
    auto OB0HasNUW = cast<OverflowingBinaryOperator>(Op0)->hasNoUnsignedWrap();

    auto CreateDivOrNull = [&](Value *A, Value *B) -> Instruction * {
      auto OB1HasNSW = cast<OverflowingBinaryOperator>(Op1)->hasNoSignedWrap();
      auto OB1HasNUW =
          cast<OverflowingBinaryOperator>(Op1)->hasNoUnsignedWrap();
      const APInt *C1, *C2;
      if (IsSigned && OB0HasNSW) {
        if (OB1HasNSW && match(B, m_APInt(C1)) && !C1->isAllOnes())
          return BinaryOperator::CreateSDiv(A, B);
      }
      if (!IsSigned && OB0HasNUW) {
        if (OB1HasNUW)
          return BinaryOperator::CreateUDiv(A, B);
        if (match(A, m_APInt(C1)) && match(B, m_APInt(C2)) && C2->ule(*C1))
          return BinaryOperator::CreateUDiv(A, B);
      }
      return nullptr;
    };

    if (match(Op1, m_c_Mul(m_Specific(X), m_Value(Z)))) {
      if (auto *Val = CreateDivOrNull(Y, Z))
        return Val;
    }
    if (match(Op1, m_c_Mul(m_Specific(Y), m_Value(Z)))) {
      if (auto *Val = CreateDivOrNull(X, Z))
        return Val;
    }
  }
  return nullptr;
}

static const unsigned MaxDepth = 6;

// Take the exact integer log2 of the value. If DoFold is true, create the
// actual instructions, otherwise return a non-null dummy value. Return nullptr
// on failure.
static Value *takeLog2(IRBuilderBase &Builder, Value *Op, unsigned Depth,
                       bool AssumeNonZero, bool DoFold) {
  auto IfFold = [DoFold](function_ref<Value *()> Fn) {
    if (!DoFold)
      return reinterpret_cast<Value *>(-1);
    return Fn();
  };

  // FIXME: assert that Op1 isn't/doesn't contain undef.

  // log2(2^C) -> C
  if (match(Op, m_Power2()))
    return IfFold([&]() {
      Constant *C = ConstantExpr::getExactLogBase2(cast<Constant>(Op));
      if (!C)
        llvm_unreachable("Failed to constant fold udiv -> logbase2");
      return C;
    });

  // The remaining tests are all recursive, so bail out if we hit the limit.
  if (Depth++ == MaxDepth)
    return nullptr;

  // log2(zext X) -> zext log2(X)
  // FIXME: Require one use?
  Value *X, *Y;
  if (match(Op, m_ZExt(m_Value(X))))
    if (Value *LogX = takeLog2(Builder, X, Depth, AssumeNonZero, DoFold))
      return IfFold([&]() { return Builder.CreateZExt(LogX, Op->getType()); });

  // log2(X << Y) -> log2(X) + Y
  // FIXME: Require one use unless X is 1?
  if (match(Op, m_Shl(m_Value(X), m_Value(Y)))) {
    auto *BO = cast<OverflowingBinaryOperator>(Op);
    // nuw will be set if the `shl` is trivially non-zero.
    if (AssumeNonZero || BO->hasNoUnsignedWrap() || BO->hasNoSignedWrap())
      if (Value *LogX = takeLog2(Builder, X, Depth, AssumeNonZero, DoFold))
        return IfFold([&]() { return Builder.CreateAdd(LogX, Y); });
  }

  // log2(Cond ? X : Y) -> Cond ? log2(X) : log2(Y)
  // FIXME: Require one use?
  if (SelectInst *SI = dyn_cast<SelectInst>(Op))
    if (Value *LogX = takeLog2(Builder, SI->getOperand(1), Depth,
                               AssumeNonZero, DoFold))
      if (Value *LogY = takeLog2(Builder, SI->getOperand(2), Depth,
                                 AssumeNonZero, DoFold))
        return IfFold([&]() {
          return Builder.CreateSelect(SI->getOperand(0), LogX, LogY);
        });

  // log2(umin(X, Y)) -> umin(log2(X), log2(Y))
  // log2(umax(X, Y)) -> umax(log2(X), log2(Y))
  auto *MinMax = dyn_cast<MinMaxIntrinsic>(Op);
  if (MinMax && MinMax->hasOneUse() && !MinMax->isSigned()) {
    // Use AssumeNonZero as false here. Otherwise we can hit case where
    // log2(umax(X, Y)) != umax(log2(X), log2(Y)) (because overflow).
    if (Value *LogX = takeLog2(Builder, MinMax->getLHS(), Depth,
                               /*AssumeNonZero*/ false, DoFold))
      if (Value *LogY = takeLog2(Builder, MinMax->getRHS(), Depth,
                                 /*AssumeNonZero*/ false, DoFold))
        return IfFold([&]() {
          return Builder.CreateBinaryIntrinsic(MinMax->getIntrinsicID(), LogX,
                                               LogY);
        });
  }

  return nullptr;
}

/// If we have zero-extended operands of an unsigned div or rem, we may be able
/// to narrow the operation (sink the zext below the math).
static Instruction *narrowUDivURem(BinaryOperator &I,
                                   InstCombinerImpl &IC) {
  Instruction::BinaryOps Opcode = I.getOpcode();
  Value *N = I.getOperand(0);
  Value *D = I.getOperand(1);
  Type *Ty = I.getType();
  Value *X, *Y;
  if (match(N, m_ZExt(m_Value(X))) && match(D, m_ZExt(m_Value(Y))) &&
      X->getType() == Y->getType() && (N->hasOneUse() || D->hasOneUse())) {
    // udiv (zext X), (zext Y) --> zext (udiv X, Y)
    // urem (zext X), (zext Y) --> zext (urem X, Y)
    Value *NarrowOp = IC.Builder.CreateBinOp(Opcode, X, Y);
    return new ZExtInst(NarrowOp, Ty);
  }

  Constant *C;
  if (isa<Instruction>(N) && match(N, m_OneUse(m_ZExt(m_Value(X)))) &&
      match(D, m_Constant(C))) {
    // If the constant is the same in the smaller type, use the narrow version.
    Constant *TruncC = IC.getLosslessUnsignedTrunc(C, X->getType());
    if (!TruncC)
      return nullptr;

    // udiv (zext X), C --> zext (udiv X, C')
    // urem (zext X), C --> zext (urem X, C')
    return new ZExtInst(IC.Builder.CreateBinOp(Opcode, X, TruncC), Ty);
  }
  if (isa<Instruction>(D) && match(D, m_OneUse(m_ZExt(m_Value(X)))) &&
      match(N, m_Constant(C))) {
    // If the constant is the same in the smaller type, use the narrow version.
    Constant *TruncC = IC.getLosslessUnsignedTrunc(C, X->getType());
    if (!TruncC)
      return nullptr;

    // udiv C, (zext X) --> zext (udiv C', X)
    // urem C, (zext X) --> zext (urem C', X)
    return new ZExtInst(IC.Builder.CreateBinOp(Opcode, TruncC, X), Ty);
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitUDiv(BinaryOperator &I) {
  if (Value *V = simplifyUDivInst(I.getOperand(0), I.getOperand(1), I.isExact(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  // Handle the integer div common cases
  if (Instruction *Common = commonIDivTransforms(I))
    return Common;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Value *X;
  const APInt *C1, *C2;
  if (match(Op0, m_LShr(m_Value(X), m_APInt(C1))) && match(Op1, m_APInt(C2))) {
    // (X lshr C1) udiv C2 --> X udiv (C2 << C1)
    bool Overflow;
    APInt C2ShlC1 = C2->ushl_ov(*C1, Overflow);
    if (!Overflow) {
      bool IsExact = I.isExact() && match(Op0, m_Exact(m_Value()));
      BinaryOperator *BO = BinaryOperator::CreateUDiv(
          X, ConstantInt::get(X->getType(), C2ShlC1));
      if (IsExact)
        BO->setIsExact();
      return BO;
    }
  }

  // Op0 / C where C is large (negative) --> zext (Op0 >= C)
  // TODO: Could use isKnownNegative() to handle non-constant values.
  Type *Ty = I.getType();
  if (match(Op1, m_Negative())) {
    Value *Cmp = Builder.CreateICmpUGE(Op0, Op1);
    return CastInst::CreateZExtOrBitCast(Cmp, Ty);
  }
  // Op0 / (sext i1 X) --> zext (Op0 == -1) (if X is 0, the div is undefined)
  if (match(Op1, m_SExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1)) {
    Value *Cmp = Builder.CreateICmpEQ(Op0, ConstantInt::getAllOnesValue(Ty));
    return CastInst::CreateZExtOrBitCast(Cmp, Ty);
  }

  if (Instruction *NarrowDiv = narrowUDivURem(I, *this))
    return NarrowDiv;

  Value *A, *B;

  // Look through a right-shift to find the common factor:
  // ((Op1 *nuw A) >> B) / Op1 --> A >> B
  if (match(Op0, m_LShr(m_NUWMul(m_Specific(Op1), m_Value(A)), m_Value(B))) ||
      match(Op0, m_LShr(m_NUWMul(m_Value(A), m_Specific(Op1)), m_Value(B)))) {
    Instruction *Lshr = BinaryOperator::CreateLShr(A, B);
    if (I.isExact() && cast<PossiblyExactOperator>(Op0)->isExact())
      Lshr->setIsExact();
    return Lshr;
  }

  // Op1 udiv Op2 -> Op1 lshr log2(Op2), if log2() folds away.
  if (takeLog2(Builder, Op1, /*Depth*/ 0, /*AssumeNonZero*/ true,
               /*DoFold*/ false)) {
    Value *Res = takeLog2(Builder, Op1, /*Depth*/ 0,
                          /*AssumeNonZero*/ true, /*DoFold*/ true);
    return replaceInstUsesWith(
        I, Builder.CreateLShr(Op0, Res, I.getName(), I.isExact()));
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitSDiv(BinaryOperator &I) {
  if (Value *V = simplifySDivInst(I.getOperand(0), I.getOperand(1), I.isExact(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  // Handle the integer div common cases
  if (Instruction *Common = commonIDivTransforms(I))
    return Common;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  Value *X;
  // sdiv Op0, -1 --> -Op0
  // sdiv Op0, (sext i1 X) --> -Op0 (because if X is 0, the op is undefined)
  if (match(Op1, m_AllOnes()) ||
      (match(Op1, m_SExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1)))
    return BinaryOperator::CreateNSWNeg(Op0);

  // X / INT_MIN --> X == INT_MIN
  if (match(Op1, m_SignMask()))
    return new ZExtInst(Builder.CreateICmpEQ(Op0, Op1), Ty);

  if (I.isExact()) {
    // sdiv exact X, 1<<C --> ashr exact X, C   iff  1<<C  is non-negative
    if (match(Op1, m_Power2()) && match(Op1, m_NonNegative())) {
      Constant *C = ConstantExpr::getExactLogBase2(cast<Constant>(Op1));
      return BinaryOperator::CreateExactAShr(Op0, C);
    }

    // sdiv exact X, (1<<ShAmt) --> ashr exact X, ShAmt (if shl is non-negative)
    Value *ShAmt;
    if (match(Op1, m_NSWShl(m_One(), m_Value(ShAmt))))
      return BinaryOperator::CreateExactAShr(Op0, ShAmt);

    // sdiv exact X, -1<<C --> -(ashr exact X, C)
    if (match(Op1, m_NegatedPower2())) {
      Constant *NegPow2C = ConstantExpr::getNeg(cast<Constant>(Op1));
      Constant *C = ConstantExpr::getExactLogBase2(NegPow2C);
      Value *Ashr = Builder.CreateAShr(Op0, C, I.getName() + ".neg", true);
      return BinaryOperator::CreateNSWNeg(Ashr);
    }
  }

  const APInt *Op1C;
  if (match(Op1, m_APInt(Op1C))) {
    // If the dividend is sign-extended and the constant divisor is small enough
    // to fit in the source type, shrink the division to the narrower type:
    // (sext X) sdiv C --> sext (X sdiv C)
    Value *Op0Src;
    if (match(Op0, m_OneUse(m_SExt(m_Value(Op0Src)))) &&
        Op0Src->getType()->getScalarSizeInBits() >=
            Op1C->getSignificantBits()) {

      // In the general case, we need to make sure that the dividend is not the
      // minimum signed value because dividing that by -1 is UB. But here, we
      // know that the -1 divisor case is already handled above.

      Constant *NarrowDivisor =
          ConstantExpr::getTrunc(cast<Constant>(Op1), Op0Src->getType());
      Value *NarrowOp = Builder.CreateSDiv(Op0Src, NarrowDivisor);
      return new SExtInst(NarrowOp, Ty);
    }

    // -X / C --> X / -C (if the negation doesn't overflow).
    // TODO: This could be enhanced to handle arbitrary vector constants by
    //       checking if all elements are not the min-signed-val.
    if (!Op1C->isMinSignedValue() && match(Op0, m_NSWNeg(m_Value(X)))) {
      Constant *NegC = ConstantInt::get(Ty, -(*Op1C));
      Instruction *BO = BinaryOperator::CreateSDiv(X, NegC);
      BO->setIsExact(I.isExact());
      return BO;
    }
  }

  // -X / Y --> -(X / Y)
  Value *Y;
  if (match(&I, m_SDiv(m_OneUse(m_NSWNeg(m_Value(X))), m_Value(Y))))
    return BinaryOperator::CreateNSWNeg(
        Builder.CreateSDiv(X, Y, I.getName(), I.isExact()));

  // abs(X) / X --> X > -1 ? 1 : -1
  // X / abs(X) --> X > -1 ? 1 : -1
  if (match(&I, m_c_BinOp(
                    m_OneUse(m_Intrinsic<Intrinsic::abs>(m_Value(X), m_One())),
                    m_Deferred(X)))) {
    Value *Cond = Builder.CreateIsNotNeg(X);
    return SelectInst::Create(Cond, ConstantInt::get(Ty, 1),
                              ConstantInt::getAllOnesValue(Ty));
  }

  KnownBits KnownDividend = computeKnownBits(Op0, 0, &I);
  if (!I.isExact() &&
      (match(Op1, m_Power2(Op1C)) || match(Op1, m_NegatedPower2(Op1C))) &&
      KnownDividend.countMinTrailingZeros() >= Op1C->countr_zero()) {
    I.setIsExact();
    return &I;
  }

  if (KnownDividend.isNonNegative()) {
    // If both operands are unsigned, turn this into a udiv.
    if (isKnownNonNegative(Op1, SQ.getWithInstruction(&I))) {
      auto *BO = BinaryOperator::CreateUDiv(Op0, Op1, I.getName());
      BO->setIsExact(I.isExact());
      return BO;
    }

    if (match(Op1, m_NegatedPower2())) {
      // X sdiv (-(1 << C)) -> -(X sdiv (1 << C)) ->
      //                    -> -(X udiv (1 << C)) -> -(X u>> C)
      Constant *CNegLog2 = ConstantExpr::getExactLogBase2(
          ConstantExpr::getNeg(cast<Constant>(Op1)));
      Value *Shr = Builder.CreateLShr(Op0, CNegLog2, I.getName(), I.isExact());
      return BinaryOperator::CreateNeg(Shr);
    }

    if (isKnownToBeAPowerOfTwo(Op1, /*OrZero*/ true, 0, &I)) {
      // X sdiv (1 << Y) -> X udiv (1 << Y) ( -> X u>> Y)
      // Safe because the only negative value (1 << Y) can take on is
      // INT_MIN, and X sdiv INT_MIN == X udiv INT_MIN == 0 if X doesn't have
      // the sign bit set.
      auto *BO = BinaryOperator::CreateUDiv(Op0, Op1, I.getName());
      BO->setIsExact(I.isExact());
      return BO;
    }
  }

  // -X / X --> X == INT_MIN ? 1 : -1
  if (isKnownNegation(Op0, Op1)) {
    APInt MinVal = APInt::getSignedMinValue(Ty->getScalarSizeInBits());
    Value *Cond = Builder.CreateICmpEQ(Op0, ConstantInt::get(Ty, MinVal));
    return SelectInst::Create(Cond, ConstantInt::get(Ty, 1),
                              ConstantInt::getAllOnesValue(Ty));
  }
  return nullptr;
}

/// Remove negation and try to convert division into multiplication.
Instruction *InstCombinerImpl::foldFDivConstantDivisor(BinaryOperator &I) {
  Constant *C;
  if (!match(I.getOperand(1), m_Constant(C)))
    return nullptr;

  // -X / C --> X / -C
  Value *X;
  const DataLayout &DL = I.getDataLayout();
  if (match(I.getOperand(0), m_FNeg(m_Value(X))))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
      return BinaryOperator::CreateFDivFMF(X, NegC, &I);

  // nnan X / +0.0 -> copysign(inf, X)
  // nnan nsz X / -0.0 -> copysign(inf, X)
  if (I.hasNoNaNs() &&
      (match(I.getOperand(1), m_PosZeroFP()) ||
       (I.hasNoSignedZeros() && match(I.getOperand(1), m_AnyZeroFP())))) {
    IRBuilder<> B(&I);
    CallInst *CopySign = B.CreateIntrinsic(
        Intrinsic::copysign, {C->getType()},
        {ConstantFP::getInfinity(I.getType()), I.getOperand(0)}, &I);
    CopySign->takeName(&I);
    return replaceInstUsesWith(I, CopySign);
  }

  // If the constant divisor has an exact inverse, this is always safe. If not,
  // then we can still create a reciprocal if fast-math-flags allow it and the
  // constant is a regular number (not zero, infinite, or denormal).
  if (!(C->hasExactInverseFP() || (I.hasAllowReciprocal() && C->isNormalFP())))
    return nullptr;

  // Disallow denormal constants because we don't know what would happen
  // on all targets.
  // TODO: Use Intrinsic::canonicalize or let function attributes tell us that
  // denorms are flushed?
  auto *RecipC = ConstantFoldBinaryOpOperands(
      Instruction::FDiv, ConstantFP::get(I.getType(), 1.0), C, DL);
  if (!RecipC || !RecipC->isNormalFP())
    return nullptr;

  // X / C --> X * (1 / C)
  return BinaryOperator::CreateFMulFMF(I.getOperand(0), RecipC, &I);
}

/// Remove negation and try to reassociate constant math.
static Instruction *foldFDivConstantDividend(BinaryOperator &I) {
  Constant *C;
  if (!match(I.getOperand(0), m_Constant(C)))
    return nullptr;

  // C / -X --> -C / X
  Value *X;
  const DataLayout &DL = I.getDataLayout();
  if (match(I.getOperand(1), m_FNeg(m_Value(X))))
    if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
      return BinaryOperator::CreateFDivFMF(NegC, X, &I);

  if (!I.hasAllowReassoc() || !I.hasAllowReciprocal())
    return nullptr;

  // Try to reassociate C / X expressions where X includes another constant.
  Constant *C2, *NewC = nullptr;
  if (match(I.getOperand(1), m_FMul(m_Value(X), m_Constant(C2)))) {
    // C / (X * C2) --> (C / C2) / X
    NewC = ConstantFoldBinaryOpOperands(Instruction::FDiv, C, C2, DL);
  } else if (match(I.getOperand(1), m_FDiv(m_Value(X), m_Constant(C2)))) {
    // C / (X / C2) --> (C * C2) / X
    NewC = ConstantFoldBinaryOpOperands(Instruction::FMul, C, C2, DL);
  }
  // Disallow denormal constants because we don't know what would happen
  // on all targets.
  // TODO: Use Intrinsic::canonicalize or let function attributes tell us that
  // denorms are flushed?
  if (!NewC || !NewC->isNormalFP())
    return nullptr;

  return BinaryOperator::CreateFDivFMF(NewC, X, &I);
}

/// Negate the exponent of pow/exp to fold division-by-pow() into multiply.
static Instruction *foldFDivPowDivisor(BinaryOperator &I,
                                       InstCombiner::BuilderTy &Builder) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  auto *II = dyn_cast<IntrinsicInst>(Op1);
  if (!II || !II->hasOneUse() || !I.hasAllowReassoc() ||
      !I.hasAllowReciprocal())
    return nullptr;

  // Z / pow(X, Y) --> Z * pow(X, -Y)
  // Z / exp{2}(Y) --> Z * exp{2}(-Y)
  // In the general case, this creates an extra instruction, but fmul allows
  // for better canonicalization and optimization than fdiv.
  Intrinsic::ID IID = II->getIntrinsicID();
  SmallVector<Value *> Args;
  switch (IID) {
  case Intrinsic::pow:
    Args.push_back(II->getArgOperand(0));
    Args.push_back(Builder.CreateFNegFMF(II->getArgOperand(1), &I));
    break;
  case Intrinsic::powi: {
    // Require 'ninf' assuming that makes powi(X, -INT_MIN) acceptable.
    // That is, X ** (huge negative number) is 0.0, ~1.0, or INF and so
    // dividing by that is INF, ~1.0, or 0.0. Code that uses powi allows
    // non-standard results, so this corner case should be acceptable if the
    // code rules out INF values.
    if (!I.hasNoInfs())
      return nullptr;
    Args.push_back(II->getArgOperand(0));
    Args.push_back(Builder.CreateNeg(II->getArgOperand(1)));
    Type *Tys[] = {I.getType(), II->getArgOperand(1)->getType()};
    Value *Pow = Builder.CreateIntrinsic(IID, Tys, Args, &I);
    return BinaryOperator::CreateFMulFMF(Op0, Pow, &I);
  }
  case Intrinsic::exp:
  case Intrinsic::exp2:
    Args.push_back(Builder.CreateFNegFMF(II->getArgOperand(0), &I));
    break;
  default:
    return nullptr;
  }
  Value *Pow = Builder.CreateIntrinsic(IID, I.getType(), Args, &I);
  return BinaryOperator::CreateFMulFMF(Op0, Pow, &I);
}

/// Convert div to mul if we have an sqrt divisor iff sqrt's operand is a fdiv
/// instruction.
static Instruction *foldFDivSqrtDivisor(BinaryOperator &I,
                                        InstCombiner::BuilderTy &Builder) {
  // X / sqrt(Y / Z) -->  X * sqrt(Z / Y)
  if (!I.hasAllowReassoc() || !I.hasAllowReciprocal())
    return nullptr;
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  auto *II = dyn_cast<IntrinsicInst>(Op1);
  if (!II || II->getIntrinsicID() != Intrinsic::sqrt || !II->hasOneUse() ||
      !II->hasAllowReassoc() || !II->hasAllowReciprocal())
    return nullptr;

  Value *Y, *Z;
  auto *DivOp = dyn_cast<Instruction>(II->getOperand(0));
  if (!DivOp)
    return nullptr;
  if (!match(DivOp, m_FDiv(m_Value(Y), m_Value(Z))))
    return nullptr;
  if (!DivOp->hasAllowReassoc() || !I.hasAllowReciprocal() ||
      !DivOp->hasOneUse())
    return nullptr;
  Value *SwapDiv = Builder.CreateFDivFMF(Z, Y, DivOp);
  Value *NewSqrt =
      Builder.CreateUnaryIntrinsic(II->getIntrinsicID(), SwapDiv, II);
  return BinaryOperator::CreateFMulFMF(Op0, NewSqrt, &I);
}

Instruction *InstCombinerImpl::visitFDiv(BinaryOperator &I) {
  Module *M = I.getModule();

  if (Value *V = simplifyFDivInst(I.getOperand(0), I.getOperand(1),
                                  I.getFastMathFlags(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  if (Instruction *R = foldFDivConstantDivisor(I))
    return R;

  if (Instruction *R = foldFDivConstantDividend(I))
    return R;

  if (Instruction *R = foldFPSignBitOps(I))
    return R;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  if (isa<Constant>(Op0))
    if (SelectInst *SI = dyn_cast<SelectInst>(Op1))
      if (Instruction *R = FoldOpIntoSelect(I, SI))
        return R;

  if (isa<Constant>(Op1))
    if (SelectInst *SI = dyn_cast<SelectInst>(Op0))
      if (Instruction *R = FoldOpIntoSelect(I, SI))
        return R;

  if (I.hasAllowReassoc() && I.hasAllowReciprocal()) {
    Value *X, *Y;
    if (match(Op0, m_OneUse(m_FDiv(m_Value(X), m_Value(Y)))) &&
        (!isa<Constant>(Y) || !isa<Constant>(Op1))) {
      // (X / Y) / Z => X / (Y * Z)
      Value *YZ = Builder.CreateFMulFMF(Y, Op1, &I);
      return BinaryOperator::CreateFDivFMF(X, YZ, &I);
    }
    if (match(Op1, m_OneUse(m_FDiv(m_Value(X), m_Value(Y)))) &&
        (!isa<Constant>(Y) || !isa<Constant>(Op0))) {
      // Z / (X / Y) => (Y * Z) / X
      Value *YZ = Builder.CreateFMulFMF(Y, Op0, &I);
      return BinaryOperator::CreateFDivFMF(YZ, X, &I);
    }
    // Z / (1.0 / Y) => (Y * Z)
    //
    // This is a special case of Z / (X / Y) => (Y * Z) / X, with X = 1.0. The
    // m_OneUse check is avoided because even in the case of the multiple uses
    // for 1.0/Y, the number of instructions remain the same and a division is
    // replaced by a multiplication.
    if (match(Op1, m_FDiv(m_SpecificFP(1.0), m_Value(Y))))
      return BinaryOperator::CreateFMulFMF(Y, Op0, &I);
  }

  if (I.hasAllowReassoc() && Op0->hasOneUse() && Op1->hasOneUse()) {
    // sin(X) / cos(X) -> tan(X)
    // cos(X) / sin(X) -> 1/tan(X) (cotangent)
    Value *X;
    bool IsTan = match(Op0, m_Intrinsic<Intrinsic::sin>(m_Value(X))) &&
                 match(Op1, m_Intrinsic<Intrinsic::cos>(m_Specific(X)));
    bool IsCot =
        !IsTan && match(Op0, m_Intrinsic<Intrinsic::cos>(m_Value(X))) &&
                  match(Op1, m_Intrinsic<Intrinsic::sin>(m_Specific(X)));

    if ((IsTan || IsCot) && hasFloatFn(M, &TLI, I.getType(), LibFunc_tan,
                                       LibFunc_tanf, LibFunc_tanl)) {
      IRBuilder<> B(&I);
      IRBuilder<>::FastMathFlagGuard FMFGuard(B);
      B.setFastMathFlags(I.getFastMathFlags());
      AttributeList Attrs =
          cast<CallBase>(Op0)->getCalledFunction()->getAttributes();
      Value *Res = emitUnaryFloatFnCall(X, &TLI, LibFunc_tan, LibFunc_tanf,
                                        LibFunc_tanl, B, Attrs);
      if (IsCot)
        Res = B.CreateFDiv(ConstantFP::get(I.getType(), 1.0), Res);
      return replaceInstUsesWith(I, Res);
    }
  }

  // X / (X * Y) --> 1.0 / Y
  // Reassociate to (X / X -> 1.0) is legal when NaNs are not allowed.
  // We can ignore the possibility that X is infinity because INF/INF is NaN.
  Value *X, *Y;
  if (I.hasNoNaNs() && I.hasAllowReassoc() &&
      match(Op1, m_c_FMul(m_Specific(Op0), m_Value(Y)))) {
    replaceOperand(I, 0, ConstantFP::get(I.getType(), 1.0));
    replaceOperand(I, 1, Y);
    return &I;
  }

  // X / fabs(X) -> copysign(1.0, X)
  // fabs(X) / X -> copysign(1.0, X)
  if (I.hasNoNaNs() && I.hasNoInfs() &&
      (match(&I, m_FDiv(m_Value(X), m_FAbs(m_Deferred(X)))) ||
       match(&I, m_FDiv(m_FAbs(m_Value(X)), m_Deferred(X))))) {
    Value *V = Builder.CreateBinaryIntrinsic(
        Intrinsic::copysign, ConstantFP::get(I.getType(), 1.0), X, &I);
    return replaceInstUsesWith(I, V);
  }

  if (Instruction *Mul = foldFDivPowDivisor(I, Builder))
    return Mul;

  if (Instruction *Mul = foldFDivSqrtDivisor(I, Builder))
    return Mul;

  // pow(X, Y) / X --> pow(X, Y-1)
  if (I.hasAllowReassoc() &&
      match(Op0, m_OneUse(m_Intrinsic<Intrinsic::pow>(m_Specific(Op1),
                                                      m_Value(Y))))) {
    Value *Y1 =
        Builder.CreateFAddFMF(Y, ConstantFP::get(I.getType(), -1.0), &I);
    Value *Pow = Builder.CreateBinaryIntrinsic(Intrinsic::pow, Op1, Y1, &I);
    return replaceInstUsesWith(I, Pow);
  }

  if (Instruction *FoldedPowi = foldPowiReassoc(I))
    return FoldedPowi;

  return nullptr;
}

// Variety of transform for:
//  (urem/srem (mul X, Y), (mul X, Z))
//  (urem/srem (shl X, Y), (shl X, Z))
//  (urem/srem (shl Y, X), (shl Z, X))
// NB: The shift cases are really just extensions of the mul case. We treat
// shift as Val * (1 << Amt).
static Instruction *simplifyIRemMulShl(BinaryOperator &I,
                                       InstCombinerImpl &IC) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1), *X = nullptr;
  APInt Y, Z;
  bool ShiftByX = false;

  // If V is not nullptr, it will be matched using m_Specific.
  auto MatchShiftOrMulXC = [](Value *Op, Value *&V, APInt &C) -> bool {
    const APInt *Tmp = nullptr;
    if ((!V && match(Op, m_Mul(m_Value(V), m_APInt(Tmp)))) ||
        (V && match(Op, m_Mul(m_Specific(V), m_APInt(Tmp)))))
      C = *Tmp;
    else if ((!V && match(Op, m_Shl(m_Value(V), m_APInt(Tmp)))) ||
             (V && match(Op, m_Shl(m_Specific(V), m_APInt(Tmp)))))
      C = APInt(Tmp->getBitWidth(), 1) << *Tmp;
    if (Tmp != nullptr)
      return true;

    // Reset `V` so we don't start with specific value on next match attempt.
    V = nullptr;
    return false;
  };

  auto MatchShiftCX = [](Value *Op, APInt &C, Value *&V) -> bool {
    const APInt *Tmp = nullptr;
    if ((!V && match(Op, m_Shl(m_APInt(Tmp), m_Value(V)))) ||
        (V && match(Op, m_Shl(m_APInt(Tmp), m_Specific(V))))) {
      C = *Tmp;
      return true;
    }

    // Reset `V` so we don't start with specific value on next match attempt.
    V = nullptr;
    return false;
  };

  if (MatchShiftOrMulXC(Op0, X, Y) && MatchShiftOrMulXC(Op1, X, Z)) {
    // pass
  } else if (MatchShiftCX(Op0, Y, X) && MatchShiftCX(Op1, Z, X)) {
    ShiftByX = true;
  } else {
    return nullptr;
  }

  bool IsSRem = I.getOpcode() == Instruction::SRem;

  OverflowingBinaryOperator *BO0 = cast<OverflowingBinaryOperator>(Op0);
  // TODO: We may be able to deduce more about nsw/nuw of BO0/BO1 based on Y >=
  // Z or Z >= Y.
  bool BO0HasNSW = BO0->hasNoSignedWrap();
  bool BO0HasNUW = BO0->hasNoUnsignedWrap();
  bool BO0NoWrap = IsSRem ? BO0HasNSW : BO0HasNUW;

  APInt RemYZ = IsSRem ? Y.srem(Z) : Y.urem(Z);
  // (rem (mul nuw/nsw X, Y), (mul X, Z))
  //      if (rem Y, Z) == 0
  //          -> 0
  if (RemYZ.isZero() && BO0NoWrap)
    return IC.replaceInstUsesWith(I, ConstantInt::getNullValue(I.getType()));

  // Helper function to emit either (RemSimplificationC << X) or
  // (RemSimplificationC * X) depending on whether we matched Op0/Op1 as
  // (shl V, X) or (mul V, X) respectively.
  auto CreateMulOrShift =
      [&](const APInt &RemSimplificationC) -> BinaryOperator * {
    Value *RemSimplification =
        ConstantInt::get(I.getType(), RemSimplificationC);
    return ShiftByX ? BinaryOperator::CreateShl(RemSimplification, X)
                    : BinaryOperator::CreateMul(X, RemSimplification);
  };

  OverflowingBinaryOperator *BO1 = cast<OverflowingBinaryOperator>(Op1);
  bool BO1HasNSW = BO1->hasNoSignedWrap();
  bool BO1HasNUW = BO1->hasNoUnsignedWrap();
  bool BO1NoWrap = IsSRem ? BO1HasNSW : BO1HasNUW;
  // (rem (mul X, Y), (mul nuw/nsw X, Z))
  //      if (rem Y, Z) == Y
  //          -> (mul nuw/nsw X, Y)
  if (RemYZ == Y && BO1NoWrap) {
    BinaryOperator *BO = CreateMulOrShift(Y);
    // Copy any overflow flags from Op0.
    BO->setHasNoSignedWrap(IsSRem || BO0HasNSW);
    BO->setHasNoUnsignedWrap(!IsSRem || BO0HasNUW);
    return BO;
  }

  // (rem (mul nuw/nsw X, Y), (mul {nsw} X, Z))
  //      if Y >= Z
  //          -> (mul {nuw} nsw X, (rem Y, Z))
  if (Y.uge(Z) && (IsSRem ? (BO0HasNSW && BO1HasNSW) : BO0HasNUW)) {
    BinaryOperator *BO = CreateMulOrShift(RemYZ);
    BO->setHasNoSignedWrap();
    BO->setHasNoUnsignedWrap(BO0HasNUW);
    return BO;
  }

  return nullptr;
}

/// This function implements the transforms common to both integer remainder
/// instructions (urem and srem). It is called by the visitors to those integer
/// remainder instructions.
/// Common integer remainder transforms
Instruction *InstCombinerImpl::commonIRemTransforms(BinaryOperator &I) {
  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  // The RHS is known non-zero.
  if (Value *V = simplifyValueKnownNonZero(I.getOperand(1), *this, I))
    return replaceOperand(I, 1, V);

  // Handle cases involving: rem X, (select Cond, Y, Z)
  if (simplifyDivRemOfSelectWithZeroOp(I))
    return &I;

  // If the divisor is a select-of-constants, try to constant fold all rem ops:
  // C % (select Cond, TrueC, FalseC) --> select Cond, (C % TrueC), (C % FalseC)
  // TODO: Adapt simplifyDivRemOfSelectWithZeroOp to allow this and other folds.
  if (match(Op0, m_ImmConstant()) &&
      match(Op1, m_Select(m_Value(), m_ImmConstant(), m_ImmConstant()))) {
    if (Instruction *R = FoldOpIntoSelect(I, cast<SelectInst>(Op1),
                                          /*FoldWithMultiUse*/ true))
      return R;
  }

  if (isa<Constant>(Op1)) {
    if (Instruction *Op0I = dyn_cast<Instruction>(Op0)) {
      if (SelectInst *SI = dyn_cast<SelectInst>(Op0I)) {
        if (Instruction *R = FoldOpIntoSelect(I, SI))
          return R;
      } else if (auto *PN = dyn_cast<PHINode>(Op0I)) {
        const APInt *Op1Int;
        if (match(Op1, m_APInt(Op1Int)) && !Op1Int->isMinValue() &&
            (I.getOpcode() == Instruction::URem ||
             !Op1Int->isMinSignedValue())) {
          // foldOpIntoPhi will speculate instructions to the end of the PHI's
          // predecessor blocks, so do this only if we know the srem or urem
          // will not fault.
          if (Instruction *NV = foldOpIntoPhi(I, PN))
            return NV;
        }
      }

      // See if we can fold away this rem instruction.
      if (SimplifyDemandedInstructionBits(I))
        return &I;
    }
  }

  if (Instruction *R = simplifyIRemMulShl(I, *this))
    return R;

  return nullptr;
}

Instruction *InstCombinerImpl::visitURem(BinaryOperator &I) {
  if (Value *V = simplifyURemInst(I.getOperand(0), I.getOperand(1),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *common = commonIRemTransforms(I))
    return common;

  if (Instruction *NarrowRem = narrowUDivURem(I, *this))
    return NarrowRem;

  // X urem Y -> X and Y-1, where Y is a power of 2,
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  if (isKnownToBeAPowerOfTwo(Op1, /*OrZero*/ true, 0, &I)) {
    // This may increase instruction count, we don't enforce that Y is a
    // constant.
    Constant *N1 = Constant::getAllOnesValue(Ty);
    Value *Add = Builder.CreateAdd(Op1, N1);
    return BinaryOperator::CreateAnd(Op0, Add);
  }

  // 1 urem X -> zext(X != 1)
  if (match(Op0, m_One())) {
    Value *Cmp = Builder.CreateICmpNE(Op1, ConstantInt::get(Ty, 1));
    return CastInst::CreateZExtOrBitCast(Cmp, Ty);
  }

  // Op0 urem C -> Op0 < C ? Op0 : Op0 - C, where C >= signbit.
  // Op0 must be frozen because we are increasing its number of uses.
  if (match(Op1, m_Negative())) {
    Value *F0 = Op0;
    if (!isGuaranteedNotToBeUndef(Op0))
      F0 = Builder.CreateFreeze(Op0, Op0->getName() + ".fr");
    Value *Cmp = Builder.CreateICmpULT(F0, Op1);
    Value *Sub = Builder.CreateSub(F0, Op1);
    return SelectInst::Create(Cmp, F0, Sub);
  }

  // If the divisor is a sext of a boolean, then the divisor must be max
  // unsigned value (-1). Therefore, the remainder is Op0 unless Op0 is also
  // max unsigned value. In that case, the remainder is 0:
  // urem Op0, (sext i1 X) --> (Op0 == -1) ? 0 : Op0
  Value *X;
  if (match(Op1, m_SExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1)) {
    Value *FrozenOp0 = Op0;
    if (!isGuaranteedNotToBeUndef(Op0))
      FrozenOp0 = Builder.CreateFreeze(Op0, Op0->getName() + ".frozen");
    Value *Cmp =
        Builder.CreateICmpEQ(FrozenOp0, ConstantInt::getAllOnesValue(Ty));
    return SelectInst::Create(Cmp, ConstantInt::getNullValue(Ty), FrozenOp0);
  }

  // For "(X + 1) % Op1" and if (X u< Op1) => (X + 1) == Op1 ? 0 : X + 1 .
  if (match(Op0, m_Add(m_Value(X), m_One()))) {
    Value *Val =
        simplifyICmpInst(ICmpInst::ICMP_ULT, X, Op1, SQ.getWithInstruction(&I));
    if (Val && match(Val, m_One())) {
      Value *FrozenOp0 = Op0;
      if (!isGuaranteedNotToBeUndef(Op0))
        FrozenOp0 = Builder.CreateFreeze(Op0, Op0->getName() + ".frozen");
      Value *Cmp = Builder.CreateICmpEQ(FrozenOp0, Op1);
      return SelectInst::Create(Cmp, ConstantInt::getNullValue(Ty), FrozenOp0);
    }
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitSRem(BinaryOperator &I) {
  if (Value *V = simplifySRemInst(I.getOperand(0), I.getOperand(1),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  // Handle the integer rem common cases
  if (Instruction *Common = commonIRemTransforms(I))
    return Common;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  {
    const APInt *Y;
    // X % -Y -> X % Y
    if (match(Op1, m_Negative(Y)) && !Y->isMinSignedValue())
      return replaceOperand(I, 1, ConstantInt::get(I.getType(), -*Y));
  }

  // -X srem Y --> -(X srem Y)
  Value *X, *Y;
  if (match(&I, m_SRem(m_OneUse(m_NSWNeg(m_Value(X))), m_Value(Y))))
    return BinaryOperator::CreateNSWNeg(Builder.CreateSRem(X, Y));

  // If the sign bits of both operands are zero (i.e. we can prove they are
  // unsigned inputs), turn this into a urem.
  APInt Mask(APInt::getSignMask(I.getType()->getScalarSizeInBits()));
  if (MaskedValueIsZero(Op1, Mask, 0, &I) &&
      MaskedValueIsZero(Op0, Mask, 0, &I)) {
    // X srem Y -> X urem Y, iff X and Y don't have sign bit set
    return BinaryOperator::CreateURem(Op0, Op1, I.getName());
  }

  // If it's a constant vector, flip any negative values positive.
  if (isa<ConstantVector>(Op1) || isa<ConstantDataVector>(Op1)) {
    Constant *C = cast<Constant>(Op1);
    unsigned VWidth = cast<FixedVectorType>(C->getType())->getNumElements();

    bool hasNegative = false;
    bool hasMissing = false;
    for (unsigned i = 0; i != VWidth; ++i) {
      Constant *Elt = C->getAggregateElement(i);
      if (!Elt) {
        hasMissing = true;
        break;
      }

      if (ConstantInt *RHS = dyn_cast<ConstantInt>(Elt))
        if (RHS->isNegative())
          hasNegative = true;
    }

    if (hasNegative && !hasMissing) {
      SmallVector<Constant *, 16> Elts(VWidth);
      for (unsigned i = 0; i != VWidth; ++i) {
        Elts[i] = C->getAggregateElement(i);  // Handle undef, etc.
        if (ConstantInt *RHS = dyn_cast<ConstantInt>(Elts[i])) {
          if (RHS->isNegative())
            Elts[i] = cast<ConstantInt>(ConstantExpr::getNeg(RHS));
        }
      }

      Constant *NewRHSV = ConstantVector::get(Elts);
      if (NewRHSV != C)  // Don't loop on -MININT
        return replaceOperand(I, 1, NewRHSV);
    }
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitFRem(BinaryOperator &I) {
  if (Value *V = simplifyFRemInst(I.getOperand(0), I.getOperand(1),
                                  I.getFastMathFlags(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  return nullptr;
}
