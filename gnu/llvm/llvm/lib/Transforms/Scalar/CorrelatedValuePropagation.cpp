//===- CorrelatedValuePropagation.cpp - Propagate CFG-derived info --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Correlated Value Propagation pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "correlated-value-propagation"

STATISTIC(NumPhis,      "Number of phis propagated");
STATISTIC(NumPhiCommon, "Number of phis deleted via common incoming value");
STATISTIC(NumSelects,   "Number of selects propagated");
STATISTIC(NumCmps,      "Number of comparisons propagated");
STATISTIC(NumReturns,   "Number of return values propagated");
STATISTIC(NumDeadCases, "Number of switch cases removed");
STATISTIC(NumSDivSRemsNarrowed,
          "Number of sdivs/srems whose width was decreased");
STATISTIC(NumSDivs,     "Number of sdiv converted to udiv");
STATISTIC(NumUDivURemsNarrowed,
          "Number of udivs/urems whose width was decreased");
STATISTIC(NumAShrsConverted, "Number of ashr converted to lshr");
STATISTIC(NumAShrsRemoved, "Number of ashr removed");
STATISTIC(NumSRems,     "Number of srem converted to urem");
STATISTIC(NumSExt,      "Number of sext converted to zext");
STATISTIC(NumSIToFP,    "Number of sitofp converted to uitofp");
STATISTIC(NumSICmps,    "Number of signed icmp preds simplified to unsigned");
STATISTIC(NumAnd,       "Number of ands removed");
STATISTIC(NumNW,        "Number of no-wrap deductions");
STATISTIC(NumNSW,       "Number of no-signed-wrap deductions");
STATISTIC(NumNUW,       "Number of no-unsigned-wrap deductions");
STATISTIC(NumAddNW,     "Number of no-wrap deductions for add");
STATISTIC(NumAddNSW,    "Number of no-signed-wrap deductions for add");
STATISTIC(NumAddNUW,    "Number of no-unsigned-wrap deductions for add");
STATISTIC(NumSubNW,     "Number of no-wrap deductions for sub");
STATISTIC(NumSubNSW,    "Number of no-signed-wrap deductions for sub");
STATISTIC(NumSubNUW,    "Number of no-unsigned-wrap deductions for sub");
STATISTIC(NumMulNW,     "Number of no-wrap deductions for mul");
STATISTIC(NumMulNSW,    "Number of no-signed-wrap deductions for mul");
STATISTIC(NumMulNUW,    "Number of no-unsigned-wrap deductions for mul");
STATISTIC(NumShlNW,     "Number of no-wrap deductions for shl");
STATISTIC(NumShlNSW,    "Number of no-signed-wrap deductions for shl");
STATISTIC(NumShlNUW,    "Number of no-unsigned-wrap deductions for shl");
STATISTIC(NumAbs,       "Number of llvm.abs intrinsics removed");
STATISTIC(NumOverflows, "Number of overflow checks removed");
STATISTIC(NumSaturating,
    "Number of saturating arithmetics converted to normal arithmetics");
STATISTIC(NumNonNull, "Number of function pointer arguments marked non-null");
STATISTIC(NumCmpIntr, "Number of llvm.[us]cmp intrinsics removed");
STATISTIC(NumMinMax, "Number of llvm.[us]{min,max} intrinsics removed");
STATISTIC(NumSMinMax,
          "Number of llvm.s{min,max} intrinsics simplified to unsigned");
STATISTIC(NumUDivURemsNarrowedExpanded,
          "Number of bound udiv's/urem's expanded");
STATISTIC(NumNNeg, "Number of zext/uitofp non-negative deductions");

static Constant *getConstantAt(Value *V, Instruction *At, LazyValueInfo *LVI) {
  if (Constant *C = LVI->getConstant(V, At))
    return C;

  // TODO: The following really should be sunk inside LVI's core algorithm, or
  // at least the outer shims around such.
  auto *C = dyn_cast<CmpInst>(V);
  if (!C)
    return nullptr;

  Value *Op0 = C->getOperand(0);
  Constant *Op1 = dyn_cast<Constant>(C->getOperand(1));
  if (!Op1)
    return nullptr;

  return LVI->getPredicateAt(C->getPredicate(), Op0, Op1, At,
                             /*UseBlockValue=*/false);
}

static bool processSelect(SelectInst *S, LazyValueInfo *LVI) {
  if (S->getType()->isVectorTy() || isa<Constant>(S->getCondition()))
    return false;

  bool Changed = false;
  for (Use &U : make_early_inc_range(S->uses())) {
    auto *I = cast<Instruction>(U.getUser());
    Constant *C;
    if (auto *PN = dyn_cast<PHINode>(I))
      C = LVI->getConstantOnEdge(S->getCondition(), PN->getIncomingBlock(U),
                                 I->getParent(), I);
    else
      C = getConstantAt(S->getCondition(), I, LVI);

    auto *CI = dyn_cast_or_null<ConstantInt>(C);
    if (!CI)
      continue;

    U.set(CI->isOne() ? S->getTrueValue() : S->getFalseValue());
    Changed = true;
    ++NumSelects;
  }

  if (Changed && S->use_empty())
    S->eraseFromParent();

  return Changed;
}

/// Try to simplify a phi with constant incoming values that match the edge
/// values of a non-constant value on all other edges:
/// bb0:
///   %isnull = icmp eq i8* %x, null
///   br i1 %isnull, label %bb2, label %bb1
/// bb1:
///   br label %bb2
/// bb2:
///   %r = phi i8* [ %x, %bb1 ], [ null, %bb0 ]
/// -->
///   %r = %x
static bool simplifyCommonValuePhi(PHINode *P, LazyValueInfo *LVI,
                                   DominatorTree *DT) {
  // Collect incoming constants and initialize possible common value.
  SmallVector<std::pair<Constant *, unsigned>, 4> IncomingConstants;
  Value *CommonValue = nullptr;
  for (unsigned i = 0, e = P->getNumIncomingValues(); i != e; ++i) {
    Value *Incoming = P->getIncomingValue(i);
    if (auto *IncomingConstant = dyn_cast<Constant>(Incoming)) {
      IncomingConstants.push_back(std::make_pair(IncomingConstant, i));
    } else if (!CommonValue) {
      // The potential common value is initialized to the first non-constant.
      CommonValue = Incoming;
    } else if (Incoming != CommonValue) {
      // There can be only one non-constant common value.
      return false;
    }
  }

  if (!CommonValue || IncomingConstants.empty())
    return false;

  // The common value must be valid in all incoming blocks.
  BasicBlock *ToBB = P->getParent();
  if (auto *CommonInst = dyn_cast<Instruction>(CommonValue))
    if (!DT->dominates(CommonInst, ToBB))
      return false;

  // We have a phi with exactly 1 variable incoming value and 1 or more constant
  // incoming values. See if all constant incoming values can be mapped back to
  // the same incoming variable value.
  for (auto &IncomingConstant : IncomingConstants) {
    Constant *C = IncomingConstant.first;
    BasicBlock *IncomingBB = P->getIncomingBlock(IncomingConstant.second);
    if (C != LVI->getConstantOnEdge(CommonValue, IncomingBB, ToBB, P))
      return false;
  }

  // LVI only guarantees that the value matches a certain constant if the value
  // is not poison. Make sure we don't replace a well-defined value with poison.
  // This is usually satisfied due to a prior branch on the value.
  if (!isGuaranteedNotToBePoison(CommonValue, nullptr, P, DT))
    return false;

  // All constant incoming values map to the same variable along the incoming
  // edges of the phi. The phi is unnecessary.
  P->replaceAllUsesWith(CommonValue);
  P->eraseFromParent();
  ++NumPhiCommon;
  return true;
}

static Value *getValueOnEdge(LazyValueInfo *LVI, Value *Incoming,
                             BasicBlock *From, BasicBlock *To,
                             Instruction *CxtI) {
  if (Constant *C = LVI->getConstantOnEdge(Incoming, From, To, CxtI))
    return C;

  // Look if the incoming value is a select with a scalar condition for which
  // LVI can tells us the value. In that case replace the incoming value with
  // the appropriate value of the select. This often allows us to remove the
  // select later.
  auto *SI = dyn_cast<SelectInst>(Incoming);
  if (!SI)
    return nullptr;

  // Once LVI learns to handle vector types, we could also add support
  // for vector type constants that are not all zeroes or all ones.
  Value *Condition = SI->getCondition();
  if (!Condition->getType()->isVectorTy()) {
    if (Constant *C = LVI->getConstantOnEdge(Condition, From, To, CxtI)) {
      if (C->isOneValue())
        return SI->getTrueValue();
      if (C->isZeroValue())
        return SI->getFalseValue();
    }
  }

  // Look if the select has a constant but LVI tells us that the incoming
  // value can never be that constant. In that case replace the incoming
  // value with the other value of the select. This often allows us to
  // remove the select later.

  // The "false" case
  if (auto *C = dyn_cast<Constant>(SI->getFalseValue()))
    if (auto *Res = dyn_cast_or_null<ConstantInt>(
            LVI->getPredicateOnEdge(ICmpInst::ICMP_EQ, SI, C, From, To, CxtI));
        Res && Res->isZero())
      return SI->getTrueValue();

  // The "true" case,
  // similar to the select "false" case, but try the select "true" value
  if (auto *C = dyn_cast<Constant>(SI->getTrueValue()))
    if (auto *Res = dyn_cast_or_null<ConstantInt>(
            LVI->getPredicateOnEdge(ICmpInst::ICMP_EQ, SI, C, From, To, CxtI));
        Res && Res->isZero())
      return SI->getFalseValue();

  return nullptr;
}

static bool processPHI(PHINode *P, LazyValueInfo *LVI, DominatorTree *DT,
                       const SimplifyQuery &SQ) {
  bool Changed = false;

  BasicBlock *BB = P->getParent();
  for (unsigned i = 0, e = P->getNumIncomingValues(); i < e; ++i) {
    Value *Incoming = P->getIncomingValue(i);
    if (isa<Constant>(Incoming)) continue;

    Value *V = getValueOnEdge(LVI, Incoming, P->getIncomingBlock(i), BB, P);
    if (V) {
      P->setIncomingValue(i, V);
      Changed = true;
    }
  }

  if (Value *V = simplifyInstruction(P, SQ)) {
    P->replaceAllUsesWith(V);
    P->eraseFromParent();
    Changed = true;
  }

  if (!Changed)
    Changed = simplifyCommonValuePhi(P, LVI, DT);

  if (Changed)
    ++NumPhis;

  return Changed;
}

static bool processICmp(ICmpInst *Cmp, LazyValueInfo *LVI) {
  // Only for signed relational comparisons of integers.
  if (!Cmp->getOperand(0)->getType()->isIntOrIntVectorTy())
    return false;

  if (!Cmp->isSigned())
    return false;

  ICmpInst::Predicate UnsignedPred =
      ConstantRange::getEquivalentPredWithFlippedSignedness(
          Cmp->getPredicate(),
          LVI->getConstantRangeAtUse(Cmp->getOperandUse(0),
                                     /*UndefAllowed*/ true),
          LVI->getConstantRangeAtUse(Cmp->getOperandUse(1),
                                     /*UndefAllowed*/ true));

  if (UnsignedPred == ICmpInst::Predicate::BAD_ICMP_PREDICATE)
    return false;

  ++NumSICmps;
  Cmp->setPredicate(UnsignedPred);

  return true;
}

/// See if LazyValueInfo's ability to exploit edge conditions or range
/// information is sufficient to prove this comparison. Even for local
/// conditions, this can sometimes prove conditions instcombine can't by
/// exploiting range information.
static bool constantFoldCmp(CmpInst *Cmp, LazyValueInfo *LVI) {
  Value *Op0 = Cmp->getOperand(0);
  Value *Op1 = Cmp->getOperand(1);
  Constant *Res = LVI->getPredicateAt(Cmp->getPredicate(), Op0, Op1, Cmp,
                                      /*UseBlockValue=*/true);
  if (!Res)
    return false;

  ++NumCmps;
  Cmp->replaceAllUsesWith(Res);
  Cmp->eraseFromParent();
  return true;
}

static bool processCmp(CmpInst *Cmp, LazyValueInfo *LVI) {
  if (constantFoldCmp(Cmp, LVI))
    return true;

  if (auto *ICmp = dyn_cast<ICmpInst>(Cmp))
    if (processICmp(ICmp, LVI))
      return true;

  return false;
}

/// Simplify a switch instruction by removing cases which can never fire. If the
/// uselessness of a case could be determined locally then constant propagation
/// would already have figured it out. Instead, walk the predecessors and
/// statically evaluate cases based on information available on that edge. Cases
/// that cannot fire no matter what the incoming edge can safely be removed. If
/// a case fires on every incoming edge then the entire switch can be removed
/// and replaced with a branch to the case destination.
static bool processSwitch(SwitchInst *I, LazyValueInfo *LVI,
                          DominatorTree *DT) {
  DomTreeUpdater DTU(*DT, DomTreeUpdater::UpdateStrategy::Lazy);
  Value *Cond = I->getCondition();
  BasicBlock *BB = I->getParent();

  // Analyse each switch case in turn.
  bool Changed = false;
  DenseMap<BasicBlock*, int> SuccessorsCount;
  for (auto *Succ : successors(BB))
    SuccessorsCount[Succ]++;

  { // Scope for SwitchInstProfUpdateWrapper. It must not live during
    // ConstantFoldTerminator() as the underlying SwitchInst can be changed.
    SwitchInstProfUpdateWrapper SI(*I);
    unsigned ReachableCaseCount = 0;

    for (auto CI = SI->case_begin(), CE = SI->case_end(); CI != CE;) {
      ConstantInt *Case = CI->getCaseValue();
      auto *Res = dyn_cast_or_null<ConstantInt>(
          LVI->getPredicateAt(CmpInst::ICMP_EQ, Cond, Case, I,
                              /* UseBlockValue */ true));

      if (Res && Res->isZero()) {
        // This case never fires - remove it.
        BasicBlock *Succ = CI->getCaseSuccessor();
        Succ->removePredecessor(BB);
        CI = SI.removeCase(CI);
        CE = SI->case_end();

        // The condition can be modified by removePredecessor's PHI simplification
        // logic.
        Cond = SI->getCondition();

        ++NumDeadCases;
        Changed = true;
        if (--SuccessorsCount[Succ] == 0)
          DTU.applyUpdatesPermissive({{DominatorTree::Delete, BB, Succ}});
        continue;
      }
      if (Res && Res->isOne()) {
        // This case always fires.  Arrange for the switch to be turned into an
        // unconditional branch by replacing the switch condition with the case
        // value.
        SI->setCondition(Case);
        NumDeadCases += SI->getNumCases();
        Changed = true;
        break;
      }

      // Increment the case iterator since we didn't delete it.
      ++CI;
      ++ReachableCaseCount;
    }

    BasicBlock *DefaultDest = SI->getDefaultDest();
    if (ReachableCaseCount > 1 &&
        !isa<UnreachableInst>(DefaultDest->getFirstNonPHIOrDbg())) {
      ConstantRange CR = LVI->getConstantRangeAtUse(I->getOperandUse(0),
                                                    /*UndefAllowed*/ false);
      // The default dest is unreachable if all cases are covered.
      if (!CR.isSizeLargerThan(ReachableCaseCount)) {
        BasicBlock *NewUnreachableBB =
            BasicBlock::Create(BB->getContext(), "default.unreachable",
                               BB->getParent(), DefaultDest);
        new UnreachableInst(BB->getContext(), NewUnreachableBB);

        DefaultDest->removePredecessor(BB);
        SI->setDefaultDest(NewUnreachableBB);

        if (SuccessorsCount[DefaultDest] == 1)
          DTU.applyUpdates({{DominatorTree::Delete, BB, DefaultDest}});
        DTU.applyUpdates({{DominatorTree::Insert, BB, NewUnreachableBB}});

        ++NumDeadCases;
        Changed = true;
      }
    }
  }

  if (Changed)
    // If the switch has been simplified to the point where it can be replaced
    // by a branch then do so now.
    ConstantFoldTerminator(BB, /*DeleteDeadConditions = */ false,
                           /*TLI = */ nullptr, &DTU);
  return Changed;
}

// See if we can prove that the given binary op intrinsic will not overflow.
static bool willNotOverflow(BinaryOpIntrinsic *BO, LazyValueInfo *LVI) {
  ConstantRange LRange =
      LVI->getConstantRangeAtUse(BO->getOperandUse(0), /*UndefAllowed*/ false);
  ConstantRange RRange =
      LVI->getConstantRangeAtUse(BO->getOperandUse(1), /*UndefAllowed*/ false);
  ConstantRange NWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
      BO->getBinaryOp(), RRange, BO->getNoWrapKind());
  return NWRegion.contains(LRange);
}

static void setDeducedOverflowingFlags(Value *V, Instruction::BinaryOps Opcode,
                                       bool NewNSW, bool NewNUW) {
  Statistic *OpcNW, *OpcNSW, *OpcNUW;
  switch (Opcode) {
  case Instruction::Add:
    OpcNW = &NumAddNW;
    OpcNSW = &NumAddNSW;
    OpcNUW = &NumAddNUW;
    break;
  case Instruction::Sub:
    OpcNW = &NumSubNW;
    OpcNSW = &NumSubNSW;
    OpcNUW = &NumSubNUW;
    break;
  case Instruction::Mul:
    OpcNW = &NumMulNW;
    OpcNSW = &NumMulNSW;
    OpcNUW = &NumMulNUW;
    break;
  case Instruction::Shl:
    OpcNW = &NumShlNW;
    OpcNSW = &NumShlNSW;
    OpcNUW = &NumShlNUW;
    break;
  default:
    llvm_unreachable("Will not be called with other binops");
  }

  auto *Inst = dyn_cast<Instruction>(V);
  if (NewNSW) {
    ++NumNW;
    ++*OpcNW;
    ++NumNSW;
    ++*OpcNSW;
    if (Inst)
      Inst->setHasNoSignedWrap();
  }
  if (NewNUW) {
    ++NumNW;
    ++*OpcNW;
    ++NumNUW;
    ++*OpcNUW;
    if (Inst)
      Inst->setHasNoUnsignedWrap();
  }
}

static bool processBinOp(BinaryOperator *BinOp, LazyValueInfo *LVI);

// See if @llvm.abs argument is alays positive/negative, and simplify.
// Notably, INT_MIN can belong to either range, regardless of the NSW,
// because it is negation-invariant.
static bool processAbsIntrinsic(IntrinsicInst *II, LazyValueInfo *LVI) {
  Value *X = II->getArgOperand(0);
  bool IsIntMinPoison = cast<ConstantInt>(II->getArgOperand(1))->isOne();
  APInt IntMin = APInt::getSignedMinValue(X->getType()->getScalarSizeInBits());
  ConstantRange Range = LVI->getConstantRangeAtUse(
      II->getOperandUse(0), /*UndefAllowed*/ IsIntMinPoison);

  // Is X in [0, IntMin]?  NOTE: INT_MIN is fine!
  if (Range.icmp(CmpInst::ICMP_ULE, IntMin)) {
    ++NumAbs;
    II->replaceAllUsesWith(X);
    II->eraseFromParent();
    return true;
  }

  // Is X in [IntMin, 0]?  NOTE: INT_MIN is fine!
  if (Range.getSignedMax().isNonPositive()) {
    IRBuilder<> B(II);
    Value *NegX = B.CreateNeg(X, II->getName(),
                              /*HasNSW=*/IsIntMinPoison);
    ++NumAbs;
    II->replaceAllUsesWith(NegX);
    II->eraseFromParent();

    // See if we can infer some no-wrap flags.
    if (auto *BO = dyn_cast<BinaryOperator>(NegX))
      processBinOp(BO, LVI);

    return true;
  }

  // Argument's range crosses zero.
  // Can we at least tell that the argument is never INT_MIN?
  if (!IsIntMinPoison && !Range.contains(IntMin)) {
    ++NumNSW;
    ++NumSubNSW;
    II->setArgOperand(1, ConstantInt::getTrue(II->getContext()));
    return true;
  }
  return false;
}

static bool processCmpIntrinsic(CmpIntrinsic *CI, LazyValueInfo *LVI) {
  ConstantRange LHS_CR =
      LVI->getConstantRangeAtUse(CI->getOperandUse(0), /*UndefAllowed*/ false);
  ConstantRange RHS_CR =
      LVI->getConstantRangeAtUse(CI->getOperandUse(1), /*UndefAllowed*/ false);

  if (LHS_CR.icmp(CI->getGTPredicate(), RHS_CR)) {
    ++NumCmpIntr;
    CI->replaceAllUsesWith(ConstantInt::get(CI->getType(), 1));
    CI->eraseFromParent();
    return true;
  }
  if (LHS_CR.icmp(CI->getLTPredicate(), RHS_CR)) {
    ++NumCmpIntr;
    CI->replaceAllUsesWith(ConstantInt::getSigned(CI->getType(), -1));
    CI->eraseFromParent();
    return true;
  }
  if (LHS_CR.icmp(ICmpInst::ICMP_EQ, RHS_CR)) {
    ++NumCmpIntr;
    CI->replaceAllUsesWith(ConstantInt::get(CI->getType(), 0));
    CI->eraseFromParent();
    return true;
  }

  return false;
}

// See if this min/max intrinsic always picks it's one specific operand.
// If not, check whether we can canonicalize signed minmax into unsigned version
static bool processMinMaxIntrinsic(MinMaxIntrinsic *MM, LazyValueInfo *LVI) {
  CmpInst::Predicate Pred = CmpInst::getNonStrictPredicate(MM->getPredicate());
  ConstantRange LHS_CR = LVI->getConstantRangeAtUse(MM->getOperandUse(0),
                                                    /*UndefAllowed*/ false);
  ConstantRange RHS_CR = LVI->getConstantRangeAtUse(MM->getOperandUse(1),
                                                    /*UndefAllowed*/ false);
  if (LHS_CR.icmp(Pred, RHS_CR)) {
    ++NumMinMax;
    MM->replaceAllUsesWith(MM->getLHS());
    MM->eraseFromParent();
    return true;
  }
  if (RHS_CR.icmp(Pred, LHS_CR)) {
    ++NumMinMax;
    MM->replaceAllUsesWith(MM->getRHS());
    MM->eraseFromParent();
    return true;
  }

  if (MM->isSigned() &&
      ConstantRange::areInsensitiveToSignednessOfICmpPredicate(LHS_CR,
                                                               RHS_CR)) {
    ++NumSMinMax;
    IRBuilder<> B(MM);
    MM->replaceAllUsesWith(B.CreateBinaryIntrinsic(
        MM->getIntrinsicID() == Intrinsic::smin ? Intrinsic::umin
                                                : Intrinsic::umax,
        MM->getLHS(), MM->getRHS()));
    MM->eraseFromParent();
    return true;
  }

  return false;
}

// Rewrite this with.overflow intrinsic as non-overflowing.
static bool processOverflowIntrinsic(WithOverflowInst *WO, LazyValueInfo *LVI) {
  IRBuilder<> B(WO);
  Instruction::BinaryOps Opcode = WO->getBinaryOp();
  bool NSW = WO->isSigned();
  bool NUW = !WO->isSigned();

  Value *NewOp =
      B.CreateBinOp(Opcode, WO->getLHS(), WO->getRHS(), WO->getName());
  setDeducedOverflowingFlags(NewOp, Opcode, NSW, NUW);

  StructType *ST = cast<StructType>(WO->getType());
  Constant *Struct = ConstantStruct::get(ST,
      { PoisonValue::get(ST->getElementType(0)),
        ConstantInt::getFalse(ST->getElementType(1)) });
  Value *NewI = B.CreateInsertValue(Struct, NewOp, 0);
  WO->replaceAllUsesWith(NewI);
  WO->eraseFromParent();
  ++NumOverflows;

  // See if we can infer the other no-wrap too.
  if (auto *BO = dyn_cast<BinaryOperator>(NewOp))
    processBinOp(BO, LVI);

  return true;
}

static bool processSaturatingInst(SaturatingInst *SI, LazyValueInfo *LVI) {
  Instruction::BinaryOps Opcode = SI->getBinaryOp();
  bool NSW = SI->isSigned();
  bool NUW = !SI->isSigned();
  BinaryOperator *BinOp = BinaryOperator::Create(
      Opcode, SI->getLHS(), SI->getRHS(), SI->getName(), SI->getIterator());
  BinOp->setDebugLoc(SI->getDebugLoc());
  setDeducedOverflowingFlags(BinOp, Opcode, NSW, NUW);

  SI->replaceAllUsesWith(BinOp);
  SI->eraseFromParent();
  ++NumSaturating;

  // See if we can infer the other no-wrap too.
  if (auto *BO = dyn_cast<BinaryOperator>(BinOp))
    processBinOp(BO, LVI);

  return true;
}

/// Infer nonnull attributes for the arguments at the specified callsite.
static bool processCallSite(CallBase &CB, LazyValueInfo *LVI) {

  if (CB.getIntrinsicID() == Intrinsic::abs) {
    return processAbsIntrinsic(&cast<IntrinsicInst>(CB), LVI);
  }

  if (auto *CI = dyn_cast<CmpIntrinsic>(&CB)) {
    return processCmpIntrinsic(CI, LVI);
  }

  if (auto *MM = dyn_cast<MinMaxIntrinsic>(&CB)) {
    return processMinMaxIntrinsic(MM, LVI);
  }

  if (auto *WO = dyn_cast<WithOverflowInst>(&CB)) {
    if (willNotOverflow(WO, LVI))
      return processOverflowIntrinsic(WO, LVI);
  }

  if (auto *SI = dyn_cast<SaturatingInst>(&CB)) {
    if (willNotOverflow(SI, LVI))
      return processSaturatingInst(SI, LVI);
  }

  bool Changed = false;

  // Deopt bundle operands are intended to capture state with minimal
  // perturbance of the code otherwise.  If we can find a constant value for
  // any such operand and remove a use of the original value, that's
  // desireable since it may allow further optimization of that value (e.g. via
  // single use rules in instcombine).  Since deopt uses tend to,
  // idiomatically, appear along rare conditional paths, it's reasonable likely
  // we may have a conditional fact with which LVI can fold.
  if (auto DeoptBundle = CB.getOperandBundle(LLVMContext::OB_deopt)) {
    for (const Use &ConstU : DeoptBundle->Inputs) {
      Use &U = const_cast<Use&>(ConstU);
      Value *V = U.get();
      if (V->getType()->isVectorTy()) continue;
      if (isa<Constant>(V)) continue;

      Constant *C = LVI->getConstant(V, &CB);
      if (!C) continue;
      U.set(C);
      Changed = true;
    }
  }

  SmallVector<unsigned, 4> ArgNos;
  unsigned ArgNo = 0;

  for (Value *V : CB.args()) {
    PointerType *Type = dyn_cast<PointerType>(V->getType());
    // Try to mark pointer typed parameters as non-null.  We skip the
    // relatively expensive analysis for constants which are obviously either
    // null or non-null to start with.
    if (Type && !CB.paramHasAttr(ArgNo, Attribute::NonNull) &&
        !isa<Constant>(V))
      if (auto *Res = dyn_cast_or_null<ConstantInt>(LVI->getPredicateAt(
              ICmpInst::ICMP_EQ, V, ConstantPointerNull::get(Type), &CB,
              /*UseBlockValue=*/false));
          Res && Res->isZero())
        ArgNos.push_back(ArgNo);
    ArgNo++;
  }

  assert(ArgNo == CB.arg_size() && "Call arguments not processed correctly.");

  if (ArgNos.empty())
    return Changed;

  NumNonNull += ArgNos.size();
  AttributeList AS = CB.getAttributes();
  LLVMContext &Ctx = CB.getContext();
  AS = AS.addParamAttribute(Ctx, ArgNos,
                            Attribute::get(Ctx, Attribute::NonNull));
  CB.setAttributes(AS);

  return true;
}

enum class Domain { NonNegative, NonPositive, Unknown };

static Domain getDomain(const ConstantRange &CR) {
  if (CR.isAllNonNegative())
    return Domain::NonNegative;
  if (CR.icmp(ICmpInst::ICMP_SLE, APInt::getZero(CR.getBitWidth())))
    return Domain::NonPositive;
  return Domain::Unknown;
}

/// Try to shrink a sdiv/srem's width down to the smallest power of two that's
/// sufficient to contain its operands.
static bool narrowSDivOrSRem(BinaryOperator *Instr, const ConstantRange &LCR,
                             const ConstantRange &RCR) {
  assert(Instr->getOpcode() == Instruction::SDiv ||
         Instr->getOpcode() == Instruction::SRem);

  // Find the smallest power of two bitwidth that's sufficient to hold Instr's
  // operands.
  unsigned OrigWidth = Instr->getType()->getScalarSizeInBits();

  // What is the smallest bit width that can accommodate the entire value ranges
  // of both of the operands?
  unsigned MinSignedBits =
      std::max(LCR.getMinSignedBits(), RCR.getMinSignedBits());

  // sdiv/srem is UB if divisor is -1 and divident is INT_MIN, so unless we can
  // prove that such a combination is impossible, we need to bump the bitwidth.
  if (RCR.contains(APInt::getAllOnes(OrigWidth)) &&
      LCR.contains(APInt::getSignedMinValue(MinSignedBits).sext(OrigWidth)))
    ++MinSignedBits;

  // Don't shrink below 8 bits wide.
  unsigned NewWidth = std::max<unsigned>(PowerOf2Ceil(MinSignedBits), 8);

  // NewWidth might be greater than OrigWidth if OrigWidth is not a power of
  // two.
  if (NewWidth >= OrigWidth)
    return false;

  ++NumSDivSRemsNarrowed;
  IRBuilder<> B{Instr};
  auto *TruncTy = Instr->getType()->getWithNewBitWidth(NewWidth);
  auto *LHS = B.CreateTruncOrBitCast(Instr->getOperand(0), TruncTy,
                                     Instr->getName() + ".lhs.trunc");
  auto *RHS = B.CreateTruncOrBitCast(Instr->getOperand(1), TruncTy,
                                     Instr->getName() + ".rhs.trunc");
  auto *BO = B.CreateBinOp(Instr->getOpcode(), LHS, RHS, Instr->getName());
  auto *Sext = B.CreateSExt(BO, Instr->getType(), Instr->getName() + ".sext");
  if (auto *BinOp = dyn_cast<BinaryOperator>(BO))
    if (BinOp->getOpcode() == Instruction::SDiv)
      BinOp->setIsExact(Instr->isExact());

  Instr->replaceAllUsesWith(Sext);
  Instr->eraseFromParent();
  return true;
}

static bool expandUDivOrURem(BinaryOperator *Instr, const ConstantRange &XCR,
                             const ConstantRange &YCR) {
  Type *Ty = Instr->getType();
  assert(Instr->getOpcode() == Instruction::UDiv ||
         Instr->getOpcode() == Instruction::URem);
  bool IsRem = Instr->getOpcode() == Instruction::URem;

  Value *X = Instr->getOperand(0);
  Value *Y = Instr->getOperand(1);

  // X u/ Y -> 0  iff X u< Y
  // X u% Y -> X  iff X u< Y
  if (XCR.icmp(ICmpInst::ICMP_ULT, YCR)) {
    Instr->replaceAllUsesWith(IsRem ? X : Constant::getNullValue(Ty));
    Instr->eraseFromParent();
    ++NumUDivURemsNarrowedExpanded;
    return true;
  }

  // Given
  //   R  = X u% Y
  // We can represent the modulo operation as a loop/self-recursion:
  //   urem_rec(X, Y):
  //     Z = X - Y
  //     if X u< Y
  //       ret X
  //     else
  //       ret urem_rec(Z, Y)
  // which isn't better, but if we only need a single iteration
  // to compute the answer, this becomes quite good:
  //   R  = X < Y ? X : X - Y    iff X u< 2*Y (w/ unsigned saturation)
  // Now, we do not care about all full multiples of Y in X, they do not change
  // the answer, thus we could rewrite the expression as:
  //   X* = X - (Y * |_ X / Y _|)
  //   R  = X* % Y
  // so we don't need the *first* iteration to return, we just need to
  // know *which* iteration will always return, so we could also rewrite it as:
  //   X* = X - (Y * |_ X / Y _|)
  //   R  = X* % Y                 iff X* u< 2*Y (w/ unsigned saturation)
  // but that does not seem profitable here.

  // Even if we don't know X's range, the divisor may be so large, X can't ever
  // be 2x larger than that. I.e. if divisor is always negative.
  if (!XCR.icmp(ICmpInst::ICMP_ULT,
                YCR.umul_sat(APInt(YCR.getBitWidth(), 2))) &&
      !YCR.isAllNegative())
    return false;

  IRBuilder<> B(Instr);
  Value *ExpandedOp;
  if (XCR.icmp(ICmpInst::ICMP_UGE, YCR)) {
    // If X is between Y and 2*Y the result is known.
    if (IsRem)
      ExpandedOp = B.CreateNUWSub(X, Y);
    else
      ExpandedOp = ConstantInt::get(Instr->getType(), 1);
  } else if (IsRem) {
    // NOTE: this transformation introduces two uses of X,
    //       but it may be undef so we must freeze it first.
    Value *FrozenX = X;
    if (!isGuaranteedNotToBeUndef(X))
      FrozenX = B.CreateFreeze(X, X->getName() + ".frozen");
    Value *FrozenY = Y;
    if (!isGuaranteedNotToBeUndef(Y))
      FrozenY = B.CreateFreeze(Y, Y->getName() + ".frozen");
    auto *AdjX = B.CreateNUWSub(FrozenX, FrozenY, Instr->getName() + ".urem");
    auto *Cmp = B.CreateICmp(ICmpInst::ICMP_ULT, FrozenX, FrozenY,
                             Instr->getName() + ".cmp");
    ExpandedOp = B.CreateSelect(Cmp, FrozenX, AdjX);
  } else {
    auto *Cmp =
        B.CreateICmp(ICmpInst::ICMP_UGE, X, Y, Instr->getName() + ".cmp");
    ExpandedOp = B.CreateZExt(Cmp, Ty, Instr->getName() + ".udiv");
  }
  ExpandedOp->takeName(Instr);
  Instr->replaceAllUsesWith(ExpandedOp);
  Instr->eraseFromParent();
  ++NumUDivURemsNarrowedExpanded;
  return true;
}

/// Try to shrink a udiv/urem's width down to the smallest power of two that's
/// sufficient to contain its operands.
static bool narrowUDivOrURem(BinaryOperator *Instr, const ConstantRange &XCR,
                             const ConstantRange &YCR) {
  assert(Instr->getOpcode() == Instruction::UDiv ||
         Instr->getOpcode() == Instruction::URem);

  // Find the smallest power of two bitwidth that's sufficient to hold Instr's
  // operands.

  // What is the smallest bit width that can accommodate the entire value ranges
  // of both of the operands?
  unsigned MaxActiveBits = std::max(XCR.getActiveBits(), YCR.getActiveBits());
  // Don't shrink below 8 bits wide.
  unsigned NewWidth = std::max<unsigned>(PowerOf2Ceil(MaxActiveBits), 8);

  // NewWidth might be greater than OrigWidth if OrigWidth is not a power of
  // two.
  if (NewWidth >= Instr->getType()->getScalarSizeInBits())
    return false;

  ++NumUDivURemsNarrowed;
  IRBuilder<> B{Instr};
  auto *TruncTy = Instr->getType()->getWithNewBitWidth(NewWidth);
  auto *LHS = B.CreateTruncOrBitCast(Instr->getOperand(0), TruncTy,
                                     Instr->getName() + ".lhs.trunc");
  auto *RHS = B.CreateTruncOrBitCast(Instr->getOperand(1), TruncTy,
                                     Instr->getName() + ".rhs.trunc");
  auto *BO = B.CreateBinOp(Instr->getOpcode(), LHS, RHS, Instr->getName());
  auto *Zext = B.CreateZExt(BO, Instr->getType(), Instr->getName() + ".zext");
  if (auto *BinOp = dyn_cast<BinaryOperator>(BO))
    if (BinOp->getOpcode() == Instruction::UDiv)
      BinOp->setIsExact(Instr->isExact());

  Instr->replaceAllUsesWith(Zext);
  Instr->eraseFromParent();
  return true;
}

static bool processUDivOrURem(BinaryOperator *Instr, LazyValueInfo *LVI) {
  assert(Instr->getOpcode() == Instruction::UDiv ||
         Instr->getOpcode() == Instruction::URem);
  ConstantRange XCR = LVI->getConstantRangeAtUse(Instr->getOperandUse(0),
                                                 /*UndefAllowed*/ false);
  // Allow undef for RHS, as we can assume it is division by zero UB.
  ConstantRange YCR = LVI->getConstantRangeAtUse(Instr->getOperandUse(1),
                                                 /*UndefAllowed*/ true);
  if (expandUDivOrURem(Instr, XCR, YCR))
    return true;

  return narrowUDivOrURem(Instr, XCR, YCR);
}

static bool processSRem(BinaryOperator *SDI, const ConstantRange &LCR,
                        const ConstantRange &RCR, LazyValueInfo *LVI) {
  assert(SDI->getOpcode() == Instruction::SRem);

  if (LCR.abs().icmp(CmpInst::ICMP_ULT, RCR.abs())) {
    SDI->replaceAllUsesWith(SDI->getOperand(0));
    SDI->eraseFromParent();
    return true;
  }

  struct Operand {
    Value *V;
    Domain D;
  };
  std::array<Operand, 2> Ops = {{{SDI->getOperand(0), getDomain(LCR)},
                                 {SDI->getOperand(1), getDomain(RCR)}}};
  if (Ops[0].D == Domain::Unknown || Ops[1].D == Domain::Unknown)
    return false;

  // We know domains of both of the operands!
  ++NumSRems;

  // We need operands to be non-negative, so negate each one that isn't.
  for (Operand &Op : Ops) {
    if (Op.D == Domain::NonNegative)
      continue;
    auto *BO = BinaryOperator::CreateNeg(Op.V, Op.V->getName() + ".nonneg",
                                         SDI->getIterator());
    BO->setDebugLoc(SDI->getDebugLoc());
    Op.V = BO;
  }

  auto *URem = BinaryOperator::CreateURem(Ops[0].V, Ops[1].V, SDI->getName(),
                                          SDI->getIterator());
  URem->setDebugLoc(SDI->getDebugLoc());

  auto *Res = URem;

  // If the divident was non-positive, we need to negate the result.
  if (Ops[0].D == Domain::NonPositive) {
    Res = BinaryOperator::CreateNeg(Res, Res->getName() + ".neg",
                                    SDI->getIterator());
    Res->setDebugLoc(SDI->getDebugLoc());
  }

  SDI->replaceAllUsesWith(Res);
  SDI->eraseFromParent();

  // Try to simplify our new urem.
  processUDivOrURem(URem, LVI);

  return true;
}

/// See if LazyValueInfo's ability to exploit edge conditions or range
/// information is sufficient to prove the signs of both operands of this SDiv.
/// If this is the case, replace the SDiv with a UDiv. Even for local
/// conditions, this can sometimes prove conditions instcombine can't by
/// exploiting range information.
static bool processSDiv(BinaryOperator *SDI, const ConstantRange &LCR,
                        const ConstantRange &RCR, LazyValueInfo *LVI) {
  assert(SDI->getOpcode() == Instruction::SDiv);

  // Check whether the division folds to a constant.
  ConstantRange DivCR = LCR.sdiv(RCR);
  if (const APInt *Elem = DivCR.getSingleElement()) {
    SDI->replaceAllUsesWith(ConstantInt::get(SDI->getType(), *Elem));
    SDI->eraseFromParent();
    return true;
  }

  struct Operand {
    Value *V;
    Domain D;
  };
  std::array<Operand, 2> Ops = {{{SDI->getOperand(0), getDomain(LCR)},
                                 {SDI->getOperand(1), getDomain(RCR)}}};
  if (Ops[0].D == Domain::Unknown || Ops[1].D == Domain::Unknown)
    return false;

  // We know domains of both of the operands!
  ++NumSDivs;

  // We need operands to be non-negative, so negate each one that isn't.
  for (Operand &Op : Ops) {
    if (Op.D == Domain::NonNegative)
      continue;
    auto *BO = BinaryOperator::CreateNeg(Op.V, Op.V->getName() + ".nonneg",
                                         SDI->getIterator());
    BO->setDebugLoc(SDI->getDebugLoc());
    Op.V = BO;
  }

  auto *UDiv = BinaryOperator::CreateUDiv(Ops[0].V, Ops[1].V, SDI->getName(),
                                          SDI->getIterator());
  UDiv->setDebugLoc(SDI->getDebugLoc());
  UDiv->setIsExact(SDI->isExact());

  auto *Res = UDiv;

  // If the operands had two different domains, we need to negate the result.
  if (Ops[0].D != Ops[1].D) {
    Res = BinaryOperator::CreateNeg(Res, Res->getName() + ".neg",
                                    SDI->getIterator());
    Res->setDebugLoc(SDI->getDebugLoc());
  }

  SDI->replaceAllUsesWith(Res);
  SDI->eraseFromParent();

  // Try to simplify our new udiv.
  processUDivOrURem(UDiv, LVI);

  return true;
}

static bool processSDivOrSRem(BinaryOperator *Instr, LazyValueInfo *LVI) {
  assert(Instr->getOpcode() == Instruction::SDiv ||
         Instr->getOpcode() == Instruction::SRem);
  ConstantRange LCR =
      LVI->getConstantRangeAtUse(Instr->getOperandUse(0), /*AllowUndef*/ false);
  // Allow undef for RHS, as we can assume it is division by zero UB.
  ConstantRange RCR =
      LVI->getConstantRangeAtUse(Instr->getOperandUse(1), /*AlloweUndef*/ true);
  if (Instr->getOpcode() == Instruction::SDiv)
    if (processSDiv(Instr, LCR, RCR, LVI))
      return true;

  if (Instr->getOpcode() == Instruction::SRem) {
    if (processSRem(Instr, LCR, RCR, LVI))
      return true;
  }

  return narrowSDivOrSRem(Instr, LCR, RCR);
}

static bool processAShr(BinaryOperator *SDI, LazyValueInfo *LVI) {
  ConstantRange LRange =
      LVI->getConstantRangeAtUse(SDI->getOperandUse(0), /*UndefAllowed*/ false);
  unsigned OrigWidth = SDI->getType()->getScalarSizeInBits();
  ConstantRange NegOneOrZero =
      ConstantRange(APInt(OrigWidth, (uint64_t)-1, true), APInt(OrigWidth, 1));
  if (NegOneOrZero.contains(LRange)) {
    // ashr of -1 or 0 never changes the value, so drop the whole instruction
    ++NumAShrsRemoved;
    SDI->replaceAllUsesWith(SDI->getOperand(0));
    SDI->eraseFromParent();
    return true;
  }

  if (!LRange.isAllNonNegative())
    return false;

  ++NumAShrsConverted;
  auto *BO = BinaryOperator::CreateLShr(SDI->getOperand(0), SDI->getOperand(1),
                                        "", SDI->getIterator());
  BO->takeName(SDI);
  BO->setDebugLoc(SDI->getDebugLoc());
  BO->setIsExact(SDI->isExact());
  SDI->replaceAllUsesWith(BO);
  SDI->eraseFromParent();

  return true;
}

static bool processSExt(SExtInst *SDI, LazyValueInfo *LVI) {
  const Use &Base = SDI->getOperandUse(0);
  if (!LVI->getConstantRangeAtUse(Base, /*UndefAllowed*/ false)
           .isAllNonNegative())
    return false;

  ++NumSExt;
  auto *ZExt = CastInst::CreateZExtOrBitCast(Base, SDI->getType(), "",
                                             SDI->getIterator());
  ZExt->takeName(SDI);
  ZExt->setDebugLoc(SDI->getDebugLoc());
  ZExt->setNonNeg();
  SDI->replaceAllUsesWith(ZExt);
  SDI->eraseFromParent();

  return true;
}

static bool processPossibleNonNeg(PossiblyNonNegInst *I, LazyValueInfo *LVI) {
  if (I->hasNonNeg())
    return false;

  const Use &Base = I->getOperandUse(0);
  if (!LVI->getConstantRangeAtUse(Base, /*UndefAllowed*/ false)
           .isAllNonNegative())
    return false;

  ++NumNNeg;
  I->setNonNeg();

  return true;
}

static bool processZExt(ZExtInst *ZExt, LazyValueInfo *LVI) {
  return processPossibleNonNeg(cast<PossiblyNonNegInst>(ZExt), LVI);
}

static bool processUIToFP(UIToFPInst *UIToFP, LazyValueInfo *LVI) {
  return processPossibleNonNeg(cast<PossiblyNonNegInst>(UIToFP), LVI);
}

static bool processSIToFP(SIToFPInst *SIToFP, LazyValueInfo *LVI) {
  const Use &Base = SIToFP->getOperandUse(0);
  if (!LVI->getConstantRangeAtUse(Base, /*UndefAllowed*/ false)
           .isAllNonNegative())
    return false;

  ++NumSIToFP;
  auto *UIToFP = CastInst::Create(Instruction::UIToFP, Base, SIToFP->getType(),
                                  "", SIToFP->getIterator());
  UIToFP->takeName(SIToFP);
  UIToFP->setDebugLoc(SIToFP->getDebugLoc());
  UIToFP->setNonNeg();
  SIToFP->replaceAllUsesWith(UIToFP);
  SIToFP->eraseFromParent();

  return true;
}

static bool processBinOp(BinaryOperator *BinOp, LazyValueInfo *LVI) {
  using OBO = OverflowingBinaryOperator;

  bool NSW = BinOp->hasNoSignedWrap();
  bool NUW = BinOp->hasNoUnsignedWrap();
  if (NSW && NUW)
    return false;

  Instruction::BinaryOps Opcode = BinOp->getOpcode();
  ConstantRange LRange = LVI->getConstantRangeAtUse(BinOp->getOperandUse(0),
                                                    /*UndefAllowed=*/false);
  ConstantRange RRange = LVI->getConstantRangeAtUse(BinOp->getOperandUse(1),
                                                    /*UndefAllowed=*/false);

  bool Changed = false;
  bool NewNUW = false, NewNSW = false;
  if (!NUW) {
    ConstantRange NUWRange = ConstantRange::makeGuaranteedNoWrapRegion(
        Opcode, RRange, OBO::NoUnsignedWrap);
    NewNUW = NUWRange.contains(LRange);
    Changed |= NewNUW;
  }
  if (!NSW) {
    ConstantRange NSWRange = ConstantRange::makeGuaranteedNoWrapRegion(
        Opcode, RRange, OBO::NoSignedWrap);
    NewNSW = NSWRange.contains(LRange);
    Changed |= NewNSW;
  }

  setDeducedOverflowingFlags(BinOp, Opcode, NewNSW, NewNUW);

  return Changed;
}

static bool processAnd(BinaryOperator *BinOp, LazyValueInfo *LVI) {
  using namespace llvm::PatternMatch;

  // Pattern match (and lhs, C) where C includes a superset of bits which might
  // be set in lhs.  This is a common truncation idiom created by instcombine.
  const Use &LHS = BinOp->getOperandUse(0);
  const APInt *RHS;
  if (!match(BinOp->getOperand(1), m_LowBitMask(RHS)))
    return false;

  // We can only replace the AND with LHS based on range info if the range does
  // not include undef.
  ConstantRange LRange =
      LVI->getConstantRangeAtUse(LHS, /*UndefAllowed=*/false);
  if (!LRange.getUnsignedMax().ule(*RHS))
    return false;

  BinOp->replaceAllUsesWith(LHS);
  BinOp->eraseFromParent();
  NumAnd++;
  return true;
}

static bool runImpl(Function &F, LazyValueInfo *LVI, DominatorTree *DT,
                    const SimplifyQuery &SQ) {
  bool FnChanged = false;
  // Visiting in a pre-order depth-first traversal causes us to simplify early
  // blocks before querying later blocks (which require us to analyze early
  // blocks).  Eagerly simplifying shallow blocks means there is strictly less
  // work to do for deep blocks.  This also means we don't visit unreachable
  // blocks.
  for (BasicBlock *BB : depth_first(&F.getEntryBlock())) {
    bool BBChanged = false;
    for (Instruction &II : llvm::make_early_inc_range(*BB)) {
      switch (II.getOpcode()) {
      case Instruction::Select:
        BBChanged |= processSelect(cast<SelectInst>(&II), LVI);
        break;
      case Instruction::PHI:
        BBChanged |= processPHI(cast<PHINode>(&II), LVI, DT, SQ);
        break;
      case Instruction::ICmp:
      case Instruction::FCmp:
        BBChanged |= processCmp(cast<CmpInst>(&II), LVI);
        break;
      case Instruction::Call:
      case Instruction::Invoke:
        BBChanged |= processCallSite(cast<CallBase>(II), LVI);
        break;
      case Instruction::SRem:
      case Instruction::SDiv:
        BBChanged |= processSDivOrSRem(cast<BinaryOperator>(&II), LVI);
        break;
      case Instruction::UDiv:
      case Instruction::URem:
        BBChanged |= processUDivOrURem(cast<BinaryOperator>(&II), LVI);
        break;
      case Instruction::AShr:
        BBChanged |= processAShr(cast<BinaryOperator>(&II), LVI);
        break;
      case Instruction::SExt:
        BBChanged |= processSExt(cast<SExtInst>(&II), LVI);
        break;
      case Instruction::ZExt:
        BBChanged |= processZExt(cast<ZExtInst>(&II), LVI);
        break;
      case Instruction::UIToFP:
        BBChanged |= processUIToFP(cast<UIToFPInst>(&II), LVI);
        break;
      case Instruction::SIToFP:
        BBChanged |= processSIToFP(cast<SIToFPInst>(&II), LVI);
        break;
      case Instruction::Add:
      case Instruction::Sub:
      case Instruction::Mul:
      case Instruction::Shl:
        BBChanged |= processBinOp(cast<BinaryOperator>(&II), LVI);
        break;
      case Instruction::And:
        BBChanged |= processAnd(cast<BinaryOperator>(&II), LVI);
        break;
      }
    }

    Instruction *Term = BB->getTerminator();
    switch (Term->getOpcode()) {
    case Instruction::Switch:
      BBChanged |= processSwitch(cast<SwitchInst>(Term), LVI, DT);
      break;
    case Instruction::Ret: {
      auto *RI = cast<ReturnInst>(Term);
      // Try to determine the return value if we can.  This is mainly here to
      // simplify the writing of unit tests, but also helps to enable IPO by
      // constant folding the return values of callees.
      auto *RetVal = RI->getReturnValue();
      if (!RetVal) break; // handle "ret void"
      if (isa<Constant>(RetVal)) break; // nothing to do
      if (auto *C = getConstantAt(RetVal, RI, LVI)) {
        ++NumReturns;
        RI->replaceUsesOfWith(RetVal, C);
        BBChanged = true;
      }
    }
    }

    FnChanged |= BBChanged;
  }

  return FnChanged;
}

PreservedAnalyses
CorrelatedValuePropagationPass::run(Function &F, FunctionAnalysisManager &AM) {
  LazyValueInfo *LVI = &AM.getResult<LazyValueAnalysis>(F);
  DominatorTree *DT = &AM.getResult<DominatorTreeAnalysis>(F);

  bool Changed = runImpl(F, LVI, DT, getBestSimplifyQuery(AM, F));

  PreservedAnalyses PA;
  if (!Changed) {
    PA = PreservedAnalyses::all();
  } else {
#if defined(EXPENSIVE_CHECKS)
    assert(DT->verify(DominatorTree::VerificationLevel::Full));
#else
    assert(DT->verify(DominatorTree::VerificationLevel::Fast));
#endif // EXPENSIVE_CHECKS

    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<LazyValueAnalysis>();
  }

  // Keeping LVI alive is expensive, both because it uses a lot of memory, and
  // because invalidating values in LVI is expensive. While CVP does preserve
  // LVI, we know that passes after JumpThreading+CVP will not need the result
  // of this analysis, so we forcefully discard it early.
  PA.abandon<LazyValueAnalysis>();
  return PA;
}
