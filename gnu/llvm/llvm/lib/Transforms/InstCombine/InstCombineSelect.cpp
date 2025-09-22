//===- InstCombineSelect.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitSelect function.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CmpInstAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/OverflowInstAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include <cassert>
#include <utility>

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

using namespace llvm;
using namespace PatternMatch;


/// Replace a select operand based on an equality comparison with the identity
/// constant of a binop.
static Instruction *foldSelectBinOpIdentity(SelectInst &Sel,
                                            const TargetLibraryInfo &TLI,
                                            InstCombinerImpl &IC) {
  // The select condition must be an equality compare with a constant operand.
  Value *X;
  Constant *C;
  CmpInst::Predicate Pred;
  if (!match(Sel.getCondition(), m_Cmp(Pred, m_Value(X), m_Constant(C))))
    return nullptr;

  bool IsEq;
  if (ICmpInst::isEquality(Pred))
    IsEq = Pred == ICmpInst::ICMP_EQ;
  else if (Pred == FCmpInst::FCMP_OEQ)
    IsEq = true;
  else if (Pred == FCmpInst::FCMP_UNE)
    IsEq = false;
  else
    return nullptr;

  // A select operand must be a binop.
  BinaryOperator *BO;
  if (!match(Sel.getOperand(IsEq ? 1 : 2), m_BinOp(BO)))
    return nullptr;

  // The compare constant must be the identity constant for that binop.
  // If this a floating-point compare with 0.0, any zero constant will do.
  Type *Ty = BO->getType();
  Constant *IdC = ConstantExpr::getBinOpIdentity(BO->getOpcode(), Ty, true);
  if (IdC != C) {
    if (!IdC || !CmpInst::isFPPredicate(Pred))
      return nullptr;
    if (!match(IdC, m_AnyZeroFP()) || !match(C, m_AnyZeroFP()))
      return nullptr;
  }

  // Last, match the compare variable operand with a binop operand.
  Value *Y;
  if (!BO->isCommutative() && !match(BO, m_BinOp(m_Value(Y), m_Specific(X))))
    return nullptr;
  if (!match(BO, m_c_BinOp(m_Value(Y), m_Specific(X))))
    return nullptr;

  // +0.0 compares equal to -0.0, and so it does not behave as required for this
  // transform. Bail out if we can not exclude that possibility.
  if (isa<FPMathOperator>(BO))
    if (!BO->hasNoSignedZeros() &&
        !cannotBeNegativeZero(Y, 0,
                              IC.getSimplifyQuery().getWithInstruction(&Sel)))
      return nullptr;

  // BO = binop Y, X
  // S = { select (cmp eq X, C), BO, ? } or { select (cmp ne X, C), ?, BO }
  // =>
  // S = { select (cmp eq X, C),  Y, ? } or { select (cmp ne X, C), ?,  Y }
  return IC.replaceOperand(Sel, IsEq ? 1 : 2, Y);
}

/// This folds:
///  select (icmp eq (and X, C1)), TC, FC
///    iff C1 is a power 2 and the difference between TC and FC is a power-of-2.
/// To something like:
///  (shr (and (X, C1)), (log2(C1) - log2(TC-FC))) + FC
/// Or:
///  (shl (and (X, C1)), (log2(TC-FC) - log2(C1))) + FC
/// With some variations depending if FC is larger than TC, or the shift
/// isn't needed, or the bit widths don't match.
static Value *foldSelectICmpAnd(SelectInst &Sel, ICmpInst *Cmp,
                                InstCombiner::BuilderTy &Builder) {
  const APInt *SelTC, *SelFC;
  if (!match(Sel.getTrueValue(), m_APInt(SelTC)) ||
      !match(Sel.getFalseValue(), m_APInt(SelFC)))
    return nullptr;

  // If this is a vector select, we need a vector compare.
  Type *SelType = Sel.getType();
  if (SelType->isVectorTy() != Cmp->getType()->isVectorTy())
    return nullptr;

  Value *V;
  APInt AndMask;
  bool CreateAnd = false;
  ICmpInst::Predicate Pred = Cmp->getPredicate();
  if (ICmpInst::isEquality(Pred)) {
    if (!match(Cmp->getOperand(1), m_Zero()))
      return nullptr;

    V = Cmp->getOperand(0);
    const APInt *AndRHS;
    if (!match(V, m_And(m_Value(), m_Power2(AndRHS))))
      return nullptr;

    AndMask = *AndRHS;
  } else if (decomposeBitTestICmp(Cmp->getOperand(0), Cmp->getOperand(1),
                                  Pred, V, AndMask)) {
    assert(ICmpInst::isEquality(Pred) && "Not equality test?");
    if (!AndMask.isPowerOf2())
      return nullptr;

    CreateAnd = true;
  } else {
    return nullptr;
  }

  // In general, when both constants are non-zero, we would need an offset to
  // replace the select. This would require more instructions than we started
  // with. But there's one special-case that we handle here because it can
  // simplify/reduce the instructions.
  APInt TC = *SelTC;
  APInt FC = *SelFC;
  if (!TC.isZero() && !FC.isZero()) {
    // If the select constants differ by exactly one bit and that's the same
    // bit that is masked and checked by the select condition, the select can
    // be replaced by bitwise logic to set/clear one bit of the constant result.
    if (TC.getBitWidth() != AndMask.getBitWidth() || (TC ^ FC) != AndMask)
      return nullptr;
    if (CreateAnd) {
      // If we have to create an 'and', then we must kill the cmp to not
      // increase the instruction count.
      if (!Cmp->hasOneUse())
        return nullptr;
      V = Builder.CreateAnd(V, ConstantInt::get(SelType, AndMask));
    }
    bool ExtraBitInTC = TC.ugt(FC);
    if (Pred == ICmpInst::ICMP_EQ) {
      // If the masked bit in V is clear, clear or set the bit in the result:
      // (V & AndMaskC) == 0 ? TC : FC --> (V & AndMaskC) ^ TC
      // (V & AndMaskC) == 0 ? TC : FC --> (V & AndMaskC) | TC
      Constant *C = ConstantInt::get(SelType, TC);
      return ExtraBitInTC ? Builder.CreateXor(V, C) : Builder.CreateOr(V, C);
    }
    if (Pred == ICmpInst::ICMP_NE) {
      // If the masked bit in V is set, set or clear the bit in the result:
      // (V & AndMaskC) != 0 ? TC : FC --> (V & AndMaskC) | FC
      // (V & AndMaskC) != 0 ? TC : FC --> (V & AndMaskC) ^ FC
      Constant *C = ConstantInt::get(SelType, FC);
      return ExtraBitInTC ? Builder.CreateOr(V, C) : Builder.CreateXor(V, C);
    }
    llvm_unreachable("Only expecting equality predicates");
  }

  // Make sure one of the select arms is a power-of-2.
  if (!TC.isPowerOf2() && !FC.isPowerOf2())
    return nullptr;

  // Determine which shift is needed to transform result of the 'and' into the
  // desired result.
  const APInt &ValC = !TC.isZero() ? TC : FC;
  unsigned ValZeros = ValC.logBase2();
  unsigned AndZeros = AndMask.logBase2();
  bool ShouldNotVal = !TC.isZero();
  ShouldNotVal ^= Pred == ICmpInst::ICMP_NE;

  // If we would need to create an 'and' + 'shift' + 'xor' to replace a 'select'
  // + 'icmp', then this transformation would result in more instructions and
  // potentially interfere with other folding.
  if (CreateAnd && ShouldNotVal && ValZeros != AndZeros)
    return nullptr;

  // Insert the 'and' instruction on the input to the truncate.
  if (CreateAnd)
    V = Builder.CreateAnd(V, ConstantInt::get(V->getType(), AndMask));

  // If types don't match, we can still convert the select by introducing a zext
  // or a trunc of the 'and'.
  if (ValZeros > AndZeros) {
    V = Builder.CreateZExtOrTrunc(V, SelType);
    V = Builder.CreateShl(V, ValZeros - AndZeros);
  } else if (ValZeros < AndZeros) {
    V = Builder.CreateLShr(V, AndZeros - ValZeros);
    V = Builder.CreateZExtOrTrunc(V, SelType);
  } else {
    V = Builder.CreateZExtOrTrunc(V, SelType);
  }

  // Okay, now we know that everything is set up, we just don't know whether we
  // have a icmp_ne or icmp_eq and whether the true or false val is the zero.
  if (ShouldNotVal)
    V = Builder.CreateXor(V, ValC);

  return V;
}

/// We want to turn code that looks like this:
///   %C = or %A, %B
///   %D = select %cond, %C, %A
/// into:
///   %C = select %cond, %B, 0
///   %D = or %A, %C
///
/// Assuming that the specified instruction is an operand to the select, return
/// a bitmask indicating which operands of this instruction are foldable if they
/// equal the other incoming value of the select.
static unsigned getSelectFoldableOperands(BinaryOperator *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return 3;              // Can fold through either operand.
  case Instruction::Sub:   // Can only fold on the amount subtracted.
  case Instruction::FSub:
  case Instruction::FDiv:  // Can only fold on the divisor amount.
  case Instruction::Shl:   // Can only fold on the shift amount.
  case Instruction::LShr:
  case Instruction::AShr:
    return 1;
  default:
    return 0;              // Cannot fold
  }
}

/// We have (select c, TI, FI), and we know that TI and FI have the same opcode.
Instruction *InstCombinerImpl::foldSelectOpOp(SelectInst &SI, Instruction *TI,
                                              Instruction *FI) {
  // Don't break up min/max patterns. The hasOneUse checks below prevent that
  // for most cases, but vector min/max with bitcasts can be transformed. If the
  // one-use restrictions are eased for other patterns, we still don't want to
  // obfuscate min/max.
  if ((match(&SI, m_SMin(m_Value(), m_Value())) ||
       match(&SI, m_SMax(m_Value(), m_Value())) ||
       match(&SI, m_UMin(m_Value(), m_Value())) ||
       match(&SI, m_UMax(m_Value(), m_Value()))))
    return nullptr;

  // If this is a cast from the same type, merge.
  Value *Cond = SI.getCondition();
  Type *CondTy = Cond->getType();
  if (TI->getNumOperands() == 1 && TI->isCast()) {
    Type *FIOpndTy = FI->getOperand(0)->getType();
    if (TI->getOperand(0)->getType() != FIOpndTy)
      return nullptr;

    // The select condition may be a vector. We may only change the operand
    // type if the vector width remains the same (and matches the condition).
    if (auto *CondVTy = dyn_cast<VectorType>(CondTy)) {
      if (!FIOpndTy->isVectorTy() ||
          CondVTy->getElementCount() !=
              cast<VectorType>(FIOpndTy)->getElementCount())
        return nullptr;

      // TODO: If the backend knew how to deal with casts better, we could
      // remove this limitation. For now, there's too much potential to create
      // worse codegen by promoting the select ahead of size-altering casts
      // (PR28160).
      //
      // Note that ValueTracking's matchSelectPattern() looks through casts
      // without checking 'hasOneUse' when it matches min/max patterns, so this
      // transform may end up happening anyway.
      if (TI->getOpcode() != Instruction::BitCast &&
          (!TI->hasOneUse() || !FI->hasOneUse()))
        return nullptr;
    } else if (!TI->hasOneUse() || !FI->hasOneUse()) {
      // TODO: The one-use restrictions for a scalar select could be eased if
      // the fold of a select in visitLoadInst() was enhanced to match a pattern
      // that includes a cast.
      return nullptr;
    }

    // Fold this by inserting a select from the input values.
    Value *NewSI =
        Builder.CreateSelect(Cond, TI->getOperand(0), FI->getOperand(0),
                             SI.getName() + ".v", &SI);
    return CastInst::Create(Instruction::CastOps(TI->getOpcode()), NewSI,
                            TI->getType());
  }

  Value *OtherOpT, *OtherOpF;
  bool MatchIsOpZero;
  auto getCommonOp = [&](Instruction *TI, Instruction *FI, bool Commute,
                         bool Swapped = false) -> Value * {
    assert(!(Commute && Swapped) &&
           "Commute and Swapped can't set at the same time");
    if (!Swapped) {
      if (TI->getOperand(0) == FI->getOperand(0)) {
        OtherOpT = TI->getOperand(1);
        OtherOpF = FI->getOperand(1);
        MatchIsOpZero = true;
        return TI->getOperand(0);
      } else if (TI->getOperand(1) == FI->getOperand(1)) {
        OtherOpT = TI->getOperand(0);
        OtherOpF = FI->getOperand(0);
        MatchIsOpZero = false;
        return TI->getOperand(1);
      }
    }

    if (!Commute && !Swapped)
      return nullptr;

    // If we are allowing commute or swap of operands, then
    // allow a cross-operand match. In that case, MatchIsOpZero
    // means that TI's operand 0 (FI's operand 1) is the common op.
    if (TI->getOperand(0) == FI->getOperand(1)) {
      OtherOpT = TI->getOperand(1);
      OtherOpF = FI->getOperand(0);
      MatchIsOpZero = true;
      return TI->getOperand(0);
    } else if (TI->getOperand(1) == FI->getOperand(0)) {
      OtherOpT = TI->getOperand(0);
      OtherOpF = FI->getOperand(1);
      MatchIsOpZero = false;
      return TI->getOperand(1);
    }
    return nullptr;
  };

  if (TI->hasOneUse() || FI->hasOneUse()) {
    // Cond ? -X : -Y --> -(Cond ? X : Y)
    Value *X, *Y;
    if (match(TI, m_FNeg(m_Value(X))) && match(FI, m_FNeg(m_Value(Y)))) {
      // Intersect FMF from the fneg instructions and union those with the
      // select.
      FastMathFlags FMF = TI->getFastMathFlags();
      FMF &= FI->getFastMathFlags();
      FMF |= SI.getFastMathFlags();
      Value *NewSel =
          Builder.CreateSelect(Cond, X, Y, SI.getName() + ".v", &SI);
      if (auto *NewSelI = dyn_cast<Instruction>(NewSel))
        NewSelI->setFastMathFlags(FMF);
      Instruction *NewFNeg = UnaryOperator::CreateFNeg(NewSel);
      NewFNeg->setFastMathFlags(FMF);
      return NewFNeg;
    }

    // Min/max intrinsic with a common operand can have the common operand
    // pulled after the select. This is the same transform as below for binops,
    // but specialized for intrinsic matching and without the restrictive uses
    // clause.
    auto *TII = dyn_cast<IntrinsicInst>(TI);
    auto *FII = dyn_cast<IntrinsicInst>(FI);
    if (TII && FII && TII->getIntrinsicID() == FII->getIntrinsicID()) {
      if (match(TII, m_MaxOrMin(m_Value(), m_Value()))) {
        if (Value *MatchOp = getCommonOp(TI, FI, true)) {
          Value *NewSel =
              Builder.CreateSelect(Cond, OtherOpT, OtherOpF, "minmaxop", &SI);
          return CallInst::Create(TII->getCalledFunction(), {NewSel, MatchOp});
        }
      }

      // select c, (ldexp v, e0), (ldexp v, e1) -> ldexp v, (select c, e0, e1)
      // select c, (ldexp v0, e), (ldexp v1, e) -> ldexp (select c, v0, v1), e
      //
      // select c, (ldexp v0, e0), (ldexp v1, e1) ->
      //     ldexp (select c, v0, v1), (select c, e0, e1)
      if (TII->getIntrinsicID() == Intrinsic::ldexp) {
        Value *LdexpVal0 = TII->getArgOperand(0);
        Value *LdexpExp0 = TII->getArgOperand(1);
        Value *LdexpVal1 = FII->getArgOperand(0);
        Value *LdexpExp1 = FII->getArgOperand(1);
        if (LdexpExp0->getType() == LdexpExp1->getType()) {
          FPMathOperator *SelectFPOp = cast<FPMathOperator>(&SI);
          FastMathFlags FMF = cast<FPMathOperator>(TII)->getFastMathFlags();
          FMF &= cast<FPMathOperator>(FII)->getFastMathFlags();
          FMF |= SelectFPOp->getFastMathFlags();

          Value *SelectVal = Builder.CreateSelect(Cond, LdexpVal0, LdexpVal1);
          Value *SelectExp = Builder.CreateSelect(Cond, LdexpExp0, LdexpExp1);

          CallInst *NewLdexp = Builder.CreateIntrinsic(
              TII->getType(), Intrinsic::ldexp, {SelectVal, SelectExp});
          NewLdexp->setFastMathFlags(FMF);
          return replaceInstUsesWith(SI, NewLdexp);
        }
      }
    }

    // icmp with a common operand also can have the common operand
    // pulled after the select.
    ICmpInst::Predicate TPred, FPred;
    if (match(TI, m_ICmp(TPred, m_Value(), m_Value())) &&
        match(FI, m_ICmp(FPred, m_Value(), m_Value()))) {
      if (TPred == FPred || TPred == CmpInst::getSwappedPredicate(FPred)) {
        bool Swapped = TPred != FPred;
        if (Value *MatchOp =
                getCommonOp(TI, FI, ICmpInst::isEquality(TPred), Swapped)) {
          Value *NewSel = Builder.CreateSelect(Cond, OtherOpT, OtherOpF,
                                               SI.getName() + ".v", &SI);
          return new ICmpInst(
              MatchIsOpZero ? TPred : CmpInst::getSwappedPredicate(TPred),
              MatchOp, NewSel);
        }
      }
    }
  }

  // Only handle binary operators (including two-operand getelementptr) with
  // one-use here. As with the cast case above, it may be possible to relax the
  // one-use constraint, but that needs be examined carefully since it may not
  // reduce the total number of instructions.
  if (TI->getNumOperands() != 2 || FI->getNumOperands() != 2 ||
      !TI->isSameOperationAs(FI) ||
      (!isa<BinaryOperator>(TI) && !isa<GetElementPtrInst>(TI)) ||
      !TI->hasOneUse() || !FI->hasOneUse())
    return nullptr;

  // Figure out if the operations have any operands in common.
  Value *MatchOp = getCommonOp(TI, FI, TI->isCommutative());
  if (!MatchOp)
    return nullptr;

  // If the select condition is a vector, the operands of the original select's
  // operands also must be vectors. This may not be the case for getelementptr
  // for example.
  if (CondTy->isVectorTy() && (!OtherOpT->getType()->isVectorTy() ||
                               !OtherOpF->getType()->isVectorTy()))
    return nullptr;

  // If we are sinking div/rem after a select, we may need to freeze the
  // condition because div/rem may induce immediate UB with a poison operand.
  // For example, the following transform is not safe if Cond can ever be poison
  // because we can replace poison with zero and then we have div-by-zero that
  // didn't exist in the original code:
  // Cond ? x/y : x/z --> x / (Cond ? y : z)
  auto *BO = dyn_cast<BinaryOperator>(TI);
  if (BO && BO->isIntDivRem() && !isGuaranteedNotToBePoison(Cond)) {
    // A udiv/urem with a common divisor is safe because UB can only occur with
    // div-by-zero, and that would be present in the original code.
    if (BO->getOpcode() == Instruction::SDiv ||
        BO->getOpcode() == Instruction::SRem || MatchIsOpZero)
      Cond = Builder.CreateFreeze(Cond);
  }

  // If we reach here, they do have operations in common.
  Value *NewSI = Builder.CreateSelect(Cond, OtherOpT, OtherOpF,
                                      SI.getName() + ".v", &SI);
  Value *Op0 = MatchIsOpZero ? MatchOp : NewSI;
  Value *Op1 = MatchIsOpZero ? NewSI : MatchOp;
  if (auto *BO = dyn_cast<BinaryOperator>(TI)) {
    BinaryOperator *NewBO = BinaryOperator::Create(BO->getOpcode(), Op0, Op1);
    NewBO->copyIRFlags(TI);
    NewBO->andIRFlags(FI);
    return NewBO;
  }
  if (auto *TGEP = dyn_cast<GetElementPtrInst>(TI)) {
    auto *FGEP = cast<GetElementPtrInst>(FI);
    Type *ElementType = TGEP->getSourceElementType();
    return GetElementPtrInst::Create(
        ElementType, Op0, Op1, TGEP->getNoWrapFlags() & FGEP->getNoWrapFlags());
  }
  llvm_unreachable("Expected BinaryOperator or GEP");
  return nullptr;
}

static bool isSelect01(const APInt &C1I, const APInt &C2I) {
  if (!C1I.isZero() && !C2I.isZero()) // One side must be zero.
    return false;
  return C1I.isOne() || C1I.isAllOnes() || C2I.isOne() || C2I.isAllOnes();
}

/// Try to fold the select into one of the operands to allow further
/// optimization.
Instruction *InstCombinerImpl::foldSelectIntoOp(SelectInst &SI, Value *TrueVal,
                                                Value *FalseVal) {
  // See the comment above getSelectFoldableOperands for a description of the
  // transformation we are doing here.
  auto TryFoldSelectIntoOp = [&](SelectInst &SI, Value *TrueVal,
                                 Value *FalseVal,
                                 bool Swapped) -> Instruction * {
    auto *TVI = dyn_cast<BinaryOperator>(TrueVal);
    if (!TVI || !TVI->hasOneUse() || isa<Constant>(FalseVal))
      return nullptr;

    unsigned SFO = getSelectFoldableOperands(TVI);
    unsigned OpToFold = 0;
    if ((SFO & 1) && FalseVal == TVI->getOperand(0))
      OpToFold = 1;
    else if ((SFO & 2) && FalseVal == TVI->getOperand(1))
      OpToFold = 2;

    if (!OpToFold)
      return nullptr;

    // TODO: We probably ought to revisit cases where the select and FP
    // instructions have different flags and add tests to ensure the
    // behaviour is correct.
    FastMathFlags FMF;
    if (isa<FPMathOperator>(&SI))
      FMF = SI.getFastMathFlags();
    Constant *C = ConstantExpr::getBinOpIdentity(
        TVI->getOpcode(), TVI->getType(), true, FMF.noSignedZeros());
    Value *OOp = TVI->getOperand(2 - OpToFold);
    // Avoid creating select between 2 constants unless it's selecting
    // between 0, 1 and -1.
    const APInt *OOpC;
    bool OOpIsAPInt = match(OOp, m_APInt(OOpC));
    if (isa<Constant>(OOp) &&
        (!OOpIsAPInt || !isSelect01(C->getUniqueInteger(), *OOpC)))
      return nullptr;

    // If the false value is a NaN then we have that the floating point math
    // operation in the transformed code may not preserve the exact NaN
    // bit-pattern -- e.g. `fadd sNaN, 0.0 -> qNaN`.
    // This makes the transformation incorrect since the original program would
    // have preserved the exact NaN bit-pattern.
    // Avoid the folding if the false value might be a NaN.
    if (isa<FPMathOperator>(&SI) &&
        !computeKnownFPClass(FalseVal, FMF, fcNan, &SI).isKnownNeverNaN())
      return nullptr;

    Value *NewSel = Builder.CreateSelect(SI.getCondition(), Swapped ? C : OOp,
                                         Swapped ? OOp : C, "", &SI);
    if (isa<FPMathOperator>(&SI))
      cast<Instruction>(NewSel)->setFastMathFlags(FMF);
    NewSel->takeName(TVI);
    BinaryOperator *BO =
        BinaryOperator::Create(TVI->getOpcode(), FalseVal, NewSel);
    BO->copyIRFlags(TVI);
    return BO;
  };

  if (Instruction *R = TryFoldSelectIntoOp(SI, TrueVal, FalseVal, false))
    return R;

  if (Instruction *R = TryFoldSelectIntoOp(SI, FalseVal, TrueVal, true))
    return R;

  return nullptr;
}

/// We want to turn:
///   (select (icmp eq (and X, Y), 0), (and (lshr X, Z), 1), 1)
/// into:
///   zext (icmp ne i32 (and X, (or Y, (shl 1, Z))), 0)
/// Note:
///   Z may be 0 if lshr is missing.
/// Worst-case scenario is that we will replace 5 instructions with 5 different
/// instructions, but we got rid of select.
static Instruction *foldSelectICmpAndAnd(Type *SelType, const ICmpInst *Cmp,
                                         Value *TVal, Value *FVal,
                                         InstCombiner::BuilderTy &Builder) {
  if (!(Cmp->hasOneUse() && Cmp->getOperand(0)->hasOneUse() &&
        Cmp->getPredicate() == ICmpInst::ICMP_EQ &&
        match(Cmp->getOperand(1), m_Zero()) && match(FVal, m_One())))
    return nullptr;

  // The TrueVal has general form of:  and %B, 1
  Value *B;
  if (!match(TVal, m_OneUse(m_And(m_Value(B), m_One()))))
    return nullptr;

  // Where %B may be optionally shifted:  lshr %X, %Z.
  Value *X, *Z;
  const bool HasShift = match(B, m_OneUse(m_LShr(m_Value(X), m_Value(Z))));

  // The shift must be valid.
  // TODO: This restricts the fold to constant shift amounts. Is there a way to
  //       handle variable shifts safely? PR47012
  if (HasShift &&
      !match(Z, m_SpecificInt_ICMP(CmpInst::ICMP_ULT,
                                   APInt(SelType->getScalarSizeInBits(),
                                         SelType->getScalarSizeInBits()))))
    return nullptr;

  if (!HasShift)
    X = B;

  Value *Y;
  if (!match(Cmp->getOperand(0), m_c_And(m_Specific(X), m_Value(Y))))
    return nullptr;

  // ((X & Y) == 0) ? ((X >> Z) & 1) : 1 --> (X & (Y | (1 << Z))) != 0
  // ((X & Y) == 0) ? (X & 1) : 1 --> (X & (Y | 1)) != 0
  Constant *One = ConstantInt::get(SelType, 1);
  Value *MaskB = HasShift ? Builder.CreateShl(One, Z) : One;
  Value *FullMask = Builder.CreateOr(Y, MaskB);
  Value *MaskedX = Builder.CreateAnd(X, FullMask);
  Value *ICmpNeZero = Builder.CreateIsNotNull(MaskedX);
  return new ZExtInst(ICmpNeZero, SelType);
}

/// We want to turn:
///   (select (icmp eq (and X, C1), 0), 0, (shl [nsw/nuw] X, C2));
///   iff C1 is a mask and the number of its leading zeros is equal to C2
/// into:
///   shl X, C2
static Value *foldSelectICmpAndZeroShl(const ICmpInst *Cmp, Value *TVal,
                                       Value *FVal,
                                       InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate Pred;
  Value *AndVal;
  if (!match(Cmp, m_ICmp(Pred, m_Value(AndVal), m_Zero())))
    return nullptr;

  if (Pred == ICmpInst::ICMP_NE) {
    Pred = ICmpInst::ICMP_EQ;
    std::swap(TVal, FVal);
  }

  Value *X;
  const APInt *C2, *C1;
  if (Pred != ICmpInst::ICMP_EQ ||
      !match(AndVal, m_And(m_Value(X), m_APInt(C1))) ||
      !match(TVal, m_Zero()) || !match(FVal, m_Shl(m_Specific(X), m_APInt(C2))))
    return nullptr;

  if (!C1->isMask() ||
      C1->countLeadingZeros() != static_cast<unsigned>(C2->getZExtValue()))
    return nullptr;

  auto *FI = dyn_cast<Instruction>(FVal);
  if (!FI)
    return nullptr;

  FI->setHasNoSignedWrap(false);
  FI->setHasNoUnsignedWrap(false);
  return FVal;
}

/// We want to turn:
///   (select (icmp sgt x, C), lshr (X, Y), ashr (X, Y)); iff C s>= -1
///   (select (icmp slt x, C), ashr (X, Y), lshr (X, Y)); iff C s>= 0
/// into:
///   ashr (X, Y)
static Value *foldSelectICmpLshrAshr(const ICmpInst *IC, Value *TrueVal,
                                     Value *FalseVal,
                                     InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate Pred = IC->getPredicate();
  Value *CmpLHS = IC->getOperand(0);
  Value *CmpRHS = IC->getOperand(1);
  if (!CmpRHS->getType()->isIntOrIntVectorTy())
    return nullptr;

  Value *X, *Y;
  unsigned Bitwidth = CmpRHS->getType()->getScalarSizeInBits();
  if ((Pred != ICmpInst::ICMP_SGT ||
       !match(CmpRHS,
              m_SpecificInt_ICMP(ICmpInst::ICMP_SGE, APInt(Bitwidth, -1)))) &&
      (Pred != ICmpInst::ICMP_SLT ||
       !match(CmpRHS,
              m_SpecificInt_ICMP(ICmpInst::ICMP_SGE, APInt(Bitwidth, 0)))))
    return nullptr;

  // Canonicalize so that ashr is in FalseVal.
  if (Pred == ICmpInst::ICMP_SLT)
    std::swap(TrueVal, FalseVal);

  if (match(TrueVal, m_LShr(m_Value(X), m_Value(Y))) &&
      match(FalseVal, m_AShr(m_Specific(X), m_Specific(Y))) &&
      match(CmpLHS, m_Specific(X))) {
    const auto *Ashr = cast<Instruction>(FalseVal);
    // if lshr is not exact and ashr is, this new ashr must not be exact.
    bool IsExact = Ashr->isExact() && cast<Instruction>(TrueVal)->isExact();
    return Builder.CreateAShr(X, Y, IC->getName(), IsExact);
  }

  return nullptr;
}

/// We want to turn:
///   (select (icmp eq (and X, C1), 0), Y, (BinOp Y, C2))
/// into:
///   IF C2 u>= C1
///     (BinOp Y, (shl (and X, C1), C3))
///   ELSE
///     (BinOp Y, (lshr (and X, C1), C3))
/// iff:
///   0 on the RHS is the identity value (i.e add, xor, shl, etc...)
///   C1 and C2 are both powers of 2
/// where:
///   IF C2 u>= C1
///     C3 = Log(C2) - Log(C1)
///   ELSE
///     C3 = Log(C1) - Log(C2)
///
/// This transform handles cases where:
/// 1. The icmp predicate is inverted
/// 2. The select operands are reversed
/// 3. The magnitude of C2 and C1 are flipped
static Value *foldSelectICmpAndBinOp(const ICmpInst *IC, Value *TrueVal,
                                  Value *FalseVal,
                                  InstCombiner::BuilderTy &Builder) {
  // Only handle integer compares. Also, if this is a vector select, we need a
  // vector compare.
  if (!TrueVal->getType()->isIntOrIntVectorTy() ||
     TrueVal->getType()->isVectorTy() != IC->getType()->isVectorTy())
    return nullptr;

  Value *CmpLHS = IC->getOperand(0);
  Value *CmpRHS = IC->getOperand(1);

  unsigned C1Log;
  bool NeedAnd = false;
  CmpInst::Predicate Pred = IC->getPredicate();
  if (IC->isEquality()) {
    if (!match(CmpRHS, m_Zero()))
      return nullptr;

    const APInt *C1;
    if (!match(CmpLHS, m_And(m_Value(), m_Power2(C1))))
      return nullptr;

    C1Log = C1->logBase2();
  } else {
    APInt C1;
    if (!decomposeBitTestICmp(CmpLHS, CmpRHS, Pred, CmpLHS, C1) ||
        !C1.isPowerOf2())
      return nullptr;

    C1Log = C1.logBase2();
    NeedAnd = true;
  }

  Value *Y, *V = CmpLHS;
  BinaryOperator *BinOp;
  const APInt *C2;
  bool NeedXor;
  if (match(FalseVal, m_BinOp(m_Specific(TrueVal), m_Power2(C2)))) {
    Y = TrueVal;
    BinOp = cast<BinaryOperator>(FalseVal);
    NeedXor = Pred == ICmpInst::ICMP_NE;
  } else if (match(TrueVal, m_BinOp(m_Specific(FalseVal), m_Power2(C2)))) {
    Y = FalseVal;
    BinOp = cast<BinaryOperator>(TrueVal);
    NeedXor = Pred == ICmpInst::ICMP_EQ;
  } else {
    return nullptr;
  }

  // Check that 0 on RHS is identity value for this binop.
  auto *IdentityC =
      ConstantExpr::getBinOpIdentity(BinOp->getOpcode(), BinOp->getType(),
                                     /*AllowRHSConstant*/ true);
  if (IdentityC == nullptr || !IdentityC->isNullValue())
    return nullptr;

  unsigned C2Log = C2->logBase2();

  bool NeedShift = C1Log != C2Log;
  bool NeedZExtTrunc = Y->getType()->getScalarSizeInBits() !=
                       V->getType()->getScalarSizeInBits();

  // Make sure we don't create more instructions than we save.
  if ((NeedShift + NeedXor + NeedZExtTrunc + NeedAnd) >
      (IC->hasOneUse() + BinOp->hasOneUse()))
    return nullptr;

  if (NeedAnd) {
    // Insert the AND instruction on the input to the truncate.
    APInt C1 = APInt::getOneBitSet(V->getType()->getScalarSizeInBits(), C1Log);
    V = Builder.CreateAnd(V, ConstantInt::get(V->getType(), C1));
  }

  if (C2Log > C1Log) {
    V = Builder.CreateZExtOrTrunc(V, Y->getType());
    V = Builder.CreateShl(V, C2Log - C1Log);
  } else if (C1Log > C2Log) {
    V = Builder.CreateLShr(V, C1Log - C2Log);
    V = Builder.CreateZExtOrTrunc(V, Y->getType());
  } else
    V = Builder.CreateZExtOrTrunc(V, Y->getType());

  if (NeedXor)
    V = Builder.CreateXor(V, *C2);

  return Builder.CreateBinOp(BinOp->getOpcode(), Y, V);
}

/// Canonicalize a set or clear of a masked set of constant bits to
/// select-of-constants form.
static Instruction *foldSetClearBits(SelectInst &Sel,
                                     InstCombiner::BuilderTy &Builder) {
  Value *Cond = Sel.getCondition();
  Value *T = Sel.getTrueValue();
  Value *F = Sel.getFalseValue();
  Type *Ty = Sel.getType();
  Value *X;
  const APInt *NotC, *C;

  // Cond ? (X & ~C) : (X | C) --> (X & ~C) | (Cond ? 0 : C)
  if (match(T, m_And(m_Value(X), m_APInt(NotC))) &&
      match(F, m_OneUse(m_Or(m_Specific(X), m_APInt(C)))) && *NotC == ~(*C)) {
    Constant *Zero = ConstantInt::getNullValue(Ty);
    Constant *OrC = ConstantInt::get(Ty, *C);
    Value *NewSel = Builder.CreateSelect(Cond, Zero, OrC, "masksel", &Sel);
    return BinaryOperator::CreateOr(T, NewSel);
  }

  // Cond ? (X | C) : (X & ~C) --> (X & ~C) | (Cond ? C : 0)
  if (match(F, m_And(m_Value(X), m_APInt(NotC))) &&
      match(T, m_OneUse(m_Or(m_Specific(X), m_APInt(C)))) && *NotC == ~(*C)) {
    Constant *Zero = ConstantInt::getNullValue(Ty);
    Constant *OrC = ConstantInt::get(Ty, *C);
    Value *NewSel = Builder.CreateSelect(Cond, OrC, Zero, "masksel", &Sel);
    return BinaryOperator::CreateOr(F, NewSel);
  }

  return nullptr;
}

//   select (x == 0), 0, x * y --> freeze(y) * x
//   select (y == 0), 0, x * y --> freeze(x) * y
//   select (x == 0), undef, x * y --> freeze(y) * x
//   select (x == undef), 0, x * y --> freeze(y) * x
// Usage of mul instead of 0 will make the result more poisonous,
// so the operand that was not checked in the condition should be frozen.
// The latter folding is applied only when a constant compared with x is
// is a vector consisting of 0 and undefs. If a constant compared with x
// is a scalar undefined value or undefined vector then an expression
// should be already folded into a constant.
static Instruction *foldSelectZeroOrMul(SelectInst &SI, InstCombinerImpl &IC) {
  auto *CondVal = SI.getCondition();
  auto *TrueVal = SI.getTrueValue();
  auto *FalseVal = SI.getFalseValue();
  Value *X, *Y;
  ICmpInst::Predicate Predicate;

  // Assuming that constant compared with zero is not undef (but it may be
  // a vector with some undef elements). Otherwise (when a constant is undef)
  // the select expression should be already simplified.
  if (!match(CondVal, m_ICmp(Predicate, m_Value(X), m_Zero())) ||
      !ICmpInst::isEquality(Predicate))
    return nullptr;

  if (Predicate == ICmpInst::ICMP_NE)
    std::swap(TrueVal, FalseVal);

  // Check that TrueVal is a constant instead of matching it with m_Zero()
  // to handle the case when it is a scalar undef value or a vector containing
  // non-zero elements that are masked by undef elements in the compare
  // constant.
  auto *TrueValC = dyn_cast<Constant>(TrueVal);
  if (TrueValC == nullptr ||
      !match(FalseVal, m_c_Mul(m_Specific(X), m_Value(Y))) ||
      !isa<Instruction>(FalseVal))
    return nullptr;

  auto *ZeroC = cast<Constant>(cast<Instruction>(CondVal)->getOperand(1));
  auto *MergedC = Constant::mergeUndefsWith(TrueValC, ZeroC);
  // If X is compared with 0 then TrueVal could be either zero or undef.
  // m_Zero match vectors containing some undef elements, but for scalars
  // m_Undef should be used explicitly.
  if (!match(MergedC, m_Zero()) && !match(MergedC, m_Undef()))
    return nullptr;

  auto *FalseValI = cast<Instruction>(FalseVal);
  auto *FrY = IC.InsertNewInstBefore(new FreezeInst(Y, Y->getName() + ".fr"),
                                     FalseValI->getIterator());
  IC.replaceOperand(*FalseValI, FalseValI->getOperand(0) == Y ? 0 : 1, FrY);
  return IC.replaceInstUsesWith(SI, FalseValI);
}

/// Transform patterns such as (a > b) ? a - b : 0 into usub.sat(a, b).
/// There are 8 commuted/swapped variants of this pattern.
/// TODO: Also support a - UMIN(a,b) patterns.
static Value *canonicalizeSaturatedSubtract(const ICmpInst *ICI,
                                            const Value *TrueVal,
                                            const Value *FalseVal,
                                            InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate Pred = ICI->getPredicate();
  Value *A = ICI->getOperand(0);
  Value *B = ICI->getOperand(1);

  // (b > a) ? 0 : a - b -> (b <= a) ? a - b : 0
  // (a == 0) ? 0 : a - 1 -> (a != 0) ? a - 1 : 0
  if (match(TrueVal, m_Zero())) {
    Pred = ICmpInst::getInversePredicate(Pred);
    std::swap(TrueVal, FalseVal);
  }

  if (!match(FalseVal, m_Zero()))
    return nullptr;

  // ugt 0 is canonicalized to ne 0 and requires special handling
  // (a != 0) ? a + -1 : 0 -> usub.sat(a, 1)
  if (Pred == ICmpInst::ICMP_NE) {
    if (match(B, m_Zero()) && match(TrueVal, m_Add(m_Specific(A), m_AllOnes())))
      return Builder.CreateBinaryIntrinsic(Intrinsic::usub_sat, A,
                                           ConstantInt::get(A->getType(), 1));
    return nullptr;
  }

  if (!ICmpInst::isUnsigned(Pred))
    return nullptr;

  if (Pred == ICmpInst::ICMP_ULE || Pred == ICmpInst::ICMP_ULT) {
    // (b < a) ? a - b : 0 -> (a > b) ? a - b : 0
    std::swap(A, B);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  assert((Pred == ICmpInst::ICMP_UGE || Pred == ICmpInst::ICMP_UGT) &&
         "Unexpected isUnsigned predicate!");

  // Ensure the sub is of the form:
  //  (a > b) ? a - b : 0 -> usub.sat(a, b)
  //  (a > b) ? b - a : 0 -> -usub.sat(a, b)
  // Checking for both a-b and a+(-b) as a constant.
  bool IsNegative = false;
  const APInt *C;
  if (match(TrueVal, m_Sub(m_Specific(B), m_Specific(A))) ||
      (match(A, m_APInt(C)) &&
       match(TrueVal, m_Add(m_Specific(B), m_SpecificInt(-*C)))))
    IsNegative = true;
  else if (!match(TrueVal, m_Sub(m_Specific(A), m_Specific(B))) &&
           !(match(B, m_APInt(C)) &&
             match(TrueVal, m_Add(m_Specific(A), m_SpecificInt(-*C)))))
    return nullptr;

  // If we are adding a negate and the sub and icmp are used anywhere else, we
  // would end up with more instructions.
  if (IsNegative && !TrueVal->hasOneUse() && !ICI->hasOneUse())
    return nullptr;

  // (a > b) ? a - b : 0 -> usub.sat(a, b)
  // (a > b) ? b - a : 0 -> -usub.sat(a, b)
  Value *Result = Builder.CreateBinaryIntrinsic(Intrinsic::usub_sat, A, B);
  if (IsNegative)
    Result = Builder.CreateNeg(Result);
  return Result;
}

static Value *canonicalizeSaturatedAdd(ICmpInst *Cmp, Value *TVal, Value *FVal,
                                       InstCombiner::BuilderTy &Builder) {
  if (!Cmp->hasOneUse())
    return nullptr;

  // Match unsigned saturated add with constant.
  Value *Cmp0 = Cmp->getOperand(0);
  Value *Cmp1 = Cmp->getOperand(1);
  ICmpInst::Predicate Pred = Cmp->getPredicate();
  Value *X;
  const APInt *C, *CmpC;
  if (Pred == ICmpInst::ICMP_ULT &&
      match(TVal, m_Add(m_Value(X), m_APInt(C))) && X == Cmp0 &&
      match(FVal, m_AllOnes()) && match(Cmp1, m_APInt(CmpC)) && *CmpC == ~*C) {
    // (X u< ~C) ? (X + C) : -1 --> uadd.sat(X, C)
    return Builder.CreateBinaryIntrinsic(
        Intrinsic::uadd_sat, X, ConstantInt::get(X->getType(), *C));
  }

  // Match unsigned saturated add of 2 variables with an unnecessary 'not'.
  // There are 8 commuted variants.
  // Canonicalize -1 (saturated result) to true value of the select.
  if (match(FVal, m_AllOnes())) {
    std::swap(TVal, FVal);
    Pred = CmpInst::getInversePredicate(Pred);
  }
  if (!match(TVal, m_AllOnes()))
    return nullptr;

  // Canonicalize predicate to less-than or less-or-equal-than.
  if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE) {
    std::swap(Cmp0, Cmp1);
    Pred = CmpInst::getSwappedPredicate(Pred);
  }
  if (Pred != ICmpInst::ICMP_ULT && Pred != ICmpInst::ICMP_ULE)
    return nullptr;

  // Match unsigned saturated add of 2 variables with an unnecessary 'not'.
  // Strictness of the comparison is irrelevant.
  Value *Y;
  if (match(Cmp0, m_Not(m_Value(X))) &&
      match(FVal, m_c_Add(m_Specific(X), m_Value(Y))) && Y == Cmp1) {
    // (~X u< Y) ? -1 : (X + Y) --> uadd.sat(X, Y)
    // (~X u< Y) ? -1 : (Y + X) --> uadd.sat(X, Y)
    return Builder.CreateBinaryIntrinsic(Intrinsic::uadd_sat, X, Y);
  }
  // The 'not' op may be included in the sum but not the compare.
  // Strictness of the comparison is irrelevant.
  X = Cmp0;
  Y = Cmp1;
  if (match(FVal, m_c_Add(m_Not(m_Specific(X)), m_Specific(Y)))) {
    // (X u< Y) ? -1 : (~X + Y) --> uadd.sat(~X, Y)
    // (X u< Y) ? -1 : (Y + ~X) --> uadd.sat(Y, ~X)
    BinaryOperator *BO = cast<BinaryOperator>(FVal);
    return Builder.CreateBinaryIntrinsic(
        Intrinsic::uadd_sat, BO->getOperand(0), BO->getOperand(1));
  }
  // The overflow may be detected via the add wrapping round.
  // This is only valid for strict comparison!
  if (Pred == ICmpInst::ICMP_ULT &&
      match(Cmp0, m_c_Add(m_Specific(Cmp1), m_Value(Y))) &&
      match(FVal, m_c_Add(m_Specific(Cmp1), m_Specific(Y)))) {
    // ((X + Y) u< X) ? -1 : (X + Y) --> uadd.sat(X, Y)
    // ((X + Y) u< Y) ? -1 : (X + Y) --> uadd.sat(X, Y)
    return Builder.CreateBinaryIntrinsic(Intrinsic::uadd_sat, Cmp1, Y);
  }

  return nullptr;
}

/// Try to match patterns with select and subtract as absolute difference.
static Value *foldAbsDiff(ICmpInst *Cmp, Value *TVal, Value *FVal,
                          InstCombiner::BuilderTy &Builder) {
  auto *TI = dyn_cast<Instruction>(TVal);
  auto *FI = dyn_cast<Instruction>(FVal);
  if (!TI || !FI)
    return nullptr;

  // Normalize predicate to gt/lt rather than ge/le.
  ICmpInst::Predicate Pred = Cmp->getStrictPredicate();
  Value *A = Cmp->getOperand(0);
  Value *B = Cmp->getOperand(1);

  // Normalize "A - B" as the true value of the select.
  if (match(FI, m_Sub(m_Specific(A), m_Specific(B)))) {
    std::swap(FI, TI);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  // With any pair of no-wrap subtracts:
  // (A > B) ? (A - B) : (B - A) --> abs(A - B)
  if (Pred == CmpInst::ICMP_SGT &&
      match(TI, m_Sub(m_Specific(A), m_Specific(B))) &&
      match(FI, m_Sub(m_Specific(B), m_Specific(A))) &&
      (TI->hasNoSignedWrap() || TI->hasNoUnsignedWrap()) &&
      (FI->hasNoSignedWrap() || FI->hasNoUnsignedWrap())) {
    // The remaining subtract is not "nuw" any more.
    // If there's one use of the subtract (no other use than the use we are
    // about to replace), then we know that the sub is "nsw" in this context
    // even if it was only "nuw" before. If there's another use, then we can't
    // add "nsw" to the existing instruction because it may not be safe in the
    // other user's context.
    TI->setHasNoUnsignedWrap(false);
    if (!TI->hasNoSignedWrap())
      TI->setHasNoSignedWrap(TI->hasOneUse());
    return Builder.CreateBinaryIntrinsic(Intrinsic::abs, TI, Builder.getTrue());
  }

  return nullptr;
}

/// Fold the following code sequence:
/// \code
///   int a = ctlz(x & -x);
//    x ? 31 - a : a;
//    // or
//    x ? 31 - a : 32;
/// \code
///
/// into:
///   cttz(x)
static Instruction *foldSelectCtlzToCttz(ICmpInst *ICI, Value *TrueVal,
                                         Value *FalseVal,
                                         InstCombiner::BuilderTy &Builder) {
  unsigned BitWidth = TrueVal->getType()->getScalarSizeInBits();
  if (!ICI->isEquality() || !match(ICI->getOperand(1), m_Zero()))
    return nullptr;

  if (ICI->getPredicate() == ICmpInst::ICMP_NE)
    std::swap(TrueVal, FalseVal);

  Value *Ctlz;
  if (!match(FalseVal,
             m_Xor(m_Value(Ctlz), m_SpecificInt(BitWidth - 1))))
    return nullptr;

  if (!match(Ctlz, m_Intrinsic<Intrinsic::ctlz>()))
    return nullptr;

  if (TrueVal != Ctlz && !match(TrueVal, m_SpecificInt(BitWidth)))
    return nullptr;

  Value *X = ICI->getOperand(0);
  auto *II = cast<IntrinsicInst>(Ctlz);
  if (!match(II->getOperand(0), m_c_And(m_Specific(X), m_Neg(m_Specific(X)))))
    return nullptr;

  Function *F = Intrinsic::getDeclaration(II->getModule(), Intrinsic::cttz,
                                          II->getType());
  return CallInst::Create(F, {X, II->getArgOperand(1)});
}

/// Attempt to fold a cttz/ctlz followed by a icmp plus select into a single
/// call to cttz/ctlz with flag 'is_zero_poison' cleared.
///
/// For example, we can fold the following code sequence:
/// \code
///   %0 = tail call i32 @llvm.cttz.i32(i32 %x, i1 true)
///   %1 = icmp ne i32 %x, 0
///   %2 = select i1 %1, i32 %0, i32 32
/// \code
///
/// into:
///   %0 = tail call i32 @llvm.cttz.i32(i32 %x, i1 false)
static Value *foldSelectCttzCtlz(ICmpInst *ICI, Value *TrueVal, Value *FalseVal,
                                 InstCombinerImpl &IC) {
  ICmpInst::Predicate Pred = ICI->getPredicate();
  Value *CmpLHS = ICI->getOperand(0);
  Value *CmpRHS = ICI->getOperand(1);

  // Check if the select condition compares a value for equality.
  if (!ICI->isEquality())
    return nullptr;

  Value *SelectArg = FalseVal;
  Value *ValueOnZero = TrueVal;
  if (Pred == ICmpInst::ICMP_NE)
    std::swap(SelectArg, ValueOnZero);

  // Skip zero extend/truncate.
  Value *Count = nullptr;
  if (!match(SelectArg, m_ZExt(m_Value(Count))) &&
      !match(SelectArg, m_Trunc(m_Value(Count))))
    Count = SelectArg;

  // Check that 'Count' is a call to intrinsic cttz/ctlz. Also check that the
  // input to the cttz/ctlz is used as LHS for the compare instruction.
  Value *X;
  if (!match(Count, m_Intrinsic<Intrinsic::cttz>(m_Value(X))) &&
      !match(Count, m_Intrinsic<Intrinsic::ctlz>(m_Value(X))))
    return nullptr;

  // (X == 0) ? BitWidth : ctz(X)
  // (X == -1) ? BitWidth : ctz(~X)
  if ((X != CmpLHS || !match(CmpRHS, m_Zero())) &&
      (!match(X, m_Not(m_Specific(CmpLHS))) || !match(CmpRHS, m_AllOnes())))
    return nullptr;

  IntrinsicInst *II = cast<IntrinsicInst>(Count);

  // Check if the value propagated on zero is a constant number equal to the
  // sizeof in bits of 'Count'.
  unsigned SizeOfInBits = Count->getType()->getScalarSizeInBits();
  if (match(ValueOnZero, m_SpecificInt(SizeOfInBits))) {
    // Explicitly clear the 'is_zero_poison' flag. It's always valid to go from
    // true to false on this flag, so we can replace it for all users.
    II->setArgOperand(1, ConstantInt::getFalse(II->getContext()));
    // A range annotation on the intrinsic may no longer be valid.
    II->dropPoisonGeneratingAnnotations();
    IC.addToWorklist(II);
    return SelectArg;
  }

  // The ValueOnZero is not the bitwidth. But if the cttz/ctlz (and optional
  // zext/trunc) have one use (ending at the select), the cttz/ctlz result will
  // not be used if the input is zero. Relax to 'zero is poison' for that case.
  if (II->hasOneUse() && SelectArg->hasOneUse() &&
      !match(II->getArgOperand(1), m_One()))
    II->setArgOperand(1, ConstantInt::getTrue(II->getContext()));

  return nullptr;
}

static Value *canonicalizeSPF(ICmpInst &Cmp, Value *TrueVal, Value *FalseVal,
                              InstCombinerImpl &IC) {
  Value *LHS, *RHS;
  // TODO: What to do with pointer min/max patterns?
  if (!TrueVal->getType()->isIntOrIntVectorTy())
    return nullptr;

  SelectPatternFlavor SPF =
      matchDecomposedSelectPattern(&Cmp, TrueVal, FalseVal, LHS, RHS).Flavor;
  if (SPF == SelectPatternFlavor::SPF_ABS ||
      SPF == SelectPatternFlavor::SPF_NABS) {
    if (!Cmp.hasOneUse() && !RHS->hasOneUse())
      return nullptr; // TODO: Relax this restriction.

    // Note that NSW flag can only be propagated for normal, non-negated abs!
    bool IntMinIsPoison = SPF == SelectPatternFlavor::SPF_ABS &&
                          match(RHS, m_NSWNeg(m_Specific(LHS)));
    Constant *IntMinIsPoisonC =
        ConstantInt::get(Type::getInt1Ty(Cmp.getContext()), IntMinIsPoison);
    Value *Abs =
        IC.Builder.CreateBinaryIntrinsic(Intrinsic::abs, LHS, IntMinIsPoisonC);

    if (SPF == SelectPatternFlavor::SPF_NABS)
      return IC.Builder.CreateNeg(Abs); // Always without NSW flag!
    return Abs;
  }

  if (SelectPatternResult::isMinOrMax(SPF)) {
    Intrinsic::ID IntrinsicID;
    switch (SPF) {
    case SelectPatternFlavor::SPF_UMIN:
      IntrinsicID = Intrinsic::umin;
      break;
    case SelectPatternFlavor::SPF_UMAX:
      IntrinsicID = Intrinsic::umax;
      break;
    case SelectPatternFlavor::SPF_SMIN:
      IntrinsicID = Intrinsic::smin;
      break;
    case SelectPatternFlavor::SPF_SMAX:
      IntrinsicID = Intrinsic::smax;
      break;
    default:
      llvm_unreachable("Unexpected SPF");
    }
    return IC.Builder.CreateBinaryIntrinsic(IntrinsicID, LHS, RHS);
  }

  return nullptr;
}

bool InstCombinerImpl::replaceInInstruction(Value *V, Value *Old, Value *New,
                                            unsigned Depth) {
  // Conservatively limit replacement to two instructions upwards.
  if (Depth == 2)
    return false;

  assert(!isa<Constant>(Old) && "Only replace non-constant values");

  auto *I = dyn_cast<Instruction>(V);
  if (!I || !I->hasOneUse() ||
      !isSafeToSpeculativelyExecuteWithVariableReplaced(I))
    return false;

  bool Changed = false;
  for (Use &U : I->operands()) {
    if (U == Old) {
      replaceUse(U, New);
      Worklist.add(I);
      Changed = true;
    } else {
      Changed |= replaceInInstruction(U, Old, New, Depth + 1);
    }
  }
  return Changed;
}

/// If we have a select with an equality comparison, then we know the value in
/// one of the arms of the select. See if substituting this value into an arm
/// and simplifying the result yields the same value as the other arm.
///
/// To make this transform safe, we must drop poison-generating flags
/// (nsw, etc) if we simplified to a binop because the select may be guarding
/// that poison from propagating. If the existing binop already had no
/// poison-generating flags, then this transform can be done by instsimplify.
///
/// Consider:
///   %cmp = icmp eq i32 %x, 2147483647
///   %add = add nsw i32 %x, 1
///   %sel = select i1 %cmp, i32 -2147483648, i32 %add
///
/// We can't replace %sel with %add unless we strip away the flags.
/// TODO: Wrapping flags could be preserved in some cases with better analysis.
Instruction *InstCombinerImpl::foldSelectValueEquivalence(SelectInst &Sel,
                                                          ICmpInst &Cmp) {
  if (!Cmp.isEquality())
    return nullptr;

  // Canonicalize the pattern to ICMP_EQ by swapping the select operands.
  Value *TrueVal = Sel.getTrueValue(), *FalseVal = Sel.getFalseValue();
  bool Swapped = false;
  if (Cmp.getPredicate() == ICmpInst::ICMP_NE) {
    std::swap(TrueVal, FalseVal);
    Swapped = true;
  }

  Value *CmpLHS = Cmp.getOperand(0), *CmpRHS = Cmp.getOperand(1);
  auto ReplaceOldOpWithNewOp = [&](Value *OldOp,
                                   Value *NewOp) -> Instruction * {
    // In X == Y ? f(X) : Z, try to evaluate f(Y) and replace the operand.
    // Take care to avoid replacing X == Y ? X : Z with X == Y ? Y : Z, as that
    // would lead to an infinite replacement cycle.
    // If we will be able to evaluate f(Y) to a constant, we can allow undef,
    // otherwise Y cannot be undef as we might pick different values for undef
    // in the icmp and in f(Y).
    if (TrueVal == OldOp)
      return nullptr;

    if (Value *V = simplifyWithOpReplaced(TrueVal, OldOp, NewOp, SQ,
                                          /* AllowRefinement=*/true)) {
      // Need some guarantees about the new simplified op to ensure we don't inf
      // loop.
      // If we simplify to a constant, replace if we aren't creating new undef.
      if (match(V, m_ImmConstant()) &&
          isGuaranteedNotToBeUndef(V, SQ.AC, &Sel, &DT))
        return replaceOperand(Sel, Swapped ? 2 : 1, V);

      // If NewOp is a constant and OldOp is not replace iff NewOp doesn't
      // contain and undef elements.
      if (match(NewOp, m_ImmConstant()) || NewOp == V) {
        if (isGuaranteedNotToBeUndef(NewOp, SQ.AC, &Sel, &DT))
          return replaceOperand(Sel, Swapped ? 2 : 1, V);
        return nullptr;
      }
    }

    // Even if TrueVal does not simplify, we can directly replace a use of
    // CmpLHS with CmpRHS, as long as the instruction is not used anywhere
    // else and is safe to speculatively execute (we may end up executing it
    // with different operands, which should not cause side-effects or trigger
    // undefined behavior). Only do this if CmpRHS is a constant, as
    // profitability is not clear for other cases.
    // FIXME: Support vectors.
    if (OldOp == CmpLHS && match(NewOp, m_ImmConstant()) &&
        !match(OldOp, m_Constant()) && !Cmp.getType()->isVectorTy() &&
        isGuaranteedNotToBeUndef(NewOp, SQ.AC, &Sel, &DT))
      if (replaceInInstruction(TrueVal, OldOp, NewOp))
        return &Sel;
    return nullptr;
  };

  if (Instruction *R = ReplaceOldOpWithNewOp(CmpLHS, CmpRHS))
    return R;
  if (Instruction *R = ReplaceOldOpWithNewOp(CmpRHS, CmpLHS))
    return R;

  auto *FalseInst = dyn_cast<Instruction>(FalseVal);
  if (!FalseInst)
    return nullptr;

  // InstSimplify already performed this fold if it was possible subject to
  // current poison-generating flags. Check whether dropping poison-generating
  // flags enables the transform.

  // Try each equivalence substitution possibility.
  // We have an 'EQ' comparison, so the select's false value will propagate.
  // Example:
  // (X == 42) ? 43 : (X + 1) --> (X == 42) ? (X + 1) : (X + 1) --> X + 1
  SmallVector<Instruction *> DropFlags;
  if (simplifyWithOpReplaced(FalseVal, CmpLHS, CmpRHS, SQ,
                             /* AllowRefinement */ false,
                             &DropFlags) == TrueVal ||
      simplifyWithOpReplaced(FalseVal, CmpRHS, CmpLHS, SQ,
                             /* AllowRefinement */ false,
                             &DropFlags) == TrueVal) {
    for (Instruction *I : DropFlags) {
      I->dropPoisonGeneratingAnnotations();
      Worklist.add(I);
    }

    return replaceInstUsesWith(Sel, FalseVal);
  }

  return nullptr;
}

// See if this is a pattern like:
//   %old_cmp1 = icmp slt i32 %x, C2
//   %old_replacement = select i1 %old_cmp1, i32 %target_low, i32 %target_high
//   %old_x_offseted = add i32 %x, C1
//   %old_cmp0 = icmp ult i32 %old_x_offseted, C0
//   %r = select i1 %old_cmp0, i32 %x, i32 %old_replacement
// This can be rewritten as more canonical pattern:
//   %new_cmp1 = icmp slt i32 %x, -C1
//   %new_cmp2 = icmp sge i32 %x, C0-C1
//   %new_clamped_low = select i1 %new_cmp1, i32 %target_low, i32 %x
//   %r = select i1 %new_cmp2, i32 %target_high, i32 %new_clamped_low
// Iff -C1 s<= C2 s<= C0-C1
// Also ULT predicate can also be UGT iff C0 != -1 (+invert result)
//      SLT predicate can also be SGT iff C2 != INT_MAX (+invert res.)
static Value *canonicalizeClampLike(SelectInst &Sel0, ICmpInst &Cmp0,
                                    InstCombiner::BuilderTy &Builder,
                                    InstCombiner &IC) {
  Value *X = Sel0.getTrueValue();
  Value *Sel1 = Sel0.getFalseValue();

  // First match the condition of the outermost select.
  // Said condition must be one-use.
  if (!Cmp0.hasOneUse())
    return nullptr;
  ICmpInst::Predicate Pred0 = Cmp0.getPredicate();
  Value *Cmp00 = Cmp0.getOperand(0);
  Constant *C0;
  if (!match(Cmp0.getOperand(1),
             m_CombineAnd(m_AnyIntegralConstant(), m_Constant(C0))))
    return nullptr;

  if (!isa<SelectInst>(Sel1)) {
    Pred0 = ICmpInst::getInversePredicate(Pred0);
    std::swap(X, Sel1);
  }

  // Canonicalize Cmp0 into ult or uge.
  // FIXME: we shouldn't care about lanes that are 'undef' in the end?
  switch (Pred0) {
  case ICmpInst::Predicate::ICMP_ULT:
  case ICmpInst::Predicate::ICMP_UGE:
    // Although icmp ult %x, 0 is an unusual thing to try and should generally
    // have been simplified, it does not verify with undef inputs so ensure we
    // are not in a strange state.
    if (!match(C0, m_SpecificInt_ICMP(
                       ICmpInst::Predicate::ICMP_NE,
                       APInt::getZero(C0->getType()->getScalarSizeInBits()))))
      return nullptr;
    break; // Great!
  case ICmpInst::Predicate::ICMP_ULE:
  case ICmpInst::Predicate::ICMP_UGT:
    // We want to canonicalize it to 'ult' or 'uge', so we'll need to increment
    // C0, which again means it must not have any all-ones elements.
    if (!match(C0,
               m_SpecificInt_ICMP(
                   ICmpInst::Predicate::ICMP_NE,
                   APInt::getAllOnes(C0->getType()->getScalarSizeInBits()))))
      return nullptr; // Can't do, have all-ones element[s].
    Pred0 = ICmpInst::getFlippedStrictnessPredicate(Pred0);
    C0 = InstCombiner::AddOne(C0);
    break;
  default:
    return nullptr; // Unknown predicate.
  }

  // Now that we've canonicalized the ICmp, we know the X we expect;
  // the select in other hand should be one-use.
  if (!Sel1->hasOneUse())
    return nullptr;

  // If the types do not match, look through any truncs to the underlying
  // instruction.
  if (Cmp00->getType() != X->getType() && X->hasOneUse())
    match(X, m_TruncOrSelf(m_Value(X)));

  // We now can finish matching the condition of the outermost select:
  // it should either be the X itself, or an addition of some constant to X.
  Constant *C1;
  if (Cmp00 == X)
    C1 = ConstantInt::getNullValue(X->getType());
  else if (!match(Cmp00,
                  m_Add(m_Specific(X),
                        m_CombineAnd(m_AnyIntegralConstant(), m_Constant(C1)))))
    return nullptr;

  Value *Cmp1;
  ICmpInst::Predicate Pred1;
  Constant *C2;
  Value *ReplacementLow, *ReplacementHigh;
  if (!match(Sel1, m_Select(m_Value(Cmp1), m_Value(ReplacementLow),
                            m_Value(ReplacementHigh))) ||
      !match(Cmp1,
             m_ICmp(Pred1, m_Specific(X),
                    m_CombineAnd(m_AnyIntegralConstant(), m_Constant(C2)))))
    return nullptr;

  if (!Cmp1->hasOneUse() && (Cmp00 == X || !Cmp00->hasOneUse()))
    return nullptr; // Not enough one-use instructions for the fold.
  // FIXME: this restriction could be relaxed if Cmp1 can be reused as one of
  //        two comparisons we'll need to build.

  // Canonicalize Cmp1 into the form we expect.
  // FIXME: we shouldn't care about lanes that are 'undef' in the end?
  switch (Pred1) {
  case ICmpInst::Predicate::ICMP_SLT:
    break;
  case ICmpInst::Predicate::ICMP_SLE:
    // We'd have to increment C2 by one, and for that it must not have signed
    // max element, but then it would have been canonicalized to 'slt' before
    // we get here. So we can't do anything useful with 'sle'.
    return nullptr;
  case ICmpInst::Predicate::ICMP_SGT:
    // We want to canonicalize it to 'slt', so we'll need to increment C2,
    // which again means it must not have any signed max elements.
    if (!match(C2,
               m_SpecificInt_ICMP(ICmpInst::Predicate::ICMP_NE,
                                  APInt::getSignedMaxValue(
                                      C2->getType()->getScalarSizeInBits()))))
      return nullptr; // Can't do, have signed max element[s].
    C2 = InstCombiner::AddOne(C2);
    [[fallthrough]];
  case ICmpInst::Predicate::ICMP_SGE:
    // Also non-canonical, but here we don't need to change C2,
    // so we don't have any restrictions on C2, so we can just handle it.
    Pred1 = ICmpInst::Predicate::ICMP_SLT;
    std::swap(ReplacementLow, ReplacementHigh);
    break;
  default:
    return nullptr; // Unknown predicate.
  }
  assert(Pred1 == ICmpInst::Predicate::ICMP_SLT &&
         "Unexpected predicate type.");

  // The thresholds of this clamp-like pattern.
  auto *ThresholdLowIncl = ConstantExpr::getNeg(C1);
  auto *ThresholdHighExcl = ConstantExpr::getSub(C0, C1);

  assert((Pred0 == ICmpInst::Predicate::ICMP_ULT ||
          Pred0 == ICmpInst::Predicate::ICMP_UGE) &&
         "Unexpected predicate type.");
  if (Pred0 == ICmpInst::Predicate::ICMP_UGE)
    std::swap(ThresholdLowIncl, ThresholdHighExcl);

  // The fold has a precondition 1: C2 s>= ThresholdLow
  auto *Precond1 = ConstantFoldCompareInstOperands(
      ICmpInst::Predicate::ICMP_SGE, C2, ThresholdLowIncl, IC.getDataLayout());
  if (!Precond1 || !match(Precond1, m_One()))
    return nullptr;
  // The fold has a precondition 2: C2 s<= ThresholdHigh
  auto *Precond2 = ConstantFoldCompareInstOperands(
      ICmpInst::Predicate::ICMP_SLE, C2, ThresholdHighExcl, IC.getDataLayout());
  if (!Precond2 || !match(Precond2, m_One()))
    return nullptr;

  // If we are matching from a truncated input, we need to sext the
  // ReplacementLow and ReplacementHigh values. Only do the transform if they
  // are free to extend due to being constants.
  if (X->getType() != Sel0.getType()) {
    Constant *LowC, *HighC;
    if (!match(ReplacementLow, m_ImmConstant(LowC)) ||
        !match(ReplacementHigh, m_ImmConstant(HighC)))
      return nullptr;
    const DataLayout &DL = Sel0.getDataLayout();
    ReplacementLow =
        ConstantFoldCastOperand(Instruction::SExt, LowC, X->getType(), DL);
    ReplacementHigh =
        ConstantFoldCastOperand(Instruction::SExt, HighC, X->getType(), DL);
    assert(ReplacementLow && ReplacementHigh &&
           "Constant folding of ImmConstant cannot fail");
  }

  // All good, finally emit the new pattern.
  Value *ShouldReplaceLow = Builder.CreateICmpSLT(X, ThresholdLowIncl);
  Value *ShouldReplaceHigh = Builder.CreateICmpSGE(X, ThresholdHighExcl);
  Value *MaybeReplacedLow =
      Builder.CreateSelect(ShouldReplaceLow, ReplacementLow, X);

  // Create the final select. If we looked through a truncate above, we will
  // need to retruncate the result.
  Value *MaybeReplacedHigh = Builder.CreateSelect(
      ShouldReplaceHigh, ReplacementHigh, MaybeReplacedLow);
  return Builder.CreateTrunc(MaybeReplacedHigh, Sel0.getType());
}

// If we have
//  %cmp = icmp [canonical predicate] i32 %x, C0
//  %r = select i1 %cmp, i32 %y, i32 C1
// Where C0 != C1 and %x may be different from %y, see if the constant that we
// will have if we flip the strictness of the predicate (i.e. without changing
// the result) is identical to the C1 in select. If it matches we can change
// original comparison to one with swapped predicate, reuse the constant,
// and swap the hands of select.
static Instruction *
tryToReuseConstantFromSelectInComparison(SelectInst &Sel, ICmpInst &Cmp,
                                         InstCombinerImpl &IC) {
  ICmpInst::Predicate Pred;
  Value *X;
  Constant *C0;
  if (!match(&Cmp, m_OneUse(m_ICmp(
                       Pred, m_Value(X),
                       m_CombineAnd(m_AnyIntegralConstant(), m_Constant(C0))))))
    return nullptr;

  // If comparison predicate is non-relational, we won't be able to do anything.
  if (ICmpInst::isEquality(Pred))
    return nullptr;

  // If comparison predicate is non-canonical, then we certainly won't be able
  // to make it canonical; canonicalizeCmpWithConstant() already tried.
  if (!InstCombiner::isCanonicalPredicate(Pred))
    return nullptr;

  // If the [input] type of comparison and select type are different, lets abort
  // for now. We could try to compare constants with trunc/[zs]ext though.
  if (C0->getType() != Sel.getType())
    return nullptr;

  // ULT with 'add' of a constant is canonical. See foldICmpAddConstant().
  // FIXME: Are there more magic icmp predicate+constant pairs we must avoid?
  //        Or should we just abandon this transform entirely?
  if (Pred == CmpInst::ICMP_ULT && match(X, m_Add(m_Value(), m_Constant())))
    return nullptr;


  Value *SelVal0, *SelVal1; // We do not care which one is from where.
  match(&Sel, m_Select(m_Value(), m_Value(SelVal0), m_Value(SelVal1)));
  // At least one of these values we are selecting between must be a constant
  // else we'll never succeed.
  if (!match(SelVal0, m_AnyIntegralConstant()) &&
      !match(SelVal1, m_AnyIntegralConstant()))
    return nullptr;

  // Does this constant C match any of the `select` values?
  auto MatchesSelectValue = [SelVal0, SelVal1](Constant *C) {
    return C->isElementWiseEqual(SelVal0) || C->isElementWiseEqual(SelVal1);
  };

  // If C0 *already* matches true/false value of select, we are done.
  if (MatchesSelectValue(C0))
    return nullptr;

  // Check the constant we'd have with flipped-strictness predicate.
  auto FlippedStrictness =
      InstCombiner::getFlippedStrictnessPredicateAndConstant(Pred, C0);
  if (!FlippedStrictness)
    return nullptr;

  // If said constant doesn't match either, then there is no hope,
  if (!MatchesSelectValue(FlippedStrictness->second))
    return nullptr;

  // It matched! Lets insert the new comparison just before select.
  InstCombiner::BuilderTy::InsertPointGuard Guard(IC.Builder);
  IC.Builder.SetInsertPoint(&Sel);

  Pred = ICmpInst::getSwappedPredicate(Pred); // Yes, swapped.
  Value *NewCmp = IC.Builder.CreateICmp(Pred, X, FlippedStrictness->second,
                                        Cmp.getName() + ".inv");
  IC.replaceOperand(Sel, 0, NewCmp);
  Sel.swapValues();
  Sel.swapProfMetadata();

  return &Sel;
}

static Instruction *foldSelectZeroOrOnes(ICmpInst *Cmp, Value *TVal,
                                         Value *FVal,
                                         InstCombiner::BuilderTy &Builder) {
  if (!Cmp->hasOneUse())
    return nullptr;

  const APInt *CmpC;
  if (!match(Cmp->getOperand(1), m_APIntAllowPoison(CmpC)))
    return nullptr;

  // (X u< 2) ? -X : -1 --> sext (X != 0)
  Value *X = Cmp->getOperand(0);
  if (Cmp->getPredicate() == ICmpInst::ICMP_ULT && *CmpC == 2 &&
      match(TVal, m_Neg(m_Specific(X))) && match(FVal, m_AllOnes()))
    return new SExtInst(Builder.CreateIsNotNull(X), TVal->getType());

  // (X u> 1) ? -1 : -X --> sext (X != 0)
  if (Cmp->getPredicate() == ICmpInst::ICMP_UGT && *CmpC == 1 &&
      match(FVal, m_Neg(m_Specific(X))) && match(TVal, m_AllOnes()))
    return new SExtInst(Builder.CreateIsNotNull(X), TVal->getType());

  return nullptr;
}

static Value *foldSelectInstWithICmpConst(SelectInst &SI, ICmpInst *ICI,
                                          InstCombiner::BuilderTy &Builder) {
  const APInt *CmpC;
  Value *V;
  CmpInst::Predicate Pred;
  if (!match(ICI, m_ICmp(Pred, m_Value(V), m_APInt(CmpC))))
    return nullptr;

  // Match clamp away from min/max value as a max/min operation.
  Value *TVal = SI.getTrueValue();
  Value *FVal = SI.getFalseValue();
  if (Pred == ICmpInst::ICMP_EQ && V == FVal) {
    // (V == UMIN) ? UMIN+1 : V --> umax(V, UMIN+1)
    if (CmpC->isMinValue() && match(TVal, m_SpecificInt(*CmpC + 1)))
      return Builder.CreateBinaryIntrinsic(Intrinsic::umax, V, TVal);
    // (V == UMAX) ? UMAX-1 : V --> umin(V, UMAX-1)
    if (CmpC->isMaxValue() && match(TVal, m_SpecificInt(*CmpC - 1)))
      return Builder.CreateBinaryIntrinsic(Intrinsic::umin, V, TVal);
    // (V == SMIN) ? SMIN+1 : V --> smax(V, SMIN+1)
    if (CmpC->isMinSignedValue() && match(TVal, m_SpecificInt(*CmpC + 1)))
      return Builder.CreateBinaryIntrinsic(Intrinsic::smax, V, TVal);
    // (V == SMAX) ? SMAX-1 : V --> smin(V, SMAX-1)
    if (CmpC->isMaxSignedValue() && match(TVal, m_SpecificInt(*CmpC - 1)))
      return Builder.CreateBinaryIntrinsic(Intrinsic::smin, V, TVal);
  }

  BinaryOperator *BO;
  const APInt *C;
  CmpInst::Predicate CPred;
  if (match(&SI, m_Select(m_Specific(ICI), m_APInt(C), m_BinOp(BO))))
    CPred = ICI->getPredicate();
  else if (match(&SI, m_Select(m_Specific(ICI), m_BinOp(BO), m_APInt(C))))
    CPred = ICI->getInversePredicate();
  else
    return nullptr;

  const APInt *BinOpC;
  if (!match(BO, m_BinOp(m_Specific(V), m_APInt(BinOpC))))
    return nullptr;

  ConstantRange R = ConstantRange::makeExactICmpRegion(CPred, *CmpC)
                        .binaryOp(BO->getOpcode(), *BinOpC);
  if (R == *C) {
    BO->dropPoisonGeneratingFlags();
    return BO;
  }
  return nullptr;
}

static Instruction *foldSelectICmpEq(SelectInst &SI, ICmpInst *ICI,
                                     InstCombinerImpl &IC) {
  ICmpInst::Predicate Pred = ICI->getPredicate();
  if (!ICmpInst::isEquality(Pred))
    return nullptr;

  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();
  Value *CmpLHS = ICI->getOperand(0);
  Value *CmpRHS = ICI->getOperand(1);

  if (Pred == ICmpInst::ICMP_NE)
    std::swap(TrueVal, FalseVal);

  // Transform (X == C) ? X : Y -> (X == C) ? C : Y
  // specific handling for Bitwise operation.
  // x&y -> (x|y) ^ (x^y)  or  (x|y) & ~(x^y)
  // x|y -> (x&y) | (x^y)  or  (x&y) ^  (x^y)
  // x^y -> (x|y) ^ (x&y)  or  (x|y) & ~(x&y)
  Value *X, *Y;
  if (!match(CmpLHS, m_BitwiseLogic(m_Value(X), m_Value(Y))) ||
      !match(TrueVal, m_c_BitwiseLogic(m_Specific(X), m_Specific(Y))))
    return nullptr;

  const unsigned AndOps = Instruction::And, OrOps = Instruction::Or,
                 XorOps = Instruction::Xor, NoOps = 0;
  enum NotMask { None = 0, NotInner, NotRHS };

  auto matchFalseVal = [&](unsigned OuterOpc, unsigned InnerOpc,
                           unsigned NotMask) {
    auto matchInner = m_c_BinOp(InnerOpc, m_Specific(X), m_Specific(Y));
    if (OuterOpc == NoOps)
      return match(CmpRHS, m_Zero()) && match(FalseVal, matchInner);

    if (NotMask == NotInner) {
      return match(FalseVal, m_c_BinOp(OuterOpc, m_NotForbidPoison(matchInner),
                                       m_Specific(CmpRHS)));
    } else if (NotMask == NotRHS) {
      return match(FalseVal, m_c_BinOp(OuterOpc, matchInner,
                                       m_NotForbidPoison(m_Specific(CmpRHS))));
    } else {
      return match(FalseVal,
                   m_c_BinOp(OuterOpc, matchInner, m_Specific(CmpRHS)));
    }
  };

  // (X&Y)==C ? X|Y : X^Y -> (X^Y)|C : X^Y  or (X^Y)^ C : X^Y
  // (X&Y)==C ? X^Y : X|Y -> (X|Y)^C : X|Y  or (X|Y)&~C : X|Y
  if (match(CmpLHS, m_And(m_Value(X), m_Value(Y)))) {
    if (match(TrueVal, m_c_Or(m_Specific(X), m_Specific(Y)))) {
      // (X&Y)==C ? X|Y : (X^Y)|C -> (X^Y)|C : (X^Y)|C -> (X^Y)|C
      // (X&Y)==C ? X|Y : (X^Y)^C -> (X^Y)^C : (X^Y)^C -> (X^Y)^C
      if (matchFalseVal(OrOps, XorOps, None) ||
          matchFalseVal(XorOps, XorOps, None))
        return IC.replaceInstUsesWith(SI, FalseVal);
    } else if (match(TrueVal, m_c_Xor(m_Specific(X), m_Specific(Y)))) {
      // (X&Y)==C ? X^Y : (X|Y)^ C -> (X|Y)^ C : (X|Y)^ C -> (X|Y)^ C
      // (X&Y)==C ? X^Y : (X|Y)&~C -> (X|Y)&~C : (X|Y)&~C -> (X|Y)&~C
      if (matchFalseVal(XorOps, OrOps, None) ||
          matchFalseVal(AndOps, OrOps, NotRHS))
        return IC.replaceInstUsesWith(SI, FalseVal);
    }
  }

  // (X|Y)==C ? X&Y : X^Y -> (X^Y)^C : X^Y  or  ~(X^Y)&C : X^Y
  // (X|Y)==C ? X^Y : X&Y -> (X&Y)^C : X&Y  or  ~(X&Y)&C : X&Y
  if (match(CmpLHS, m_Or(m_Value(X), m_Value(Y)))) {
    if (match(TrueVal, m_c_And(m_Specific(X), m_Specific(Y)))) {
      // (X|Y)==C ? X&Y: (X^Y)^C -> (X^Y)^C: (X^Y)^C ->  (X^Y)^C
      // (X|Y)==C ? X&Y:~(X^Y)&C ->~(X^Y)&C:~(X^Y)&C -> ~(X^Y)&C
      if (matchFalseVal(XorOps, XorOps, None) ||
          matchFalseVal(AndOps, XorOps, NotInner))
        return IC.replaceInstUsesWith(SI, FalseVal);
    } else if (match(TrueVal, m_c_Xor(m_Specific(X), m_Specific(Y)))) {
      // (X|Y)==C ? X^Y : (X&Y)^C ->  (X&Y)^C : (X&Y)^C ->  (X&Y)^C
      // (X|Y)==C ? X^Y :~(X&Y)&C -> ~(X&Y)&C :~(X&Y)&C -> ~(X&Y)&C
      if (matchFalseVal(XorOps, AndOps, None) ||
          matchFalseVal(AndOps, AndOps, NotInner))
        return IC.replaceInstUsesWith(SI, FalseVal);
    }
  }

  // (X^Y)==C ? X&Y : X|Y -> (X|Y)^C : X|Y  or (X|Y)&~C : X|Y
  // (X^Y)==C ? X|Y : X&Y -> (X&Y)|C : X&Y  or (X&Y)^ C : X&Y
  if (match(CmpLHS, m_Xor(m_Value(X), m_Value(Y)))) {
    if ((match(TrueVal, m_c_And(m_Specific(X), m_Specific(Y))))) {
      // (X^Y)==C ? X&Y : (X|Y)^C -> (X|Y)^C
      // (X^Y)==C ? X&Y : (X|Y)&~C -> (X|Y)&~C
      if (matchFalseVal(XorOps, OrOps, None) ||
          matchFalseVal(AndOps, OrOps, NotRHS))
        return IC.replaceInstUsesWith(SI, FalseVal);
    } else if (match(TrueVal, m_c_Or(m_Specific(X), m_Specific(Y)))) {
      // (X^Y)==C ? (X|Y) : (X&Y)|C -> (X&Y)|C
      // (X^Y)==C ? (X|Y) : (X&Y)^C -> (X&Y)^C
      if (matchFalseVal(OrOps, AndOps, None) ||
          matchFalseVal(XorOps, AndOps, None))
        return IC.replaceInstUsesWith(SI, FalseVal);
    }
  }

  return nullptr;
}

/// Visit a SelectInst that has an ICmpInst as its first operand.
Instruction *InstCombinerImpl::foldSelectInstWithICmp(SelectInst &SI,
                                                      ICmpInst *ICI) {
  if (Instruction *NewSel = foldSelectValueEquivalence(SI, *ICI))
    return NewSel;

  if (Value *V =
          canonicalizeSPF(*ICI, SI.getTrueValue(), SI.getFalseValue(), *this))
    return replaceInstUsesWith(SI, V);

  if (Value *V = foldSelectInstWithICmpConst(SI, ICI, Builder))
    return replaceInstUsesWith(SI, V);

  if (Value *V = canonicalizeClampLike(SI, *ICI, Builder, *this))
    return replaceInstUsesWith(SI, V);

  if (Instruction *NewSel =
          tryToReuseConstantFromSelectInComparison(SI, *ICI, *this))
    return NewSel;

  if (Value *V = foldSelectICmpAnd(SI, ICI, Builder))
    return replaceInstUsesWith(SI, V);

  // NOTE: if we wanted to, this is where to detect integer MIN/MAX
  bool Changed = false;
  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();
  ICmpInst::Predicate Pred = ICI->getPredicate();
  Value *CmpLHS = ICI->getOperand(0);
  Value *CmpRHS = ICI->getOperand(1);
  if (CmpRHS != CmpLHS && isa<Constant>(CmpRHS) && !isa<Constant>(CmpLHS)) {
    if (CmpLHS == TrueVal && Pred == ICmpInst::ICMP_EQ) {
      // Transform (X == C) ? X : Y -> (X == C) ? C : Y
      replaceOperand(SI, 1, CmpRHS);
      Changed = true;
    } else if (CmpLHS == FalseVal && Pred == ICmpInst::ICMP_NE) {
      // Transform (X != C) ? Y : X -> (X != C) ? Y : C
      replaceOperand(SI, 2, CmpRHS);
      Changed = true;
    }
  }

  if (Instruction *NewSel = foldSelectICmpEq(SI, ICI, *this))
    return NewSel;

  // Canonicalize a signbit condition to use zero constant by swapping:
  // (CmpLHS > -1) ? TV : FV --> (CmpLHS < 0) ? FV : TV
  // To avoid conflicts (infinite loops) with other canonicalizations, this is
  // not applied with any constant select arm.
  if (Pred == ICmpInst::ICMP_SGT && match(CmpRHS, m_AllOnes()) &&
      !match(TrueVal, m_Constant()) && !match(FalseVal, m_Constant()) &&
      ICI->hasOneUse()) {
    InstCombiner::BuilderTy::InsertPointGuard Guard(Builder);
    Builder.SetInsertPoint(&SI);
    Value *IsNeg = Builder.CreateIsNeg(CmpLHS, ICI->getName());
    replaceOperand(SI, 0, IsNeg);
    SI.swapValues();
    SI.swapProfMetadata();
    return &SI;
  }

  // FIXME: This code is nearly duplicated in InstSimplify. Using/refactoring
  // decomposeBitTestICmp() might help.
  if (TrueVal->getType()->isIntOrIntVectorTy()) {
    unsigned BitWidth =
        DL.getTypeSizeInBits(TrueVal->getType()->getScalarType());
    APInt MinSignedValue = APInt::getSignedMinValue(BitWidth);
    Value *X;
    const APInt *Y, *C;
    bool TrueWhenUnset;
    bool IsBitTest = false;
    if (ICmpInst::isEquality(Pred) &&
        match(CmpLHS, m_And(m_Value(X), m_Power2(Y))) &&
        match(CmpRHS, m_Zero())) {
      IsBitTest = true;
      TrueWhenUnset = Pred == ICmpInst::ICMP_EQ;
    } else if (Pred == ICmpInst::ICMP_SLT && match(CmpRHS, m_Zero())) {
      X = CmpLHS;
      Y = &MinSignedValue;
      IsBitTest = true;
      TrueWhenUnset = false;
    } else if (Pred == ICmpInst::ICMP_SGT && match(CmpRHS, m_AllOnes())) {
      X = CmpLHS;
      Y = &MinSignedValue;
      IsBitTest = true;
      TrueWhenUnset = true;
    }
    if (IsBitTest) {
      Value *V = nullptr;
      // (X & Y) == 0 ? X : X ^ Y  --> X & ~Y
      if (TrueWhenUnset && TrueVal == X &&
          match(FalseVal, m_Xor(m_Specific(X), m_APInt(C))) && *Y == *C)
        V = Builder.CreateAnd(X, ~(*Y));
      // (X & Y) != 0 ? X ^ Y : X  --> X & ~Y
      else if (!TrueWhenUnset && FalseVal == X &&
               match(TrueVal, m_Xor(m_Specific(X), m_APInt(C))) && *Y == *C)
        V = Builder.CreateAnd(X, ~(*Y));
      // (X & Y) == 0 ? X ^ Y : X  --> X | Y
      else if (TrueWhenUnset && FalseVal == X &&
               match(TrueVal, m_Xor(m_Specific(X), m_APInt(C))) && *Y == *C)
        V = Builder.CreateOr(X, *Y);
      // (X & Y) != 0 ? X : X ^ Y  --> X | Y
      else if (!TrueWhenUnset && TrueVal == X &&
               match(FalseVal, m_Xor(m_Specific(X), m_APInt(C))) && *Y == *C)
        V = Builder.CreateOr(X, *Y);

      if (V)
        return replaceInstUsesWith(SI, V);
    }
  }

  if (Instruction *V =
          foldSelectICmpAndAnd(SI.getType(), ICI, TrueVal, FalseVal, Builder))
    return V;

  if (Value *V = foldSelectICmpAndZeroShl(ICI, TrueVal, FalseVal, Builder))
    return replaceInstUsesWith(SI, V);

  if (Instruction *V = foldSelectCtlzToCttz(ICI, TrueVal, FalseVal, Builder))
    return V;

  if (Instruction *V = foldSelectZeroOrOnes(ICI, TrueVal, FalseVal, Builder))
    return V;

  if (Value *V = foldSelectICmpAndBinOp(ICI, TrueVal, FalseVal, Builder))
    return replaceInstUsesWith(SI, V);

  if (Value *V = foldSelectICmpLshrAshr(ICI, TrueVal, FalseVal, Builder))
    return replaceInstUsesWith(SI, V);

  if (Value *V = foldSelectCttzCtlz(ICI, TrueVal, FalseVal, *this))
    return replaceInstUsesWith(SI, V);

  if (Value *V = canonicalizeSaturatedSubtract(ICI, TrueVal, FalseVal, Builder))
    return replaceInstUsesWith(SI, V);

  if (Value *V = canonicalizeSaturatedAdd(ICI, TrueVal, FalseVal, Builder))
    return replaceInstUsesWith(SI, V);

  if (Value *V = foldAbsDiff(ICI, TrueVal, FalseVal, Builder))
    return replaceInstUsesWith(SI, V);

  return Changed ? &SI : nullptr;
}

/// SI is a select whose condition is a PHI node (but the two may be in
/// different blocks). See if the true/false values (V) are live in all of the
/// predecessor blocks of the PHI. For example, cases like this can't be mapped:
///
///   X = phi [ C1, BB1], [C2, BB2]
///   Y = add
///   Z = select X, Y, 0
///
/// because Y is not live in BB1/BB2.
static bool canSelectOperandBeMappingIntoPredBlock(const Value *V,
                                                   const SelectInst &SI) {
  // If the value is a non-instruction value like a constant or argument, it
  // can always be mapped.
  const Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return true;

  // If V is a PHI node defined in the same block as the condition PHI, we can
  // map the arguments.
  const PHINode *CondPHI = cast<PHINode>(SI.getCondition());

  if (const PHINode *VP = dyn_cast<PHINode>(I))
    if (VP->getParent() == CondPHI->getParent())
      return true;

  // Otherwise, if the PHI and select are defined in the same block and if V is
  // defined in a different block, then we can transform it.
  if (SI.getParent() == CondPHI->getParent() &&
      I->getParent() != CondPHI->getParent())
    return true;

  // Otherwise we have a 'hard' case and we can't tell without doing more
  // detailed dominator based analysis, punt.
  return false;
}

/// We have an SPF (e.g. a min or max) of an SPF of the form:
///   SPF2(SPF1(A, B), C)
Instruction *InstCombinerImpl::foldSPFofSPF(Instruction *Inner,
                                            SelectPatternFlavor SPF1, Value *A,
                                            Value *B, Instruction &Outer,
                                            SelectPatternFlavor SPF2,
                                            Value *C) {
  if (Outer.getType() != Inner->getType())
    return nullptr;

  if (C == A || C == B) {
    // MAX(MAX(A, B), B) -> MAX(A, B)
    // MIN(MIN(a, b), a) -> MIN(a, b)
    // TODO: This could be done in instsimplify.
    if (SPF1 == SPF2 && SelectPatternResult::isMinOrMax(SPF1))
      return replaceInstUsesWith(Outer, Inner);
  }

  return nullptr;
}

/// Turn select C, (X + Y), (X - Y) --> (X + (select C, Y, (-Y))).
/// This is even legal for FP.
static Instruction *foldAddSubSelect(SelectInst &SI,
                                     InstCombiner::BuilderTy &Builder) {
  Value *CondVal = SI.getCondition();
  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();
  auto *TI = dyn_cast<Instruction>(TrueVal);
  auto *FI = dyn_cast<Instruction>(FalseVal);
  if (!TI || !FI || !TI->hasOneUse() || !FI->hasOneUse())
    return nullptr;

  Instruction *AddOp = nullptr, *SubOp = nullptr;
  if ((TI->getOpcode() == Instruction::Sub &&
       FI->getOpcode() == Instruction::Add) ||
      (TI->getOpcode() == Instruction::FSub &&
       FI->getOpcode() == Instruction::FAdd)) {
    AddOp = FI;
    SubOp = TI;
  } else if ((FI->getOpcode() == Instruction::Sub &&
              TI->getOpcode() == Instruction::Add) ||
             (FI->getOpcode() == Instruction::FSub &&
              TI->getOpcode() == Instruction::FAdd)) {
    AddOp = TI;
    SubOp = FI;
  }

  if (AddOp) {
    Value *OtherAddOp = nullptr;
    if (SubOp->getOperand(0) == AddOp->getOperand(0)) {
      OtherAddOp = AddOp->getOperand(1);
    } else if (SubOp->getOperand(0) == AddOp->getOperand(1)) {
      OtherAddOp = AddOp->getOperand(0);
    }

    if (OtherAddOp) {
      // So at this point we know we have (Y -> OtherAddOp):
      //        select C, (add X, Y), (sub X, Z)
      Value *NegVal; // Compute -Z
      if (SI.getType()->isFPOrFPVectorTy()) {
        NegVal = Builder.CreateFNeg(SubOp->getOperand(1));
        if (Instruction *NegInst = dyn_cast<Instruction>(NegVal)) {
          FastMathFlags Flags = AddOp->getFastMathFlags();
          Flags &= SubOp->getFastMathFlags();
          NegInst->setFastMathFlags(Flags);
        }
      } else {
        NegVal = Builder.CreateNeg(SubOp->getOperand(1));
      }

      Value *NewTrueOp = OtherAddOp;
      Value *NewFalseOp = NegVal;
      if (AddOp != TI)
        std::swap(NewTrueOp, NewFalseOp);
      Value *NewSel = Builder.CreateSelect(CondVal, NewTrueOp, NewFalseOp,
                                           SI.getName() + ".p", &SI);

      if (SI.getType()->isFPOrFPVectorTy()) {
        Instruction *RI =
            BinaryOperator::CreateFAdd(SubOp->getOperand(0), NewSel);

        FastMathFlags Flags = AddOp->getFastMathFlags();
        Flags &= SubOp->getFastMathFlags();
        RI->setFastMathFlags(Flags);
        return RI;
      } else
        return BinaryOperator::CreateAdd(SubOp->getOperand(0), NewSel);
    }
  }
  return nullptr;
}

/// Turn X + Y overflows ? -1 : X + Y -> uadd_sat X, Y
/// And X - Y overflows ? 0 : X - Y -> usub_sat X, Y
/// Along with a number of patterns similar to:
/// X + Y overflows ? (X < 0 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
/// X - Y overflows ? (X > 0 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
static Instruction *
foldOverflowingAddSubSelect(SelectInst &SI, InstCombiner::BuilderTy &Builder) {
  Value *CondVal = SI.getCondition();
  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();

  WithOverflowInst *II;
  if (!match(CondVal, m_ExtractValue<1>(m_WithOverflowInst(II))) ||
      !match(FalseVal, m_ExtractValue<0>(m_Specific(II))))
    return nullptr;

  Value *X = II->getLHS();
  Value *Y = II->getRHS();

  auto IsSignedSaturateLimit = [&](Value *Limit, bool IsAdd) {
    Type *Ty = Limit->getType();

    ICmpInst::Predicate Pred;
    Value *TrueVal, *FalseVal, *Op;
    const APInt *C;
    if (!match(Limit, m_Select(m_ICmp(Pred, m_Value(Op), m_APInt(C)),
                               m_Value(TrueVal), m_Value(FalseVal))))
      return false;

    auto IsZeroOrOne = [](const APInt &C) { return C.isZero() || C.isOne(); };
    auto IsMinMax = [&](Value *Min, Value *Max) {
      APInt MinVal = APInt::getSignedMinValue(Ty->getScalarSizeInBits());
      APInt MaxVal = APInt::getSignedMaxValue(Ty->getScalarSizeInBits());
      return match(Min, m_SpecificInt(MinVal)) &&
             match(Max, m_SpecificInt(MaxVal));
    };

    if (Op != X && Op != Y)
      return false;

    if (IsAdd) {
      // X + Y overflows ? (X <s 0 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
      // X + Y overflows ? (X <s 1 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
      // X + Y overflows ? (Y <s 0 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
      // X + Y overflows ? (Y <s 1 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
      if (Pred == ICmpInst::ICMP_SLT && IsZeroOrOne(*C) &&
          IsMinMax(TrueVal, FalseVal))
        return true;
      // X + Y overflows ? (X >s 0 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
      // X + Y overflows ? (X >s -1 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
      // X + Y overflows ? (Y >s 0 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
      // X + Y overflows ? (Y >s -1 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
      if (Pred == ICmpInst::ICMP_SGT && IsZeroOrOne(*C + 1) &&
          IsMinMax(FalseVal, TrueVal))
        return true;
    } else {
      // X - Y overflows ? (X <s 0 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
      // X - Y overflows ? (X <s -1 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
      if (Op == X && Pred == ICmpInst::ICMP_SLT && IsZeroOrOne(*C + 1) &&
          IsMinMax(TrueVal, FalseVal))
        return true;
      // X - Y overflows ? (X >s -1 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
      // X - Y overflows ? (X >s -2 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
      if (Op == X && Pred == ICmpInst::ICMP_SGT && IsZeroOrOne(*C + 2) &&
          IsMinMax(FalseVal, TrueVal))
        return true;
      // X - Y overflows ? (Y <s 0 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
      // X - Y overflows ? (Y <s 1 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
      if (Op == Y && Pred == ICmpInst::ICMP_SLT && IsZeroOrOne(*C) &&
          IsMinMax(FalseVal, TrueVal))
        return true;
      // X - Y overflows ? (Y >s 0 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
      // X - Y overflows ? (Y >s -1 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
      if (Op == Y && Pred == ICmpInst::ICMP_SGT && IsZeroOrOne(*C + 1) &&
          IsMinMax(TrueVal, FalseVal))
        return true;
    }

    return false;
  };

  Intrinsic::ID NewIntrinsicID;
  if (II->getIntrinsicID() == Intrinsic::uadd_with_overflow &&
      match(TrueVal, m_AllOnes()))
    // X + Y overflows ? -1 : X + Y -> uadd_sat X, Y
    NewIntrinsicID = Intrinsic::uadd_sat;
  else if (II->getIntrinsicID() == Intrinsic::usub_with_overflow &&
           match(TrueVal, m_Zero()))
    // X - Y overflows ? 0 : X - Y -> usub_sat X, Y
    NewIntrinsicID = Intrinsic::usub_sat;
  else if (II->getIntrinsicID() == Intrinsic::sadd_with_overflow &&
           IsSignedSaturateLimit(TrueVal, /*IsAdd=*/true))
    // X + Y overflows ? (X <s 0 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
    // X + Y overflows ? (X <s 1 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
    // X + Y overflows ? (X >s 0 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
    // X + Y overflows ? (X >s -1 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
    // X + Y overflows ? (Y <s 0 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
    // X + Y overflows ? (Y <s 1 ? INTMIN : INTMAX) : X + Y --> sadd_sat X, Y
    // X + Y overflows ? (Y >s 0 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
    // X + Y overflows ? (Y >s -1 ? INTMAX : INTMIN) : X + Y --> sadd_sat X, Y
    NewIntrinsicID = Intrinsic::sadd_sat;
  else if (II->getIntrinsicID() == Intrinsic::ssub_with_overflow &&
           IsSignedSaturateLimit(TrueVal, /*IsAdd=*/false))
    // X - Y overflows ? (X <s 0 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
    // X - Y overflows ? (X <s -1 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
    // X - Y overflows ? (X >s -1 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
    // X - Y overflows ? (X >s -2 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
    // X - Y overflows ? (Y <s 0 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
    // X - Y overflows ? (Y <s 1 ? INTMAX : INTMIN) : X - Y --> ssub_sat X, Y
    // X - Y overflows ? (Y >s 0 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
    // X - Y overflows ? (Y >s -1 ? INTMIN : INTMAX) : X - Y --> ssub_sat X, Y
    NewIntrinsicID = Intrinsic::ssub_sat;
  else
    return nullptr;

  Function *F =
      Intrinsic::getDeclaration(SI.getModule(), NewIntrinsicID, SI.getType());
  return CallInst::Create(F, {X, Y});
}

Instruction *InstCombinerImpl::foldSelectExtConst(SelectInst &Sel) {
  Constant *C;
  if (!match(Sel.getTrueValue(), m_Constant(C)) &&
      !match(Sel.getFalseValue(), m_Constant(C)))
    return nullptr;

  Instruction *ExtInst;
  if (!match(Sel.getTrueValue(), m_Instruction(ExtInst)) &&
      !match(Sel.getFalseValue(), m_Instruction(ExtInst)))
    return nullptr;

  auto ExtOpcode = ExtInst->getOpcode();
  if (ExtOpcode != Instruction::ZExt && ExtOpcode != Instruction::SExt)
    return nullptr;

  // If we are extending from a boolean type or if we can create a select that
  // has the same size operands as its condition, try to narrow the select.
  Value *X = ExtInst->getOperand(0);
  Type *SmallType = X->getType();
  Value *Cond = Sel.getCondition();
  auto *Cmp = dyn_cast<CmpInst>(Cond);
  if (!SmallType->isIntOrIntVectorTy(1) &&
      (!Cmp || Cmp->getOperand(0)->getType() != SmallType))
    return nullptr;

  // If the constant is the same after truncation to the smaller type and
  // extension to the original type, we can narrow the select.
  Type *SelType = Sel.getType();
  Constant *TruncC = getLosslessTrunc(C, SmallType, ExtOpcode);
  if (TruncC && ExtInst->hasOneUse()) {
    Value *TruncCVal = cast<Value>(TruncC);
    if (ExtInst == Sel.getFalseValue())
      std::swap(X, TruncCVal);

    // select Cond, (ext X), C --> ext(select Cond, X, C')
    // select Cond, C, (ext X) --> ext(select Cond, C', X)
    Value *NewSel = Builder.CreateSelect(Cond, X, TruncCVal, "narrow", &Sel);
    return CastInst::Create(Instruction::CastOps(ExtOpcode), NewSel, SelType);
  }

  return nullptr;
}

/// Try to transform a vector select with a constant condition vector into a
/// shuffle for easier combining with other shuffles and insert/extract.
static Instruction *canonicalizeSelectToShuffle(SelectInst &SI) {
  Value *CondVal = SI.getCondition();
  Constant *CondC;
  auto *CondValTy = dyn_cast<FixedVectorType>(CondVal->getType());
  if (!CondValTy || !match(CondVal, m_Constant(CondC)))
    return nullptr;

  unsigned NumElts = CondValTy->getNumElements();
  SmallVector<int, 16> Mask;
  Mask.reserve(NumElts);
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *Elt = CondC->getAggregateElement(i);
    if (!Elt)
      return nullptr;

    if (Elt->isOneValue()) {
      // If the select condition element is true, choose from the 1st vector.
      Mask.push_back(i);
    } else if (Elt->isNullValue()) {
      // If the select condition element is false, choose from the 2nd vector.
      Mask.push_back(i + NumElts);
    } else if (isa<UndefValue>(Elt)) {
      // Undef in a select condition (choose one of the operands) does not mean
      // the same thing as undef in a shuffle mask (any value is acceptable), so
      // give up.
      return nullptr;
    } else {
      // Bail out on a constant expression.
      return nullptr;
    }
  }

  return new ShuffleVectorInst(SI.getTrueValue(), SI.getFalseValue(), Mask);
}

/// If we have a select of vectors with a scalar condition, try to convert that
/// to a vector select by splatting the condition. A splat may get folded with
/// other operations in IR and having all operands of a select be vector types
/// is likely better for vector codegen.
static Instruction *canonicalizeScalarSelectOfVecs(SelectInst &Sel,
                                                   InstCombinerImpl &IC) {
  auto *Ty = dyn_cast<VectorType>(Sel.getType());
  if (!Ty)
    return nullptr;

  // We can replace a single-use extract with constant index.
  Value *Cond = Sel.getCondition();
  if (!match(Cond, m_OneUse(m_ExtractElt(m_Value(), m_ConstantInt()))))
    return nullptr;

  // select (extelt V, Index), T, F --> select (splat V, Index), T, F
  // Splatting the extracted condition reduces code (we could directly create a
  // splat shuffle of the source vector to eliminate the intermediate step).
  return IC.replaceOperand(
      Sel, 0, IC.Builder.CreateVectorSplat(Ty->getElementCount(), Cond));
}

/// Reuse bitcasted operands between a compare and select:
/// select (cmp (bitcast C), (bitcast D)), (bitcast' C), (bitcast' D) -->
/// bitcast (select (cmp (bitcast C), (bitcast D)), (bitcast C), (bitcast D))
static Instruction *foldSelectCmpBitcasts(SelectInst &Sel,
                                          InstCombiner::BuilderTy &Builder) {
  Value *Cond = Sel.getCondition();
  Value *TVal = Sel.getTrueValue();
  Value *FVal = Sel.getFalseValue();

  CmpInst::Predicate Pred;
  Value *A, *B;
  if (!match(Cond, m_Cmp(Pred, m_Value(A), m_Value(B))))
    return nullptr;

  // The select condition is a compare instruction. If the select's true/false
  // values are already the same as the compare operands, there's nothing to do.
  if (TVal == A || TVal == B || FVal == A || FVal == B)
    return nullptr;

  Value *C, *D;
  if (!match(A, m_BitCast(m_Value(C))) || !match(B, m_BitCast(m_Value(D))))
    return nullptr;

  // select (cmp (bitcast C), (bitcast D)), (bitcast TSrc), (bitcast FSrc)
  Value *TSrc, *FSrc;
  if (!match(TVal, m_BitCast(m_Value(TSrc))) ||
      !match(FVal, m_BitCast(m_Value(FSrc))))
    return nullptr;

  // If the select true/false values are *different bitcasts* of the same source
  // operands, make the select operands the same as the compare operands and
  // cast the result. This is the canonical select form for min/max.
  Value *NewSel;
  if (TSrc == C && FSrc == D) {
    // select (cmp (bitcast C), (bitcast D)), (bitcast' C), (bitcast' D) -->
    // bitcast (select (cmp A, B), A, B)
    NewSel = Builder.CreateSelect(Cond, A, B, "", &Sel);
  } else if (TSrc == D && FSrc == C) {
    // select (cmp (bitcast C), (bitcast D)), (bitcast' D), (bitcast' C) -->
    // bitcast (select (cmp A, B), B, A)
    NewSel = Builder.CreateSelect(Cond, B, A, "", &Sel);
  } else {
    return nullptr;
  }
  return CastInst::CreateBitOrPointerCast(NewSel, Sel.getType());
}

/// Try to eliminate select instructions that test the returned flag of cmpxchg
/// instructions.
///
/// If a select instruction tests the returned flag of a cmpxchg instruction and
/// selects between the returned value of the cmpxchg instruction its compare
/// operand, the result of the select will always be equal to its false value.
/// For example:
///
///   %cmpxchg = cmpxchg ptr %ptr, i64 %compare, i64 %new_value seq_cst seq_cst
///   %val = extractvalue { i64, i1 } %cmpxchg, 0
///   %success = extractvalue { i64, i1 } %cmpxchg, 1
///   %sel = select i1 %success, i64 %compare, i64 %val
///   ret i64 %sel
///
/// The returned value of the cmpxchg instruction (%val) is the original value
/// located at %ptr prior to any update. If the cmpxchg operation succeeds, %val
/// must have been equal to %compare. Thus, the result of the select is always
/// equal to %val, and the code can be simplified to:
///
///   %cmpxchg = cmpxchg ptr %ptr, i64 %compare, i64 %new_value seq_cst seq_cst
///   %val = extractvalue { i64, i1 } %cmpxchg, 0
///   ret i64 %val
///
static Value *foldSelectCmpXchg(SelectInst &SI) {
  // A helper that determines if V is an extractvalue instruction whose
  // aggregate operand is a cmpxchg instruction and whose single index is equal
  // to I. If such conditions are true, the helper returns the cmpxchg
  // instruction; otherwise, a nullptr is returned.
  auto isExtractFromCmpXchg = [](Value *V, unsigned I) -> AtomicCmpXchgInst * {
    auto *Extract = dyn_cast<ExtractValueInst>(V);
    if (!Extract)
      return nullptr;
    if (Extract->getIndices()[0] != I)
      return nullptr;
    return dyn_cast<AtomicCmpXchgInst>(Extract->getAggregateOperand());
  };

  // If the select has a single user, and this user is a select instruction that
  // we can simplify, skip the cmpxchg simplification for now.
  if (SI.hasOneUse())
    if (auto *Select = dyn_cast<SelectInst>(SI.user_back()))
      if (Select->getCondition() == SI.getCondition())
        if (Select->getFalseValue() == SI.getTrueValue() ||
            Select->getTrueValue() == SI.getFalseValue())
          return nullptr;

  // Ensure the select condition is the returned flag of a cmpxchg instruction.
  auto *CmpXchg = isExtractFromCmpXchg(SI.getCondition(), 1);
  if (!CmpXchg)
    return nullptr;

  // Check the true value case: The true value of the select is the returned
  // value of the same cmpxchg used by the condition, and the false value is the
  // cmpxchg instruction's compare operand.
  if (auto *X = isExtractFromCmpXchg(SI.getTrueValue(), 0))
    if (X == CmpXchg && X->getCompareOperand() == SI.getFalseValue())
      return SI.getFalseValue();

  // Check the false value case: The false value of the select is the returned
  // value of the same cmpxchg used by the condition, and the true value is the
  // cmpxchg instruction's compare operand.
  if (auto *X = isExtractFromCmpXchg(SI.getFalseValue(), 0))
    if (X == CmpXchg && X->getCompareOperand() == SI.getTrueValue())
      return SI.getFalseValue();

  return nullptr;
}

/// Try to reduce a funnel/rotate pattern that includes a compare and select
/// into a funnel shift intrinsic. Example:
/// rotl32(a, b) --> (b == 0 ? a : ((a >> (32 - b)) | (a << b)))
///              --> call llvm.fshl.i32(a, a, b)
/// fshl32(a, b, c) --> (c == 0 ? a : ((b >> (32 - c)) | (a << c)))
///                 --> call llvm.fshl.i32(a, b, c)
/// fshr32(a, b, c) --> (c == 0 ? b : ((a >> (32 - c)) | (b << c)))
///                 --> call llvm.fshr.i32(a, b, c)
static Instruction *foldSelectFunnelShift(SelectInst &Sel,
                                          InstCombiner::BuilderTy &Builder) {
  // This must be a power-of-2 type for a bitmasking transform to be valid.
  unsigned Width = Sel.getType()->getScalarSizeInBits();
  if (!isPowerOf2_32(Width))
    return nullptr;

  BinaryOperator *Or0, *Or1;
  if (!match(Sel.getFalseValue(), m_OneUse(m_Or(m_BinOp(Or0), m_BinOp(Or1)))))
    return nullptr;

  Value *SV0, *SV1, *SA0, *SA1;
  if (!match(Or0, m_OneUse(m_LogicalShift(m_Value(SV0),
                                          m_ZExtOrSelf(m_Value(SA0))))) ||
      !match(Or1, m_OneUse(m_LogicalShift(m_Value(SV1),
                                          m_ZExtOrSelf(m_Value(SA1))))) ||
      Or0->getOpcode() == Or1->getOpcode())
    return nullptr;

  // Canonicalize to or(shl(SV0, SA0), lshr(SV1, SA1)).
  if (Or0->getOpcode() == BinaryOperator::LShr) {
    std::swap(Or0, Or1);
    std::swap(SV0, SV1);
    std::swap(SA0, SA1);
  }
  assert(Or0->getOpcode() == BinaryOperator::Shl &&
         Or1->getOpcode() == BinaryOperator::LShr &&
         "Illegal or(shift,shift) pair");

  // Check the shift amounts to see if they are an opposite pair.
  Value *ShAmt;
  if (match(SA1, m_OneUse(m_Sub(m_SpecificInt(Width), m_Specific(SA0)))))
    ShAmt = SA0;
  else if (match(SA0, m_OneUse(m_Sub(m_SpecificInt(Width), m_Specific(SA1)))))
    ShAmt = SA1;
  else
    return nullptr;

  // We should now have this pattern:
  // select ?, TVal, (or (shl SV0, SA0), (lshr SV1, SA1))
  // The false value of the select must be a funnel-shift of the true value:
  // IsFShl -> TVal must be SV0 else TVal must be SV1.
  bool IsFshl = (ShAmt == SA0);
  Value *TVal = Sel.getTrueValue();
  if ((IsFshl && TVal != SV0) || (!IsFshl && TVal != SV1))
    return nullptr;

  // Finally, see if the select is filtering out a shift-by-zero.
  Value *Cond = Sel.getCondition();
  ICmpInst::Predicate Pred;
  if (!match(Cond, m_OneUse(m_ICmp(Pred, m_Specific(ShAmt), m_ZeroInt()))) ||
      Pred != ICmpInst::ICMP_EQ)
    return nullptr;

  // If this is not a rotate then the select was blocking poison from the
  // 'shift-by-zero' non-TVal, but a funnel shift won't - so freeze it.
  if (SV0 != SV1) {
    if (IsFshl && !llvm::isGuaranteedNotToBePoison(SV1))
      SV1 = Builder.CreateFreeze(SV1);
    else if (!IsFshl && !llvm::isGuaranteedNotToBePoison(SV0))
      SV0 = Builder.CreateFreeze(SV0);
  }

  // This is a funnel/rotate that avoids shift-by-bitwidth UB in a suboptimal way.
  // Convert to funnel shift intrinsic.
  Intrinsic::ID IID = IsFshl ? Intrinsic::fshl : Intrinsic::fshr;
  Function *F = Intrinsic::getDeclaration(Sel.getModule(), IID, Sel.getType());
  ShAmt = Builder.CreateZExt(ShAmt, Sel.getType());
  return CallInst::Create(F, { SV0, SV1, ShAmt });
}

static Instruction *foldSelectToCopysign(SelectInst &Sel,
                                         InstCombiner::BuilderTy &Builder) {
  Value *Cond = Sel.getCondition();
  Value *TVal = Sel.getTrueValue();
  Value *FVal = Sel.getFalseValue();
  Type *SelType = Sel.getType();

  // Match select ?, TC, FC where the constants are equal but negated.
  // TODO: Generalize to handle a negated variable operand?
  const APFloat *TC, *FC;
  if (!match(TVal, m_APFloatAllowPoison(TC)) ||
      !match(FVal, m_APFloatAllowPoison(FC)) ||
      !abs(*TC).bitwiseIsEqual(abs(*FC)))
    return nullptr;

  assert(TC != FC && "Expected equal select arms to simplify");

  Value *X;
  const APInt *C;
  bool IsTrueIfSignSet;
  ICmpInst::Predicate Pred;
  if (!match(Cond, m_OneUse(m_ICmp(Pred, m_ElementWiseBitCast(m_Value(X)),
                                   m_APInt(C)))) ||
      !isSignBitCheck(Pred, *C, IsTrueIfSignSet) || X->getType() != SelType)
    return nullptr;

  // If needed, negate the value that will be the sign argument of the copysign:
  // (bitcast X) <  0 ? -TC :  TC --> copysign(TC,  X)
  // (bitcast X) <  0 ?  TC : -TC --> copysign(TC, -X)
  // (bitcast X) >= 0 ? -TC :  TC --> copysign(TC, -X)
  // (bitcast X) >= 0 ?  TC : -TC --> copysign(TC,  X)
  // Note: FMF from the select can not be propagated to the new instructions.
  if (IsTrueIfSignSet ^ TC->isNegative())
    X = Builder.CreateFNeg(X);

  // Canonicalize the magnitude argument as the positive constant since we do
  // not care about its sign.
  Value *MagArg = ConstantFP::get(SelType, abs(*TC));
  Function *F = Intrinsic::getDeclaration(Sel.getModule(), Intrinsic::copysign,
                                          Sel.getType());
  return CallInst::Create(F, { MagArg, X });
}

Instruction *InstCombinerImpl::foldVectorSelect(SelectInst &Sel) {
  if (!isa<VectorType>(Sel.getType()))
    return nullptr;

  Value *Cond = Sel.getCondition();
  Value *TVal = Sel.getTrueValue();
  Value *FVal = Sel.getFalseValue();
  Value *C, *X, *Y;

  if (match(Cond, m_VecReverse(m_Value(C)))) {
    auto createSelReverse = [&](Value *C, Value *X, Value *Y) {
      Value *V = Builder.CreateSelect(C, X, Y, Sel.getName(), &Sel);
      if (auto *I = dyn_cast<Instruction>(V))
        I->copyIRFlags(&Sel);
      Module *M = Sel.getModule();
      Function *F =
          Intrinsic::getDeclaration(M, Intrinsic::vector_reverse, V->getType());
      return CallInst::Create(F, V);
    };

    if (match(TVal, m_VecReverse(m_Value(X)))) {
      // select rev(C), rev(X), rev(Y) --> rev(select C, X, Y)
      if (match(FVal, m_VecReverse(m_Value(Y))) &&
          (Cond->hasOneUse() || TVal->hasOneUse() || FVal->hasOneUse()))
        return createSelReverse(C, X, Y);

      // select rev(C), rev(X), FValSplat --> rev(select C, X, FValSplat)
      if ((Cond->hasOneUse() || TVal->hasOneUse()) && isSplatValue(FVal))
        return createSelReverse(C, X, FVal);
    }
    // select rev(C), TValSplat, rev(Y) --> rev(select C, TValSplat, Y)
    else if (isSplatValue(TVal) && match(FVal, m_VecReverse(m_Value(Y))) &&
             (Cond->hasOneUse() || FVal->hasOneUse()))
      return createSelReverse(C, TVal, Y);
  }

  auto *VecTy = dyn_cast<FixedVectorType>(Sel.getType());
  if (!VecTy)
    return nullptr;

  unsigned NumElts = VecTy->getNumElements();
  APInt PoisonElts(NumElts, 0);
  APInt AllOnesEltMask(APInt::getAllOnes(NumElts));
  if (Value *V = SimplifyDemandedVectorElts(&Sel, AllOnesEltMask, PoisonElts)) {
    if (V != &Sel)
      return replaceInstUsesWith(Sel, V);
    return &Sel;
  }

  // A select of a "select shuffle" with a common operand can be rearranged
  // to select followed by "select shuffle". Because of poison, this only works
  // in the case of a shuffle with no undefined mask elements.
  ArrayRef<int> Mask;
  if (match(TVal, m_OneUse(m_Shuffle(m_Value(X), m_Value(Y), m_Mask(Mask)))) &&
      !is_contained(Mask, PoisonMaskElem) &&
      cast<ShuffleVectorInst>(TVal)->isSelect()) {
    if (X == FVal) {
      // select Cond, (shuf_sel X, Y), X --> shuf_sel X, (select Cond, Y, X)
      Value *NewSel = Builder.CreateSelect(Cond, Y, X, "sel", &Sel);
      return new ShuffleVectorInst(X, NewSel, Mask);
    }
    if (Y == FVal) {
      // select Cond, (shuf_sel X, Y), Y --> shuf_sel (select Cond, X, Y), Y
      Value *NewSel = Builder.CreateSelect(Cond, X, Y, "sel", &Sel);
      return new ShuffleVectorInst(NewSel, Y, Mask);
    }
  }
  if (match(FVal, m_OneUse(m_Shuffle(m_Value(X), m_Value(Y), m_Mask(Mask)))) &&
      !is_contained(Mask, PoisonMaskElem) &&
      cast<ShuffleVectorInst>(FVal)->isSelect()) {
    if (X == TVal) {
      // select Cond, X, (shuf_sel X, Y) --> shuf_sel X, (select Cond, X, Y)
      Value *NewSel = Builder.CreateSelect(Cond, X, Y, "sel", &Sel);
      return new ShuffleVectorInst(X, NewSel, Mask);
    }
    if (Y == TVal) {
      // select Cond, Y, (shuf_sel X, Y) --> shuf_sel (select Cond, Y, X), Y
      Value *NewSel = Builder.CreateSelect(Cond, Y, X, "sel", &Sel);
      return new ShuffleVectorInst(NewSel, Y, Mask);
    }
  }

  return nullptr;
}

static Instruction *foldSelectToPhiImpl(SelectInst &Sel, BasicBlock *BB,
                                        const DominatorTree &DT,
                                        InstCombiner::BuilderTy &Builder) {
  // Find the block's immediate dominator that ends with a conditional branch
  // that matches select's condition (maybe inverted).
  auto *IDomNode = DT[BB]->getIDom();
  if (!IDomNode)
    return nullptr;
  BasicBlock *IDom = IDomNode->getBlock();

  Value *Cond = Sel.getCondition();
  Value *IfTrue, *IfFalse;
  BasicBlock *TrueSucc, *FalseSucc;
  if (match(IDom->getTerminator(),
            m_Br(m_Specific(Cond), m_BasicBlock(TrueSucc),
                 m_BasicBlock(FalseSucc)))) {
    IfTrue = Sel.getTrueValue();
    IfFalse = Sel.getFalseValue();
  } else if (match(IDom->getTerminator(),
                   m_Br(m_Not(m_Specific(Cond)), m_BasicBlock(TrueSucc),
                        m_BasicBlock(FalseSucc)))) {
    IfTrue = Sel.getFalseValue();
    IfFalse = Sel.getTrueValue();
  } else
    return nullptr;

  // Make sure the branches are actually different.
  if (TrueSucc == FalseSucc)
    return nullptr;

  // We want to replace select %cond, %a, %b with a phi that takes value %a
  // for all incoming edges that are dominated by condition `%cond == true`,
  // and value %b for edges dominated by condition `%cond == false`. If %a
  // or %b are also phis from the same basic block, we can go further and take
  // their incoming values from the corresponding blocks.
  BasicBlockEdge TrueEdge(IDom, TrueSucc);
  BasicBlockEdge FalseEdge(IDom, FalseSucc);
  DenseMap<BasicBlock *, Value *> Inputs;
  for (auto *Pred : predecessors(BB)) {
    // Check implication.
    BasicBlockEdge Incoming(Pred, BB);
    if (DT.dominates(TrueEdge, Incoming))
      Inputs[Pred] = IfTrue->DoPHITranslation(BB, Pred);
    else if (DT.dominates(FalseEdge, Incoming))
      Inputs[Pred] = IfFalse->DoPHITranslation(BB, Pred);
    else
      return nullptr;
    // Check availability.
    if (auto *Insn = dyn_cast<Instruction>(Inputs[Pred]))
      if (!DT.dominates(Insn, Pred->getTerminator()))
        return nullptr;
  }

  Builder.SetInsertPoint(BB, BB->begin());
  auto *PN = Builder.CreatePHI(Sel.getType(), Inputs.size());
  for (auto *Pred : predecessors(BB))
    PN->addIncoming(Inputs[Pred], Pred);
  PN->takeName(&Sel);
  return PN;
}

static Instruction *foldSelectToPhi(SelectInst &Sel, const DominatorTree &DT,
                                    InstCombiner::BuilderTy &Builder) {
  // Try to replace this select with Phi in one of these blocks.
  SmallSetVector<BasicBlock *, 4> CandidateBlocks;
  CandidateBlocks.insert(Sel.getParent());
  for (Value *V : Sel.operands())
    if (auto *I = dyn_cast<Instruction>(V))
      CandidateBlocks.insert(I->getParent());

  for (BasicBlock *BB : CandidateBlocks)
    if (auto *PN = foldSelectToPhiImpl(Sel, BB, DT, Builder))
      return PN;
  return nullptr;
}

/// Tries to reduce a pattern that arises when calculating the remainder of the
/// Euclidean division. When the divisor is a power of two and is guaranteed not
/// to be negative, a signed remainder can be folded with a bitwise and.
///
/// (x % n) < 0 ? (x % n) + n : (x % n)
///    -> x & (n - 1)
static Instruction *foldSelectWithSRem(SelectInst &SI, InstCombinerImpl &IC,
                                       IRBuilderBase &Builder) {
  Value *CondVal = SI.getCondition();
  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();

  ICmpInst::Predicate Pred;
  Value *Op, *RemRes, *Remainder;
  const APInt *C;
  bool TrueIfSigned = false;

  if (!(match(CondVal, m_ICmp(Pred, m_Value(RemRes), m_APInt(C))) &&
        isSignBitCheck(Pred, *C, TrueIfSigned)))
    return nullptr;

  // If the sign bit is not set, we have a SGE/SGT comparison, and the operands
  // of the select are inverted.
  if (!TrueIfSigned)
    std::swap(TrueVal, FalseVal);

  auto FoldToBitwiseAnd = [&](Value *Remainder) -> Instruction * {
    Value *Add = Builder.CreateAdd(
        Remainder, Constant::getAllOnesValue(RemRes->getType()));
    return BinaryOperator::CreateAnd(Op, Add);
  };

  // Match the general case:
  // %rem = srem i32 %x, %n
  // %cnd = icmp slt i32 %rem, 0
  // %add = add i32 %rem, %n
  // %sel = select i1 %cnd, i32 %add, i32 %rem
  if (match(TrueVal, m_Add(m_Specific(RemRes), m_Value(Remainder))) &&
      match(RemRes, m_SRem(m_Value(Op), m_Specific(Remainder))) &&
      IC.isKnownToBeAPowerOfTwo(Remainder, /*OrZero*/ true) &&
      FalseVal == RemRes)
    return FoldToBitwiseAnd(Remainder);

  // Match the case where the one arm has been replaced by constant 1:
  // %rem = srem i32 %n, 2
  // %cnd = icmp slt i32 %rem, 0
  // %sel = select i1 %cnd, i32 1, i32 %rem
  if (match(TrueVal, m_One()) &&
      match(RemRes, m_SRem(m_Value(Op), m_SpecificInt(2))) &&
      FalseVal == RemRes)
    return FoldToBitwiseAnd(ConstantInt::get(RemRes->getType(), 2));

  return nullptr;
}

static Value *foldSelectWithFrozenICmp(SelectInst &Sel, InstCombiner::BuilderTy &Builder) {
  FreezeInst *FI = dyn_cast<FreezeInst>(Sel.getCondition());
  if (!FI)
    return nullptr;

  Value *Cond = FI->getOperand(0);
  Value *TrueVal = Sel.getTrueValue(), *FalseVal = Sel.getFalseValue();

  //   select (freeze(x == y)), x, y --> y
  //   select (freeze(x != y)), x, y --> x
  // The freeze should be only used by this select. Otherwise, remaining uses of
  // the freeze can observe a contradictory value.
  //   c = freeze(x == y)   ; Let's assume that y = poison & x = 42; c is 0 or 1
  //   a = select c, x, y   ;
  //   f(a, c)              ; f(poison, 1) cannot happen, but if a is folded
  //                        ; to y, this can happen.
  CmpInst::Predicate Pred;
  if (FI->hasOneUse() &&
      match(Cond, m_c_ICmp(Pred, m_Specific(TrueVal), m_Specific(FalseVal))) &&
      (Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE)) {
    return Pred == ICmpInst::ICMP_EQ ? FalseVal : TrueVal;
  }

  return nullptr;
}

/// Given that \p CondVal is known to be \p CondIsTrue, try to simplify \p SI.
static Value *simplifyNestedSelectsUsingImpliedCond(SelectInst &SI,
                                                    Value *CondVal,
                                                    bool CondIsTrue,
                                                    const DataLayout &DL) {
  Value *InnerCondVal = SI.getCondition();
  Value *InnerTrueVal = SI.getTrueValue();
  Value *InnerFalseVal = SI.getFalseValue();
  assert(CondVal->getType() == InnerCondVal->getType() &&
         "The type of inner condition must match with the outer.");
  if (auto Implied = isImpliedCondition(CondVal, InnerCondVal, DL, CondIsTrue))
    return *Implied ? InnerTrueVal : InnerFalseVal;
  return nullptr;
}

Instruction *InstCombinerImpl::foldAndOrOfSelectUsingImpliedCond(Value *Op,
                                                                 SelectInst &SI,
                                                                 bool IsAnd) {
  assert(Op->getType()->isIntOrIntVectorTy(1) &&
         "Op must be either i1 or vector of i1.");
  if (SI.getCondition()->getType() != Op->getType())
    return nullptr;
  if (Value *V = simplifyNestedSelectsUsingImpliedCond(SI, Op, IsAnd, DL))
    return SelectInst::Create(Op,
                              IsAnd ? V : ConstantInt::getTrue(Op->getType()),
                              IsAnd ? ConstantInt::getFalse(Op->getType()) : V);
  return nullptr;
}

// Canonicalize select with fcmp to fabs(). -0.0 makes this tricky. We need
// fast-math-flags (nsz) or fsub with +0.0 (not fneg) for this to work.
static Instruction *foldSelectWithFCmpToFabs(SelectInst &SI,
                                             InstCombinerImpl &IC) {
  Value *CondVal = SI.getCondition();

  bool ChangedFMF = false;
  for (bool Swap : {false, true}) {
    Value *TrueVal = SI.getTrueValue();
    Value *X = SI.getFalseValue();
    CmpInst::Predicate Pred;

    if (Swap)
      std::swap(TrueVal, X);

    if (!match(CondVal, m_FCmp(Pred, m_Specific(X), m_AnyZeroFP())))
      continue;

    // fold (X <= +/-0.0) ? (0.0 - X) : X to fabs(X), when 'Swap' is false
    // fold (X >  +/-0.0) ? X : (0.0 - X) to fabs(X), when 'Swap' is true
    if (match(TrueVal, m_FSub(m_PosZeroFP(), m_Specific(X)))) {
      if (!Swap && (Pred == FCmpInst::FCMP_OLE || Pred == FCmpInst::FCMP_ULE)) {
        Value *Fabs = IC.Builder.CreateUnaryIntrinsic(Intrinsic::fabs, X, &SI);
        return IC.replaceInstUsesWith(SI, Fabs);
      }
      if (Swap && (Pred == FCmpInst::FCMP_OGT || Pred == FCmpInst::FCMP_UGT)) {
        Value *Fabs = IC.Builder.CreateUnaryIntrinsic(Intrinsic::fabs, X, &SI);
        return IC.replaceInstUsesWith(SI, Fabs);
      }
    }

    if (!match(TrueVal, m_FNeg(m_Specific(X))))
      return nullptr;

    // Forward-propagate nnan and ninf from the fneg to the select.
    // If all inputs are not those values, then the select is not either.
    // Note: nsz is defined differently, so it may not be correct to propagate.
    FastMathFlags FMF = cast<FPMathOperator>(TrueVal)->getFastMathFlags();
    if (FMF.noNaNs() && !SI.hasNoNaNs()) {
      SI.setHasNoNaNs(true);
      ChangedFMF = true;
    }
    if (FMF.noInfs() && !SI.hasNoInfs()) {
      SI.setHasNoInfs(true);
      ChangedFMF = true;
    }

    // With nsz, when 'Swap' is false:
    // fold (X < +/-0.0) ? -X : X or (X <= +/-0.0) ? -X : X to fabs(X)
    // fold (X > +/-0.0) ? -X : X or (X >= +/-0.0) ? -X : X to -fabs(x)
    // when 'Swap' is true:
    // fold (X > +/-0.0) ? X : -X or (X >= +/-0.0) ? X : -X to fabs(X)
    // fold (X < +/-0.0) ? X : -X or (X <= +/-0.0) ? X : -X to -fabs(X)
    //
    // Note: We require "nnan" for this fold because fcmp ignores the signbit
    //       of NAN, but IEEE-754 specifies the signbit of NAN values with
    //       fneg/fabs operations.
    if (!SI.hasNoSignedZeros() || !SI.hasNoNaNs())
      return nullptr;

    if (Swap)
      Pred = FCmpInst::getSwappedPredicate(Pred);

    bool IsLTOrLE = Pred == FCmpInst::FCMP_OLT || Pred == FCmpInst::FCMP_OLE ||
                    Pred == FCmpInst::FCMP_ULT || Pred == FCmpInst::FCMP_ULE;
    bool IsGTOrGE = Pred == FCmpInst::FCMP_OGT || Pred == FCmpInst::FCMP_OGE ||
                    Pred == FCmpInst::FCMP_UGT || Pred == FCmpInst::FCMP_UGE;

    if (IsLTOrLE) {
      Value *Fabs = IC.Builder.CreateUnaryIntrinsic(Intrinsic::fabs, X, &SI);
      return IC.replaceInstUsesWith(SI, Fabs);
    }
    if (IsGTOrGE) {
      Value *Fabs = IC.Builder.CreateUnaryIntrinsic(Intrinsic::fabs, X, &SI);
      Instruction *NewFNeg = UnaryOperator::CreateFNeg(Fabs);
      NewFNeg->setFastMathFlags(SI.getFastMathFlags());
      return NewFNeg;
    }
  }

  // Match select with (icmp slt (bitcast X to int), 0)
  //                or (icmp sgt (bitcast X to int), -1)

  for (bool Swap : {false, true}) {
    Value *TrueVal = SI.getTrueValue();
    Value *X = SI.getFalseValue();

    if (Swap)
      std::swap(TrueVal, X);

    CmpInst::Predicate Pred;
    const APInt *C;
    bool TrueIfSigned;
    if (!match(CondVal,
               m_ICmp(Pred, m_ElementWiseBitCast(m_Specific(X)), m_APInt(C))) ||
        !isSignBitCheck(Pred, *C, TrueIfSigned))
      continue;
    if (!match(TrueVal, m_FNeg(m_Specific(X))))
      return nullptr;
    if (Swap == TrueIfSigned && !CondVal->hasOneUse() && !TrueVal->hasOneUse())
      return nullptr;

    // Fold (IsNeg ? -X : X) or (!IsNeg ? X : -X) to fabs(X)
    // Fold (IsNeg ? X : -X) or (!IsNeg ? -X : X) to -fabs(X)
    Value *Fabs = IC.Builder.CreateUnaryIntrinsic(Intrinsic::fabs, X, &SI);
    if (Swap != TrueIfSigned)
      return IC.replaceInstUsesWith(SI, Fabs);
    return UnaryOperator::CreateFNegFMF(Fabs, &SI);
  }

  return ChangedFMF ? &SI : nullptr;
}

// Match the following IR pattern:
//   %x.lowbits = and i8 %x, %lowbitmask
//   %x.lowbits.are.zero = icmp eq i8 %x.lowbits, 0
//   %x.biased = add i8 %x, %bias
//   %x.biased.highbits = and i8 %x.biased, %highbitmask
//   %x.roundedup = select i1 %x.lowbits.are.zero, i8 %x, i8 %x.biased.highbits
// Define:
//   %alignment = add i8 %lowbitmask, 1
// Iff 1. an %alignment is a power-of-two (aka, %lowbitmask is a low bit mask)
// and 2. %bias is equal to either %lowbitmask or %alignment,
// and 3. %highbitmask is equal to ~%lowbitmask (aka, to -%alignment)
// then this pattern can be transformed into:
//   %x.offset = add i8 %x, %lowbitmask
//   %x.roundedup = and i8 %x.offset, %highbitmask
static Value *
foldRoundUpIntegerWithPow2Alignment(SelectInst &SI,
                                    InstCombiner::BuilderTy &Builder) {
  Value *Cond = SI.getCondition();
  Value *X = SI.getTrueValue();
  Value *XBiasedHighBits = SI.getFalseValue();

  ICmpInst::Predicate Pred;
  Value *XLowBits;
  if (!match(Cond, m_ICmp(Pred, m_Value(XLowBits), m_ZeroInt())) ||
      !ICmpInst::isEquality(Pred))
    return nullptr;

  if (Pred == ICmpInst::Predicate::ICMP_NE)
    std::swap(X, XBiasedHighBits);

  // FIXME: we could support non non-splats here.

  const APInt *LowBitMaskCst;
  if (!match(XLowBits, m_And(m_Specific(X), m_APIntAllowPoison(LowBitMaskCst))))
    return nullptr;

  // Match even if the AND and ADD are swapped.
  const APInt *BiasCst, *HighBitMaskCst;
  if (!match(XBiasedHighBits,
             m_And(m_Add(m_Specific(X), m_APIntAllowPoison(BiasCst)),
                   m_APIntAllowPoison(HighBitMaskCst))) &&
      !match(XBiasedHighBits,
             m_Add(m_And(m_Specific(X), m_APIntAllowPoison(HighBitMaskCst)),
                   m_APIntAllowPoison(BiasCst))))
    return nullptr;

  if (!LowBitMaskCst->isMask())
    return nullptr;

  APInt InvertedLowBitMaskCst = ~*LowBitMaskCst;
  if (InvertedLowBitMaskCst != *HighBitMaskCst)
    return nullptr;

  APInt AlignmentCst = *LowBitMaskCst + 1;

  if (*BiasCst != AlignmentCst && *BiasCst != *LowBitMaskCst)
    return nullptr;

  if (!XBiasedHighBits->hasOneUse()) {
    // We can't directly return XBiasedHighBits if it is more poisonous.
    if (*BiasCst == *LowBitMaskCst && impliesPoison(XBiasedHighBits, X))
      return XBiasedHighBits;
    return nullptr;
  }

  // FIXME: could we preserve undef's here?
  Type *Ty = X->getType();
  Value *XOffset = Builder.CreateAdd(X, ConstantInt::get(Ty, *LowBitMaskCst),
                                     X->getName() + ".biased");
  Value *R = Builder.CreateAnd(XOffset, ConstantInt::get(Ty, *HighBitMaskCst));
  R->takeName(&SI);
  return R;
}

namespace {
struct DecomposedSelect {
  Value *Cond = nullptr;
  Value *TrueVal = nullptr;
  Value *FalseVal = nullptr;
};
} // namespace

/// Folds patterns like:
///   select c2 (select c1 a b) (select c1 b a)
/// into:
///   select (xor c1 c2) b a
static Instruction *
foldSelectOfSymmetricSelect(SelectInst &OuterSelVal,
                            InstCombiner::BuilderTy &Builder) {

  Value *OuterCond, *InnerCond, *InnerTrueVal, *InnerFalseVal;
  if (!match(
          &OuterSelVal,
          m_Select(m_Value(OuterCond),
                   m_OneUse(m_Select(m_Value(InnerCond), m_Value(InnerTrueVal),
                                     m_Value(InnerFalseVal))),
                   m_OneUse(m_Select(m_Deferred(InnerCond),
                                     m_Deferred(InnerFalseVal),
                                     m_Deferred(InnerTrueVal))))))
    return nullptr;

  if (OuterCond->getType() != InnerCond->getType())
    return nullptr;

  Value *Xor = Builder.CreateXor(InnerCond, OuterCond);
  return SelectInst::Create(Xor, InnerFalseVal, InnerTrueVal);
}

/// Look for patterns like
///   %outer.cond = select i1 %inner.cond, i1 %alt.cond, i1 false
///   %inner.sel = select i1 %inner.cond, i8 %inner.sel.t, i8 %inner.sel.f
///   %outer.sel = select i1 %outer.cond, i8 %outer.sel.t, i8 %inner.sel
/// and rewrite it as
///   %inner.sel = select i1 %cond.alternative, i8 %sel.outer.t, i8 %sel.inner.t
///   %sel.outer = select i1 %cond.inner, i8 %inner.sel, i8 %sel.inner.f
static Instruction *foldNestedSelects(SelectInst &OuterSelVal,
                                      InstCombiner::BuilderTy &Builder) {
  // We must start with a `select`.
  DecomposedSelect OuterSel;
  match(&OuterSelVal,
        m_Select(m_Value(OuterSel.Cond), m_Value(OuterSel.TrueVal),
                 m_Value(OuterSel.FalseVal)));

  // Canonicalize inversion of the outermost `select`'s condition.
  if (match(OuterSel.Cond, m_Not(m_Value(OuterSel.Cond))))
    std::swap(OuterSel.TrueVal, OuterSel.FalseVal);

  // The condition of the outermost select must be an `and`/`or`.
  if (!match(OuterSel.Cond, m_c_LogicalOp(m_Value(), m_Value())))
    return nullptr;

  // Depending on the logical op, inner select might be in different hand.
  bool IsAndVariant = match(OuterSel.Cond, m_LogicalAnd());
  Value *InnerSelVal = IsAndVariant ? OuterSel.FalseVal : OuterSel.TrueVal;

  // Profitability check - avoid increasing instruction count.
  if (none_of(ArrayRef<Value *>({OuterSelVal.getCondition(), InnerSelVal}),
              [](Value *V) { return V->hasOneUse(); }))
    return nullptr;

  // The appropriate hand of the outermost `select` must be a select itself.
  DecomposedSelect InnerSel;
  if (!match(InnerSelVal,
             m_Select(m_Value(InnerSel.Cond), m_Value(InnerSel.TrueVal),
                      m_Value(InnerSel.FalseVal))))
    return nullptr;

  // Canonicalize inversion of the innermost `select`'s condition.
  if (match(InnerSel.Cond, m_Not(m_Value(InnerSel.Cond))))
    std::swap(InnerSel.TrueVal, InnerSel.FalseVal);

  Value *AltCond = nullptr;
  auto matchOuterCond = [OuterSel, IsAndVariant, &AltCond](auto m_InnerCond) {
    // An unsimplified select condition can match both LogicalAnd and LogicalOr
    // (select true, true, false). Since below we assume that LogicalAnd implies
    // InnerSel match the FVal and vice versa for LogicalOr, we can't match the
    // alternative pattern here.
    return IsAndVariant ? match(OuterSel.Cond,
                                m_c_LogicalAnd(m_InnerCond, m_Value(AltCond)))
                        : match(OuterSel.Cond,
                                m_c_LogicalOr(m_InnerCond, m_Value(AltCond)));
  };

  // Finally, match the condition that was driving the outermost `select`,
  // it should be a logical operation between the condition that was driving
  // the innermost `select` (after accounting for the possible inversions
  // of the condition), and some other condition.
  if (matchOuterCond(m_Specific(InnerSel.Cond))) {
    // Done!
  } else if (Value * NotInnerCond; matchOuterCond(m_CombineAnd(
                 m_Not(m_Specific(InnerSel.Cond)), m_Value(NotInnerCond)))) {
    // Done!
    std::swap(InnerSel.TrueVal, InnerSel.FalseVal);
    InnerSel.Cond = NotInnerCond;
  } else // Not the pattern we were looking for.
    return nullptr;

  Value *SelInner = Builder.CreateSelect(
      AltCond, IsAndVariant ? OuterSel.TrueVal : InnerSel.FalseVal,
      IsAndVariant ? InnerSel.TrueVal : OuterSel.FalseVal);
  SelInner->takeName(InnerSelVal);
  return SelectInst::Create(InnerSel.Cond,
                            IsAndVariant ? SelInner : InnerSel.TrueVal,
                            !IsAndVariant ? SelInner : InnerSel.FalseVal);
}

Instruction *InstCombinerImpl::foldSelectOfBools(SelectInst &SI) {
  Value *CondVal = SI.getCondition();
  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();
  Type *SelType = SI.getType();

  // Avoid potential infinite loops by checking for non-constant condition.
  // TODO: Can we assert instead by improving canonicalizeSelectToShuffle()?
  //       Scalar select must have simplified?
  if (!SelType->isIntOrIntVectorTy(1) || isa<Constant>(CondVal) ||
      TrueVal->getType() != CondVal->getType())
    return nullptr;

  auto *One = ConstantInt::getTrue(SelType);
  auto *Zero = ConstantInt::getFalse(SelType);
  Value *A, *B, *C, *D;

  // Folding select to and/or i1 isn't poison safe in general. impliesPoison
  // checks whether folding it does not convert a well-defined value into
  // poison.
  if (match(TrueVal, m_One())) {
    if (impliesPoison(FalseVal, CondVal)) {
      // Change: A = select B, true, C --> A = or B, C
      return BinaryOperator::CreateOr(CondVal, FalseVal);
    }

    if (match(CondVal, m_OneUse(m_Select(m_Value(A), m_One(), m_Value(B)))) &&
        impliesPoison(FalseVal, B)) {
      // (A || B) || C --> A || (B | C)
      return replaceInstUsesWith(
          SI, Builder.CreateLogicalOr(A, Builder.CreateOr(B, FalseVal)));
    }

    if (auto *LHS = dyn_cast<FCmpInst>(CondVal))
      if (auto *RHS = dyn_cast<FCmpInst>(FalseVal))
        if (Value *V = foldLogicOfFCmps(LHS, RHS, /*IsAnd*/ false,
                                        /*IsSelectLogical*/ true))
          return replaceInstUsesWith(SI, V);

    // (A && B) || (C && B) --> (A || C) && B
    if (match(CondVal, m_LogicalAnd(m_Value(A), m_Value(B))) &&
        match(FalseVal, m_LogicalAnd(m_Value(C), m_Value(D))) &&
        (CondVal->hasOneUse() || FalseVal->hasOneUse())) {
      bool CondLogicAnd = isa<SelectInst>(CondVal);
      bool FalseLogicAnd = isa<SelectInst>(FalseVal);
      auto AndFactorization = [&](Value *Common, Value *InnerCond,
                                  Value *InnerVal,
                                  bool SelFirst = false) -> Instruction * {
        Value *InnerSel = Builder.CreateSelect(InnerCond, One, InnerVal);
        if (SelFirst)
          std::swap(Common, InnerSel);
        if (FalseLogicAnd || (CondLogicAnd && Common == A))
          return SelectInst::Create(Common, InnerSel, Zero);
        else
          return BinaryOperator::CreateAnd(Common, InnerSel);
      };

      if (A == C)
        return AndFactorization(A, B, D);
      if (A == D)
        return AndFactorization(A, B, C);
      if (B == C)
        return AndFactorization(B, A, D);
      if (B == D)
        return AndFactorization(B, A, C, CondLogicAnd && FalseLogicAnd);
    }
  }

  if (match(FalseVal, m_Zero())) {
    if (impliesPoison(TrueVal, CondVal)) {
      // Change: A = select B, C, false --> A = and B, C
      return BinaryOperator::CreateAnd(CondVal, TrueVal);
    }

    if (match(CondVal, m_OneUse(m_Select(m_Value(A), m_Value(B), m_Zero()))) &&
        impliesPoison(TrueVal, B)) {
      // (A && B) && C --> A && (B & C)
      return replaceInstUsesWith(
          SI, Builder.CreateLogicalAnd(A, Builder.CreateAnd(B, TrueVal)));
    }

    if (auto *LHS = dyn_cast<FCmpInst>(CondVal))
      if (auto *RHS = dyn_cast<FCmpInst>(TrueVal))
        if (Value *V = foldLogicOfFCmps(LHS, RHS, /*IsAnd*/ true,
                                        /*IsSelectLogical*/ true))
          return replaceInstUsesWith(SI, V);

    // (A || B) && (C || B) --> (A && C) || B
    if (match(CondVal, m_LogicalOr(m_Value(A), m_Value(B))) &&
        match(TrueVal, m_LogicalOr(m_Value(C), m_Value(D))) &&
        (CondVal->hasOneUse() || TrueVal->hasOneUse())) {
      bool CondLogicOr = isa<SelectInst>(CondVal);
      bool TrueLogicOr = isa<SelectInst>(TrueVal);
      auto OrFactorization = [&](Value *Common, Value *InnerCond,
                                 Value *InnerVal,
                                 bool SelFirst = false) -> Instruction * {
        Value *InnerSel = Builder.CreateSelect(InnerCond, InnerVal, Zero);
        if (SelFirst)
          std::swap(Common, InnerSel);
        if (TrueLogicOr || (CondLogicOr && Common == A))
          return SelectInst::Create(Common, One, InnerSel);
        else
          return BinaryOperator::CreateOr(Common, InnerSel);
      };

      if (A == C)
        return OrFactorization(A, B, D);
      if (A == D)
        return OrFactorization(A, B, C);
      if (B == C)
        return OrFactorization(B, A, D);
      if (B == D)
        return OrFactorization(B, A, C, CondLogicOr && TrueLogicOr);
    }
  }

  // We match the "full" 0 or 1 constant here to avoid a potential infinite
  // loop with vectors that may have undefined/poison elements.
  // select a, false, b -> select !a, b, false
  if (match(TrueVal, m_Specific(Zero))) {
    Value *NotCond = Builder.CreateNot(CondVal, "not." + CondVal->getName());
    return SelectInst::Create(NotCond, FalseVal, Zero);
  }
  // select a, b, true -> select !a, true, b
  if (match(FalseVal, m_Specific(One))) {
    Value *NotCond = Builder.CreateNot(CondVal, "not." + CondVal->getName());
    return SelectInst::Create(NotCond, One, TrueVal);
  }

  // DeMorgan in select form: !a && !b --> !(a || b)
  // select !a, !b, false --> not (select a, true, b)
  if (match(&SI, m_LogicalAnd(m_Not(m_Value(A)), m_Not(m_Value(B)))) &&
      (CondVal->hasOneUse() || TrueVal->hasOneUse()) &&
      !match(A, m_ConstantExpr()) && !match(B, m_ConstantExpr()))
    return BinaryOperator::CreateNot(Builder.CreateSelect(A, One, B));

  // DeMorgan in select form: !a || !b --> !(a && b)
  // select !a, true, !b --> not (select a, b, false)
  if (match(&SI, m_LogicalOr(m_Not(m_Value(A)), m_Not(m_Value(B)))) &&
      (CondVal->hasOneUse() || FalseVal->hasOneUse()) &&
      !match(A, m_ConstantExpr()) && !match(B, m_ConstantExpr()))
    return BinaryOperator::CreateNot(Builder.CreateSelect(A, B, Zero));

  // select (select a, true, b), true, b -> select a, true, b
  if (match(CondVal, m_Select(m_Value(A), m_One(), m_Value(B))) &&
      match(TrueVal, m_One()) && match(FalseVal, m_Specific(B)))
    return replaceOperand(SI, 0, A);
  // select (select a, b, false), b, false -> select a, b, false
  if (match(CondVal, m_Select(m_Value(A), m_Value(B), m_Zero())) &&
      match(TrueVal, m_Specific(B)) && match(FalseVal, m_Zero()))
    return replaceOperand(SI, 0, A);

  // ~(A & B) & (A | B) --> A ^ B
  if (match(&SI, m_c_LogicalAnd(m_Not(m_LogicalAnd(m_Value(A), m_Value(B))),
                                m_c_LogicalOr(m_Deferred(A), m_Deferred(B)))))
    return BinaryOperator::CreateXor(A, B);

  // select (~a | c), a, b -> select a, (select c, true, b), false
  if (match(CondVal,
            m_OneUse(m_c_Or(m_Not(m_Specific(TrueVal)), m_Value(C))))) {
    Value *OrV = Builder.CreateSelect(C, One, FalseVal);
    return SelectInst::Create(TrueVal, OrV, Zero);
  }
  // select (c & b), a, b -> select b, (select ~c, true, a), false
  if (match(CondVal, m_OneUse(m_c_And(m_Value(C), m_Specific(FalseVal))))) {
    if (Value *NotC = getFreelyInverted(C, C->hasOneUse(), &Builder)) {
      Value *OrV = Builder.CreateSelect(NotC, One, TrueVal);
      return SelectInst::Create(FalseVal, OrV, Zero);
    }
  }
  // select (a | c), a, b -> select a, true, (select ~c, b, false)
  if (match(CondVal, m_OneUse(m_c_Or(m_Specific(TrueVal), m_Value(C))))) {
    if (Value *NotC = getFreelyInverted(C, C->hasOneUse(), &Builder)) {
      Value *AndV = Builder.CreateSelect(NotC, FalseVal, Zero);
      return SelectInst::Create(TrueVal, One, AndV);
    }
  }
  // select (c & ~b), a, b -> select b, true, (select c, a, false)
  if (match(CondVal,
            m_OneUse(m_c_And(m_Value(C), m_Not(m_Specific(FalseVal)))))) {
    Value *AndV = Builder.CreateSelect(C, TrueVal, Zero);
    return SelectInst::Create(FalseVal, One, AndV);
  }

  if (match(FalseVal, m_Zero()) || match(TrueVal, m_One())) {
    Use *Y = nullptr;
    bool IsAnd = match(FalseVal, m_Zero()) ? true : false;
    Value *Op1 = IsAnd ? TrueVal : FalseVal;
    if (isCheckForZeroAndMulWithOverflow(CondVal, Op1, IsAnd, Y)) {
      auto *FI = new FreezeInst(*Y, (*Y)->getName() + ".fr");
      InsertNewInstBefore(FI, cast<Instruction>(Y->getUser())->getIterator());
      replaceUse(*Y, FI);
      return replaceInstUsesWith(SI, Op1);
    }

    if (auto *ICmp0 = dyn_cast<ICmpInst>(CondVal))
      if (auto *ICmp1 = dyn_cast<ICmpInst>(Op1))
        if (auto *V = foldAndOrOfICmps(ICmp0, ICmp1, SI, IsAnd,
                                       /* IsLogical */ true))
          return replaceInstUsesWith(SI, V);
  }

  // select (a || b), c, false -> select a, c, false
  // select c, (a || b), false -> select c, a, false
  //   if c implies that b is false.
  if (match(CondVal, m_LogicalOr(m_Value(A), m_Value(B))) &&
      match(FalseVal, m_Zero())) {
    std::optional<bool> Res = isImpliedCondition(TrueVal, B, DL);
    if (Res && *Res == false)
      return replaceOperand(SI, 0, A);
  }
  if (match(TrueVal, m_LogicalOr(m_Value(A), m_Value(B))) &&
      match(FalseVal, m_Zero())) {
    std::optional<bool> Res = isImpliedCondition(CondVal, B, DL);
    if (Res && *Res == false)
      return replaceOperand(SI, 1, A);
  }
  // select c, true, (a && b)  -> select c, true, a
  // select (a && b), true, c  -> select a, true, c
  //   if c = false implies that b = true
  if (match(TrueVal, m_One()) &&
      match(FalseVal, m_LogicalAnd(m_Value(A), m_Value(B)))) {
    std::optional<bool> Res = isImpliedCondition(CondVal, B, DL, false);
    if (Res && *Res == true)
      return replaceOperand(SI, 2, A);
  }
  if (match(CondVal, m_LogicalAnd(m_Value(A), m_Value(B))) &&
      match(TrueVal, m_One())) {
    std::optional<bool> Res = isImpliedCondition(FalseVal, B, DL, false);
    if (Res && *Res == true)
      return replaceOperand(SI, 0, A);
  }

  if (match(TrueVal, m_One())) {
    Value *C;

    // (C && A) || (!C && B) --> sel C, A, B
    // (A && C) || (!C && B) --> sel C, A, B
    // (C && A) || (B && !C) --> sel C, A, B
    // (A && C) || (B && !C) --> sel C, A, B (may require freeze)
    if (match(FalseVal, m_c_LogicalAnd(m_Not(m_Value(C)), m_Value(B))) &&
        match(CondVal, m_c_LogicalAnd(m_Specific(C), m_Value(A)))) {
      auto *SelCond = dyn_cast<SelectInst>(CondVal);
      auto *SelFVal = dyn_cast<SelectInst>(FalseVal);
      bool MayNeedFreeze = SelCond && SelFVal &&
                           match(SelFVal->getTrueValue(),
                                 m_Not(m_Specific(SelCond->getTrueValue())));
      if (MayNeedFreeze)
        C = Builder.CreateFreeze(C);
      return SelectInst::Create(C, A, B);
    }

    // (!C && A) || (C && B) --> sel C, B, A
    // (A && !C) || (C && B) --> sel C, B, A
    // (!C && A) || (B && C) --> sel C, B, A
    // (A && !C) || (B && C) --> sel C, B, A (may require freeze)
    if (match(CondVal, m_c_LogicalAnd(m_Not(m_Value(C)), m_Value(A))) &&
        match(FalseVal, m_c_LogicalAnd(m_Specific(C), m_Value(B)))) {
      auto *SelCond = dyn_cast<SelectInst>(CondVal);
      auto *SelFVal = dyn_cast<SelectInst>(FalseVal);
      bool MayNeedFreeze = SelCond && SelFVal &&
                           match(SelCond->getTrueValue(),
                                 m_Not(m_Specific(SelFVal->getTrueValue())));
      if (MayNeedFreeze)
        C = Builder.CreateFreeze(C);
      return SelectInst::Create(C, B, A);
    }
  }

  return nullptr;
}

// Return true if we can safely remove the select instruction for std::bit_ceil
// pattern.
static bool isSafeToRemoveBitCeilSelect(ICmpInst::Predicate Pred, Value *Cond0,
                                        const APInt *Cond1, Value *CtlzOp,
                                        unsigned BitWidth,
                                        bool &ShouldDropNUW) {
  // The challenge in recognizing std::bit_ceil(X) is that the operand is used
  // for the CTLZ proper and select condition, each possibly with some
  // operation like add and sub.
  //
  // Our aim is to make sure that -ctlz & (BitWidth - 1) == 0 even when the
  // select instruction would select 1, which allows us to get rid of the select
  // instruction.
  //
  // To see if we can do so, we do some symbolic execution with ConstantRange.
  // Specifically, we compute the range of values that Cond0 could take when
  // Cond == false.  Then we successively transform the range until we obtain
  // the range of values that CtlzOp could take.
  //
  // Conceptually, we follow the def-use chain backward from Cond0 while
  // transforming the range for Cond0 until we meet the common ancestor of Cond0
  // and CtlzOp.  Then we follow the def-use chain forward until we obtain the
  // range for CtlzOp.  That said, we only follow at most one ancestor from
  // Cond0.  Likewise, we only follow at most one ancestor from CtrlOp.

  ConstantRange CR = ConstantRange::makeExactICmpRegion(
      CmpInst::getInversePredicate(Pred), *Cond1);

  ShouldDropNUW = false;

  // Match the operation that's used to compute CtlzOp from CommonAncestor.  If
  // CtlzOp == CommonAncestor, return true as no operation is needed.  If a
  // match is found, execute the operation on CR, update CR, and return true.
  // Otherwise, return false.
  auto MatchForward = [&](Value *CommonAncestor) {
    const APInt *C = nullptr;
    if (CtlzOp == CommonAncestor)
      return true;
    if (match(CtlzOp, m_Add(m_Specific(CommonAncestor), m_APInt(C)))) {
      CR = CR.add(*C);
      return true;
    }
    if (match(CtlzOp, m_Sub(m_APInt(C), m_Specific(CommonAncestor)))) {
      ShouldDropNUW = true;
      CR = ConstantRange(*C).sub(CR);
      return true;
    }
    if (match(CtlzOp, m_Not(m_Specific(CommonAncestor)))) {
      CR = CR.binaryNot();
      return true;
    }
    return false;
  };

  const APInt *C = nullptr;
  Value *CommonAncestor;
  if (MatchForward(Cond0)) {
    // Cond0 is either CtlzOp or CtlzOp's parent.  CR has been updated.
  } else if (match(Cond0, m_Add(m_Value(CommonAncestor), m_APInt(C)))) {
    CR = CR.sub(*C);
    if (!MatchForward(CommonAncestor))
      return false;
    // Cond0's parent is either CtlzOp or CtlzOp's parent.  CR has been updated.
  } else {
    return false;
  }

  // Return true if all the values in the range are either 0 or negative (if
  // treated as signed).  We do so by evaluating:
  //
  //   CR - 1 u>= (1 << BitWidth) - 1.
  APInt IntMax = APInt::getSignMask(BitWidth) - 1;
  CR = CR.sub(APInt(BitWidth, 1));
  return CR.icmp(ICmpInst::ICMP_UGE, IntMax);
}

// Transform the std::bit_ceil(X) pattern like:
//
//   %dec = add i32 %x, -1
//   %ctlz = tail call i32 @llvm.ctlz.i32(i32 %dec, i1 false)
//   %sub = sub i32 32, %ctlz
//   %shl = shl i32 1, %sub
//   %ugt = icmp ugt i32 %x, 1
//   %sel = select i1 %ugt, i32 %shl, i32 1
//
// into:
//
//   %dec = add i32 %x, -1
//   %ctlz = tail call i32 @llvm.ctlz.i32(i32 %dec, i1 false)
//   %neg = sub i32 0, %ctlz
//   %masked = and i32 %ctlz, 31
//   %shl = shl i32 1, %sub
//
// Note that the select is optimized away while the shift count is masked with
// 31.  We handle some variations of the input operand like std::bit_ceil(X +
// 1).
static Instruction *foldBitCeil(SelectInst &SI, IRBuilderBase &Builder) {
  Type *SelType = SI.getType();
  unsigned BitWidth = SelType->getScalarSizeInBits();

  Value *FalseVal = SI.getFalseValue();
  Value *TrueVal = SI.getTrueValue();
  ICmpInst::Predicate Pred;
  const APInt *Cond1;
  Value *Cond0, *Ctlz, *CtlzOp;
  if (!match(SI.getCondition(), m_ICmp(Pred, m_Value(Cond0), m_APInt(Cond1))))
    return nullptr;

  if (match(TrueVal, m_One())) {
    std::swap(FalseVal, TrueVal);
    Pred = CmpInst::getInversePredicate(Pred);
  }

  bool ShouldDropNUW;

  if (!match(FalseVal, m_One()) ||
      !match(TrueVal,
             m_OneUse(m_Shl(m_One(), m_OneUse(m_Sub(m_SpecificInt(BitWidth),
                                                    m_Value(Ctlz)))))) ||
      !match(Ctlz, m_Intrinsic<Intrinsic::ctlz>(m_Value(CtlzOp), m_Zero())) ||
      !isSafeToRemoveBitCeilSelect(Pred, Cond0, Cond1, CtlzOp, BitWidth,
                                   ShouldDropNUW))
    return nullptr;

  if (ShouldDropNUW)
    cast<Instruction>(CtlzOp)->setHasNoUnsignedWrap(false);

  // Build 1 << (-CTLZ & (BitWidth-1)).  The negation likely corresponds to a
  // single hardware instruction as opposed to BitWidth - CTLZ, where BitWidth
  // is an integer constant.  Masking with BitWidth-1 comes free on some
  // hardware as part of the shift instruction.
  Value *Neg = Builder.CreateNeg(Ctlz);
  Value *Masked =
      Builder.CreateAnd(Neg, ConstantInt::get(SelType, BitWidth - 1));
  return BinaryOperator::Create(Instruction::Shl, ConstantInt::get(SelType, 1),
                                Masked);
}

bool InstCombinerImpl::fmulByZeroIsZero(Value *MulVal, FastMathFlags FMF,
                                        const Instruction *CtxI) const {
  KnownFPClass Known = computeKnownFPClass(MulVal, FMF, fcNegative, CtxI);

  return Known.isKnownNeverNaN() && Known.isKnownNeverInfinity() &&
         (FMF.noSignedZeros() || Known.signBitIsZeroOrNaN());
}

static bool matchFMulByZeroIfResultEqZero(InstCombinerImpl &IC, Value *Cmp0,
                                          Value *Cmp1, Value *TrueVal,
                                          Value *FalseVal, Instruction &CtxI,
                                          bool SelectIsNSZ) {
  Value *MulRHS;
  if (match(Cmp1, m_PosZeroFP()) &&
      match(TrueVal, m_c_FMul(m_Specific(Cmp0), m_Value(MulRHS)))) {
    FastMathFlags FMF = cast<FPMathOperator>(TrueVal)->getFastMathFlags();
    // nsz must be on the select, it must be ignored on the multiply. We
    // need nnan and ninf on the multiply for the other value.
    FMF.setNoSignedZeros(SelectIsNSZ);
    return IC.fmulByZeroIsZero(MulRHS, FMF, &CtxI);
  }

  return false;
}

/// Check whether the KnownBits of a select arm may be affected by the
/// select condition.
static bool hasAffectedValue(Value *V, SmallPtrSetImpl<Value *> &Affected,
                             unsigned Depth) {
  if (Depth == MaxAnalysisRecursionDepth)
    return false;

  // Ignore the case where the select arm itself is affected. These cases
  // are handled more efficiently by other optimizations.
  if (Depth != 0 && Affected.contains(V))
    return true;

  if (auto *I = dyn_cast<Instruction>(V)) {
    if (isa<PHINode>(I)) {
      if (Depth == MaxAnalysisRecursionDepth - 1)
        return false;
      Depth = MaxAnalysisRecursionDepth - 2;
    }
    return any_of(I->operands(), [&](Value *Op) {
      return Op->getType()->isIntOrIntVectorTy() &&
             hasAffectedValue(Op, Affected, Depth + 1);
    });
  }

  return false;
}

Instruction *InstCombinerImpl::visitSelectInst(SelectInst &SI) {
  Value *CondVal = SI.getCondition();
  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();
  Type *SelType = SI.getType();

  if (Value *V = simplifySelectInst(CondVal, TrueVal, FalseVal,
                                    SQ.getWithInstruction(&SI)))
    return replaceInstUsesWith(SI, V);

  if (Instruction *I = canonicalizeSelectToShuffle(SI))
    return I;

  if (Instruction *I = canonicalizeScalarSelectOfVecs(SI, *this))
    return I;

  // If the type of select is not an integer type or if the condition and
  // the selection type are not both scalar nor both vector types, there is no
  // point in attempting to match these patterns.
  Type *CondType = CondVal->getType();
  if (!isa<Constant>(CondVal) && SelType->isIntOrIntVectorTy() &&
      CondType->isVectorTy() == SelType->isVectorTy()) {
    if (Value *S = simplifyWithOpReplaced(TrueVal, CondVal,
                                          ConstantInt::getTrue(CondType), SQ,
                                          /* AllowRefinement */ true))
      return replaceOperand(SI, 1, S);

    if (Value *S = simplifyWithOpReplaced(FalseVal, CondVal,
                                          ConstantInt::getFalse(CondType), SQ,
                                          /* AllowRefinement */ true))
      return replaceOperand(SI, 2, S);
  }

  if (Instruction *R = foldSelectOfBools(SI))
    return R;

  // Selecting between two integer or vector splat integer constants?
  //
  // Note that we don't handle a scalar select of vectors:
  // select i1 %c, <2 x i8> <1, 1>, <2 x i8> <0, 0>
  // because that may need 3 instructions to splat the condition value:
  // extend, insertelement, shufflevector.
  //
  // Do not handle i1 TrueVal and FalseVal otherwise would result in
  // zext/sext i1 to i1.
  if (SelType->isIntOrIntVectorTy() && !SelType->isIntOrIntVectorTy(1) &&
      CondVal->getType()->isVectorTy() == SelType->isVectorTy()) {
    // select C, 1, 0 -> zext C to int
    if (match(TrueVal, m_One()) && match(FalseVal, m_Zero()))
      return new ZExtInst(CondVal, SelType);

    // select C, -1, 0 -> sext C to int
    if (match(TrueVal, m_AllOnes()) && match(FalseVal, m_Zero()))
      return new SExtInst(CondVal, SelType);

    // select C, 0, 1 -> zext !C to int
    if (match(TrueVal, m_Zero()) && match(FalseVal, m_One())) {
      Value *NotCond = Builder.CreateNot(CondVal, "not." + CondVal->getName());
      return new ZExtInst(NotCond, SelType);
    }

    // select C, 0, -1 -> sext !C to int
    if (match(TrueVal, m_Zero()) && match(FalseVal, m_AllOnes())) {
      Value *NotCond = Builder.CreateNot(CondVal, "not." + CondVal->getName());
      return new SExtInst(NotCond, SelType);
    }
  }

  auto *SIFPOp = dyn_cast<FPMathOperator>(&SI);

  if (auto *FCmp = dyn_cast<FCmpInst>(CondVal)) {
    FCmpInst::Predicate Pred = FCmp->getPredicate();
    Value *Cmp0 = FCmp->getOperand(0), *Cmp1 = FCmp->getOperand(1);
    // Are we selecting a value based on a comparison of the two values?
    if ((Cmp0 == TrueVal && Cmp1 == FalseVal) ||
        (Cmp0 == FalseVal && Cmp1 == TrueVal)) {
      // Canonicalize to use ordered comparisons by swapping the select
      // operands.
      //
      // e.g.
      // (X ugt Y) ? X : Y -> (X ole Y) ? Y : X
      if (FCmp->hasOneUse() && FCmpInst::isUnordered(Pred)) {
        FCmpInst::Predicate InvPred = FCmp->getInversePredicate();
        IRBuilder<>::FastMathFlagGuard FMFG(Builder);
        // FIXME: The FMF should propagate from the select, not the fcmp.
        Builder.setFastMathFlags(FCmp->getFastMathFlags());
        Value *NewCond = Builder.CreateFCmp(InvPred, Cmp0, Cmp1,
                                            FCmp->getName() + ".inv");
        Value *NewSel = Builder.CreateSelect(NewCond, FalseVal, TrueVal);
        return replaceInstUsesWith(SI, NewSel);
      }
    }

    if (SIFPOp) {
      // Fold out scale-if-equals-zero pattern.
      //
      // This pattern appears in code with denormal range checks after it's
      // assumed denormals are treated as zero. This drops a canonicalization.

      // TODO: Could relax the signed zero logic. We just need to know the sign
      // of the result matches (fmul x, y has the same sign as x).
      //
      // TODO: Handle always-canonicalizing variant that selects some value or 1
      // scaling factor in the fmul visitor.

      // TODO: Handle ldexp too

      Value *MatchCmp0 = nullptr;
      Value *MatchCmp1 = nullptr;

      // (select (fcmp [ou]eq x, 0.0), (fmul x, K), x => x
      // (select (fcmp [ou]ne x, 0.0), x, (fmul x, K) => x
      if (Pred == CmpInst::FCMP_OEQ || Pred == CmpInst::FCMP_UEQ) {
        MatchCmp0 = FalseVal;
        MatchCmp1 = TrueVal;
      } else if (Pred == CmpInst::FCMP_ONE || Pred == CmpInst::FCMP_UNE) {
        MatchCmp0 = TrueVal;
        MatchCmp1 = FalseVal;
      }

      if (Cmp0 == MatchCmp0 &&
          matchFMulByZeroIfResultEqZero(*this, Cmp0, Cmp1, MatchCmp1, MatchCmp0,
                                        SI, SIFPOp->hasNoSignedZeros()))
        return replaceInstUsesWith(SI, Cmp0);
    }
  }

  if (SIFPOp) {
    // TODO: Try to forward-propagate FMF from select arms to the select.

    // Canonicalize select of FP values where NaN and -0.0 are not valid as
    // minnum/maxnum intrinsics.
    if (SIFPOp->hasNoNaNs() && SIFPOp->hasNoSignedZeros()) {
      Value *X, *Y;
      if (match(&SI, m_OrdFMax(m_Value(X), m_Value(Y))))
        return replaceInstUsesWith(
            SI, Builder.CreateBinaryIntrinsic(Intrinsic::maxnum, X, Y, &SI));

      if (match(&SI, m_OrdFMin(m_Value(X), m_Value(Y))))
        return replaceInstUsesWith(
            SI, Builder.CreateBinaryIntrinsic(Intrinsic::minnum, X, Y, &SI));
    }
  }

  // Fold selecting to fabs.
  if (Instruction *Fabs = foldSelectWithFCmpToFabs(SI, *this))
    return Fabs;

  // See if we are selecting two values based on a comparison of the two values.
  if (ICmpInst *ICI = dyn_cast<ICmpInst>(CondVal))
    if (Instruction *Result = foldSelectInstWithICmp(SI, ICI))
      return Result;

  if (Instruction *Add = foldAddSubSelect(SI, Builder))
    return Add;
  if (Instruction *Add = foldOverflowingAddSubSelect(SI, Builder))
    return Add;
  if (Instruction *Or = foldSetClearBits(SI, Builder))
    return Or;
  if (Instruction *Mul = foldSelectZeroOrMul(SI, *this))
    return Mul;

  // Turn (select C, (op X, Y), (op X, Z)) -> (op X, (select C, Y, Z))
  auto *TI = dyn_cast<Instruction>(TrueVal);
  auto *FI = dyn_cast<Instruction>(FalseVal);
  if (TI && FI && TI->getOpcode() == FI->getOpcode())
    if (Instruction *IV = foldSelectOpOp(SI, TI, FI))
      return IV;

  if (Instruction *I = foldSelectExtConst(SI))
    return I;

  if (Instruction *I = foldSelectWithSRem(SI, *this, Builder))
    return I;

  // Fold (select C, (gep Ptr, Idx), Ptr) -> (gep Ptr, (select C, Idx, 0))
  // Fold (select C, Ptr, (gep Ptr, Idx)) -> (gep Ptr, (select C, 0, Idx))
  auto SelectGepWithBase = [&](GetElementPtrInst *Gep, Value *Base,
                               bool Swap) -> GetElementPtrInst * {
    Value *Ptr = Gep->getPointerOperand();
    if (Gep->getNumOperands() != 2 || Gep->getPointerOperand() != Base ||
        !Gep->hasOneUse())
      return nullptr;
    Value *Idx = Gep->getOperand(1);
    if (isa<VectorType>(CondVal->getType()) && !isa<VectorType>(Idx->getType()))
      return nullptr;
    Type *ElementType = Gep->getSourceElementType();
    Value *NewT = Idx;
    Value *NewF = Constant::getNullValue(Idx->getType());
    if (Swap)
      std::swap(NewT, NewF);
    Value *NewSI =
        Builder.CreateSelect(CondVal, NewT, NewF, SI.getName() + ".idx", &SI);
    return GetElementPtrInst::Create(ElementType, Ptr, NewSI,
                                     Gep->getNoWrapFlags());
  };
  if (auto *TrueGep = dyn_cast<GetElementPtrInst>(TrueVal))
    if (auto *NewGep = SelectGepWithBase(TrueGep, FalseVal, false))
      return NewGep;
  if (auto *FalseGep = dyn_cast<GetElementPtrInst>(FalseVal))
    if (auto *NewGep = SelectGepWithBase(FalseGep, TrueVal, true))
      return NewGep;

  // See if we can fold the select into one of our operands.
  if (SelType->isIntOrIntVectorTy() || SelType->isFPOrFPVectorTy()) {
    if (Instruction *FoldI = foldSelectIntoOp(SI, TrueVal, FalseVal))
      return FoldI;

    Value *LHS, *RHS;
    Instruction::CastOps CastOp;
    SelectPatternResult SPR = matchSelectPattern(&SI, LHS, RHS, &CastOp);
    auto SPF = SPR.Flavor;
    if (SPF) {
      Value *LHS2, *RHS2;
      if (SelectPatternFlavor SPF2 = matchSelectPattern(LHS, LHS2, RHS2).Flavor)
        if (Instruction *R = foldSPFofSPF(cast<Instruction>(LHS), SPF2, LHS2,
                                          RHS2, SI, SPF, RHS))
          return R;
      if (SelectPatternFlavor SPF2 = matchSelectPattern(RHS, LHS2, RHS2).Flavor)
        if (Instruction *R = foldSPFofSPF(cast<Instruction>(RHS), SPF2, LHS2,
                                          RHS2, SI, SPF, LHS))
          return R;
    }

    if (SelectPatternResult::isMinOrMax(SPF)) {
      // Canonicalize so that
      // - type casts are outside select patterns.
      // - float clamp is transformed to min/max pattern

      bool IsCastNeeded = LHS->getType() != SelType;
      Value *CmpLHS = cast<CmpInst>(CondVal)->getOperand(0);
      Value *CmpRHS = cast<CmpInst>(CondVal)->getOperand(1);
      if (IsCastNeeded ||
          (LHS->getType()->isFPOrFPVectorTy() &&
           ((CmpLHS != LHS && CmpLHS != RHS) ||
            (CmpRHS != LHS && CmpRHS != RHS)))) {
        CmpInst::Predicate MinMaxPred = getMinMaxPred(SPF, SPR.Ordered);

        Value *Cmp;
        if (CmpInst::isIntPredicate(MinMaxPred)) {
          Cmp = Builder.CreateICmp(MinMaxPred, LHS, RHS);
        } else {
          IRBuilder<>::FastMathFlagGuard FMFG(Builder);
          auto FMF =
              cast<FPMathOperator>(SI.getCondition())->getFastMathFlags();
          Builder.setFastMathFlags(FMF);
          Cmp = Builder.CreateFCmp(MinMaxPred, LHS, RHS);
        }

        Value *NewSI = Builder.CreateSelect(Cmp, LHS, RHS, SI.getName(), &SI);
        if (!IsCastNeeded)
          return replaceInstUsesWith(SI, NewSI);

        Value *NewCast = Builder.CreateCast(CastOp, NewSI, SelType);
        return replaceInstUsesWith(SI, NewCast);
      }
    }
  }

  // See if we can fold the select into a phi node if the condition is a select.
  if (auto *PN = dyn_cast<PHINode>(SI.getCondition()))
    // The true/false values have to be live in the PHI predecessor's blocks.
    if (canSelectOperandBeMappingIntoPredBlock(TrueVal, SI) &&
        canSelectOperandBeMappingIntoPredBlock(FalseVal, SI))
      if (Instruction *NV = foldOpIntoPhi(SI, PN))
        return NV;

  if (SelectInst *TrueSI = dyn_cast<SelectInst>(TrueVal)) {
    if (TrueSI->getCondition()->getType() == CondVal->getType()) {
      // Fold nested selects if the inner condition can be implied by the outer
      // condition.
      if (Value *V = simplifyNestedSelectsUsingImpliedCond(
              *TrueSI, CondVal, /*CondIsTrue=*/true, DL))
        return replaceOperand(SI, 1, V);

      // select(C0, select(C1, a, b), b) -> select(C0&C1, a, b)
      // We choose this as normal form to enable folding on the And and
      // shortening paths for the values (this helps getUnderlyingObjects() for
      // example).
      if (TrueSI->getFalseValue() == FalseVal && TrueSI->hasOneUse()) {
        Value *And = Builder.CreateLogicalAnd(CondVal, TrueSI->getCondition());
        replaceOperand(SI, 0, And);
        replaceOperand(SI, 1, TrueSI->getTrueValue());
        return &SI;
      }
    }
  }
  if (SelectInst *FalseSI = dyn_cast<SelectInst>(FalseVal)) {
    if (FalseSI->getCondition()->getType() == CondVal->getType()) {
      // Fold nested selects if the inner condition can be implied by the outer
      // condition.
      if (Value *V = simplifyNestedSelectsUsingImpliedCond(
              *FalseSI, CondVal, /*CondIsTrue=*/false, DL))
        return replaceOperand(SI, 2, V);

      // select(C0, a, select(C1, a, b)) -> select(C0|C1, a, b)
      if (FalseSI->getTrueValue() == TrueVal && FalseSI->hasOneUse()) {
        Value *Or = Builder.CreateLogicalOr(CondVal, FalseSI->getCondition());
        replaceOperand(SI, 0, Or);
        replaceOperand(SI, 2, FalseSI->getFalseValue());
        return &SI;
      }
    }
  }

  // Try to simplify a binop sandwiched between 2 selects with the same
  // condition. This is not valid for div/rem because the select might be
  // preventing a division-by-zero.
  // TODO: A div/rem restriction is conservative; use something like
  //       isSafeToSpeculativelyExecute().
  // select(C, binop(select(C, X, Y), W), Z) -> select(C, binop(X, W), Z)
  BinaryOperator *TrueBO;
  if (match(TrueVal, m_OneUse(m_BinOp(TrueBO))) && !TrueBO->isIntDivRem()) {
    if (auto *TrueBOSI = dyn_cast<SelectInst>(TrueBO->getOperand(0))) {
      if (TrueBOSI->getCondition() == CondVal) {
        replaceOperand(*TrueBO, 0, TrueBOSI->getTrueValue());
        Worklist.push(TrueBO);
        return &SI;
      }
    }
    if (auto *TrueBOSI = dyn_cast<SelectInst>(TrueBO->getOperand(1))) {
      if (TrueBOSI->getCondition() == CondVal) {
        replaceOperand(*TrueBO, 1, TrueBOSI->getTrueValue());
        Worklist.push(TrueBO);
        return &SI;
      }
    }
  }

  // select(C, Z, binop(select(C, X, Y), W)) -> select(C, Z, binop(Y, W))
  BinaryOperator *FalseBO;
  if (match(FalseVal, m_OneUse(m_BinOp(FalseBO))) && !FalseBO->isIntDivRem()) {
    if (auto *FalseBOSI = dyn_cast<SelectInst>(FalseBO->getOperand(0))) {
      if (FalseBOSI->getCondition() == CondVal) {
        replaceOperand(*FalseBO, 0, FalseBOSI->getFalseValue());
        Worklist.push(FalseBO);
        return &SI;
      }
    }
    if (auto *FalseBOSI = dyn_cast<SelectInst>(FalseBO->getOperand(1))) {
      if (FalseBOSI->getCondition() == CondVal) {
        replaceOperand(*FalseBO, 1, FalseBOSI->getFalseValue());
        Worklist.push(FalseBO);
        return &SI;
      }
    }
  }

  Value *NotCond;
  if (match(CondVal, m_Not(m_Value(NotCond))) &&
      !InstCombiner::shouldAvoidAbsorbingNotIntoSelect(SI)) {
    replaceOperand(SI, 0, NotCond);
    SI.swapValues();
    SI.swapProfMetadata();
    return &SI;
  }

  if (Instruction *I = foldVectorSelect(SI))
    return I;

  // If we can compute the condition, there's no need for a select.
  // Like the above fold, we are attempting to reduce compile-time cost by
  // putting this fold here with limitations rather than in InstSimplify.
  // The motivation for this call into value tracking is to take advantage of
  // the assumption cache, so make sure that is populated.
  if (!CondVal->getType()->isVectorTy() && !AC.assumptions().empty()) {
    KnownBits Known(1);
    computeKnownBits(CondVal, Known, 0, &SI);
    if (Known.One.isOne())
      return replaceInstUsesWith(SI, TrueVal);
    if (Known.Zero.isOne())
      return replaceInstUsesWith(SI, FalseVal);
  }

  if (Instruction *BitCastSel = foldSelectCmpBitcasts(SI, Builder))
    return BitCastSel;

  // Simplify selects that test the returned flag of cmpxchg instructions.
  if (Value *V = foldSelectCmpXchg(SI))
    return replaceInstUsesWith(SI, V);

  if (Instruction *Select = foldSelectBinOpIdentity(SI, TLI, *this))
    return Select;

  if (Instruction *Funnel = foldSelectFunnelShift(SI, Builder))
    return Funnel;

  if (Instruction *Copysign = foldSelectToCopysign(SI, Builder))
    return Copysign;

  if (Instruction *PN = foldSelectToPhi(SI, DT, Builder))
    return replaceInstUsesWith(SI, PN);

  if (Value *Fr = foldSelectWithFrozenICmp(SI, Builder))
    return replaceInstUsesWith(SI, Fr);

  if (Value *V = foldRoundUpIntegerWithPow2Alignment(SI, Builder))
    return replaceInstUsesWith(SI, V);

  // select(mask, mload(,,mask,0), 0) -> mload(,,mask,0)
  // Load inst is intentionally not checked for hasOneUse()
  if (match(FalseVal, m_Zero()) &&
      (match(TrueVal, m_MaskedLoad(m_Value(), m_Value(), m_Specific(CondVal),
                                   m_CombineOr(m_Undef(), m_Zero()))) ||
       match(TrueVal, m_MaskedGather(m_Value(), m_Value(), m_Specific(CondVal),
                                     m_CombineOr(m_Undef(), m_Zero()))))) {
    auto *MaskedInst = cast<IntrinsicInst>(TrueVal);
    if (isa<UndefValue>(MaskedInst->getArgOperand(3)))
      MaskedInst->setArgOperand(3, FalseVal /* Zero */);
    return replaceInstUsesWith(SI, MaskedInst);
  }

  Value *Mask;
  if (match(TrueVal, m_Zero()) &&
      (match(FalseVal, m_MaskedLoad(m_Value(), m_Value(), m_Value(Mask),
                                    m_CombineOr(m_Undef(), m_Zero()))) ||
       match(FalseVal, m_MaskedGather(m_Value(), m_Value(), m_Value(Mask),
                                      m_CombineOr(m_Undef(), m_Zero())))) &&
      (CondVal->getType() == Mask->getType())) {
    // We can remove the select by ensuring the load zeros all lanes the
    // select would have.  We determine this by proving there is no overlap
    // between the load and select masks.
    // (i.e (load_mask & select_mask) == 0 == no overlap)
    bool CanMergeSelectIntoLoad = false;
    if (Value *V = simplifyAndInst(CondVal, Mask, SQ.getWithInstruction(&SI)))
      CanMergeSelectIntoLoad = match(V, m_Zero());

    if (CanMergeSelectIntoLoad) {
      auto *MaskedInst = cast<IntrinsicInst>(FalseVal);
      if (isa<UndefValue>(MaskedInst->getArgOperand(3)))
        MaskedInst->setArgOperand(3, TrueVal /* Zero */);
      return replaceInstUsesWith(SI, MaskedInst);
    }
  }

  if (Instruction *I = foldSelectOfSymmetricSelect(SI, Builder))
    return I;

  if (Instruction *I = foldNestedSelects(SI, Builder))
    return I;

  // Match logical variants of the pattern,
  // and transform them iff that gets rid of inversions.
  //   (~x) | y  -->  ~(x & (~y))
  //   (~x) & y  -->  ~(x | (~y))
  if (sinkNotIntoOtherHandOfLogicalOp(SI))
    return &SI;

  if (Instruction *I = foldBitCeil(SI, Builder))
    return I;

  // Fold:
  // (select A && B, T, F) -> (select A, (select B, T, F), F)
  // (select A || B, T, F) -> (select A, T, (select B, T, F))
  // if (select B, T, F) is foldable.
  // TODO: preserve FMF flags
  auto FoldSelectWithAndOrCond = [&](bool IsAnd, Value *A,
                                     Value *B) -> Instruction * {
    if (Value *V = simplifySelectInst(B, TrueVal, FalseVal,
                                      SQ.getWithInstruction(&SI)))
      return SelectInst::Create(A, IsAnd ? V : TrueVal, IsAnd ? FalseVal : V);

    // Is (select B, T, F) a SPF?
    if (CondVal->hasOneUse() && SelType->isIntOrIntVectorTy()) {
      if (ICmpInst *Cmp = dyn_cast<ICmpInst>(B))
        if (Value *V = canonicalizeSPF(*Cmp, TrueVal, FalseVal, *this))
          return SelectInst::Create(A, IsAnd ? V : TrueVal,
                                    IsAnd ? FalseVal : V);
    }

    return nullptr;
  };

  Value *LHS, *RHS;
  if (match(CondVal, m_And(m_Value(LHS), m_Value(RHS)))) {
    if (Instruction *I = FoldSelectWithAndOrCond(/*IsAnd*/ true, LHS, RHS))
      return I;
    if (Instruction *I = FoldSelectWithAndOrCond(/*IsAnd*/ true, RHS, LHS))
      return I;
  } else if (match(CondVal, m_Or(m_Value(LHS), m_Value(RHS)))) {
    if (Instruction *I = FoldSelectWithAndOrCond(/*IsAnd*/ false, LHS, RHS))
      return I;
    if (Instruction *I = FoldSelectWithAndOrCond(/*IsAnd*/ false, RHS, LHS))
      return I;
  } else {
    // We cannot swap the operands of logical and/or.
    // TODO: Can we swap the operands by inserting a freeze?
    if (match(CondVal, m_LogicalAnd(m_Value(LHS), m_Value(RHS)))) {
      if (Instruction *I = FoldSelectWithAndOrCond(/*IsAnd*/ true, LHS, RHS))
        return I;
    } else if (match(CondVal, m_LogicalOr(m_Value(LHS), m_Value(RHS)))) {
      if (Instruction *I = FoldSelectWithAndOrCond(/*IsAnd*/ false, LHS, RHS))
        return I;
    }
  }

  // select Cond, !X, X -> xor Cond, X
  if (CondVal->getType() == SI.getType() && isKnownInversion(FalseVal, TrueVal))
    return BinaryOperator::CreateXor(CondVal, FalseVal);

  // For vectors, this transform is only safe if the simplification does not
  // look through any lane-crossing operations. For now, limit to scalars only.
  if (SelType->isIntegerTy() &&
      (!isa<Constant>(TrueVal) || !isa<Constant>(FalseVal))) {
    // Try to simplify select arms based on KnownBits implied by the condition.
    CondContext CC(CondVal);
    findValuesAffectedByCondition(CondVal, /*IsAssume=*/false, [&](Value *V) {
      CC.AffectedValues.insert(V);
    });
    SimplifyQuery Q = SQ.getWithInstruction(&SI).getWithCondContext(CC);
    if (!CC.AffectedValues.empty()) {
      if (!isa<Constant>(TrueVal) &&
          hasAffectedValue(TrueVal, CC.AffectedValues, /*Depth=*/0)) {
        KnownBits Known = llvm::computeKnownBits(TrueVal, /*Depth=*/0, Q);
        if (Known.isConstant())
          return replaceOperand(SI, 1,
                                ConstantInt::get(SelType, Known.getConstant()));
      }

      CC.Invert = true;
      if (!isa<Constant>(FalseVal) &&
          hasAffectedValue(FalseVal, CC.AffectedValues, /*Depth=*/0)) {
        KnownBits Known = llvm::computeKnownBits(FalseVal, /*Depth=*/0, Q);
        if (Known.isConstant())
          return replaceOperand(SI, 2,
                                ConstantInt::get(SelType, Known.getConstant()));
      }
    }
  }

  return nullptr;
}
