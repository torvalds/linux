//===- InstCombineSimplifyDemanded.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains logic for simplifying instructions based on information
// about how they are used.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "instcombine"

static cl::opt<bool>
    VerifyKnownBits("instcombine-verify-known-bits",
                    cl::desc("Verify that computeKnownBits() and "
                             "SimplifyDemandedBits() are consistent"),
                    cl::Hidden, cl::init(false));

/// Check to see if the specified operand of the specified instruction is a
/// constant integer. If so, check to see if there are any bits set in the
/// constant that are not demanded. If so, shrink the constant and return true.
static bool ShrinkDemandedConstant(Instruction *I, unsigned OpNo,
                                   const APInt &Demanded) {
  assert(I && "No instruction?");
  assert(OpNo < I->getNumOperands() && "Operand index too large");

  // The operand must be a constant integer or splat integer.
  Value *Op = I->getOperand(OpNo);
  const APInt *C;
  if (!match(Op, m_APInt(C)))
    return false;

  // If there are no bits set that aren't demanded, nothing to do.
  if (C->isSubsetOf(Demanded))
    return false;

  // This instruction is producing bits that are not demanded. Shrink the RHS.
  I->setOperand(OpNo, ConstantInt::get(Op->getType(), *C & Demanded));

  return true;
}

/// Returns the bitwidth of the given scalar or pointer type. For vector types,
/// returns the element type's bitwidth.
static unsigned getBitWidth(Type *Ty, const DataLayout &DL) {
  if (unsigned BitWidth = Ty->getScalarSizeInBits())
    return BitWidth;

  return DL.getPointerTypeSizeInBits(Ty);
}

/// Inst is an integer instruction that SimplifyDemandedBits knows about. See if
/// the instruction has any properties that allow us to simplify its operands.
bool InstCombinerImpl::SimplifyDemandedInstructionBits(Instruction &Inst,
                                                       KnownBits &Known) {
  APInt DemandedMask(APInt::getAllOnes(Known.getBitWidth()));
  Value *V = SimplifyDemandedUseBits(&Inst, DemandedMask, Known,
                                     0, SQ.getWithInstruction(&Inst));
  if (!V) return false;
  if (V == &Inst) return true;
  replaceInstUsesWith(Inst, V);
  return true;
}

/// Inst is an integer instruction that SimplifyDemandedBits knows about. See if
/// the instruction has any properties that allow us to simplify its operands.
bool InstCombinerImpl::SimplifyDemandedInstructionBits(Instruction &Inst) {
  KnownBits Known(getBitWidth(Inst.getType(), DL));
  return SimplifyDemandedInstructionBits(Inst, Known);
}

/// This form of SimplifyDemandedBits simplifies the specified instruction
/// operand if possible, updating it in place. It returns true if it made any
/// change and false otherwise.
bool InstCombinerImpl::SimplifyDemandedBits(Instruction *I, unsigned OpNo,
                                            const APInt &DemandedMask,
                                            KnownBits &Known, unsigned Depth,
                                            const SimplifyQuery &Q) {
  Use &U = I->getOperandUse(OpNo);
  Value *V = U.get();
  if (isa<Constant>(V)) {
    llvm::computeKnownBits(V, Known, Depth, Q);
    return false;
  }

  Known.resetAll();
  if (DemandedMask.isZero()) {
    // Not demanding any bits from V.
    replaceUse(U, UndefValue::get(V->getType()));
    return true;
  }

  if (Depth == MaxAnalysisRecursionDepth)
    return false;

  Instruction *VInst = dyn_cast<Instruction>(V);
  if (!VInst) {
    llvm::computeKnownBits(V, Known, Depth, Q);
    return false;
  }

  Value *NewVal;
  if (VInst->hasOneUse()) {
    // If the instruction has one use, we can directly simplify it.
    NewVal = SimplifyDemandedUseBits(VInst, DemandedMask, Known, Depth, Q);
  } else {
    // If there are multiple uses of this instruction, then we can simplify
    // VInst to some other value, but not modify the instruction.
    NewVal =
        SimplifyMultipleUseDemandedBits(VInst, DemandedMask, Known, Depth, Q);
  }
  if (!NewVal) return false;
  if (Instruction* OpInst = dyn_cast<Instruction>(U))
    salvageDebugInfo(*OpInst);

  replaceUse(U, NewVal);
  return true;
}

/// This function attempts to replace V with a simpler value based on the
/// demanded bits. When this function is called, it is known that only the bits
/// set in DemandedMask of the result of V are ever used downstream.
/// Consequently, depending on the mask and V, it may be possible to replace V
/// with a constant or one of its operands. In such cases, this function does
/// the replacement and returns true. In all other cases, it returns false after
/// analyzing the expression and setting KnownOne and known to be one in the
/// expression. Known.Zero contains all the bits that are known to be zero in
/// the expression. These are provided to potentially allow the caller (which
/// might recursively be SimplifyDemandedBits itself) to simplify the
/// expression.
/// Known.One and Known.Zero always follow the invariant that:
///   Known.One & Known.Zero == 0.
/// That is, a bit can't be both 1 and 0. The bits in Known.One and Known.Zero
/// are accurate even for bits not in DemandedMask. Note
/// also that the bitwidth of V, DemandedMask, Known.Zero and Known.One must all
/// be the same.
///
/// This returns null if it did not change anything and it permits no
/// simplification.  This returns V itself if it did some simplification of V's
/// operands based on the information about what bits are demanded. This returns
/// some other non-null value if it found out that V is equal to another value
/// in the context where the specified bits are demanded, but not for all users.
Value *InstCombinerImpl::SimplifyDemandedUseBits(Instruction *I,
                                                 const APInt &DemandedMask,
                                                 KnownBits &Known,
                                                 unsigned Depth,
                                                 const SimplifyQuery &Q) {
  assert(I != nullptr && "Null pointer of Value???");
  assert(Depth <= MaxAnalysisRecursionDepth && "Limit Search Depth");
  uint32_t BitWidth = DemandedMask.getBitWidth();
  Type *VTy = I->getType();
  assert(
      (!VTy->isIntOrIntVectorTy() || VTy->getScalarSizeInBits() == BitWidth) &&
      Known.getBitWidth() == BitWidth &&
      "Value *V, DemandedMask and Known must have same BitWidth");

  KnownBits LHSKnown(BitWidth), RHSKnown(BitWidth);

  // Update flags after simplifying an operand based on the fact that some high
  // order bits are not demanded.
  auto disableWrapFlagsBasedOnUnusedHighBits = [](Instruction *I,
                                                  unsigned NLZ) {
    if (NLZ > 0) {
      // Disable the nsw and nuw flags here: We can no longer guarantee that
      // we won't wrap after simplification. Removing the nsw/nuw flags is
      // legal here because the top bit is not demanded.
      I->setHasNoSignedWrap(false);
      I->setHasNoUnsignedWrap(false);
    }
    return I;
  };

  // If the high-bits of an ADD/SUB/MUL are not demanded, then we do not care
  // about the high bits of the operands.
  auto simplifyOperandsBasedOnUnusedHighBits = [&](APInt &DemandedFromOps) {
    unsigned NLZ = DemandedMask.countl_zero();
    // Right fill the mask of bits for the operands to demand the most
    // significant bit and all those below it.
    DemandedFromOps = APInt::getLowBitsSet(BitWidth, BitWidth - NLZ);
    if (ShrinkDemandedConstant(I, 0, DemandedFromOps) ||
        SimplifyDemandedBits(I, 0, DemandedFromOps, LHSKnown, Depth + 1, Q) ||
        ShrinkDemandedConstant(I, 1, DemandedFromOps) ||
        SimplifyDemandedBits(I, 1, DemandedFromOps, RHSKnown, Depth + 1, Q)) {
      disableWrapFlagsBasedOnUnusedHighBits(I, NLZ);
      return true;
    }
    return false;
  };

  switch (I->getOpcode()) {
  default:
    llvm::computeKnownBits(I, Known, Depth, Q);
    break;
  case Instruction::And: {
    // If either the LHS or the RHS are Zero, the result is zero.
    if (SimplifyDemandedBits(I, 1, DemandedMask, RHSKnown, Depth + 1, Q) ||
        SimplifyDemandedBits(I, 0, DemandedMask & ~RHSKnown.Zero, LHSKnown,
                             Depth + 1, Q))
      return I;

    Known = analyzeKnownBitsFromAndXorOr(cast<Operator>(I), LHSKnown, RHSKnown,
                                         Depth, Q);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero | Known.One))
      return Constant::getIntegerValue(VTy, Known.One);

    // If all of the demanded bits are known 1 on one side, return the other.
    // These bits cannot contribute to the result of the 'and'.
    if (DemandedMask.isSubsetOf(LHSKnown.Zero | RHSKnown.One))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.Zero | LHSKnown.One))
      return I->getOperand(1);

    // If the RHS is a constant, see if we can simplify it.
    if (ShrinkDemandedConstant(I, 1, DemandedMask & ~LHSKnown.Zero))
      return I;

    break;
  }
  case Instruction::Or: {
    // If either the LHS or the RHS are One, the result is One.
    if (SimplifyDemandedBits(I, 1, DemandedMask, RHSKnown, Depth + 1, Q) ||
        SimplifyDemandedBits(I, 0, DemandedMask & ~RHSKnown.One, LHSKnown,
                             Depth + 1, Q)) {
      // Disjoint flag may not longer hold.
      I->dropPoisonGeneratingFlags();
      return I;
    }

    Known = analyzeKnownBitsFromAndXorOr(cast<Operator>(I), LHSKnown, RHSKnown,
                                         Depth, Q);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero | Known.One))
      return Constant::getIntegerValue(VTy, Known.One);

    // If all of the demanded bits are known zero on one side, return the other.
    // These bits cannot contribute to the result of the 'or'.
    if (DemandedMask.isSubsetOf(LHSKnown.One | RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.One | LHSKnown.Zero))
      return I->getOperand(1);

    // If the RHS is a constant, see if we can simplify it.
    if (ShrinkDemandedConstant(I, 1, DemandedMask))
      return I;

    // Infer disjoint flag if no common bits are set.
    if (!cast<PossiblyDisjointInst>(I)->isDisjoint()) {
      WithCache<const Value *> LHSCache(I->getOperand(0), LHSKnown),
          RHSCache(I->getOperand(1), RHSKnown);
      if (haveNoCommonBitsSet(LHSCache, RHSCache, Q)) {
        cast<PossiblyDisjointInst>(I)->setIsDisjoint(true);
        return I;
      }
    }

    break;
  }
  case Instruction::Xor: {
    if (SimplifyDemandedBits(I, 1, DemandedMask, RHSKnown, Depth + 1, Q) ||
        SimplifyDemandedBits(I, 0, DemandedMask, LHSKnown, Depth + 1, Q))
      return I;
    Value *LHS, *RHS;
    if (DemandedMask == 1 &&
        match(I->getOperand(0), m_Intrinsic<Intrinsic::ctpop>(m_Value(LHS))) &&
        match(I->getOperand(1), m_Intrinsic<Intrinsic::ctpop>(m_Value(RHS)))) {
      // (ctpop(X) ^ ctpop(Y)) & 1 --> ctpop(X^Y) & 1
      IRBuilderBase::InsertPointGuard Guard(Builder);
      Builder.SetInsertPoint(I);
      auto *Xor = Builder.CreateXor(LHS, RHS);
      return Builder.CreateUnaryIntrinsic(Intrinsic::ctpop, Xor);
    }

    Known = analyzeKnownBitsFromAndXorOr(cast<Operator>(I), LHSKnown, RHSKnown,
                                         Depth, Q);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero | Known.One))
      return Constant::getIntegerValue(VTy, Known.One);

    // If all of the demanded bits are known zero on one side, return the other.
    // These bits cannot contribute to the result of the 'xor'.
    if (DemandedMask.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    // If all of the demanded bits are known to be zero on one side or the
    // other, turn this into an *inclusive* or.
    //    e.g. (A & C1)^(B & C2) -> (A & C1)|(B & C2) iff C1&C2 == 0
    if (DemandedMask.isSubsetOf(RHSKnown.Zero | LHSKnown.Zero)) {
      Instruction *Or =
          BinaryOperator::CreateOr(I->getOperand(0), I->getOperand(1));
      if (DemandedMask.isAllOnes())
        cast<PossiblyDisjointInst>(Or)->setIsDisjoint(true);
      Or->takeName(I);
      return InsertNewInstWith(Or, I->getIterator());
    }

    // If all of the demanded bits on one side are known, and all of the set
    // bits on that side are also known to be set on the other side, turn this
    // into an AND, as we know the bits will be cleared.
    //    e.g. (X | C1) ^ C2 --> (X | C1) & ~C2 iff (C1&C2) == C2
    if (DemandedMask.isSubsetOf(RHSKnown.Zero|RHSKnown.One) &&
        RHSKnown.One.isSubsetOf(LHSKnown.One)) {
      Constant *AndC = Constant::getIntegerValue(VTy,
                                                 ~RHSKnown.One & DemandedMask);
      Instruction *And = BinaryOperator::CreateAnd(I->getOperand(0), AndC);
      return InsertNewInstWith(And, I->getIterator());
    }

    // If the RHS is a constant, see if we can change it. Don't alter a -1
    // constant because that's a canonical 'not' op, and that is better for
    // combining, SCEV, and codegen.
    const APInt *C;
    if (match(I->getOperand(1), m_APInt(C)) && !C->isAllOnes()) {
      if ((*C | ~DemandedMask).isAllOnes()) {
        // Force bits to 1 to create a 'not' op.
        I->setOperand(1, ConstantInt::getAllOnesValue(VTy));
        return I;
      }
      // If we can't turn this into a 'not', try to shrink the constant.
      if (ShrinkDemandedConstant(I, 1, DemandedMask))
        return I;
    }

    // If our LHS is an 'and' and if it has one use, and if any of the bits we
    // are flipping are known to be set, then the xor is just resetting those
    // bits to zero.  We can just knock out bits from the 'and' and the 'xor',
    // simplifying both of them.
    if (Instruction *LHSInst = dyn_cast<Instruction>(I->getOperand(0))) {
      ConstantInt *AndRHS, *XorRHS;
      if (LHSInst->getOpcode() == Instruction::And && LHSInst->hasOneUse() &&
          match(I->getOperand(1), m_ConstantInt(XorRHS)) &&
          match(LHSInst->getOperand(1), m_ConstantInt(AndRHS)) &&
          (LHSKnown.One & RHSKnown.One & DemandedMask) != 0) {
        APInt NewMask = ~(LHSKnown.One & RHSKnown.One & DemandedMask);

        Constant *AndC = ConstantInt::get(VTy, NewMask & AndRHS->getValue());
        Instruction *NewAnd = BinaryOperator::CreateAnd(I->getOperand(0), AndC);
        InsertNewInstWith(NewAnd, I->getIterator());

        Constant *XorC = ConstantInt::get(VTy, NewMask & XorRHS->getValue());
        Instruction *NewXor = BinaryOperator::CreateXor(NewAnd, XorC);
        return InsertNewInstWith(NewXor, I->getIterator());
      }
    }
    break;
  }
  case Instruction::Select: {
    if (SimplifyDemandedBits(I, 2, DemandedMask, RHSKnown, Depth + 1, Q) ||
        SimplifyDemandedBits(I, 1, DemandedMask, LHSKnown, Depth + 1, Q))
      return I;

    // If the operands are constants, see if we can simplify them.
    // This is similar to ShrinkDemandedConstant, but for a select we want to
    // try to keep the selected constants the same as icmp value constants, if
    // we can. This helps not break apart (or helps put back together)
    // canonical patterns like min and max.
    auto CanonicalizeSelectConstant = [](Instruction *I, unsigned OpNo,
                                         const APInt &DemandedMask) {
      const APInt *SelC;
      if (!match(I->getOperand(OpNo), m_APInt(SelC)))
        return false;

      // Get the constant out of the ICmp, if there is one.
      // Only try this when exactly 1 operand is a constant (if both operands
      // are constant, the icmp should eventually simplify). Otherwise, we may
      // invert the transform that reduces set bits and infinite-loop.
      Value *X;
      const APInt *CmpC;
      ICmpInst::Predicate Pred;
      if (!match(I->getOperand(0), m_ICmp(Pred, m_Value(X), m_APInt(CmpC))) ||
          isa<Constant>(X) || CmpC->getBitWidth() != SelC->getBitWidth())
        return ShrinkDemandedConstant(I, OpNo, DemandedMask);

      // If the constant is already the same as the ICmp, leave it as-is.
      if (*CmpC == *SelC)
        return false;
      // If the constants are not already the same, but can be with the demand
      // mask, use the constant value from the ICmp.
      if ((*CmpC & DemandedMask) == (*SelC & DemandedMask)) {
        I->setOperand(OpNo, ConstantInt::get(I->getType(), *CmpC));
        return true;
      }
      return ShrinkDemandedConstant(I, OpNo, DemandedMask);
    };
    if (CanonicalizeSelectConstant(I, 1, DemandedMask) ||
        CanonicalizeSelectConstant(I, 2, DemandedMask))
      return I;

    // Only known if known in both the LHS and RHS.
    adjustKnownBitsForSelectArm(LHSKnown, I->getOperand(0), I->getOperand(1),
                                /*Invert=*/false, Depth, Q);
    adjustKnownBitsForSelectArm(RHSKnown, I->getOperand(0), I->getOperand(2),
                                /*Invert=*/true, Depth, Q);
    Known = LHSKnown.intersectWith(RHSKnown);
    break;
  }
  case Instruction::Trunc: {
    // If we do not demand the high bits of a right-shifted and truncated value,
    // then we may be able to truncate it before the shift.
    Value *X;
    const APInt *C;
    if (match(I->getOperand(0), m_OneUse(m_LShr(m_Value(X), m_APInt(C))))) {
      // The shift amount must be valid (not poison) in the narrow type, and
      // it must not be greater than the high bits demanded of the result.
      if (C->ult(VTy->getScalarSizeInBits()) &&
          C->ule(DemandedMask.countl_zero())) {
        // trunc (lshr X, C) --> lshr (trunc X), C
        IRBuilderBase::InsertPointGuard Guard(Builder);
        Builder.SetInsertPoint(I);
        Value *Trunc = Builder.CreateTrunc(X, VTy);
        return Builder.CreateLShr(Trunc, C->getZExtValue());
      }
    }
  }
    [[fallthrough]];
  case Instruction::ZExt: {
    unsigned SrcBitWidth = I->getOperand(0)->getType()->getScalarSizeInBits();

    APInt InputDemandedMask = DemandedMask.zextOrTrunc(SrcBitWidth);
    KnownBits InputKnown(SrcBitWidth);
    if (SimplifyDemandedBits(I, 0, InputDemandedMask, InputKnown, Depth + 1,
                             Q)) {
      // For zext nneg, we may have dropped the instruction which made the
      // input non-negative.
      I->dropPoisonGeneratingFlags();
      return I;
    }
    assert(InputKnown.getBitWidth() == SrcBitWidth && "Src width changed?");
    if (I->getOpcode() == Instruction::ZExt && I->hasNonNeg() &&
        !InputKnown.isNegative())
      InputKnown.makeNonNegative();
    Known = InputKnown.zextOrTrunc(BitWidth);

    break;
  }
  case Instruction::SExt: {
    // Compute the bits in the result that are not present in the input.
    unsigned SrcBitWidth = I->getOperand(0)->getType()->getScalarSizeInBits();

    APInt InputDemandedBits = DemandedMask.trunc(SrcBitWidth);

    // If any of the sign extended bits are demanded, we know that the sign
    // bit is demanded.
    if (DemandedMask.getActiveBits() > SrcBitWidth)
      InputDemandedBits.setBit(SrcBitWidth-1);

    KnownBits InputKnown(SrcBitWidth);
    if (SimplifyDemandedBits(I, 0, InputDemandedBits, InputKnown, Depth + 1, Q))
      return I;

    // If the input sign bit is known zero, or if the NewBits are not demanded
    // convert this into a zero extension.
    if (InputKnown.isNonNegative() ||
        DemandedMask.getActiveBits() <= SrcBitWidth) {
      // Convert to ZExt cast.
      CastInst *NewCast = new ZExtInst(I->getOperand(0), VTy);
      NewCast->takeName(I);
      return InsertNewInstWith(NewCast, I->getIterator());
    }

    // If the sign bit of the input is known set or clear, then we know the
    // top bits of the result.
    Known = InputKnown.sext(BitWidth);
    break;
  }
  case Instruction::Add: {
    if ((DemandedMask & 1) == 0) {
      // If we do not need the low bit, try to convert bool math to logic:
      // add iN (zext i1 X), (sext i1 Y) --> sext (~X & Y) to iN
      Value *X, *Y;
      if (match(I, m_c_Add(m_OneUse(m_ZExt(m_Value(X))),
                           m_OneUse(m_SExt(m_Value(Y))))) &&
          X->getType()->isIntOrIntVectorTy(1) && X->getType() == Y->getType()) {
        // Truth table for inputs and output signbits:
        //       X:0 | X:1
        //      ----------
        // Y:0  |  0 | 0 |
        // Y:1  | -1 | 0 |
        //      ----------
        IRBuilderBase::InsertPointGuard Guard(Builder);
        Builder.SetInsertPoint(I);
        Value *AndNot = Builder.CreateAnd(Builder.CreateNot(X), Y);
        return Builder.CreateSExt(AndNot, VTy);
      }

      // add iN (sext i1 X), (sext i1 Y) --> sext (X | Y) to iN
      if (match(I, m_Add(m_SExt(m_Value(X)), m_SExt(m_Value(Y)))) &&
          X->getType()->isIntOrIntVectorTy(1) && X->getType() == Y->getType() &&
          (I->getOperand(0)->hasOneUse() || I->getOperand(1)->hasOneUse())) {

        // Truth table for inputs and output signbits:
        //       X:0 | X:1
        //      -----------
        // Y:0  | -1 | -1 |
        // Y:1  | -1 |  0 |
        //      -----------
        IRBuilderBase::InsertPointGuard Guard(Builder);
        Builder.SetInsertPoint(I);
        Value *Or = Builder.CreateOr(X, Y);
        return Builder.CreateSExt(Or, VTy);
      }
    }

    // Right fill the mask of bits for the operands to demand the most
    // significant bit and all those below it.
    unsigned NLZ = DemandedMask.countl_zero();
    APInt DemandedFromOps = APInt::getLowBitsSet(BitWidth, BitWidth - NLZ);
    if (ShrinkDemandedConstant(I, 1, DemandedFromOps) ||
        SimplifyDemandedBits(I, 1, DemandedFromOps, RHSKnown, Depth + 1, Q))
      return disableWrapFlagsBasedOnUnusedHighBits(I, NLZ);

    // If low order bits are not demanded and known to be zero in one operand,
    // then we don't need to demand them from the other operand, since they
    // can't cause overflow into any bits that are demanded in the result.
    unsigned NTZ = (~DemandedMask & RHSKnown.Zero).countr_one();
    APInt DemandedFromLHS = DemandedFromOps;
    DemandedFromLHS.clearLowBits(NTZ);
    if (ShrinkDemandedConstant(I, 0, DemandedFromLHS) ||
        SimplifyDemandedBits(I, 0, DemandedFromLHS, LHSKnown, Depth + 1, Q))
      return disableWrapFlagsBasedOnUnusedHighBits(I, NLZ);

    // If we are known to be adding zeros to every bit below
    // the highest demanded bit, we just return the other side.
    if (DemandedFromOps.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedFromOps.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    // (add X, C) --> (xor X, C) IFF C is equal to the top bit of the DemandMask
    {
      const APInt *C;
      if (match(I->getOperand(1), m_APInt(C)) &&
          C->isOneBitSet(DemandedMask.getActiveBits() - 1)) {
        IRBuilderBase::InsertPointGuard Guard(Builder);
        Builder.SetInsertPoint(I);
        return Builder.CreateXor(I->getOperand(0), ConstantInt::get(VTy, *C));
      }
    }

    // Otherwise just compute the known bits of the result.
    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    bool NUW = cast<OverflowingBinaryOperator>(I)->hasNoUnsignedWrap();
    Known = KnownBits::computeForAddSub(true, NSW, NUW, LHSKnown, RHSKnown);
    break;
  }
  case Instruction::Sub: {
    // Right fill the mask of bits for the operands to demand the most
    // significant bit and all those below it.
    unsigned NLZ = DemandedMask.countl_zero();
    APInt DemandedFromOps = APInt::getLowBitsSet(BitWidth, BitWidth - NLZ);
    if (ShrinkDemandedConstant(I, 1, DemandedFromOps) ||
        SimplifyDemandedBits(I, 1, DemandedFromOps, RHSKnown, Depth + 1, Q))
      return disableWrapFlagsBasedOnUnusedHighBits(I, NLZ);

    // If low order bits are not demanded and are known to be zero in RHS,
    // then we don't need to demand them from LHS, since they can't cause a
    // borrow from any bits that are demanded in the result.
    unsigned NTZ = (~DemandedMask & RHSKnown.Zero).countr_one();
    APInt DemandedFromLHS = DemandedFromOps;
    DemandedFromLHS.clearLowBits(NTZ);
    if (ShrinkDemandedConstant(I, 0, DemandedFromLHS) ||
        SimplifyDemandedBits(I, 0, DemandedFromLHS, LHSKnown, Depth + 1, Q))
      return disableWrapFlagsBasedOnUnusedHighBits(I, NLZ);

    // If we are known to be subtracting zeros from every bit below
    // the highest demanded bit, we just return the other side.
    if (DemandedFromOps.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);
    // We can't do this with the LHS for subtraction, unless we are only
    // demanding the LSB.
    if (DemandedFromOps.isOne() && DemandedFromOps.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    // Otherwise just compute the known bits of the result.
    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    bool NUW = cast<OverflowingBinaryOperator>(I)->hasNoUnsignedWrap();
    Known = KnownBits::computeForAddSub(false, NSW, NUW, LHSKnown, RHSKnown);
    break;
  }
  case Instruction::Mul: {
    APInt DemandedFromOps;
    if (simplifyOperandsBasedOnUnusedHighBits(DemandedFromOps))
      return I;

    if (DemandedMask.isPowerOf2()) {
      // The LSB of X*Y is set only if (X & 1) == 1 and (Y & 1) == 1.
      // If we demand exactly one bit N and we have "X * (C' << N)" where C' is
      // odd (has LSB set), then the left-shifted low bit of X is the answer.
      unsigned CTZ = DemandedMask.countr_zero();
      const APInt *C;
      if (match(I->getOperand(1), m_APInt(C)) && C->countr_zero() == CTZ) {
        Constant *ShiftC = ConstantInt::get(VTy, CTZ);
        Instruction *Shl = BinaryOperator::CreateShl(I->getOperand(0), ShiftC);
        return InsertNewInstWith(Shl, I->getIterator());
      }
    }
    // For a squared value "X * X", the bottom 2 bits are 0 and X[0] because:
    // X * X is odd iff X is odd.
    // 'Quadratic Reciprocity': X * X -> 0 for bit[1]
    if (I->getOperand(0) == I->getOperand(1) && DemandedMask.ult(4)) {
      Constant *One = ConstantInt::get(VTy, 1);
      Instruction *And1 = BinaryOperator::CreateAnd(I->getOperand(0), One);
      return InsertNewInstWith(And1, I->getIterator());
    }

    llvm::computeKnownBits(I, Known, Depth, Q);
    break;
  }
  case Instruction::Shl: {
    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      const APInt *ShrAmt;
      if (match(I->getOperand(0), m_Shr(m_Value(), m_APInt(ShrAmt))))
        if (Instruction *Shr = dyn_cast<Instruction>(I->getOperand(0)))
          if (Value *R = simplifyShrShlDemandedBits(Shr, *ShrAmt, I, *SA,
                                                    DemandedMask, Known))
            return R;

      // Do not simplify if shl is part of funnel-shift pattern
      if (I->hasOneUse()) {
        auto *Inst = dyn_cast<Instruction>(I->user_back());
        if (Inst && Inst->getOpcode() == BinaryOperator::Or) {
          if (auto Opt = convertOrOfShiftsToFunnelShift(*Inst)) {
            auto [IID, FShiftArgs] = *Opt;
            if ((IID == Intrinsic::fshl || IID == Intrinsic::fshr) &&
                FShiftArgs[0] == FShiftArgs[1]) {
              llvm::computeKnownBits(I, Known, Depth, Q);
              break;
            }
          }
        }
      }

      // We only want bits that already match the signbit then we don't
      // need to shift.
      uint64_t ShiftAmt = SA->getLimitedValue(BitWidth - 1);
      if (DemandedMask.countr_zero() >= ShiftAmt) {
        if (I->hasNoSignedWrap()) {
          unsigned NumHiDemandedBits = BitWidth - DemandedMask.countr_zero();
          unsigned SignBits =
              ComputeNumSignBits(I->getOperand(0), Depth + 1, Q.CxtI);
          if (SignBits > ShiftAmt && SignBits - ShiftAmt >= NumHiDemandedBits)
            return I->getOperand(0);
        }

        // If we can pre-shift a right-shifted constant to the left without
        // losing any high bits and we don't demand the low bits, then eliminate
        // the left-shift:
        // (C >> X) << LeftShiftAmtC --> (C << LeftShiftAmtC) >> X
        Value *X;
        Constant *C;
        if (match(I->getOperand(0), m_LShr(m_ImmConstant(C), m_Value(X)))) {
          Constant *LeftShiftAmtC = ConstantInt::get(VTy, ShiftAmt);
          Constant *NewC = ConstantFoldBinaryOpOperands(Instruction::Shl, C,
                                                        LeftShiftAmtC, DL);
          if (ConstantFoldBinaryOpOperands(Instruction::LShr, NewC,
                                           LeftShiftAmtC, DL) == C) {
            Instruction *Lshr = BinaryOperator::CreateLShr(NewC, X);
            return InsertNewInstWith(Lshr, I->getIterator());
          }
        }
      }

      APInt DemandedMaskIn(DemandedMask.lshr(ShiftAmt));

      // If the shift is NUW/NSW, then it does demand the high bits.
      ShlOperator *IOp = cast<ShlOperator>(I);
      if (IOp->hasNoSignedWrap())
        DemandedMaskIn.setHighBits(ShiftAmt+1);
      else if (IOp->hasNoUnsignedWrap())
        DemandedMaskIn.setHighBits(ShiftAmt);

      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, Known, Depth + 1, Q))
        return I;

      Known = KnownBits::shl(Known,
                             KnownBits::makeConstant(APInt(BitWidth, ShiftAmt)),
                             /* NUW */ IOp->hasNoUnsignedWrap(),
                             /* NSW */ IOp->hasNoSignedWrap());
    } else {
      // This is a variable shift, so we can't shift the demand mask by a known
      // amount. But if we are not demanding high bits, then we are not
      // demanding those bits from the pre-shifted operand either.
      if (unsigned CTLZ = DemandedMask.countl_zero()) {
        APInt DemandedFromOp(APInt::getLowBitsSet(BitWidth, BitWidth - CTLZ));
        if (SimplifyDemandedBits(I, 0, DemandedFromOp, Known, Depth + 1, Q)) {
          // We can't guarantee that nsw/nuw hold after simplifying the operand.
          I->dropPoisonGeneratingFlags();
          return I;
        }
      }
      llvm::computeKnownBits(I, Known, Depth, Q);
    }
    break;
  }
  case Instruction::LShr: {
    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      uint64_t ShiftAmt = SA->getLimitedValue(BitWidth-1);

      // Do not simplify if lshr is part of funnel-shift pattern
      if (I->hasOneUse()) {
        auto *Inst = dyn_cast<Instruction>(I->user_back());
        if (Inst && Inst->getOpcode() == BinaryOperator::Or) {
          if (auto Opt = convertOrOfShiftsToFunnelShift(*Inst)) {
            auto [IID, FShiftArgs] = *Opt;
            if ((IID == Intrinsic::fshl || IID == Intrinsic::fshr) &&
                FShiftArgs[0] == FShiftArgs[1]) {
              llvm::computeKnownBits(I, Known, Depth, Q);
              break;
            }
          }
        }
      }

      // If we are just demanding the shifted sign bit and below, then this can
      // be treated as an ASHR in disguise.
      if (DemandedMask.countl_zero() >= ShiftAmt) {
        // If we only want bits that already match the signbit then we don't
        // need to shift.
        unsigned NumHiDemandedBits = BitWidth - DemandedMask.countr_zero();
        unsigned SignBits =
            ComputeNumSignBits(I->getOperand(0), Depth + 1, Q.CxtI);
        if (SignBits >= NumHiDemandedBits)
          return I->getOperand(0);

        // If we can pre-shift a left-shifted constant to the right without
        // losing any low bits (we already know we don't demand the high bits),
        // then eliminate the right-shift:
        // (C << X) >> RightShiftAmtC --> (C >> RightShiftAmtC) << X
        Value *X;
        Constant *C;
        if (match(I->getOperand(0), m_Shl(m_ImmConstant(C), m_Value(X)))) {
          Constant *RightShiftAmtC = ConstantInt::get(VTy, ShiftAmt);
          Constant *NewC = ConstantFoldBinaryOpOperands(Instruction::LShr, C,
                                                        RightShiftAmtC, DL);
          if (ConstantFoldBinaryOpOperands(Instruction::Shl, NewC,
                                           RightShiftAmtC, DL) == C) {
            Instruction *Shl = BinaryOperator::CreateShl(NewC, X);
            return InsertNewInstWith(Shl, I->getIterator());
          }
        }
      }

      // Unsigned shift right.
      APInt DemandedMaskIn(DemandedMask.shl(ShiftAmt));
      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, Known, Depth + 1, Q)) {
        // exact flag may not longer hold.
        I->dropPoisonGeneratingFlags();
        return I;
      }
      Known.Zero.lshrInPlace(ShiftAmt);
      Known.One.lshrInPlace(ShiftAmt);
      if (ShiftAmt)
        Known.Zero.setHighBits(ShiftAmt);  // high bits known zero.
    } else {
      llvm::computeKnownBits(I, Known, Depth, Q);
    }
    break;
  }
  case Instruction::AShr: {
    unsigned SignBits = ComputeNumSignBits(I->getOperand(0), Depth + 1, Q.CxtI);

    // If we only want bits that already match the signbit then we don't need
    // to shift.
    unsigned NumHiDemandedBits = BitWidth - DemandedMask.countr_zero();
    if (SignBits >= NumHiDemandedBits)
      return I->getOperand(0);

    // If this is an arithmetic shift right and only the low-bit is set, we can
    // always convert this into a logical shr, even if the shift amount is
    // variable.  The low bit of the shift cannot be an input sign bit unless
    // the shift amount is >= the size of the datatype, which is undefined.
    if (DemandedMask.isOne()) {
      // Perform the logical shift right.
      Instruction *NewVal = BinaryOperator::CreateLShr(
                        I->getOperand(0), I->getOperand(1), I->getName());
      return InsertNewInstWith(NewVal, I->getIterator());
    }

    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      uint32_t ShiftAmt = SA->getLimitedValue(BitWidth-1);

      // Signed shift right.
      APInt DemandedMaskIn(DemandedMask.shl(ShiftAmt));
      // If any of the bits being shifted in are demanded, then we should set
      // the sign bit as demanded.
      bool ShiftedInBitsDemanded = DemandedMask.countl_zero() < ShiftAmt;
      if (ShiftedInBitsDemanded)
        DemandedMaskIn.setSignBit();
      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, Known, Depth + 1, Q)) {
        // exact flag may not longer hold.
        I->dropPoisonGeneratingFlags();
        return I;
      }

      // If the input sign bit is known to be zero, or if none of the shifted in
      // bits are demanded, turn this into an unsigned shift right.
      if (Known.Zero[BitWidth - 1] || !ShiftedInBitsDemanded) {
        BinaryOperator *LShr = BinaryOperator::CreateLShr(I->getOperand(0),
                                                          I->getOperand(1));
        LShr->setIsExact(cast<BinaryOperator>(I)->isExact());
        LShr->takeName(I);
        return InsertNewInstWith(LShr, I->getIterator());
      }

      Known = KnownBits::ashr(
          Known, KnownBits::makeConstant(APInt(BitWidth, ShiftAmt)),
          ShiftAmt != 0, I->isExact());
    } else {
      llvm::computeKnownBits(I, Known, Depth, Q);
    }
    break;
  }
  case Instruction::UDiv: {
    // UDiv doesn't demand low bits that are zero in the divisor.
    const APInt *SA;
    if (match(I->getOperand(1), m_APInt(SA))) {
      // TODO: Take the demanded mask of the result into account.
      unsigned RHSTrailingZeros = SA->countr_zero();
      APInt DemandedMaskIn =
          APInt::getHighBitsSet(BitWidth, BitWidth - RHSTrailingZeros);
      if (SimplifyDemandedBits(I, 0, DemandedMaskIn, LHSKnown, Depth + 1, Q)) {
        // We can't guarantee that "exact" is still true after changing the
        // the dividend.
        I->dropPoisonGeneratingFlags();
        return I;
      }

      Known = KnownBits::udiv(LHSKnown, KnownBits::makeConstant(*SA),
                              cast<BinaryOperator>(I)->isExact());
    } else {
      llvm::computeKnownBits(I, Known, Depth, Q);
    }
    break;
  }
  case Instruction::SRem: {
    const APInt *Rem;
    if (match(I->getOperand(1), m_APInt(Rem))) {
      // X % -1 demands all the bits because we don't want to introduce
      // INT_MIN % -1 (== undef) by accident.
      if (Rem->isAllOnes())
        break;
      APInt RA = Rem->abs();
      if (RA.isPowerOf2()) {
        if (DemandedMask.ult(RA))    // srem won't affect demanded bits
          return I->getOperand(0);

        APInt LowBits = RA - 1;
        APInt Mask2 = LowBits | APInt::getSignMask(BitWidth);
        if (SimplifyDemandedBits(I, 0, Mask2, LHSKnown, Depth + 1, Q))
          return I;

        // The low bits of LHS are unchanged by the srem.
        Known.Zero = LHSKnown.Zero & LowBits;
        Known.One = LHSKnown.One & LowBits;

        // If LHS is non-negative or has all low bits zero, then the upper bits
        // are all zero.
        if (LHSKnown.isNonNegative() || LowBits.isSubsetOf(LHSKnown.Zero))
          Known.Zero |= ~LowBits;

        // If LHS is negative and not all low bits are zero, then the upper bits
        // are all one.
        if (LHSKnown.isNegative() && LowBits.intersects(LHSKnown.One))
          Known.One |= ~LowBits;

        break;
      }
    }

    llvm::computeKnownBits(I, Known, Depth, Q);
    break;
  }
  case Instruction::Call: {
    bool KnownBitsComputed = false;
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::abs: {
        if (DemandedMask == 1)
          return II->getArgOperand(0);
        break;
      }
      case Intrinsic::ctpop: {
        // Checking if the number of clear bits is odd (parity)? If the type has
        // an even number of bits, that's the same as checking if the number of
        // set bits is odd, so we can eliminate the 'not' op.
        Value *X;
        if (DemandedMask == 1 && VTy->getScalarSizeInBits() % 2 == 0 &&
            match(II->getArgOperand(0), m_Not(m_Value(X)))) {
          Function *Ctpop = Intrinsic::getDeclaration(
              II->getModule(), Intrinsic::ctpop, VTy);
          return InsertNewInstWith(CallInst::Create(Ctpop, {X}), I->getIterator());
        }
        break;
      }
      case Intrinsic::bswap: {
        // If the only bits demanded come from one byte of the bswap result,
        // just shift the input byte into position to eliminate the bswap.
        unsigned NLZ = DemandedMask.countl_zero();
        unsigned NTZ = DemandedMask.countr_zero();

        // Round NTZ down to the next byte.  If we have 11 trailing zeros, then
        // we need all the bits down to bit 8.  Likewise, round NLZ.  If we
        // have 14 leading zeros, round to 8.
        NLZ = alignDown(NLZ, 8);
        NTZ = alignDown(NTZ, 8);
        // If we need exactly one byte, we can do this transformation.
        if (BitWidth - NLZ - NTZ == 8) {
          // Replace this with either a left or right shift to get the byte into
          // the right place.
          Instruction *NewVal;
          if (NLZ > NTZ)
            NewVal = BinaryOperator::CreateLShr(
                II->getArgOperand(0), ConstantInt::get(VTy, NLZ - NTZ));
          else
            NewVal = BinaryOperator::CreateShl(
                II->getArgOperand(0), ConstantInt::get(VTy, NTZ - NLZ));
          NewVal->takeName(I);
          return InsertNewInstWith(NewVal, I->getIterator());
        }
        break;
      }
      case Intrinsic::ptrmask: {
        unsigned MaskWidth = I->getOperand(1)->getType()->getScalarSizeInBits();
        RHSKnown = KnownBits(MaskWidth);
        // If either the LHS or the RHS are Zero, the result is zero.
        if (SimplifyDemandedBits(I, 0, DemandedMask, LHSKnown, Depth + 1, Q) ||
            SimplifyDemandedBits(
                I, 1, (DemandedMask & ~LHSKnown.Zero).zextOrTrunc(MaskWidth),
                RHSKnown, Depth + 1, Q))
          return I;

        // TODO: Should be 1-extend
        RHSKnown = RHSKnown.anyextOrTrunc(BitWidth);

        Known = LHSKnown & RHSKnown;
        KnownBitsComputed = true;

        // If the client is only demanding bits we know to be zero, return
        // `llvm.ptrmask(p, 0)`. We can't return `null` here due to pointer
        // provenance, but making the mask zero will be easily optimizable in
        // the backend.
        if (DemandedMask.isSubsetOf(Known.Zero) &&
            !match(I->getOperand(1), m_Zero()))
          return replaceOperand(
              *I, 1, Constant::getNullValue(I->getOperand(1)->getType()));

        // Mask in demanded space does nothing.
        // NOTE: We may have attributes associated with the return value of the
        // llvm.ptrmask intrinsic that will be lost when we just return the
        // operand. We should try to preserve them.
        if (DemandedMask.isSubsetOf(RHSKnown.One | LHSKnown.Zero))
          return I->getOperand(0);

        // If the RHS is a constant, see if we can simplify it.
        if (ShrinkDemandedConstant(
                I, 1, (DemandedMask & ~LHSKnown.Zero).zextOrTrunc(MaskWidth)))
          return I;

        // Combine:
        // (ptrmask (getelementptr i8, ptr p, imm i), imm mask)
        //   -> (ptrmask (getelementptr i8, ptr p, imm (i & mask)), imm mask)
        // where only the low bits known to be zero in the pointer are changed
        Value *InnerPtr;
        uint64_t GEPIndex;
        uint64_t PtrMaskImmediate;
        if (match(I, m_Intrinsic<Intrinsic::ptrmask>(
                         m_PtrAdd(m_Value(InnerPtr), m_ConstantInt(GEPIndex)),
                         m_ConstantInt(PtrMaskImmediate)))) {

          LHSKnown = computeKnownBits(InnerPtr, Depth + 1, I);
          if (!LHSKnown.isZero()) {
            const unsigned trailingZeros = LHSKnown.countMinTrailingZeros();
            uint64_t PointerAlignBits = (uint64_t(1) << trailingZeros) - 1;

            uint64_t HighBitsGEPIndex = GEPIndex & ~PointerAlignBits;
            uint64_t MaskedLowBitsGEPIndex =
                GEPIndex & PointerAlignBits & PtrMaskImmediate;

            uint64_t MaskedGEPIndex = HighBitsGEPIndex | MaskedLowBitsGEPIndex;

            if (MaskedGEPIndex != GEPIndex) {
              auto *GEP = cast<GEPOperator>(II->getArgOperand(0));
              Builder.SetInsertPoint(I);
              Type *GEPIndexType =
                  DL.getIndexType(GEP->getPointerOperand()->getType());
              Value *MaskedGEP = Builder.CreateGEP(
                  GEP->getSourceElementType(), InnerPtr,
                  ConstantInt::get(GEPIndexType, MaskedGEPIndex),
                  GEP->getName(), GEP->isInBounds());

              replaceOperand(*I, 0, MaskedGEP);
              return I;
            }
          }
        }

        break;
      }

      case Intrinsic::fshr:
      case Intrinsic::fshl: {
        const APInt *SA;
        if (!match(I->getOperand(2), m_APInt(SA)))
          break;

        // Normalize to funnel shift left. APInt shifts of BitWidth are well-
        // defined, so no need to special-case zero shifts here.
        uint64_t ShiftAmt = SA->urem(BitWidth);
        if (II->getIntrinsicID() == Intrinsic::fshr)
          ShiftAmt = BitWidth - ShiftAmt;

        APInt DemandedMaskLHS(DemandedMask.lshr(ShiftAmt));
        APInt DemandedMaskRHS(DemandedMask.shl(BitWidth - ShiftAmt));
        if (I->getOperand(0) != I->getOperand(1)) {
          if (SimplifyDemandedBits(I, 0, DemandedMaskLHS, LHSKnown,
                                   Depth + 1, Q) ||
              SimplifyDemandedBits(I, 1, DemandedMaskRHS, RHSKnown, Depth + 1,
                                   Q))
            return I;
        } else { // fshl is a rotate
          // Avoid converting rotate into funnel shift.
          // Only simplify if one operand is constant.
          LHSKnown = computeKnownBits(I->getOperand(0), Depth + 1, I);
          if (DemandedMaskLHS.isSubsetOf(LHSKnown.Zero | LHSKnown.One) &&
              !match(I->getOperand(0), m_SpecificInt(LHSKnown.One))) {
            replaceOperand(*I, 0, Constant::getIntegerValue(VTy, LHSKnown.One));
            return I;
          }

          RHSKnown = computeKnownBits(I->getOperand(1), Depth + 1, I);
          if (DemandedMaskRHS.isSubsetOf(RHSKnown.Zero | RHSKnown.One) &&
              !match(I->getOperand(1), m_SpecificInt(RHSKnown.One))) {
            replaceOperand(*I, 1, Constant::getIntegerValue(VTy, RHSKnown.One));
            return I;
          }
        }

        Known.Zero = LHSKnown.Zero.shl(ShiftAmt) |
                     RHSKnown.Zero.lshr(BitWidth - ShiftAmt);
        Known.One = LHSKnown.One.shl(ShiftAmt) |
                    RHSKnown.One.lshr(BitWidth - ShiftAmt);
        KnownBitsComputed = true;
        break;
      }
      case Intrinsic::umax: {
        // UMax(A, C) == A if ...
        // The lowest non-zero bit of DemandMask is higher than the highest
        // non-zero bit of C.
        const APInt *C;
        unsigned CTZ = DemandedMask.countr_zero();
        if (match(II->getArgOperand(1), m_APInt(C)) &&
            CTZ >= C->getActiveBits())
          return II->getArgOperand(0);
        break;
      }
      case Intrinsic::umin: {
        // UMin(A, C) == A if ...
        // The lowest non-zero bit of DemandMask is higher than the highest
        // non-one bit of C.
        // This comes from using DeMorgans on the above umax example.
        const APInt *C;
        unsigned CTZ = DemandedMask.countr_zero();
        if (match(II->getArgOperand(1), m_APInt(C)) &&
            CTZ >= C->getBitWidth() - C->countl_one())
          return II->getArgOperand(0);
        break;
      }
      default: {
        // Handle target specific intrinsics
        std::optional<Value *> V = targetSimplifyDemandedUseBitsIntrinsic(
            *II, DemandedMask, Known, KnownBitsComputed);
        if (V)
          return *V;
        break;
      }
      }
    }

    if (!KnownBitsComputed)
      llvm::computeKnownBits(I, Known, Depth, Q);
    break;
  }
  }

  if (I->getType()->isPointerTy()) {
    Align Alignment = I->getPointerAlignment(DL);
    Known.Zero.setLowBits(Log2(Alignment));
  }

  // If the client is only demanding bits that we know, return the known
  // constant. We can't directly simplify pointers as a constant because of
  // pointer provenance.
  // TODO: We could return `(inttoptr const)` for pointers.
  if (!I->getType()->isPointerTy() &&
      DemandedMask.isSubsetOf(Known.Zero | Known.One))
    return Constant::getIntegerValue(VTy, Known.One);

  if (VerifyKnownBits) {
    KnownBits ReferenceKnown = llvm::computeKnownBits(I, Depth, Q);
    if (Known != ReferenceKnown) {
      errs() << "Mismatched known bits for " << *I << " in "
             << I->getFunction()->getName() << "\n";
      errs() << "computeKnownBits(): " << ReferenceKnown << "\n";
      errs() << "SimplifyDemandedBits(): " << Known << "\n";
      std::abort();
    }
  }

  return nullptr;
}

/// Helper routine of SimplifyDemandedUseBits. It computes Known
/// bits. It also tries to handle simplifications that can be done based on
/// DemandedMask, but without modifying the Instruction.
Value *InstCombinerImpl::SimplifyMultipleUseDemandedBits(
    Instruction *I, const APInt &DemandedMask, KnownBits &Known, unsigned Depth,
    const SimplifyQuery &Q) {
  unsigned BitWidth = DemandedMask.getBitWidth();
  Type *ITy = I->getType();

  KnownBits LHSKnown(BitWidth);
  KnownBits RHSKnown(BitWidth);

  // Despite the fact that we can't simplify this instruction in all User's
  // context, we can at least compute the known bits, and we can
  // do simplifications that apply to *just* the one user if we know that
  // this instruction has a simpler value in that context.
  switch (I->getOpcode()) {
  case Instruction::And: {
    llvm::computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, Q);
    llvm::computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1, Q);
    Known = analyzeKnownBitsFromAndXorOr(cast<Operator>(I), LHSKnown, RHSKnown,
                                         Depth, Q);
    computeKnownBitsFromContext(I, Known, Depth, Q);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero | Known.One))
      return Constant::getIntegerValue(ITy, Known.One);

    // If all of the demanded bits are known 1 on one side, return the other.
    // These bits cannot contribute to the result of the 'and' in this context.
    if (DemandedMask.isSubsetOf(LHSKnown.Zero | RHSKnown.One))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.Zero | LHSKnown.One))
      return I->getOperand(1);

    break;
  }
  case Instruction::Or: {
    llvm::computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, Q);
    llvm::computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1, Q);
    Known = analyzeKnownBitsFromAndXorOr(cast<Operator>(I), LHSKnown, RHSKnown,
                                         Depth, Q);
    computeKnownBitsFromContext(I, Known, Depth, Q);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero | Known.One))
      return Constant::getIntegerValue(ITy, Known.One);

    // We can simplify (X|Y) -> X or Y in the user's context if we know that
    // only bits from X or Y are demanded.
    // If all of the demanded bits are known zero on one side, return the other.
    // These bits cannot contribute to the result of the 'or' in this context.
    if (DemandedMask.isSubsetOf(LHSKnown.One | RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(RHSKnown.One | LHSKnown.Zero))
      return I->getOperand(1);

    break;
  }
  case Instruction::Xor: {
    llvm::computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, Q);
    llvm::computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1, Q);
    Known = analyzeKnownBitsFromAndXorOr(cast<Operator>(I), LHSKnown, RHSKnown,
                                         Depth, Q);
    computeKnownBitsFromContext(I, Known, Depth, Q);

    // If the client is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero | Known.One))
      return Constant::getIntegerValue(ITy, Known.One);

    // We can simplify (X^Y) -> X or Y in the user's context if we know that
    // only bits from X or Y are demanded.
    // If all of the demanded bits are known zero on one side, return the other.
    if (DemandedMask.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);
    if (DemandedMask.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    break;
  }
  case Instruction::Add: {
    unsigned NLZ = DemandedMask.countl_zero();
    APInt DemandedFromOps = APInt::getLowBitsSet(BitWidth, BitWidth - NLZ);

    // If an operand adds zeros to every bit below the highest demanded bit,
    // that operand doesn't change the result. Return the other side.
    llvm::computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, Q);
    if (DemandedFromOps.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);

    llvm::computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1, Q);
    if (DemandedFromOps.isSubsetOf(LHSKnown.Zero))
      return I->getOperand(1);

    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    bool NUW = cast<OverflowingBinaryOperator>(I)->hasNoUnsignedWrap();
    Known =
        KnownBits::computeForAddSub(/*Add=*/true, NSW, NUW, LHSKnown, RHSKnown);
    computeKnownBitsFromContext(I, Known, Depth, Q);
    break;
  }
  case Instruction::Sub: {
    unsigned NLZ = DemandedMask.countl_zero();
    APInt DemandedFromOps = APInt::getLowBitsSet(BitWidth, BitWidth - NLZ);

    // If an operand subtracts zeros from every bit below the highest demanded
    // bit, that operand doesn't change the result. Return the other side.
    llvm::computeKnownBits(I->getOperand(1), RHSKnown, Depth + 1, Q);
    if (DemandedFromOps.isSubsetOf(RHSKnown.Zero))
      return I->getOperand(0);

    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    bool NUW = cast<OverflowingBinaryOperator>(I)->hasNoUnsignedWrap();
    llvm::computeKnownBits(I->getOperand(0), LHSKnown, Depth + 1, Q);
    Known = KnownBits::computeForAddSub(/*Add=*/false, NSW, NUW, LHSKnown,
                                        RHSKnown);
    computeKnownBitsFromContext(I, Known, Depth, Q);
    break;
  }
  case Instruction::AShr: {
    // Compute the Known bits to simplify things downstream.
    llvm::computeKnownBits(I, Known, Depth, Q);

    // If this user is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero | Known.One))
      return Constant::getIntegerValue(ITy, Known.One);

    // If the right shift operand 0 is a result of a left shift by the same
    // amount, this is probably a zero/sign extension, which may be unnecessary,
    // if we do not demand any of the new sign bits. So, return the original
    // operand instead.
    const APInt *ShiftRC;
    const APInt *ShiftLC;
    Value *X;
    unsigned BitWidth = DemandedMask.getBitWidth();
    if (match(I,
              m_AShr(m_Shl(m_Value(X), m_APInt(ShiftLC)), m_APInt(ShiftRC))) &&
        ShiftLC == ShiftRC && ShiftLC->ult(BitWidth) &&
        DemandedMask.isSubsetOf(APInt::getLowBitsSet(
            BitWidth, BitWidth - ShiftRC->getZExtValue()))) {
      return X;
    }

    break;
  }
  default:
    // Compute the Known bits to simplify things downstream.
    llvm::computeKnownBits(I, Known, Depth, Q);

    // If this user is only demanding bits that we know, return the known
    // constant.
    if (DemandedMask.isSubsetOf(Known.Zero|Known.One))
      return Constant::getIntegerValue(ITy, Known.One);

    break;
  }

  return nullptr;
}

/// Helper routine of SimplifyDemandedUseBits. It tries to simplify
/// "E1 = (X lsr C1) << C2", where the C1 and C2 are constant, into
/// "E2 = X << (C2 - C1)" or "E2 = X >> (C1 - C2)", depending on the sign
/// of "C2-C1".
///
/// Suppose E1 and E2 are generally different in bits S={bm, bm+1,
/// ..., bn}, without considering the specific value X is holding.
/// This transformation is legal iff one of following conditions is hold:
///  1) All the bit in S are 0, in this case E1 == E2.
///  2) We don't care those bits in S, per the input DemandedMask.
///  3) Combination of 1) and 2). Some bits in S are 0, and we don't care the
///     rest bits.
///
/// Currently we only test condition 2).
///
/// As with SimplifyDemandedUseBits, it returns NULL if the simplification was
/// not successful.
Value *InstCombinerImpl::simplifyShrShlDemandedBits(
    Instruction *Shr, const APInt &ShrOp1, Instruction *Shl,
    const APInt &ShlOp1, const APInt &DemandedMask, KnownBits &Known) {
  if (!ShlOp1 || !ShrOp1)
    return nullptr; // No-op.

  Value *VarX = Shr->getOperand(0);
  Type *Ty = VarX->getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();
  if (ShlOp1.uge(BitWidth) || ShrOp1.uge(BitWidth))
    return nullptr; // Undef.

  unsigned ShlAmt = ShlOp1.getZExtValue();
  unsigned ShrAmt = ShrOp1.getZExtValue();

  Known.One.clearAllBits();
  Known.Zero.setLowBits(ShlAmt - 1);
  Known.Zero &= DemandedMask;

  APInt BitMask1(APInt::getAllOnes(BitWidth));
  APInt BitMask2(APInt::getAllOnes(BitWidth));

  bool isLshr = (Shr->getOpcode() == Instruction::LShr);
  BitMask1 = isLshr ? (BitMask1.lshr(ShrAmt) << ShlAmt) :
                      (BitMask1.ashr(ShrAmt) << ShlAmt);

  if (ShrAmt <= ShlAmt) {
    BitMask2 <<= (ShlAmt - ShrAmt);
  } else {
    BitMask2 = isLshr ? BitMask2.lshr(ShrAmt - ShlAmt):
                        BitMask2.ashr(ShrAmt - ShlAmt);
  }

  // Check if condition-2 (see the comment to this function) is satified.
  if ((BitMask1 & DemandedMask) == (BitMask2 & DemandedMask)) {
    if (ShrAmt == ShlAmt)
      return VarX;

    if (!Shr->hasOneUse())
      return nullptr;

    BinaryOperator *New;
    if (ShrAmt < ShlAmt) {
      Constant *Amt = ConstantInt::get(VarX->getType(), ShlAmt - ShrAmt);
      New = BinaryOperator::CreateShl(VarX, Amt);
      BinaryOperator *Orig = cast<BinaryOperator>(Shl);
      New->setHasNoSignedWrap(Orig->hasNoSignedWrap());
      New->setHasNoUnsignedWrap(Orig->hasNoUnsignedWrap());
    } else {
      Constant *Amt = ConstantInt::get(VarX->getType(), ShrAmt - ShlAmt);
      New = isLshr ? BinaryOperator::CreateLShr(VarX, Amt) :
                     BinaryOperator::CreateAShr(VarX, Amt);
      if (cast<BinaryOperator>(Shr)->isExact())
        New->setIsExact(true);
    }

    return InsertNewInstWith(New, Shl->getIterator());
  }

  return nullptr;
}

/// The specified value produces a vector with any number of elements.
/// This method analyzes which elements of the operand are poison and
/// returns that information in PoisonElts.
///
/// DemandedElts contains the set of elements that are actually used by the
/// caller, and by default (AllowMultipleUsers equals false) the value is
/// simplified only if it has a single caller. If AllowMultipleUsers is set
/// to true, DemandedElts refers to the union of sets of elements that are
/// used by all callers.
///
/// If the information about demanded elements can be used to simplify the
/// operation, the operation is simplified, then the resultant value is
/// returned.  This returns null if no change was made.
Value *InstCombinerImpl::SimplifyDemandedVectorElts(Value *V,
                                                    APInt DemandedElts,
                                                    APInt &PoisonElts,
                                                    unsigned Depth,
                                                    bool AllowMultipleUsers) {
  // Cannot analyze scalable type. The number of vector elements is not a
  // compile-time constant.
  if (isa<ScalableVectorType>(V->getType()))
    return nullptr;

  unsigned VWidth = cast<FixedVectorType>(V->getType())->getNumElements();
  APInt EltMask(APInt::getAllOnes(VWidth));
  assert((DemandedElts & ~EltMask) == 0 && "Invalid DemandedElts!");

  if (match(V, m_Poison())) {
    // If the entire vector is poison, just return this info.
    PoisonElts = EltMask;
    return nullptr;
  }

  if (DemandedElts.isZero()) { // If nothing is demanded, provide poison.
    PoisonElts = EltMask;
    return PoisonValue::get(V->getType());
  }

  PoisonElts = 0;

  if (auto *C = dyn_cast<Constant>(V)) {
    // Check if this is identity. If so, return 0 since we are not simplifying
    // anything.
    if (DemandedElts.isAllOnes())
      return nullptr;

    Type *EltTy = cast<VectorType>(V->getType())->getElementType();
    Constant *Poison = PoisonValue::get(EltTy);
    SmallVector<Constant*, 16> Elts;
    for (unsigned i = 0; i != VWidth; ++i) {
      if (!DemandedElts[i]) {   // If not demanded, set to poison.
        Elts.push_back(Poison);
        PoisonElts.setBit(i);
        continue;
      }

      Constant *Elt = C->getAggregateElement(i);
      if (!Elt) return nullptr;

      Elts.push_back(Elt);
      if (isa<PoisonValue>(Elt)) // Already poison.
        PoisonElts.setBit(i);
    }

    // If we changed the constant, return it.
    Constant *NewCV = ConstantVector::get(Elts);
    return NewCV != C ? NewCV : nullptr;
  }

  // Limit search depth.
  if (Depth == 10)
    return nullptr;

  if (!AllowMultipleUsers) {
    // If multiple users are using the root value, proceed with
    // simplification conservatively assuming that all elements
    // are needed.
    if (!V->hasOneUse()) {
      // Quit if we find multiple users of a non-root value though.
      // They'll be handled when it's their turn to be visited by
      // the main instcombine process.
      if (Depth != 0)
        // TODO: Just compute the PoisonElts information recursively.
        return nullptr;

      // Conservatively assume that all elements are needed.
      DemandedElts = EltMask;
    }
  }

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return nullptr;        // Only analyze instructions.

  bool MadeChange = false;
  auto simplifyAndSetOp = [&](Instruction *Inst, unsigned OpNum,
                              APInt Demanded, APInt &Undef) {
    auto *II = dyn_cast<IntrinsicInst>(Inst);
    Value *Op = II ? II->getArgOperand(OpNum) : Inst->getOperand(OpNum);
    if (Value *V = SimplifyDemandedVectorElts(Op, Demanded, Undef, Depth + 1)) {
      replaceOperand(*Inst, OpNum, V);
      MadeChange = true;
    }
  };

  APInt PoisonElts2(VWidth, 0);
  APInt PoisonElts3(VWidth, 0);
  switch (I->getOpcode()) {
  default: break;

  case Instruction::GetElementPtr: {
    // The LangRef requires that struct geps have all constant indices.  As
    // such, we can't convert any operand to partial undef.
    auto mayIndexStructType = [](GetElementPtrInst &GEP) {
      for (auto I = gep_type_begin(GEP), E = gep_type_end(GEP);
           I != E; I++)
        if (I.isStruct())
          return true;
      return false;
    };
    if (mayIndexStructType(cast<GetElementPtrInst>(*I)))
      break;

    // Conservatively track the demanded elements back through any vector
    // operands we may have.  We know there must be at least one, or we
    // wouldn't have a vector result to get here. Note that we intentionally
    // merge the undef bits here since gepping with either an poison base or
    // index results in poison.
    for (unsigned i = 0; i < I->getNumOperands(); i++) {
      if (i == 0 ? match(I->getOperand(i), m_Undef())
                 : match(I->getOperand(i), m_Poison())) {
        // If the entire vector is undefined, just return this info.
        PoisonElts = EltMask;
        return nullptr;
      }
      if (I->getOperand(i)->getType()->isVectorTy()) {
        APInt PoisonEltsOp(VWidth, 0);
        simplifyAndSetOp(I, i, DemandedElts, PoisonEltsOp);
        // gep(x, undef) is not undef, so skip considering idx ops here
        // Note that we could propagate poison, but we can't distinguish between
        // undef & poison bits ATM
        if (i == 0)
          PoisonElts |= PoisonEltsOp;
      }
    }

    break;
  }
  case Instruction::InsertElement: {
    // If this is a variable index, we don't know which element it overwrites.
    // demand exactly the same input as we produce.
    ConstantInt *Idx = dyn_cast<ConstantInt>(I->getOperand(2));
    if (!Idx) {
      // Note that we can't propagate undef elt info, because we don't know
      // which elt is getting updated.
      simplifyAndSetOp(I, 0, DemandedElts, PoisonElts2);
      break;
    }

    // The element inserted overwrites whatever was there, so the input demanded
    // set is simpler than the output set.
    unsigned IdxNo = Idx->getZExtValue();
    APInt PreInsertDemandedElts = DemandedElts;
    if (IdxNo < VWidth)
      PreInsertDemandedElts.clearBit(IdxNo);

    // If we only demand the element that is being inserted and that element
    // was extracted from the same index in another vector with the same type,
    // replace this insert with that other vector.
    // Note: This is attempted before the call to simplifyAndSetOp because that
    //       may change PoisonElts to a value that does not match with Vec.
    Value *Vec;
    if (PreInsertDemandedElts == 0 &&
        match(I->getOperand(1),
              m_ExtractElt(m_Value(Vec), m_SpecificInt(IdxNo))) &&
        Vec->getType() == I->getType()) {
      return Vec;
    }

    simplifyAndSetOp(I, 0, PreInsertDemandedElts, PoisonElts);

    // If this is inserting an element that isn't demanded, remove this
    // insertelement.
    if (IdxNo >= VWidth || !DemandedElts[IdxNo]) {
      Worklist.push(I);
      return I->getOperand(0);
    }

    // The inserted element is defined.
    PoisonElts.clearBit(IdxNo);
    break;
  }
  case Instruction::ShuffleVector: {
    auto *Shuffle = cast<ShuffleVectorInst>(I);
    assert(Shuffle->getOperand(0)->getType() ==
           Shuffle->getOperand(1)->getType() &&
           "Expected shuffle operands to have same type");
    unsigned OpWidth = cast<FixedVectorType>(Shuffle->getOperand(0)->getType())
                           ->getNumElements();
    // Handle trivial case of a splat. Only check the first element of LHS
    // operand.
    if (all_of(Shuffle->getShuffleMask(), [](int Elt) { return Elt == 0; }) &&
        DemandedElts.isAllOnes()) {
      if (!isa<PoisonValue>(I->getOperand(1))) {
        I->setOperand(1, PoisonValue::get(I->getOperand(1)->getType()));
        MadeChange = true;
      }
      APInt LeftDemanded(OpWidth, 1);
      APInt LHSPoisonElts(OpWidth, 0);
      simplifyAndSetOp(I, 0, LeftDemanded, LHSPoisonElts);
      if (LHSPoisonElts[0])
        PoisonElts = EltMask;
      else
        PoisonElts.clearAllBits();
      break;
    }

    APInt LeftDemanded(OpWidth, 0), RightDemanded(OpWidth, 0);
    for (unsigned i = 0; i < VWidth; i++) {
      if (DemandedElts[i]) {
        unsigned MaskVal = Shuffle->getMaskValue(i);
        if (MaskVal != -1u) {
          assert(MaskVal < OpWidth * 2 &&
                 "shufflevector mask index out of range!");
          if (MaskVal < OpWidth)
            LeftDemanded.setBit(MaskVal);
          else
            RightDemanded.setBit(MaskVal - OpWidth);
        }
      }
    }

    APInt LHSPoisonElts(OpWidth, 0);
    simplifyAndSetOp(I, 0, LeftDemanded, LHSPoisonElts);

    APInt RHSPoisonElts(OpWidth, 0);
    simplifyAndSetOp(I, 1, RightDemanded, RHSPoisonElts);

    // If this shuffle does not change the vector length and the elements
    // demanded by this shuffle are an identity mask, then this shuffle is
    // unnecessary.
    //
    // We are assuming canonical form for the mask, so the source vector is
    // operand 0 and operand 1 is not used.
    //
    // Note that if an element is demanded and this shuffle mask is undefined
    // for that element, then the shuffle is not considered an identity
    // operation. The shuffle prevents poison from the operand vector from
    // leaking to the result by replacing poison with an undefined value.
    if (VWidth == OpWidth) {
      bool IsIdentityShuffle = true;
      for (unsigned i = 0; i < VWidth; i++) {
        unsigned MaskVal = Shuffle->getMaskValue(i);
        if (DemandedElts[i] && i != MaskVal) {
          IsIdentityShuffle = false;
          break;
        }
      }
      if (IsIdentityShuffle)
        return Shuffle->getOperand(0);
    }

    bool NewPoisonElts = false;
    unsigned LHSIdx = -1u, LHSValIdx = -1u;
    unsigned RHSIdx = -1u, RHSValIdx = -1u;
    bool LHSUniform = true;
    bool RHSUniform = true;
    for (unsigned i = 0; i < VWidth; i++) {
      unsigned MaskVal = Shuffle->getMaskValue(i);
      if (MaskVal == -1u) {
        PoisonElts.setBit(i);
      } else if (!DemandedElts[i]) {
        NewPoisonElts = true;
        PoisonElts.setBit(i);
      } else if (MaskVal < OpWidth) {
        if (LHSPoisonElts[MaskVal]) {
          NewPoisonElts = true;
          PoisonElts.setBit(i);
        } else {
          LHSIdx = LHSIdx == -1u ? i : OpWidth;
          LHSValIdx = LHSValIdx == -1u ? MaskVal : OpWidth;
          LHSUniform = LHSUniform && (MaskVal == i);
        }
      } else {
        if (RHSPoisonElts[MaskVal - OpWidth]) {
          NewPoisonElts = true;
          PoisonElts.setBit(i);
        } else {
          RHSIdx = RHSIdx == -1u ? i : OpWidth;
          RHSValIdx = RHSValIdx == -1u ? MaskVal - OpWidth : OpWidth;
          RHSUniform = RHSUniform && (MaskVal - OpWidth == i);
        }
      }
    }

    // Try to transform shuffle with constant vector and single element from
    // this constant vector to single insertelement instruction.
    // shufflevector V, C, <v1, v2, .., ci, .., vm> ->
    // insertelement V, C[ci], ci-n
    if (OpWidth ==
        cast<FixedVectorType>(Shuffle->getType())->getNumElements()) {
      Value *Op = nullptr;
      Constant *Value = nullptr;
      unsigned Idx = -1u;

      // Find constant vector with the single element in shuffle (LHS or RHS).
      if (LHSIdx < OpWidth && RHSUniform) {
        if (auto *CV = dyn_cast<ConstantVector>(Shuffle->getOperand(0))) {
          Op = Shuffle->getOperand(1);
          Value = CV->getOperand(LHSValIdx);
          Idx = LHSIdx;
        }
      }
      if (RHSIdx < OpWidth && LHSUniform) {
        if (auto *CV = dyn_cast<ConstantVector>(Shuffle->getOperand(1))) {
          Op = Shuffle->getOperand(0);
          Value = CV->getOperand(RHSValIdx);
          Idx = RHSIdx;
        }
      }
      // Found constant vector with single element - convert to insertelement.
      if (Op && Value) {
        Instruction *New = InsertElementInst::Create(
            Op, Value, ConstantInt::get(Type::getInt64Ty(I->getContext()), Idx),
            Shuffle->getName());
        InsertNewInstWith(New, Shuffle->getIterator());
        return New;
      }
    }
    if (NewPoisonElts) {
      // Add additional discovered undefs.
      SmallVector<int, 16> Elts;
      for (unsigned i = 0; i < VWidth; ++i) {
        if (PoisonElts[i])
          Elts.push_back(PoisonMaskElem);
        else
          Elts.push_back(Shuffle->getMaskValue(i));
      }
      Shuffle->setShuffleMask(Elts);
      MadeChange = true;
    }
    break;
  }
  case Instruction::Select: {
    // If this is a vector select, try to transform the select condition based
    // on the current demanded elements.
    SelectInst *Sel = cast<SelectInst>(I);
    if (Sel->getCondition()->getType()->isVectorTy()) {
      // TODO: We are not doing anything with PoisonElts based on this call.
      // It is overwritten below based on the other select operands. If an
      // element of the select condition is known undef, then we are free to
      // choose the output value from either arm of the select. If we know that
      // one of those values is undef, then the output can be undef.
      simplifyAndSetOp(I, 0, DemandedElts, PoisonElts);
    }

    // Next, see if we can transform the arms of the select.
    APInt DemandedLHS(DemandedElts), DemandedRHS(DemandedElts);
    if (auto *CV = dyn_cast<ConstantVector>(Sel->getCondition())) {
      for (unsigned i = 0; i < VWidth; i++) {
        // isNullValue() always returns false when called on a ConstantExpr.
        // Skip constant expressions to avoid propagating incorrect information.
        Constant *CElt = CV->getAggregateElement(i);
        if (isa<ConstantExpr>(CElt))
          continue;
        // TODO: If a select condition element is undef, we can demand from
        // either side. If one side is known undef, choosing that side would
        // propagate undef.
        if (CElt->isNullValue())
          DemandedLHS.clearBit(i);
        else
          DemandedRHS.clearBit(i);
      }
    }

    simplifyAndSetOp(I, 1, DemandedLHS, PoisonElts2);
    simplifyAndSetOp(I, 2, DemandedRHS, PoisonElts3);

    // Output elements are undefined if the element from each arm is undefined.
    // TODO: This can be improved. See comment in select condition handling.
    PoisonElts = PoisonElts2 & PoisonElts3;
    break;
  }
  case Instruction::BitCast: {
    // Vector->vector casts only.
    VectorType *VTy = dyn_cast<VectorType>(I->getOperand(0)->getType());
    if (!VTy) break;
    unsigned InVWidth = cast<FixedVectorType>(VTy)->getNumElements();
    APInt InputDemandedElts(InVWidth, 0);
    PoisonElts2 = APInt(InVWidth, 0);
    unsigned Ratio;

    if (VWidth == InVWidth) {
      // If we are converting from <4 x i32> -> <4 x f32>, we demand the same
      // elements as are demanded of us.
      Ratio = 1;
      InputDemandedElts = DemandedElts;
    } else if ((VWidth % InVWidth) == 0) {
      // If the number of elements in the output is a multiple of the number of
      // elements in the input then an input element is live if any of the
      // corresponding output elements are live.
      Ratio = VWidth / InVWidth;
      for (unsigned OutIdx = 0; OutIdx != VWidth; ++OutIdx)
        if (DemandedElts[OutIdx])
          InputDemandedElts.setBit(OutIdx / Ratio);
    } else if ((InVWidth % VWidth) == 0) {
      // If the number of elements in the input is a multiple of the number of
      // elements in the output then an input element is live if the
      // corresponding output element is live.
      Ratio = InVWidth / VWidth;
      for (unsigned InIdx = 0; InIdx != InVWidth; ++InIdx)
        if (DemandedElts[InIdx / Ratio])
          InputDemandedElts.setBit(InIdx);
    } else {
      // Unsupported so far.
      break;
    }

    simplifyAndSetOp(I, 0, InputDemandedElts, PoisonElts2);

    if (VWidth == InVWidth) {
      PoisonElts = PoisonElts2;
    } else if ((VWidth % InVWidth) == 0) {
      // If the number of elements in the output is a multiple of the number of
      // elements in the input then an output element is undef if the
      // corresponding input element is undef.
      for (unsigned OutIdx = 0; OutIdx != VWidth; ++OutIdx)
        if (PoisonElts2[OutIdx / Ratio])
          PoisonElts.setBit(OutIdx);
    } else if ((InVWidth % VWidth) == 0) {
      // If the number of elements in the input is a multiple of the number of
      // elements in the output then an output element is undef if all of the
      // corresponding input elements are undef.
      for (unsigned OutIdx = 0; OutIdx != VWidth; ++OutIdx) {
        APInt SubUndef = PoisonElts2.lshr(OutIdx * Ratio).zextOrTrunc(Ratio);
        if (SubUndef.popcount() == Ratio)
          PoisonElts.setBit(OutIdx);
      }
    } else {
      llvm_unreachable("Unimp");
    }
    break;
  }
  case Instruction::FPTrunc:
  case Instruction::FPExt:
    simplifyAndSetOp(I, 0, DemandedElts, PoisonElts);
    break;

  case Instruction::Call: {
    IntrinsicInst *II = dyn_cast<IntrinsicInst>(I);
    if (!II) break;
    switch (II->getIntrinsicID()) {
    case Intrinsic::masked_gather: // fallthrough
    case Intrinsic::masked_load: {
      // Subtlety: If we load from a pointer, the pointer must be valid
      // regardless of whether the element is demanded.  Doing otherwise risks
      // segfaults which didn't exist in the original program.
      APInt DemandedPtrs(APInt::getAllOnes(VWidth)),
          DemandedPassThrough(DemandedElts);
      if (auto *CV = dyn_cast<ConstantVector>(II->getOperand(2)))
        for (unsigned i = 0; i < VWidth; i++) {
          Constant *CElt = CV->getAggregateElement(i);
          if (CElt->isNullValue())
            DemandedPtrs.clearBit(i);
          else if (CElt->isAllOnesValue())
            DemandedPassThrough.clearBit(i);
        }
      if (II->getIntrinsicID() == Intrinsic::masked_gather)
        simplifyAndSetOp(II, 0, DemandedPtrs, PoisonElts2);
      simplifyAndSetOp(II, 3, DemandedPassThrough, PoisonElts3);

      // Output elements are undefined if the element from both sources are.
      // TODO: can strengthen via mask as well.
      PoisonElts = PoisonElts2 & PoisonElts3;
      break;
    }
    default: {
      // Handle target specific intrinsics
      std::optional<Value *> V = targetSimplifyDemandedVectorEltsIntrinsic(
          *II, DemandedElts, PoisonElts, PoisonElts2, PoisonElts3,
          simplifyAndSetOp);
      if (V)
        return *V;
      break;
    }
    } // switch on IntrinsicID
    break;
  } // case Call
  } // switch on Opcode

  // TODO: We bail completely on integer div/rem and shifts because they have
  // UB/poison potential, but that should be refined.
  BinaryOperator *BO;
  if (match(I, m_BinOp(BO)) && !BO->isIntDivRem() && !BO->isShift()) {
    Value *X = BO->getOperand(0);
    Value *Y = BO->getOperand(1);

    // Look for an equivalent binop except that one operand has been shuffled.
    // If the demand for this binop only includes elements that are the same as
    // the other binop, then we may be able to replace this binop with a use of
    // the earlier one.
    //
    // Example:
    // %other_bo = bo (shuf X, {0}), Y
    // %this_extracted_bo = extelt (bo X, Y), 0
    // -->
    // %other_bo = bo (shuf X, {0}), Y
    // %this_extracted_bo = extelt %other_bo, 0
    //
    // TODO: Handle demand of an arbitrary single element or more than one
    //       element instead of just element 0.
    // TODO: Unlike general demanded elements transforms, this should be safe
    //       for any (div/rem/shift) opcode too.
    if (DemandedElts == 1 && !X->hasOneUse() && !Y->hasOneUse() &&
        BO->hasOneUse() ) {

      auto findShufBO = [&](bool MatchShufAsOp0) -> User * {
        // Try to use shuffle-of-operand in place of an operand:
        // bo X, Y --> bo (shuf X), Y
        // bo X, Y --> bo X, (shuf Y)
        BinaryOperator::BinaryOps Opcode = BO->getOpcode();
        Value *ShufOp = MatchShufAsOp0 ? X : Y;
        Value *OtherOp = MatchShufAsOp0 ? Y : X;
        for (User *U : OtherOp->users()) {
          ArrayRef<int> Mask;
          auto Shuf = m_Shuffle(m_Specific(ShufOp), m_Value(), m_Mask(Mask));
          if (BO->isCommutative()
                  ? match(U, m_c_BinOp(Opcode, Shuf, m_Specific(OtherOp)))
                  : MatchShufAsOp0
                        ? match(U, m_BinOp(Opcode, Shuf, m_Specific(OtherOp)))
                        : match(U, m_BinOp(Opcode, m_Specific(OtherOp), Shuf)))
            if (match(Mask, m_ZeroMask()) && Mask[0] != PoisonMaskElem)
              if (DT.dominates(U, I))
                return U;
        }
        return nullptr;
      };

      if (User *ShufBO = findShufBO(/* MatchShufAsOp0 */ true))
        return ShufBO;
      if (User *ShufBO = findShufBO(/* MatchShufAsOp0 */ false))
        return ShufBO;
    }

    simplifyAndSetOp(I, 0, DemandedElts, PoisonElts);
    simplifyAndSetOp(I, 1, DemandedElts, PoisonElts2);

    // Output elements are undefined if both are undefined. Consider things
    // like undef & 0. The result is known zero, not undef.
    PoisonElts &= PoisonElts2;
  }

  // If we've proven all of the lanes poison, return a poison value.
  // TODO: Intersect w/demanded lanes
  if (PoisonElts.isAllOnes())
    return PoisonValue::get(I->getType());

  return MadeChange ? I : nullptr;
}

/// For floating-point classes that resolve to a single bit pattern, return that
/// value.
static Constant *getFPClassConstant(Type *Ty, FPClassTest Mask) {
  switch (Mask) {
  case fcPosZero:
    return ConstantFP::getZero(Ty);
  case fcNegZero:
    return ConstantFP::getZero(Ty, true);
  case fcPosInf:
    return ConstantFP::getInfinity(Ty);
  case fcNegInf:
    return ConstantFP::getInfinity(Ty, true);
  case fcNone:
    return PoisonValue::get(Ty);
  default:
    return nullptr;
  }
}

Value *InstCombinerImpl::SimplifyDemandedUseFPClass(
    Value *V, const FPClassTest DemandedMask, KnownFPClass &Known,
    unsigned Depth, Instruction *CxtI) {
  assert(Depth <= MaxAnalysisRecursionDepth && "Limit Search Depth");
  Type *VTy = V->getType();

  assert(Known == KnownFPClass() && "expected uninitialized state");

  if (DemandedMask == fcNone)
    return isa<UndefValue>(V) ? nullptr : PoisonValue::get(VTy);

  if (Depth == MaxAnalysisRecursionDepth)
    return nullptr;

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) {
    // Handle constants and arguments
    Known = computeKnownFPClass(V, fcAllFlags, CxtI, Depth + 1);
    Value *FoldedToConst =
        getFPClassConstant(VTy, DemandedMask & Known.KnownFPClasses);
    return FoldedToConst == V ? nullptr : FoldedToConst;
  }

  if (!I->hasOneUse())
    return nullptr;

  // TODO: Should account for nofpclass/FastMathFlags on current instruction
  switch (I->getOpcode()) {
  case Instruction::FNeg: {
    if (SimplifyDemandedFPClass(I, 0, llvm::fneg(DemandedMask), Known,
                                Depth + 1))
      return I;
    Known.fneg();
    break;
  }
  case Instruction::Call: {
    CallInst *CI = cast<CallInst>(I);
    switch (CI->getIntrinsicID()) {
    case Intrinsic::fabs:
      if (SimplifyDemandedFPClass(I, 0, llvm::inverse_fabs(DemandedMask), Known,
                                  Depth + 1))
        return I;
      Known.fabs();
      break;
    case Intrinsic::arithmetic_fence:
      if (SimplifyDemandedFPClass(I, 0, DemandedMask, Known, Depth + 1))
        return I;
      break;
    case Intrinsic::copysign: {
      // Flip on more potentially demanded classes
      const FPClassTest DemandedMaskAnySign = llvm::unknown_sign(DemandedMask);
      if (SimplifyDemandedFPClass(I, 0, DemandedMaskAnySign, Known, Depth + 1))
        return I;

      if ((DemandedMask & fcPositive) == fcNone) {
        // Roundabout way of replacing with fneg(fabs)
        I->setOperand(1, ConstantFP::get(VTy, -1.0));
        return I;
      }

      if ((DemandedMask & fcNegative) == fcNone) {
        // Roundabout way of replacing with fabs
        I->setOperand(1, ConstantFP::getZero(VTy));
        return I;
      }

      KnownFPClass KnownSign =
          computeKnownFPClass(I->getOperand(1), fcAllFlags, CxtI, Depth + 1);
      Known.copysign(KnownSign);
      break;
    }
    default:
      Known = computeKnownFPClass(I, ~DemandedMask, CxtI, Depth + 1);
      break;
    }

    break;
  }
  case Instruction::Select: {
    KnownFPClass KnownLHS, KnownRHS;
    if (SimplifyDemandedFPClass(I, 2, DemandedMask, KnownRHS, Depth + 1) ||
        SimplifyDemandedFPClass(I, 1, DemandedMask, KnownLHS, Depth + 1))
      return I;

    if (KnownLHS.isKnownNever(DemandedMask))
      return I->getOperand(2);
    if (KnownRHS.isKnownNever(DemandedMask))
      return I->getOperand(1);

    // TODO: Recognize clamping patterns
    Known = KnownLHS | KnownRHS;
    break;
  }
  default:
    Known = computeKnownFPClass(I, ~DemandedMask, CxtI, Depth + 1);
    break;
  }

  return getFPClassConstant(VTy, DemandedMask & Known.KnownFPClasses);
}

bool InstCombinerImpl::SimplifyDemandedFPClass(Instruction *I, unsigned OpNo,
                                               FPClassTest DemandedMask,
                                               KnownFPClass &Known,
                                               unsigned Depth) {
  Use &U = I->getOperandUse(OpNo);
  Value *NewVal =
      SimplifyDemandedUseFPClass(U.get(), DemandedMask, Known, Depth, I);
  if (!NewVal)
    return false;
  if (Instruction *OpInst = dyn_cast<Instruction>(U))
    salvageDebugInfo(*OpInst);

  replaceUse(U, NewVal);
  return true;
}
