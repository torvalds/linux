//===- InstCombineNegator.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements sinking of negation into expression trees,
// as long as that can be done without increasing instruction count.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace llvm {
class DataLayout;
class LLVMContext;
} // namespace llvm

using namespace llvm;

#define DEBUG_TYPE "instcombine"

STATISTIC(NegatorTotalNegationsAttempted,
          "Negator: Number of negations attempted to be sinked");
STATISTIC(NegatorNumTreesNegated,
          "Negator: Number of negations successfully sinked");
STATISTIC(NegatorMaxDepthVisited, "Negator: Maximal traversal depth ever "
                                  "reached while attempting to sink negation");
STATISTIC(NegatorTimesDepthLimitReached,
          "Negator: How many times did the traversal depth limit was reached "
          "during sinking");
STATISTIC(
    NegatorNumValuesVisited,
    "Negator: Total number of values visited during attempts to sink negation");
STATISTIC(NegatorNumNegationsFoundInCache,
          "Negator: How many negations did we retrieve/reuse from cache");
STATISTIC(NegatorMaxTotalValuesVisited,
          "Negator: Maximal number of values ever visited while attempting to "
          "sink negation");
STATISTIC(NegatorNumInstructionsCreatedTotal,
          "Negator: Number of new negated instructions created, total");
STATISTIC(NegatorMaxInstructionsCreated,
          "Negator: Maximal number of new instructions created during negation "
          "attempt");
STATISTIC(NegatorNumInstructionsNegatedSuccess,
          "Negator: Number of new negated instructions created in successful "
          "negation sinking attempts");

DEBUG_COUNTER(NegatorCounter, "instcombine-negator",
              "Controls Negator transformations in InstCombine pass");

static cl::opt<bool>
    NegatorEnabled("instcombine-negator-enabled", cl::init(true),
                   cl::desc("Should we attempt to sink negations?"));

static cl::opt<unsigned>
    NegatorMaxDepth("instcombine-negator-max-depth",
                    cl::init(NegatorDefaultMaxDepth),
                    cl::desc("What is the maximal lookup depth when trying to "
                             "check for viability of negation sinking."));

Negator::Negator(LLVMContext &C, const DataLayout &DL, bool IsTrulyNegation_)
    : Builder(C, TargetFolder(DL),
              IRBuilderCallbackInserter([&](Instruction *I) {
                ++NegatorNumInstructionsCreatedTotal;
                NewInstructions.push_back(I);
              })),
      IsTrulyNegation(IsTrulyNegation_) {}

#if LLVM_ENABLE_STATS
Negator::~Negator() {
  NegatorMaxTotalValuesVisited.updateMax(NumValuesVisitedInThisNegator);
}
#endif

// Due to the InstCombine's worklist management, there are no guarantees that
// each instruction we'll encounter has been visited by InstCombine already.
// In particular, most importantly for us, that means we have to canonicalize
// constants to RHS ourselves, since that is helpful sometimes.
std::array<Value *, 2> Negator::getSortedOperandsOfBinOp(Instruction *I) {
  assert(I->getNumOperands() == 2 && "Only for binops!");
  std::array<Value *, 2> Ops{I->getOperand(0), I->getOperand(1)};
  if (I->isCommutative() && InstCombiner::getComplexity(I->getOperand(0)) <
                                InstCombiner::getComplexity(I->getOperand(1)))
    std::swap(Ops[0], Ops[1]);
  return Ops;
}

// FIXME: can this be reworked into a worklist-based algorithm while preserving
// the depth-first, early bailout traversal?
[[nodiscard]] Value *Negator::visitImpl(Value *V, bool IsNSW, unsigned Depth) {
  // -(undef) -> undef.
  if (match(V, m_Undef()))
    return V;

  // In i1, negation can simply be ignored.
  if (V->getType()->isIntOrIntVectorTy(1))
    return V;

  Value *X;

  // -(-(X)) -> X.
  if (match(V, m_Neg(m_Value(X))))
    return X;

  // Integral constants can be freely negated.
  if (match(V, m_AnyIntegralConstant()))
    return ConstantExpr::getNeg(cast<Constant>(V),
                                /*HasNSW=*/false);

  // If we have a non-instruction, then give up.
  if (!isa<Instruction>(V))
    return nullptr;

  // If we have started with a true negation (i.e. `sub 0, %y`), then if we've
  // got instruction that does not require recursive reasoning, we can still
  // negate it even if it has other uses, without increasing instruction count.
  if (!V->hasOneUse() && !IsTrulyNegation)
    return nullptr;

  auto *I = cast<Instruction>(V);
  unsigned BitWidth = I->getType()->getScalarSizeInBits();

  // We must preserve the insertion point and debug info that is set in the
  // builder at the time this function is called.
  InstCombiner::BuilderTy::InsertPointGuard Guard(Builder);
  // And since we are trying to negate instruction I, that tells us about the
  // insertion point and the debug info that we need to keep.
  Builder.SetInsertPoint(I);

  // In some cases we can give the answer without further recursion.
  switch (I->getOpcode()) {
  case Instruction::Add: {
    std::array<Value *, 2> Ops = getSortedOperandsOfBinOp(I);
    // `inc` is always negatible.
    if (match(Ops[1], m_One()))
      return Builder.CreateNot(Ops[0], I->getName() + ".neg");
    break;
  }
  case Instruction::Xor:
    // `not` is always negatible.
    if (match(I, m_Not(m_Value(X))))
      return Builder.CreateAdd(X, ConstantInt::get(X->getType(), 1),
                               I->getName() + ".neg");
    break;
  case Instruction::AShr:
  case Instruction::LShr: {
    // Right-shift sign bit smear is negatible.
    const APInt *Op1Val;
    if (match(I->getOperand(1), m_APInt(Op1Val)) && *Op1Val == BitWidth - 1) {
      Value *BO = I->getOpcode() == Instruction::AShr
                      ? Builder.CreateLShr(I->getOperand(0), I->getOperand(1))
                      : Builder.CreateAShr(I->getOperand(0), I->getOperand(1));
      if (auto *NewInstr = dyn_cast<Instruction>(BO)) {
        NewInstr->copyIRFlags(I);
        NewInstr->setName(I->getName() + ".neg");
      }
      return BO;
    }
    // While we could negate exact arithmetic shift:
    //   ashr exact %x, C  -->   sdiv exact i8 %x, -1<<C
    // iff C != 0 and C u< bitwidth(%x), we don't want to,
    // because division is *THAT* much worse than a shift.
    break;
  }
  case Instruction::SExt:
  case Instruction::ZExt:
    // `*ext` of i1 is always negatible
    if (I->getOperand(0)->getType()->isIntOrIntVectorTy(1))
      return I->getOpcode() == Instruction::SExt
                 ? Builder.CreateZExt(I->getOperand(0), I->getType(),
                                      I->getName() + ".neg")
                 : Builder.CreateSExt(I->getOperand(0), I->getType(),
                                      I->getName() + ".neg");
    break;
  case Instruction::Select: {
    // If both arms of the select are constants, we don't need to recurse.
    // Therefore, this transform is not limited by uses.
    auto *Sel = cast<SelectInst>(I);
    Constant *TrueC, *FalseC;
    if (match(Sel->getTrueValue(), m_ImmConstant(TrueC)) &&
        match(Sel->getFalseValue(), m_ImmConstant(FalseC))) {
      Constant *NegTrueC = ConstantExpr::getNeg(TrueC);
      Constant *NegFalseC = ConstantExpr::getNeg(FalseC);
      return Builder.CreateSelect(Sel->getCondition(), NegTrueC, NegFalseC,
                                  I->getName() + ".neg", /*MDFrom=*/I);
    }
    break;
  }
  case Instruction::Call:
    if (auto *CI = dyn_cast<CmpIntrinsic>(I); CI && CI->hasOneUse())
      return Builder.CreateIntrinsic(CI->getType(), CI->getIntrinsicID(),
                                     {CI->getRHS(), CI->getLHS()});
    break;
  default:
    break; // Other instructions require recursive reasoning.
  }

  if (I->getOpcode() == Instruction::Sub &&
      (I->hasOneUse() || match(I->getOperand(0), m_ImmConstant()))) {
    // `sub` is always negatible.
    // However, only do this either if the old `sub` doesn't stick around, or
    // it was subtracting from a constant. Otherwise, this isn't profitable.
    return Builder.CreateSub(I->getOperand(1), I->getOperand(0),
                             I->getName() + ".neg", /* HasNUW */ false,
                             IsNSW && I->hasNoSignedWrap());
  }

  // Some other cases, while still don't require recursion,
  // are restricted to the one-use case.
  if (!V->hasOneUse())
    return nullptr;

  switch (I->getOpcode()) {
  case Instruction::ZExt: {
    // Negation of zext of signbit is signbit splat:
    // 0 - (zext (i8 X u>> 7) to iN) --> sext (i8 X s>> 7) to iN
    Value *SrcOp = I->getOperand(0);
    unsigned SrcWidth = SrcOp->getType()->getScalarSizeInBits();
    const APInt &FullShift = APInt(SrcWidth, SrcWidth - 1);
    if (IsTrulyNegation &&
        match(SrcOp, m_LShr(m_Value(X), m_SpecificIntAllowPoison(FullShift)))) {
      Value *Ashr = Builder.CreateAShr(X, FullShift);
      return Builder.CreateSExt(Ashr, I->getType());
    }
    break;
  }
  case Instruction::And: {
    Constant *ShAmt;
    // sub(y,and(lshr(x,C),1)) --> add(ashr(shl(x,(BW-1)-C),BW-1),y)
    if (match(I, m_And(m_OneUse(m_TruncOrSelf(
                           m_LShr(m_Value(X), m_ImmConstant(ShAmt)))),
                       m_One()))) {
      unsigned BW = X->getType()->getScalarSizeInBits();
      Constant *BWMinusOne = ConstantInt::get(X->getType(), BW - 1);
      Value *R = Builder.CreateShl(X, Builder.CreateSub(BWMinusOne, ShAmt));
      R = Builder.CreateAShr(R, BWMinusOne);
      return Builder.CreateTruncOrBitCast(R, I->getType());
    }
    break;
  }
  case Instruction::SDiv:
    // `sdiv` is negatible if divisor is not undef/INT_MIN/1.
    // While this is normally not behind a use-check,
    // let's consider division to be special since it's costly.
    if (auto *Op1C = dyn_cast<Constant>(I->getOperand(1))) {
      if (!Op1C->containsUndefOrPoisonElement() &&
          Op1C->isNotMinSignedValue() && Op1C->isNotOneValue()) {
        Value *BO =
            Builder.CreateSDiv(I->getOperand(0), ConstantExpr::getNeg(Op1C),
                               I->getName() + ".neg");
        if (auto *NewInstr = dyn_cast<Instruction>(BO))
          NewInstr->setIsExact(I->isExact());
        return BO;
      }
    }
    break;
  }

  // Rest of the logic is recursive, so if it's time to give up then it's time.
  if (Depth > NegatorMaxDepth) {
    LLVM_DEBUG(dbgs() << "Negator: reached maximal allowed traversal depth in "
                      << *V << ". Giving up.\n");
    ++NegatorTimesDepthLimitReached;
    return nullptr;
  }

  switch (I->getOpcode()) {
  case Instruction::Freeze: {
    // `freeze` is negatible if its operand is negatible.
    Value *NegOp = negate(I->getOperand(0), IsNSW, Depth + 1);
    if (!NegOp) // Early return.
      return nullptr;
    return Builder.CreateFreeze(NegOp, I->getName() + ".neg");
  }
  case Instruction::PHI: {
    // `phi` is negatible if all the incoming values are negatible.
    auto *PHI = cast<PHINode>(I);
    SmallVector<Value *, 4> NegatedIncomingValues(PHI->getNumOperands());
    for (auto I : zip(PHI->incoming_values(), NegatedIncomingValues)) {
      if (!(std::get<1>(I) =
                negate(std::get<0>(I), IsNSW, Depth + 1))) // Early return.
        return nullptr;
    }
    // All incoming values are indeed negatible. Create negated PHI node.
    PHINode *NegatedPHI = Builder.CreatePHI(
        PHI->getType(), PHI->getNumOperands(), PHI->getName() + ".neg");
    for (auto I : zip(NegatedIncomingValues, PHI->blocks()))
      NegatedPHI->addIncoming(std::get<0>(I), std::get<1>(I));
    return NegatedPHI;
  }
  case Instruction::Select: {
    if (isKnownNegation(I->getOperand(1), I->getOperand(2), /*NeedNSW=*/false,
                        /*AllowPoison=*/false)) {
      // Of one hand of select is known to be negation of another hand,
      // just swap the hands around.
      auto *NewSelect = cast<SelectInst>(I->clone());
      // Just swap the operands of the select.
      NewSelect->swapValues();
      // Don't swap prof metadata, we didn't change the branch behavior.
      NewSelect->setName(I->getName() + ".neg");
      // Poison-generating flags should be dropped
      Value *TV = NewSelect->getTrueValue();
      Value *FV = NewSelect->getFalseValue();
      if (match(TV, m_Neg(m_Specific(FV))))
        cast<Instruction>(TV)->dropPoisonGeneratingFlags();
      else if (match(FV, m_Neg(m_Specific(TV))))
        cast<Instruction>(FV)->dropPoisonGeneratingFlags();
      else {
        cast<Instruction>(TV)->dropPoisonGeneratingFlags();
        cast<Instruction>(FV)->dropPoisonGeneratingFlags();
      }
      Builder.Insert(NewSelect);
      return NewSelect;
    }
    // `select` is negatible if both hands of `select` are negatible.
    Value *NegOp1 = negate(I->getOperand(1), IsNSW, Depth + 1);
    if (!NegOp1) // Early return.
      return nullptr;
    Value *NegOp2 = negate(I->getOperand(2), IsNSW, Depth + 1);
    if (!NegOp2)
      return nullptr;
    // Do preserve the metadata!
    return Builder.CreateSelect(I->getOperand(0), NegOp1, NegOp2,
                                I->getName() + ".neg", /*MDFrom=*/I);
  }
  case Instruction::ShuffleVector: {
    // `shufflevector` is negatible if both operands are negatible.
    auto *Shuf = cast<ShuffleVectorInst>(I);
    Value *NegOp0 = negate(I->getOperand(0), IsNSW, Depth + 1);
    if (!NegOp0) // Early return.
      return nullptr;
    Value *NegOp1 = negate(I->getOperand(1), IsNSW, Depth + 1);
    if (!NegOp1)
      return nullptr;
    return Builder.CreateShuffleVector(NegOp0, NegOp1, Shuf->getShuffleMask(),
                                       I->getName() + ".neg");
  }
  case Instruction::ExtractElement: {
    // `extractelement` is negatible if source operand is negatible.
    auto *EEI = cast<ExtractElementInst>(I);
    Value *NegVector = negate(EEI->getVectorOperand(), IsNSW, Depth + 1);
    if (!NegVector) // Early return.
      return nullptr;
    return Builder.CreateExtractElement(NegVector, EEI->getIndexOperand(),
                                        I->getName() + ".neg");
  }
  case Instruction::InsertElement: {
    // `insertelement` is negatible if both the source vector and
    // element-to-be-inserted are negatible.
    auto *IEI = cast<InsertElementInst>(I);
    Value *NegVector = negate(IEI->getOperand(0), IsNSW, Depth + 1);
    if (!NegVector) // Early return.
      return nullptr;
    Value *NegNewElt = negate(IEI->getOperand(1), IsNSW, Depth + 1);
    if (!NegNewElt) // Early return.
      return nullptr;
    return Builder.CreateInsertElement(NegVector, NegNewElt, IEI->getOperand(2),
                                       I->getName() + ".neg");
  }
  case Instruction::Trunc: {
    // `trunc` is negatible if its operand is negatible.
    Value *NegOp = negate(I->getOperand(0), /* IsNSW */ false, Depth + 1);
    if (!NegOp) // Early return.
      return nullptr;
    return Builder.CreateTrunc(NegOp, I->getType(), I->getName() + ".neg");
  }
  case Instruction::Shl: {
    // `shl` is negatible if the first operand is negatible.
    IsNSW &= I->hasNoSignedWrap();
    if (Value *NegOp0 = negate(I->getOperand(0), IsNSW, Depth + 1))
      return Builder.CreateShl(NegOp0, I->getOperand(1), I->getName() + ".neg",
                               /* HasNUW */ false, IsNSW);
    // Otherwise, `shl %x, C` can be interpreted as `mul %x, 1<<C`.
    Constant *Op1C;
    if (!match(I->getOperand(1), m_ImmConstant(Op1C)) || !IsTrulyNegation)
      return nullptr;
    return Builder.CreateMul(
        I->getOperand(0),
        Builder.CreateShl(Constant::getAllOnesValue(Op1C->getType()), Op1C),
        I->getName() + ".neg", /* HasNUW */ false, IsNSW);
  }
  case Instruction::Or: {
    if (!cast<PossiblyDisjointInst>(I)->isDisjoint())
      return nullptr; // Don't know how to handle `or` in general.
    std::array<Value *, 2> Ops = getSortedOperandsOfBinOp(I);
    // `or`/`add` are interchangeable when operands have no common bits set.
    // `inc` is always negatible.
    if (match(Ops[1], m_One()))
      return Builder.CreateNot(Ops[0], I->getName() + ".neg");
    // Else, just defer to Instruction::Add handling.
    [[fallthrough]];
  }
  case Instruction::Add: {
    // `add` is negatible if both of its operands are negatible.
    SmallVector<Value *, 2> NegatedOps, NonNegatedOps;
    for (Value *Op : I->operands()) {
      // Can we sink the negation into this operand?
      if (Value *NegOp = negate(Op, /* IsNSW */ false, Depth + 1)) {
        NegatedOps.emplace_back(NegOp); // Successfully negated operand!
        continue;
      }
      // Failed to sink negation into this operand. IFF we started from negation
      // and we manage to sink negation into one operand, we can still do this.
      if (!IsTrulyNegation)
        return nullptr;
      NonNegatedOps.emplace_back(Op); // Just record which operand that was.
    }
    assert((NegatedOps.size() + NonNegatedOps.size()) == 2 &&
           "Internal consistency check failed.");
    // Did we manage to sink negation into both of the operands?
    if (NegatedOps.size() == 2) // Then we get to keep the `add`!
      return Builder.CreateAdd(NegatedOps[0], NegatedOps[1],
                               I->getName() + ".neg");
    assert(IsTrulyNegation && "We should have early-exited then.");
    // Completely failed to sink negation?
    if (NonNegatedOps.size() == 2)
      return nullptr;
    // 0-(a+b) --> (-a)-b
    return Builder.CreateSub(NegatedOps[0], NonNegatedOps[0],
                             I->getName() + ".neg");
  }
  case Instruction::Xor: {
    std::array<Value *, 2> Ops = getSortedOperandsOfBinOp(I);
    // `xor` is negatible if one of its operands is invertible.
    // FIXME: InstCombineInverter? But how to connect Inverter and Negator?
    if (auto *C = dyn_cast<Constant>(Ops[1])) {
      if (IsTrulyNegation) {
        Value *Xor = Builder.CreateXor(Ops[0], ConstantExpr::getNot(C));
        return Builder.CreateAdd(Xor, ConstantInt::get(Xor->getType(), 1),
                                 I->getName() + ".neg");
      }
    }
    return nullptr;
  }
  case Instruction::Mul: {
    std::array<Value *, 2> Ops = getSortedOperandsOfBinOp(I);
    // `mul` is negatible if one of its operands is negatible.
    Value *NegatedOp, *OtherOp;
    // First try the second operand, in case it's a constant it will be best to
    // just invert it instead of sinking the `neg` deeper.
    if (Value *NegOp1 = negate(Ops[1], /* IsNSW */ false, Depth + 1)) {
      NegatedOp = NegOp1;
      OtherOp = Ops[0];
    } else if (Value *NegOp0 = negate(Ops[0], /* IsNSW */ false, Depth + 1)) {
      NegatedOp = NegOp0;
      OtherOp = Ops[1];
    } else
      // Can't negate either of them.
      return nullptr;
    return Builder.CreateMul(NegatedOp, OtherOp, I->getName() + ".neg",
                             /* HasNUW */ false, IsNSW && I->hasNoSignedWrap());
  }
  default:
    return nullptr; // Don't know, likely not negatible for free.
  }

  llvm_unreachable("Can't get here. We always return from switch.");
}

[[nodiscard]] Value *Negator::negate(Value *V, bool IsNSW, unsigned Depth) {
  NegatorMaxDepthVisited.updateMax(Depth);
  ++NegatorNumValuesVisited;

#if LLVM_ENABLE_STATS
  ++NumValuesVisitedInThisNegator;
#endif

#ifndef NDEBUG
  // We can't ever have a Value with such an address.
  Value *Placeholder = reinterpret_cast<Value *>(static_cast<uintptr_t>(-1));
#endif

  // Did we already try to negate this value?
  auto NegationsCacheIterator = NegationsCache.find(V);
  if (NegationsCacheIterator != NegationsCache.end()) {
    ++NegatorNumNegationsFoundInCache;
    Value *NegatedV = NegationsCacheIterator->second;
    assert(NegatedV != Placeholder && "Encountered a cycle during negation.");
    return NegatedV;
  }

#ifndef NDEBUG
  // We did not find a cached result for negation of V. While there,
  // let's temporairly cache a placeholder value, with the idea that if later
  // during negation we fetch it from cache, we'll know we're in a cycle.
  NegationsCache[V] = Placeholder;
#endif

  // No luck. Try negating it for real.
  Value *NegatedV = visitImpl(V, IsNSW, Depth);
  // And cache the (real) result for the future.
  NegationsCache[V] = NegatedV;

  return NegatedV;
}

[[nodiscard]] std::optional<Negator::Result> Negator::run(Value *Root,
                                                          bool IsNSW) {
  Value *Negated = negate(Root, IsNSW, /*Depth=*/0);
  if (!Negated) {
    // We must cleanup newly-inserted instructions, to avoid any potential
    // endless combine looping.
    for (Instruction *I : llvm::reverse(NewInstructions))
      I->eraseFromParent();
    return std::nullopt;
  }
  return std::make_pair(ArrayRef<Instruction *>(NewInstructions), Negated);
}

[[nodiscard]] Value *Negator::Negate(bool LHSIsZero, bool IsNSW, Value *Root,
                                     InstCombinerImpl &IC) {
  ++NegatorTotalNegationsAttempted;
  LLVM_DEBUG(dbgs() << "Negator: attempting to sink negation into " << *Root
                    << "\n");

  if (!NegatorEnabled || !DebugCounter::shouldExecute(NegatorCounter))
    return nullptr;

  Negator N(Root->getContext(), IC.getDataLayout(), LHSIsZero);
  std::optional<Result> Res = N.run(Root, IsNSW);
  if (!Res) { // Negation failed.
    LLVM_DEBUG(dbgs() << "Negator: failed to sink negation into " << *Root
                      << "\n");
    return nullptr;
  }

  LLVM_DEBUG(dbgs() << "Negator: successfully sunk negation into " << *Root
                    << "\n         NEW: " << *Res->second << "\n");
  ++NegatorNumTreesNegated;

  // We must temporarily unset the 'current' insertion point and DebugLoc of the
  // InstCombine's IRBuilder so that it won't interfere with the ones we have
  // already specified when producing negated instructions.
  InstCombiner::BuilderTy::InsertPointGuard Guard(IC.Builder);
  IC.Builder.ClearInsertionPoint();
  IC.Builder.SetCurrentDebugLocation(DebugLoc());

  // And finally, we must add newly-created instructions into the InstCombine's
  // worklist (in a proper order!) so it can attempt to combine them.
  LLVM_DEBUG(dbgs() << "Negator: Propagating " << Res->first.size()
                    << " instrs to InstCombine\n");
  NegatorMaxInstructionsCreated.updateMax(Res->first.size());
  NegatorNumInstructionsNegatedSuccess += Res->first.size();

  // They are in def-use order, so nothing fancy, just insert them in order.
  for (Instruction *I : Res->first)
    IC.Builder.Insert(I, I->getName());

  // And return the new root.
  return Res->second;
}
