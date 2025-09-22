//===- InstructionSimplify.cpp - Fold instruction operands ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements routines for folding instructions into simpler forms
// that do not require creating new instructions.  This does constant folding
// ("add i32 1, 1" -> "2") but can also handle non-constant operands, either
// returning a constant ("and i32 %x, 0" -> "0") or an already existing value
// ("and i32 %x, %x" -> "%x").  All operands are assumed to have already been
// simplified: This is usually true and assuming it simplifies the logic (if
// they have not been simplified then results are correct but maybe suboptimal).
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/InstructionSimplify.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/CmpInstAnalysis.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstSimplifyFolder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/OverflowInstAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/Support/KnownBits.h"
#include <algorithm>
#include <optional>
using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "instsimplify"

enum { RecursionLimit = 3 };

STATISTIC(NumExpand, "Number of expansions");
STATISTIC(NumReassoc, "Number of reassociations");

static Value *simplifyAndInst(Value *, Value *, const SimplifyQuery &,
                              unsigned);
static Value *simplifyUnOp(unsigned, Value *, const SimplifyQuery &, unsigned);
static Value *simplifyFPUnOp(unsigned, Value *, const FastMathFlags &,
                             const SimplifyQuery &, unsigned);
static Value *simplifyBinOp(unsigned, Value *, Value *, const SimplifyQuery &,
                            unsigned);
static Value *simplifyBinOp(unsigned, Value *, Value *, const FastMathFlags &,
                            const SimplifyQuery &, unsigned);
static Value *simplifyCmpInst(unsigned, Value *, Value *, const SimplifyQuery &,
                              unsigned);
static Value *simplifyICmpInst(unsigned Predicate, Value *LHS, Value *RHS,
                               const SimplifyQuery &Q, unsigned MaxRecurse);
static Value *simplifyOrInst(Value *, Value *, const SimplifyQuery &, unsigned);
static Value *simplifyXorInst(Value *, Value *, const SimplifyQuery &,
                              unsigned);
static Value *simplifyCastInst(unsigned, Value *, Type *, const SimplifyQuery &,
                               unsigned);
static Value *simplifyGEPInst(Type *, Value *, ArrayRef<Value *>,
                              GEPNoWrapFlags, const SimplifyQuery &, unsigned);
static Value *simplifySelectInst(Value *, Value *, Value *,
                                 const SimplifyQuery &, unsigned);
static Value *simplifyInstructionWithOperands(Instruction *I,
                                              ArrayRef<Value *> NewOps,
                                              const SimplifyQuery &SQ,
                                              unsigned MaxRecurse);

static Value *foldSelectWithBinaryOp(Value *Cond, Value *TrueVal,
                                     Value *FalseVal) {
  BinaryOperator::BinaryOps BinOpCode;
  if (auto *BO = dyn_cast<BinaryOperator>(Cond))
    BinOpCode = BO->getOpcode();
  else
    return nullptr;

  CmpInst::Predicate ExpectedPred, Pred1, Pred2;
  if (BinOpCode == BinaryOperator::Or) {
    ExpectedPred = ICmpInst::ICMP_NE;
  } else if (BinOpCode == BinaryOperator::And) {
    ExpectedPred = ICmpInst::ICMP_EQ;
  } else
    return nullptr;

  // %A = icmp eq %TV, %FV
  // %B = icmp eq %X, %Y (and one of these is a select operand)
  // %C = and %A, %B
  // %D = select %C, %TV, %FV
  // -->
  // %FV

  // %A = icmp ne %TV, %FV
  // %B = icmp ne %X, %Y (and one of these is a select operand)
  // %C = or %A, %B
  // %D = select %C, %TV, %FV
  // -->
  // %TV
  Value *X, *Y;
  if (!match(Cond, m_c_BinOp(m_c_ICmp(Pred1, m_Specific(TrueVal),
                                      m_Specific(FalseVal)),
                             m_ICmp(Pred2, m_Value(X), m_Value(Y)))) ||
      Pred1 != Pred2 || Pred1 != ExpectedPred)
    return nullptr;

  if (X == TrueVal || X == FalseVal || Y == TrueVal || Y == FalseVal)
    return BinOpCode == BinaryOperator::Or ? TrueVal : FalseVal;

  return nullptr;
}

/// For a boolean type or a vector of boolean type, return false or a vector
/// with every element false.
static Constant *getFalse(Type *Ty) { return ConstantInt::getFalse(Ty); }

/// For a boolean type or a vector of boolean type, return true or a vector
/// with every element true.
static Constant *getTrue(Type *Ty) { return ConstantInt::getTrue(Ty); }

/// isSameCompare - Is V equivalent to the comparison "LHS Pred RHS"?
static bool isSameCompare(Value *V, CmpInst::Predicate Pred, Value *LHS,
                          Value *RHS) {
  CmpInst *Cmp = dyn_cast<CmpInst>(V);
  if (!Cmp)
    return false;
  CmpInst::Predicate CPred = Cmp->getPredicate();
  Value *CLHS = Cmp->getOperand(0), *CRHS = Cmp->getOperand(1);
  if (CPred == Pred && CLHS == LHS && CRHS == RHS)
    return true;
  return CPred == CmpInst::getSwappedPredicate(Pred) && CLHS == RHS &&
         CRHS == LHS;
}

/// Simplify comparison with true or false branch of select:
///  %sel = select i1 %cond, i32 %tv, i32 %fv
///  %cmp = icmp sle i32 %sel, %rhs
/// Compose new comparison by substituting %sel with either %tv or %fv
/// and see if it simplifies.
static Value *simplifyCmpSelCase(CmpInst::Predicate Pred, Value *LHS,
                                 Value *RHS, Value *Cond,
                                 const SimplifyQuery &Q, unsigned MaxRecurse,
                                 Constant *TrueOrFalse) {
  Value *SimplifiedCmp = simplifyCmpInst(Pred, LHS, RHS, Q, MaxRecurse);
  if (SimplifiedCmp == Cond) {
    // %cmp simplified to the select condition (%cond).
    return TrueOrFalse;
  } else if (!SimplifiedCmp && isSameCompare(Cond, Pred, LHS, RHS)) {
    // It didn't simplify. However, if composed comparison is equivalent
    // to the select condition (%cond) then we can replace it.
    return TrueOrFalse;
  }
  return SimplifiedCmp;
}

/// Simplify comparison with true branch of select
static Value *simplifyCmpSelTrueCase(CmpInst::Predicate Pred, Value *LHS,
                                     Value *RHS, Value *Cond,
                                     const SimplifyQuery &Q,
                                     unsigned MaxRecurse) {
  return simplifyCmpSelCase(Pred, LHS, RHS, Cond, Q, MaxRecurse,
                            getTrue(Cond->getType()));
}

/// Simplify comparison with false branch of select
static Value *simplifyCmpSelFalseCase(CmpInst::Predicate Pred, Value *LHS,
                                      Value *RHS, Value *Cond,
                                      const SimplifyQuery &Q,
                                      unsigned MaxRecurse) {
  return simplifyCmpSelCase(Pred, LHS, RHS, Cond, Q, MaxRecurse,
                            getFalse(Cond->getType()));
}

/// We know comparison with both branches of select can be simplified, but they
/// are not equal. This routine handles some logical simplifications.
static Value *handleOtherCmpSelSimplifications(Value *TCmp, Value *FCmp,
                                               Value *Cond,
                                               const SimplifyQuery &Q,
                                               unsigned MaxRecurse) {
  // If the false value simplified to false, then the result of the compare
  // is equal to "Cond && TCmp".  This also catches the case when the false
  // value simplified to false and the true value to true, returning "Cond".
  // Folding select to and/or isn't poison-safe in general; impliesPoison
  // checks whether folding it does not convert a well-defined value into
  // poison.
  if (match(FCmp, m_Zero()) && impliesPoison(TCmp, Cond))
    if (Value *V = simplifyAndInst(Cond, TCmp, Q, MaxRecurse))
      return V;
  // If the true value simplified to true, then the result of the compare
  // is equal to "Cond || FCmp".
  if (match(TCmp, m_One()) && impliesPoison(FCmp, Cond))
    if (Value *V = simplifyOrInst(Cond, FCmp, Q, MaxRecurse))
      return V;
  // Finally, if the false value simplified to true and the true value to
  // false, then the result of the compare is equal to "!Cond".
  if (match(FCmp, m_One()) && match(TCmp, m_Zero()))
    if (Value *V = simplifyXorInst(
            Cond, Constant::getAllOnesValue(Cond->getType()), Q, MaxRecurse))
      return V;
  return nullptr;
}

/// Does the given value dominate the specified phi node?
static bool valueDominatesPHI(Value *V, PHINode *P, const DominatorTree *DT) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    // Arguments and constants dominate all instructions.
    return true;

  // If we have a DominatorTree then do a precise test.
  if (DT)
    return DT->dominates(I, P);

  // Otherwise, if the instruction is in the entry block and is not an invoke,
  // then it obviously dominates all phi nodes.
  if (I->getParent()->isEntryBlock() && !isa<InvokeInst>(I) &&
      !isa<CallBrInst>(I))
    return true;

  return false;
}

/// Try to simplify a binary operator of form "V op OtherOp" where V is
/// "(B0 opex B1)" by distributing 'op' across 'opex' as
/// "(B0 op OtherOp) opex (B1 op OtherOp)".
static Value *expandBinOp(Instruction::BinaryOps Opcode, Value *V,
                          Value *OtherOp, Instruction::BinaryOps OpcodeToExpand,
                          const SimplifyQuery &Q, unsigned MaxRecurse) {
  auto *B = dyn_cast<BinaryOperator>(V);
  if (!B || B->getOpcode() != OpcodeToExpand)
    return nullptr;
  Value *B0 = B->getOperand(0), *B1 = B->getOperand(1);
  Value *L =
      simplifyBinOp(Opcode, B0, OtherOp, Q.getWithoutUndef(), MaxRecurse);
  if (!L)
    return nullptr;
  Value *R =
      simplifyBinOp(Opcode, B1, OtherOp, Q.getWithoutUndef(), MaxRecurse);
  if (!R)
    return nullptr;

  // Does the expanded pair of binops simplify to the existing binop?
  if ((L == B0 && R == B1) ||
      (Instruction::isCommutative(OpcodeToExpand) && L == B1 && R == B0)) {
    ++NumExpand;
    return B;
  }

  // Otherwise, return "L op' R" if it simplifies.
  Value *S = simplifyBinOp(OpcodeToExpand, L, R, Q, MaxRecurse);
  if (!S)
    return nullptr;

  ++NumExpand;
  return S;
}

/// Try to simplify binops of form "A op (B op' C)" or the commuted variant by
/// distributing op over op'.
static Value *expandCommutativeBinOp(Instruction::BinaryOps Opcode, Value *L,
                                     Value *R,
                                     Instruction::BinaryOps OpcodeToExpand,
                                     const SimplifyQuery &Q,
                                     unsigned MaxRecurse) {
  // Recursion is always used, so bail out at once if we already hit the limit.
  if (!MaxRecurse--)
    return nullptr;

  if (Value *V = expandBinOp(Opcode, L, R, OpcodeToExpand, Q, MaxRecurse))
    return V;
  if (Value *V = expandBinOp(Opcode, R, L, OpcodeToExpand, Q, MaxRecurse))
    return V;
  return nullptr;
}

/// Generic simplifications for associative binary operations.
/// Returns the simpler value, or null if none was found.
static Value *simplifyAssociativeBinOp(Instruction::BinaryOps Opcode,
                                       Value *LHS, Value *RHS,
                                       const SimplifyQuery &Q,
                                       unsigned MaxRecurse) {
  assert(Instruction::isAssociative(Opcode) && "Not an associative operation!");

  // Recursion is always used, so bail out at once if we already hit the limit.
  if (!MaxRecurse--)
    return nullptr;

  BinaryOperator *Op0 = dyn_cast<BinaryOperator>(LHS);
  BinaryOperator *Op1 = dyn_cast<BinaryOperator>(RHS);

  // Transform: "(A op B) op C" ==> "A op (B op C)" if it simplifies completely.
  if (Op0 && Op0->getOpcode() == Opcode) {
    Value *A = Op0->getOperand(0);
    Value *B = Op0->getOperand(1);
    Value *C = RHS;

    // Does "B op C" simplify?
    if (Value *V = simplifyBinOp(Opcode, B, C, Q, MaxRecurse)) {
      // It does!  Return "A op V" if it simplifies or is already available.
      // If V equals B then "A op V" is just the LHS.
      if (V == B)
        return LHS;
      // Otherwise return "A op V" if it simplifies.
      if (Value *W = simplifyBinOp(Opcode, A, V, Q, MaxRecurse)) {
        ++NumReassoc;
        return W;
      }
    }
  }

  // Transform: "A op (B op C)" ==> "(A op B) op C" if it simplifies completely.
  if (Op1 && Op1->getOpcode() == Opcode) {
    Value *A = LHS;
    Value *B = Op1->getOperand(0);
    Value *C = Op1->getOperand(1);

    // Does "A op B" simplify?
    if (Value *V = simplifyBinOp(Opcode, A, B, Q, MaxRecurse)) {
      // It does!  Return "V op C" if it simplifies or is already available.
      // If V equals B then "V op C" is just the RHS.
      if (V == B)
        return RHS;
      // Otherwise return "V op C" if it simplifies.
      if (Value *W = simplifyBinOp(Opcode, V, C, Q, MaxRecurse)) {
        ++NumReassoc;
        return W;
      }
    }
  }

  // The remaining transforms require commutativity as well as associativity.
  if (!Instruction::isCommutative(Opcode))
    return nullptr;

  // Transform: "(A op B) op C" ==> "(C op A) op B" if it simplifies completely.
  if (Op0 && Op0->getOpcode() == Opcode) {
    Value *A = Op0->getOperand(0);
    Value *B = Op0->getOperand(1);
    Value *C = RHS;

    // Does "C op A" simplify?
    if (Value *V = simplifyBinOp(Opcode, C, A, Q, MaxRecurse)) {
      // It does!  Return "V op B" if it simplifies or is already available.
      // If V equals A then "V op B" is just the LHS.
      if (V == A)
        return LHS;
      // Otherwise return "V op B" if it simplifies.
      if (Value *W = simplifyBinOp(Opcode, V, B, Q, MaxRecurse)) {
        ++NumReassoc;
        return W;
      }
    }
  }

  // Transform: "A op (B op C)" ==> "B op (C op A)" if it simplifies completely.
  if (Op1 && Op1->getOpcode() == Opcode) {
    Value *A = LHS;
    Value *B = Op1->getOperand(0);
    Value *C = Op1->getOperand(1);

    // Does "C op A" simplify?
    if (Value *V = simplifyBinOp(Opcode, C, A, Q, MaxRecurse)) {
      // It does!  Return "B op V" if it simplifies or is already available.
      // If V equals C then "B op V" is just the RHS.
      if (V == C)
        return RHS;
      // Otherwise return "B op V" if it simplifies.
      if (Value *W = simplifyBinOp(Opcode, B, V, Q, MaxRecurse)) {
        ++NumReassoc;
        return W;
      }
    }
  }

  return nullptr;
}

/// In the case of a binary operation with a select instruction as an operand,
/// try to simplify the binop by seeing whether evaluating it on both branches
/// of the select results in the same value. Returns the common value if so,
/// otherwise returns null.
static Value *threadBinOpOverSelect(Instruction::BinaryOps Opcode, Value *LHS,
                                    Value *RHS, const SimplifyQuery &Q,
                                    unsigned MaxRecurse) {
  // Recursion is always used, so bail out at once if we already hit the limit.
  if (!MaxRecurse--)
    return nullptr;

  SelectInst *SI;
  if (isa<SelectInst>(LHS)) {
    SI = cast<SelectInst>(LHS);
  } else {
    assert(isa<SelectInst>(RHS) && "No select instruction operand!");
    SI = cast<SelectInst>(RHS);
  }

  // Evaluate the BinOp on the true and false branches of the select.
  Value *TV;
  Value *FV;
  if (SI == LHS) {
    TV = simplifyBinOp(Opcode, SI->getTrueValue(), RHS, Q, MaxRecurse);
    FV = simplifyBinOp(Opcode, SI->getFalseValue(), RHS, Q, MaxRecurse);
  } else {
    TV = simplifyBinOp(Opcode, LHS, SI->getTrueValue(), Q, MaxRecurse);
    FV = simplifyBinOp(Opcode, LHS, SI->getFalseValue(), Q, MaxRecurse);
  }

  // If they simplified to the same value, then return the common value.
  // If they both failed to simplify then return null.
  if (TV == FV)
    return TV;

  // If one branch simplified to undef, return the other one.
  if (TV && Q.isUndefValue(TV))
    return FV;
  if (FV && Q.isUndefValue(FV))
    return TV;

  // If applying the operation did not change the true and false select values,
  // then the result of the binop is the select itself.
  if (TV == SI->getTrueValue() && FV == SI->getFalseValue())
    return SI;

  // If one branch simplified and the other did not, and the simplified
  // value is equal to the unsimplified one, return the simplified value.
  // For example, select (cond, X, X & Z) & Z -> X & Z.
  if ((FV && !TV) || (TV && !FV)) {
    // Check that the simplified value has the form "X op Y" where "op" is the
    // same as the original operation.
    Instruction *Simplified = dyn_cast<Instruction>(FV ? FV : TV);
    if (Simplified && Simplified->getOpcode() == unsigned(Opcode) &&
        !Simplified->hasPoisonGeneratingFlags()) {
      // The value that didn't simplify is "UnsimplifiedLHS op UnsimplifiedRHS".
      // We already know that "op" is the same as for the simplified value.  See
      // if the operands match too.  If so, return the simplified value.
      Value *UnsimplifiedBranch = FV ? SI->getTrueValue() : SI->getFalseValue();
      Value *UnsimplifiedLHS = SI == LHS ? UnsimplifiedBranch : LHS;
      Value *UnsimplifiedRHS = SI == LHS ? RHS : UnsimplifiedBranch;
      if (Simplified->getOperand(0) == UnsimplifiedLHS &&
          Simplified->getOperand(1) == UnsimplifiedRHS)
        return Simplified;
      if (Simplified->isCommutative() &&
          Simplified->getOperand(1) == UnsimplifiedLHS &&
          Simplified->getOperand(0) == UnsimplifiedRHS)
        return Simplified;
    }
  }

  return nullptr;
}

/// In the case of a comparison with a select instruction, try to simplify the
/// comparison by seeing whether both branches of the select result in the same
/// value. Returns the common value if so, otherwise returns null.
/// For example, if we have:
///  %tmp = select i1 %cmp, i32 1, i32 2
///  %cmp1 = icmp sle i32 %tmp, 3
/// We can simplify %cmp1 to true, because both branches of select are
/// less than 3. We compose new comparison by substituting %tmp with both
/// branches of select and see if it can be simplified.
static Value *threadCmpOverSelect(CmpInst::Predicate Pred, Value *LHS,
                                  Value *RHS, const SimplifyQuery &Q,
                                  unsigned MaxRecurse) {
  // Recursion is always used, so bail out at once if we already hit the limit.
  if (!MaxRecurse--)
    return nullptr;

  // Make sure the select is on the LHS.
  if (!isa<SelectInst>(LHS)) {
    std::swap(LHS, RHS);
    Pred = CmpInst::getSwappedPredicate(Pred);
  }
  assert(isa<SelectInst>(LHS) && "Not comparing with a select instruction!");
  SelectInst *SI = cast<SelectInst>(LHS);
  Value *Cond = SI->getCondition();
  Value *TV = SI->getTrueValue();
  Value *FV = SI->getFalseValue();

  // Now that we have "cmp select(Cond, TV, FV), RHS", analyse it.
  // Does "cmp TV, RHS" simplify?
  Value *TCmp = simplifyCmpSelTrueCase(Pred, TV, RHS, Cond, Q, MaxRecurse);
  if (!TCmp)
    return nullptr;

  // Does "cmp FV, RHS" simplify?
  Value *FCmp = simplifyCmpSelFalseCase(Pred, FV, RHS, Cond, Q, MaxRecurse);
  if (!FCmp)
    return nullptr;

  // If both sides simplified to the same value, then use it as the result of
  // the original comparison.
  if (TCmp == FCmp)
    return TCmp;

  // The remaining cases only make sense if the select condition has the same
  // type as the result of the comparison, so bail out if this is not so.
  if (Cond->getType()->isVectorTy() == RHS->getType()->isVectorTy())
    return handleOtherCmpSelSimplifications(TCmp, FCmp, Cond, Q, MaxRecurse);

  return nullptr;
}

/// In the case of a binary operation with an operand that is a PHI instruction,
/// try to simplify the binop by seeing whether evaluating it on the incoming
/// phi values yields the same result for every value. If so returns the common
/// value, otherwise returns null.
static Value *threadBinOpOverPHI(Instruction::BinaryOps Opcode, Value *LHS,
                                 Value *RHS, const SimplifyQuery &Q,
                                 unsigned MaxRecurse) {
  // Recursion is always used, so bail out at once if we already hit the limit.
  if (!MaxRecurse--)
    return nullptr;

  PHINode *PI;
  if (isa<PHINode>(LHS)) {
    PI = cast<PHINode>(LHS);
    // Bail out if RHS and the phi may be mutually interdependent due to a loop.
    if (!valueDominatesPHI(RHS, PI, Q.DT))
      return nullptr;
  } else {
    assert(isa<PHINode>(RHS) && "No PHI instruction operand!");
    PI = cast<PHINode>(RHS);
    // Bail out if LHS and the phi may be mutually interdependent due to a loop.
    if (!valueDominatesPHI(LHS, PI, Q.DT))
      return nullptr;
  }

  // Evaluate the BinOp on the incoming phi values.
  Value *CommonValue = nullptr;
  for (Use &Incoming : PI->incoming_values()) {
    // If the incoming value is the phi node itself, it can safely be skipped.
    if (Incoming == PI)
      continue;
    Instruction *InTI = PI->getIncomingBlock(Incoming)->getTerminator();
    Value *V = PI == LHS
                   ? simplifyBinOp(Opcode, Incoming, RHS,
                                   Q.getWithInstruction(InTI), MaxRecurse)
                   : simplifyBinOp(Opcode, LHS, Incoming,
                                   Q.getWithInstruction(InTI), MaxRecurse);
    // If the operation failed to simplify, or simplified to a different value
    // to previously, then give up.
    if (!V || (CommonValue && V != CommonValue))
      return nullptr;
    CommonValue = V;
  }

  return CommonValue;
}

/// In the case of a comparison with a PHI instruction, try to simplify the
/// comparison by seeing whether comparing with all of the incoming phi values
/// yields the same result every time. If so returns the common result,
/// otherwise returns null.
static Value *threadCmpOverPHI(CmpInst::Predicate Pred, Value *LHS, Value *RHS,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  // Recursion is always used, so bail out at once if we already hit the limit.
  if (!MaxRecurse--)
    return nullptr;

  // Make sure the phi is on the LHS.
  if (!isa<PHINode>(LHS)) {
    std::swap(LHS, RHS);
    Pred = CmpInst::getSwappedPredicate(Pred);
  }
  assert(isa<PHINode>(LHS) && "Not comparing with a phi instruction!");
  PHINode *PI = cast<PHINode>(LHS);

  // Bail out if RHS and the phi may be mutually interdependent due to a loop.
  if (!valueDominatesPHI(RHS, PI, Q.DT))
    return nullptr;

  // Evaluate the BinOp on the incoming phi values.
  Value *CommonValue = nullptr;
  for (unsigned u = 0, e = PI->getNumIncomingValues(); u < e; ++u) {
    Value *Incoming = PI->getIncomingValue(u);
    Instruction *InTI = PI->getIncomingBlock(u)->getTerminator();
    // If the incoming value is the phi node itself, it can safely be skipped.
    if (Incoming == PI)
      continue;
    // Change the context instruction to the "edge" that flows into the phi.
    // This is important because that is where incoming is actually "evaluated"
    // even though it is used later somewhere else.
    Value *V = simplifyCmpInst(Pred, Incoming, RHS, Q.getWithInstruction(InTI),
                               MaxRecurse);
    // If the operation failed to simplify, or simplified to a different value
    // to previously, then give up.
    if (!V || (CommonValue && V != CommonValue))
      return nullptr;
    CommonValue = V;
  }

  return CommonValue;
}

static Constant *foldOrCommuteConstant(Instruction::BinaryOps Opcode,
                                       Value *&Op0, Value *&Op1,
                                       const SimplifyQuery &Q) {
  if (auto *CLHS = dyn_cast<Constant>(Op0)) {
    if (auto *CRHS = dyn_cast<Constant>(Op1)) {
      switch (Opcode) {
      default:
        break;
      case Instruction::FAdd:
      case Instruction::FSub:
      case Instruction::FMul:
      case Instruction::FDiv:
      case Instruction::FRem:
        if (Q.CxtI != nullptr)
          return ConstantFoldFPInstOperands(Opcode, CLHS, CRHS, Q.DL, Q.CxtI);
      }
      return ConstantFoldBinaryOpOperands(Opcode, CLHS, CRHS, Q.DL);
    }

    // Canonicalize the constant to the RHS if this is a commutative operation.
    if (Instruction::isCommutative(Opcode))
      std::swap(Op0, Op1);
  }
  return nullptr;
}

/// Given operands for an Add, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyAddInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                              const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Instruction::Add, Op0, Op1, Q))
    return C;

  // X + poison -> poison
  if (isa<PoisonValue>(Op1))
    return Op1;

  // X + undef -> undef
  if (Q.isUndefValue(Op1))
    return Op1;

  // X + 0 -> X
  if (match(Op1, m_Zero()))
    return Op0;

  // If two operands are negative, return 0.
  if (isKnownNegation(Op0, Op1))
    return Constant::getNullValue(Op0->getType());

  // X + (Y - X) -> Y
  // (Y - X) + X -> Y
  // Eg: X + -X -> 0
  Value *Y = nullptr;
  if (match(Op1, m_Sub(m_Value(Y), m_Specific(Op0))) ||
      match(Op0, m_Sub(m_Value(Y), m_Specific(Op1))))
    return Y;

  // X + ~X -> -1   since   ~X = -X-1
  Type *Ty = Op0->getType();
  if (match(Op0, m_Not(m_Specific(Op1))) || match(Op1, m_Not(m_Specific(Op0))))
    return Constant::getAllOnesValue(Ty);

  // add nsw/nuw (xor Y, signmask), signmask --> Y
  // The no-wrapping add guarantees that the top bit will be set by the add.
  // Therefore, the xor must be clearing the already set sign bit of Y.
  if ((IsNSW || IsNUW) && match(Op1, m_SignMask()) &&
      match(Op0, m_Xor(m_Value(Y), m_SignMask())))
    return Y;

  // add nuw %x, -1  ->  -1, because %x can only be 0.
  if (IsNUW && match(Op1, m_AllOnes()))
    return Op1; // Which is -1.

  /// i1 add -> xor.
  if (MaxRecurse && Op0->getType()->isIntOrIntVectorTy(1))
    if (Value *V = simplifyXorInst(Op0, Op1, Q, MaxRecurse - 1))
      return V;

  // Try some generic simplifications for associative operations.
  if (Value *V =
          simplifyAssociativeBinOp(Instruction::Add, Op0, Op1, Q, MaxRecurse))
    return V;

  // Threading Add over selects and phi nodes is pointless, so don't bother.
  // Threading over the select in "A + select(cond, B, C)" means evaluating
  // "A+B" and "A+C" and seeing if they are equal; but they are equal if and
  // only if B and C are equal.  If B and C are equal then (since we assume
  // that operands have already been simplified) "select(cond, B, C)" should
  // have been simplified to the common value of B and C already.  Analysing
  // "A+B" and "A+C" thus gains nothing, but costs compile time.  Similarly
  // for threading over phi nodes.

  return nullptr;
}

Value *llvm::simplifyAddInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                             const SimplifyQuery &Query) {
  return ::simplifyAddInst(Op0, Op1, IsNSW, IsNUW, Query, RecursionLimit);
}

/// Compute the base pointer and cumulative constant offsets for V.
///
/// This strips all constant offsets off of V, leaving it the base pointer, and
/// accumulates the total constant offset applied in the returned constant.
/// It returns zero if there are no constant offsets applied.
///
/// This is very similar to stripAndAccumulateConstantOffsets(), except it
/// normalizes the offset bitwidth to the stripped pointer type, not the
/// original pointer type.
static APInt stripAndComputeConstantOffsets(const DataLayout &DL, Value *&V,
                                            bool AllowNonInbounds = false) {
  assert(V->getType()->isPtrOrPtrVectorTy());

  APInt Offset = APInt::getZero(DL.getIndexTypeSizeInBits(V->getType()));
  V = V->stripAndAccumulateConstantOffsets(DL, Offset, AllowNonInbounds);
  // As that strip may trace through `addrspacecast`, need to sext or trunc
  // the offset calculated.
  return Offset.sextOrTrunc(DL.getIndexTypeSizeInBits(V->getType()));
}

/// Compute the constant difference between two pointer values.
/// If the difference is not a constant, returns zero.
static Constant *computePointerDifference(const DataLayout &DL, Value *LHS,
                                          Value *RHS) {
  APInt LHSOffset = stripAndComputeConstantOffsets(DL, LHS);
  APInt RHSOffset = stripAndComputeConstantOffsets(DL, RHS);

  // If LHS and RHS are not related via constant offsets to the same base
  // value, there is nothing we can do here.
  if (LHS != RHS)
    return nullptr;

  // Otherwise, the difference of LHS - RHS can be computed as:
  //    LHS - RHS
  //  = (LHSOffset + Base) - (RHSOffset + Base)
  //  = LHSOffset - RHSOffset
  Constant *Res = ConstantInt::get(LHS->getContext(), LHSOffset - RHSOffset);
  if (auto *VecTy = dyn_cast<VectorType>(LHS->getType()))
    Res = ConstantVector::getSplat(VecTy->getElementCount(), Res);
  return Res;
}

/// Test if there is a dominating equivalence condition for the
/// two operands. If there is, try to reduce the binary operation
/// between the two operands.
/// Example: Op0 - Op1 --> 0 when Op0 == Op1
static Value *simplifyByDomEq(unsigned Opcode, Value *Op0, Value *Op1,
                              const SimplifyQuery &Q, unsigned MaxRecurse) {
  // Recursive run it can not get any benefit
  if (MaxRecurse != RecursionLimit)
    return nullptr;

  std::optional<bool> Imp =
      isImpliedByDomCondition(CmpInst::ICMP_EQ, Op0, Op1, Q.CxtI, Q.DL);
  if (Imp && *Imp) {
    Type *Ty = Op0->getType();
    switch (Opcode) {
    case Instruction::Sub:
    case Instruction::Xor:
    case Instruction::URem:
    case Instruction::SRem:
      return Constant::getNullValue(Ty);

    case Instruction::SDiv:
    case Instruction::UDiv:
      return ConstantInt::get(Ty, 1);

    case Instruction::And:
    case Instruction::Or:
      // Could be either one - choose Op1 since that's more likely a constant.
      return Op1;
    default:
      break;
    }
  }
  return nullptr;
}

/// Given operands for a Sub, see if we can fold the result.
/// If not, this returns null.
static Value *simplifySubInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                              const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Instruction::Sub, Op0, Op1, Q))
    return C;

  // X - poison -> poison
  // poison - X -> poison
  if (isa<PoisonValue>(Op0) || isa<PoisonValue>(Op1))
    return PoisonValue::get(Op0->getType());

  // X - undef -> undef
  // undef - X -> undef
  if (Q.isUndefValue(Op0) || Q.isUndefValue(Op1))
    return UndefValue::get(Op0->getType());

  // X - 0 -> X
  if (match(Op1, m_Zero()))
    return Op0;

  // X - X -> 0
  if (Op0 == Op1)
    return Constant::getNullValue(Op0->getType());

  // Is this a negation?
  if (match(Op0, m_Zero())) {
    // 0 - X -> 0 if the sub is NUW.
    if (IsNUW)
      return Constant::getNullValue(Op0->getType());

    KnownBits Known = computeKnownBits(Op1, /* Depth */ 0, Q);
    if (Known.Zero.isMaxSignedValue()) {
      // Op1 is either 0 or the minimum signed value. If the sub is NSW, then
      // Op1 must be 0 because negating the minimum signed value is undefined.
      if (IsNSW)
        return Constant::getNullValue(Op0->getType());

      // 0 - X -> X if X is 0 or the minimum signed value.
      return Op1;
    }
  }

  // (X + Y) - Z -> X + (Y - Z) or Y + (X - Z) if everything simplifies.
  // For example, (X + Y) - Y -> X; (Y + X) - Y -> X
  Value *X = nullptr, *Y = nullptr, *Z = Op1;
  if (MaxRecurse && match(Op0, m_Add(m_Value(X), m_Value(Y)))) { // (X + Y) - Z
    // See if "V === Y - Z" simplifies.
    if (Value *V = simplifyBinOp(Instruction::Sub, Y, Z, Q, MaxRecurse - 1))
      // It does!  Now see if "X + V" simplifies.
      if (Value *W = simplifyBinOp(Instruction::Add, X, V, Q, MaxRecurse - 1)) {
        // It does, we successfully reassociated!
        ++NumReassoc;
        return W;
      }
    // See if "V === X - Z" simplifies.
    if (Value *V = simplifyBinOp(Instruction::Sub, X, Z, Q, MaxRecurse - 1))
      // It does!  Now see if "Y + V" simplifies.
      if (Value *W = simplifyBinOp(Instruction::Add, Y, V, Q, MaxRecurse - 1)) {
        // It does, we successfully reassociated!
        ++NumReassoc;
        return W;
      }
  }

  // X - (Y + Z) -> (X - Y) - Z or (X - Z) - Y if everything simplifies.
  // For example, X - (X + 1) -> -1
  X = Op0;
  if (MaxRecurse && match(Op1, m_Add(m_Value(Y), m_Value(Z)))) { // X - (Y + Z)
    // See if "V === X - Y" simplifies.
    if (Value *V = simplifyBinOp(Instruction::Sub, X, Y, Q, MaxRecurse - 1))
      // It does!  Now see if "V - Z" simplifies.
      if (Value *W = simplifyBinOp(Instruction::Sub, V, Z, Q, MaxRecurse - 1)) {
        // It does, we successfully reassociated!
        ++NumReassoc;
        return W;
      }
    // See if "V === X - Z" simplifies.
    if (Value *V = simplifyBinOp(Instruction::Sub, X, Z, Q, MaxRecurse - 1))
      // It does!  Now see if "V - Y" simplifies.
      if (Value *W = simplifyBinOp(Instruction::Sub, V, Y, Q, MaxRecurse - 1)) {
        // It does, we successfully reassociated!
        ++NumReassoc;
        return W;
      }
  }

  // Z - (X - Y) -> (Z - X) + Y if everything simplifies.
  // For example, X - (X - Y) -> Y.
  Z = Op0;
  if (MaxRecurse && match(Op1, m_Sub(m_Value(X), m_Value(Y)))) // Z - (X - Y)
    // See if "V === Z - X" simplifies.
    if (Value *V = simplifyBinOp(Instruction::Sub, Z, X, Q, MaxRecurse - 1))
      // It does!  Now see if "V + Y" simplifies.
      if (Value *W = simplifyBinOp(Instruction::Add, V, Y, Q, MaxRecurse - 1)) {
        // It does, we successfully reassociated!
        ++NumReassoc;
        return W;
      }

  // trunc(X) - trunc(Y) -> trunc(X - Y) if everything simplifies.
  if (MaxRecurse && match(Op0, m_Trunc(m_Value(X))) &&
      match(Op1, m_Trunc(m_Value(Y))))
    if (X->getType() == Y->getType())
      // See if "V === X - Y" simplifies.
      if (Value *V = simplifyBinOp(Instruction::Sub, X, Y, Q, MaxRecurse - 1))
        // It does!  Now see if "trunc V" simplifies.
        if (Value *W = simplifyCastInst(Instruction::Trunc, V, Op0->getType(),
                                        Q, MaxRecurse - 1))
          // It does, return the simplified "trunc V".
          return W;

  // Variations on GEP(base, I, ...) - GEP(base, i, ...) -> GEP(null, I-i, ...).
  if (match(Op0, m_PtrToInt(m_Value(X))) && match(Op1, m_PtrToInt(m_Value(Y))))
    if (Constant *Result = computePointerDifference(Q.DL, X, Y))
      return ConstantFoldIntegerCast(Result, Op0->getType(), /*IsSigned*/ true,
                                     Q.DL);

  // i1 sub -> xor.
  if (MaxRecurse && Op0->getType()->isIntOrIntVectorTy(1))
    if (Value *V = simplifyXorInst(Op0, Op1, Q, MaxRecurse - 1))
      return V;

  // Threading Sub over selects and phi nodes is pointless, so don't bother.
  // Threading over the select in "A - select(cond, B, C)" means evaluating
  // "A-B" and "A-C" and seeing if they are equal; but they are equal if and
  // only if B and C are equal.  If B and C are equal then (since we assume
  // that operands have already been simplified) "select(cond, B, C)" should
  // have been simplified to the common value of B and C already.  Analysing
  // "A-B" and "A-C" thus gains nothing, but costs compile time.  Similarly
  // for threading over phi nodes.

  if (Value *V = simplifyByDomEq(Instruction::Sub, Op0, Op1, Q, MaxRecurse))
    return V;

  return nullptr;
}

Value *llvm::simplifySubInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                             const SimplifyQuery &Q) {
  return ::simplifySubInst(Op0, Op1, IsNSW, IsNUW, Q, RecursionLimit);
}

/// Given operands for a Mul, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyMulInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                              const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Instruction::Mul, Op0, Op1, Q))
    return C;

  // X * poison -> poison
  if (isa<PoisonValue>(Op1))
    return Op1;

  // X * undef -> 0
  // X * 0 -> 0
  if (Q.isUndefValue(Op1) || match(Op1, m_Zero()))
    return Constant::getNullValue(Op0->getType());

  // X * 1 -> X
  if (match(Op1, m_One()))
    return Op0;

  // (X / Y) * Y -> X if the division is exact.
  Value *X = nullptr;
  if (Q.IIQ.UseInstrInfo &&
      (match(Op0,
             m_Exact(m_IDiv(m_Value(X), m_Specific(Op1)))) ||     // (X / Y) * Y
       match(Op1, m_Exact(m_IDiv(m_Value(X), m_Specific(Op0)))))) // Y * (X / Y)
    return X;

   if (Op0->getType()->isIntOrIntVectorTy(1)) {
    // mul i1 nsw is a special-case because -1 * -1 is poison (+1 is not
    // representable). All other cases reduce to 0, so just return 0.
    if (IsNSW)
      return ConstantInt::getNullValue(Op0->getType());

    // Treat "mul i1" as "and i1".
    if (MaxRecurse)
      if (Value *V = simplifyAndInst(Op0, Op1, Q, MaxRecurse - 1))
        return V;
  }

  // Try some generic simplifications for associative operations.
  if (Value *V =
          simplifyAssociativeBinOp(Instruction::Mul, Op0, Op1, Q, MaxRecurse))
    return V;

  // Mul distributes over Add. Try some generic simplifications based on this.
  if (Value *V = expandCommutativeBinOp(Instruction::Mul, Op0, Op1,
                                        Instruction::Add, Q, MaxRecurse))
    return V;

  // If the operation is with the result of a select instruction, check whether
  // operating on either branch of the select always yields the same value.
  if (isa<SelectInst>(Op0) || isa<SelectInst>(Op1))
    if (Value *V =
            threadBinOpOverSelect(Instruction::Mul, Op0, Op1, Q, MaxRecurse))
      return V;

  // If the operation is with the result of a phi instruction, check whether
  // operating on all incoming values of the phi always yields the same value.
  if (isa<PHINode>(Op0) || isa<PHINode>(Op1))
    if (Value *V =
            threadBinOpOverPHI(Instruction::Mul, Op0, Op1, Q, MaxRecurse))
      return V;

  return nullptr;
}

Value *llvm::simplifyMulInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                             const SimplifyQuery &Q) {
  return ::simplifyMulInst(Op0, Op1, IsNSW, IsNUW, Q, RecursionLimit);
}

/// Given a predicate and two operands, return true if the comparison is true.
/// This is a helper for div/rem simplification where we return some other value
/// when we can prove a relationship between the operands.
static bool isICmpTrue(ICmpInst::Predicate Pred, Value *LHS, Value *RHS,
                       const SimplifyQuery &Q, unsigned MaxRecurse) {
  Value *V = simplifyICmpInst(Pred, LHS, RHS, Q, MaxRecurse);
  Constant *C = dyn_cast_or_null<Constant>(V);
  return (C && C->isAllOnesValue());
}

/// Return true if we can simplify X / Y to 0. Remainder can adapt that answer
/// to simplify X % Y to X.
static bool isDivZero(Value *X, Value *Y, const SimplifyQuery &Q,
                      unsigned MaxRecurse, bool IsSigned) {
  // Recursion is always used, so bail out at once if we already hit the limit.
  if (!MaxRecurse--)
    return false;

  if (IsSigned) {
    // (X srem Y) sdiv Y --> 0
    if (match(X, m_SRem(m_Value(), m_Specific(Y))))
      return true;

    // |X| / |Y| --> 0
    //
    // We require that 1 operand is a simple constant. That could be extended to
    // 2 variables if we computed the sign bit for each.
    //
    // Make sure that a constant is not the minimum signed value because taking
    // the abs() of that is undefined.
    Type *Ty = X->getType();
    const APInt *C;
    if (match(X, m_APInt(C)) && !C->isMinSignedValue()) {
      // Is the variable divisor magnitude always greater than the constant
      // dividend magnitude?
      // |Y| > |C| --> Y < -abs(C) or Y > abs(C)
      Constant *PosDividendC = ConstantInt::get(Ty, C->abs());
      Constant *NegDividendC = ConstantInt::get(Ty, -C->abs());
      if (isICmpTrue(CmpInst::ICMP_SLT, Y, NegDividendC, Q, MaxRecurse) ||
          isICmpTrue(CmpInst::ICMP_SGT, Y, PosDividendC, Q, MaxRecurse))
        return true;
    }
    if (match(Y, m_APInt(C))) {
      // Special-case: we can't take the abs() of a minimum signed value. If
      // that's the divisor, then all we have to do is prove that the dividend
      // is also not the minimum signed value.
      if (C->isMinSignedValue())
        return isICmpTrue(CmpInst::ICMP_NE, X, Y, Q, MaxRecurse);

      // Is the variable dividend magnitude always less than the constant
      // divisor magnitude?
      // |X| < |C| --> X > -abs(C) and X < abs(C)
      Constant *PosDivisorC = ConstantInt::get(Ty, C->abs());
      Constant *NegDivisorC = ConstantInt::get(Ty, -C->abs());
      if (isICmpTrue(CmpInst::ICMP_SGT, X, NegDivisorC, Q, MaxRecurse) &&
          isICmpTrue(CmpInst::ICMP_SLT, X, PosDivisorC, Q, MaxRecurse))
        return true;
    }
    return false;
  }

  // IsSigned == false.

  // Is the unsigned dividend known to be less than a constant divisor?
  // TODO: Convert this (and above) to range analysis
  //      ("computeConstantRangeIncludingKnownBits")?
  const APInt *C;
  if (match(Y, m_APInt(C)) &&
      computeKnownBits(X, /* Depth */ 0, Q).getMaxValue().ult(*C))
    return true;

  // Try again for any divisor:
  // Is the dividend unsigned less than the divisor?
  return isICmpTrue(ICmpInst::ICMP_ULT, X, Y, Q, MaxRecurse);
}

/// Check for common or similar folds of integer division or integer remainder.
/// This applies to all 4 opcodes (sdiv/udiv/srem/urem).
static Value *simplifyDivRem(Instruction::BinaryOps Opcode, Value *Op0,
                             Value *Op1, const SimplifyQuery &Q,
                             unsigned MaxRecurse) {
  bool IsDiv = (Opcode == Instruction::SDiv || Opcode == Instruction::UDiv);
  bool IsSigned = (Opcode == Instruction::SDiv || Opcode == Instruction::SRem);

  Type *Ty = Op0->getType();

  // X / undef -> poison
  // X % undef -> poison
  if (Q.isUndefValue(Op1) || isa<PoisonValue>(Op1))
    return PoisonValue::get(Ty);

  // X / 0 -> poison
  // X % 0 -> poison
  // We don't need to preserve faults!
  if (match(Op1, m_Zero()))
    return PoisonValue::get(Ty);

  // If any element of a constant divisor fixed width vector is zero or undef
  // the behavior is undefined and we can fold the whole op to poison.
  auto *Op1C = dyn_cast<Constant>(Op1);
  auto *VTy = dyn_cast<FixedVectorType>(Ty);
  if (Op1C && VTy) {
    unsigned NumElts = VTy->getNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *Elt = Op1C->getAggregateElement(i);
      if (Elt && (Elt->isNullValue() || Q.isUndefValue(Elt)))
        return PoisonValue::get(Ty);
    }
  }

  // poison / X -> poison
  // poison % X -> poison
  if (isa<PoisonValue>(Op0))
    return Op0;

  // undef / X -> 0
  // undef % X -> 0
  if (Q.isUndefValue(Op0))
    return Constant::getNullValue(Ty);

  // 0 / X -> 0
  // 0 % X -> 0
  if (match(Op0, m_Zero()))
    return Constant::getNullValue(Op0->getType());

  // X / X -> 1
  // X % X -> 0
  if (Op0 == Op1)
    return IsDiv ? ConstantInt::get(Ty, 1) : Constant::getNullValue(Ty);

  KnownBits Known = computeKnownBits(Op1, /* Depth */ 0, Q);
  // X / 0 -> poison
  // X % 0 -> poison
  // If the divisor is known to be zero, just return poison. This can happen in
  // some cases where its provable indirectly the denominator is zero but it's
  // not trivially simplifiable (i.e known zero through a phi node).
  if (Known.isZero())
    return PoisonValue::get(Ty);

  // X / 1 -> X
  // X % 1 -> 0
  // If the divisor can only be zero or one, we can't have division-by-zero
  // or remainder-by-zero, so assume the divisor is 1.
  //   e.g. 1, zext (i8 X), sdiv X (Y and 1)
  if (Known.countMinLeadingZeros() == Known.getBitWidth() - 1)
    return IsDiv ? Op0 : Constant::getNullValue(Ty);

  // If X * Y does not overflow, then:
  //   X * Y / Y -> X
  //   X * Y % Y -> 0
  Value *X;
  if (match(Op0, m_c_Mul(m_Value(X), m_Specific(Op1)))) {
    auto *Mul = cast<OverflowingBinaryOperator>(Op0);
    // The multiplication can't overflow if it is defined not to, or if
    // X == A / Y for some A.
    if ((IsSigned && Q.IIQ.hasNoSignedWrap(Mul)) ||
        (!IsSigned && Q.IIQ.hasNoUnsignedWrap(Mul)) ||
        (IsSigned && match(X, m_SDiv(m_Value(), m_Specific(Op1)))) ||
        (!IsSigned && match(X, m_UDiv(m_Value(), m_Specific(Op1))))) {
      return IsDiv ? X : Constant::getNullValue(Op0->getType());
    }
  }

  if (isDivZero(Op0, Op1, Q, MaxRecurse, IsSigned))
    return IsDiv ? Constant::getNullValue(Op0->getType()) : Op0;

  if (Value *V = simplifyByDomEq(Opcode, Op0, Op1, Q, MaxRecurse))
    return V;

  // If the operation is with the result of a select instruction, check whether
  // operating on either branch of the select always yields the same value.
  if (isa<SelectInst>(Op0) || isa<SelectInst>(Op1))
    if (Value *V = threadBinOpOverSelect(Opcode, Op0, Op1, Q, MaxRecurse))
      return V;

  // If the operation is with the result of a phi instruction, check whether
  // operating on all incoming values of the phi always yields the same value.
  if (isa<PHINode>(Op0) || isa<PHINode>(Op1))
    if (Value *V = threadBinOpOverPHI(Opcode, Op0, Op1, Q, MaxRecurse))
      return V;

  return nullptr;
}

/// These are simplifications common to SDiv and UDiv.
static Value *simplifyDiv(Instruction::BinaryOps Opcode, Value *Op0, Value *Op1,
                          bool IsExact, const SimplifyQuery &Q,
                          unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Opcode, Op0, Op1, Q))
    return C;

  if (Value *V = simplifyDivRem(Opcode, Op0, Op1, Q, MaxRecurse))
    return V;

  const APInt *DivC;
  if (IsExact && match(Op1, m_APInt(DivC))) {
    // If this is an exact divide by a constant, then the dividend (Op0) must
    // have at least as many trailing zeros as the divisor to divide evenly. If
    // it has less trailing zeros, then the result must be poison.
    if (DivC->countr_zero()) {
      KnownBits KnownOp0 = computeKnownBits(Op0, /* Depth */ 0, Q);
      if (KnownOp0.countMaxTrailingZeros() < DivC->countr_zero())
        return PoisonValue::get(Op0->getType());
    }

    // udiv exact (mul nsw X, C), C --> X
    // sdiv exact (mul nuw X, C), C --> X
    // where C is not a power of 2.
    Value *X;
    if (!DivC->isPowerOf2() &&
        (Opcode == Instruction::UDiv
             ? match(Op0, m_NSWMul(m_Value(X), m_Specific(Op1)))
             : match(Op0, m_NUWMul(m_Value(X), m_Specific(Op1)))))
      return X;
  }

  return nullptr;
}

/// These are simplifications common to SRem and URem.
static Value *simplifyRem(Instruction::BinaryOps Opcode, Value *Op0, Value *Op1,
                          const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Opcode, Op0, Op1, Q))
    return C;

  if (Value *V = simplifyDivRem(Opcode, Op0, Op1, Q, MaxRecurse))
    return V;

  // (X << Y) % X -> 0
  if (Q.IIQ.UseInstrInfo) {
    if ((Opcode == Instruction::SRem &&
         match(Op0, m_NSWShl(m_Specific(Op1), m_Value()))) ||
        (Opcode == Instruction::URem &&
         match(Op0, m_NUWShl(m_Specific(Op1), m_Value()))))
      return Constant::getNullValue(Op0->getType());

    const APInt *C0;
    if (match(Op1, m_APInt(C0))) {
      // (srem (mul nsw X, C1), C0) -> 0 if C1 s% C0 == 0
      // (urem (mul nuw X, C1), C0) -> 0 if C1 u% C0 == 0
      if (Opcode == Instruction::SRem
              ? match(Op0,
                      m_NSWMul(m_Value(), m_CheckedInt([C0](const APInt &C) {
                                 return C.srem(*C0).isZero();
                               })))
              : match(Op0,
                      m_NUWMul(m_Value(), m_CheckedInt([C0](const APInt &C) {
                                 return C.urem(*C0).isZero();
                               }))))
        return Constant::getNullValue(Op0->getType());
    }
  }
  return nullptr;
}

/// Given operands for an SDiv, see if we can fold the result.
/// If not, this returns null.
static Value *simplifySDivInst(Value *Op0, Value *Op1, bool IsExact,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  // If two operands are negated and no signed overflow, return -1.
  if (isKnownNegation(Op0, Op1, /*NeedNSW=*/true))
    return Constant::getAllOnesValue(Op0->getType());

  return simplifyDiv(Instruction::SDiv, Op0, Op1, IsExact, Q, MaxRecurse);
}

Value *llvm::simplifySDivInst(Value *Op0, Value *Op1, bool IsExact,
                              const SimplifyQuery &Q) {
  return ::simplifySDivInst(Op0, Op1, IsExact, Q, RecursionLimit);
}

/// Given operands for a UDiv, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyUDivInst(Value *Op0, Value *Op1, bool IsExact,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  return simplifyDiv(Instruction::UDiv, Op0, Op1, IsExact, Q, MaxRecurse);
}

Value *llvm::simplifyUDivInst(Value *Op0, Value *Op1, bool IsExact,
                              const SimplifyQuery &Q) {
  return ::simplifyUDivInst(Op0, Op1, IsExact, Q, RecursionLimit);
}

/// Given operands for an SRem, see if we can fold the result.
/// If not, this returns null.
static Value *simplifySRemInst(Value *Op0, Value *Op1, const SimplifyQuery &Q,
                               unsigned MaxRecurse) {
  // If the divisor is 0, the result is undefined, so assume the divisor is -1.
  // srem Op0, (sext i1 X) --> srem Op0, -1 --> 0
  Value *X;
  if (match(Op1, m_SExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1))
    return ConstantInt::getNullValue(Op0->getType());

  // If the two operands are negated, return 0.
  if (isKnownNegation(Op0, Op1))
    return ConstantInt::getNullValue(Op0->getType());

  return simplifyRem(Instruction::SRem, Op0, Op1, Q, MaxRecurse);
}

Value *llvm::simplifySRemInst(Value *Op0, Value *Op1, const SimplifyQuery &Q) {
  return ::simplifySRemInst(Op0, Op1, Q, RecursionLimit);
}

/// Given operands for a URem, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyURemInst(Value *Op0, Value *Op1, const SimplifyQuery &Q,
                               unsigned MaxRecurse) {
  return simplifyRem(Instruction::URem, Op0, Op1, Q, MaxRecurse);
}

Value *llvm::simplifyURemInst(Value *Op0, Value *Op1, const SimplifyQuery &Q) {
  return ::simplifyURemInst(Op0, Op1, Q, RecursionLimit);
}

/// Returns true if a shift by \c Amount always yields poison.
static bool isPoisonShift(Value *Amount, const SimplifyQuery &Q) {
  Constant *C = dyn_cast<Constant>(Amount);
  if (!C)
    return false;

  // X shift by undef -> poison because it may shift by the bitwidth.
  if (Q.isUndefValue(C))
    return true;

  // Shifting by the bitwidth or more is poison. This covers scalars and
  // fixed/scalable vectors with splat constants.
  const APInt *AmountC;
  if (match(C, m_APInt(AmountC)) && AmountC->uge(AmountC->getBitWidth()))
    return true;

  // Try harder for fixed-length vectors:
  // If all lanes of a vector shift are poison, the whole shift is poison.
  if (isa<ConstantVector>(C) || isa<ConstantDataVector>(C)) {
    for (unsigned I = 0,
                  E = cast<FixedVectorType>(C->getType())->getNumElements();
         I != E; ++I)
      if (!isPoisonShift(C->getAggregateElement(I), Q))
        return false;
    return true;
  }

  return false;
}

/// Given operands for an Shl, LShr or AShr, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyShift(Instruction::BinaryOps Opcode, Value *Op0,
                            Value *Op1, bool IsNSW, const SimplifyQuery &Q,
                            unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Opcode, Op0, Op1, Q))
    return C;

  // poison shift by X -> poison
  if (isa<PoisonValue>(Op0))
    return Op0;

  // 0 shift by X -> 0
  if (match(Op0, m_Zero()))
    return Constant::getNullValue(Op0->getType());

  // X shift by 0 -> X
  // Shift-by-sign-extended bool must be shift-by-0 because shift-by-all-ones
  // would be poison.
  Value *X;
  if (match(Op1, m_Zero()) ||
      (match(Op1, m_SExt(m_Value(X))) && X->getType()->isIntOrIntVectorTy(1)))
    return Op0;

  // Fold undefined shifts.
  if (isPoisonShift(Op1, Q))
    return PoisonValue::get(Op0->getType());

  // If the operation is with the result of a select instruction, check whether
  // operating on either branch of the select always yields the same value.
  if (isa<SelectInst>(Op0) || isa<SelectInst>(Op1))
    if (Value *V = threadBinOpOverSelect(Opcode, Op0, Op1, Q, MaxRecurse))
      return V;

  // If the operation is with the result of a phi instruction, check whether
  // operating on all incoming values of the phi always yields the same value.
  if (isa<PHINode>(Op0) || isa<PHINode>(Op1))
    if (Value *V = threadBinOpOverPHI(Opcode, Op0, Op1, Q, MaxRecurse))
      return V;

  // If any bits in the shift amount make that value greater than or equal to
  // the number of bits in the type, the shift is undefined.
  KnownBits KnownAmt = computeKnownBits(Op1, /* Depth */ 0, Q);
  if (KnownAmt.getMinValue().uge(KnownAmt.getBitWidth()))
    return PoisonValue::get(Op0->getType());

  // If all valid bits in the shift amount are known zero, the first operand is
  // unchanged.
  unsigned NumValidShiftBits = Log2_32_Ceil(KnownAmt.getBitWidth());
  if (KnownAmt.countMinTrailingZeros() >= NumValidShiftBits)
    return Op0;

  // Check for nsw shl leading to a poison value.
  if (IsNSW) {
    assert(Opcode == Instruction::Shl && "Expected shl for nsw instruction");
    KnownBits KnownVal = computeKnownBits(Op0, /* Depth */ 0, Q);
    KnownBits KnownShl = KnownBits::shl(KnownVal, KnownAmt);

    if (KnownVal.Zero.isSignBitSet())
      KnownShl.Zero.setSignBit();
    if (KnownVal.One.isSignBitSet())
      KnownShl.One.setSignBit();

    if (KnownShl.hasConflict())
      return PoisonValue::get(Op0->getType());
  }

  return nullptr;
}

/// Given operands for an LShr or AShr, see if we can fold the result.  If not,
/// this returns null.
static Value *simplifyRightShift(Instruction::BinaryOps Opcode, Value *Op0,
                                 Value *Op1, bool IsExact,
                                 const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Value *V =
          simplifyShift(Opcode, Op0, Op1, /*IsNSW*/ false, Q, MaxRecurse))
    return V;

  // X >> X -> 0
  if (Op0 == Op1)
    return Constant::getNullValue(Op0->getType());

  // undef >> X -> 0
  // undef >> X -> undef (if it's exact)
  if (Q.isUndefValue(Op0))
    return IsExact ? Op0 : Constant::getNullValue(Op0->getType());

  // The low bit cannot be shifted out of an exact shift if it is set.
  // TODO: Generalize by counting trailing zeros (see fold for exact division).
  if (IsExact) {
    KnownBits Op0Known = computeKnownBits(Op0, /* Depth */ 0, Q);
    if (Op0Known.One[0])
      return Op0;
  }

  return nullptr;
}

/// Given operands for an Shl, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyShlInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                              const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Value *V =
          simplifyShift(Instruction::Shl, Op0, Op1, IsNSW, Q, MaxRecurse))
    return V;

  Type *Ty = Op0->getType();
  // undef << X -> 0
  // undef << X -> undef if (if it's NSW/NUW)
  if (Q.isUndefValue(Op0))
    return IsNSW || IsNUW ? Op0 : Constant::getNullValue(Ty);

  // (X >> A) << A -> X
  Value *X;
  if (Q.IIQ.UseInstrInfo &&
      match(Op0, m_Exact(m_Shr(m_Value(X), m_Specific(Op1)))))
    return X;

  // shl nuw i8 C, %x  ->  C  iff C has sign bit set.
  if (IsNUW && match(Op0, m_Negative()))
    return Op0;
  // NOTE: could use computeKnownBits() / LazyValueInfo,
  // but the cost-benefit analysis suggests it isn't worth it.

  // "nuw" guarantees that only zeros are shifted out, and "nsw" guarantees
  // that the sign-bit does not change, so the only input that does not
  // produce poison is 0, and "0 << (bitwidth-1) --> 0".
  if (IsNSW && IsNUW &&
      match(Op1, m_SpecificInt(Ty->getScalarSizeInBits() - 1)))
    return Constant::getNullValue(Ty);

  return nullptr;
}

Value *llvm::simplifyShlInst(Value *Op0, Value *Op1, bool IsNSW, bool IsNUW,
                             const SimplifyQuery &Q) {
  return ::simplifyShlInst(Op0, Op1, IsNSW, IsNUW, Q, RecursionLimit);
}

/// Given operands for an LShr, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyLShrInst(Value *Op0, Value *Op1, bool IsExact,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Value *V = simplifyRightShift(Instruction::LShr, Op0, Op1, IsExact, Q,
                                    MaxRecurse))
    return V;

  // (X << A) >> A -> X
  Value *X;
  if (Q.IIQ.UseInstrInfo && match(Op0, m_NUWShl(m_Value(X), m_Specific(Op1))))
    return X;

  // ((X << A) | Y) >> A -> X  if effective width of Y is not larger than A.
  // We can return X as we do in the above case since OR alters no bits in X.
  // SimplifyDemandedBits in InstCombine can do more general optimization for
  // bit manipulation. This pattern aims to provide opportunities for other
  // optimizers by supporting a simple but common case in InstSimplify.
  Value *Y;
  const APInt *ShRAmt, *ShLAmt;
  if (Q.IIQ.UseInstrInfo && match(Op1, m_APInt(ShRAmt)) &&
      match(Op0, m_c_Or(m_NUWShl(m_Value(X), m_APInt(ShLAmt)), m_Value(Y))) &&
      *ShRAmt == *ShLAmt) {
    const KnownBits YKnown = computeKnownBits(Y, /* Depth */ 0, Q);
    const unsigned EffWidthY = YKnown.countMaxActiveBits();
    if (ShRAmt->uge(EffWidthY))
      return X;
  }

  return nullptr;
}

Value *llvm::simplifyLShrInst(Value *Op0, Value *Op1, bool IsExact,
                              const SimplifyQuery &Q) {
  return ::simplifyLShrInst(Op0, Op1, IsExact, Q, RecursionLimit);
}

/// Given operands for an AShr, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyAShrInst(Value *Op0, Value *Op1, bool IsExact,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Value *V = simplifyRightShift(Instruction::AShr, Op0, Op1, IsExact, Q,
                                    MaxRecurse))
    return V;

  // -1 >>a X --> -1
  // (-1 << X) a>> X --> -1
  // We could return the original -1 constant to preserve poison elements.
  if (match(Op0, m_AllOnes()) ||
      match(Op0, m_Shl(m_AllOnes(), m_Specific(Op1))))
    return Constant::getAllOnesValue(Op0->getType());

  // (X << A) >> A -> X
  Value *X;
  if (Q.IIQ.UseInstrInfo && match(Op0, m_NSWShl(m_Value(X), m_Specific(Op1))))
    return X;

  // Arithmetic shifting an all-sign-bit value is a no-op.
  unsigned NumSignBits = ComputeNumSignBits(Op0, Q.DL, 0, Q.AC, Q.CxtI, Q.DT);
  if (NumSignBits == Op0->getType()->getScalarSizeInBits())
    return Op0;

  return nullptr;
}

Value *llvm::simplifyAShrInst(Value *Op0, Value *Op1, bool IsExact,
                              const SimplifyQuery &Q) {
  return ::simplifyAShrInst(Op0, Op1, IsExact, Q, RecursionLimit);
}

/// Commuted variants are assumed to be handled by calling this function again
/// with the parameters swapped.
static Value *simplifyUnsignedRangeCheck(ICmpInst *ZeroICmp,
                                         ICmpInst *UnsignedICmp, bool IsAnd,
                                         const SimplifyQuery &Q) {
  Value *X, *Y;

  ICmpInst::Predicate EqPred;
  if (!match(ZeroICmp, m_ICmp(EqPred, m_Value(Y), m_Zero())) ||
      !ICmpInst::isEquality(EqPred))
    return nullptr;

  ICmpInst::Predicate UnsignedPred;

  Value *A, *B;
  // Y = (A - B);
  if (match(Y, m_Sub(m_Value(A), m_Value(B)))) {
    if (match(UnsignedICmp,
              m_c_ICmp(UnsignedPred, m_Specific(A), m_Specific(B))) &&
        ICmpInst::isUnsigned(UnsignedPred)) {
      // A >=/<= B || (A - B) != 0  <-->  true
      if ((UnsignedPred == ICmpInst::ICMP_UGE ||
           UnsignedPred == ICmpInst::ICMP_ULE) &&
          EqPred == ICmpInst::ICMP_NE && !IsAnd)
        return ConstantInt::getTrue(UnsignedICmp->getType());
      // A </> B && (A - B) == 0  <-->  false
      if ((UnsignedPred == ICmpInst::ICMP_ULT ||
           UnsignedPred == ICmpInst::ICMP_UGT) &&
          EqPred == ICmpInst::ICMP_EQ && IsAnd)
        return ConstantInt::getFalse(UnsignedICmp->getType());

      // A </> B && (A - B) != 0  <-->  A </> B
      // A </> B || (A - B) != 0  <-->  (A - B) != 0
      if (EqPred == ICmpInst::ICMP_NE && (UnsignedPred == ICmpInst::ICMP_ULT ||
                                          UnsignedPred == ICmpInst::ICMP_UGT))
        return IsAnd ? UnsignedICmp : ZeroICmp;

      // A <=/>= B && (A - B) == 0  <-->  (A - B) == 0
      // A <=/>= B || (A - B) == 0  <-->  A <=/>= B
      if (EqPred == ICmpInst::ICMP_EQ && (UnsignedPred == ICmpInst::ICMP_ULE ||
                                          UnsignedPred == ICmpInst::ICMP_UGE))
        return IsAnd ? ZeroICmp : UnsignedICmp;
    }

    // Given  Y = (A - B)
    //   Y >= A && Y != 0  --> Y >= A  iff B != 0
    //   Y <  A || Y == 0  --> Y <  A  iff B != 0
    if (match(UnsignedICmp,
              m_c_ICmp(UnsignedPred, m_Specific(Y), m_Specific(A)))) {
      if (UnsignedPred == ICmpInst::ICMP_UGE && IsAnd &&
          EqPred == ICmpInst::ICMP_NE && isKnownNonZero(B, Q))
        return UnsignedICmp;
      if (UnsignedPred == ICmpInst::ICMP_ULT && !IsAnd &&
          EqPred == ICmpInst::ICMP_EQ && isKnownNonZero(B, Q))
        return UnsignedICmp;
    }
  }

  if (match(UnsignedICmp, m_ICmp(UnsignedPred, m_Value(X), m_Specific(Y))) &&
      ICmpInst::isUnsigned(UnsignedPred))
    ;
  else if (match(UnsignedICmp,
                 m_ICmp(UnsignedPred, m_Specific(Y), m_Value(X))) &&
           ICmpInst::isUnsigned(UnsignedPred))
    UnsignedPred = ICmpInst::getSwappedPredicate(UnsignedPred);
  else
    return nullptr;

  // X > Y && Y == 0  -->  Y == 0  iff X != 0
  // X > Y || Y == 0  -->  X > Y   iff X != 0
  if (UnsignedPred == ICmpInst::ICMP_UGT && EqPred == ICmpInst::ICMP_EQ &&
      isKnownNonZero(X, Q))
    return IsAnd ? ZeroICmp : UnsignedICmp;

  // X <= Y && Y != 0  -->  X <= Y  iff X != 0
  // X <= Y || Y != 0  -->  Y != 0  iff X != 0
  if (UnsignedPred == ICmpInst::ICMP_ULE && EqPred == ICmpInst::ICMP_NE &&
      isKnownNonZero(X, Q))
    return IsAnd ? UnsignedICmp : ZeroICmp;

  // The transforms below here are expected to be handled more generally with
  // simplifyAndOrOfICmpsWithLimitConst() or in InstCombine's
  // foldAndOrOfICmpsWithConstEq(). If we are looking to trim optimizer overlap,
  // these are candidates for removal.

  // X < Y && Y != 0  -->  X < Y
  // X < Y || Y != 0  -->  Y != 0
  if (UnsignedPred == ICmpInst::ICMP_ULT && EqPred == ICmpInst::ICMP_NE)
    return IsAnd ? UnsignedICmp : ZeroICmp;

  // X >= Y && Y == 0  -->  Y == 0
  // X >= Y || Y == 0  -->  X >= Y
  if (UnsignedPred == ICmpInst::ICMP_UGE && EqPred == ICmpInst::ICMP_EQ)
    return IsAnd ? ZeroICmp : UnsignedICmp;

  // X < Y && Y == 0  -->  false
  if (UnsignedPred == ICmpInst::ICMP_ULT && EqPred == ICmpInst::ICMP_EQ &&
      IsAnd)
    return getFalse(UnsignedICmp->getType());

  // X >= Y || Y != 0  -->  true
  if (UnsignedPred == ICmpInst::ICMP_UGE && EqPred == ICmpInst::ICMP_NE &&
      !IsAnd)
    return getTrue(UnsignedICmp->getType());

  return nullptr;
}

/// Test if a pair of compares with a shared operand and 2 constants has an
/// empty set intersection, full set union, or if one compare is a superset of
/// the other.
static Value *simplifyAndOrOfICmpsWithConstants(ICmpInst *Cmp0, ICmpInst *Cmp1,
                                                bool IsAnd) {
  // Look for this pattern: {and/or} (icmp X, C0), (icmp X, C1)).
  if (Cmp0->getOperand(0) != Cmp1->getOperand(0))
    return nullptr;

  const APInt *C0, *C1;
  if (!match(Cmp0->getOperand(1), m_APInt(C0)) ||
      !match(Cmp1->getOperand(1), m_APInt(C1)))
    return nullptr;

  auto Range0 = ConstantRange::makeExactICmpRegion(Cmp0->getPredicate(), *C0);
  auto Range1 = ConstantRange::makeExactICmpRegion(Cmp1->getPredicate(), *C1);

  // For and-of-compares, check if the intersection is empty:
  // (icmp X, C0) && (icmp X, C1) --> empty set --> false
  if (IsAnd && Range0.intersectWith(Range1).isEmptySet())
    return getFalse(Cmp0->getType());

  // For or-of-compares, check if the union is full:
  // (icmp X, C0) || (icmp X, C1) --> full set --> true
  if (!IsAnd && Range0.unionWith(Range1).isFullSet())
    return getTrue(Cmp0->getType());

  // Is one range a superset of the other?
  // If this is and-of-compares, take the smaller set:
  // (icmp sgt X, 4) && (icmp sgt X, 42) --> icmp sgt X, 42
  // If this is or-of-compares, take the larger set:
  // (icmp sgt X, 4) || (icmp sgt X, 42) --> icmp sgt X, 4
  if (Range0.contains(Range1))
    return IsAnd ? Cmp1 : Cmp0;
  if (Range1.contains(Range0))
    return IsAnd ? Cmp0 : Cmp1;

  return nullptr;
}

static Value *simplifyAndOfICmpsWithAdd(ICmpInst *Op0, ICmpInst *Op1,
                                        const InstrInfoQuery &IIQ) {
  // (icmp (add V, C0), C1) & (icmp V, C0)
  ICmpInst::Predicate Pred0, Pred1;
  const APInt *C0, *C1;
  Value *V;
  if (!match(Op0, m_ICmp(Pred0, m_Add(m_Value(V), m_APInt(C0)), m_APInt(C1))))
    return nullptr;

  if (!match(Op1, m_ICmp(Pred1, m_Specific(V), m_Value())))
    return nullptr;

  auto *AddInst = cast<OverflowingBinaryOperator>(Op0->getOperand(0));
  if (AddInst->getOperand(1) != Op1->getOperand(1))
    return nullptr;

  Type *ITy = Op0->getType();
  bool IsNSW = IIQ.hasNoSignedWrap(AddInst);
  bool IsNUW = IIQ.hasNoUnsignedWrap(AddInst);

  const APInt Delta = *C1 - *C0;
  if (C0->isStrictlyPositive()) {
    if (Delta == 2) {
      if (Pred0 == ICmpInst::ICMP_ULT && Pred1 == ICmpInst::ICMP_SGT)
        return getFalse(ITy);
      if (Pred0 == ICmpInst::ICMP_SLT && Pred1 == ICmpInst::ICMP_SGT && IsNSW)
        return getFalse(ITy);
    }
    if (Delta == 1) {
      if (Pred0 == ICmpInst::ICMP_ULE && Pred1 == ICmpInst::ICMP_SGT)
        return getFalse(ITy);
      if (Pred0 == ICmpInst::ICMP_SLE && Pred1 == ICmpInst::ICMP_SGT && IsNSW)
        return getFalse(ITy);
    }
  }
  if (C0->getBoolValue() && IsNUW) {
    if (Delta == 2)
      if (Pred0 == ICmpInst::ICMP_ULT && Pred1 == ICmpInst::ICMP_UGT)
        return getFalse(ITy);
    if (Delta == 1)
      if (Pred0 == ICmpInst::ICMP_ULE && Pred1 == ICmpInst::ICMP_UGT)
        return getFalse(ITy);
  }

  return nullptr;
}

/// Try to simplify and/or of icmp with ctpop intrinsic.
static Value *simplifyAndOrOfICmpsWithCtpop(ICmpInst *Cmp0, ICmpInst *Cmp1,
                                            bool IsAnd) {
  ICmpInst::Predicate Pred0, Pred1;
  Value *X;
  const APInt *C;
  if (!match(Cmp0, m_ICmp(Pred0, m_Intrinsic<Intrinsic::ctpop>(m_Value(X)),
                          m_APInt(C))) ||
      !match(Cmp1, m_ICmp(Pred1, m_Specific(X), m_ZeroInt())) || C->isZero())
    return nullptr;

  // (ctpop(X) == C) || (X != 0) --> X != 0 where C > 0
  if (!IsAnd && Pred0 == ICmpInst::ICMP_EQ && Pred1 == ICmpInst::ICMP_NE)
    return Cmp1;
  // (ctpop(X) != C) && (X == 0) --> X == 0 where C > 0
  if (IsAnd && Pred0 == ICmpInst::ICMP_NE && Pred1 == ICmpInst::ICMP_EQ)
    return Cmp1;

  return nullptr;
}

static Value *simplifyAndOfICmps(ICmpInst *Op0, ICmpInst *Op1,
                                 const SimplifyQuery &Q) {
  if (Value *X = simplifyUnsignedRangeCheck(Op0, Op1, /*IsAnd=*/true, Q))
    return X;
  if (Value *X = simplifyUnsignedRangeCheck(Op1, Op0, /*IsAnd=*/true, Q))
    return X;

  if (Value *X = simplifyAndOrOfICmpsWithConstants(Op0, Op1, true))
    return X;

  if (Value *X = simplifyAndOrOfICmpsWithCtpop(Op0, Op1, true))
    return X;
  if (Value *X = simplifyAndOrOfICmpsWithCtpop(Op1, Op0, true))
    return X;

  if (Value *X = simplifyAndOfICmpsWithAdd(Op0, Op1, Q.IIQ))
    return X;
  if (Value *X = simplifyAndOfICmpsWithAdd(Op1, Op0, Q.IIQ))
    return X;

  return nullptr;
}

static Value *simplifyOrOfICmpsWithAdd(ICmpInst *Op0, ICmpInst *Op1,
                                       const InstrInfoQuery &IIQ) {
  // (icmp (add V, C0), C1) | (icmp V, C0)
  ICmpInst::Predicate Pred0, Pred1;
  const APInt *C0, *C1;
  Value *V;
  if (!match(Op0, m_ICmp(Pred0, m_Add(m_Value(V), m_APInt(C0)), m_APInt(C1))))
    return nullptr;

  if (!match(Op1, m_ICmp(Pred1, m_Specific(V), m_Value())))
    return nullptr;

  auto *AddInst = cast<BinaryOperator>(Op0->getOperand(0));
  if (AddInst->getOperand(1) != Op1->getOperand(1))
    return nullptr;

  Type *ITy = Op0->getType();
  bool IsNSW = IIQ.hasNoSignedWrap(AddInst);
  bool IsNUW = IIQ.hasNoUnsignedWrap(AddInst);

  const APInt Delta = *C1 - *C0;
  if (C0->isStrictlyPositive()) {
    if (Delta == 2) {
      if (Pred0 == ICmpInst::ICMP_UGE && Pred1 == ICmpInst::ICMP_SLE)
        return getTrue(ITy);
      if (Pred0 == ICmpInst::ICMP_SGE && Pred1 == ICmpInst::ICMP_SLE && IsNSW)
        return getTrue(ITy);
    }
    if (Delta == 1) {
      if (Pred0 == ICmpInst::ICMP_UGT && Pred1 == ICmpInst::ICMP_SLE)
        return getTrue(ITy);
      if (Pred0 == ICmpInst::ICMP_SGT && Pred1 == ICmpInst::ICMP_SLE && IsNSW)
        return getTrue(ITy);
    }
  }
  if (C0->getBoolValue() && IsNUW) {
    if (Delta == 2)
      if (Pred0 == ICmpInst::ICMP_UGE && Pred1 == ICmpInst::ICMP_ULE)
        return getTrue(ITy);
    if (Delta == 1)
      if (Pred0 == ICmpInst::ICMP_UGT && Pred1 == ICmpInst::ICMP_ULE)
        return getTrue(ITy);
  }

  return nullptr;
}

static Value *simplifyOrOfICmps(ICmpInst *Op0, ICmpInst *Op1,
                                const SimplifyQuery &Q) {
  if (Value *X = simplifyUnsignedRangeCheck(Op0, Op1, /*IsAnd=*/false, Q))
    return X;
  if (Value *X = simplifyUnsignedRangeCheck(Op1, Op0, /*IsAnd=*/false, Q))
    return X;

  if (Value *X = simplifyAndOrOfICmpsWithConstants(Op0, Op1, false))
    return X;

  if (Value *X = simplifyAndOrOfICmpsWithCtpop(Op0, Op1, false))
    return X;
  if (Value *X = simplifyAndOrOfICmpsWithCtpop(Op1, Op0, false))
    return X;

  if (Value *X = simplifyOrOfICmpsWithAdd(Op0, Op1, Q.IIQ))
    return X;
  if (Value *X = simplifyOrOfICmpsWithAdd(Op1, Op0, Q.IIQ))
    return X;

  return nullptr;
}

static Value *simplifyAndOrOfFCmps(const SimplifyQuery &Q, FCmpInst *LHS,
                                   FCmpInst *RHS, bool IsAnd) {
  Value *LHS0 = LHS->getOperand(0), *LHS1 = LHS->getOperand(1);
  Value *RHS0 = RHS->getOperand(0), *RHS1 = RHS->getOperand(1);
  if (LHS0->getType() != RHS0->getType())
    return nullptr;

  FCmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();
  if ((PredL == FCmpInst::FCMP_ORD || PredL == FCmpInst::FCMP_UNO) &&
      ((FCmpInst::isOrdered(PredR) && IsAnd) ||
       (FCmpInst::isUnordered(PredR) && !IsAnd))) {
    // (fcmp ord X, 0) & (fcmp o** X, Y) --> fcmp o** X, Y
    // (fcmp uno X, 0) & (fcmp o** X, Y) --> false
    // (fcmp uno X, 0) | (fcmp u** X, Y) --> fcmp u** X, Y
    // (fcmp ord X, 0) | (fcmp u** X, Y) --> true
    if ((LHS0 == RHS0 || LHS0 == RHS1) && match(LHS1, m_PosZeroFP()))
      return FCmpInst::isOrdered(PredL) == FCmpInst::isOrdered(PredR)
                 ? static_cast<Value *>(RHS)
                 : ConstantInt::getBool(LHS->getType(), !IsAnd);
  }

  if ((PredR == FCmpInst::FCMP_ORD || PredR == FCmpInst::FCMP_UNO) &&
      ((FCmpInst::isOrdered(PredL) && IsAnd) ||
       (FCmpInst::isUnordered(PredL) && !IsAnd))) {
    // (fcmp o** X, Y) & (fcmp ord X, 0) --> fcmp o** X, Y
    // (fcmp o** X, Y) & (fcmp uno X, 0) --> false
    // (fcmp u** X, Y) | (fcmp uno X, 0) --> fcmp u** X, Y
    // (fcmp u** X, Y) | (fcmp ord X, 0) --> true
    if ((RHS0 == LHS0 || RHS0 == LHS1) && match(RHS1, m_PosZeroFP()))
      return FCmpInst::isOrdered(PredL) == FCmpInst::isOrdered(PredR)
                 ? static_cast<Value *>(LHS)
                 : ConstantInt::getBool(LHS->getType(), !IsAnd);
  }

  return nullptr;
}

static Value *simplifyAndOrOfCmps(const SimplifyQuery &Q, Value *Op0,
                                  Value *Op1, bool IsAnd) {
  // Look through casts of the 'and' operands to find compares.
  auto *Cast0 = dyn_cast<CastInst>(Op0);
  auto *Cast1 = dyn_cast<CastInst>(Op1);
  if (Cast0 && Cast1 && Cast0->getOpcode() == Cast1->getOpcode() &&
      Cast0->getSrcTy() == Cast1->getSrcTy()) {
    Op0 = Cast0->getOperand(0);
    Op1 = Cast1->getOperand(0);
  }

  Value *V = nullptr;
  auto *ICmp0 = dyn_cast<ICmpInst>(Op0);
  auto *ICmp1 = dyn_cast<ICmpInst>(Op1);
  if (ICmp0 && ICmp1)
    V = IsAnd ? simplifyAndOfICmps(ICmp0, ICmp1, Q)
              : simplifyOrOfICmps(ICmp0, ICmp1, Q);

  auto *FCmp0 = dyn_cast<FCmpInst>(Op0);
  auto *FCmp1 = dyn_cast<FCmpInst>(Op1);
  if (FCmp0 && FCmp1)
    V = simplifyAndOrOfFCmps(Q, FCmp0, FCmp1, IsAnd);

  if (!V)
    return nullptr;
  if (!Cast0)
    return V;

  // If we looked through casts, we can only handle a constant simplification
  // because we are not allowed to create a cast instruction here.
  if (auto *C = dyn_cast<Constant>(V))
    return ConstantFoldCastOperand(Cast0->getOpcode(), C, Cast0->getType(),
                                   Q.DL);

  return nullptr;
}

static Value *simplifyWithOpReplaced(Value *V, Value *Op, Value *RepOp,
                                     const SimplifyQuery &Q,
                                     bool AllowRefinement,
                                     SmallVectorImpl<Instruction *> *DropFlags,
                                     unsigned MaxRecurse);

static Value *simplifyAndOrWithICmpEq(unsigned Opcode, Value *Op0, Value *Op1,
                                      const SimplifyQuery &Q,
                                      unsigned MaxRecurse) {
  assert((Opcode == Instruction::And || Opcode == Instruction::Or) &&
         "Must be and/or");
  ICmpInst::Predicate Pred;
  Value *A, *B;
  if (!match(Op0, m_ICmp(Pred, m_Value(A), m_Value(B))) ||
      !ICmpInst::isEquality(Pred))
    return nullptr;

  auto Simplify = [&](Value *Res) -> Value * {
    Constant *Absorber = ConstantExpr::getBinOpAbsorber(Opcode, Res->getType());

    // and (icmp eq a, b), x implies (a==b) inside x.
    // or (icmp ne a, b), x implies (a==b) inside x.
    // If x simplifies to true/false, we can simplify the and/or.
    if (Pred ==
        (Opcode == Instruction::And ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE)) {
      if (Res == Absorber)
        return Absorber;
      if (Res == ConstantExpr::getBinOpIdentity(Opcode, Res->getType()))
        return Op0;
      return nullptr;
    }

    // If we have and (icmp ne a, b), x and for a==b we can simplify x to false,
    // then we can drop the icmp, as x will already be false in the case where
    // the icmp is false. Similar for or and true.
    if (Res == Absorber)
      return Op1;
    return nullptr;
  };

  // In the final case (Res == Absorber with inverted predicate), it is safe to
  // refine poison during simplification, but not undef. For simplicity always
  // disable undef-based folds here.
  if (Value *Res = simplifyWithOpReplaced(Op1, A, B, Q.getWithoutUndef(),
                                          /* AllowRefinement */ true,
                                          /* DropFlags */ nullptr, MaxRecurse))
    return Simplify(Res);
  if (Value *Res = simplifyWithOpReplaced(Op1, B, A, Q.getWithoutUndef(),
                                          /* AllowRefinement */ true,
                                          /* DropFlags */ nullptr, MaxRecurse))
    return Simplify(Res);

  return nullptr;
}

/// Given a bitwise logic op, check if the operands are add/sub with a common
/// source value and inverted constant (identity: C - X -> ~(X + ~C)).
static Value *simplifyLogicOfAddSub(Value *Op0, Value *Op1,
                                    Instruction::BinaryOps Opcode) {
  assert(Op0->getType() == Op1->getType() && "Mismatched binop types");
  assert(BinaryOperator::isBitwiseLogicOp(Opcode) && "Expected logic op");
  Value *X;
  Constant *C1, *C2;
  if ((match(Op0, m_Add(m_Value(X), m_Constant(C1))) &&
       match(Op1, m_Sub(m_Constant(C2), m_Specific(X)))) ||
      (match(Op1, m_Add(m_Value(X), m_Constant(C1))) &&
       match(Op0, m_Sub(m_Constant(C2), m_Specific(X))))) {
    if (ConstantExpr::getNot(C1) == C2) {
      // (X + C) & (~C - X) --> (X + C) & ~(X + C) --> 0
      // (X + C) | (~C - X) --> (X + C) | ~(X + C) --> -1
      // (X + C) ^ (~C - X) --> (X + C) ^ ~(X + C) --> -1
      Type *Ty = Op0->getType();
      return Opcode == Instruction::And ? ConstantInt::getNullValue(Ty)
                                        : ConstantInt::getAllOnesValue(Ty);
    }
  }
  return nullptr;
}

// Commutative patterns for and that will be tried with both operand orders.
static Value *simplifyAndCommutative(Value *Op0, Value *Op1,
                                     const SimplifyQuery &Q,
                                     unsigned MaxRecurse) {
  // ~A & A =  0
  if (match(Op0, m_Not(m_Specific(Op1))))
    return Constant::getNullValue(Op0->getType());

  // (A | ?) & A = A
  if (match(Op0, m_c_Or(m_Specific(Op1), m_Value())))
    return Op1;

  // (X | ~Y) & (X | Y) --> X
  Value *X, *Y;
  if (match(Op0, m_c_Or(m_Value(X), m_Not(m_Value(Y)))) &&
      match(Op1, m_c_Or(m_Specific(X), m_Specific(Y))))
    return X;

  // If we have a multiplication overflow check that is being 'and'ed with a
  // check that one of the multipliers is not zero, we can omit the 'and', and
  // only keep the overflow check.
  if (isCheckForZeroAndMulWithOverflow(Op0, Op1, true))
    return Op1;

  // -A & A = A if A is a power of two or zero.
  if (match(Op0, m_Neg(m_Specific(Op1))) &&
      isKnownToBeAPowerOfTwo(Op1, Q.DL, /*OrZero*/ true, 0, Q.AC, Q.CxtI, Q.DT))
    return Op1;

  // This is a similar pattern used for checking if a value is a power-of-2:
  // (A - 1) & A --> 0 (if A is a power-of-2 or 0)
  if (match(Op0, m_Add(m_Specific(Op1), m_AllOnes())) &&
      isKnownToBeAPowerOfTwo(Op1, Q.DL, /*OrZero*/ true, 0, Q.AC, Q.CxtI, Q.DT))
    return Constant::getNullValue(Op1->getType());

  // (x << N) & ((x << M) - 1) --> 0, where x is known to be a power of 2 and
  // M <= N.
  const APInt *Shift1, *Shift2;
  if (match(Op0, m_Shl(m_Value(X), m_APInt(Shift1))) &&
      match(Op1, m_Add(m_Shl(m_Specific(X), m_APInt(Shift2)), m_AllOnes())) &&
      isKnownToBeAPowerOfTwo(X, Q.DL, /*OrZero*/ true, /*Depth*/ 0, Q.AC,
                             Q.CxtI) &&
      Shift1->uge(*Shift2))
    return Constant::getNullValue(Op0->getType());

  if (Value *V =
          simplifyAndOrWithICmpEq(Instruction::And, Op0, Op1, Q, MaxRecurse))
    return V;

  return nullptr;
}

/// Given operands for an And, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyAndInst(Value *Op0, Value *Op1, const SimplifyQuery &Q,
                              unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Instruction::And, Op0, Op1, Q))
    return C;

  // X & poison -> poison
  if (isa<PoisonValue>(Op1))
    return Op1;

  // X & undef -> 0
  if (Q.isUndefValue(Op1))
    return Constant::getNullValue(Op0->getType());

  // X & X = X
  if (Op0 == Op1)
    return Op0;

  // X & 0 = 0
  if (match(Op1, m_Zero()))
    return Constant::getNullValue(Op0->getType());

  // X & -1 = X
  if (match(Op1, m_AllOnes()))
    return Op0;

  if (Value *Res = simplifyAndCommutative(Op0, Op1, Q, MaxRecurse))
    return Res;
  if (Value *Res = simplifyAndCommutative(Op1, Op0, Q, MaxRecurse))
    return Res;

  if (Value *V = simplifyLogicOfAddSub(Op0, Op1, Instruction::And))
    return V;

  // A mask that only clears known zeros of a shifted value is a no-op.
  const APInt *Mask;
  const APInt *ShAmt;
  Value *X, *Y;
  if (match(Op1, m_APInt(Mask))) {
    // If all bits in the inverted and shifted mask are clear:
    // and (shl X, ShAmt), Mask --> shl X, ShAmt
    if (match(Op0, m_Shl(m_Value(X), m_APInt(ShAmt))) &&
        (~(*Mask)).lshr(*ShAmt).isZero())
      return Op0;

    // If all bits in the inverted and shifted mask are clear:
    // and (lshr X, ShAmt), Mask --> lshr X, ShAmt
    if (match(Op0, m_LShr(m_Value(X), m_APInt(ShAmt))) &&
        (~(*Mask)).shl(*ShAmt).isZero())
      return Op0;
  }

  // and 2^x-1, 2^C --> 0 where x <= C.
  const APInt *PowerC;
  Value *Shift;
  if (match(Op1, m_Power2(PowerC)) &&
      match(Op0, m_Add(m_Value(Shift), m_AllOnes())) &&
      isKnownToBeAPowerOfTwo(Shift, Q.DL, /*OrZero*/ false, 0, Q.AC, Q.CxtI,
                             Q.DT)) {
    KnownBits Known = computeKnownBits(Shift, /* Depth */ 0, Q);
    // Use getActiveBits() to make use of the additional power of two knowledge
    if (PowerC->getActiveBits() >= Known.getMaxValue().getActiveBits())
      return ConstantInt::getNullValue(Op1->getType());
  }

  if (Value *V = simplifyAndOrOfCmps(Q, Op0, Op1, true))
    return V;

  // Try some generic simplifications for associative operations.
  if (Value *V =
          simplifyAssociativeBinOp(Instruction::And, Op0, Op1, Q, MaxRecurse))
    return V;

  // And distributes over Or.  Try some generic simplifications based on this.
  if (Value *V = expandCommutativeBinOp(Instruction::And, Op0, Op1,
                                        Instruction::Or, Q, MaxRecurse))
    return V;

  // And distributes over Xor.  Try some generic simplifications based on this.
  if (Value *V = expandCommutativeBinOp(Instruction::And, Op0, Op1,
                                        Instruction::Xor, Q, MaxRecurse))
    return V;

  if (isa<SelectInst>(Op0) || isa<SelectInst>(Op1)) {
    if (Op0->getType()->isIntOrIntVectorTy(1)) {
      // A & (A && B) -> A && B
      if (match(Op1, m_Select(m_Specific(Op0), m_Value(), m_Zero())))
        return Op1;
      else if (match(Op0, m_Select(m_Specific(Op1), m_Value(), m_Zero())))
        return Op0;
    }
    // If the operation is with the result of a select instruction, check
    // whether operating on either branch of the select always yields the same
    // value.
    if (Value *V =
            threadBinOpOverSelect(Instruction::And, Op0, Op1, Q, MaxRecurse))
      return V;
  }

  // If the operation is with the result of a phi instruction, check whether
  // operating on all incoming values of the phi always yields the same value.
  if (isa<PHINode>(Op0) || isa<PHINode>(Op1))
    if (Value *V =
            threadBinOpOverPHI(Instruction::And, Op0, Op1, Q, MaxRecurse))
      return V;

  // Assuming the effective width of Y is not larger than A, i.e. all bits
  // from X and Y are disjoint in (X << A) | Y,
  // if the mask of this AND op covers all bits of X or Y, while it covers
  // no bits from the other, we can bypass this AND op. E.g.,
  // ((X << A) | Y) & Mask -> Y,
  //     if Mask = ((1 << effective_width_of(Y)) - 1)
  // ((X << A) | Y) & Mask -> X << A,
  //     if Mask = ((1 << effective_width_of(X)) - 1) << A
  // SimplifyDemandedBits in InstCombine can optimize the general case.
  // This pattern aims to help other passes for a common case.
  Value *XShifted;
  if (Q.IIQ.UseInstrInfo && match(Op1, m_APInt(Mask)) &&
      match(Op0, m_c_Or(m_CombineAnd(m_NUWShl(m_Value(X), m_APInt(ShAmt)),
                                     m_Value(XShifted)),
                        m_Value(Y)))) {
    const unsigned Width = Op0->getType()->getScalarSizeInBits();
    const unsigned ShftCnt = ShAmt->getLimitedValue(Width);
    const KnownBits YKnown = computeKnownBits(Y, /* Depth */ 0, Q);
    const unsigned EffWidthY = YKnown.countMaxActiveBits();
    if (EffWidthY <= ShftCnt) {
      const KnownBits XKnown = computeKnownBits(X, /* Depth */ 0, Q);
      const unsigned EffWidthX = XKnown.countMaxActiveBits();
      const APInt EffBitsY = APInt::getLowBitsSet(Width, EffWidthY);
      const APInt EffBitsX = APInt::getLowBitsSet(Width, EffWidthX) << ShftCnt;
      // If the mask is extracting all bits from X or Y as is, we can skip
      // this AND op.
      if (EffBitsY.isSubsetOf(*Mask) && !EffBitsX.intersects(*Mask))
        return Y;
      if (EffBitsX.isSubsetOf(*Mask) && !EffBitsY.intersects(*Mask))
        return XShifted;
    }
  }

  // ((X | Y) ^ X ) & ((X | Y) ^ Y) --> 0
  // ((X | Y) ^ Y ) & ((X | Y) ^ X) --> 0
  BinaryOperator *Or;
  if (match(Op0, m_c_Xor(m_Value(X),
                         m_CombineAnd(m_BinOp(Or),
                                      m_c_Or(m_Deferred(X), m_Value(Y))))) &&
      match(Op1, m_c_Xor(m_Specific(Or), m_Specific(Y))))
    return Constant::getNullValue(Op0->getType());

  const APInt *C1;
  Value *A;
  // (A ^ C) & (A ^ ~C) -> 0
  if (match(Op0, m_Xor(m_Value(A), m_APInt(C1))) &&
      match(Op1, m_Xor(m_Specific(A), m_SpecificInt(~*C1))))
    return Constant::getNullValue(Op0->getType());

  if (Op0->getType()->isIntOrIntVectorTy(1)) {
    if (std::optional<bool> Implied = isImpliedCondition(Op0, Op1, Q.DL)) {
      // If Op0 is true implies Op1 is true, then Op0 is a subset of Op1.
      if (*Implied == true)
        return Op0;
      // If Op0 is true implies Op1 is false, then they are not true together.
      if (*Implied == false)
        return ConstantInt::getFalse(Op0->getType());
    }
    if (std::optional<bool> Implied = isImpliedCondition(Op1, Op0, Q.DL)) {
      // If Op1 is true implies Op0 is true, then Op1 is a subset of Op0.
      if (*Implied)
        return Op1;
      // If Op1 is true implies Op0 is false, then they are not true together.
      if (!*Implied)
        return ConstantInt::getFalse(Op1->getType());
    }
  }

  if (Value *V = simplifyByDomEq(Instruction::And, Op0, Op1, Q, MaxRecurse))
    return V;

  return nullptr;
}

Value *llvm::simplifyAndInst(Value *Op0, Value *Op1, const SimplifyQuery &Q) {
  return ::simplifyAndInst(Op0, Op1, Q, RecursionLimit);
}

// TODO: Many of these folds could use LogicalAnd/LogicalOr.
static Value *simplifyOrLogic(Value *X, Value *Y) {
  assert(X->getType() == Y->getType() && "Expected same type for 'or' ops");
  Type *Ty = X->getType();

  // X | ~X --> -1
  if (match(Y, m_Not(m_Specific(X))))
    return ConstantInt::getAllOnesValue(Ty);

  // X | ~(X & ?) = -1
  if (match(Y, m_Not(m_c_And(m_Specific(X), m_Value()))))
    return ConstantInt::getAllOnesValue(Ty);

  // X | (X & ?) --> X
  if (match(Y, m_c_And(m_Specific(X), m_Value())))
    return X;

  Value *A, *B;

  // (A ^ B) | (A | B) --> A | B
  // (A ^ B) | (B | A) --> B | A
  if (match(X, m_Xor(m_Value(A), m_Value(B))) &&
      match(Y, m_c_Or(m_Specific(A), m_Specific(B))))
    return Y;

  // ~(A ^ B) | (A | B) --> -1
  // ~(A ^ B) | (B | A) --> -1
  if (match(X, m_Not(m_Xor(m_Value(A), m_Value(B)))) &&
      match(Y, m_c_Or(m_Specific(A), m_Specific(B))))
    return ConstantInt::getAllOnesValue(Ty);

  // (A & ~B) | (A ^ B) --> A ^ B
  // (~B & A) | (A ^ B) --> A ^ B
  // (A & ~B) | (B ^ A) --> B ^ A
  // (~B & A) | (B ^ A) --> B ^ A
  if (match(X, m_c_And(m_Value(A), m_Not(m_Value(B)))) &&
      match(Y, m_c_Xor(m_Specific(A), m_Specific(B))))
    return Y;

  // (~A ^ B) | (A & B) --> ~A ^ B
  // (B ^ ~A) | (A & B) --> B ^ ~A
  // (~A ^ B) | (B & A) --> ~A ^ B
  // (B ^ ~A) | (B & A) --> B ^ ~A
  if (match(X, m_c_Xor(m_Not(m_Value(A)), m_Value(B))) &&
      match(Y, m_c_And(m_Specific(A), m_Specific(B))))
    return X;

  // (~A | B) | (A ^ B) --> -1
  // (~A | B) | (B ^ A) --> -1
  // (B | ~A) | (A ^ B) --> -1
  // (B | ~A) | (B ^ A) --> -1
  if (match(X, m_c_Or(m_Not(m_Value(A)), m_Value(B))) &&
      match(Y, m_c_Xor(m_Specific(A), m_Specific(B))))
    return ConstantInt::getAllOnesValue(Ty);

  // (~A & B) | ~(A | B) --> ~A
  // (~A & B) | ~(B | A) --> ~A
  // (B & ~A) | ~(A | B) --> ~A
  // (B & ~A) | ~(B | A) --> ~A
  Value *NotA;
  if (match(X, m_c_And(m_CombineAnd(m_Value(NotA), m_Not(m_Value(A))),
                       m_Value(B))) &&
      match(Y, m_Not(m_c_Or(m_Specific(A), m_Specific(B)))))
    return NotA;
  // The same is true of Logical And
  // TODO: This could share the logic of the version above if there was a
  // version of LogicalAnd that allowed more than just i1 types.
  if (match(X, m_c_LogicalAnd(m_CombineAnd(m_Value(NotA), m_Not(m_Value(A))),
                              m_Value(B))) &&
      match(Y, m_Not(m_c_LogicalOr(m_Specific(A), m_Specific(B)))))
    return NotA;

  // ~(A ^ B) | (A & B) --> ~(A ^ B)
  // ~(A ^ B) | (B & A) --> ~(A ^ B)
  Value *NotAB;
  if (match(X, m_CombineAnd(m_Not(m_Xor(m_Value(A), m_Value(B))),
                            m_Value(NotAB))) &&
      match(Y, m_c_And(m_Specific(A), m_Specific(B))))
    return NotAB;

  // ~(A & B) | (A ^ B) --> ~(A & B)
  // ~(A & B) | (B ^ A) --> ~(A & B)
  if (match(X, m_CombineAnd(m_Not(m_And(m_Value(A), m_Value(B))),
                            m_Value(NotAB))) &&
      match(Y, m_c_Xor(m_Specific(A), m_Specific(B))))
    return NotAB;

  return nullptr;
}

/// Given operands for an Or, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyOrInst(Value *Op0, Value *Op1, const SimplifyQuery &Q,
                             unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Instruction::Or, Op0, Op1, Q))
    return C;

  // X | poison -> poison
  if (isa<PoisonValue>(Op1))
    return Op1;

  // X | undef -> -1
  // X | -1 = -1
  // Do not return Op1 because it may contain undef elements if it's a vector.
  if (Q.isUndefValue(Op1) || match(Op1, m_AllOnes()))
    return Constant::getAllOnesValue(Op0->getType());

  // X | X = X
  // X | 0 = X
  if (Op0 == Op1 || match(Op1, m_Zero()))
    return Op0;

  if (Value *R = simplifyOrLogic(Op0, Op1))
    return R;
  if (Value *R = simplifyOrLogic(Op1, Op0))
    return R;

  if (Value *V = simplifyLogicOfAddSub(Op0, Op1, Instruction::Or))
    return V;

  // Rotated -1 is still -1:
  // (-1 << X) | (-1 >> (C - X)) --> -1
  // (-1 >> X) | (-1 << (C - X)) --> -1
  // ...with C <= bitwidth (and commuted variants).
  Value *X, *Y;
  if ((match(Op0, m_Shl(m_AllOnes(), m_Value(X))) &&
       match(Op1, m_LShr(m_AllOnes(), m_Value(Y)))) ||
      (match(Op1, m_Shl(m_AllOnes(), m_Value(X))) &&
       match(Op0, m_LShr(m_AllOnes(), m_Value(Y))))) {
    const APInt *C;
    if ((match(X, m_Sub(m_APInt(C), m_Specific(Y))) ||
         match(Y, m_Sub(m_APInt(C), m_Specific(X)))) &&
        C->ule(X->getType()->getScalarSizeInBits())) {
      return ConstantInt::getAllOnesValue(X->getType());
    }
  }

  // A funnel shift (rotate) can be decomposed into simpler shifts. See if we
  // are mixing in another shift that is redundant with the funnel shift.

  // (fshl X, ?, Y) | (shl X, Y) --> fshl X, ?, Y
  // (shl X, Y) | (fshl X, ?, Y) --> fshl X, ?, Y
  if (match(Op0,
            m_Intrinsic<Intrinsic::fshl>(m_Value(X), m_Value(), m_Value(Y))) &&
      match(Op1, m_Shl(m_Specific(X), m_Specific(Y))))
    return Op0;
  if (match(Op1,
            m_Intrinsic<Intrinsic::fshl>(m_Value(X), m_Value(), m_Value(Y))) &&
      match(Op0, m_Shl(m_Specific(X), m_Specific(Y))))
    return Op1;

  // (fshr ?, X, Y) | (lshr X, Y) --> fshr ?, X, Y
  // (lshr X, Y) | (fshr ?, X, Y) --> fshr ?, X, Y
  if (match(Op0,
            m_Intrinsic<Intrinsic::fshr>(m_Value(), m_Value(X), m_Value(Y))) &&
      match(Op1, m_LShr(m_Specific(X), m_Specific(Y))))
    return Op0;
  if (match(Op1,
            m_Intrinsic<Intrinsic::fshr>(m_Value(), m_Value(X), m_Value(Y))) &&
      match(Op0, m_LShr(m_Specific(X), m_Specific(Y))))
    return Op1;

  if (Value *V =
          simplifyAndOrWithICmpEq(Instruction::Or, Op0, Op1, Q, MaxRecurse))
    return V;
  if (Value *V =
          simplifyAndOrWithICmpEq(Instruction::Or, Op1, Op0, Q, MaxRecurse))
    return V;

  if (Value *V = simplifyAndOrOfCmps(Q, Op0, Op1, false))
    return V;

  // If we have a multiplication overflow check that is being 'and'ed with a
  // check that one of the multipliers is not zero, we can omit the 'and', and
  // only keep the overflow check.
  if (isCheckForZeroAndMulWithOverflow(Op0, Op1, false))
    return Op1;
  if (isCheckForZeroAndMulWithOverflow(Op1, Op0, false))
    return Op0;

  // Try some generic simplifications for associative operations.
  if (Value *V =
          simplifyAssociativeBinOp(Instruction::Or, Op0, Op1, Q, MaxRecurse))
    return V;

  // Or distributes over And.  Try some generic simplifications based on this.
  if (Value *V = expandCommutativeBinOp(Instruction::Or, Op0, Op1,
                                        Instruction::And, Q, MaxRecurse))
    return V;

  if (isa<SelectInst>(Op0) || isa<SelectInst>(Op1)) {
    if (Op0->getType()->isIntOrIntVectorTy(1)) {
      // A | (A || B) -> A || B
      if (match(Op1, m_Select(m_Specific(Op0), m_One(), m_Value())))
        return Op1;
      else if (match(Op0, m_Select(m_Specific(Op1), m_One(), m_Value())))
        return Op0;
    }
    // If the operation is with the result of a select instruction, check
    // whether operating on either branch of the select always yields the same
    // value.
    if (Value *V =
            threadBinOpOverSelect(Instruction::Or, Op0, Op1, Q, MaxRecurse))
      return V;
  }

  // (A & C1)|(B & C2)
  Value *A, *B;
  const APInt *C1, *C2;
  if (match(Op0, m_And(m_Value(A), m_APInt(C1))) &&
      match(Op1, m_And(m_Value(B), m_APInt(C2)))) {
    if (*C1 == ~*C2) {
      // (A & C1)|(B & C2)
      // If we have: ((V + N) & C1) | (V & C2)
      // .. and C2 = ~C1 and C2 is 0+1+ and (N & C2) == 0
      // replace with V+N.
      Value *N;
      if (C2->isMask() && // C2 == 0+1+
          match(A, m_c_Add(m_Specific(B), m_Value(N)))) {
        // Add commutes, try both ways.
        if (MaskedValueIsZero(N, *C2, Q))
          return A;
      }
      // Or commutes, try both ways.
      if (C1->isMask() && match(B, m_c_Add(m_Specific(A), m_Value(N)))) {
        // Add commutes, try both ways.
        if (MaskedValueIsZero(N, *C1, Q))
          return B;
      }
    }
  }

  // If the operation is with the result of a phi instruction, check whether
  // operating on all incoming values of the phi always yields the same value.
  if (isa<PHINode>(Op0) || isa<PHINode>(Op1))
    if (Value *V = threadBinOpOverPHI(Instruction::Or, Op0, Op1, Q, MaxRecurse))
      return V;

  // (A ^ C) | (A ^ ~C) -> -1, i.e. all bits set to one.
  if (match(Op0, m_Xor(m_Value(A), m_APInt(C1))) &&
      match(Op1, m_Xor(m_Specific(A), m_SpecificInt(~*C1))))
    return Constant::getAllOnesValue(Op0->getType());

  if (Op0->getType()->isIntOrIntVectorTy(1)) {
    if (std::optional<bool> Implied =
            isImpliedCondition(Op0, Op1, Q.DL, false)) {
      // If Op0 is false implies Op1 is false, then Op1 is a subset of Op0.
      if (*Implied == false)
        return Op0;
      // If Op0 is false implies Op1 is true, then at least one is always true.
      if (*Implied == true)
        return ConstantInt::getTrue(Op0->getType());
    }
    if (std::optional<bool> Implied =
            isImpliedCondition(Op1, Op0, Q.DL, false)) {
      // If Op1 is false implies Op0 is false, then Op0 is a subset of Op1.
      if (*Implied == false)
        return Op1;
      // If Op1 is false implies Op0 is true, then at least one is always true.
      if (*Implied == true)
        return ConstantInt::getTrue(Op1->getType());
    }
  }

  if (Value *V = simplifyByDomEq(Instruction::Or, Op0, Op1, Q, MaxRecurse))
    return V;

  return nullptr;
}

Value *llvm::simplifyOrInst(Value *Op0, Value *Op1, const SimplifyQuery &Q) {
  return ::simplifyOrInst(Op0, Op1, Q, RecursionLimit);
}

/// Given operands for a Xor, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyXorInst(Value *Op0, Value *Op1, const SimplifyQuery &Q,
                              unsigned MaxRecurse) {
  if (Constant *C = foldOrCommuteConstant(Instruction::Xor, Op0, Op1, Q))
    return C;

  // X ^ poison -> poison
  if (isa<PoisonValue>(Op1))
    return Op1;

  // A ^ undef -> undef
  if (Q.isUndefValue(Op1))
    return Op1;

  // A ^ 0 = A
  if (match(Op1, m_Zero()))
    return Op0;

  // A ^ A = 0
  if (Op0 == Op1)
    return Constant::getNullValue(Op0->getType());

  // A ^ ~A  =  ~A ^ A  =  -1
  if (match(Op0, m_Not(m_Specific(Op1))) || match(Op1, m_Not(m_Specific(Op0))))
    return Constant::getAllOnesValue(Op0->getType());

  auto foldAndOrNot = [](Value *X, Value *Y) -> Value * {
    Value *A, *B;
    // (~A & B) ^ (A | B) --> A -- There are 8 commuted variants.
    if (match(X, m_c_And(m_Not(m_Value(A)), m_Value(B))) &&
        match(Y, m_c_Or(m_Specific(A), m_Specific(B))))
      return A;

    // (~A | B) ^ (A & B) --> ~A -- There are 8 commuted variants.
    // The 'not' op must contain a complete -1 operand (no undef elements for
    // vector) for the transform to be safe.
    Value *NotA;
    if (match(X, m_c_Or(m_CombineAnd(m_Not(m_Value(A)), m_Value(NotA)),
                        m_Value(B))) &&
        match(Y, m_c_And(m_Specific(A), m_Specific(B))))
      return NotA;

    return nullptr;
  };
  if (Value *R = foldAndOrNot(Op0, Op1))
    return R;
  if (Value *R = foldAndOrNot(Op1, Op0))
    return R;

  if (Value *V = simplifyLogicOfAddSub(Op0, Op1, Instruction::Xor))
    return V;

  // Try some generic simplifications for associative operations.
  if (Value *V =
          simplifyAssociativeBinOp(Instruction::Xor, Op0, Op1, Q, MaxRecurse))
    return V;

  // Threading Xor over selects and phi nodes is pointless, so don't bother.
  // Threading over the select in "A ^ select(cond, B, C)" means evaluating
  // "A^B" and "A^C" and seeing if they are equal; but they are equal if and
  // only if B and C are equal.  If B and C are equal then (since we assume
  // that operands have already been simplified) "select(cond, B, C)" should
  // have been simplified to the common value of B and C already.  Analysing
  // "A^B" and "A^C" thus gains nothing, but costs compile time.  Similarly
  // for threading over phi nodes.

  if (Value *V = simplifyByDomEq(Instruction::Xor, Op0, Op1, Q, MaxRecurse))
    return V;

  return nullptr;
}

Value *llvm::simplifyXorInst(Value *Op0, Value *Op1, const SimplifyQuery &Q) {
  return ::simplifyXorInst(Op0, Op1, Q, RecursionLimit);
}

static Type *getCompareTy(Value *Op) {
  return CmpInst::makeCmpResultType(Op->getType());
}

/// Rummage around inside V looking for something equivalent to the comparison
/// "LHS Pred RHS". Return such a value if found, otherwise return null.
/// Helper function for analyzing max/min idioms.
static Value *extractEquivalentCondition(Value *V, CmpInst::Predicate Pred,
                                         Value *LHS, Value *RHS) {
  SelectInst *SI = dyn_cast<SelectInst>(V);
  if (!SI)
    return nullptr;
  CmpInst *Cmp = dyn_cast<CmpInst>(SI->getCondition());
  if (!Cmp)
    return nullptr;
  Value *CmpLHS = Cmp->getOperand(0), *CmpRHS = Cmp->getOperand(1);
  if (Pred == Cmp->getPredicate() && LHS == CmpLHS && RHS == CmpRHS)
    return Cmp;
  if (Pred == CmpInst::getSwappedPredicate(Cmp->getPredicate()) &&
      LHS == CmpRHS && RHS == CmpLHS)
    return Cmp;
  return nullptr;
}

/// Return true if the underlying object (storage) must be disjoint from
/// storage returned by any noalias return call.
static bool isAllocDisjoint(const Value *V) {
  // For allocas, we consider only static ones (dynamic
  // allocas might be transformed into calls to malloc not simultaneously
  // live with the compared-to allocation). For globals, we exclude symbols
  // that might be resolve lazily to symbols in another dynamically-loaded
  // library (and, thus, could be malloc'ed by the implementation).
  if (const AllocaInst *AI = dyn_cast<AllocaInst>(V))
    return AI->isStaticAlloca();
  if (const GlobalValue *GV = dyn_cast<GlobalValue>(V))
    return (GV->hasLocalLinkage() || GV->hasHiddenVisibility() ||
            GV->hasProtectedVisibility() || GV->hasGlobalUnnamedAddr()) &&
           !GV->isThreadLocal();
  if (const Argument *A = dyn_cast<Argument>(V))
    return A->hasByValAttr();
  return false;
}

/// Return true if V1 and V2 are each the base of some distict storage region
/// [V, object_size(V)] which do not overlap.  Note that zero sized regions
/// *are* possible, and that zero sized regions do not overlap with any other.
static bool haveNonOverlappingStorage(const Value *V1, const Value *V2) {
  // Global variables always exist, so they always exist during the lifetime
  // of each other and all allocas.  Global variables themselves usually have
  // non-overlapping storage, but since their addresses are constants, the
  // case involving two globals does not reach here and is instead handled in
  // constant folding.
  //
  // Two different allocas usually have different addresses...
  //
  // However, if there's an @llvm.stackrestore dynamically in between two
  // allocas, they may have the same address. It's tempting to reduce the
  // scope of the problem by only looking at *static* allocas here. That would
  // cover the majority of allocas while significantly reducing the likelihood
  // of having an @llvm.stackrestore pop up in the middle. However, it's not
  // actually impossible for an @llvm.stackrestore to pop up in the middle of
  // an entry block. Also, if we have a block that's not attached to a
  // function, we can't tell if it's "static" under the current definition.
  // Theoretically, this problem could be fixed by creating a new kind of
  // instruction kind specifically for static allocas. Such a new instruction
  // could be required to be at the top of the entry block, thus preventing it
  // from being subject to a @llvm.stackrestore. Instcombine could even
  // convert regular allocas into these special allocas. It'd be nifty.
  // However, until then, this problem remains open.
  //
  // So, we'll assume that two non-empty allocas have different addresses
  // for now.
  auto isByValArg = [](const Value *V) {
    const Argument *A = dyn_cast<Argument>(V);
    return A && A->hasByValAttr();
  };

  // Byval args are backed by store which does not overlap with each other,
  // allocas, or globals.
  if (isByValArg(V1))
    return isa<AllocaInst>(V2) || isa<GlobalVariable>(V2) || isByValArg(V2);
  if (isByValArg(V2))
    return isa<AllocaInst>(V1) || isa<GlobalVariable>(V1) || isByValArg(V1);

  return isa<AllocaInst>(V1) &&
         (isa<AllocaInst>(V2) || isa<GlobalVariable>(V2));
}

// A significant optimization not implemented here is assuming that alloca
// addresses are not equal to incoming argument values. They don't *alias*,
// as we say, but that doesn't mean they aren't equal, so we take a
// conservative approach.
//
// This is inspired in part by C++11 5.10p1:
//   "Two pointers of the same type compare equal if and only if they are both
//    null, both point to the same function, or both represent the same
//    address."
//
// This is pretty permissive.
//
// It's also partly due to C11 6.5.9p6:
//   "Two pointers compare equal if and only if both are null pointers, both are
//    pointers to the same object (including a pointer to an object and a
//    subobject at its beginning) or function, both are pointers to one past the
//    last element of the same array object, or one is a pointer to one past the
//    end of one array object and the other is a pointer to the start of a
//    different array object that happens to immediately follow the first array
//    object in the address space.)
//
// C11's version is more restrictive, however there's no reason why an argument
// couldn't be a one-past-the-end value for a stack object in the caller and be
// equal to the beginning of a stack object in the callee.
//
// If the C and C++ standards are ever made sufficiently restrictive in this
// area, it may be possible to update LLVM's semantics accordingly and reinstate
// this optimization.
static Constant *computePointerICmp(CmpInst::Predicate Pred, Value *LHS,
                                    Value *RHS, const SimplifyQuery &Q) {
  assert(LHS->getType() == RHS->getType() && "Must have same types");
  const DataLayout &DL = Q.DL;
  const TargetLibraryInfo *TLI = Q.TLI;

  // We can only fold certain predicates on pointer comparisons.
  switch (Pred) {
  default:
    return nullptr;

    // Equality comparisons are easy to fold.
  case CmpInst::ICMP_EQ:
  case CmpInst::ICMP_NE:
    break;

    // We can only handle unsigned relational comparisons because 'inbounds' on
    // a GEP only protects against unsigned wrapping.
  case CmpInst::ICMP_UGT:
  case CmpInst::ICMP_UGE:
  case CmpInst::ICMP_ULT:
  case CmpInst::ICMP_ULE:
    // However, we have to switch them to their signed variants to handle
    // negative indices from the base pointer.
    Pred = ICmpInst::getSignedPredicate(Pred);
    break;
  }

  // Strip off any constant offsets so that we can reason about them.
  // It's tempting to use getUnderlyingObject or even just stripInBoundsOffsets
  // here and compare base addresses like AliasAnalysis does, however there are
  // numerous hazards. AliasAnalysis and its utilities rely on special rules
  // governing loads and stores which don't apply to icmps. Also, AliasAnalysis
  // doesn't need to guarantee pointer inequality when it says NoAlias.

  // Even if an non-inbounds GEP occurs along the path we can still optimize
  // equality comparisons concerning the result.
  bool AllowNonInbounds = ICmpInst::isEquality(Pred);
  unsigned IndexSize = DL.getIndexTypeSizeInBits(LHS->getType());
  APInt LHSOffset(IndexSize, 0), RHSOffset(IndexSize, 0);
  LHS = LHS->stripAndAccumulateConstantOffsets(DL, LHSOffset, AllowNonInbounds);
  RHS = RHS->stripAndAccumulateConstantOffsets(DL, RHSOffset, AllowNonInbounds);

  // If LHS and RHS are related via constant offsets to the same base
  // value, we can replace it with an icmp which just compares the offsets.
  if (LHS == RHS)
    return ConstantInt::get(getCompareTy(LHS),
                            ICmpInst::compare(LHSOffset, RHSOffset, Pred));

  // Various optimizations for (in)equality comparisons.
  if (Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_NE) {
    // Different non-empty allocations that exist at the same time have
    // different addresses (if the program can tell). If the offsets are
    // within the bounds of their allocations (and not one-past-the-end!
    // so we can't use inbounds!), and their allocations aren't the same,
    // the pointers are not equal.
    if (haveNonOverlappingStorage(LHS, RHS)) {
      uint64_t LHSSize, RHSSize;
      ObjectSizeOpts Opts;
      Opts.EvalMode = ObjectSizeOpts::Mode::Min;
      auto *F = [](Value *V) -> Function * {
        if (auto *I = dyn_cast<Instruction>(V))
          return I->getFunction();
        if (auto *A = dyn_cast<Argument>(V))
          return A->getParent();
        return nullptr;
      }(LHS);
      Opts.NullIsUnknownSize = F ? NullPointerIsDefined(F) : true;
      if (getObjectSize(LHS, LHSSize, DL, TLI, Opts) &&
          getObjectSize(RHS, RHSSize, DL, TLI, Opts)) {
        APInt Dist = LHSOffset - RHSOffset;
        if (Dist.isNonNegative() ? Dist.ult(LHSSize) : (-Dist).ult(RHSSize))
          return ConstantInt::get(getCompareTy(LHS),
                                  !CmpInst::isTrueWhenEqual(Pred));
      }
    }

    // If one side of the equality comparison must come from a noalias call
    // (meaning a system memory allocation function), and the other side must
    // come from a pointer that cannot overlap with dynamically-allocated
    // memory within the lifetime of the current function (allocas, byval
    // arguments, globals), then determine the comparison result here.
    SmallVector<const Value *, 8> LHSUObjs, RHSUObjs;
    getUnderlyingObjects(LHS, LHSUObjs);
    getUnderlyingObjects(RHS, RHSUObjs);

    // Is the set of underlying objects all noalias calls?
    auto IsNAC = [](ArrayRef<const Value *> Objects) {
      return all_of(Objects, isNoAliasCall);
    };

    // Is the set of underlying objects all things which must be disjoint from
    // noalias calls.  We assume that indexing from such disjoint storage
    // into the heap is undefined, and thus offsets can be safely ignored.
    auto IsAllocDisjoint = [](ArrayRef<const Value *> Objects) {
      return all_of(Objects, ::isAllocDisjoint);
    };

    if ((IsNAC(LHSUObjs) && IsAllocDisjoint(RHSUObjs)) ||
        (IsNAC(RHSUObjs) && IsAllocDisjoint(LHSUObjs)))
      return ConstantInt::get(getCompareTy(LHS),
                              !CmpInst::isTrueWhenEqual(Pred));

    // Fold comparisons for non-escaping pointer even if the allocation call
    // cannot be elided. We cannot fold malloc comparison to null. Also, the
    // dynamic allocation call could be either of the operands.  Note that
    // the other operand can not be based on the alloc - if it were, then
    // the cmp itself would be a capture.
    Value *MI = nullptr;
    if (isAllocLikeFn(LHS, TLI) && llvm::isKnownNonZero(RHS, Q))
      MI = LHS;
    else if (isAllocLikeFn(RHS, TLI) && llvm::isKnownNonZero(LHS, Q))
      MI = RHS;
    if (MI) {
      // FIXME: This is incorrect, see PR54002. While we can assume that the
      // allocation is at an address that makes the comparison false, this
      // requires that *all* comparisons to that address be false, which
      // InstSimplify cannot guarantee.
      struct CustomCaptureTracker : public CaptureTracker {
        bool Captured = false;
        void tooManyUses() override { Captured = true; }
        bool captured(const Use *U) override {
          if (auto *ICmp = dyn_cast<ICmpInst>(U->getUser())) {
            // Comparison against value stored in global variable. Given the
            // pointer does not escape, its value cannot be guessed and stored
            // separately in a global variable.
            unsigned OtherIdx = 1 - U->getOperandNo();
            auto *LI = dyn_cast<LoadInst>(ICmp->getOperand(OtherIdx));
            if (LI && isa<GlobalVariable>(LI->getPointerOperand()))
              return false;
          }

          Captured = true;
          return true;
        }
      };
      CustomCaptureTracker Tracker;
      PointerMayBeCaptured(MI, &Tracker);
      if (!Tracker.Captured)
        return ConstantInt::get(getCompareTy(LHS),
                                CmpInst::isFalseWhenEqual(Pred));
    }
  }

  // Otherwise, fail.
  return nullptr;
}

/// Fold an icmp when its operands have i1 scalar type.
static Value *simplifyICmpOfBools(CmpInst::Predicate Pred, Value *LHS,
                                  Value *RHS, const SimplifyQuery &Q) {
  Type *ITy = getCompareTy(LHS); // The return type.
  Type *OpTy = LHS->getType();   // The operand type.
  if (!OpTy->isIntOrIntVectorTy(1))
    return nullptr;

  // A boolean compared to true/false can be reduced in 14 out of the 20
  // (10 predicates * 2 constants) possible combinations. The other
  // 6 cases require a 'not' of the LHS.

  auto ExtractNotLHS = [](Value *V) -> Value * {
    Value *X;
    if (match(V, m_Not(m_Value(X))))
      return X;
    return nullptr;
  };

  if (match(RHS, m_Zero())) {
    switch (Pred) {
    case CmpInst::ICMP_NE:  // X !=  0 -> X
    case CmpInst::ICMP_UGT: // X >u  0 -> X
    case CmpInst::ICMP_SLT: // X <s  0 -> X
      return LHS;

    case CmpInst::ICMP_EQ:  // not(X) ==  0 -> X != 0 -> X
    case CmpInst::ICMP_ULE: // not(X) <=u 0 -> X >u 0 -> X
    case CmpInst::ICMP_SGE: // not(X) >=s 0 -> X <s 0 -> X
      if (Value *X = ExtractNotLHS(LHS))
        return X;
      break;

    case CmpInst::ICMP_ULT: // X <u  0 -> false
    case CmpInst::ICMP_SGT: // X >s  0 -> false
      return getFalse(ITy);

    case CmpInst::ICMP_UGE: // X >=u 0 -> true
    case CmpInst::ICMP_SLE: // X <=s 0 -> true
      return getTrue(ITy);

    default:
      break;
    }
  } else if (match(RHS, m_One())) {
    switch (Pred) {
    case CmpInst::ICMP_EQ:  // X ==   1 -> X
    case CmpInst::ICMP_UGE: // X >=u  1 -> X
    case CmpInst::ICMP_SLE: // X <=s -1 -> X
      return LHS;

    case CmpInst::ICMP_NE:  // not(X) !=  1 -> X ==   1 -> X
    case CmpInst::ICMP_ULT: // not(X) <=u 1 -> X >=u  1 -> X
    case CmpInst::ICMP_SGT: // not(X) >s  1 -> X <=s -1 -> X
      if (Value *X = ExtractNotLHS(LHS))
        return X;
      break;

    case CmpInst::ICMP_UGT: // X >u   1 -> false
    case CmpInst::ICMP_SLT: // X <s  -1 -> false
      return getFalse(ITy);

    case CmpInst::ICMP_ULE: // X <=u  1 -> true
    case CmpInst::ICMP_SGE: // X >=s -1 -> true
      return getTrue(ITy);

    default:
      break;
    }
  }

  switch (Pred) {
  default:
    break;
  case ICmpInst::ICMP_UGE:
    if (isImpliedCondition(RHS, LHS, Q.DL).value_or(false))
      return getTrue(ITy);
    break;
  case ICmpInst::ICMP_SGE:
    /// For signed comparison, the values for an i1 are 0 and -1
    /// respectively. This maps into a truth table of:
    /// LHS | RHS | LHS >=s RHS   | LHS implies RHS
    ///  0  |  0  |  1 (0 >= 0)   |  1
    ///  0  |  1  |  1 (0 >= -1)  |  1
    ///  1  |  0  |  0 (-1 >= 0)  |  0
    ///  1  |  1  |  1 (-1 >= -1) |  1
    if (isImpliedCondition(LHS, RHS, Q.DL).value_or(false))
      return getTrue(ITy);
    break;
  case ICmpInst::ICMP_ULE:
    if (isImpliedCondition(LHS, RHS, Q.DL).value_or(false))
      return getTrue(ITy);
    break;
  case ICmpInst::ICMP_SLE:
    /// SLE follows the same logic as SGE with the LHS and RHS swapped.
    if (isImpliedCondition(RHS, LHS, Q.DL).value_or(false))
      return getTrue(ITy);
    break;
  }

  return nullptr;
}

/// Try hard to fold icmp with zero RHS because this is a common case.
static Value *simplifyICmpWithZero(CmpInst::Predicate Pred, Value *LHS,
                                   Value *RHS, const SimplifyQuery &Q) {
  if (!match(RHS, m_Zero()))
    return nullptr;

  Type *ITy = getCompareTy(LHS); // The return type.
  switch (Pred) {
  default:
    llvm_unreachable("Unknown ICmp predicate!");
  case ICmpInst::ICMP_ULT:
    return getFalse(ITy);
  case ICmpInst::ICMP_UGE:
    return getTrue(ITy);
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_ULE:
    if (isKnownNonZero(LHS, Q))
      return getFalse(ITy);
    break;
  case ICmpInst::ICMP_NE:
  case ICmpInst::ICMP_UGT:
    if (isKnownNonZero(LHS, Q))
      return getTrue(ITy);
    break;
  case ICmpInst::ICMP_SLT: {
    KnownBits LHSKnown = computeKnownBits(LHS, /* Depth */ 0, Q);
    if (LHSKnown.isNegative())
      return getTrue(ITy);
    if (LHSKnown.isNonNegative())
      return getFalse(ITy);
    break;
  }
  case ICmpInst::ICMP_SLE: {
    KnownBits LHSKnown = computeKnownBits(LHS, /* Depth */ 0, Q);
    if (LHSKnown.isNegative())
      return getTrue(ITy);
    if (LHSKnown.isNonNegative() && isKnownNonZero(LHS, Q))
      return getFalse(ITy);
    break;
  }
  case ICmpInst::ICMP_SGE: {
    KnownBits LHSKnown = computeKnownBits(LHS, /* Depth */ 0, Q);
    if (LHSKnown.isNegative())
      return getFalse(ITy);
    if (LHSKnown.isNonNegative())
      return getTrue(ITy);
    break;
  }
  case ICmpInst::ICMP_SGT: {
    KnownBits LHSKnown = computeKnownBits(LHS, /* Depth */ 0, Q);
    if (LHSKnown.isNegative())
      return getFalse(ITy);
    if (LHSKnown.isNonNegative() && isKnownNonZero(LHS, Q))
      return getTrue(ITy);
    break;
  }
  }

  return nullptr;
}

static Value *simplifyICmpWithConstant(CmpInst::Predicate Pred, Value *LHS,
                                       Value *RHS, const InstrInfoQuery &IIQ) {
  Type *ITy = getCompareTy(RHS); // The return type.

  Value *X;
  const APInt *C;
  if (!match(RHS, m_APIntAllowPoison(C)))
    return nullptr;

  // Sign-bit checks can be optimized to true/false after unsigned
  // floating-point casts:
  // icmp slt (bitcast (uitofp X)),  0 --> false
  // icmp sgt (bitcast (uitofp X)), -1 --> true
  if (match(LHS, m_ElementWiseBitCast(m_UIToFP(m_Value(X))))) {
    bool TrueIfSigned;
    if (isSignBitCheck(Pred, *C, TrueIfSigned))
      return ConstantInt::getBool(ITy, !TrueIfSigned);
  }

  // Rule out tautological comparisons (eg., ult 0 or uge 0).
  ConstantRange RHS_CR = ConstantRange::makeExactICmpRegion(Pred, *C);
  if (RHS_CR.isEmptySet())
    return ConstantInt::getFalse(ITy);
  if (RHS_CR.isFullSet())
    return ConstantInt::getTrue(ITy);

  ConstantRange LHS_CR =
      computeConstantRange(LHS, CmpInst::isSigned(Pred), IIQ.UseInstrInfo);
  if (!LHS_CR.isFullSet()) {
    if (RHS_CR.contains(LHS_CR))
      return ConstantInt::getTrue(ITy);
    if (RHS_CR.inverse().contains(LHS_CR))
      return ConstantInt::getFalse(ITy);
  }

  // (mul nuw/nsw X, MulC) != C --> true  (if C is not a multiple of MulC)
  // (mul nuw/nsw X, MulC) == C --> false (if C is not a multiple of MulC)
  const APInt *MulC;
  if (IIQ.UseInstrInfo && ICmpInst::isEquality(Pred) &&
      ((match(LHS, m_NUWMul(m_Value(), m_APIntAllowPoison(MulC))) &&
        *MulC != 0 && C->urem(*MulC) != 0) ||
       (match(LHS, m_NSWMul(m_Value(), m_APIntAllowPoison(MulC))) &&
        *MulC != 0 && C->srem(*MulC) != 0)))
    return ConstantInt::get(ITy, Pred == ICmpInst::ICMP_NE);

  return nullptr;
}

static Value *simplifyICmpWithBinOpOnLHS(CmpInst::Predicate Pred,
                                         BinaryOperator *LBO, Value *RHS,
                                         const SimplifyQuery &Q,
                                         unsigned MaxRecurse) {
  Type *ITy = getCompareTy(RHS); // The return type.

  Value *Y = nullptr;
  // icmp pred (or X, Y), X
  if (match(LBO, m_c_Or(m_Value(Y), m_Specific(RHS)))) {
    if (Pred == ICmpInst::ICMP_ULT)
      return getFalse(ITy);
    if (Pred == ICmpInst::ICMP_UGE)
      return getTrue(ITy);

    if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SGE) {
      KnownBits RHSKnown = computeKnownBits(RHS, /* Depth */ 0, Q);
      KnownBits YKnown = computeKnownBits(Y, /* Depth */ 0, Q);
      if (RHSKnown.isNonNegative() && YKnown.isNegative())
        return Pred == ICmpInst::ICMP_SLT ? getTrue(ITy) : getFalse(ITy);
      if (RHSKnown.isNegative() || YKnown.isNonNegative())
        return Pred == ICmpInst::ICMP_SLT ? getFalse(ITy) : getTrue(ITy);
    }
  }

  // icmp pred (and X, Y), X
  if (match(LBO, m_c_And(m_Value(), m_Specific(RHS)))) {
    if (Pred == ICmpInst::ICMP_UGT)
      return getFalse(ITy);
    if (Pred == ICmpInst::ICMP_ULE)
      return getTrue(ITy);
  }

  // icmp pred (urem X, Y), Y
  if (match(LBO, m_URem(m_Value(), m_Specific(RHS)))) {
    switch (Pred) {
    default:
      break;
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE: {
      KnownBits Known = computeKnownBits(RHS, /* Depth */ 0, Q);
      if (!Known.isNonNegative())
        break;
      [[fallthrough]];
    }
    case ICmpInst::ICMP_EQ:
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE:
      return getFalse(ITy);
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE: {
      KnownBits Known = computeKnownBits(RHS, /* Depth */ 0, Q);
      if (!Known.isNonNegative())
        break;
      [[fallthrough]];
    }
    case ICmpInst::ICMP_NE:
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_ULE:
      return getTrue(ITy);
    }
  }

  // icmp pred (urem X, Y), X
  if (match(LBO, m_URem(m_Specific(RHS), m_Value()))) {
    if (Pred == ICmpInst::ICMP_ULE)
      return getTrue(ITy);
    if (Pred == ICmpInst::ICMP_UGT)
      return getFalse(ITy);
  }

  // x >>u y <=u x --> true.
  // x >>u y >u  x --> false.
  // x udiv y <=u x --> true.
  // x udiv y >u  x --> false.
  if (match(LBO, m_LShr(m_Specific(RHS), m_Value())) ||
      match(LBO, m_UDiv(m_Specific(RHS), m_Value()))) {
    // icmp pred (X op Y), X
    if (Pred == ICmpInst::ICMP_UGT)
      return getFalse(ITy);
    if (Pred == ICmpInst::ICMP_ULE)
      return getTrue(ITy);
  }

  // If x is nonzero:
  // x >>u C <u  x --> true  for C != 0.
  // x >>u C !=  x --> true  for C != 0.
  // x >>u C >=u x --> false for C != 0.
  // x >>u C ==  x --> false for C != 0.
  // x udiv C <u  x --> true  for C != 1.
  // x udiv C !=  x --> true  for C != 1.
  // x udiv C >=u x --> false for C != 1.
  // x udiv C ==  x --> false for C != 1.
  // TODO: allow non-constant shift amount/divisor
  const APInt *C;
  if ((match(LBO, m_LShr(m_Specific(RHS), m_APInt(C))) && *C != 0) ||
      (match(LBO, m_UDiv(m_Specific(RHS), m_APInt(C))) && *C != 1)) {
    if (isKnownNonZero(RHS, Q)) {
      switch (Pred) {
      default:
        break;
      case ICmpInst::ICMP_EQ:
      case ICmpInst::ICMP_UGE:
        return getFalse(ITy);
      case ICmpInst::ICMP_NE:
      case ICmpInst::ICMP_ULT:
        return getTrue(ITy);
      case ICmpInst::ICMP_UGT:
      case ICmpInst::ICMP_ULE:
        // UGT/ULE are handled by the more general case just above
        llvm_unreachable("Unexpected UGT/ULE, should have been handled");
      }
    }
  }

  // (x*C1)/C2 <= x for C1 <= C2.
  // This holds even if the multiplication overflows: Assume that x != 0 and
  // arithmetic is modulo M. For overflow to occur we must have C1 >= M/x and
  // thus C2 >= M/x. It follows that (x*C1)/C2 <= (M-1)/C2 <= ((M-1)*x)/M < x.
  //
  // Additionally, either the multiplication and division might be represented
  // as shifts:
  // (x*C1)>>C2 <= x for C1 < 2**C2.
  // (x<<C1)/C2 <= x for 2**C1 < C2.
  const APInt *C1, *C2;
  if ((match(LBO, m_UDiv(m_Mul(m_Specific(RHS), m_APInt(C1)), m_APInt(C2))) &&
       C1->ule(*C2)) ||
      (match(LBO, m_LShr(m_Mul(m_Specific(RHS), m_APInt(C1)), m_APInt(C2))) &&
       C1->ule(APInt(C2->getBitWidth(), 1) << *C2)) ||
      (match(LBO, m_UDiv(m_Shl(m_Specific(RHS), m_APInt(C1)), m_APInt(C2))) &&
       (APInt(C1->getBitWidth(), 1) << *C1).ule(*C2))) {
    if (Pred == ICmpInst::ICMP_UGT)
      return getFalse(ITy);
    if (Pred == ICmpInst::ICMP_ULE)
      return getTrue(ITy);
  }

  // (sub C, X) == X, C is odd  --> false
  // (sub C, X) != X, C is odd  --> true
  if (match(LBO, m_Sub(m_APIntAllowPoison(C), m_Specific(RHS))) &&
      (*C & 1) == 1 && ICmpInst::isEquality(Pred))
    return (Pred == ICmpInst::ICMP_EQ) ? getFalse(ITy) : getTrue(ITy);

  return nullptr;
}

// If only one of the icmp's operands has NSW flags, try to prove that:
//
//   icmp slt (x + C1), (x +nsw C2)
//
// is equivalent to:
//
//   icmp slt C1, C2
//
// which is true if x + C2 has the NSW flags set and:
// *) C1 < C2 && C1 >= 0, or
// *) C2 < C1 && C1 <= 0.
//
static bool trySimplifyICmpWithAdds(CmpInst::Predicate Pred, Value *LHS,
                                    Value *RHS, const InstrInfoQuery &IIQ) {
  // TODO: only support icmp slt for now.
  if (Pred != CmpInst::ICMP_SLT || !IIQ.UseInstrInfo)
    return false;

  // Canonicalize nsw add as RHS.
  if (!match(RHS, m_NSWAdd(m_Value(), m_Value())))
    std::swap(LHS, RHS);
  if (!match(RHS, m_NSWAdd(m_Value(), m_Value())))
    return false;

  Value *X;
  const APInt *C1, *C2;
  if (!match(LHS, m_Add(m_Value(X), m_APInt(C1))) ||
      !match(RHS, m_Add(m_Specific(X), m_APInt(C2))))
    return false;

  return (C1->slt(*C2) && C1->isNonNegative()) ||
         (C2->slt(*C1) && C1->isNonPositive());
}

/// TODO: A large part of this logic is duplicated in InstCombine's
/// foldICmpBinOp(). We should be able to share that and avoid the code
/// duplication.
static Value *simplifyICmpWithBinOp(CmpInst::Predicate Pred, Value *LHS,
                                    Value *RHS, const SimplifyQuery &Q,
                                    unsigned MaxRecurse) {
  BinaryOperator *LBO = dyn_cast<BinaryOperator>(LHS);
  BinaryOperator *RBO = dyn_cast<BinaryOperator>(RHS);
  if (MaxRecurse && (LBO || RBO)) {
    // Analyze the case when either LHS or RHS is an add instruction.
    Value *A = nullptr, *B = nullptr, *C = nullptr, *D = nullptr;
    // LHS = A + B (or A and B are null); RHS = C + D (or C and D are null).
    bool NoLHSWrapProblem = false, NoRHSWrapProblem = false;
    if (LBO && LBO->getOpcode() == Instruction::Add) {
      A = LBO->getOperand(0);
      B = LBO->getOperand(1);
      NoLHSWrapProblem =
          ICmpInst::isEquality(Pred) ||
          (CmpInst::isUnsigned(Pred) &&
           Q.IIQ.hasNoUnsignedWrap(cast<OverflowingBinaryOperator>(LBO))) ||
          (CmpInst::isSigned(Pred) &&
           Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(LBO)));
    }
    if (RBO && RBO->getOpcode() == Instruction::Add) {
      C = RBO->getOperand(0);
      D = RBO->getOperand(1);
      NoRHSWrapProblem =
          ICmpInst::isEquality(Pred) ||
          (CmpInst::isUnsigned(Pred) &&
           Q.IIQ.hasNoUnsignedWrap(cast<OverflowingBinaryOperator>(RBO))) ||
          (CmpInst::isSigned(Pred) &&
           Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(RBO)));
    }

    // icmp (X+Y), X -> icmp Y, 0 for equalities or if there is no overflow.
    if ((A == RHS || B == RHS) && NoLHSWrapProblem)
      if (Value *V = simplifyICmpInst(Pred, A == RHS ? B : A,
                                      Constant::getNullValue(RHS->getType()), Q,
                                      MaxRecurse - 1))
        return V;

    // icmp X, (X+Y) -> icmp 0, Y for equalities or if there is no overflow.
    if ((C == LHS || D == LHS) && NoRHSWrapProblem)
      if (Value *V =
              simplifyICmpInst(Pred, Constant::getNullValue(LHS->getType()),
                               C == LHS ? D : C, Q, MaxRecurse - 1))
        return V;

    // icmp (X+Y), (X+Z) -> icmp Y,Z for equalities or if there is no overflow.
    bool CanSimplify = (NoLHSWrapProblem && NoRHSWrapProblem) ||
                       trySimplifyICmpWithAdds(Pred, LHS, RHS, Q.IIQ);
    if (A && C && (A == C || A == D || B == C || B == D) && CanSimplify) {
      // Determine Y and Z in the form icmp (X+Y), (X+Z).
      Value *Y, *Z;
      if (A == C) {
        // C + B == C + D  ->  B == D
        Y = B;
        Z = D;
      } else if (A == D) {
        // D + B == C + D  ->  B == C
        Y = B;
        Z = C;
      } else if (B == C) {
        // A + C == C + D  ->  A == D
        Y = A;
        Z = D;
      } else {
        assert(B == D);
        // A + D == C + D  ->  A == C
        Y = A;
        Z = C;
      }
      if (Value *V = simplifyICmpInst(Pred, Y, Z, Q, MaxRecurse - 1))
        return V;
    }
  }

  if (LBO)
    if (Value *V = simplifyICmpWithBinOpOnLHS(Pred, LBO, RHS, Q, MaxRecurse))
      return V;

  if (RBO)
    if (Value *V = simplifyICmpWithBinOpOnLHS(
            ICmpInst::getSwappedPredicate(Pred), RBO, LHS, Q, MaxRecurse))
      return V;

  // 0 - (zext X) pred C
  if (!CmpInst::isUnsigned(Pred) && match(LHS, m_Neg(m_ZExt(m_Value())))) {
    const APInt *C;
    if (match(RHS, m_APInt(C))) {
      if (C->isStrictlyPositive()) {
        if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_NE)
          return ConstantInt::getTrue(getCompareTy(RHS));
        if (Pred == ICmpInst::ICMP_SGE || Pred == ICmpInst::ICMP_EQ)
          return ConstantInt::getFalse(getCompareTy(RHS));
      }
      if (C->isNonNegative()) {
        if (Pred == ICmpInst::ICMP_SLE)
          return ConstantInt::getTrue(getCompareTy(RHS));
        if (Pred == ICmpInst::ICMP_SGT)
          return ConstantInt::getFalse(getCompareTy(RHS));
      }
    }
  }

  //   If C2 is a power-of-2 and C is not:
  //   (C2 << X) == C --> false
  //   (C2 << X) != C --> true
  const APInt *C;
  if (match(LHS, m_Shl(m_Power2(), m_Value())) &&
      match(RHS, m_APIntAllowPoison(C)) && !C->isPowerOf2()) {
    // C2 << X can equal zero in some circumstances.
    // This simplification might be unsafe if C is zero.
    //
    // We know it is safe if:
    // - The shift is nsw. We can't shift out the one bit.
    // - The shift is nuw. We can't shift out the one bit.
    // - C2 is one.
    // - C isn't zero.
    if (Q.IIQ.hasNoSignedWrap(cast<OverflowingBinaryOperator>(LBO)) ||
        Q.IIQ.hasNoUnsignedWrap(cast<OverflowingBinaryOperator>(LBO)) ||
        match(LHS, m_Shl(m_One(), m_Value())) || !C->isZero()) {
      if (Pred == ICmpInst::ICMP_EQ)
        return ConstantInt::getFalse(getCompareTy(RHS));
      if (Pred == ICmpInst::ICMP_NE)
        return ConstantInt::getTrue(getCompareTy(RHS));
    }
  }

  // If C is a power-of-2:
  // (C << X)  >u 0x8000 --> false
  // (C << X) <=u 0x8000 --> true
  if (match(LHS, m_Shl(m_Power2(), m_Value())) && match(RHS, m_SignMask())) {
    if (Pred == ICmpInst::ICMP_UGT)
      return ConstantInt::getFalse(getCompareTy(RHS));
    if (Pred == ICmpInst::ICMP_ULE)
      return ConstantInt::getTrue(getCompareTy(RHS));
  }

  if (!MaxRecurse || !LBO || !RBO || LBO->getOpcode() != RBO->getOpcode())
    return nullptr;

  if (LBO->getOperand(0) == RBO->getOperand(0)) {
    switch (LBO->getOpcode()) {
    default:
      break;
    case Instruction::Shl: {
      bool NUW = Q.IIQ.hasNoUnsignedWrap(LBO) && Q.IIQ.hasNoUnsignedWrap(RBO);
      bool NSW = Q.IIQ.hasNoSignedWrap(LBO) && Q.IIQ.hasNoSignedWrap(RBO);
      if (!NUW || (ICmpInst::isSigned(Pred) && !NSW) ||
          !isKnownNonZero(LBO->getOperand(0), Q))
        break;
      if (Value *V = simplifyICmpInst(Pred, LBO->getOperand(1),
                                      RBO->getOperand(1), Q, MaxRecurse - 1))
        return V;
      break;
    }
    // If C1 & C2 == C1, A = X and/or C1, B = X and/or C2:
    // icmp ule A, B -> true
    // icmp ugt A, B -> false
    // icmp sle A, B -> true (C1 and C2 are the same sign)
    // icmp sgt A, B -> false (C1 and C2 are the same sign)
    case Instruction::And:
    case Instruction::Or: {
      const APInt *C1, *C2;
      if (ICmpInst::isRelational(Pred) &&
          match(LBO->getOperand(1), m_APInt(C1)) &&
          match(RBO->getOperand(1), m_APInt(C2))) {
        if (!C1->isSubsetOf(*C2)) {
          std::swap(C1, C2);
          Pred = ICmpInst::getSwappedPredicate(Pred);
        }
        if (C1->isSubsetOf(*C2)) {
          if (Pred == ICmpInst::ICMP_ULE)
            return ConstantInt::getTrue(getCompareTy(LHS));
          if (Pred == ICmpInst::ICMP_UGT)
            return ConstantInt::getFalse(getCompareTy(LHS));
          if (C1->isNonNegative() == C2->isNonNegative()) {
            if (Pred == ICmpInst::ICMP_SLE)
              return ConstantInt::getTrue(getCompareTy(LHS));
            if (Pred == ICmpInst::ICMP_SGT)
              return ConstantInt::getFalse(getCompareTy(LHS));
          }
        }
      }
      break;
    }
    }
  }

  if (LBO->getOperand(1) == RBO->getOperand(1)) {
    switch (LBO->getOpcode()) {
    default:
      break;
    case Instruction::UDiv:
    case Instruction::LShr:
      if (ICmpInst::isSigned(Pred) || !Q.IIQ.isExact(LBO) ||
          !Q.IIQ.isExact(RBO))
        break;
      if (Value *V = simplifyICmpInst(Pred, LBO->getOperand(0),
                                      RBO->getOperand(0), Q, MaxRecurse - 1))
        return V;
      break;
    case Instruction::SDiv:
      if (!ICmpInst::isEquality(Pred) || !Q.IIQ.isExact(LBO) ||
          !Q.IIQ.isExact(RBO))
        break;
      if (Value *V = simplifyICmpInst(Pred, LBO->getOperand(0),
                                      RBO->getOperand(0), Q, MaxRecurse - 1))
        return V;
      break;
    case Instruction::AShr:
      if (!Q.IIQ.isExact(LBO) || !Q.IIQ.isExact(RBO))
        break;
      if (Value *V = simplifyICmpInst(Pred, LBO->getOperand(0),
                                      RBO->getOperand(0), Q, MaxRecurse - 1))
        return V;
      break;
    case Instruction::Shl: {
      bool NUW = Q.IIQ.hasNoUnsignedWrap(LBO) && Q.IIQ.hasNoUnsignedWrap(RBO);
      bool NSW = Q.IIQ.hasNoSignedWrap(LBO) && Q.IIQ.hasNoSignedWrap(RBO);
      if (!NUW && !NSW)
        break;
      if (!NSW && ICmpInst::isSigned(Pred))
        break;
      if (Value *V = simplifyICmpInst(Pred, LBO->getOperand(0),
                                      RBO->getOperand(0), Q, MaxRecurse - 1))
        return V;
      break;
    }
    }
  }
  return nullptr;
}

/// simplify integer comparisons where at least one operand of the compare
/// matches an integer min/max idiom.
static Value *simplifyICmpWithMinMax(CmpInst::Predicate Pred, Value *LHS,
                                     Value *RHS, const SimplifyQuery &Q,
                                     unsigned MaxRecurse) {
  Type *ITy = getCompareTy(LHS); // The return type.
  Value *A, *B;
  CmpInst::Predicate P = CmpInst::BAD_ICMP_PREDICATE;
  CmpInst::Predicate EqP; // Chosen so that "A == max/min(A,B)" iff "A EqP B".

  // Signed variants on "max(a,b)>=a -> true".
  if (match(LHS, m_SMax(m_Value(A), m_Value(B))) && (A == RHS || B == RHS)) {
    if (A != RHS)
      std::swap(A, B);       // smax(A, B) pred A.
    EqP = CmpInst::ICMP_SGE; // "A == smax(A, B)" iff "A sge B".
    // We analyze this as smax(A, B) pred A.
    P = Pred;
  } else if (match(RHS, m_SMax(m_Value(A), m_Value(B))) &&
             (A == LHS || B == LHS)) {
    if (A != LHS)
      std::swap(A, B);       // A pred smax(A, B).
    EqP = CmpInst::ICMP_SGE; // "A == smax(A, B)" iff "A sge B".
    // We analyze this as smax(A, B) swapped-pred A.
    P = CmpInst::getSwappedPredicate(Pred);
  } else if (match(LHS, m_SMin(m_Value(A), m_Value(B))) &&
             (A == RHS || B == RHS)) {
    if (A != RHS)
      std::swap(A, B);       // smin(A, B) pred A.
    EqP = CmpInst::ICMP_SLE; // "A == smin(A, B)" iff "A sle B".
    // We analyze this as smax(-A, -B) swapped-pred -A.
    // Note that we do not need to actually form -A or -B thanks to EqP.
    P = CmpInst::getSwappedPredicate(Pred);
  } else if (match(RHS, m_SMin(m_Value(A), m_Value(B))) &&
             (A == LHS || B == LHS)) {
    if (A != LHS)
      std::swap(A, B);       // A pred smin(A, B).
    EqP = CmpInst::ICMP_SLE; // "A == smin(A, B)" iff "A sle B".
    // We analyze this as smax(-A, -B) pred -A.
    // Note that we do not need to actually form -A or -B thanks to EqP.
    P = Pred;
  }
  if (P != CmpInst::BAD_ICMP_PREDICATE) {
    // Cases correspond to "max(A, B) p A".
    switch (P) {
    default:
      break;
    case CmpInst::ICMP_EQ:
    case CmpInst::ICMP_SLE:
      // Equivalent to "A EqP B".  This may be the same as the condition tested
      // in the max/min; if so, we can just return that.
      if (Value *V = extractEquivalentCondition(LHS, EqP, A, B))
        return V;
      if (Value *V = extractEquivalentCondition(RHS, EqP, A, B))
        return V;
      // Otherwise, see if "A EqP B" simplifies.
      if (MaxRecurse)
        if (Value *V = simplifyICmpInst(EqP, A, B, Q, MaxRecurse - 1))
          return V;
      break;
    case CmpInst::ICMP_NE:
    case CmpInst::ICMP_SGT: {
      CmpInst::Predicate InvEqP = CmpInst::getInversePredicate(EqP);
      // Equivalent to "A InvEqP B".  This may be the same as the condition
      // tested in the max/min; if so, we can just return that.
      if (Value *V = extractEquivalentCondition(LHS, InvEqP, A, B))
        return V;
      if (Value *V = extractEquivalentCondition(RHS, InvEqP, A, B))
        return V;
      // Otherwise, see if "A InvEqP B" simplifies.
      if (MaxRecurse)
        if (Value *V = simplifyICmpInst(InvEqP, A, B, Q, MaxRecurse - 1))
          return V;
      break;
    }
    case CmpInst::ICMP_SGE:
      // Always true.
      return getTrue(ITy);
    case CmpInst::ICMP_SLT:
      // Always false.
      return getFalse(ITy);
    }
  }

  // Unsigned variants on "max(a,b)>=a -> true".
  P = CmpInst::BAD_ICMP_PREDICATE;
  if (match(LHS, m_UMax(m_Value(A), m_Value(B))) && (A == RHS || B == RHS)) {
    if (A != RHS)
      std::swap(A, B);       // umax(A, B) pred A.
    EqP = CmpInst::ICMP_UGE; // "A == umax(A, B)" iff "A uge B".
    // We analyze this as umax(A, B) pred A.
    P = Pred;
  } else if (match(RHS, m_UMax(m_Value(A), m_Value(B))) &&
             (A == LHS || B == LHS)) {
    if (A != LHS)
      std::swap(A, B);       // A pred umax(A, B).
    EqP = CmpInst::ICMP_UGE; // "A == umax(A, B)" iff "A uge B".
    // We analyze this as umax(A, B) swapped-pred A.
    P = CmpInst::getSwappedPredicate(Pred);
  } else if (match(LHS, m_UMin(m_Value(A), m_Value(B))) &&
             (A == RHS || B == RHS)) {
    if (A != RHS)
      std::swap(A, B);       // umin(A, B) pred A.
    EqP = CmpInst::ICMP_ULE; // "A == umin(A, B)" iff "A ule B".
    // We analyze this as umax(-A, -B) swapped-pred -A.
    // Note that we do not need to actually form -A or -B thanks to EqP.
    P = CmpInst::getSwappedPredicate(Pred);
  } else if (match(RHS, m_UMin(m_Value(A), m_Value(B))) &&
             (A == LHS || B == LHS)) {
    if (A != LHS)
      std::swap(A, B);       // A pred umin(A, B).
    EqP = CmpInst::ICMP_ULE; // "A == umin(A, B)" iff "A ule B".
    // We analyze this as umax(-A, -B) pred -A.
    // Note that we do not need to actually form -A or -B thanks to EqP.
    P = Pred;
  }
  if (P != CmpInst::BAD_ICMP_PREDICATE) {
    // Cases correspond to "max(A, B) p A".
    switch (P) {
    default:
      break;
    case CmpInst::ICMP_EQ:
    case CmpInst::ICMP_ULE:
      // Equivalent to "A EqP B".  This may be the same as the condition tested
      // in the max/min; if so, we can just return that.
      if (Value *V = extractEquivalentCondition(LHS, EqP, A, B))
        return V;
      if (Value *V = extractEquivalentCondition(RHS, EqP, A, B))
        return V;
      // Otherwise, see if "A EqP B" simplifies.
      if (MaxRecurse)
        if (Value *V = simplifyICmpInst(EqP, A, B, Q, MaxRecurse - 1))
          return V;
      break;
    case CmpInst::ICMP_NE:
    case CmpInst::ICMP_UGT: {
      CmpInst::Predicate InvEqP = CmpInst::getInversePredicate(EqP);
      // Equivalent to "A InvEqP B".  This may be the same as the condition
      // tested in the max/min; if so, we can just return that.
      if (Value *V = extractEquivalentCondition(LHS, InvEqP, A, B))
        return V;
      if (Value *V = extractEquivalentCondition(RHS, InvEqP, A, B))
        return V;
      // Otherwise, see if "A InvEqP B" simplifies.
      if (MaxRecurse)
        if (Value *V = simplifyICmpInst(InvEqP, A, B, Q, MaxRecurse - 1))
          return V;
      break;
    }
    case CmpInst::ICMP_UGE:
      return getTrue(ITy);
    case CmpInst::ICMP_ULT:
      return getFalse(ITy);
    }
  }

  // Comparing 1 each of min/max with a common operand?
  // Canonicalize min operand to RHS.
  if (match(LHS, m_UMin(m_Value(), m_Value())) ||
      match(LHS, m_SMin(m_Value(), m_Value()))) {
    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  Value *C, *D;
  if (match(LHS, m_SMax(m_Value(A), m_Value(B))) &&
      match(RHS, m_SMin(m_Value(C), m_Value(D))) &&
      (A == C || A == D || B == C || B == D)) {
    // smax(A, B) >=s smin(A, D) --> true
    if (Pred == CmpInst::ICMP_SGE)
      return getTrue(ITy);
    // smax(A, B) <s smin(A, D) --> false
    if (Pred == CmpInst::ICMP_SLT)
      return getFalse(ITy);
  } else if (match(LHS, m_UMax(m_Value(A), m_Value(B))) &&
             match(RHS, m_UMin(m_Value(C), m_Value(D))) &&
             (A == C || A == D || B == C || B == D)) {
    // umax(A, B) >=u umin(A, D) --> true
    if (Pred == CmpInst::ICMP_UGE)
      return getTrue(ITy);
    // umax(A, B) <u umin(A, D) --> false
    if (Pred == CmpInst::ICMP_ULT)
      return getFalse(ITy);
  }

  return nullptr;
}

static Value *simplifyICmpWithDominatingAssume(CmpInst::Predicate Predicate,
                                               Value *LHS, Value *RHS,
                                               const SimplifyQuery &Q) {
  // Gracefully handle instructions that have not been inserted yet.
  if (!Q.AC || !Q.CxtI)
    return nullptr;

  for (Value *AssumeBaseOp : {LHS, RHS}) {
    for (auto &AssumeVH : Q.AC->assumptionsFor(AssumeBaseOp)) {
      if (!AssumeVH)
        continue;

      CallInst *Assume = cast<CallInst>(AssumeVH);
      if (std::optional<bool> Imp = isImpliedCondition(
              Assume->getArgOperand(0), Predicate, LHS, RHS, Q.DL))
        if (isValidAssumeForContext(Assume, Q.CxtI, Q.DT))
          return ConstantInt::get(getCompareTy(LHS), *Imp);
    }
  }

  return nullptr;
}

static Value *simplifyICmpWithIntrinsicOnLHS(CmpInst::Predicate Pred,
                                             Value *LHS, Value *RHS) {
  auto *II = dyn_cast<IntrinsicInst>(LHS);
  if (!II)
    return nullptr;

  switch (II->getIntrinsicID()) {
  case Intrinsic::uadd_sat:
    // uadd.sat(X, Y) uge X, uadd.sat(X, Y) uge Y
    if (II->getArgOperand(0) == RHS || II->getArgOperand(1) == RHS) {
      if (Pred == ICmpInst::ICMP_UGE)
        return ConstantInt::getTrue(getCompareTy(II));
      if (Pred == ICmpInst::ICMP_ULT)
        return ConstantInt::getFalse(getCompareTy(II));
    }
    return nullptr;
  case Intrinsic::usub_sat:
    // usub.sat(X, Y) ule X
    if (II->getArgOperand(0) == RHS) {
      if (Pred == ICmpInst::ICMP_ULE)
        return ConstantInt::getTrue(getCompareTy(II));
      if (Pred == ICmpInst::ICMP_UGT)
        return ConstantInt::getFalse(getCompareTy(II));
    }
    return nullptr;
  default:
    return nullptr;
  }
}

/// Helper method to get range from metadata or attribute.
static std::optional<ConstantRange> getRange(Value *V,
                                             const InstrInfoQuery &IIQ) {
  if (Instruction *I = dyn_cast<Instruction>(V))
    if (MDNode *MD = IIQ.getMetadata(I, LLVMContext::MD_range))
      return getConstantRangeFromMetadata(*MD);

  if (const Argument *A = dyn_cast<Argument>(V))
    return A->getRange();
  else if (const CallBase *CB = dyn_cast<CallBase>(V))
    return CB->getRange();

  return std::nullopt;
}

/// Given operands for an ICmpInst, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyICmpInst(unsigned Predicate, Value *LHS, Value *RHS,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  CmpInst::Predicate Pred = (CmpInst::Predicate)Predicate;
  assert(CmpInst::isIntPredicate(Pred) && "Not an integer compare!");

  if (Constant *CLHS = dyn_cast<Constant>(LHS)) {
    if (Constant *CRHS = dyn_cast<Constant>(RHS))
      return ConstantFoldCompareInstOperands(Pred, CLHS, CRHS, Q.DL, Q.TLI);

    // If we have a constant, make sure it is on the RHS.
    std::swap(LHS, RHS);
    Pred = CmpInst::getSwappedPredicate(Pred);
  }
  assert(!isa<UndefValue>(LHS) && "Unexpected icmp undef,%X");

  Type *ITy = getCompareTy(LHS); // The return type.

  // icmp poison, X -> poison
  if (isa<PoisonValue>(RHS))
    return PoisonValue::get(ITy);

  // For EQ and NE, we can always pick a value for the undef to make the
  // predicate pass or fail, so we can return undef.
  // Matches behavior in llvm::ConstantFoldCompareInstruction.
  if (Q.isUndefValue(RHS) && ICmpInst::isEquality(Pred))
    return UndefValue::get(ITy);

  // icmp X, X -> true/false
  // icmp X, undef -> true/false because undef could be X.
  if (LHS == RHS || Q.isUndefValue(RHS))
    return ConstantInt::get(ITy, CmpInst::isTrueWhenEqual(Pred));

  if (Value *V = simplifyICmpOfBools(Pred, LHS, RHS, Q))
    return V;

  // TODO: Sink/common this with other potentially expensive calls that use
  //       ValueTracking? See comment below for isKnownNonEqual().
  if (Value *V = simplifyICmpWithZero(Pred, LHS, RHS, Q))
    return V;

  if (Value *V = simplifyICmpWithConstant(Pred, LHS, RHS, Q.IIQ))
    return V;

  // If both operands have range metadata, use the metadata
  // to simplify the comparison.
  if (std::optional<ConstantRange> RhsCr = getRange(RHS, Q.IIQ))
    if (std::optional<ConstantRange> LhsCr = getRange(LHS, Q.IIQ)) {
      if (LhsCr->icmp(Pred, *RhsCr))
        return ConstantInt::getTrue(ITy);

      if (LhsCr->icmp(CmpInst::getInversePredicate(Pred), *RhsCr))
        return ConstantInt::getFalse(ITy);
    }

  // Compare of cast, for example (zext X) != 0 -> X != 0
  if (isa<CastInst>(LHS) && (isa<Constant>(RHS) || isa<CastInst>(RHS))) {
    Instruction *LI = cast<CastInst>(LHS);
    Value *SrcOp = LI->getOperand(0);
    Type *SrcTy = SrcOp->getType();
    Type *DstTy = LI->getType();

    // Turn icmp (ptrtoint x), (ptrtoint/constant) into a compare of the input
    // if the integer type is the same size as the pointer type.
    if (MaxRecurse && isa<PtrToIntInst>(LI) &&
        Q.DL.getTypeSizeInBits(SrcTy) == DstTy->getPrimitiveSizeInBits()) {
      if (Constant *RHSC = dyn_cast<Constant>(RHS)) {
        // Transfer the cast to the constant.
        if (Value *V = simplifyICmpInst(Pred, SrcOp,
                                        ConstantExpr::getIntToPtr(RHSC, SrcTy),
                                        Q, MaxRecurse - 1))
          return V;
      } else if (PtrToIntInst *RI = dyn_cast<PtrToIntInst>(RHS)) {
        if (RI->getOperand(0)->getType() == SrcTy)
          // Compare without the cast.
          if (Value *V = simplifyICmpInst(Pred, SrcOp, RI->getOperand(0), Q,
                                          MaxRecurse - 1))
            return V;
      }
    }

    if (isa<ZExtInst>(LHS)) {
      // Turn icmp (zext X), (zext Y) into a compare of X and Y if they have the
      // same type.
      if (ZExtInst *RI = dyn_cast<ZExtInst>(RHS)) {
        if (MaxRecurse && SrcTy == RI->getOperand(0)->getType())
          // Compare X and Y.  Note that signed predicates become unsigned.
          if (Value *V =
                  simplifyICmpInst(ICmpInst::getUnsignedPredicate(Pred), SrcOp,
                                   RI->getOperand(0), Q, MaxRecurse - 1))
            return V;
      }
      // Fold (zext X) ule (sext X), (zext X) sge (sext X) to true.
      else if (SExtInst *RI = dyn_cast<SExtInst>(RHS)) {
        if (SrcOp == RI->getOperand(0)) {
          if (Pred == ICmpInst::ICMP_ULE || Pred == ICmpInst::ICMP_SGE)
            return ConstantInt::getTrue(ITy);
          if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_SLT)
            return ConstantInt::getFalse(ITy);
        }
      }
      // Turn icmp (zext X), Cst into a compare of X and Cst if Cst is extended
      // too.  If not, then try to deduce the result of the comparison.
      else if (match(RHS, m_ImmConstant())) {
        Constant *C = dyn_cast<Constant>(RHS);
        assert(C != nullptr);

        // Compute the constant that would happen if we truncated to SrcTy then
        // reextended to DstTy.
        Constant *Trunc =
            ConstantFoldCastOperand(Instruction::Trunc, C, SrcTy, Q.DL);
        assert(Trunc && "Constant-fold of ImmConstant should not fail");
        Constant *RExt =
            ConstantFoldCastOperand(CastInst::ZExt, Trunc, DstTy, Q.DL);
        assert(RExt && "Constant-fold of ImmConstant should not fail");
        Constant *AnyEq =
            ConstantFoldCompareInstOperands(ICmpInst::ICMP_EQ, RExt, C, Q.DL);
        assert(AnyEq && "Constant-fold of ImmConstant should not fail");

        // If the re-extended constant didn't change any of the elements then
        // this is effectively also a case of comparing two zero-extended
        // values.
        if (AnyEq->isAllOnesValue() && MaxRecurse)
          if (Value *V = simplifyICmpInst(ICmpInst::getUnsignedPredicate(Pred),
                                          SrcOp, Trunc, Q, MaxRecurse - 1))
            return V;

        // Otherwise the upper bits of LHS are zero while RHS has a non-zero bit
        // there.  Use this to work out the result of the comparison.
        if (AnyEq->isNullValue()) {
          switch (Pred) {
          default:
            llvm_unreachable("Unknown ICmp predicate!");
          // LHS <u RHS.
          case ICmpInst::ICMP_EQ:
          case ICmpInst::ICMP_UGT:
          case ICmpInst::ICMP_UGE:
            return Constant::getNullValue(ITy);

          case ICmpInst::ICMP_NE:
          case ICmpInst::ICMP_ULT:
          case ICmpInst::ICMP_ULE:
            return Constant::getAllOnesValue(ITy);

          // LHS is non-negative.  If RHS is negative then LHS >s LHS.  If RHS
          // is non-negative then LHS <s RHS.
          case ICmpInst::ICMP_SGT:
          case ICmpInst::ICMP_SGE:
            return ConstantFoldCompareInstOperands(
                ICmpInst::ICMP_SLT, C, Constant::getNullValue(C->getType()),
                Q.DL);
          case ICmpInst::ICMP_SLT:
          case ICmpInst::ICMP_SLE:
            return ConstantFoldCompareInstOperands(
                ICmpInst::ICMP_SGE, C, Constant::getNullValue(C->getType()),
                Q.DL);
          }
        }
      }
    }

    if (isa<SExtInst>(LHS)) {
      // Turn icmp (sext X), (sext Y) into a compare of X and Y if they have the
      // same type.
      if (SExtInst *RI = dyn_cast<SExtInst>(RHS)) {
        if (MaxRecurse && SrcTy == RI->getOperand(0)->getType())
          // Compare X and Y.  Note that the predicate does not change.
          if (Value *V = simplifyICmpInst(Pred, SrcOp, RI->getOperand(0), Q,
                                          MaxRecurse - 1))
            return V;
      }
      // Fold (sext X) uge (zext X), (sext X) sle (zext X) to true.
      else if (ZExtInst *RI = dyn_cast<ZExtInst>(RHS)) {
        if (SrcOp == RI->getOperand(0)) {
          if (Pred == ICmpInst::ICMP_UGE || Pred == ICmpInst::ICMP_SLE)
            return ConstantInt::getTrue(ITy);
          if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_SGT)
            return ConstantInt::getFalse(ITy);
        }
      }
      // Turn icmp (sext X), Cst into a compare of X and Cst if Cst is extended
      // too.  If not, then try to deduce the result of the comparison.
      else if (match(RHS, m_ImmConstant())) {
        Constant *C = cast<Constant>(RHS);

        // Compute the constant that would happen if we truncated to SrcTy then
        // reextended to DstTy.
        Constant *Trunc =
            ConstantFoldCastOperand(Instruction::Trunc, C, SrcTy, Q.DL);
        assert(Trunc && "Constant-fold of ImmConstant should not fail");
        Constant *RExt =
            ConstantFoldCastOperand(CastInst::SExt, Trunc, DstTy, Q.DL);
        assert(RExt && "Constant-fold of ImmConstant should not fail");
        Constant *AnyEq =
            ConstantFoldCompareInstOperands(ICmpInst::ICMP_EQ, RExt, C, Q.DL);
        assert(AnyEq && "Constant-fold of ImmConstant should not fail");

        // If the re-extended constant didn't change then this is effectively
        // also a case of comparing two sign-extended values.
        if (AnyEq->isAllOnesValue() && MaxRecurse)
          if (Value *V =
                  simplifyICmpInst(Pred, SrcOp, Trunc, Q, MaxRecurse - 1))
            return V;

        // Otherwise the upper bits of LHS are all equal, while RHS has varying
        // bits there.  Use this to work out the result of the comparison.
        if (AnyEq->isNullValue()) {
          switch (Pred) {
          default:
            llvm_unreachable("Unknown ICmp predicate!");
          case ICmpInst::ICMP_EQ:
            return Constant::getNullValue(ITy);
          case ICmpInst::ICMP_NE:
            return Constant::getAllOnesValue(ITy);

          // If RHS is non-negative then LHS <s RHS.  If RHS is negative then
          // LHS >s RHS.
          case ICmpInst::ICMP_SGT:
          case ICmpInst::ICMP_SGE:
            return ConstantFoldCompareInstOperands(
                ICmpInst::ICMP_SLT, C, Constant::getNullValue(C->getType()),
                Q.DL);
          case ICmpInst::ICMP_SLT:
          case ICmpInst::ICMP_SLE:
            return ConstantFoldCompareInstOperands(
                ICmpInst::ICMP_SGE, C, Constant::getNullValue(C->getType()),
                Q.DL);

          // If LHS is non-negative then LHS <u RHS.  If LHS is negative then
          // LHS >u RHS.
          case ICmpInst::ICMP_UGT:
          case ICmpInst::ICMP_UGE:
            // Comparison is true iff the LHS <s 0.
            if (MaxRecurse)
              if (Value *V = simplifyICmpInst(ICmpInst::ICMP_SLT, SrcOp,
                                              Constant::getNullValue(SrcTy), Q,
                                              MaxRecurse - 1))
                return V;
            break;
          case ICmpInst::ICMP_ULT:
          case ICmpInst::ICMP_ULE:
            // Comparison is true iff the LHS >=s 0.
            if (MaxRecurse)
              if (Value *V = simplifyICmpInst(ICmpInst::ICMP_SGE, SrcOp,
                                              Constant::getNullValue(SrcTy), Q,
                                              MaxRecurse - 1))
                return V;
            break;
          }
        }
      }
    }
  }

  // icmp eq|ne X, Y -> false|true if X != Y
  // This is potentially expensive, and we have already computedKnownBits for
  // compares with 0 above here, so only try this for a non-zero compare.
  if (ICmpInst::isEquality(Pred) && !match(RHS, m_Zero()) &&
      isKnownNonEqual(LHS, RHS, Q.DL, Q.AC, Q.CxtI, Q.DT, Q.IIQ.UseInstrInfo)) {
    return Pred == ICmpInst::ICMP_NE ? getTrue(ITy) : getFalse(ITy);
  }

  if (Value *V = simplifyICmpWithBinOp(Pred, LHS, RHS, Q, MaxRecurse))
    return V;

  if (Value *V = simplifyICmpWithMinMax(Pred, LHS, RHS, Q, MaxRecurse))
    return V;

  if (Value *V = simplifyICmpWithIntrinsicOnLHS(Pred, LHS, RHS))
    return V;
  if (Value *V = simplifyICmpWithIntrinsicOnLHS(
          ICmpInst::getSwappedPredicate(Pred), RHS, LHS))
    return V;

  if (Value *V = simplifyICmpWithDominatingAssume(Pred, LHS, RHS, Q))
    return V;

  if (std::optional<bool> Res =
          isImpliedByDomCondition(Pred, LHS, RHS, Q.CxtI, Q.DL))
    return ConstantInt::getBool(ITy, *Res);

  // Simplify comparisons of related pointers using a powerful, recursive
  // GEP-walk when we have target data available..
  if (LHS->getType()->isPointerTy())
    if (auto *C = computePointerICmp(Pred, LHS, RHS, Q))
      return C;
  if (auto *CLHS = dyn_cast<PtrToIntOperator>(LHS))
    if (auto *CRHS = dyn_cast<PtrToIntOperator>(RHS))
      if (CLHS->getPointerOperandType() == CRHS->getPointerOperandType() &&
          Q.DL.getTypeSizeInBits(CLHS->getPointerOperandType()) ==
              Q.DL.getTypeSizeInBits(CLHS->getType()))
        if (auto *C = computePointerICmp(Pred, CLHS->getPointerOperand(),
                                         CRHS->getPointerOperand(), Q))
          return C;

  // If the comparison is with the result of a select instruction, check whether
  // comparing with either branch of the select always yields the same value.
  if (isa<SelectInst>(LHS) || isa<SelectInst>(RHS))
    if (Value *V = threadCmpOverSelect(Pred, LHS, RHS, Q, MaxRecurse))
      return V;

  // If the comparison is with the result of a phi instruction, check whether
  // doing the compare with each incoming phi value yields a common result.
  if (isa<PHINode>(LHS) || isa<PHINode>(RHS))
    if (Value *V = threadCmpOverPHI(Pred, LHS, RHS, Q, MaxRecurse))
      return V;

  return nullptr;
}

Value *llvm::simplifyICmpInst(unsigned Predicate, Value *LHS, Value *RHS,
                              const SimplifyQuery &Q) {
  return ::simplifyICmpInst(Predicate, LHS, RHS, Q, RecursionLimit);
}

/// Given operands for an FCmpInst, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyFCmpInst(unsigned Predicate, Value *LHS, Value *RHS,
                               FastMathFlags FMF, const SimplifyQuery &Q,
                               unsigned MaxRecurse) {
  CmpInst::Predicate Pred = (CmpInst::Predicate)Predicate;
  assert(CmpInst::isFPPredicate(Pred) && "Not an FP compare!");

  if (Constant *CLHS = dyn_cast<Constant>(LHS)) {
    if (Constant *CRHS = dyn_cast<Constant>(RHS))
      return ConstantFoldCompareInstOperands(Pred, CLHS, CRHS, Q.DL, Q.TLI,
                                             Q.CxtI);

    // If we have a constant, make sure it is on the RHS.
    std::swap(LHS, RHS);
    Pred = CmpInst::getSwappedPredicate(Pred);
  }

  // Fold trivial predicates.
  Type *RetTy = getCompareTy(LHS);
  if (Pred == FCmpInst::FCMP_FALSE)
    return getFalse(RetTy);
  if (Pred == FCmpInst::FCMP_TRUE)
    return getTrue(RetTy);

  // fcmp pred x, poison and  fcmp pred poison, x
  // fold to poison
  if (isa<PoisonValue>(LHS) || isa<PoisonValue>(RHS))
    return PoisonValue::get(RetTy);

  // fcmp pred x, undef  and  fcmp pred undef, x
  // fold to true if unordered, false if ordered
  if (Q.isUndefValue(LHS) || Q.isUndefValue(RHS)) {
    // Choosing NaN for the undef will always make unordered comparison succeed
    // and ordered comparison fail.
    return ConstantInt::get(RetTy, CmpInst::isUnordered(Pred));
  }

  // fcmp x,x -> true/false.  Not all compares are foldable.
  if (LHS == RHS) {
    if (CmpInst::isTrueWhenEqual(Pred))
      return getTrue(RetTy);
    if (CmpInst::isFalseWhenEqual(Pred))
      return getFalse(RetTy);
  }

  // Fold (un)ordered comparison if we can determine there are no NaNs.
  //
  // This catches the 2 variable input case, constants are handled below as a
  // class-like compare.
  if (Pred == FCmpInst::FCMP_ORD || Pred == FCmpInst::FCMP_UNO) {
    KnownFPClass RHSClass =
        computeKnownFPClass(RHS, fcAllFlags, /*Depth=*/0, Q);
    KnownFPClass LHSClass =
        computeKnownFPClass(LHS, fcAllFlags, /*Depth=*/0, Q);

    if (FMF.noNaNs() ||
        (RHSClass.isKnownNeverNaN() && LHSClass.isKnownNeverNaN()))
      return ConstantInt::get(RetTy, Pred == FCmpInst::FCMP_ORD);

    if (RHSClass.isKnownAlwaysNaN() || LHSClass.isKnownAlwaysNaN())
      return ConstantInt::get(RetTy, Pred == CmpInst::FCMP_UNO);
  }

  const APFloat *C = nullptr;
  match(RHS, m_APFloatAllowPoison(C));
  std::optional<KnownFPClass> FullKnownClassLHS;

  // Lazily compute the possible classes for LHS. Avoid computing it twice if
  // RHS is a 0.
  auto computeLHSClass = [=, &FullKnownClassLHS](FPClassTest InterestedFlags =
                                                     fcAllFlags) {
    if (FullKnownClassLHS)
      return *FullKnownClassLHS;
    return computeKnownFPClass(LHS, FMF, InterestedFlags, 0, Q);
  };

  if (C && Q.CxtI) {
    // Fold out compares that express a class test.
    //
    // FIXME: Should be able to perform folds without context
    // instruction. Always pass in the context function?

    const Function *ParentF = Q.CxtI->getFunction();
    auto [ClassVal, ClassTest] = fcmpToClassTest(Pred, *ParentF, LHS, C);
    if (ClassVal) {
      FullKnownClassLHS = computeLHSClass();
      if ((FullKnownClassLHS->KnownFPClasses & ClassTest) == fcNone)
        return getFalse(RetTy);
      if ((FullKnownClassLHS->KnownFPClasses & ~ClassTest) == fcNone)
        return getTrue(RetTy);
    }
  }

  // Handle fcmp with constant RHS.
  if (C) {
    // TODO: If we always required a context function, we wouldn't need to
    // special case nans.
    if (C->isNaN())
      return ConstantInt::get(RetTy, CmpInst::isUnordered(Pred));

    // TODO: Need version fcmpToClassTest which returns implied class when the
    // compare isn't a complete class test. e.g. > 1.0 implies fcPositive, but
    // isn't implementable as a class call.
    if (C->isNegative() && !C->isNegZero()) {
      FPClassTest Interested = KnownFPClass::OrderedLessThanZeroMask;

      // TODO: We can catch more cases by using a range check rather than
      //       relying on CannotBeOrderedLessThanZero.
      switch (Pred) {
      case FCmpInst::FCMP_UGE:
      case FCmpInst::FCMP_UGT:
      case FCmpInst::FCMP_UNE: {
        KnownFPClass KnownClass = computeLHSClass(Interested);

        // (X >= 0) implies (X > C) when (C < 0)
        if (KnownClass.cannotBeOrderedLessThanZero())
          return getTrue(RetTy);
        break;
      }
      case FCmpInst::FCMP_OEQ:
      case FCmpInst::FCMP_OLE:
      case FCmpInst::FCMP_OLT: {
        KnownFPClass KnownClass = computeLHSClass(Interested);

        // (X >= 0) implies !(X < C) when (C < 0)
        if (KnownClass.cannotBeOrderedLessThanZero())
          return getFalse(RetTy);
        break;
      }
      default:
        break;
      }
    }
    // Check comparison of [minnum/maxnum with constant] with other constant.
    const APFloat *C2;
    if ((match(LHS, m_Intrinsic<Intrinsic::minnum>(m_Value(), m_APFloat(C2))) &&
         *C2 < *C) ||
        (match(LHS, m_Intrinsic<Intrinsic::maxnum>(m_Value(), m_APFloat(C2))) &&
         *C2 > *C)) {
      bool IsMaxNum =
          cast<IntrinsicInst>(LHS)->getIntrinsicID() == Intrinsic::maxnum;
      // The ordered relationship and minnum/maxnum guarantee that we do not
      // have NaN constants, so ordered/unordered preds are handled the same.
      switch (Pred) {
      case FCmpInst::FCMP_OEQ:
      case FCmpInst::FCMP_UEQ:
        // minnum(X, LesserC)  == C --> false
        // maxnum(X, GreaterC) == C --> false
        return getFalse(RetTy);
      case FCmpInst::FCMP_ONE:
      case FCmpInst::FCMP_UNE:
        // minnum(X, LesserC)  != C --> true
        // maxnum(X, GreaterC) != C --> true
        return getTrue(RetTy);
      case FCmpInst::FCMP_OGE:
      case FCmpInst::FCMP_UGE:
      case FCmpInst::FCMP_OGT:
      case FCmpInst::FCMP_UGT:
        // minnum(X, LesserC)  >= C --> false
        // minnum(X, LesserC)  >  C --> false
        // maxnum(X, GreaterC) >= C --> true
        // maxnum(X, GreaterC) >  C --> true
        return ConstantInt::get(RetTy, IsMaxNum);
      case FCmpInst::FCMP_OLE:
      case FCmpInst::FCMP_ULE:
      case FCmpInst::FCMP_OLT:
      case FCmpInst::FCMP_ULT:
        // minnum(X, LesserC)  <= C --> true
        // minnum(X, LesserC)  <  C --> true
        // maxnum(X, GreaterC) <= C --> false
        // maxnum(X, GreaterC) <  C --> false
        return ConstantInt::get(RetTy, !IsMaxNum);
      default:
        // TRUE/FALSE/ORD/UNO should be handled before this.
        llvm_unreachable("Unexpected fcmp predicate");
      }
    }
  }

  // TODO: Could fold this with above if there were a matcher which returned all
  // classes in a non-splat vector.
  if (match(RHS, m_AnyZeroFP())) {
    switch (Pred) {
    case FCmpInst::FCMP_OGE:
    case FCmpInst::FCMP_ULT: {
      FPClassTest Interested = KnownFPClass::OrderedLessThanZeroMask;
      if (!FMF.noNaNs())
        Interested |= fcNan;

      KnownFPClass Known = computeLHSClass(Interested);

      // Positive or zero X >= 0.0 --> true
      // Positive or zero X <  0.0 --> false
      if ((FMF.noNaNs() || Known.isKnownNeverNaN()) &&
          Known.cannotBeOrderedLessThanZero())
        return Pred == FCmpInst::FCMP_OGE ? getTrue(RetTy) : getFalse(RetTy);
      break;
    }
    case FCmpInst::FCMP_UGE:
    case FCmpInst::FCMP_OLT: {
      FPClassTest Interested = KnownFPClass::OrderedLessThanZeroMask;
      KnownFPClass Known = computeLHSClass(Interested);

      // Positive or zero or nan X >= 0.0 --> true
      // Positive or zero or nan X <  0.0 --> false
      if (Known.cannotBeOrderedLessThanZero())
        return Pred == FCmpInst::FCMP_UGE ? getTrue(RetTy) : getFalse(RetTy);
      break;
    }
    default:
      break;
    }
  }

  // If the comparison is with the result of a select instruction, check whether
  // comparing with either branch of the select always yields the same value.
  if (isa<SelectInst>(LHS) || isa<SelectInst>(RHS))
    if (Value *V = threadCmpOverSelect(Pred, LHS, RHS, Q, MaxRecurse))
      return V;

  // If the comparison is with the result of a phi instruction, check whether
  // doing the compare with each incoming phi value yields a common result.
  if (isa<PHINode>(LHS) || isa<PHINode>(RHS))
    if (Value *V = threadCmpOverPHI(Pred, LHS, RHS, Q, MaxRecurse))
      return V;

  return nullptr;
}

Value *llvm::simplifyFCmpInst(unsigned Predicate, Value *LHS, Value *RHS,
                              FastMathFlags FMF, const SimplifyQuery &Q) {
  return ::simplifyFCmpInst(Predicate, LHS, RHS, FMF, Q, RecursionLimit);
}

static Value *simplifyWithOpReplaced(Value *V, Value *Op, Value *RepOp,
                                     const SimplifyQuery &Q,
                                     bool AllowRefinement,
                                     SmallVectorImpl<Instruction *> *DropFlags,
                                     unsigned MaxRecurse) {
  assert((AllowRefinement || !Q.CanUseUndef) &&
         "If AllowRefinement=false then CanUseUndef=false");

  // Trivial replacement.
  if (V == Op)
    return RepOp;

  if (!MaxRecurse--)
    return nullptr;

  // We cannot replace a constant, and shouldn't even try.
  if (isa<Constant>(Op))
    return nullptr;

  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return nullptr;

  // The arguments of a phi node might refer to a value from a previous
  // cycle iteration.
  if (isa<PHINode>(I))
    return nullptr;

  if (Op->getType()->isVectorTy()) {
    // For vector types, the simplification must hold per-lane, so forbid
    // potentially cross-lane operations like shufflevector.
    if (!I->getType()->isVectorTy() || isa<ShuffleVectorInst>(I) ||
        isa<CallBase>(I) || isa<BitCastInst>(I))
      return nullptr;
  }

  // Don't fold away llvm.is.constant checks based on assumptions.
  if (match(I, m_Intrinsic<Intrinsic::is_constant>()))
    return nullptr;

  // Don't simplify freeze.
  if (isa<FreezeInst>(I))
    return nullptr;

  // Replace Op with RepOp in instruction operands.
  SmallVector<Value *, 8> NewOps;
  bool AnyReplaced = false;
  for (Value *InstOp : I->operands()) {
    if (Value *NewInstOp = simplifyWithOpReplaced(
            InstOp, Op, RepOp, Q, AllowRefinement, DropFlags, MaxRecurse)) {
      NewOps.push_back(NewInstOp);
      AnyReplaced = InstOp != NewInstOp;
    } else {
      NewOps.push_back(InstOp);
    }

    // Bail out if any operand is undef and SimplifyQuery disables undef
    // simplification. Constant folding currently doesn't respect this option.
    if (isa<UndefValue>(NewOps.back()) && !Q.CanUseUndef)
      return nullptr;
  }

  if (!AnyReplaced)
    return nullptr;

  if (!AllowRefinement) {
    // General InstSimplify functions may refine the result, e.g. by returning
    // a constant for a potentially poison value. To avoid this, implement only
    // a few non-refining but profitable transforms here.

    if (auto *BO = dyn_cast<BinaryOperator>(I)) {
      unsigned Opcode = BO->getOpcode();
      // id op x -> x, x op id -> x
      if (NewOps[0] == ConstantExpr::getBinOpIdentity(Opcode, I->getType()))
        return NewOps[1];
      if (NewOps[1] == ConstantExpr::getBinOpIdentity(Opcode, I->getType(),
                                                      /* RHS */ true))
        return NewOps[0];

      // x & x -> x, x | x -> x
      if ((Opcode == Instruction::And || Opcode == Instruction::Or) &&
          NewOps[0] == NewOps[1]) {
        // or disjoint x, x results in poison.
        if (auto *PDI = dyn_cast<PossiblyDisjointInst>(BO)) {
          if (PDI->isDisjoint()) {
            if (!DropFlags)
              return nullptr;
            DropFlags->push_back(BO);
          }
        }
        return NewOps[0];
      }

      // x - x -> 0, x ^ x -> 0. This is non-refining, because x is non-poison
      // by assumption and this case never wraps, so nowrap flags can be
      // ignored.
      if ((Opcode == Instruction::Sub || Opcode == Instruction::Xor) &&
          NewOps[0] == RepOp && NewOps[1] == RepOp)
        return Constant::getNullValue(I->getType());

      // If we are substituting an absorber constant into a binop and extra
      // poison can't leak if we remove the select -- because both operands of
      // the binop are based on the same value -- then it may be safe to replace
      // the value with the absorber constant. Examples:
      // (Op == 0) ? 0 : (Op & -Op)            --> Op & -Op
      // (Op == 0) ? 0 : (Op * (binop Op, C))  --> Op * (binop Op, C)
      // (Op == -1) ? -1 : (Op | (binop C, Op) --> Op | (binop C, Op)
      Constant *Absorber =
          ConstantExpr::getBinOpAbsorber(Opcode, I->getType());
      if ((NewOps[0] == Absorber || NewOps[1] == Absorber) &&
          impliesPoison(BO, Op))
        return Absorber;
    }

    if (isa<GetElementPtrInst>(I)) {
      // getelementptr x, 0 -> x.
      // This never returns poison, even if inbounds is set.
      if (NewOps.size() == 2 && match(NewOps[1], m_Zero()))
        return NewOps[0];
    }
  } else {
    // The simplification queries below may return the original value. Consider:
    //   %div = udiv i32 %arg, %arg2
    //   %mul = mul nsw i32 %div, %arg2
    //   %cmp = icmp eq i32 %mul, %arg
    //   %sel = select i1 %cmp, i32 %div, i32 undef
    // Replacing %arg by %mul, %div becomes "udiv i32 %mul, %arg2", which
    // simplifies back to %arg. This can only happen because %mul does not
    // dominate %div. To ensure a consistent return value contract, we make sure
    // that this case returns nullptr as well.
    auto PreventSelfSimplify = [V](Value *Simplified) {
      return Simplified != V ? Simplified : nullptr;
    };

    return PreventSelfSimplify(
        ::simplifyInstructionWithOperands(I, NewOps, Q, MaxRecurse));
  }

  // If all operands are constant after substituting Op for RepOp then we can
  // constant fold the instruction.
  SmallVector<Constant *, 8> ConstOps;
  for (Value *NewOp : NewOps) {
    if (Constant *ConstOp = dyn_cast<Constant>(NewOp))
      ConstOps.push_back(ConstOp);
    else
      return nullptr;
  }

  // Consider:
  //   %cmp = icmp eq i32 %x, 2147483647
  //   %add = add nsw i32 %x, 1
  //   %sel = select i1 %cmp, i32 -2147483648, i32 %add
  //
  // We can't replace %sel with %add unless we strip away the flags (which
  // will be done in InstCombine).
  // TODO: This may be unsound, because it only catches some forms of
  // refinement.
  if (!AllowRefinement) {
    if (canCreatePoison(cast<Operator>(I), !DropFlags)) {
      // abs cannot create poison if the value is known to never be int_min.
      if (auto *II = dyn_cast<IntrinsicInst>(I);
          II && II->getIntrinsicID() == Intrinsic::abs) {
        if (!ConstOps[0]->isNotMinSignedValue())
          return nullptr;
      } else
        return nullptr;
    }
    Constant *Res = ConstantFoldInstOperands(I, ConstOps, Q.DL, Q.TLI);
    if (DropFlags && Res && I->hasPoisonGeneratingAnnotations())
      DropFlags->push_back(I);
    return Res;
  }

  return ConstantFoldInstOperands(I, ConstOps, Q.DL, Q.TLI);
}

Value *llvm::simplifyWithOpReplaced(Value *V, Value *Op, Value *RepOp,
                                    const SimplifyQuery &Q,
                                    bool AllowRefinement,
                                    SmallVectorImpl<Instruction *> *DropFlags) {
  // If refinement is disabled, also disable undef simplifications (which are
  // always refinements) in SimplifyQuery.
  if (!AllowRefinement)
    return ::simplifyWithOpReplaced(V, Op, RepOp, Q.getWithoutUndef(),
                                    AllowRefinement, DropFlags, RecursionLimit);
  return ::simplifyWithOpReplaced(V, Op, RepOp, Q, AllowRefinement, DropFlags,
                                  RecursionLimit);
}

/// Try to simplify a select instruction when its condition operand is an
/// integer comparison where one operand of the compare is a constant.
static Value *simplifySelectBitTest(Value *TrueVal, Value *FalseVal, Value *X,
                                    const APInt *Y, bool TrueWhenUnset) {
  const APInt *C;

  // (X & Y) == 0 ? X & ~Y : X  --> X
  // (X & Y) != 0 ? X & ~Y : X  --> X & ~Y
  if (FalseVal == X && match(TrueVal, m_And(m_Specific(X), m_APInt(C))) &&
      *Y == ~*C)
    return TrueWhenUnset ? FalseVal : TrueVal;

  // (X & Y) == 0 ? X : X & ~Y  --> X & ~Y
  // (X & Y) != 0 ? X : X & ~Y  --> X
  if (TrueVal == X && match(FalseVal, m_And(m_Specific(X), m_APInt(C))) &&
      *Y == ~*C)
    return TrueWhenUnset ? FalseVal : TrueVal;

  if (Y->isPowerOf2()) {
    // (X & Y) == 0 ? X | Y : X  --> X | Y
    // (X & Y) != 0 ? X | Y : X  --> X
    if (FalseVal == X && match(TrueVal, m_Or(m_Specific(X), m_APInt(C))) &&
        *Y == *C) {
      // We can't return the or if it has the disjoint flag.
      if (TrueWhenUnset && cast<PossiblyDisjointInst>(TrueVal)->isDisjoint())
        return nullptr;
      return TrueWhenUnset ? TrueVal : FalseVal;
    }

    // (X & Y) == 0 ? X : X | Y  --> X
    // (X & Y) != 0 ? X : X | Y  --> X | Y
    if (TrueVal == X && match(FalseVal, m_Or(m_Specific(X), m_APInt(C))) &&
        *Y == *C) {
      // We can't return the or if it has the disjoint flag.
      if (!TrueWhenUnset && cast<PossiblyDisjointInst>(FalseVal)->isDisjoint())
        return nullptr;
      return TrueWhenUnset ? TrueVal : FalseVal;
    }
  }

  return nullptr;
}

static Value *simplifyCmpSelOfMaxMin(Value *CmpLHS, Value *CmpRHS,
                                     ICmpInst::Predicate Pred, Value *TVal,
                                     Value *FVal) {
  // Canonicalize common cmp+sel operand as CmpLHS.
  if (CmpRHS == TVal || CmpRHS == FVal) {
    std::swap(CmpLHS, CmpRHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  // Canonicalize common cmp+sel operand as TVal.
  if (CmpLHS == FVal) {
    std::swap(TVal, FVal);
    Pred = ICmpInst::getInversePredicate(Pred);
  }

  // A vector select may be shuffling together elements that are equivalent
  // based on the max/min/select relationship.
  Value *X = CmpLHS, *Y = CmpRHS;
  bool PeekedThroughSelectShuffle = false;
  auto *Shuf = dyn_cast<ShuffleVectorInst>(FVal);
  if (Shuf && Shuf->isSelect()) {
    if (Shuf->getOperand(0) == Y)
      FVal = Shuf->getOperand(1);
    else if (Shuf->getOperand(1) == Y)
      FVal = Shuf->getOperand(0);
    else
      return nullptr;
    PeekedThroughSelectShuffle = true;
  }

  // (X pred Y) ? X : max/min(X, Y)
  auto *MMI = dyn_cast<MinMaxIntrinsic>(FVal);
  if (!MMI || TVal != X ||
      !match(FVal, m_c_MaxOrMin(m_Specific(X), m_Specific(Y))))
    return nullptr;

  // (X >  Y) ? X : max(X, Y) --> max(X, Y)
  // (X >= Y) ? X : max(X, Y) --> max(X, Y)
  // (X <  Y) ? X : min(X, Y) --> min(X, Y)
  // (X <= Y) ? X : min(X, Y) --> min(X, Y)
  //
  // The equivalence allows a vector select (shuffle) of max/min and Y. Ex:
  // (X > Y) ? X : (Z ? max(X, Y) : Y)
  // If Z is true, this reduces as above, and if Z is false:
  // (X > Y) ? X : Y --> max(X, Y)
  ICmpInst::Predicate MMPred = MMI->getPredicate();
  if (MMPred == CmpInst::getStrictPredicate(Pred))
    return MMI;

  // Other transforms are not valid with a shuffle.
  if (PeekedThroughSelectShuffle)
    return nullptr;

  // (X == Y) ? X : max/min(X, Y) --> max/min(X, Y)
  if (Pred == CmpInst::ICMP_EQ)
    return MMI;

  // (X != Y) ? X : max/min(X, Y) --> X
  if (Pred == CmpInst::ICMP_NE)
    return X;

  // (X <  Y) ? X : max(X, Y) --> X
  // (X <= Y) ? X : max(X, Y) --> X
  // (X >  Y) ? X : min(X, Y) --> X
  // (X >= Y) ? X : min(X, Y) --> X
  ICmpInst::Predicate InvPred = CmpInst::getInversePredicate(Pred);
  if (MMPred == CmpInst::getStrictPredicate(InvPred))
    return X;

  return nullptr;
}

/// An alternative way to test if a bit is set or not uses sgt/slt instead of
/// eq/ne.
static Value *simplifySelectWithFakeICmpEq(Value *CmpLHS, Value *CmpRHS,
                                           ICmpInst::Predicate Pred,
                                           Value *TrueVal, Value *FalseVal) {
  Value *X;
  APInt Mask;
  if (!decomposeBitTestICmp(CmpLHS, CmpRHS, Pred, X, Mask))
    return nullptr;

  return simplifySelectBitTest(TrueVal, FalseVal, X, &Mask,
                               Pred == ICmpInst::ICMP_EQ);
}

/// Try to simplify a select instruction when its condition operand is an
/// integer equality comparison.
static Value *simplifySelectWithICmpEq(Value *CmpLHS, Value *CmpRHS,
                                       Value *TrueVal, Value *FalseVal,
                                       const SimplifyQuery &Q,
                                       unsigned MaxRecurse) {
  if (simplifyWithOpReplaced(FalseVal, CmpLHS, CmpRHS, Q.getWithoutUndef(),
                             /* AllowRefinement */ false,
                             /* DropFlags */ nullptr, MaxRecurse) == TrueVal)
    return FalseVal;
  if (simplifyWithOpReplaced(TrueVal, CmpLHS, CmpRHS, Q,
                             /* AllowRefinement */ true,
                             /* DropFlags */ nullptr, MaxRecurse) == FalseVal)
    return FalseVal;

  return nullptr;
}

/// Try to simplify a select instruction when its condition operand is an
/// integer comparison.
static Value *simplifySelectWithICmpCond(Value *CondVal, Value *TrueVal,
                                         Value *FalseVal,
                                         const SimplifyQuery &Q,
                                         unsigned MaxRecurse) {
  ICmpInst::Predicate Pred;
  Value *CmpLHS, *CmpRHS;
  if (!match(CondVal, m_ICmp(Pred, m_Value(CmpLHS), m_Value(CmpRHS))))
    return nullptr;

  if (Value *V = simplifyCmpSelOfMaxMin(CmpLHS, CmpRHS, Pred, TrueVal, FalseVal))
    return V;

  // Canonicalize ne to eq predicate.
  if (Pred == ICmpInst::ICMP_NE) {
    Pred = ICmpInst::ICMP_EQ;
    std::swap(TrueVal, FalseVal);
  }

  // Check for integer min/max with a limit constant:
  // X > MIN_INT ? X : MIN_INT --> X
  // X < MAX_INT ? X : MAX_INT --> X
  if (TrueVal->getType()->isIntOrIntVectorTy()) {
    Value *X, *Y;
    SelectPatternFlavor SPF =
        matchDecomposedSelectPattern(cast<ICmpInst>(CondVal), TrueVal, FalseVal,
                                     X, Y)
            .Flavor;
    if (SelectPatternResult::isMinOrMax(SPF) && Pred == getMinMaxPred(SPF)) {
      APInt LimitC = getMinMaxLimit(getInverseMinMaxFlavor(SPF),
                                    X->getType()->getScalarSizeInBits());
      if (match(Y, m_SpecificInt(LimitC)))
        return X;
    }
  }

  if (Pred == ICmpInst::ICMP_EQ && match(CmpRHS, m_Zero())) {
    Value *X;
    const APInt *Y;
    if (match(CmpLHS, m_And(m_Value(X), m_APInt(Y))))
      if (Value *V = simplifySelectBitTest(TrueVal, FalseVal, X, Y,
                                           /*TrueWhenUnset=*/true))
        return V;

    // Test for a bogus zero-shift-guard-op around funnel-shift or rotate.
    Value *ShAmt;
    auto isFsh = m_CombineOr(m_FShl(m_Value(X), m_Value(), m_Value(ShAmt)),
                             m_FShr(m_Value(), m_Value(X), m_Value(ShAmt)));
    // (ShAmt == 0) ? fshl(X, *, ShAmt) : X --> X
    // (ShAmt == 0) ? fshr(*, X, ShAmt) : X --> X
    if (match(TrueVal, isFsh) && FalseVal == X && CmpLHS == ShAmt)
      return X;

    // Test for a zero-shift-guard-op around rotates. These are used to
    // avoid UB from oversized shifts in raw IR rotate patterns, but the
    // intrinsics do not have that problem.
    // We do not allow this transform for the general funnel shift case because
    // that would not preserve the poison safety of the original code.
    auto isRotate =
        m_CombineOr(m_FShl(m_Value(X), m_Deferred(X), m_Value(ShAmt)),
                    m_FShr(m_Value(X), m_Deferred(X), m_Value(ShAmt)));
    // (ShAmt == 0) ? X : fshl(X, X, ShAmt) --> fshl(X, X, ShAmt)
    // (ShAmt == 0) ? X : fshr(X, X, ShAmt) --> fshr(X, X, ShAmt)
    if (match(FalseVal, isRotate) && TrueVal == X && CmpLHS == ShAmt &&
        Pred == ICmpInst::ICMP_EQ)
      return FalseVal;

    // X == 0 ? abs(X) : -abs(X) --> -abs(X)
    // X == 0 ? -abs(X) : abs(X) --> abs(X)
    if (match(TrueVal, m_Intrinsic<Intrinsic::abs>(m_Specific(CmpLHS))) &&
        match(FalseVal, m_Neg(m_Intrinsic<Intrinsic::abs>(m_Specific(CmpLHS)))))
      return FalseVal;
    if (match(TrueVal,
              m_Neg(m_Intrinsic<Intrinsic::abs>(m_Specific(CmpLHS)))) &&
        match(FalseVal, m_Intrinsic<Intrinsic::abs>(m_Specific(CmpLHS))))
      return FalseVal;
  }

  // Check for other compares that behave like bit test.
  if (Value *V =
          simplifySelectWithFakeICmpEq(CmpLHS, CmpRHS, Pred, TrueVal, FalseVal))
    return V;

  // If we have a scalar equality comparison, then we know the value in one of
  // the arms of the select. See if substituting this value into the arm and
  // simplifying the result yields the same value as the other arm.
  if (Pred == ICmpInst::ICMP_EQ) {
    if (Value *V = simplifySelectWithICmpEq(CmpLHS, CmpRHS, TrueVal, FalseVal,
                                            Q, MaxRecurse))
      return V;
    if (Value *V = simplifySelectWithICmpEq(CmpRHS, CmpLHS, TrueVal, FalseVal,
                                            Q, MaxRecurse))
      return V;

    Value *X;
    Value *Y;
    // select((X | Y) == 0 ?  X : 0) --> 0 (commuted 2 ways)
    if (match(CmpLHS, m_Or(m_Value(X), m_Value(Y))) &&
        match(CmpRHS, m_Zero())) {
      // (X | Y) == 0 implies X == 0 and Y == 0.
      if (Value *V = simplifySelectWithICmpEq(X, CmpRHS, TrueVal, FalseVal, Q,
                                              MaxRecurse))
        return V;
      if (Value *V = simplifySelectWithICmpEq(Y, CmpRHS, TrueVal, FalseVal, Q,
                                              MaxRecurse))
        return V;
    }

    // select((X & Y) == -1 ?  X : -1) --> -1 (commuted 2 ways)
    if (match(CmpLHS, m_And(m_Value(X), m_Value(Y))) &&
        match(CmpRHS, m_AllOnes())) {
      // (X & Y) == -1 implies X == -1 and Y == -1.
      if (Value *V = simplifySelectWithICmpEq(X, CmpRHS, TrueVal, FalseVal, Q,
                                              MaxRecurse))
        return V;
      if (Value *V = simplifySelectWithICmpEq(Y, CmpRHS, TrueVal, FalseVal, Q,
                                              MaxRecurse))
        return V;
    }
  }

  return nullptr;
}

/// Try to simplify a select instruction when its condition operand is a
/// floating-point comparison.
static Value *simplifySelectWithFCmp(Value *Cond, Value *T, Value *F,
                                     const SimplifyQuery &Q) {
  FCmpInst::Predicate Pred;
  if (!match(Cond, m_FCmp(Pred, m_Specific(T), m_Specific(F))) &&
      !match(Cond, m_FCmp(Pred, m_Specific(F), m_Specific(T))))
    return nullptr;

  // This transform is safe if we do not have (do not care about) -0.0 or if
  // at least one operand is known to not be -0.0. Otherwise, the select can
  // change the sign of a zero operand.
  bool HasNoSignedZeros =
      Q.CxtI && isa<FPMathOperator>(Q.CxtI) && Q.CxtI->hasNoSignedZeros();
  const APFloat *C;
  if (HasNoSignedZeros || (match(T, m_APFloat(C)) && C->isNonZero()) ||
      (match(F, m_APFloat(C)) && C->isNonZero())) {
    // (T == F) ? T : F --> F
    // (F == T) ? T : F --> F
    if (Pred == FCmpInst::FCMP_OEQ)
      return F;

    // (T != F) ? T : F --> T
    // (F != T) ? T : F --> T
    if (Pred == FCmpInst::FCMP_UNE)
      return T;
  }

  return nullptr;
}

/// Given operands for a SelectInst, see if we can fold the result.
/// If not, this returns null.
static Value *simplifySelectInst(Value *Cond, Value *TrueVal, Value *FalseVal,
                                 const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (auto *CondC = dyn_cast<Constant>(Cond)) {
    if (auto *TrueC = dyn_cast<Constant>(TrueVal))
      if (auto *FalseC = dyn_cast<Constant>(FalseVal))
        if (Constant *C = ConstantFoldSelectInstruction(CondC, TrueC, FalseC))
          return C;

    // select poison, X, Y -> poison
    if (isa<PoisonValue>(CondC))
      return PoisonValue::get(TrueVal->getType());

    // select undef, X, Y -> X or Y
    if (Q.isUndefValue(CondC))
      return isa<Constant>(FalseVal) ? FalseVal : TrueVal;

    // select true,  X, Y --> X
    // select false, X, Y --> Y
    // For vectors, allow undef/poison elements in the condition to match the
    // defined elements, so we can eliminate the select.
    if (match(CondC, m_One()))
      return TrueVal;
    if (match(CondC, m_Zero()))
      return FalseVal;
  }

  assert(Cond->getType()->isIntOrIntVectorTy(1) &&
         "Select must have bool or bool vector condition");
  assert(TrueVal->getType() == FalseVal->getType() &&
         "Select must have same types for true/false ops");

  if (Cond->getType() == TrueVal->getType()) {
    // select i1 Cond, i1 true, i1 false --> i1 Cond
    if (match(TrueVal, m_One()) && match(FalseVal, m_ZeroInt()))
      return Cond;

    // (X && Y) ? X : Y --> Y (commuted 2 ways)
    if (match(Cond, m_c_LogicalAnd(m_Specific(TrueVal), m_Specific(FalseVal))))
      return FalseVal;

    // (X || Y) ? X : Y --> X (commuted 2 ways)
    if (match(Cond, m_c_LogicalOr(m_Specific(TrueVal), m_Specific(FalseVal))))
      return TrueVal;

    // (X || Y) ? false : X --> false (commuted 2 ways)
    if (match(Cond, m_c_LogicalOr(m_Specific(FalseVal), m_Value())) &&
        match(TrueVal, m_ZeroInt()))
      return ConstantInt::getFalse(Cond->getType());

    // Match patterns that end in logical-and.
    if (match(FalseVal, m_ZeroInt())) {
      // !(X || Y) && X --> false (commuted 2 ways)
      if (match(Cond, m_Not(m_c_LogicalOr(m_Specific(TrueVal), m_Value()))))
        return ConstantInt::getFalse(Cond->getType());
      // X && !(X || Y) --> false (commuted 2 ways)
      if (match(TrueVal, m_Not(m_c_LogicalOr(m_Specific(Cond), m_Value()))))
        return ConstantInt::getFalse(Cond->getType());

      // (X || Y) && Y --> Y (commuted 2 ways)
      if (match(Cond, m_c_LogicalOr(m_Specific(TrueVal), m_Value())))
        return TrueVal;
      // Y && (X || Y) --> Y (commuted 2 ways)
      if (match(TrueVal, m_c_LogicalOr(m_Specific(Cond), m_Value())))
        return Cond;

      // (X || Y) && (X || !Y) --> X (commuted 8 ways)
      Value *X, *Y;
      if (match(Cond, m_c_LogicalOr(m_Value(X), m_Not(m_Value(Y)))) &&
          match(TrueVal, m_c_LogicalOr(m_Specific(X), m_Specific(Y))))
        return X;
      if (match(TrueVal, m_c_LogicalOr(m_Value(X), m_Not(m_Value(Y)))) &&
          match(Cond, m_c_LogicalOr(m_Specific(X), m_Specific(Y))))
        return X;
    }

    // Match patterns that end in logical-or.
    if (match(TrueVal, m_One())) {
      // !(X && Y) || X --> true (commuted 2 ways)
      if (match(Cond, m_Not(m_c_LogicalAnd(m_Specific(FalseVal), m_Value()))))
        return ConstantInt::getTrue(Cond->getType());
      // X || !(X && Y) --> true (commuted 2 ways)
      if (match(FalseVal, m_Not(m_c_LogicalAnd(m_Specific(Cond), m_Value()))))
        return ConstantInt::getTrue(Cond->getType());

      // (X && Y) || Y --> Y (commuted 2 ways)
      if (match(Cond, m_c_LogicalAnd(m_Specific(FalseVal), m_Value())))
        return FalseVal;
      // Y || (X && Y) --> Y (commuted 2 ways)
      if (match(FalseVal, m_c_LogicalAnd(m_Specific(Cond), m_Value())))
        return Cond;
    }
  }

  // select ?, X, X -> X
  if (TrueVal == FalseVal)
    return TrueVal;

  if (Cond == TrueVal) {
    // select i1 X, i1 X, i1 false --> X (logical-and)
    if (match(FalseVal, m_ZeroInt()))
      return Cond;
    // select i1 X, i1 X, i1 true --> true
    if (match(FalseVal, m_One()))
      return ConstantInt::getTrue(Cond->getType());
  }
  if (Cond == FalseVal) {
    // select i1 X, i1 true, i1 X --> X (logical-or)
    if (match(TrueVal, m_One()))
      return Cond;
    // select i1 X, i1 false, i1 X --> false
    if (match(TrueVal, m_ZeroInt()))
      return ConstantInt::getFalse(Cond->getType());
  }

  // If the true or false value is poison, we can fold to the other value.
  // If the true or false value is undef, we can fold to the other value as
  // long as the other value isn't poison.
  // select ?, poison, X -> X
  // select ?, undef,  X -> X
  if (isa<PoisonValue>(TrueVal) ||
      (Q.isUndefValue(TrueVal) && impliesPoison(FalseVal, Cond)))
    return FalseVal;
  // select ?, X, poison -> X
  // select ?, X, undef  -> X
  if (isa<PoisonValue>(FalseVal) ||
      (Q.isUndefValue(FalseVal) && impliesPoison(TrueVal, Cond)))
    return TrueVal;

  // Deal with partial undef vector constants: select ?, VecC, VecC' --> VecC''
  Constant *TrueC, *FalseC;
  if (isa<FixedVectorType>(TrueVal->getType()) &&
      match(TrueVal, m_Constant(TrueC)) &&
      match(FalseVal, m_Constant(FalseC))) {
    unsigned NumElts =
        cast<FixedVectorType>(TrueC->getType())->getNumElements();
    SmallVector<Constant *, 16> NewC;
    for (unsigned i = 0; i != NumElts; ++i) {
      // Bail out on incomplete vector constants.
      Constant *TEltC = TrueC->getAggregateElement(i);
      Constant *FEltC = FalseC->getAggregateElement(i);
      if (!TEltC || !FEltC)
        break;

      // If the elements match (undef or not), that value is the result. If only
      // one element is undef, choose the defined element as the safe result.
      if (TEltC == FEltC)
        NewC.push_back(TEltC);
      else if (isa<PoisonValue>(TEltC) ||
               (Q.isUndefValue(TEltC) && isGuaranteedNotToBePoison(FEltC)))
        NewC.push_back(FEltC);
      else if (isa<PoisonValue>(FEltC) ||
               (Q.isUndefValue(FEltC) && isGuaranteedNotToBePoison(TEltC)))
        NewC.push_back(TEltC);
      else
        break;
    }
    if (NewC.size() == NumElts)
      return ConstantVector::get(NewC);
  }

  if (Value *V =
          simplifySelectWithICmpCond(Cond, TrueVal, FalseVal, Q, MaxRecurse))
    return V;

  if (Value *V = simplifySelectWithFCmp(Cond, TrueVal, FalseVal, Q))
    return V;

  if (Value *V = foldSelectWithBinaryOp(Cond, TrueVal, FalseVal))
    return V;

  std::optional<bool> Imp = isImpliedByDomCondition(Cond, Q.CxtI, Q.DL);
  if (Imp)
    return *Imp ? TrueVal : FalseVal;

  return nullptr;
}

Value *llvm::simplifySelectInst(Value *Cond, Value *TrueVal, Value *FalseVal,
                                const SimplifyQuery &Q) {
  return ::simplifySelectInst(Cond, TrueVal, FalseVal, Q, RecursionLimit);
}

/// Given operands for an GetElementPtrInst, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyGEPInst(Type *SrcTy, Value *Ptr,
                              ArrayRef<Value *> Indices, GEPNoWrapFlags NW,
                              const SimplifyQuery &Q, unsigned) {
  // The type of the GEP pointer operand.
  unsigned AS =
      cast<PointerType>(Ptr->getType()->getScalarType())->getAddressSpace();

  // getelementptr P -> P.
  if (Indices.empty())
    return Ptr;

  // Compute the (pointer) type returned by the GEP instruction.
  Type *LastType = GetElementPtrInst::getIndexedType(SrcTy, Indices);
  Type *GEPTy = Ptr->getType();
  if (!GEPTy->isVectorTy()) {
    for (Value *Op : Indices) {
      // If one of the operands is a vector, the result type is a vector of
      // pointers. All vector operands must have the same number of elements.
      if (VectorType *VT = dyn_cast<VectorType>(Op->getType())) {
        GEPTy = VectorType::get(GEPTy, VT->getElementCount());
        break;
      }
    }
  }

  // All-zero GEP is a no-op, unless it performs a vector splat.
  if (Ptr->getType() == GEPTy &&
      all_of(Indices, [](const auto *V) { return match(V, m_Zero()); }))
    return Ptr;

  // getelementptr poison, idx -> poison
  // getelementptr baseptr, poison -> poison
  if (isa<PoisonValue>(Ptr) ||
      any_of(Indices, [](const auto *V) { return isa<PoisonValue>(V); }))
    return PoisonValue::get(GEPTy);

  // getelementptr undef, idx -> undef
  if (Q.isUndefValue(Ptr))
    return UndefValue::get(GEPTy);

  bool IsScalableVec =
      SrcTy->isScalableTy() || any_of(Indices, [](const Value *V) {
        return isa<ScalableVectorType>(V->getType());
      });

  if (Indices.size() == 1) {
    Type *Ty = SrcTy;
    if (!IsScalableVec && Ty->isSized()) {
      Value *P;
      uint64_t C;
      uint64_t TyAllocSize = Q.DL.getTypeAllocSize(Ty);
      // getelementptr P, N -> P if P points to a type of zero size.
      if (TyAllocSize == 0 && Ptr->getType() == GEPTy)
        return Ptr;

      // The following transforms are only safe if the ptrtoint cast
      // doesn't truncate the pointers.
      if (Indices[0]->getType()->getScalarSizeInBits() ==
          Q.DL.getPointerSizeInBits(AS)) {
        auto CanSimplify = [GEPTy, &P, Ptr]() -> bool {
          return P->getType() == GEPTy &&
                 getUnderlyingObject(P) == getUnderlyingObject(Ptr);
        };
        // getelementptr V, (sub P, V) -> P if P points to a type of size 1.
        if (TyAllocSize == 1 &&
            match(Indices[0],
                  m_Sub(m_PtrToInt(m_Value(P)), m_PtrToInt(m_Specific(Ptr)))) &&
            CanSimplify())
          return P;

        // getelementptr V, (ashr (sub P, V), C) -> P if P points to a type of
        // size 1 << C.
        if (match(Indices[0], m_AShr(m_Sub(m_PtrToInt(m_Value(P)),
                                           m_PtrToInt(m_Specific(Ptr))),
                                     m_ConstantInt(C))) &&
            TyAllocSize == 1ULL << C && CanSimplify())
          return P;

        // getelementptr V, (sdiv (sub P, V), C) -> P if P points to a type of
        // size C.
        if (match(Indices[0], m_SDiv(m_Sub(m_PtrToInt(m_Value(P)),
                                           m_PtrToInt(m_Specific(Ptr))),
                                     m_SpecificInt(TyAllocSize))) &&
            CanSimplify())
          return P;
      }
    }
  }

  if (!IsScalableVec && Q.DL.getTypeAllocSize(LastType) == 1 &&
      all_of(Indices.drop_back(1),
             [](Value *Idx) { return match(Idx, m_Zero()); })) {
    unsigned IdxWidth =
        Q.DL.getIndexSizeInBits(Ptr->getType()->getPointerAddressSpace());
    if (Q.DL.getTypeSizeInBits(Indices.back()->getType()) == IdxWidth) {
      APInt BasePtrOffset(IdxWidth, 0);
      Value *StrippedBasePtr =
          Ptr->stripAndAccumulateInBoundsConstantOffsets(Q.DL, BasePtrOffset);

      // Avoid creating inttoptr of zero here: While LLVMs treatment of
      // inttoptr is generally conservative, this particular case is folded to
      // a null pointer, which will have incorrect provenance.

      // gep (gep V, C), (sub 0, V) -> C
      if (match(Indices.back(),
                m_Neg(m_PtrToInt(m_Specific(StrippedBasePtr)))) &&
          !BasePtrOffset.isZero()) {
        auto *CI = ConstantInt::get(GEPTy->getContext(), BasePtrOffset);
        return ConstantExpr::getIntToPtr(CI, GEPTy);
      }
      // gep (gep V, C), (xor V, -1) -> C-1
      if (match(Indices.back(),
                m_Xor(m_PtrToInt(m_Specific(StrippedBasePtr)), m_AllOnes())) &&
          !BasePtrOffset.isOne()) {
        auto *CI = ConstantInt::get(GEPTy->getContext(), BasePtrOffset - 1);
        return ConstantExpr::getIntToPtr(CI, GEPTy);
      }
    }
  }

  // Check to see if this is constant foldable.
  if (!isa<Constant>(Ptr) ||
      !all_of(Indices, [](Value *V) { return isa<Constant>(V); }))
    return nullptr;

  if (!ConstantExpr::isSupportedGetElementPtr(SrcTy))
    return ConstantFoldGetElementPtr(SrcTy, cast<Constant>(Ptr), std::nullopt,
                                     Indices);

  auto *CE =
      ConstantExpr::getGetElementPtr(SrcTy, cast<Constant>(Ptr), Indices, NW);
  return ConstantFoldConstant(CE, Q.DL);
}

Value *llvm::simplifyGEPInst(Type *SrcTy, Value *Ptr, ArrayRef<Value *> Indices,
                             GEPNoWrapFlags NW, const SimplifyQuery &Q) {
  return ::simplifyGEPInst(SrcTy, Ptr, Indices, NW, Q, RecursionLimit);
}

/// Given operands for an InsertValueInst, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyInsertValueInst(Value *Agg, Value *Val,
                                      ArrayRef<unsigned> Idxs,
                                      const SimplifyQuery &Q, unsigned) {
  if (Constant *CAgg = dyn_cast<Constant>(Agg))
    if (Constant *CVal = dyn_cast<Constant>(Val))
      return ConstantFoldInsertValueInstruction(CAgg, CVal, Idxs);

  // insertvalue x, poison, n -> x
  // insertvalue x, undef, n -> x if x cannot be poison
  if (isa<PoisonValue>(Val) ||
      (Q.isUndefValue(Val) && isGuaranteedNotToBePoison(Agg)))
    return Agg;

  // insertvalue x, (extractvalue y, n), n
  if (ExtractValueInst *EV = dyn_cast<ExtractValueInst>(Val))
    if (EV->getAggregateOperand()->getType() == Agg->getType() &&
        EV->getIndices() == Idxs) {
      // insertvalue poison, (extractvalue y, n), n -> y
      // insertvalue undef, (extractvalue y, n), n -> y if y cannot be poison
      if (isa<PoisonValue>(Agg) ||
          (Q.isUndefValue(Agg) &&
           isGuaranteedNotToBePoison(EV->getAggregateOperand())))
        return EV->getAggregateOperand();

      // insertvalue y, (extractvalue y, n), n -> y
      if (Agg == EV->getAggregateOperand())
        return Agg;
    }

  return nullptr;
}

Value *llvm::simplifyInsertValueInst(Value *Agg, Value *Val,
                                     ArrayRef<unsigned> Idxs,
                                     const SimplifyQuery &Q) {
  return ::simplifyInsertValueInst(Agg, Val, Idxs, Q, RecursionLimit);
}

Value *llvm::simplifyInsertElementInst(Value *Vec, Value *Val, Value *Idx,
                                       const SimplifyQuery &Q) {
  // Try to constant fold.
  auto *VecC = dyn_cast<Constant>(Vec);
  auto *ValC = dyn_cast<Constant>(Val);
  auto *IdxC = dyn_cast<Constant>(Idx);
  if (VecC && ValC && IdxC)
    return ConstantExpr::getInsertElement(VecC, ValC, IdxC);

  // For fixed-length vector, fold into poison if index is out of bounds.
  if (auto *CI = dyn_cast<ConstantInt>(Idx)) {
    if (isa<FixedVectorType>(Vec->getType()) &&
        CI->uge(cast<FixedVectorType>(Vec->getType())->getNumElements()))
      return PoisonValue::get(Vec->getType());
  }

  // If index is undef, it might be out of bounds (see above case)
  if (Q.isUndefValue(Idx))
    return PoisonValue::get(Vec->getType());

  // If the scalar is poison, or it is undef and there is no risk of
  // propagating poison from the vector value, simplify to the vector value.
  if (isa<PoisonValue>(Val) ||
      (Q.isUndefValue(Val) && isGuaranteedNotToBePoison(Vec)))
    return Vec;

  // If we are extracting a value from a vector, then inserting it into the same
  // place, that's the input vector:
  // insertelt Vec, (extractelt Vec, Idx), Idx --> Vec
  if (match(Val, m_ExtractElt(m_Specific(Vec), m_Specific(Idx))))
    return Vec;

  return nullptr;
}

/// Given operands for an ExtractValueInst, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyExtractValueInst(Value *Agg, ArrayRef<unsigned> Idxs,
                                       const SimplifyQuery &, unsigned) {
  if (auto *CAgg = dyn_cast<Constant>(Agg))
    return ConstantFoldExtractValueInstruction(CAgg, Idxs);

  // extractvalue x, (insertvalue y, elt, n), n -> elt
  unsigned NumIdxs = Idxs.size();
  for (auto *IVI = dyn_cast<InsertValueInst>(Agg); IVI != nullptr;
       IVI = dyn_cast<InsertValueInst>(IVI->getAggregateOperand())) {
    ArrayRef<unsigned> InsertValueIdxs = IVI->getIndices();
    unsigned NumInsertValueIdxs = InsertValueIdxs.size();
    unsigned NumCommonIdxs = std::min(NumInsertValueIdxs, NumIdxs);
    if (InsertValueIdxs.slice(0, NumCommonIdxs) ==
        Idxs.slice(0, NumCommonIdxs)) {
      if (NumIdxs == NumInsertValueIdxs)
        return IVI->getInsertedValueOperand();
      break;
    }
  }

  return nullptr;
}

Value *llvm::simplifyExtractValueInst(Value *Agg, ArrayRef<unsigned> Idxs,
                                      const SimplifyQuery &Q) {
  return ::simplifyExtractValueInst(Agg, Idxs, Q, RecursionLimit);
}

/// Given operands for an ExtractElementInst, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyExtractElementInst(Value *Vec, Value *Idx,
                                         const SimplifyQuery &Q, unsigned) {
  auto *VecVTy = cast<VectorType>(Vec->getType());
  if (auto *CVec = dyn_cast<Constant>(Vec)) {
    if (auto *CIdx = dyn_cast<Constant>(Idx))
      return ConstantExpr::getExtractElement(CVec, CIdx);

    if (Q.isUndefValue(Vec))
      return UndefValue::get(VecVTy->getElementType());
  }

  // An undef extract index can be arbitrarily chosen to be an out-of-range
  // index value, which would result in the instruction being poison.
  if (Q.isUndefValue(Idx))
    return PoisonValue::get(VecVTy->getElementType());

  // If extracting a specified index from the vector, see if we can recursively
  // find a previously computed scalar that was inserted into the vector.
  if (auto *IdxC = dyn_cast<ConstantInt>(Idx)) {
    // For fixed-length vector, fold into undef if index is out of bounds.
    unsigned MinNumElts = VecVTy->getElementCount().getKnownMinValue();
    if (isa<FixedVectorType>(VecVTy) && IdxC->getValue().uge(MinNumElts))
      return PoisonValue::get(VecVTy->getElementType());
    // Handle case where an element is extracted from a splat.
    if (IdxC->getValue().ult(MinNumElts))
      if (auto *Splat = getSplatValue(Vec))
        return Splat;
    if (Value *Elt = findScalarElement(Vec, IdxC->getZExtValue()))
      return Elt;
  } else {
    // extractelt x, (insertelt y, elt, n), n -> elt
    // If the possibly-variable indices are trivially known to be equal
    // (because they are the same operand) then use the value that was
    // inserted directly.
    auto *IE = dyn_cast<InsertElementInst>(Vec);
    if (IE && IE->getOperand(2) == Idx)
      return IE->getOperand(1);

    // The index is not relevant if our vector is a splat.
    if (Value *Splat = getSplatValue(Vec))
      return Splat;
  }
  return nullptr;
}

Value *llvm::simplifyExtractElementInst(Value *Vec, Value *Idx,
                                        const SimplifyQuery &Q) {
  return ::simplifyExtractElementInst(Vec, Idx, Q, RecursionLimit);
}

/// See if we can fold the given phi. If not, returns null.
static Value *simplifyPHINode(PHINode *PN, ArrayRef<Value *> IncomingValues,
                              const SimplifyQuery &Q) {
  // WARNING: no matter how worthwhile it may seem, we can not perform PHI CSE
  //          here, because the PHI we may succeed simplifying to was not
  //          def-reachable from the original PHI!

  // If all of the PHI's incoming values are the same then replace the PHI node
  // with the common value.
  Value *CommonValue = nullptr;
  bool HasPoisonInput = false;
  bool HasUndefInput = false;
  for (Value *Incoming : IncomingValues) {
    // If the incoming value is the phi node itself, it can safely be skipped.
    if (Incoming == PN)
      continue;
    if (isa<PoisonValue>(Incoming)) {
      HasPoisonInput = true;
      continue;
    }
    if (Q.isUndefValue(Incoming)) {
      // Remember that we saw an undef value, but otherwise ignore them.
      HasUndefInput = true;
      continue;
    }
    if (CommonValue && Incoming != CommonValue)
      return nullptr; // Not the same, bail out.
    CommonValue = Incoming;
  }

  // If CommonValue is null then all of the incoming values were either undef,
  // poison or equal to the phi node itself.
  if (!CommonValue)
    return HasUndefInput ? UndefValue::get(PN->getType())
                         : PoisonValue::get(PN->getType());

  if (HasPoisonInput || HasUndefInput) {
    // If we have a PHI node like phi(X, undef, X), where X is defined by some
    // instruction, we cannot return X as the result of the PHI node unless it
    // dominates the PHI block.
    return valueDominatesPHI(CommonValue, PN, Q.DT) ? CommonValue : nullptr;
  }

  return CommonValue;
}

static Value *simplifyCastInst(unsigned CastOpc, Value *Op, Type *Ty,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (auto *C = dyn_cast<Constant>(Op))
    return ConstantFoldCastOperand(CastOpc, C, Ty, Q.DL);

  if (auto *CI = dyn_cast<CastInst>(Op)) {
    auto *Src = CI->getOperand(0);
    Type *SrcTy = Src->getType();
    Type *MidTy = CI->getType();
    Type *DstTy = Ty;
    if (Src->getType() == Ty) {
      auto FirstOp = static_cast<Instruction::CastOps>(CI->getOpcode());
      auto SecondOp = static_cast<Instruction::CastOps>(CastOpc);
      Type *SrcIntPtrTy =
          SrcTy->isPtrOrPtrVectorTy() ? Q.DL.getIntPtrType(SrcTy) : nullptr;
      Type *MidIntPtrTy =
          MidTy->isPtrOrPtrVectorTy() ? Q.DL.getIntPtrType(MidTy) : nullptr;
      Type *DstIntPtrTy =
          DstTy->isPtrOrPtrVectorTy() ? Q.DL.getIntPtrType(DstTy) : nullptr;
      if (CastInst::isEliminableCastPair(FirstOp, SecondOp, SrcTy, MidTy, DstTy,
                                         SrcIntPtrTy, MidIntPtrTy,
                                         DstIntPtrTy) == Instruction::BitCast)
        return Src;
    }
  }

  // bitcast x -> x
  if (CastOpc == Instruction::BitCast)
    if (Op->getType() == Ty)
      return Op;

  // ptrtoint (ptradd (Ptr, X - ptrtoint(Ptr))) -> X
  Value *Ptr, *X;
  if (CastOpc == Instruction::PtrToInt &&
      match(Op, m_PtrAdd(m_Value(Ptr),
                         m_Sub(m_Value(X), m_PtrToInt(m_Deferred(Ptr))))) &&
      X->getType() == Ty && Ty == Q.DL.getIndexType(Ptr->getType()))
    return X;

  return nullptr;
}

Value *llvm::simplifyCastInst(unsigned CastOpc, Value *Op, Type *Ty,
                              const SimplifyQuery &Q) {
  return ::simplifyCastInst(CastOpc, Op, Ty, Q, RecursionLimit);
}

/// For the given destination element of a shuffle, peek through shuffles to
/// match a root vector source operand that contains that element in the same
/// vector lane (ie, the same mask index), so we can eliminate the shuffle(s).
static Value *foldIdentityShuffles(int DestElt, Value *Op0, Value *Op1,
                                   int MaskVal, Value *RootVec,
                                   unsigned MaxRecurse) {
  if (!MaxRecurse--)
    return nullptr;

  // Bail out if any mask value is undefined. That kind of shuffle may be
  // simplified further based on demanded bits or other folds.
  if (MaskVal == -1)
    return nullptr;

  // The mask value chooses which source operand we need to look at next.
  int InVecNumElts = cast<FixedVectorType>(Op0->getType())->getNumElements();
  int RootElt = MaskVal;
  Value *SourceOp = Op0;
  if (MaskVal >= InVecNumElts) {
    RootElt = MaskVal - InVecNumElts;
    SourceOp = Op1;
  }

  // If the source operand is a shuffle itself, look through it to find the
  // matching root vector.
  if (auto *SourceShuf = dyn_cast<ShuffleVectorInst>(SourceOp)) {
    return foldIdentityShuffles(
        DestElt, SourceShuf->getOperand(0), SourceShuf->getOperand(1),
        SourceShuf->getMaskValue(RootElt), RootVec, MaxRecurse);
  }

  // The source operand is not a shuffle. Initialize the root vector value for
  // this shuffle if that has not been done yet.
  if (!RootVec)
    RootVec = SourceOp;

  // Give up as soon as a source operand does not match the existing root value.
  if (RootVec != SourceOp)
    return nullptr;

  // The element must be coming from the same lane in the source vector
  // (although it may have crossed lanes in intermediate shuffles).
  if (RootElt != DestElt)
    return nullptr;

  return RootVec;
}

static Value *simplifyShuffleVectorInst(Value *Op0, Value *Op1,
                                        ArrayRef<int> Mask, Type *RetTy,
                                        const SimplifyQuery &Q,
                                        unsigned MaxRecurse) {
  if (all_of(Mask, [](int Elem) { return Elem == PoisonMaskElem; }))
    return PoisonValue::get(RetTy);

  auto *InVecTy = cast<VectorType>(Op0->getType());
  unsigned MaskNumElts = Mask.size();
  ElementCount InVecEltCount = InVecTy->getElementCount();

  bool Scalable = InVecEltCount.isScalable();

  SmallVector<int, 32> Indices;
  Indices.assign(Mask.begin(), Mask.end());

  // Canonicalization: If mask does not select elements from an input vector,
  // replace that input vector with poison.
  if (!Scalable) {
    bool MaskSelects0 = false, MaskSelects1 = false;
    unsigned InVecNumElts = InVecEltCount.getKnownMinValue();
    for (unsigned i = 0; i != MaskNumElts; ++i) {
      if (Indices[i] == -1)
        continue;
      if ((unsigned)Indices[i] < InVecNumElts)
        MaskSelects0 = true;
      else
        MaskSelects1 = true;
    }
    if (!MaskSelects0)
      Op0 = PoisonValue::get(InVecTy);
    if (!MaskSelects1)
      Op1 = PoisonValue::get(InVecTy);
  }

  auto *Op0Const = dyn_cast<Constant>(Op0);
  auto *Op1Const = dyn_cast<Constant>(Op1);

  // If all operands are constant, constant fold the shuffle. This
  // transformation depends on the value of the mask which is not known at
  // compile time for scalable vectors
  if (Op0Const && Op1Const)
    return ConstantExpr::getShuffleVector(Op0Const, Op1Const, Mask);

  // Canonicalization: if only one input vector is constant, it shall be the
  // second one. This transformation depends on the value of the mask which
  // is not known at compile time for scalable vectors
  if (!Scalable && Op0Const && !Op1Const) {
    std::swap(Op0, Op1);
    ShuffleVectorInst::commuteShuffleMask(Indices,
                                          InVecEltCount.getKnownMinValue());
  }

  // A splat of an inserted scalar constant becomes a vector constant:
  // shuf (inselt ?, C, IndexC), undef, <IndexC, IndexC...> --> <C, C...>
  // NOTE: We may have commuted above, so analyze the updated Indices, not the
  //       original mask constant.
  // NOTE: This transformation depends on the value of the mask which is not
  // known at compile time for scalable vectors
  Constant *C;
  ConstantInt *IndexC;
  if (!Scalable && match(Op0, m_InsertElt(m_Value(), m_Constant(C),
                                          m_ConstantInt(IndexC)))) {
    // Match a splat shuffle mask of the insert index allowing undef elements.
    int InsertIndex = IndexC->getZExtValue();
    if (all_of(Indices, [InsertIndex](int MaskElt) {
          return MaskElt == InsertIndex || MaskElt == -1;
        })) {
      assert(isa<UndefValue>(Op1) && "Expected undef operand 1 for splat");

      // Shuffle mask poisons become poison constant result elements.
      SmallVector<Constant *, 16> VecC(MaskNumElts, C);
      for (unsigned i = 0; i != MaskNumElts; ++i)
        if (Indices[i] == -1)
          VecC[i] = PoisonValue::get(C->getType());
      return ConstantVector::get(VecC);
    }
  }

  // A shuffle of a splat is always the splat itself. Legal if the shuffle's
  // value type is same as the input vectors' type.
  if (auto *OpShuf = dyn_cast<ShuffleVectorInst>(Op0))
    if (Q.isUndefValue(Op1) && RetTy == InVecTy &&
        all_equal(OpShuf->getShuffleMask()))
      return Op0;

  // All remaining transformation depend on the value of the mask, which is
  // not known at compile time for scalable vectors.
  if (Scalable)
    return nullptr;

  // Don't fold a shuffle with undef mask elements. This may get folded in a
  // better way using demanded bits or other analysis.
  // TODO: Should we allow this?
  if (is_contained(Indices, -1))
    return nullptr;

  // Check if every element of this shuffle can be mapped back to the
  // corresponding element of a single root vector. If so, we don't need this
  // shuffle. This handles simple identity shuffles as well as chains of
  // shuffles that may widen/narrow and/or move elements across lanes and back.
  Value *RootVec = nullptr;
  for (unsigned i = 0; i != MaskNumElts; ++i) {
    // Note that recursion is limited for each vector element, so if any element
    // exceeds the limit, this will fail to simplify.
    RootVec =
        foldIdentityShuffles(i, Op0, Op1, Indices[i], RootVec, MaxRecurse);

    // We can't replace a widening/narrowing shuffle with one of its operands.
    if (!RootVec || RootVec->getType() != RetTy)
      return nullptr;
  }
  return RootVec;
}

/// Given operands for a ShuffleVectorInst, fold the result or return null.
Value *llvm::simplifyShuffleVectorInst(Value *Op0, Value *Op1,
                                       ArrayRef<int> Mask, Type *RetTy,
                                       const SimplifyQuery &Q) {
  return ::simplifyShuffleVectorInst(Op0, Op1, Mask, RetTy, Q, RecursionLimit);
}

static Constant *foldConstant(Instruction::UnaryOps Opcode, Value *&Op,
                              const SimplifyQuery &Q) {
  if (auto *C = dyn_cast<Constant>(Op))
    return ConstantFoldUnaryOpOperand(Opcode, C, Q.DL);
  return nullptr;
}

/// Given the operand for an FNeg, see if we can fold the result.  If not, this
/// returns null.
static Value *simplifyFNegInst(Value *Op, FastMathFlags FMF,
                               const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (Constant *C = foldConstant(Instruction::FNeg, Op, Q))
    return C;

  Value *X;
  // fneg (fneg X) ==> X
  if (match(Op, m_FNeg(m_Value(X))))
    return X;

  return nullptr;
}

Value *llvm::simplifyFNegInst(Value *Op, FastMathFlags FMF,
                              const SimplifyQuery &Q) {
  return ::simplifyFNegInst(Op, FMF, Q, RecursionLimit);
}

/// Try to propagate existing NaN values when possible. If not, replace the
/// constant or elements in the constant with a canonical NaN.
static Constant *propagateNaN(Constant *In) {
  Type *Ty = In->getType();
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    unsigned NumElts = VecTy->getNumElements();
    SmallVector<Constant *, 32> NewC(NumElts);
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *EltC = In->getAggregateElement(i);
      // Poison elements propagate. NaN propagates except signaling is quieted.
      // Replace unknown or undef elements with canonical NaN.
      if (EltC && isa<PoisonValue>(EltC))
        NewC[i] = EltC;
      else if (EltC && EltC->isNaN())
        NewC[i] = ConstantFP::get(
            EltC->getType(), cast<ConstantFP>(EltC)->getValue().makeQuiet());
      else
        NewC[i] = ConstantFP::getNaN(VecTy->getElementType());
    }
    return ConstantVector::get(NewC);
  }

  // If it is not a fixed vector, but not a simple NaN either, return a
  // canonical NaN.
  if (!In->isNaN())
    return ConstantFP::getNaN(Ty);

  // If we known this is a NaN, and it's scalable vector, we must have a splat
  // on our hands. Grab that before splatting a QNaN constant.
  if (isa<ScalableVectorType>(Ty)) {
    auto *Splat = In->getSplatValue();
    assert(Splat && Splat->isNaN() &&
           "Found a scalable-vector NaN but not a splat");
    In = Splat;
  }

  // Propagate an existing QNaN constant. If it is an SNaN, make it quiet, but
  // preserve the sign/payload.
  return ConstantFP::get(Ty, cast<ConstantFP>(In)->getValue().makeQuiet());
}

/// Perform folds that are common to any floating-point operation. This implies
/// transforms based on poison/undef/NaN because the operation itself makes no
/// difference to the result.
static Constant *simplifyFPOp(ArrayRef<Value *> Ops, FastMathFlags FMF,
                              const SimplifyQuery &Q,
                              fp::ExceptionBehavior ExBehavior,
                              RoundingMode Rounding) {
  // Poison is independent of anything else. It always propagates from an
  // operand to a math result.
  if (any_of(Ops, [](Value *V) { return match(V, m_Poison()); }))
    return PoisonValue::get(Ops[0]->getType());

  for (Value *V : Ops) {
    bool IsNan = match(V, m_NaN());
    bool IsInf = match(V, m_Inf());
    bool IsUndef = Q.isUndefValue(V);

    // If this operation has 'nnan' or 'ninf' and at least 1 disallowed operand
    // (an undef operand can be chosen to be Nan/Inf), then the result of
    // this operation is poison.
    if (FMF.noNaNs() && (IsNan || IsUndef))
      return PoisonValue::get(V->getType());
    if (FMF.noInfs() && (IsInf || IsUndef))
      return PoisonValue::get(V->getType());

    if (isDefaultFPEnvironment(ExBehavior, Rounding)) {
      // Undef does not propagate because undef means that all bits can take on
      // any value. If this is undef * NaN for example, then the result values
      // (at least the exponent bits) are limited. Assume the undef is a
      // canonical NaN and propagate that.
      if (IsUndef)
        return ConstantFP::getNaN(V->getType());
      if (IsNan)
        return propagateNaN(cast<Constant>(V));
    } else if (ExBehavior != fp::ebStrict) {
      if (IsNan)
        return propagateNaN(cast<Constant>(V));
    }
  }
  return nullptr;
}

/// Given operands for an FAdd, see if we can fold the result.  If not, this
/// returns null.
static Value *
simplifyFAddInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                 const SimplifyQuery &Q, unsigned MaxRecurse,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven) {
  if (isDefaultFPEnvironment(ExBehavior, Rounding))
    if (Constant *C = foldOrCommuteConstant(Instruction::FAdd, Op0, Op1, Q))
      return C;

  if (Constant *C = simplifyFPOp({Op0, Op1}, FMF, Q, ExBehavior, Rounding))
    return C;

  // fadd X, -0 ==> X
  // With strict/constrained FP, we have these possible edge cases that do
  // not simplify to Op0:
  // fadd SNaN, -0.0 --> QNaN
  // fadd +0.0, -0.0 --> -0.0 (but only with round toward negative)
  if (canIgnoreSNaN(ExBehavior, FMF) &&
      (!canRoundingModeBe(Rounding, RoundingMode::TowardNegative) ||
       FMF.noSignedZeros()))
    if (match(Op1, m_NegZeroFP()))
      return Op0;

  // fadd X, 0 ==> X, when we know X is not -0
  if (canIgnoreSNaN(ExBehavior, FMF))
    if (match(Op1, m_PosZeroFP()) &&
        (FMF.noSignedZeros() || cannotBeNegativeZero(Op0, /*Depth=*/0, Q)))
      return Op0;

  if (!isDefaultFPEnvironment(ExBehavior, Rounding))
    return nullptr;

  if (FMF.noNaNs()) {
    // With nnan: X + {+/-}Inf --> {+/-}Inf
    if (match(Op1, m_Inf()))
      return Op1;

    // With nnan: -X + X --> 0.0 (and commuted variant)
    // We don't have to explicitly exclude infinities (ninf): INF + -INF == NaN.
    // Negative zeros are allowed because we always end up with positive zero:
    // X = -0.0: (-0.0 - (-0.0)) + (-0.0) == ( 0.0) + (-0.0) == 0.0
    // X = -0.0: ( 0.0 - (-0.0)) + (-0.0) == ( 0.0) + (-0.0) == 0.0
    // X =  0.0: (-0.0 - ( 0.0)) + ( 0.0) == (-0.0) + ( 0.0) == 0.0
    // X =  0.0: ( 0.0 - ( 0.0)) + ( 0.0) == ( 0.0) + ( 0.0) == 0.0
    if (match(Op0, m_FSub(m_AnyZeroFP(), m_Specific(Op1))) ||
        match(Op1, m_FSub(m_AnyZeroFP(), m_Specific(Op0))))
      return ConstantFP::getZero(Op0->getType());

    if (match(Op0, m_FNeg(m_Specific(Op1))) ||
        match(Op1, m_FNeg(m_Specific(Op0))))
      return ConstantFP::getZero(Op0->getType());
  }

  // (X - Y) + Y --> X
  // Y + (X - Y) --> X
  Value *X;
  if (FMF.noSignedZeros() && FMF.allowReassoc() &&
      (match(Op0, m_FSub(m_Value(X), m_Specific(Op1))) ||
       match(Op1, m_FSub(m_Value(X), m_Specific(Op0)))))
    return X;

  return nullptr;
}

/// Given operands for an FSub, see if we can fold the result.  If not, this
/// returns null.
static Value *
simplifyFSubInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                 const SimplifyQuery &Q, unsigned MaxRecurse,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven) {
  if (isDefaultFPEnvironment(ExBehavior, Rounding))
    if (Constant *C = foldOrCommuteConstant(Instruction::FSub, Op0, Op1, Q))
      return C;

  if (Constant *C = simplifyFPOp({Op0, Op1}, FMF, Q, ExBehavior, Rounding))
    return C;

  // fsub X, +0 ==> X
  if (canIgnoreSNaN(ExBehavior, FMF) &&
      (!canRoundingModeBe(Rounding, RoundingMode::TowardNegative) ||
       FMF.noSignedZeros()))
    if (match(Op1, m_PosZeroFP()))
      return Op0;

  // fsub X, -0 ==> X, when we know X is not -0
  if (canIgnoreSNaN(ExBehavior, FMF))
    if (match(Op1, m_NegZeroFP()) &&
        (FMF.noSignedZeros() || cannotBeNegativeZero(Op0, /*Depth=*/0, Q)))
      return Op0;

  // fsub -0.0, (fsub -0.0, X) ==> X
  // fsub -0.0, (fneg X) ==> X
  Value *X;
  if (canIgnoreSNaN(ExBehavior, FMF))
    if (match(Op0, m_NegZeroFP()) && match(Op1, m_FNeg(m_Value(X))))
      return X;

  // fsub 0.0, (fsub 0.0, X) ==> X if signed zeros are ignored.
  // fsub 0.0, (fneg X) ==> X if signed zeros are ignored.
  if (canIgnoreSNaN(ExBehavior, FMF))
    if (FMF.noSignedZeros() && match(Op0, m_AnyZeroFP()) &&
        (match(Op1, m_FSub(m_AnyZeroFP(), m_Value(X))) ||
         match(Op1, m_FNeg(m_Value(X)))))
      return X;

  if (!isDefaultFPEnvironment(ExBehavior, Rounding))
    return nullptr;

  if (FMF.noNaNs()) {
    // fsub nnan x, x ==> 0.0
    if (Op0 == Op1)
      return Constant::getNullValue(Op0->getType());

    // With nnan: {+/-}Inf - X --> {+/-}Inf
    if (match(Op0, m_Inf()))
      return Op0;

    // With nnan: X - {+/-}Inf --> {-/+}Inf
    if (match(Op1, m_Inf()))
      return foldConstant(Instruction::FNeg, Op1, Q);
  }

  // Y - (Y - X) --> X
  // (X + Y) - Y --> X
  if (FMF.noSignedZeros() && FMF.allowReassoc() &&
      (match(Op1, m_FSub(m_Specific(Op0), m_Value(X))) ||
       match(Op0, m_c_FAdd(m_Specific(Op1), m_Value(X)))))
    return X;

  return nullptr;
}

static Value *simplifyFMAFMul(Value *Op0, Value *Op1, FastMathFlags FMF,
                              const SimplifyQuery &Q, unsigned MaxRecurse,
                              fp::ExceptionBehavior ExBehavior,
                              RoundingMode Rounding) {
  if (Constant *C = simplifyFPOp({Op0, Op1}, FMF, Q, ExBehavior, Rounding))
    return C;

  if (!isDefaultFPEnvironment(ExBehavior, Rounding))
    return nullptr;

  // Canonicalize special constants as operand 1.
  if (match(Op0, m_FPOne()) || match(Op0, m_AnyZeroFP()))
    std::swap(Op0, Op1);

  // X * 1.0 --> X
  if (match(Op1, m_FPOne()))
    return Op0;

  if (match(Op1, m_AnyZeroFP())) {
    // X * 0.0 --> 0.0 (with nnan and nsz)
    if (FMF.noNaNs() && FMF.noSignedZeros())
      return ConstantFP::getZero(Op0->getType());

    KnownFPClass Known =
        computeKnownFPClass(Op0, FMF, fcInf | fcNan, /*Depth=*/0, Q);
    if (Known.isKnownNever(fcInf | fcNan)) {
      // +normal number * (-)0.0 --> (-)0.0
      if (Known.SignBit == false)
        return Op1;
      // -normal number * (-)0.0 --> -(-)0.0
      if (Known.SignBit == true)
        return foldConstant(Instruction::FNeg, Op1, Q);
    }
  }

  // sqrt(X) * sqrt(X) --> X, if we can:
  // 1. Remove the intermediate rounding (reassociate).
  // 2. Ignore non-zero negative numbers because sqrt would produce NAN.
  // 3. Ignore -0.0 because sqrt(-0.0) == -0.0, but -0.0 * -0.0 == 0.0.
  Value *X;
  if (Op0 == Op1 && match(Op0, m_Sqrt(m_Value(X))) && FMF.allowReassoc() &&
      FMF.noNaNs() && FMF.noSignedZeros())
    return X;

  return nullptr;
}

/// Given the operands for an FMul, see if we can fold the result
static Value *
simplifyFMulInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                 const SimplifyQuery &Q, unsigned MaxRecurse,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven) {
  if (isDefaultFPEnvironment(ExBehavior, Rounding))
    if (Constant *C = foldOrCommuteConstant(Instruction::FMul, Op0, Op1, Q))
      return C;

  // Now apply simplifications that do not require rounding.
  return simplifyFMAFMul(Op0, Op1, FMF, Q, MaxRecurse, ExBehavior, Rounding);
}

Value *llvm::simplifyFAddInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                              const SimplifyQuery &Q,
                              fp::ExceptionBehavior ExBehavior,
                              RoundingMode Rounding) {
  return ::simplifyFAddInst(Op0, Op1, FMF, Q, RecursionLimit, ExBehavior,
                            Rounding);
}

Value *llvm::simplifyFSubInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                              const SimplifyQuery &Q,
                              fp::ExceptionBehavior ExBehavior,
                              RoundingMode Rounding) {
  return ::simplifyFSubInst(Op0, Op1, FMF, Q, RecursionLimit, ExBehavior,
                            Rounding);
}

Value *llvm::simplifyFMulInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                              const SimplifyQuery &Q,
                              fp::ExceptionBehavior ExBehavior,
                              RoundingMode Rounding) {
  return ::simplifyFMulInst(Op0, Op1, FMF, Q, RecursionLimit, ExBehavior,
                            Rounding);
}

Value *llvm::simplifyFMAFMul(Value *Op0, Value *Op1, FastMathFlags FMF,
                             const SimplifyQuery &Q,
                             fp::ExceptionBehavior ExBehavior,
                             RoundingMode Rounding) {
  return ::simplifyFMAFMul(Op0, Op1, FMF, Q, RecursionLimit, ExBehavior,
                           Rounding);
}

static Value *
simplifyFDivInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                 const SimplifyQuery &Q, unsigned,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven) {
  if (isDefaultFPEnvironment(ExBehavior, Rounding))
    if (Constant *C = foldOrCommuteConstant(Instruction::FDiv, Op0, Op1, Q))
      return C;

  if (Constant *C = simplifyFPOp({Op0, Op1}, FMF, Q, ExBehavior, Rounding))
    return C;

  if (!isDefaultFPEnvironment(ExBehavior, Rounding))
    return nullptr;

  // X / 1.0 -> X
  if (match(Op1, m_FPOne()))
    return Op0;

  // 0 / X -> 0
  // Requires that NaNs are off (X could be zero) and signed zeroes are
  // ignored (X could be positive or negative, so the output sign is unknown).
  if (FMF.noNaNs() && FMF.noSignedZeros() && match(Op0, m_AnyZeroFP()))
    return ConstantFP::getZero(Op0->getType());

  if (FMF.noNaNs()) {
    // X / X -> 1.0 is legal when NaNs are ignored.
    // We can ignore infinities because INF/INF is NaN.
    if (Op0 == Op1)
      return ConstantFP::get(Op0->getType(), 1.0);

    // (X * Y) / Y --> X if we can reassociate to the above form.
    Value *X;
    if (FMF.allowReassoc() && match(Op0, m_c_FMul(m_Value(X), m_Specific(Op1))))
      return X;

    // -X /  X -> -1.0 and
    //  X / -X -> -1.0 are legal when NaNs are ignored.
    // We can ignore signed zeros because +-0.0/+-0.0 is NaN and ignored.
    if (match(Op0, m_FNegNSZ(m_Specific(Op1))) ||
        match(Op1, m_FNegNSZ(m_Specific(Op0))))
      return ConstantFP::get(Op0->getType(), -1.0);

    // nnan ninf X / [-]0.0 -> poison
    if (FMF.noInfs() && match(Op1, m_AnyZeroFP()))
      return PoisonValue::get(Op1->getType());
  }

  return nullptr;
}

Value *llvm::simplifyFDivInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                              const SimplifyQuery &Q,
                              fp::ExceptionBehavior ExBehavior,
                              RoundingMode Rounding) {
  return ::simplifyFDivInst(Op0, Op1, FMF, Q, RecursionLimit, ExBehavior,
                            Rounding);
}

static Value *
simplifyFRemInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                 const SimplifyQuery &Q, unsigned,
                 fp::ExceptionBehavior ExBehavior = fp::ebIgnore,
                 RoundingMode Rounding = RoundingMode::NearestTiesToEven) {
  if (isDefaultFPEnvironment(ExBehavior, Rounding))
    if (Constant *C = foldOrCommuteConstant(Instruction::FRem, Op0, Op1, Q))
      return C;

  if (Constant *C = simplifyFPOp({Op0, Op1}, FMF, Q, ExBehavior, Rounding))
    return C;

  if (!isDefaultFPEnvironment(ExBehavior, Rounding))
    return nullptr;

  // Unlike fdiv, the result of frem always matches the sign of the dividend.
  // The constant match may include undef elements in a vector, so return a full
  // zero constant as the result.
  if (FMF.noNaNs()) {
    // +0 % X -> 0
    if (match(Op0, m_PosZeroFP()))
      return ConstantFP::getZero(Op0->getType());
    // -0 % X -> -0
    if (match(Op0, m_NegZeroFP()))
      return ConstantFP::getNegativeZero(Op0->getType());
  }

  return nullptr;
}

Value *llvm::simplifyFRemInst(Value *Op0, Value *Op1, FastMathFlags FMF,
                              const SimplifyQuery &Q,
                              fp::ExceptionBehavior ExBehavior,
                              RoundingMode Rounding) {
  return ::simplifyFRemInst(Op0, Op1, FMF, Q, RecursionLimit, ExBehavior,
                            Rounding);
}

//=== Helper functions for higher up the class hierarchy.

/// Given the operand for a UnaryOperator, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyUnOp(unsigned Opcode, Value *Op, const SimplifyQuery &Q,
                           unsigned MaxRecurse) {
  switch (Opcode) {
  case Instruction::FNeg:
    return simplifyFNegInst(Op, FastMathFlags(), Q, MaxRecurse);
  default:
    llvm_unreachable("Unexpected opcode");
  }
}

/// Given the operand for a UnaryOperator, see if we can fold the result.
/// If not, this returns null.
/// Try to use FastMathFlags when folding the result.
static Value *simplifyFPUnOp(unsigned Opcode, Value *Op,
                             const FastMathFlags &FMF, const SimplifyQuery &Q,
                             unsigned MaxRecurse) {
  switch (Opcode) {
  case Instruction::FNeg:
    return simplifyFNegInst(Op, FMF, Q, MaxRecurse);
  default:
    return simplifyUnOp(Opcode, Op, Q, MaxRecurse);
  }
}

Value *llvm::simplifyUnOp(unsigned Opcode, Value *Op, const SimplifyQuery &Q) {
  return ::simplifyUnOp(Opcode, Op, Q, RecursionLimit);
}

Value *llvm::simplifyUnOp(unsigned Opcode, Value *Op, FastMathFlags FMF,
                          const SimplifyQuery &Q) {
  return ::simplifyFPUnOp(Opcode, Op, FMF, Q, RecursionLimit);
}

/// Given operands for a BinaryOperator, see if we can fold the result.
/// If not, this returns null.
static Value *simplifyBinOp(unsigned Opcode, Value *LHS, Value *RHS,
                            const SimplifyQuery &Q, unsigned MaxRecurse) {
  switch (Opcode) {
  case Instruction::Add:
    return simplifyAddInst(LHS, RHS, /* IsNSW */ false, /* IsNUW */ false, Q,
                           MaxRecurse);
  case Instruction::Sub:
    return simplifySubInst(LHS, RHS,  /* IsNSW */ false, /* IsNUW */ false, Q,
                           MaxRecurse);
  case Instruction::Mul:
    return simplifyMulInst(LHS, RHS, /* IsNSW */ false, /* IsNUW */ false, Q,
                           MaxRecurse);
  case Instruction::SDiv:
    return simplifySDivInst(LHS, RHS, /* IsExact */ false, Q, MaxRecurse);
  case Instruction::UDiv:
    return simplifyUDivInst(LHS, RHS, /* IsExact */ false, Q, MaxRecurse);
  case Instruction::SRem:
    return simplifySRemInst(LHS, RHS, Q, MaxRecurse);
  case Instruction::URem:
    return simplifyURemInst(LHS, RHS, Q, MaxRecurse);
  case Instruction::Shl:
    return simplifyShlInst(LHS, RHS, /* IsNSW */ false, /* IsNUW */ false, Q,
                           MaxRecurse);
  case Instruction::LShr:
    return simplifyLShrInst(LHS, RHS, /* IsExact */ false, Q, MaxRecurse);
  case Instruction::AShr:
    return simplifyAShrInst(LHS, RHS, /* IsExact */ false, Q, MaxRecurse);
  case Instruction::And:
    return simplifyAndInst(LHS, RHS, Q, MaxRecurse);
  case Instruction::Or:
    return simplifyOrInst(LHS, RHS, Q, MaxRecurse);
  case Instruction::Xor:
    return simplifyXorInst(LHS, RHS, Q, MaxRecurse);
  case Instruction::FAdd:
    return simplifyFAddInst(LHS, RHS, FastMathFlags(), Q, MaxRecurse);
  case Instruction::FSub:
    return simplifyFSubInst(LHS, RHS, FastMathFlags(), Q, MaxRecurse);
  case Instruction::FMul:
    return simplifyFMulInst(LHS, RHS, FastMathFlags(), Q, MaxRecurse);
  case Instruction::FDiv:
    return simplifyFDivInst(LHS, RHS, FastMathFlags(), Q, MaxRecurse);
  case Instruction::FRem:
    return simplifyFRemInst(LHS, RHS, FastMathFlags(), Q, MaxRecurse);
  default:
    llvm_unreachable("Unexpected opcode");
  }
}

/// Given operands for a BinaryOperator, see if we can fold the result.
/// If not, this returns null.
/// Try to use FastMathFlags when folding the result.
static Value *simplifyBinOp(unsigned Opcode, Value *LHS, Value *RHS,
                            const FastMathFlags &FMF, const SimplifyQuery &Q,
                            unsigned MaxRecurse) {
  switch (Opcode) {
  case Instruction::FAdd:
    return simplifyFAddInst(LHS, RHS, FMF, Q, MaxRecurse);
  case Instruction::FSub:
    return simplifyFSubInst(LHS, RHS, FMF, Q, MaxRecurse);
  case Instruction::FMul:
    return simplifyFMulInst(LHS, RHS, FMF, Q, MaxRecurse);
  case Instruction::FDiv:
    return simplifyFDivInst(LHS, RHS, FMF, Q, MaxRecurse);
  default:
    return simplifyBinOp(Opcode, LHS, RHS, Q, MaxRecurse);
  }
}

Value *llvm::simplifyBinOp(unsigned Opcode, Value *LHS, Value *RHS,
                           const SimplifyQuery &Q) {
  return ::simplifyBinOp(Opcode, LHS, RHS, Q, RecursionLimit);
}

Value *llvm::simplifyBinOp(unsigned Opcode, Value *LHS, Value *RHS,
                           FastMathFlags FMF, const SimplifyQuery &Q) {
  return ::simplifyBinOp(Opcode, LHS, RHS, FMF, Q, RecursionLimit);
}

/// Given operands for a CmpInst, see if we can fold the result.
static Value *simplifyCmpInst(unsigned Predicate, Value *LHS, Value *RHS,
                              const SimplifyQuery &Q, unsigned MaxRecurse) {
  if (CmpInst::isIntPredicate((CmpInst::Predicate)Predicate))
    return simplifyICmpInst(Predicate, LHS, RHS, Q, MaxRecurse);
  return simplifyFCmpInst(Predicate, LHS, RHS, FastMathFlags(), Q, MaxRecurse);
}

Value *llvm::simplifyCmpInst(unsigned Predicate, Value *LHS, Value *RHS,
                             const SimplifyQuery &Q) {
  return ::simplifyCmpInst(Predicate, LHS, RHS, Q, RecursionLimit);
}

static bool isIdempotent(Intrinsic::ID ID) {
  switch (ID) {
  default:
    return false;

  // Unary idempotent: f(f(x)) = f(x)
  case Intrinsic::fabs:
  case Intrinsic::floor:
  case Intrinsic::ceil:
  case Intrinsic::trunc:
  case Intrinsic::rint:
  case Intrinsic::nearbyint:
  case Intrinsic::round:
  case Intrinsic::roundeven:
  case Intrinsic::canonicalize:
  case Intrinsic::arithmetic_fence:
    return true;
  }
}

/// Return true if the intrinsic rounds a floating-point value to an integral
/// floating-point value (not an integer type).
static bool removesFPFraction(Intrinsic::ID ID) {
  switch (ID) {
  default:
    return false;

  case Intrinsic::floor:
  case Intrinsic::ceil:
  case Intrinsic::trunc:
  case Intrinsic::rint:
  case Intrinsic::nearbyint:
  case Intrinsic::round:
  case Intrinsic::roundeven:
    return true;
  }
}

static Value *simplifyRelativeLoad(Constant *Ptr, Constant *Offset,
                                   const DataLayout &DL) {
  GlobalValue *PtrSym;
  APInt PtrOffset;
  if (!IsConstantOffsetFromGlobal(Ptr, PtrSym, PtrOffset, DL))
    return nullptr;

  Type *Int32Ty = Type::getInt32Ty(Ptr->getContext());

  auto *OffsetConstInt = dyn_cast<ConstantInt>(Offset);
  if (!OffsetConstInt || OffsetConstInt->getBitWidth() > 64)
    return nullptr;

  APInt OffsetInt = OffsetConstInt->getValue().sextOrTrunc(
      DL.getIndexTypeSizeInBits(Ptr->getType()));
  if (OffsetInt.srem(4) != 0)
    return nullptr;

  Constant *Loaded =
      ConstantFoldLoadFromConstPtr(Ptr, Int32Ty, std::move(OffsetInt), DL);
  if (!Loaded)
    return nullptr;

  auto *LoadedCE = dyn_cast<ConstantExpr>(Loaded);
  if (!LoadedCE)
    return nullptr;

  if (LoadedCE->getOpcode() == Instruction::Trunc) {
    LoadedCE = dyn_cast<ConstantExpr>(LoadedCE->getOperand(0));
    if (!LoadedCE)
      return nullptr;
  }

  if (LoadedCE->getOpcode() != Instruction::Sub)
    return nullptr;

  auto *LoadedLHS = dyn_cast<ConstantExpr>(LoadedCE->getOperand(0));
  if (!LoadedLHS || LoadedLHS->getOpcode() != Instruction::PtrToInt)
    return nullptr;
  auto *LoadedLHSPtr = LoadedLHS->getOperand(0);

  Constant *LoadedRHS = LoadedCE->getOperand(1);
  GlobalValue *LoadedRHSSym;
  APInt LoadedRHSOffset;
  if (!IsConstantOffsetFromGlobal(LoadedRHS, LoadedRHSSym, LoadedRHSOffset,
                                  DL) ||
      PtrSym != LoadedRHSSym || PtrOffset != LoadedRHSOffset)
    return nullptr;

  return LoadedLHSPtr;
}

// TODO: Need to pass in FastMathFlags
static Value *simplifyLdexp(Value *Op0, Value *Op1, const SimplifyQuery &Q,
                            bool IsStrict) {
  // ldexp(poison, x) -> poison
  // ldexp(x, poison) -> poison
  if (isa<PoisonValue>(Op0) || isa<PoisonValue>(Op1))
    return Op0;

  // ldexp(undef, x) -> nan
  if (Q.isUndefValue(Op0))
    return ConstantFP::getNaN(Op0->getType());

  if (!IsStrict) {
    // TODO: Could insert a canonicalize for strict

    // ldexp(x, undef) -> x
    if (Q.isUndefValue(Op1))
      return Op0;
  }

  const APFloat *C = nullptr;
  match(Op0, PatternMatch::m_APFloat(C));

  // These cases should be safe, even with strictfp.
  // ldexp(0.0, x) -> 0.0
  // ldexp(-0.0, x) -> -0.0
  // ldexp(inf, x) -> inf
  // ldexp(-inf, x) -> -inf
  if (C && (C->isZero() || C->isInfinity()))
    return Op0;

  // These are canonicalization dropping, could do it if we knew how we could
  // ignore denormal flushes and target handling of nan payload bits.
  if (IsStrict)
    return nullptr;

  // TODO: Could quiet this with strictfp if the exception mode isn't strict.
  if (C && C->isNaN())
    return ConstantFP::get(Op0->getType(), C->makeQuiet());

  // ldexp(x, 0) -> x

  // TODO: Could fold this if we know the exception mode isn't
  // strict, we know the denormal mode and other target modes.
  if (match(Op1, PatternMatch::m_ZeroInt()))
    return Op0;

  return nullptr;
}

static Value *simplifyUnaryIntrinsic(Function *F, Value *Op0,
                                     const SimplifyQuery &Q,
                                     const CallBase *Call) {
  // Idempotent functions return the same result when called repeatedly.
  Intrinsic::ID IID = F->getIntrinsicID();
  if (isIdempotent(IID))
    if (auto *II = dyn_cast<IntrinsicInst>(Op0))
      if (II->getIntrinsicID() == IID)
        return II;

  if (removesFPFraction(IID)) {
    // Converting from int or calling a rounding function always results in a
    // finite integral number or infinity. For those inputs, rounding functions
    // always return the same value, so the (2nd) rounding is eliminated. Ex:
    // floor (sitofp x) -> sitofp x
    // round (ceil x) -> ceil x
    auto *II = dyn_cast<IntrinsicInst>(Op0);
    if ((II && removesFPFraction(II->getIntrinsicID())) ||
        match(Op0, m_SIToFP(m_Value())) || match(Op0, m_UIToFP(m_Value())))
      return Op0;
  }

  Value *X;
  switch (IID) {
  case Intrinsic::fabs:
    if (computeKnownFPSignBit(Op0, /*Depth=*/0, Q) == false)
      return Op0;
    break;
  case Intrinsic::bswap:
    // bswap(bswap(x)) -> x
    if (match(Op0, m_BSwap(m_Value(X))))
      return X;
    break;
  case Intrinsic::bitreverse:
    // bitreverse(bitreverse(x)) -> x
    if (match(Op0, m_BitReverse(m_Value(X))))
      return X;
    break;
  case Intrinsic::ctpop: {
    // ctpop(X) -> 1 iff X is non-zero power of 2.
    if (isKnownToBeAPowerOfTwo(Op0, Q.DL, /*OrZero*/ false, 0, Q.AC, Q.CxtI,
                               Q.DT))
      return ConstantInt::get(Op0->getType(), 1);
    // If everything but the lowest bit is zero, that bit is the pop-count. Ex:
    // ctpop(and X, 1) --> and X, 1
    unsigned BitWidth = Op0->getType()->getScalarSizeInBits();
    if (MaskedValueIsZero(Op0, APInt::getHighBitsSet(BitWidth, BitWidth - 1),
                          Q))
      return Op0;
    break;
  }
  case Intrinsic::exp:
    // exp(log(x)) -> x
    if (Call->hasAllowReassoc() &&
        match(Op0, m_Intrinsic<Intrinsic::log>(m_Value(X))))
      return X;
    break;
  case Intrinsic::exp2:
    // exp2(log2(x)) -> x
    if (Call->hasAllowReassoc() &&
        match(Op0, m_Intrinsic<Intrinsic::log2>(m_Value(X))))
      return X;
    break;
  case Intrinsic::exp10:
    // exp10(log10(x)) -> x
    if (Call->hasAllowReassoc() &&
        match(Op0, m_Intrinsic<Intrinsic::log10>(m_Value(X))))
      return X;
    break;
  case Intrinsic::log:
    // log(exp(x)) -> x
    if (Call->hasAllowReassoc() &&
        match(Op0, m_Intrinsic<Intrinsic::exp>(m_Value(X))))
      return X;
    break;
  case Intrinsic::log2:
    // log2(exp2(x)) -> x
    if (Call->hasAllowReassoc() &&
        (match(Op0, m_Intrinsic<Intrinsic::exp2>(m_Value(X))) ||
         match(Op0,
               m_Intrinsic<Intrinsic::pow>(m_SpecificFP(2.0), m_Value(X)))))
      return X;
    break;
  case Intrinsic::log10:
    // log10(pow(10.0, x)) -> x
    // log10(exp10(x)) -> x
    if (Call->hasAllowReassoc() &&
        (match(Op0, m_Intrinsic<Intrinsic::exp10>(m_Value(X))) ||
         match(Op0,
               m_Intrinsic<Intrinsic::pow>(m_SpecificFP(10.0), m_Value(X)))))
      return X;
    break;
  case Intrinsic::vector_reverse:
    // vector.reverse(vector.reverse(x)) -> x
    if (match(Op0, m_VecReverse(m_Value(X))))
      return X;
    // vector.reverse(splat(X)) -> splat(X)
    if (isSplatValue(Op0))
      return Op0;
    break;
  case Intrinsic::frexp: {
    // Frexp is idempotent with the added complication of the struct return.
    if (match(Op0, m_ExtractValue<0>(m_Value(X)))) {
      if (match(X, m_Intrinsic<Intrinsic::frexp>(m_Value())))
        return X;
    }

    break;
  }
  default:
    break;
  }

  return nullptr;
}

/// Given a min/max intrinsic, see if it can be removed based on having an
/// operand that is another min/max intrinsic with shared operand(s). The caller
/// is expected to swap the operand arguments to handle commutation.
static Value *foldMinMaxSharedOp(Intrinsic::ID IID, Value *Op0, Value *Op1) {
  Value *X, *Y;
  if (!match(Op0, m_MaxOrMin(m_Value(X), m_Value(Y))))
    return nullptr;

  auto *MM0 = dyn_cast<IntrinsicInst>(Op0);
  if (!MM0)
    return nullptr;
  Intrinsic::ID IID0 = MM0->getIntrinsicID();

  if (Op1 == X || Op1 == Y ||
      match(Op1, m_c_MaxOrMin(m_Specific(X), m_Specific(Y)))) {
    // max (max X, Y), X --> max X, Y
    if (IID0 == IID)
      return MM0;
    // max (min X, Y), X --> X
    if (IID0 == getInverseMinMaxIntrinsic(IID))
      return Op1;
  }
  return nullptr;
}

/// Given a min/max intrinsic, see if it can be removed based on having an
/// operand that is another min/max intrinsic with shared operand(s). The caller
/// is expected to swap the operand arguments to handle commutation.
static Value *foldMinimumMaximumSharedOp(Intrinsic::ID IID, Value *Op0,
                                         Value *Op1) {
  assert((IID == Intrinsic::maxnum || IID == Intrinsic::minnum ||
          IID == Intrinsic::maximum || IID == Intrinsic::minimum) &&
         "Unsupported intrinsic");

  auto *M0 = dyn_cast<IntrinsicInst>(Op0);
  // If Op0 is not the same intrinsic as IID, do not process.
  // This is a difference with integer min/max handling. We do not process the
  // case like max(min(X,Y),min(X,Y)) => min(X,Y). But it can be handled by GVN.
  if (!M0 || M0->getIntrinsicID() != IID)
    return nullptr;
  Value *X0 = M0->getOperand(0);
  Value *Y0 = M0->getOperand(1);
  // Simple case, m(m(X,Y), X) => m(X, Y)
  //              m(m(X,Y), Y) => m(X, Y)
  // For minimum/maximum, X is NaN => m(NaN, Y) == NaN and m(NaN, NaN) == NaN.
  // For minimum/maximum, Y is NaN => m(X, NaN) == NaN  and m(NaN, NaN) == NaN.
  // For minnum/maxnum, X is NaN => m(NaN, Y) == Y and m(Y, Y) == Y.
  // For minnum/maxnum, Y is NaN => m(X, NaN) == X and m(X, NaN) == X.
  if (X0 == Op1 || Y0 == Op1)
    return M0;

  auto *M1 = dyn_cast<IntrinsicInst>(Op1);
  if (!M1)
    return nullptr;
  Value *X1 = M1->getOperand(0);
  Value *Y1 = M1->getOperand(1);
  Intrinsic::ID IID1 = M1->getIntrinsicID();
  // we have a case m(m(X,Y),m'(X,Y)) taking into account m' is commutative.
  // if m' is m or inversion of m => m(m(X,Y),m'(X,Y)) == m(X,Y).
  // For minimum/maximum, X is NaN => m(NaN,Y) == m'(NaN, Y) == NaN.
  // For minimum/maximum, Y is NaN => m(X,NaN) == m'(X, NaN) == NaN.
  // For minnum/maxnum, X is NaN => m(NaN,Y) == m'(NaN, Y) == Y.
  // For minnum/maxnum, Y is NaN => m(X,NaN) == m'(X, NaN) == X.
  if ((X0 == X1 && Y0 == Y1) || (X0 == Y1 && Y0 == X1))
    if (IID1 == IID || getInverseMinMaxIntrinsic(IID1) == IID)
      return M0;

  return nullptr;
}

Value *llvm::simplifyBinaryIntrinsic(Intrinsic::ID IID, Type *ReturnType,
                                     Value *Op0, Value *Op1,
                                     const SimplifyQuery &Q,
                                     const CallBase *Call) {
  unsigned BitWidth = ReturnType->getScalarSizeInBits();
  switch (IID) {
  case Intrinsic::abs:
    // abs(abs(x)) -> abs(x). We don't need to worry about the nsw arg here.
    // It is always ok to pick the earlier abs. We'll just lose nsw if its only
    // on the outer abs.
    if (match(Op0, m_Intrinsic<Intrinsic::abs>(m_Value(), m_Value())))
      return Op0;
    break;

  case Intrinsic::cttz: {
    Value *X;
    if (match(Op0, m_Shl(m_One(), m_Value(X))))
      return X;
    break;
  }
  case Intrinsic::ctlz: {
    Value *X;
    if (match(Op0, m_LShr(m_Negative(), m_Value(X))))
      return X;
    if (match(Op0, m_AShr(m_Negative(), m_Value())))
      return Constant::getNullValue(ReturnType);
    break;
  }
  case Intrinsic::ptrmask: {
    if (isa<PoisonValue>(Op0) || isa<PoisonValue>(Op1))
      return PoisonValue::get(Op0->getType());

    // NOTE: We can't apply this simplifications based on the value of Op1
    // because we need to preserve provenance.
    if (Q.isUndefValue(Op0) || match(Op0, m_Zero()))
      return Constant::getNullValue(Op0->getType());

    assert(Op1->getType()->getScalarSizeInBits() ==
               Q.DL.getIndexTypeSizeInBits(Op0->getType()) &&
           "Invalid mask width");
    // If index-width (mask size) is less than pointer-size then mask is
    // 1-extended.
    if (match(Op1, m_PtrToInt(m_Specific(Op0))))
      return Op0;

    // NOTE: We may have attributes associated with the return value of the
    // llvm.ptrmask intrinsic that will be lost when we just return the
    // operand. We should try to preserve them.
    if (match(Op1, m_AllOnes()) || Q.isUndefValue(Op1))
      return Op0;

    Constant *C;
    if (match(Op1, m_ImmConstant(C))) {
      KnownBits PtrKnown = computeKnownBits(Op0, /*Depth=*/0, Q);
      // See if we only masking off bits we know are already zero due to
      // alignment.
      APInt IrrelevantPtrBits =
          PtrKnown.Zero.zextOrTrunc(C->getType()->getScalarSizeInBits());
      C = ConstantFoldBinaryOpOperands(
          Instruction::Or, C, ConstantInt::get(C->getType(), IrrelevantPtrBits),
          Q.DL);
      if (C != nullptr && C->isAllOnesValue())
        return Op0;
    }
    break;
  }
  case Intrinsic::smax:
  case Intrinsic::smin:
  case Intrinsic::umax:
  case Intrinsic::umin: {
    // If the arguments are the same, this is a no-op.
    if (Op0 == Op1)
      return Op0;

    // Canonicalize immediate constant operand as Op1.
    if (match(Op0, m_ImmConstant()))
      std::swap(Op0, Op1);

    // Assume undef is the limit value.
    if (Q.isUndefValue(Op1))
      return ConstantInt::get(
          ReturnType, MinMaxIntrinsic::getSaturationPoint(IID, BitWidth));

    const APInt *C;
    if (match(Op1, m_APIntAllowPoison(C))) {
      // Clamp to limit value. For example:
      // umax(i8 %x, i8 255) --> 255
      if (*C == MinMaxIntrinsic::getSaturationPoint(IID, BitWidth))
        return ConstantInt::get(ReturnType, *C);

      // If the constant op is the opposite of the limit value, the other must
      // be larger/smaller or equal. For example:
      // umin(i8 %x, i8 255) --> %x
      if (*C == MinMaxIntrinsic::getSaturationPoint(
                    getInverseMinMaxIntrinsic(IID), BitWidth))
        return Op0;

      // Remove nested call if constant operands allow it. Example:
      // max (max X, 7), 5 -> max X, 7
      auto *MinMax0 = dyn_cast<IntrinsicInst>(Op0);
      if (MinMax0 && MinMax0->getIntrinsicID() == IID) {
        // TODO: loosen undef/splat restrictions for vector constants.
        Value *M00 = MinMax0->getOperand(0), *M01 = MinMax0->getOperand(1);
        const APInt *InnerC;
        if ((match(M00, m_APInt(InnerC)) || match(M01, m_APInt(InnerC))) &&
            ICmpInst::compare(*InnerC, *C,
                              ICmpInst::getNonStrictPredicate(
                                  MinMaxIntrinsic::getPredicate(IID))))
          return Op0;
      }
    }

    if (Value *V = foldMinMaxSharedOp(IID, Op0, Op1))
      return V;
    if (Value *V = foldMinMaxSharedOp(IID, Op1, Op0))
      return V;

    ICmpInst::Predicate Pred =
        ICmpInst::getNonStrictPredicate(MinMaxIntrinsic::getPredicate(IID));
    if (isICmpTrue(Pred, Op0, Op1, Q.getWithoutUndef(), RecursionLimit))
      return Op0;
    if (isICmpTrue(Pred, Op1, Op0, Q.getWithoutUndef(), RecursionLimit))
      return Op1;

    break;
  }
  case Intrinsic::scmp:
  case Intrinsic::ucmp: {
    // Fold to a constant if the relationship between operands can be
    // established with certainty
    if (isICmpTrue(CmpInst::ICMP_EQ, Op0, Op1, Q, RecursionLimit))
      return Constant::getNullValue(ReturnType);

    ICmpInst::Predicate PredGT =
        IID == Intrinsic::scmp ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
    if (isICmpTrue(PredGT, Op0, Op1, Q, RecursionLimit))
      return ConstantInt::get(ReturnType, 1);

    ICmpInst::Predicate PredLT =
        IID == Intrinsic::scmp ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
    if (isICmpTrue(PredLT, Op0, Op1, Q, RecursionLimit))
      return ConstantInt::getSigned(ReturnType, -1);

    break;
  }
  case Intrinsic::usub_with_overflow:
  case Intrinsic::ssub_with_overflow:
    // X - X -> { 0, false }
    // X - undef -> { 0, false }
    // undef - X -> { 0, false }
    if (Op0 == Op1 || Q.isUndefValue(Op0) || Q.isUndefValue(Op1))
      return Constant::getNullValue(ReturnType);
    break;
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::sadd_with_overflow:
    // X + undef -> { -1, false }
    // undef + x -> { -1, false }
    if (Q.isUndefValue(Op0) || Q.isUndefValue(Op1)) {
      return ConstantStruct::get(
          cast<StructType>(ReturnType),
          {Constant::getAllOnesValue(ReturnType->getStructElementType(0)),
           Constant::getNullValue(ReturnType->getStructElementType(1))});
    }
    break;
  case Intrinsic::umul_with_overflow:
  case Intrinsic::smul_with_overflow:
    // 0 * X -> { 0, false }
    // X * 0 -> { 0, false }
    if (match(Op0, m_Zero()) || match(Op1, m_Zero()))
      return Constant::getNullValue(ReturnType);
    // undef * X -> { 0, false }
    // X * undef -> { 0, false }
    if (Q.isUndefValue(Op0) || Q.isUndefValue(Op1))
      return Constant::getNullValue(ReturnType);
    break;
  case Intrinsic::uadd_sat:
    // sat(MAX + X) -> MAX
    // sat(X + MAX) -> MAX
    if (match(Op0, m_AllOnes()) || match(Op1, m_AllOnes()))
      return Constant::getAllOnesValue(ReturnType);
    [[fallthrough]];
  case Intrinsic::sadd_sat:
    // sat(X + undef) -> -1
    // sat(undef + X) -> -1
    // For unsigned: Assume undef is MAX, thus we saturate to MAX (-1).
    // For signed: Assume undef is ~X, in which case X + ~X = -1.
    if (Q.isUndefValue(Op0) || Q.isUndefValue(Op1))
      return Constant::getAllOnesValue(ReturnType);

    // X + 0 -> X
    if (match(Op1, m_Zero()))
      return Op0;
    // 0 + X -> X
    if (match(Op0, m_Zero()))
      return Op1;
    break;
  case Intrinsic::usub_sat:
    // sat(0 - X) -> 0, sat(X - MAX) -> 0
    if (match(Op0, m_Zero()) || match(Op1, m_AllOnes()))
      return Constant::getNullValue(ReturnType);
    [[fallthrough]];
  case Intrinsic::ssub_sat:
    // X - X -> 0, X - undef -> 0, undef - X -> 0
    if (Op0 == Op1 || Q.isUndefValue(Op0) || Q.isUndefValue(Op1))
      return Constant::getNullValue(ReturnType);
    // X - 0 -> X
    if (match(Op1, m_Zero()))
      return Op0;
    break;
  case Intrinsic::load_relative:
    if (auto *C0 = dyn_cast<Constant>(Op0))
      if (auto *C1 = dyn_cast<Constant>(Op1))
        return simplifyRelativeLoad(C0, C1, Q.DL);
    break;
  case Intrinsic::powi:
    if (auto *Power = dyn_cast<ConstantInt>(Op1)) {
      // powi(x, 0) -> 1.0
      if (Power->isZero())
        return ConstantFP::get(Op0->getType(), 1.0);
      // powi(x, 1) -> x
      if (Power->isOne())
        return Op0;
    }
    break;
  case Intrinsic::ldexp:
    return simplifyLdexp(Op0, Op1, Q, false);
  case Intrinsic::copysign:
    // copysign X, X --> X
    if (Op0 == Op1)
      return Op0;
    // copysign -X, X --> X
    // copysign X, -X --> -X
    if (match(Op0, m_FNeg(m_Specific(Op1))) ||
        match(Op1, m_FNeg(m_Specific(Op0))))
      return Op1;
    break;
  case Intrinsic::is_fpclass: {
    if (isa<PoisonValue>(Op0))
      return PoisonValue::get(ReturnType);

    uint64_t Mask = cast<ConstantInt>(Op1)->getZExtValue();
    // If all tests are made, it doesn't matter what the value is.
    if ((Mask & fcAllFlags) == fcAllFlags)
      return ConstantInt::get(ReturnType, true);
    if ((Mask & fcAllFlags) == 0)
      return ConstantInt::get(ReturnType, false);
    if (Q.isUndefValue(Op0))
      return UndefValue::get(ReturnType);
    break;
  }
  case Intrinsic::maxnum:
  case Intrinsic::minnum:
  case Intrinsic::maximum:
  case Intrinsic::minimum: {
    // If the arguments are the same, this is a no-op.
    if (Op0 == Op1)
      return Op0;

    // Canonicalize constant operand as Op1.
    if (isa<Constant>(Op0))
      std::swap(Op0, Op1);

    // If an argument is undef, return the other argument.
    if (Q.isUndefValue(Op1))
      return Op0;

    bool PropagateNaN = IID == Intrinsic::minimum || IID == Intrinsic::maximum;
    bool IsMin = IID == Intrinsic::minimum || IID == Intrinsic::minnum;

    // minnum(X, nan) -> X
    // maxnum(X, nan) -> X
    // minimum(X, nan) -> nan
    // maximum(X, nan) -> nan
    if (match(Op1, m_NaN()))
      return PropagateNaN ? propagateNaN(cast<Constant>(Op1)) : Op0;

    // In the following folds, inf can be replaced with the largest finite
    // float, if the ninf flag is set.
    const APFloat *C;
    if (match(Op1, m_APFloat(C)) &&
        (C->isInfinity() || (Call && Call->hasNoInfs() && C->isLargest()))) {
      // minnum(X, -inf) -> -inf
      // maxnum(X, +inf) -> +inf
      // minimum(X, -inf) -> -inf if nnan
      // maximum(X, +inf) -> +inf if nnan
      if (C->isNegative() == IsMin &&
          (!PropagateNaN || (Call && Call->hasNoNaNs())))
        return ConstantFP::get(ReturnType, *C);

      // minnum(X, +inf) -> X if nnan
      // maxnum(X, -inf) -> X if nnan
      // minimum(X, +inf) -> X
      // maximum(X, -inf) -> X
      if (C->isNegative() != IsMin &&
          (PropagateNaN || (Call && Call->hasNoNaNs())))
        return Op0;
    }

    // Min/max of the same operation with common operand:
    // m(m(X, Y)), X --> m(X, Y) (4 commuted variants)
    if (Value *V = foldMinimumMaximumSharedOp(IID, Op0, Op1))
      return V;
    if (Value *V = foldMinimumMaximumSharedOp(IID, Op1, Op0))
      return V;

    break;
  }
  case Intrinsic::vector_extract: {
    // (extract_vector (insert_vector _, X, 0), 0) -> X
    unsigned IdxN = cast<ConstantInt>(Op1)->getZExtValue();
    Value *X = nullptr;
    if (match(Op0, m_Intrinsic<Intrinsic::vector_insert>(m_Value(), m_Value(X),
                                                         m_Zero())) &&
        IdxN == 0 && X->getType() == ReturnType)
      return X;

    break;
  }
  default:
    break;
  }

  return nullptr;
}

static Value *simplifyIntrinsic(CallBase *Call, Value *Callee,
                                ArrayRef<Value *> Args,
                                const SimplifyQuery &Q) {
  // Operand bundles should not be in Args.
  assert(Call->arg_size() == Args.size());
  unsigned NumOperands = Args.size();
  Function *F = cast<Function>(Callee);
  Intrinsic::ID IID = F->getIntrinsicID();

  // Most of the intrinsics with no operands have some kind of side effect.
  // Don't simplify.
  if (!NumOperands) {
    switch (IID) {
    case Intrinsic::vscale: {
      Type *RetTy = F->getReturnType();
      ConstantRange CR = getVScaleRange(Call->getFunction(), 64);
      if (const APInt *C = CR.getSingleElement())
        return ConstantInt::get(RetTy, C->getZExtValue());
      return nullptr;
    }
    default:
      return nullptr;
    }
  }

  if (NumOperands == 1)
    return simplifyUnaryIntrinsic(F, Args[0], Q, Call);

  if (NumOperands == 2)
    return simplifyBinaryIntrinsic(IID, F->getReturnType(), Args[0], Args[1], Q,
                                   Call);

  // Handle intrinsics with 3 or more arguments.
  switch (IID) {
  case Intrinsic::masked_load:
  case Intrinsic::masked_gather: {
    Value *MaskArg = Args[2];
    Value *PassthruArg = Args[3];
    // If the mask is all zeros or undef, the "passthru" argument is the result.
    if (maskIsAllZeroOrUndef(MaskArg))
      return PassthruArg;
    return nullptr;
  }
  case Intrinsic::fshl:
  case Intrinsic::fshr: {
    Value *Op0 = Args[0], *Op1 = Args[1], *ShAmtArg = Args[2];

    // If both operands are undef, the result is undef.
    if (Q.isUndefValue(Op0) && Q.isUndefValue(Op1))
      return UndefValue::get(F->getReturnType());

    // If shift amount is undef, assume it is zero.
    if (Q.isUndefValue(ShAmtArg))
      return Args[IID == Intrinsic::fshl ? 0 : 1];

    const APInt *ShAmtC;
    if (match(ShAmtArg, m_APInt(ShAmtC))) {
      // If there's effectively no shift, return the 1st arg or 2nd arg.
      APInt BitWidth = APInt(ShAmtC->getBitWidth(), ShAmtC->getBitWidth());
      if (ShAmtC->urem(BitWidth).isZero())
        return Args[IID == Intrinsic::fshl ? 0 : 1];
    }

    // Rotating zero by anything is zero.
    if (match(Op0, m_Zero()) && match(Op1, m_Zero()))
      return ConstantInt::getNullValue(F->getReturnType());

    // Rotating -1 by anything is -1.
    if (match(Op0, m_AllOnes()) && match(Op1, m_AllOnes()))
      return ConstantInt::getAllOnesValue(F->getReturnType());

    return nullptr;
  }
  case Intrinsic::experimental_constrained_fma: {
    auto *FPI = cast<ConstrainedFPIntrinsic>(Call);
    if (Value *V = simplifyFPOp(Args, {}, Q, *FPI->getExceptionBehavior(),
                                *FPI->getRoundingMode()))
      return V;
    return nullptr;
  }
  case Intrinsic::fma:
  case Intrinsic::fmuladd: {
    if (Value *V = simplifyFPOp(Args, {}, Q, fp::ebIgnore,
                                RoundingMode::NearestTiesToEven))
      return V;
    return nullptr;
  }
  case Intrinsic::smul_fix:
  case Intrinsic::smul_fix_sat: {
    Value *Op0 = Args[0];
    Value *Op1 = Args[1];
    Value *Op2 = Args[2];
    Type *ReturnType = F->getReturnType();

    // Canonicalize constant operand as Op1 (ConstantFolding handles the case
    // when both Op0 and Op1 are constant so we do not care about that special
    // case here).
    if (isa<Constant>(Op0))
      std::swap(Op0, Op1);

    // X * 0 -> 0
    if (match(Op1, m_Zero()))
      return Constant::getNullValue(ReturnType);

    // X * undef -> 0
    if (Q.isUndefValue(Op1))
      return Constant::getNullValue(ReturnType);

    // X * (1 << Scale) -> X
    APInt ScaledOne =
        APInt::getOneBitSet(ReturnType->getScalarSizeInBits(),
                            cast<ConstantInt>(Op2)->getZExtValue());
    if (ScaledOne.isNonNegative() && match(Op1, m_SpecificInt(ScaledOne)))
      return Op0;

    return nullptr;
  }
  case Intrinsic::vector_insert: {
    Value *Vec = Args[0];
    Value *SubVec = Args[1];
    Value *Idx = Args[2];
    Type *ReturnType = F->getReturnType();

    // (insert_vector Y, (extract_vector X, 0), 0) -> X
    // where: Y is X, or Y is undef
    unsigned IdxN = cast<ConstantInt>(Idx)->getZExtValue();
    Value *X = nullptr;
    if (match(SubVec,
              m_Intrinsic<Intrinsic::vector_extract>(m_Value(X), m_Zero())) &&
        (Q.isUndefValue(Vec) || Vec == X) && IdxN == 0 &&
        X->getType() == ReturnType)
      return X;

    return nullptr;
  }
  case Intrinsic::experimental_constrained_fadd: {
    auto *FPI = cast<ConstrainedFPIntrinsic>(Call);
    return simplifyFAddInst(Args[0], Args[1], FPI->getFastMathFlags(), Q,
                            *FPI->getExceptionBehavior(),
                            *FPI->getRoundingMode());
  }
  case Intrinsic::experimental_constrained_fsub: {
    auto *FPI = cast<ConstrainedFPIntrinsic>(Call);
    return simplifyFSubInst(Args[0], Args[1], FPI->getFastMathFlags(), Q,
                            *FPI->getExceptionBehavior(),
                            *FPI->getRoundingMode());
  }
  case Intrinsic::experimental_constrained_fmul: {
    auto *FPI = cast<ConstrainedFPIntrinsic>(Call);
    return simplifyFMulInst(Args[0], Args[1], FPI->getFastMathFlags(), Q,
                            *FPI->getExceptionBehavior(),
                            *FPI->getRoundingMode());
  }
  case Intrinsic::experimental_constrained_fdiv: {
    auto *FPI = cast<ConstrainedFPIntrinsic>(Call);
    return simplifyFDivInst(Args[0], Args[1], FPI->getFastMathFlags(), Q,
                            *FPI->getExceptionBehavior(),
                            *FPI->getRoundingMode());
  }
  case Intrinsic::experimental_constrained_frem: {
    auto *FPI = cast<ConstrainedFPIntrinsic>(Call);
    return simplifyFRemInst(Args[0], Args[1], FPI->getFastMathFlags(), Q,
                            *FPI->getExceptionBehavior(),
                            *FPI->getRoundingMode());
  }
  case Intrinsic::experimental_constrained_ldexp:
    return simplifyLdexp(Args[0], Args[1], Q, true);
  case Intrinsic::experimental_gc_relocate: {
    GCRelocateInst &GCR = *cast<GCRelocateInst>(Call);
    Value *DerivedPtr = GCR.getDerivedPtr();
    Value *BasePtr = GCR.getBasePtr();

    // Undef is undef, even after relocation.
    if (isa<UndefValue>(DerivedPtr) || isa<UndefValue>(BasePtr)) {
      return UndefValue::get(GCR.getType());
    }

    if (auto *PT = dyn_cast<PointerType>(GCR.getType())) {
      // For now, the assumption is that the relocation of null will be null
      // for most any collector. If this ever changes, a corresponding hook
      // should be added to GCStrategy and this code should check it first.
      if (isa<ConstantPointerNull>(DerivedPtr)) {
        // Use null-pointer of gc_relocate's type to replace it.
        return ConstantPointerNull::get(PT);
      }
    }
    return nullptr;
  }
  default:
    return nullptr;
  }
}

static Value *tryConstantFoldCall(CallBase *Call, Value *Callee,
                                  ArrayRef<Value *> Args,
                                  const SimplifyQuery &Q) {
  auto *F = dyn_cast<Function>(Callee);
  if (!F || !canConstantFoldCallTo(Call, F))
    return nullptr;

  SmallVector<Constant *, 4> ConstantArgs;
  ConstantArgs.reserve(Args.size());
  for (Value *Arg : Args) {
    Constant *C = dyn_cast<Constant>(Arg);
    if (!C) {
      if (isa<MetadataAsValue>(Arg))
        continue;
      return nullptr;
    }
    ConstantArgs.push_back(C);
  }

  return ConstantFoldCall(Call, F, ConstantArgs, Q.TLI);
}

Value *llvm::simplifyCall(CallBase *Call, Value *Callee, ArrayRef<Value *> Args,
                          const SimplifyQuery &Q) {
  // Args should not contain operand bundle operands.
  assert(Call->arg_size() == Args.size());

  // musttail calls can only be simplified if they are also DCEd.
  // As we can't guarantee this here, don't simplify them.
  if (Call->isMustTailCall())
    return nullptr;

  // call undef -> poison
  // call null -> poison
  if (isa<UndefValue>(Callee) || isa<ConstantPointerNull>(Callee))
    return PoisonValue::get(Call->getType());

  if (Value *V = tryConstantFoldCall(Call, Callee, Args, Q))
    return V;

  auto *F = dyn_cast<Function>(Callee);
  if (F && F->isIntrinsic())
    if (Value *Ret = simplifyIntrinsic(Call, Callee, Args, Q))
      return Ret;

  return nullptr;
}

Value *llvm::simplifyConstrainedFPCall(CallBase *Call, const SimplifyQuery &Q) {
  assert(isa<ConstrainedFPIntrinsic>(Call));
  SmallVector<Value *, 4> Args(Call->args());
  if (Value *V = tryConstantFoldCall(Call, Call->getCalledOperand(), Args, Q))
    return V;
  if (Value *Ret = simplifyIntrinsic(Call, Call->getCalledOperand(), Args, Q))
    return Ret;
  return nullptr;
}

/// Given operands for a Freeze, see if we can fold the result.
static Value *simplifyFreezeInst(Value *Op0, const SimplifyQuery &Q) {
  // Use a utility function defined in ValueTracking.
  if (llvm::isGuaranteedNotToBeUndefOrPoison(Op0, Q.AC, Q.CxtI, Q.DT))
    return Op0;
  // We have room for improvement.
  return nullptr;
}

Value *llvm::simplifyFreezeInst(Value *Op0, const SimplifyQuery &Q) {
  return ::simplifyFreezeInst(Op0, Q);
}

Value *llvm::simplifyLoadInst(LoadInst *LI, Value *PtrOp,
                              const SimplifyQuery &Q) {
  if (LI->isVolatile())
    return nullptr;

  if (auto *PtrOpC = dyn_cast<Constant>(PtrOp))
    return ConstantFoldLoadFromConstPtr(PtrOpC, LI->getType(), Q.DL);

  // We can only fold the load if it is from a constant global with definitive
  // initializer. Skip expensive logic if this is not the case.
  auto *GV = dyn_cast<GlobalVariable>(getUnderlyingObject(PtrOp));
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
    return nullptr;

  // If GlobalVariable's initializer is uniform, then return the constant
  // regardless of its offset.
  if (Constant *C = ConstantFoldLoadFromUniformValue(GV->getInitializer(),
                                                     LI->getType(), Q.DL))
    return C;

  // Try to convert operand into a constant by stripping offsets while looking
  // through invariant.group intrinsics.
  APInt Offset(Q.DL.getIndexTypeSizeInBits(PtrOp->getType()), 0);
  PtrOp = PtrOp->stripAndAccumulateConstantOffsets(
      Q.DL, Offset, /* AllowNonInbounts */ true,
      /* AllowInvariantGroup */ true);
  if (PtrOp == GV) {
    // Index size may have changed due to address space casts.
    Offset = Offset.sextOrTrunc(Q.DL.getIndexTypeSizeInBits(PtrOp->getType()));
    return ConstantFoldLoadFromConstPtr(GV, LI->getType(), std::move(Offset),
                                        Q.DL);
  }

  return nullptr;
}

/// See if we can compute a simplified version of this instruction.
/// If not, this returns null.

static Value *simplifyInstructionWithOperands(Instruction *I,
                                              ArrayRef<Value *> NewOps,
                                              const SimplifyQuery &SQ,
                                              unsigned MaxRecurse) {
  assert(I->getFunction() && "instruction should be inserted in a function");
  assert((!SQ.CxtI || SQ.CxtI->getFunction() == I->getFunction()) &&
         "context instruction should be in the same function");

  const SimplifyQuery Q = SQ.CxtI ? SQ : SQ.getWithInstruction(I);

  switch (I->getOpcode()) {
  default:
    if (llvm::all_of(NewOps, [](Value *V) { return isa<Constant>(V); })) {
      SmallVector<Constant *, 8> NewConstOps(NewOps.size());
      transform(NewOps, NewConstOps.begin(),
                [](Value *V) { return cast<Constant>(V); });
      return ConstantFoldInstOperands(I, NewConstOps, Q.DL, Q.TLI);
    }
    return nullptr;
  case Instruction::FNeg:
    return simplifyFNegInst(NewOps[0], I->getFastMathFlags(), Q, MaxRecurse);
  case Instruction::FAdd:
    return simplifyFAddInst(NewOps[0], NewOps[1], I->getFastMathFlags(), Q,
                            MaxRecurse);
  case Instruction::Add:
    return simplifyAddInst(
        NewOps[0], NewOps[1], Q.IIQ.hasNoSignedWrap(cast<BinaryOperator>(I)),
        Q.IIQ.hasNoUnsignedWrap(cast<BinaryOperator>(I)), Q, MaxRecurse);
  case Instruction::FSub:
    return simplifyFSubInst(NewOps[0], NewOps[1], I->getFastMathFlags(), Q,
                            MaxRecurse);
  case Instruction::Sub:
    return simplifySubInst(
        NewOps[0], NewOps[1], Q.IIQ.hasNoSignedWrap(cast<BinaryOperator>(I)),
        Q.IIQ.hasNoUnsignedWrap(cast<BinaryOperator>(I)), Q, MaxRecurse);
  case Instruction::FMul:
    return simplifyFMulInst(NewOps[0], NewOps[1], I->getFastMathFlags(), Q,
                            MaxRecurse);
  case Instruction::Mul:
    return simplifyMulInst(
        NewOps[0], NewOps[1], Q.IIQ.hasNoSignedWrap(cast<BinaryOperator>(I)),
        Q.IIQ.hasNoUnsignedWrap(cast<BinaryOperator>(I)), Q, MaxRecurse);
  case Instruction::SDiv:
    return simplifySDivInst(NewOps[0], NewOps[1],
                            Q.IIQ.isExact(cast<BinaryOperator>(I)), Q,
                            MaxRecurse);
  case Instruction::UDiv:
    return simplifyUDivInst(NewOps[0], NewOps[1],
                            Q.IIQ.isExact(cast<BinaryOperator>(I)), Q,
                            MaxRecurse);
  case Instruction::FDiv:
    return simplifyFDivInst(NewOps[0], NewOps[1], I->getFastMathFlags(), Q,
                            MaxRecurse);
  case Instruction::SRem:
    return simplifySRemInst(NewOps[0], NewOps[1], Q, MaxRecurse);
  case Instruction::URem:
    return simplifyURemInst(NewOps[0], NewOps[1], Q, MaxRecurse);
  case Instruction::FRem:
    return simplifyFRemInst(NewOps[0], NewOps[1], I->getFastMathFlags(), Q,
                            MaxRecurse);
  case Instruction::Shl:
    return simplifyShlInst(
        NewOps[0], NewOps[1], Q.IIQ.hasNoSignedWrap(cast<BinaryOperator>(I)),
        Q.IIQ.hasNoUnsignedWrap(cast<BinaryOperator>(I)), Q, MaxRecurse);
  case Instruction::LShr:
    return simplifyLShrInst(NewOps[0], NewOps[1],
                            Q.IIQ.isExact(cast<BinaryOperator>(I)), Q,
                            MaxRecurse);
  case Instruction::AShr:
    return simplifyAShrInst(NewOps[0], NewOps[1],
                            Q.IIQ.isExact(cast<BinaryOperator>(I)), Q,
                            MaxRecurse);
  case Instruction::And:
    return simplifyAndInst(NewOps[0], NewOps[1], Q, MaxRecurse);
  case Instruction::Or:
    return simplifyOrInst(NewOps[0], NewOps[1], Q, MaxRecurse);
  case Instruction::Xor:
    return simplifyXorInst(NewOps[0], NewOps[1], Q, MaxRecurse);
  case Instruction::ICmp:
    return simplifyICmpInst(cast<ICmpInst>(I)->getPredicate(), NewOps[0],
                            NewOps[1], Q, MaxRecurse);
  case Instruction::FCmp:
    return simplifyFCmpInst(cast<FCmpInst>(I)->getPredicate(), NewOps[0],
                            NewOps[1], I->getFastMathFlags(), Q, MaxRecurse);
  case Instruction::Select:
    return simplifySelectInst(NewOps[0], NewOps[1], NewOps[2], Q, MaxRecurse);
    break;
  case Instruction::GetElementPtr: {
    auto *GEPI = cast<GetElementPtrInst>(I);
    return simplifyGEPInst(GEPI->getSourceElementType(), NewOps[0],
                           ArrayRef(NewOps).slice(1), GEPI->getNoWrapFlags(), Q,
                           MaxRecurse);
  }
  case Instruction::InsertValue: {
    InsertValueInst *IV = cast<InsertValueInst>(I);
    return simplifyInsertValueInst(NewOps[0], NewOps[1], IV->getIndices(), Q,
                                   MaxRecurse);
  }
  case Instruction::InsertElement:
    return simplifyInsertElementInst(NewOps[0], NewOps[1], NewOps[2], Q);
  case Instruction::ExtractValue: {
    auto *EVI = cast<ExtractValueInst>(I);
    return simplifyExtractValueInst(NewOps[0], EVI->getIndices(), Q,
                                    MaxRecurse);
  }
  case Instruction::ExtractElement:
    return simplifyExtractElementInst(NewOps[0], NewOps[1], Q, MaxRecurse);
  case Instruction::ShuffleVector: {
    auto *SVI = cast<ShuffleVectorInst>(I);
    return simplifyShuffleVectorInst(NewOps[0], NewOps[1],
                                     SVI->getShuffleMask(), SVI->getType(), Q,
                                     MaxRecurse);
  }
  case Instruction::PHI:
    return simplifyPHINode(cast<PHINode>(I), NewOps, Q);
  case Instruction::Call:
    return simplifyCall(
        cast<CallInst>(I), NewOps.back(),
        NewOps.drop_back(1 + cast<CallInst>(I)->getNumTotalBundleOperands()), Q);
  case Instruction::Freeze:
    return llvm::simplifyFreezeInst(NewOps[0], Q);
#define HANDLE_CAST_INST(num, opc, clas) case Instruction::opc:
#include "llvm/IR/Instruction.def"
#undef HANDLE_CAST_INST
    return simplifyCastInst(I->getOpcode(), NewOps[0], I->getType(), Q,
                            MaxRecurse);
  case Instruction::Alloca:
    // No simplifications for Alloca and it can't be constant folded.
    return nullptr;
  case Instruction::Load:
    return simplifyLoadInst(cast<LoadInst>(I), NewOps[0], Q);
  }
}

Value *llvm::simplifyInstructionWithOperands(Instruction *I,
                                             ArrayRef<Value *> NewOps,
                                             const SimplifyQuery &SQ) {
  assert(NewOps.size() == I->getNumOperands() &&
         "Number of operands should match the instruction!");
  return ::simplifyInstructionWithOperands(I, NewOps, SQ, RecursionLimit);
}

Value *llvm::simplifyInstruction(Instruction *I, const SimplifyQuery &SQ) {
  SmallVector<Value *, 8> Ops(I->operands());
  Value *Result = ::simplifyInstructionWithOperands(I, Ops, SQ, RecursionLimit);

  /// If called on unreachable code, the instruction may simplify to itself.
  /// Make life easier for users by detecting that case here, and returning a
  /// safe value instead.
  return Result == I ? PoisonValue::get(I->getType()) : Result;
}

/// Implementation of recursive simplification through an instruction's
/// uses.
///
/// This is the common implementation of the recursive simplification routines.
/// If we have a pre-simplified value in 'SimpleV', that is forcibly used to
/// replace the instruction 'I'. Otherwise, we simply add 'I' to the list of
/// instructions to process and attempt to simplify it using
/// InstructionSimplify. Recursively visited users which could not be
/// simplified themselves are to the optional UnsimplifiedUsers set for
/// further processing by the caller.
///
/// This routine returns 'true' only when *it* simplifies something. The passed
/// in simplified value does not count toward this.
static bool replaceAndRecursivelySimplifyImpl(
    Instruction *I, Value *SimpleV, const TargetLibraryInfo *TLI,
    const DominatorTree *DT, AssumptionCache *AC,
    SmallSetVector<Instruction *, 8> *UnsimplifiedUsers = nullptr) {
  bool Simplified = false;
  SmallSetVector<Instruction *, 8> Worklist;
  const DataLayout &DL = I->getDataLayout();

  // If we have an explicit value to collapse to, do that round of the
  // simplification loop by hand initially.
  if (SimpleV) {
    for (User *U : I->users())
      if (U != I)
        Worklist.insert(cast<Instruction>(U));

    // Replace the instruction with its simplified value.
    I->replaceAllUsesWith(SimpleV);

    if (!I->isEHPad() && !I->isTerminator() && !I->mayHaveSideEffects())
      I->eraseFromParent();
  } else {
    Worklist.insert(I);
  }

  // Note that we must test the size on each iteration, the worklist can grow.
  for (unsigned Idx = 0; Idx != Worklist.size(); ++Idx) {
    I = Worklist[Idx];

    // See if this instruction simplifies.
    SimpleV = simplifyInstruction(I, {DL, TLI, DT, AC});
    if (!SimpleV) {
      if (UnsimplifiedUsers)
        UnsimplifiedUsers->insert(I);
      continue;
    }

    Simplified = true;

    // Stash away all the uses of the old instruction so we can check them for
    // recursive simplifications after a RAUW. This is cheaper than checking all
    // uses of To on the recursive step in most cases.
    for (User *U : I->users())
      Worklist.insert(cast<Instruction>(U));

    // Replace the instruction with its simplified value.
    I->replaceAllUsesWith(SimpleV);

    if (!I->isEHPad() && !I->isTerminator() && !I->mayHaveSideEffects())
      I->eraseFromParent();
  }
  return Simplified;
}

bool llvm::replaceAndRecursivelySimplify(
    Instruction *I, Value *SimpleV, const TargetLibraryInfo *TLI,
    const DominatorTree *DT, AssumptionCache *AC,
    SmallSetVector<Instruction *, 8> *UnsimplifiedUsers) {
  assert(I != SimpleV && "replaceAndRecursivelySimplify(X,X) is not valid!");
  assert(SimpleV && "Must provide a simplified value.");
  return replaceAndRecursivelySimplifyImpl(I, SimpleV, TLI, DT, AC,
                                           UnsimplifiedUsers);
}

namespace llvm {
const SimplifyQuery getBestSimplifyQuery(Pass &P, Function &F) {
  auto *DTWP = P.getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  auto *DT = DTWP ? &DTWP->getDomTree() : nullptr;
  auto *TLIWP = P.getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>();
  auto *TLI = TLIWP ? &TLIWP->getTLI(F) : nullptr;
  auto *ACWP = P.getAnalysisIfAvailable<AssumptionCacheTracker>();
  auto *AC = ACWP ? &ACWP->getAssumptionCache(F) : nullptr;
  return {F.getDataLayout(), TLI, DT, AC};
}

const SimplifyQuery getBestSimplifyQuery(LoopStandardAnalysisResults &AR,
                                         const DataLayout &DL) {
  return {DL, &AR.TLI, &AR.DT, &AR.AC};
}

template <class T, class... TArgs>
const SimplifyQuery getBestSimplifyQuery(AnalysisManager<T, TArgs...> &AM,
                                         Function &F) {
  auto *DT = AM.template getCachedResult<DominatorTreeAnalysis>(F);
  auto *TLI = AM.template getCachedResult<TargetLibraryAnalysis>(F);
  auto *AC = AM.template getCachedResult<AssumptionAnalysis>(F);
  return {F.getDataLayout(), TLI, DT, AC};
}
template const SimplifyQuery getBestSimplifyQuery(AnalysisManager<Function> &,
                                                  Function &);

bool SimplifyQuery::isUndefValue(Value *V) const {
  if (!CanUseUndef)
    return false;

  return match(V, m_Undef());
}

} // namespace llvm

void InstSimplifyFolder::anchor() {}
