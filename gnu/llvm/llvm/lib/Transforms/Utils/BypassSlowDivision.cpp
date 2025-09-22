//===- BypassSlowDivision.cpp - Bypass slow division ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains an optimization for div and rem on architectures that
// execute short instructions significantly faster than longer instructions.
// For example, on Intel Atom 32-bit divides are slow enough that during
// runtime it is profitable to check the value of the operands, and if they are
// positive and less than 256 use an unsigned 8-bit divide.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/BypassSlowDivision.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/KnownBits.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "bypass-slow-division"

namespace {

  struct QuotRemPair {
    Value *Quotient;
    Value *Remainder;

    QuotRemPair(Value *InQuotient, Value *InRemainder)
        : Quotient(InQuotient), Remainder(InRemainder) {}
  };

  /// A quotient and remainder, plus a BB from which they logically "originate".
  /// If you use Quotient or Remainder in a Phi node, you should use BB as its
  /// corresponding predecessor.
  struct QuotRemWithBB {
    BasicBlock *BB = nullptr;
    Value *Quotient = nullptr;
    Value *Remainder = nullptr;
  };

using DivCacheTy = DenseMap<DivRemMapKey, QuotRemPair>;
using BypassWidthsTy = DenseMap<unsigned, unsigned>;
using VisitedSetTy = SmallPtrSet<Instruction *, 4>;

enum ValueRange {
  /// Operand definitely fits into BypassType. No runtime checks are needed.
  VALRNG_KNOWN_SHORT,
  /// A runtime check is required, as value range is unknown.
  VALRNG_UNKNOWN,
  /// Operand is unlikely to fit into BypassType. The bypassing should be
  /// disabled.
  VALRNG_LIKELY_LONG
};

class FastDivInsertionTask {
  bool IsValidTask = false;
  Instruction *SlowDivOrRem = nullptr;
  IntegerType *BypassType = nullptr;
  BasicBlock *MainBB = nullptr;

  bool isHashLikeValue(Value *V, VisitedSetTy &Visited);
  ValueRange getValueRange(Value *Op, VisitedSetTy &Visited);
  QuotRemWithBB createSlowBB(BasicBlock *Successor);
  QuotRemWithBB createFastBB(BasicBlock *Successor);
  QuotRemPair createDivRemPhiNodes(QuotRemWithBB &LHS, QuotRemWithBB &RHS,
                                   BasicBlock *PhiBB);
  Value *insertOperandRuntimeCheck(Value *Op1, Value *Op2);
  std::optional<QuotRemPair> insertFastDivAndRem();

  bool isSignedOp() {
    return SlowDivOrRem->getOpcode() == Instruction::SDiv ||
           SlowDivOrRem->getOpcode() == Instruction::SRem;
  }

  bool isDivisionOp() {
    return SlowDivOrRem->getOpcode() == Instruction::SDiv ||
           SlowDivOrRem->getOpcode() == Instruction::UDiv;
  }

  Type *getSlowType() { return SlowDivOrRem->getType(); }

public:
  FastDivInsertionTask(Instruction *I, const BypassWidthsTy &BypassWidths);

  Value *getReplacement(DivCacheTy &Cache);
};

} // end anonymous namespace

FastDivInsertionTask::FastDivInsertionTask(Instruction *I,
                                           const BypassWidthsTy &BypassWidths) {
  switch (I->getOpcode()) {
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    SlowDivOrRem = I;
    break;
  default:
    // I is not a div/rem operation.
    return;
  }

  // Skip division on vector types. Only optimize integer instructions.
  IntegerType *SlowType = dyn_cast<IntegerType>(SlowDivOrRem->getType());
  if (!SlowType)
    return;

  // Skip if this bitwidth is not bypassed.
  auto BI = BypassWidths.find(SlowType->getBitWidth());
  if (BI == BypassWidths.end())
    return;

  // Get type for div/rem instruction with bypass bitwidth.
  IntegerType *BT = IntegerType::get(I->getContext(), BI->second);
  BypassType = BT;

  // The original basic block.
  MainBB = I->getParent();

  // The instruction is indeed a slow div or rem operation.
  IsValidTask = true;
}

/// Reuses previously-computed dividend or remainder from the current BB if
/// operands and operation are identical. Otherwise calls insertFastDivAndRem to
/// perform the optimization and caches the resulting dividend and remainder.
/// If no replacement can be generated, nullptr is returned.
Value *FastDivInsertionTask::getReplacement(DivCacheTy &Cache) {
  // First, make sure that the task is valid.
  if (!IsValidTask)
    return nullptr;

  // Then, look for a value in Cache.
  Value *Dividend = SlowDivOrRem->getOperand(0);
  Value *Divisor = SlowDivOrRem->getOperand(1);
  DivRemMapKey Key(isSignedOp(), Dividend, Divisor);
  auto CacheI = Cache.find(Key);

  if (CacheI == Cache.end()) {
    // If previous instance does not exist, try to insert fast div.
    std::optional<QuotRemPair> OptResult = insertFastDivAndRem();
    // Bail out if insertFastDivAndRem has failed.
    if (!OptResult)
      return nullptr;
    CacheI = Cache.insert({Key, *OptResult}).first;
  }

  QuotRemPair &Value = CacheI->second;
  return isDivisionOp() ? Value.Quotient : Value.Remainder;
}

/// Check if a value looks like a hash.
///
/// The routine is expected to detect values computed using the most common hash
/// algorithms. Typically, hash computations end with one of the following
/// instructions:
///
/// 1) MUL with a constant wider than BypassType
/// 2) XOR instruction
///
/// And even if we are wrong and the value is not a hash, it is still quite
/// unlikely that such values will fit into BypassType.
///
/// To detect string hash algorithms like FNV we have to look through PHI-nodes.
/// It is implemented as a depth-first search for values that look neither long
/// nor hash-like.
bool FastDivInsertionTask::isHashLikeValue(Value *V, VisitedSetTy &Visited) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  switch (I->getOpcode()) {
  case Instruction::Xor:
    return true;
  case Instruction::Mul: {
    // After Constant Hoisting pass, long constants may be represented as
    // bitcast instructions. As a result, some constants may look like an
    // instruction at first, and an additional check is necessary to find out if
    // an operand is actually a constant.
    Value *Op1 = I->getOperand(1);
    ConstantInt *C = dyn_cast<ConstantInt>(Op1);
    if (!C && isa<BitCastInst>(Op1))
      C = dyn_cast<ConstantInt>(cast<BitCastInst>(Op1)->getOperand(0));
    return C && C->getValue().getSignificantBits() > BypassType->getBitWidth();
  }
  case Instruction::PHI:
    // Stop IR traversal in case of a crazy input code. This limits recursion
    // depth.
    if (Visited.size() >= 16)
      return false;
    // Do not visit nodes that have been visited already. We return true because
    // it means that we couldn't find any value that doesn't look hash-like.
    if (!Visited.insert(I).second)
      return true;
    return llvm::all_of(cast<PHINode>(I)->incoming_values(), [&](Value *V) {
      // Ignore undef values as they probably don't affect the division
      // operands.
      return getValueRange(V, Visited) == VALRNG_LIKELY_LONG ||
             isa<UndefValue>(V);
    });
  default:
    return false;
  }
}

/// Check if an integer value fits into our bypass type.
ValueRange FastDivInsertionTask::getValueRange(Value *V,
                                               VisitedSetTy &Visited) {
  unsigned ShortLen = BypassType->getBitWidth();
  unsigned LongLen = V->getType()->getIntegerBitWidth();

  assert(LongLen > ShortLen && "Value type must be wider than BypassType");
  unsigned HiBits = LongLen - ShortLen;

  const DataLayout &DL = SlowDivOrRem->getDataLayout();
  KnownBits Known(LongLen);

  computeKnownBits(V, Known, DL);

  if (Known.countMinLeadingZeros() >= HiBits)
    return VALRNG_KNOWN_SHORT;

  if (Known.countMaxLeadingZeros() < HiBits)
    return VALRNG_LIKELY_LONG;

  // Long integer divisions are often used in hashtable implementations. It's
  // not worth bypassing such divisions because hash values are extremely
  // unlikely to have enough leading zeros. The call below tries to detect
  // values that are unlikely to fit BypassType (including hashes).
  if (isHashLikeValue(V, Visited))
    return VALRNG_LIKELY_LONG;

  return VALRNG_UNKNOWN;
}

/// Add new basic block for slow div and rem operations and put it before
/// SuccessorBB.
QuotRemWithBB FastDivInsertionTask::createSlowBB(BasicBlock *SuccessorBB) {
  QuotRemWithBB DivRemPair;
  DivRemPair.BB = BasicBlock::Create(MainBB->getParent()->getContext(), "",
                                     MainBB->getParent(), SuccessorBB);
  IRBuilder<> Builder(DivRemPair.BB, DivRemPair.BB->begin());
  Builder.SetCurrentDebugLocation(SlowDivOrRem->getDebugLoc());

  Value *Dividend = SlowDivOrRem->getOperand(0);
  Value *Divisor = SlowDivOrRem->getOperand(1);

  if (isSignedOp()) {
    DivRemPair.Quotient = Builder.CreateSDiv(Dividend, Divisor);
    DivRemPair.Remainder = Builder.CreateSRem(Dividend, Divisor);
  } else {
    DivRemPair.Quotient = Builder.CreateUDiv(Dividend, Divisor);
    DivRemPair.Remainder = Builder.CreateURem(Dividend, Divisor);
  }

  Builder.CreateBr(SuccessorBB);
  return DivRemPair;
}

/// Add new basic block for fast div and rem operations and put it before
/// SuccessorBB.
QuotRemWithBB FastDivInsertionTask::createFastBB(BasicBlock *SuccessorBB) {
  QuotRemWithBB DivRemPair;
  DivRemPair.BB = BasicBlock::Create(MainBB->getParent()->getContext(), "",
                                     MainBB->getParent(), SuccessorBB);
  IRBuilder<> Builder(DivRemPair.BB, DivRemPair.BB->begin());
  Builder.SetCurrentDebugLocation(SlowDivOrRem->getDebugLoc());

  Value *Dividend = SlowDivOrRem->getOperand(0);
  Value *Divisor = SlowDivOrRem->getOperand(1);
  Value *ShortDivisorV =
      Builder.CreateCast(Instruction::Trunc, Divisor, BypassType);
  Value *ShortDividendV =
      Builder.CreateCast(Instruction::Trunc, Dividend, BypassType);

  // udiv/urem because this optimization only handles positive numbers.
  Value *ShortQV = Builder.CreateUDiv(ShortDividendV, ShortDivisorV);
  Value *ShortRV = Builder.CreateURem(ShortDividendV, ShortDivisorV);
  DivRemPair.Quotient =
      Builder.CreateCast(Instruction::ZExt, ShortQV, getSlowType());
  DivRemPair.Remainder =
      Builder.CreateCast(Instruction::ZExt, ShortRV, getSlowType());
  Builder.CreateBr(SuccessorBB);

  return DivRemPair;
}

/// Creates Phi nodes for result of Div and Rem.
QuotRemPair FastDivInsertionTask::createDivRemPhiNodes(QuotRemWithBB &LHS,
                                                       QuotRemWithBB &RHS,
                                                       BasicBlock *PhiBB) {
  IRBuilder<> Builder(PhiBB, PhiBB->begin());
  Builder.SetCurrentDebugLocation(SlowDivOrRem->getDebugLoc());
  PHINode *QuoPhi = Builder.CreatePHI(getSlowType(), 2);
  QuoPhi->addIncoming(LHS.Quotient, LHS.BB);
  QuoPhi->addIncoming(RHS.Quotient, RHS.BB);
  PHINode *RemPhi = Builder.CreatePHI(getSlowType(), 2);
  RemPhi->addIncoming(LHS.Remainder, LHS.BB);
  RemPhi->addIncoming(RHS.Remainder, RHS.BB);
  return QuotRemPair(QuoPhi, RemPhi);
}

/// Creates a runtime check to test whether both the divisor and dividend fit
/// into BypassType. The check is inserted at the end of MainBB. True return
/// value means that the operands fit. Either of the operands may be NULL if it
/// doesn't need a runtime check.
Value *FastDivInsertionTask::insertOperandRuntimeCheck(Value *Op1, Value *Op2) {
  assert((Op1 || Op2) && "Nothing to check");
  IRBuilder<> Builder(MainBB, MainBB->end());
  Builder.SetCurrentDebugLocation(SlowDivOrRem->getDebugLoc());

  Value *OrV;
  if (Op1 && Op2)
    OrV = Builder.CreateOr(Op1, Op2);
  else
    OrV = Op1 ? Op1 : Op2;

  // BitMask is inverted to check if the operands are
  // larger than the bypass type
  uint64_t BitMask = ~BypassType->getBitMask();
  Value *AndV = Builder.CreateAnd(OrV, BitMask);

  // Compare operand values
  Value *ZeroV = ConstantInt::getSigned(getSlowType(), 0);
  return Builder.CreateICmpEQ(AndV, ZeroV);
}

/// Substitutes the div/rem instruction with code that checks the value of the
/// operands and uses a shorter-faster div/rem instruction when possible.
std::optional<QuotRemPair> FastDivInsertionTask::insertFastDivAndRem() {
  Value *Dividend = SlowDivOrRem->getOperand(0);
  Value *Divisor = SlowDivOrRem->getOperand(1);

  VisitedSetTy SetL;
  ValueRange DividendRange = getValueRange(Dividend, SetL);
  if (DividendRange == VALRNG_LIKELY_LONG)
    return std::nullopt;

  VisitedSetTy SetR;
  ValueRange DivisorRange = getValueRange(Divisor, SetR);
  if (DivisorRange == VALRNG_LIKELY_LONG)
    return std::nullopt;

  bool DividendShort = (DividendRange == VALRNG_KNOWN_SHORT);
  bool DivisorShort = (DivisorRange == VALRNG_KNOWN_SHORT);

  if (DividendShort && DivisorShort) {
    // If both operands are known to be short then just replace the long
    // division with a short one in-place.  Since we're not introducing control
    // flow in this case, narrowing the division is always a win, even if the
    // divisor is a constant (and will later get replaced by a multiplication).

    IRBuilder<> Builder(SlowDivOrRem);
    Value *TruncDividend = Builder.CreateTrunc(Dividend, BypassType);
    Value *TruncDivisor = Builder.CreateTrunc(Divisor, BypassType);
    Value *TruncDiv = Builder.CreateUDiv(TruncDividend, TruncDivisor);
    Value *TruncRem = Builder.CreateURem(TruncDividend, TruncDivisor);
    Value *ExtDiv = Builder.CreateZExt(TruncDiv, getSlowType());
    Value *ExtRem = Builder.CreateZExt(TruncRem, getSlowType());
    return QuotRemPair(ExtDiv, ExtRem);
  }

  if (isa<ConstantInt>(Divisor)) {
    // If the divisor is not a constant, DAGCombiner will convert it to a
    // multiplication by a magic constant.  It isn't clear if it is worth
    // introducing control flow to get a narrower multiply.
    return std::nullopt;
  }

  // After Constant Hoisting pass, long constants may be represented as
  // bitcast instructions. As a result, some constants may look like an
  // instruction at first, and an additional check is necessary to find out if
  // an operand is actually a constant.
  if (auto *BCI = dyn_cast<BitCastInst>(Divisor))
    if (BCI->getParent() == SlowDivOrRem->getParent() &&
        isa<ConstantInt>(BCI->getOperand(0)))
      return std::nullopt;

  IRBuilder<> Builder(MainBB, MainBB->end());
  Builder.SetCurrentDebugLocation(SlowDivOrRem->getDebugLoc());

  if (DividendShort && !isSignedOp()) {
    // If the division is unsigned and Dividend is known to be short, then
    // either
    // 1) Divisor is less or equal to Dividend, and the result can be computed
    //    with a short division.
    // 2) Divisor is greater than Dividend. In this case, no division is needed
    //    at all: The quotient is 0 and the remainder is equal to Dividend.
    //
    // So instead of checking at runtime whether Divisor fits into BypassType,
    // we emit a runtime check to differentiate between these two cases. This
    // lets us entirely avoid a long div.

    // Split the basic block before the div/rem.
    BasicBlock *SuccessorBB = MainBB->splitBasicBlock(SlowDivOrRem);
    // Remove the unconditional branch from MainBB to SuccessorBB.
    MainBB->back().eraseFromParent();
    QuotRemWithBB Long;
    Long.BB = MainBB;
    Long.Quotient = ConstantInt::get(getSlowType(), 0);
    Long.Remainder = Dividend;
    QuotRemWithBB Fast = createFastBB(SuccessorBB);
    QuotRemPair Result = createDivRemPhiNodes(Fast, Long, SuccessorBB);
    Value *CmpV = Builder.CreateICmpUGE(Dividend, Divisor);
    Builder.CreateCondBr(CmpV, Fast.BB, SuccessorBB);
    return Result;
  } else {
    // General case. Create both slow and fast div/rem pairs and choose one of
    // them at runtime.

    // Split the basic block before the div/rem.
    BasicBlock *SuccessorBB = MainBB->splitBasicBlock(SlowDivOrRem);
    // Remove the unconditional branch from MainBB to SuccessorBB.
    MainBB->back().eraseFromParent();
    QuotRemWithBB Fast = createFastBB(SuccessorBB);
    QuotRemWithBB Slow = createSlowBB(SuccessorBB);
    QuotRemPair Result = createDivRemPhiNodes(Fast, Slow, SuccessorBB);
    Value *CmpV = insertOperandRuntimeCheck(DividendShort ? nullptr : Dividend,
                                            DivisorShort ? nullptr : Divisor);
    Builder.CreateCondBr(CmpV, Fast.BB, Slow.BB);
    return Result;
  }
}

/// This optimization identifies DIV/REM instructions in a BB that can be
/// profitably bypassed and carried out with a shorter, faster divide.
bool llvm::bypassSlowDivision(BasicBlock *BB,
                              const BypassWidthsTy &BypassWidths) {
  DivCacheTy PerBBDivCache;

  bool MadeChange = false;
  Instruction *Next = &*BB->begin();
  while (Next != nullptr) {
    // We may add instructions immediately after I, but we want to skip over
    // them.
    Instruction *I = Next;
    Next = Next->getNextNode();

    // Ignore dead code to save time and avoid bugs.
    if (I->hasNUses(0))
      continue;

    FastDivInsertionTask Task(I, BypassWidths);
    if (Value *Replacement = Task.getReplacement(PerBBDivCache)) {
      I->replaceAllUsesWith(Replacement);
      I->eraseFromParent();
      MadeChange = true;
    }
  }

  // Above we eagerly create divs and rems, as pairs, so that we can efficiently
  // create divrem machine instructions.  Now erase any unused divs / rems so we
  // don't leave extra instructions sitting around.
  for (auto &KV : PerBBDivCache)
    for (Value *V : {KV.second.Quotient, KV.second.Remainder})
      RecursivelyDeleteTriviallyDeadInstructions(V);

  return MadeChange;
}
