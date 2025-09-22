//===- InstCombineCompares.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitICmp and visitFCmp functions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/CmpInstAnalysis.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include <bitset>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

// How many times is a select replaced by one of its operands?
STATISTIC(NumSel, "Number of select opts");


/// Compute Result = In1+In2, returning true if the result overflowed for this
/// type.
static bool addWithOverflow(APInt &Result, const APInt &In1,
                            const APInt &In2, bool IsSigned = false) {
  bool Overflow;
  if (IsSigned)
    Result = In1.sadd_ov(In2, Overflow);
  else
    Result = In1.uadd_ov(In2, Overflow);

  return Overflow;
}

/// Compute Result = In1-In2, returning true if the result overflowed for this
/// type.
static bool subWithOverflow(APInt &Result, const APInt &In1,
                            const APInt &In2, bool IsSigned = false) {
  bool Overflow;
  if (IsSigned)
    Result = In1.ssub_ov(In2, Overflow);
  else
    Result = In1.usub_ov(In2, Overflow);

  return Overflow;
}

/// Given an icmp instruction, return true if any use of this comparison is a
/// branch on sign bit comparison.
static bool hasBranchUse(ICmpInst &I) {
  for (auto *U : I.users())
    if (isa<BranchInst>(U))
      return true;
  return false;
}

/// Returns true if the exploded icmp can be expressed as a signed comparison
/// to zero and updates the predicate accordingly.
/// The signedness of the comparison is preserved.
/// TODO: Refactor with decomposeBitTestICmp()?
static bool isSignTest(ICmpInst::Predicate &Pred, const APInt &C) {
  if (!ICmpInst::isSigned(Pred))
    return false;

  if (C.isZero())
    return ICmpInst::isRelational(Pred);

  if (C.isOne()) {
    if (Pred == ICmpInst::ICMP_SLT) {
      Pred = ICmpInst::ICMP_SLE;
      return true;
    }
  } else if (C.isAllOnes()) {
    if (Pred == ICmpInst::ICMP_SGT) {
      Pred = ICmpInst::ICMP_SGE;
      return true;
    }
  }

  return false;
}

/// This is called when we see this pattern:
///   cmp pred (load (gep GV, ...)), cmpcst
/// where GV is a global variable with a constant initializer. Try to simplify
/// this into some simple computation that does not need the load. For example
/// we can optimize "icmp eq (load (gep "foo", 0, i)), 0" into "icmp eq i, 3".
///
/// If AndCst is non-null, then the loaded value is masked with that constant
/// before doing the comparison. This handles cases like "A[i]&4 == 0".
Instruction *InstCombinerImpl::foldCmpLoadFromIndexedGlobal(
    LoadInst *LI, GetElementPtrInst *GEP, GlobalVariable *GV, CmpInst &ICI,
    ConstantInt *AndCst) {
  if (LI->isVolatile() || LI->getType() != GEP->getResultElementType() ||
      GV->getValueType() != GEP->getSourceElementType() || !GV->isConstant() ||
      !GV->hasDefinitiveInitializer())
    return nullptr;

  Constant *Init = GV->getInitializer();
  if (!isa<ConstantArray>(Init) && !isa<ConstantDataArray>(Init))
    return nullptr;

  uint64_t ArrayElementCount = Init->getType()->getArrayNumElements();
  // Don't blow up on huge arrays.
  if (ArrayElementCount > MaxArraySizeForCombine)
    return nullptr;

  // There are many forms of this optimization we can handle, for now, just do
  // the simple index into a single-dimensional array.
  //
  // Require: GEP GV, 0, i {{, constant indices}}
  if (GEP->getNumOperands() < 3 || !isa<ConstantInt>(GEP->getOperand(1)) ||
      !cast<ConstantInt>(GEP->getOperand(1))->isZero() ||
      isa<Constant>(GEP->getOperand(2)))
    return nullptr;

  // Check that indices after the variable are constants and in-range for the
  // type they index.  Collect the indices.  This is typically for arrays of
  // structs.
  SmallVector<unsigned, 4> LaterIndices;

  Type *EltTy = Init->getType()->getArrayElementType();
  for (unsigned i = 3, e = GEP->getNumOperands(); i != e; ++i) {
    ConstantInt *Idx = dyn_cast<ConstantInt>(GEP->getOperand(i));
    if (!Idx)
      return nullptr; // Variable index.

    uint64_t IdxVal = Idx->getZExtValue();
    if ((unsigned)IdxVal != IdxVal)
      return nullptr; // Too large array index.

    if (StructType *STy = dyn_cast<StructType>(EltTy))
      EltTy = STy->getElementType(IdxVal);
    else if (ArrayType *ATy = dyn_cast<ArrayType>(EltTy)) {
      if (IdxVal >= ATy->getNumElements())
        return nullptr;
      EltTy = ATy->getElementType();
    } else {
      return nullptr; // Unknown type.
    }

    LaterIndices.push_back(IdxVal);
  }

  enum { Overdefined = -3, Undefined = -2 };

  // Variables for our state machines.

  // FirstTrueElement/SecondTrueElement - Used to emit a comparison of the form
  // "i == 47 | i == 87", where 47 is the first index the condition is true for,
  // and 87 is the second (and last) index.  FirstTrueElement is -2 when
  // undefined, otherwise set to the first true element.  SecondTrueElement is
  // -2 when undefined, -3 when overdefined and >= 0 when that index is true.
  int FirstTrueElement = Undefined, SecondTrueElement = Undefined;

  // FirstFalseElement/SecondFalseElement - Used to emit a comparison of the
  // form "i != 47 & i != 87".  Same state transitions as for true elements.
  int FirstFalseElement = Undefined, SecondFalseElement = Undefined;

  /// TrueRangeEnd/FalseRangeEnd - In conjunction with First*Element, these
  /// define a state machine that triggers for ranges of values that the index
  /// is true or false for.  This triggers on things like "abbbbc"[i] == 'b'.
  /// This is -2 when undefined, -3 when overdefined, and otherwise the last
  /// index in the range (inclusive).  We use -2 for undefined here because we
  /// use relative comparisons and don't want 0-1 to match -1.
  int TrueRangeEnd = Undefined, FalseRangeEnd = Undefined;

  // MagicBitvector - This is a magic bitvector where we set a bit if the
  // comparison is true for element 'i'.  If there are 64 elements or less in
  // the array, this will fully represent all the comparison results.
  uint64_t MagicBitvector = 0;

  // Scan the array and see if one of our patterns matches.
  Constant *CompareRHS = cast<Constant>(ICI.getOperand(1));
  for (unsigned i = 0, e = ArrayElementCount; i != e; ++i) {
    Constant *Elt = Init->getAggregateElement(i);
    if (!Elt)
      return nullptr;

    // If this is indexing an array of structures, get the structure element.
    if (!LaterIndices.empty()) {
      Elt = ConstantFoldExtractValueInstruction(Elt, LaterIndices);
      if (!Elt)
        return nullptr;
    }

    // If the element is masked, handle it.
    if (AndCst) {
      Elt = ConstantFoldBinaryOpOperands(Instruction::And, Elt, AndCst, DL);
      if (!Elt)
        return nullptr;
    }

    // Find out if the comparison would be true or false for the i'th element.
    Constant *C = ConstantFoldCompareInstOperands(ICI.getPredicate(), Elt,
                                                  CompareRHS, DL, &TLI);
    if (!C)
      return nullptr;

    // If the result is undef for this element, ignore it.
    if (isa<UndefValue>(C)) {
      // Extend range state machines to cover this element in case there is an
      // undef in the middle of the range.
      if (TrueRangeEnd == (int)i - 1)
        TrueRangeEnd = i;
      if (FalseRangeEnd == (int)i - 1)
        FalseRangeEnd = i;
      continue;
    }

    // If we can't compute the result for any of the elements, we have to give
    // up evaluating the entire conditional.
    if (!isa<ConstantInt>(C))
      return nullptr;

    // Otherwise, we know if the comparison is true or false for this element,
    // update our state machines.
    bool IsTrueForElt = !cast<ConstantInt>(C)->isZero();

    // State machine for single/double/range index comparison.
    if (IsTrueForElt) {
      // Update the TrueElement state machine.
      if (FirstTrueElement == Undefined)
        FirstTrueElement = TrueRangeEnd = i; // First true element.
      else {
        // Update double-compare state machine.
        if (SecondTrueElement == Undefined)
          SecondTrueElement = i;
        else
          SecondTrueElement = Overdefined;

        // Update range state machine.
        if (TrueRangeEnd == (int)i - 1)
          TrueRangeEnd = i;
        else
          TrueRangeEnd = Overdefined;
      }
    } else {
      // Update the FalseElement state machine.
      if (FirstFalseElement == Undefined)
        FirstFalseElement = FalseRangeEnd = i; // First false element.
      else {
        // Update double-compare state machine.
        if (SecondFalseElement == Undefined)
          SecondFalseElement = i;
        else
          SecondFalseElement = Overdefined;

        // Update range state machine.
        if (FalseRangeEnd == (int)i - 1)
          FalseRangeEnd = i;
        else
          FalseRangeEnd = Overdefined;
      }
    }

    // If this element is in range, update our magic bitvector.
    if (i < 64 && IsTrueForElt)
      MagicBitvector |= 1ULL << i;

    // If all of our states become overdefined, bail out early.  Since the
    // predicate is expensive, only check it every 8 elements.  This is only
    // really useful for really huge arrays.
    if ((i & 8) == 0 && i >= 64 && SecondTrueElement == Overdefined &&
        SecondFalseElement == Overdefined && TrueRangeEnd == Overdefined &&
        FalseRangeEnd == Overdefined)
      return nullptr;
  }

  // Now that we've scanned the entire array, emit our new comparison(s).  We
  // order the state machines in complexity of the generated code.
  Value *Idx = GEP->getOperand(2);

  // If the index is larger than the pointer offset size of the target, truncate
  // the index down like the GEP would do implicitly.  We don't have to do this
  // for an inbounds GEP because the index can't be out of range.
  if (!GEP->isInBounds()) {
    Type *PtrIdxTy = DL.getIndexType(GEP->getType());
    unsigned OffsetSize = PtrIdxTy->getIntegerBitWidth();
    if (Idx->getType()->getPrimitiveSizeInBits().getFixedValue() > OffsetSize)
      Idx = Builder.CreateTrunc(Idx, PtrIdxTy);
  }

  // If inbounds keyword is not present, Idx * ElementSize can overflow.
  // Let's assume that ElementSize is 2 and the wanted value is at offset 0.
  // Then, there are two possible values for Idx to match offset 0:
  // 0x00..00, 0x80..00.
  // Emitting 'icmp eq Idx, 0' isn't correct in this case because the
  // comparison is false if Idx was 0x80..00.
  // We need to erase the highest countTrailingZeros(ElementSize) bits of Idx.
  unsigned ElementSize =
      DL.getTypeAllocSize(Init->getType()->getArrayElementType());
  auto MaskIdx = [&](Value *Idx) {
    if (!GEP->isInBounds() && llvm::countr_zero(ElementSize) != 0) {
      Value *Mask = ConstantInt::get(Idx->getType(), -1);
      Mask = Builder.CreateLShr(Mask, llvm::countr_zero(ElementSize));
      Idx = Builder.CreateAnd(Idx, Mask);
    }
    return Idx;
  };

  // If the comparison is only true for one or two elements, emit direct
  // comparisons.
  if (SecondTrueElement != Overdefined) {
    Idx = MaskIdx(Idx);
    // None true -> false.
    if (FirstTrueElement == Undefined)
      return replaceInstUsesWith(ICI, Builder.getFalse());

    Value *FirstTrueIdx = ConstantInt::get(Idx->getType(), FirstTrueElement);

    // True for one element -> 'i == 47'.
    if (SecondTrueElement == Undefined)
      return new ICmpInst(ICmpInst::ICMP_EQ, Idx, FirstTrueIdx);

    // True for two elements -> 'i == 47 | i == 72'.
    Value *C1 = Builder.CreateICmpEQ(Idx, FirstTrueIdx);
    Value *SecondTrueIdx = ConstantInt::get(Idx->getType(), SecondTrueElement);
    Value *C2 = Builder.CreateICmpEQ(Idx, SecondTrueIdx);
    return BinaryOperator::CreateOr(C1, C2);
  }

  // If the comparison is only false for one or two elements, emit direct
  // comparisons.
  if (SecondFalseElement != Overdefined) {
    Idx = MaskIdx(Idx);
    // None false -> true.
    if (FirstFalseElement == Undefined)
      return replaceInstUsesWith(ICI, Builder.getTrue());

    Value *FirstFalseIdx = ConstantInt::get(Idx->getType(), FirstFalseElement);

    // False for one element -> 'i != 47'.
    if (SecondFalseElement == Undefined)
      return new ICmpInst(ICmpInst::ICMP_NE, Idx, FirstFalseIdx);

    // False for two elements -> 'i != 47 & i != 72'.
    Value *C1 = Builder.CreateICmpNE(Idx, FirstFalseIdx);
    Value *SecondFalseIdx =
        ConstantInt::get(Idx->getType(), SecondFalseElement);
    Value *C2 = Builder.CreateICmpNE(Idx, SecondFalseIdx);
    return BinaryOperator::CreateAnd(C1, C2);
  }

  // If the comparison can be replaced with a range comparison for the elements
  // where it is true, emit the range check.
  if (TrueRangeEnd != Overdefined) {
    assert(TrueRangeEnd != FirstTrueElement && "Should emit single compare");
    Idx = MaskIdx(Idx);

    // Generate (i-FirstTrue) <u (TrueRangeEnd-FirstTrue+1).
    if (FirstTrueElement) {
      Value *Offs = ConstantInt::get(Idx->getType(), -FirstTrueElement);
      Idx = Builder.CreateAdd(Idx, Offs);
    }

    Value *End =
        ConstantInt::get(Idx->getType(), TrueRangeEnd - FirstTrueElement + 1);
    return new ICmpInst(ICmpInst::ICMP_ULT, Idx, End);
  }

  // False range check.
  if (FalseRangeEnd != Overdefined) {
    assert(FalseRangeEnd != FirstFalseElement && "Should emit single compare");
    Idx = MaskIdx(Idx);
    // Generate (i-FirstFalse) >u (FalseRangeEnd-FirstFalse).
    if (FirstFalseElement) {
      Value *Offs = ConstantInt::get(Idx->getType(), -FirstFalseElement);
      Idx = Builder.CreateAdd(Idx, Offs);
    }

    Value *End =
        ConstantInt::get(Idx->getType(), FalseRangeEnd - FirstFalseElement);
    return new ICmpInst(ICmpInst::ICMP_UGT, Idx, End);
  }

  // If a magic bitvector captures the entire comparison state
  // of this load, replace it with computation that does:
  //   ((magic_cst >> i) & 1) != 0
  {
    Type *Ty = nullptr;

    // Look for an appropriate type:
    // - The type of Idx if the magic fits
    // - The smallest fitting legal type
    if (ArrayElementCount <= Idx->getType()->getIntegerBitWidth())
      Ty = Idx->getType();
    else
      Ty = DL.getSmallestLegalIntType(Init->getContext(), ArrayElementCount);

    if (Ty) {
      Idx = MaskIdx(Idx);
      Value *V = Builder.CreateIntCast(Idx, Ty, false);
      V = Builder.CreateLShr(ConstantInt::get(Ty, MagicBitvector), V);
      V = Builder.CreateAnd(ConstantInt::get(Ty, 1), V);
      return new ICmpInst(ICmpInst::ICMP_NE, V, ConstantInt::get(Ty, 0));
    }
  }

  return nullptr;
}

/// Returns true if we can rewrite Start as a GEP with pointer Base
/// and some integer offset. The nodes that need to be re-written
/// for this transformation will be added to Explored.
static bool canRewriteGEPAsOffset(Value *Start, Value *Base,
                                  const DataLayout &DL,
                                  SetVector<Value *> &Explored) {
  SmallVector<Value *, 16> WorkList(1, Start);
  Explored.insert(Base);

  // The following traversal gives us an order which can be used
  // when doing the final transformation. Since in the final
  // transformation we create the PHI replacement instructions first,
  // we don't have to get them in any particular order.
  //
  // However, for other instructions we will have to traverse the
  // operands of an instruction first, which means that we have to
  // do a post-order traversal.
  while (!WorkList.empty()) {
    SetVector<PHINode *> PHIs;

    while (!WorkList.empty()) {
      if (Explored.size() >= 100)
        return false;

      Value *V = WorkList.back();

      if (Explored.contains(V)) {
        WorkList.pop_back();
        continue;
      }

      if (!isa<GetElementPtrInst>(V) && !isa<PHINode>(V))
        // We've found some value that we can't explore which is different from
        // the base. Therefore we can't do this transformation.
        return false;

      if (auto *GEP = dyn_cast<GEPOperator>(V)) {
        // Only allow inbounds GEPs with at most one variable offset.
        auto IsNonConst = [](Value *V) { return !isa<ConstantInt>(V); };
        if (!GEP->isInBounds() || count_if(GEP->indices(), IsNonConst) > 1)
          return false;

        if (!Explored.contains(GEP->getOperand(0)))
          WorkList.push_back(GEP->getOperand(0));
      }

      if (WorkList.back() == V) {
        WorkList.pop_back();
        // We've finished visiting this node, mark it as such.
        Explored.insert(V);
      }

      if (auto *PN = dyn_cast<PHINode>(V)) {
        // We cannot transform PHIs on unsplittable basic blocks.
        if (isa<CatchSwitchInst>(PN->getParent()->getTerminator()))
          return false;
        Explored.insert(PN);
        PHIs.insert(PN);
      }
    }

    // Explore the PHI nodes further.
    for (auto *PN : PHIs)
      for (Value *Op : PN->incoming_values())
        if (!Explored.contains(Op))
          WorkList.push_back(Op);
  }

  // Make sure that we can do this. Since we can't insert GEPs in a basic
  // block before a PHI node, we can't easily do this transformation if
  // we have PHI node users of transformed instructions.
  for (Value *Val : Explored) {
    for (Value *Use : Val->uses()) {

      auto *PHI = dyn_cast<PHINode>(Use);
      auto *Inst = dyn_cast<Instruction>(Val);

      if (Inst == Base || Inst == PHI || !Inst || !PHI ||
          !Explored.contains(PHI))
        continue;

      if (PHI->getParent() == Inst->getParent())
        return false;
    }
  }
  return true;
}

// Sets the appropriate insert point on Builder where we can add
// a replacement Instruction for V (if that is possible).
static void setInsertionPoint(IRBuilder<> &Builder, Value *V,
                              bool Before = true) {
  if (auto *PHI = dyn_cast<PHINode>(V)) {
    BasicBlock *Parent = PHI->getParent();
    Builder.SetInsertPoint(Parent, Parent->getFirstInsertionPt());
    return;
  }
  if (auto *I = dyn_cast<Instruction>(V)) {
    if (!Before)
      I = &*std::next(I->getIterator());
    Builder.SetInsertPoint(I);
    return;
  }
  if (auto *A = dyn_cast<Argument>(V)) {
    // Set the insertion point in the entry block.
    BasicBlock &Entry = A->getParent()->getEntryBlock();
    Builder.SetInsertPoint(&Entry, Entry.getFirstInsertionPt());
    return;
  }
  // Otherwise, this is a constant and we don't need to set a new
  // insertion point.
  assert(isa<Constant>(V) && "Setting insertion point for unknown value!");
}

/// Returns a re-written value of Start as an indexed GEP using Base as a
/// pointer.
static Value *rewriteGEPAsOffset(Value *Start, Value *Base,
                                 const DataLayout &DL,
                                 SetVector<Value *> &Explored,
                                 InstCombiner &IC) {
  // Perform all the substitutions. This is a bit tricky because we can
  // have cycles in our use-def chains.
  // 1. Create the PHI nodes without any incoming values.
  // 2. Create all the other values.
  // 3. Add the edges for the PHI nodes.
  // 4. Emit GEPs to get the original pointers.
  // 5. Remove the original instructions.
  Type *IndexType = IntegerType::get(
      Base->getContext(), DL.getIndexTypeSizeInBits(Start->getType()));

  DenseMap<Value *, Value *> NewInsts;
  NewInsts[Base] = ConstantInt::getNullValue(IndexType);

  // Create the new PHI nodes, without adding any incoming values.
  for (Value *Val : Explored) {
    if (Val == Base)
      continue;
    // Create empty phi nodes. This avoids cyclic dependencies when creating
    // the remaining instructions.
    if (auto *PHI = dyn_cast<PHINode>(Val))
      NewInsts[PHI] =
          PHINode::Create(IndexType, PHI->getNumIncomingValues(),
                          PHI->getName() + ".idx", PHI->getIterator());
  }
  IRBuilder<> Builder(Base->getContext());

  // Create all the other instructions.
  for (Value *Val : Explored) {
    if (NewInsts.contains(Val))
      continue;

    if (auto *GEP = dyn_cast<GEPOperator>(Val)) {
      setInsertionPoint(Builder, GEP);
      Value *Op = NewInsts[GEP->getOperand(0)];
      Value *OffsetV = emitGEPOffset(&Builder, DL, GEP);
      if (isa<ConstantInt>(Op) && cast<ConstantInt>(Op)->isZero())
        NewInsts[GEP] = OffsetV;
      else
        NewInsts[GEP] = Builder.CreateNSWAdd(
            Op, OffsetV, GEP->getOperand(0)->getName() + ".add");
      continue;
    }
    if (isa<PHINode>(Val))
      continue;

    llvm_unreachable("Unexpected instruction type");
  }

  // Add the incoming values to the PHI nodes.
  for (Value *Val : Explored) {
    if (Val == Base)
      continue;
    // All the instructions have been created, we can now add edges to the
    // phi nodes.
    if (auto *PHI = dyn_cast<PHINode>(Val)) {
      PHINode *NewPhi = static_cast<PHINode *>(NewInsts[PHI]);
      for (unsigned I = 0, E = PHI->getNumIncomingValues(); I < E; ++I) {
        Value *NewIncoming = PHI->getIncomingValue(I);

        if (NewInsts.contains(NewIncoming))
          NewIncoming = NewInsts[NewIncoming];

        NewPhi->addIncoming(NewIncoming, PHI->getIncomingBlock(I));
      }
    }
  }

  for (Value *Val : Explored) {
    if (Val == Base)
      continue;

    setInsertionPoint(Builder, Val, false);
    // Create GEP for external users.
    Value *NewVal = Builder.CreateInBoundsGEP(
        Builder.getInt8Ty(), Base, NewInsts[Val], Val->getName() + ".ptr");
    IC.replaceInstUsesWith(*cast<Instruction>(Val), NewVal);
    // Add old instruction to worklist for DCE. We don't directly remove it
    // here because the original compare is one of the users.
    IC.addToWorklist(cast<Instruction>(Val));
  }

  return NewInsts[Start];
}

/// Converts (CMP GEPLHS, RHS) if this change would make RHS a constant.
/// We can look through PHIs, GEPs and casts in order to determine a common base
/// between GEPLHS and RHS.
static Instruction *transformToIndexedCompare(GEPOperator *GEPLHS, Value *RHS,
                                              ICmpInst::Predicate Cond,
                                              const DataLayout &DL,
                                              InstCombiner &IC) {
  // FIXME: Support vector of pointers.
  if (GEPLHS->getType()->isVectorTy())
    return nullptr;

  if (!GEPLHS->hasAllConstantIndices())
    return nullptr;

  APInt Offset(DL.getIndexTypeSizeInBits(GEPLHS->getType()), 0);
  Value *PtrBase =
      GEPLHS->stripAndAccumulateConstantOffsets(DL, Offset,
                                                /*AllowNonInbounds*/ false);

  // Bail if we looked through addrspacecast.
  if (PtrBase->getType() != GEPLHS->getType())
    return nullptr;

  // The set of nodes that will take part in this transformation.
  SetVector<Value *> Nodes;

  if (!canRewriteGEPAsOffset(RHS, PtrBase, DL, Nodes))
    return nullptr;

  // We know we can re-write this as
  //  ((gep Ptr, OFFSET1) cmp (gep Ptr, OFFSET2)
  // Since we've only looked through inbouds GEPs we know that we
  // can't have overflow on either side. We can therefore re-write
  // this as:
  //   OFFSET1 cmp OFFSET2
  Value *NewRHS = rewriteGEPAsOffset(RHS, PtrBase, DL, Nodes, IC);

  // RewriteGEPAsOffset has replaced RHS and all of its uses with a re-written
  // GEP having PtrBase as the pointer base, and has returned in NewRHS the
  // offset. Since Index is the offset of LHS to the base pointer, we will now
  // compare the offsets instead of comparing the pointers.
  return new ICmpInst(ICmpInst::getSignedPredicate(Cond),
                      IC.Builder.getInt(Offset), NewRHS);
}

/// Fold comparisons between a GEP instruction and something else. At this point
/// we know that the GEP is on the LHS of the comparison.
Instruction *InstCombinerImpl::foldGEPICmp(GEPOperator *GEPLHS, Value *RHS,
                                           ICmpInst::Predicate Cond,
                                           Instruction &I) {
  // Don't transform signed compares of GEPs into index compares. Even if the
  // GEP is inbounds, the final add of the base pointer can have signed overflow
  // and would change the result of the icmp.
  // e.g. "&foo[0] <s &foo[1]" can't be folded to "true" because "foo" could be
  // the maximum signed value for the pointer type.
  if (ICmpInst::isSigned(Cond))
    return nullptr;

  // Look through bitcasts and addrspacecasts. We do not however want to remove
  // 0 GEPs.
  if (!isa<GetElementPtrInst>(RHS))
    RHS = RHS->stripPointerCasts();

  Value *PtrBase = GEPLHS->getOperand(0);
  if (PtrBase == RHS && (GEPLHS->isInBounds() || ICmpInst::isEquality(Cond))) {
    // ((gep Ptr, OFFSET) cmp Ptr)   ---> (OFFSET cmp 0).
    Value *Offset = EmitGEPOffset(GEPLHS);
    return new ICmpInst(ICmpInst::getSignedPredicate(Cond), Offset,
                        Constant::getNullValue(Offset->getType()));
  }

  if (GEPLHS->isInBounds() && ICmpInst::isEquality(Cond) &&
      isa<Constant>(RHS) && cast<Constant>(RHS)->isNullValue() &&
      !NullPointerIsDefined(I.getFunction(),
                            RHS->getType()->getPointerAddressSpace())) {
    // For most address spaces, an allocation can't be placed at null, but null
    // itself is treated as a 0 size allocation in the in bounds rules.  Thus,
    // the only valid inbounds address derived from null, is null itself.
    // Thus, we have four cases to consider:
    // 1) Base == nullptr, Offset == 0 -> inbounds, null
    // 2) Base == nullptr, Offset != 0 -> poison as the result is out of bounds
    // 3) Base != nullptr, Offset == (-base) -> poison (crossing allocations)
    // 4) Base != nullptr, Offset != (-base) -> nonnull (and possibly poison)
    //
    // (Note if we're indexing a type of size 0, that simply collapses into one
    //  of the buckets above.)
    //
    // In general, we're allowed to make values less poison (i.e. remove
    //   sources of full UB), so in this case, we just select between the two
    //   non-poison cases (1 and 4 above).
    //
    // For vectors, we apply the same reasoning on a per-lane basis.
    auto *Base = GEPLHS->getPointerOperand();
    if (GEPLHS->getType()->isVectorTy() && Base->getType()->isPointerTy()) {
      auto EC = cast<VectorType>(GEPLHS->getType())->getElementCount();
      Base = Builder.CreateVectorSplat(EC, Base);
    }
    return new ICmpInst(Cond, Base,
                        ConstantExpr::getPointerBitCastOrAddrSpaceCast(
                            cast<Constant>(RHS), Base->getType()));
  } else if (GEPOperator *GEPRHS = dyn_cast<GEPOperator>(RHS)) {
    // If the base pointers are different, but the indices are the same, just
    // compare the base pointer.
    if (PtrBase != GEPRHS->getOperand(0)) {
      bool IndicesTheSame =
          GEPLHS->getNumOperands() == GEPRHS->getNumOperands() &&
          GEPLHS->getPointerOperand()->getType() ==
              GEPRHS->getPointerOperand()->getType() &&
          GEPLHS->getSourceElementType() == GEPRHS->getSourceElementType();
      if (IndicesTheSame)
        for (unsigned i = 1, e = GEPLHS->getNumOperands(); i != e; ++i)
          if (GEPLHS->getOperand(i) != GEPRHS->getOperand(i)) {
            IndicesTheSame = false;
            break;
          }

      // If all indices are the same, just compare the base pointers.
      Type *BaseType = GEPLHS->getOperand(0)->getType();
      if (IndicesTheSame && CmpInst::makeCmpResultType(BaseType) == I.getType())
        return new ICmpInst(Cond, GEPLHS->getOperand(0), GEPRHS->getOperand(0));

      // If we're comparing GEPs with two base pointers that only differ in type
      // and both GEPs have only constant indices or just one use, then fold
      // the compare with the adjusted indices.
      // FIXME: Support vector of pointers.
      if (GEPLHS->isInBounds() && GEPRHS->isInBounds() &&
          (GEPLHS->hasAllConstantIndices() || GEPLHS->hasOneUse()) &&
          (GEPRHS->hasAllConstantIndices() || GEPRHS->hasOneUse()) &&
          PtrBase->stripPointerCasts() ==
              GEPRHS->getOperand(0)->stripPointerCasts() &&
          !GEPLHS->getType()->isVectorTy()) {
        Value *LOffset = EmitGEPOffset(GEPLHS);
        Value *ROffset = EmitGEPOffset(GEPRHS);

        // If we looked through an addrspacecast between different sized address
        // spaces, the LHS and RHS pointers are different sized
        // integers. Truncate to the smaller one.
        Type *LHSIndexTy = LOffset->getType();
        Type *RHSIndexTy = ROffset->getType();
        if (LHSIndexTy != RHSIndexTy) {
          if (LHSIndexTy->getPrimitiveSizeInBits().getFixedValue() <
              RHSIndexTy->getPrimitiveSizeInBits().getFixedValue()) {
            ROffset = Builder.CreateTrunc(ROffset, LHSIndexTy);
          } else
            LOffset = Builder.CreateTrunc(LOffset, RHSIndexTy);
        }

        Value *Cmp = Builder.CreateICmp(ICmpInst::getSignedPredicate(Cond),
                                        LOffset, ROffset);
        return replaceInstUsesWith(I, Cmp);
      }

      // Otherwise, the base pointers are different and the indices are
      // different. Try convert this to an indexed compare by looking through
      // PHIs/casts.
      return transformToIndexedCompare(GEPLHS, RHS, Cond, DL, *this);
    }

    bool GEPsInBounds = GEPLHS->isInBounds() && GEPRHS->isInBounds();
    if (GEPLHS->getNumOperands() == GEPRHS->getNumOperands() &&
        GEPLHS->getSourceElementType() == GEPRHS->getSourceElementType()) {
      // If the GEPs only differ by one index, compare it.
      unsigned NumDifferences = 0;  // Keep track of # differences.
      unsigned DiffOperand = 0;     // The operand that differs.
      for (unsigned i = 1, e = GEPRHS->getNumOperands(); i != e; ++i)
        if (GEPLHS->getOperand(i) != GEPRHS->getOperand(i)) {
          Type *LHSType = GEPLHS->getOperand(i)->getType();
          Type *RHSType = GEPRHS->getOperand(i)->getType();
          // FIXME: Better support for vector of pointers.
          if (LHSType->getPrimitiveSizeInBits() !=
                   RHSType->getPrimitiveSizeInBits() ||
              (GEPLHS->getType()->isVectorTy() &&
               (!LHSType->isVectorTy() || !RHSType->isVectorTy()))) {
            // Irreconcilable differences.
            NumDifferences = 2;
            break;
          }

          if (NumDifferences++) break;
          DiffOperand = i;
        }

      if (NumDifferences == 0)   // SAME GEP?
        return replaceInstUsesWith(I, // No comparison is needed here.
          ConstantInt::get(I.getType(), ICmpInst::isTrueWhenEqual(Cond)));

      else if (NumDifferences == 1 && GEPsInBounds) {
        Value *LHSV = GEPLHS->getOperand(DiffOperand);
        Value *RHSV = GEPRHS->getOperand(DiffOperand);
        // Make sure we do a signed comparison here.
        return new ICmpInst(ICmpInst::getSignedPredicate(Cond), LHSV, RHSV);
      }
    }

    if (GEPsInBounds || CmpInst::isEquality(Cond)) {
      // ((gep Ptr, OFFSET1) cmp (gep Ptr, OFFSET2)  --->  (OFFSET1 cmp OFFSET2)
      Value *L = EmitGEPOffset(GEPLHS, /*RewriteGEP=*/true);
      Value *R = EmitGEPOffset(GEPRHS, /*RewriteGEP=*/true);
      return new ICmpInst(ICmpInst::getSignedPredicate(Cond), L, R);
    }
  }

  // Try convert this to an indexed compare by looking through PHIs/casts as a
  // last resort.
  return transformToIndexedCompare(GEPLHS, RHS, Cond, DL, *this);
}

bool InstCombinerImpl::foldAllocaCmp(AllocaInst *Alloca) {
  // It would be tempting to fold away comparisons between allocas and any
  // pointer not based on that alloca (e.g. an argument). However, even
  // though such pointers cannot alias, they can still compare equal.
  //
  // But LLVM doesn't specify where allocas get their memory, so if the alloca
  // doesn't escape we can argue that it's impossible to guess its value, and we
  // can therefore act as if any such guesses are wrong.
  //
  // However, we need to ensure that this folding is consistent: We can't fold
  // one comparison to false, and then leave a different comparison against the
  // same value alone (as it might evaluate to true at runtime, leading to a
  // contradiction). As such, this code ensures that all comparisons are folded
  // at the same time, and there are no other escapes.

  struct CmpCaptureTracker : public CaptureTracker {
    AllocaInst *Alloca;
    bool Captured = false;
    /// The value of the map is a bit mask of which icmp operands the alloca is
    /// used in.
    SmallMapVector<ICmpInst *, unsigned, 4> ICmps;

    CmpCaptureTracker(AllocaInst *Alloca) : Alloca(Alloca) {}

    void tooManyUses() override { Captured = true; }

    bool captured(const Use *U) override {
      auto *ICmp = dyn_cast<ICmpInst>(U->getUser());
      // We need to check that U is based *only* on the alloca, and doesn't
      // have other contributions from a select/phi operand.
      // TODO: We could check whether getUnderlyingObjects() reduces to one
      // object, which would allow looking through phi nodes.
      if (ICmp && ICmp->isEquality() && getUnderlyingObject(*U) == Alloca) {
        // Collect equality icmps of the alloca, and don't treat them as
        // captures.
        auto Res = ICmps.insert({ICmp, 0});
        Res.first->second |= 1u << U->getOperandNo();
        return false;
      }

      Captured = true;
      return true;
    }
  };

  CmpCaptureTracker Tracker(Alloca);
  PointerMayBeCaptured(Alloca, &Tracker);
  if (Tracker.Captured)
    return false;

  bool Changed = false;
  for (auto [ICmp, Operands] : Tracker.ICmps) {
    switch (Operands) {
    case 1:
    case 2: {
      // The alloca is only used in one icmp operand. Assume that the
      // equality is false.
      auto *Res = ConstantInt::get(
          ICmp->getType(), ICmp->getPredicate() == ICmpInst::ICMP_NE);
      replaceInstUsesWith(*ICmp, Res);
      eraseInstFromFunction(*ICmp);
      Changed = true;
      break;
    }
    case 3:
      // Both icmp operands are based on the alloca, so this is comparing
      // pointer offsets, without leaking any information about the address
      // of the alloca. Ignore such comparisons.
      break;
    default:
      llvm_unreachable("Cannot happen");
    }
  }

  return Changed;
}

/// Fold "icmp pred (X+C), X".
Instruction *InstCombinerImpl::foldICmpAddOpConst(Value *X, const APInt &C,
                                                  ICmpInst::Predicate Pred) {
  // From this point on, we know that (X+C <= X) --> (X+C < X) because C != 0,
  // so the values can never be equal.  Similarly for all other "or equals"
  // operators.
  assert(!!C && "C should not be zero!");

  // (X+1) <u X        --> X >u (MAXUINT-1)        --> X == 255
  // (X+2) <u X        --> X >u (MAXUINT-2)        --> X > 253
  // (X+MAXUINT) <u X  --> X >u (MAXUINT-MAXUINT)  --> X != 0
  if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE) {
    Constant *R = ConstantInt::get(X->getType(),
                                   APInt::getMaxValue(C.getBitWidth()) - C);
    return new ICmpInst(ICmpInst::ICMP_UGT, X, R);
  }

  // (X+1) >u X        --> X <u (0-1)        --> X != 255
  // (X+2) >u X        --> X <u (0-2)        --> X <u 254
  // (X+MAXUINT) >u X  --> X <u (0-MAXUINT)  --> X <u 1  --> X == 0
  if (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE)
    return new ICmpInst(ICmpInst::ICMP_ULT, X,
                        ConstantInt::get(X->getType(), -C));

  APInt SMax = APInt::getSignedMaxValue(C.getBitWidth());

  // (X+ 1) <s X       --> X >s (MAXSINT-1)          --> X == 127
  // (X+ 2) <s X       --> X >s (MAXSINT-2)          --> X >s 125
  // (X+MAXSINT) <s X  --> X >s (MAXSINT-MAXSINT)    --> X >s 0
  // (X+MINSINT) <s X  --> X >s (MAXSINT-MINSINT)    --> X >s -1
  // (X+ -2) <s X      --> X >s (MAXSINT- -2)        --> X >s 126
  // (X+ -1) <s X      --> X >s (MAXSINT- -1)        --> X != 127
  if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE)
    return new ICmpInst(ICmpInst::ICMP_SGT, X,
                        ConstantInt::get(X->getType(), SMax - C));

  // (X+ 1) >s X       --> X <s (MAXSINT-(1-1))       --> X != 127
  // (X+ 2) >s X       --> X <s (MAXSINT-(2-1))       --> X <s 126
  // (X+MAXSINT) >s X  --> X <s (MAXSINT-(MAXSINT-1)) --> X <s 1
  // (X+MINSINT) >s X  --> X <s (MAXSINT-(MINSINT-1)) --> X <s -2
  // (X+ -2) >s X      --> X <s (MAXSINT-(-2-1))      --> X <s -126
  // (X+ -1) >s X      --> X <s (MAXSINT-(-1-1))      --> X == -128

  assert(Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE);
  return new ICmpInst(ICmpInst::ICMP_SLT, X,
                      ConstantInt::get(X->getType(), SMax - (C - 1)));
}

/// Handle "(icmp eq/ne (ashr/lshr AP2, A), AP1)" ->
/// (icmp eq/ne A, Log2(AP2/AP1)) ->
/// (icmp eq/ne A, Log2(AP2) - Log2(AP1)).
Instruction *InstCombinerImpl::foldICmpShrConstConst(ICmpInst &I, Value *A,
                                                     const APInt &AP1,
                                                     const APInt &AP2) {
  assert(I.isEquality() && "Cannot fold icmp gt/lt");

  auto getICmp = [&I](CmpInst::Predicate Pred, Value *LHS, Value *RHS) {
    if (I.getPredicate() == I.ICMP_NE)
      Pred = CmpInst::getInversePredicate(Pred);
    return new ICmpInst(Pred, LHS, RHS);
  };

  // Don't bother doing any work for cases which InstSimplify handles.
  if (AP2.isZero())
    return nullptr;

  bool IsAShr = isa<AShrOperator>(I.getOperand(0));
  if (IsAShr) {
    if (AP2.isAllOnes())
      return nullptr;
    if (AP2.isNegative() != AP1.isNegative())
      return nullptr;
    if (AP2.sgt(AP1))
      return nullptr;
  }

  if (!AP1)
    // 'A' must be large enough to shift out the highest set bit.
    return getICmp(I.ICMP_UGT, A,
                   ConstantInt::get(A->getType(), AP2.logBase2()));

  if (AP1 == AP2)
    return getICmp(I.ICMP_EQ, A, ConstantInt::getNullValue(A->getType()));

  int Shift;
  if (IsAShr && AP1.isNegative())
    Shift = AP1.countl_one() - AP2.countl_one();
  else
    Shift = AP1.countl_zero() - AP2.countl_zero();

  if (Shift > 0) {
    if (IsAShr && AP1 == AP2.ashr(Shift)) {
      // There are multiple solutions if we are comparing against -1 and the LHS
      // of the ashr is not a power of two.
      if (AP1.isAllOnes() && !AP2.isPowerOf2())
        return getICmp(I.ICMP_UGE, A, ConstantInt::get(A->getType(), Shift));
      return getICmp(I.ICMP_EQ, A, ConstantInt::get(A->getType(), Shift));
    } else if (AP1 == AP2.lshr(Shift)) {
      return getICmp(I.ICMP_EQ, A, ConstantInt::get(A->getType(), Shift));
    }
  }

  // Shifting const2 will never be equal to const1.
  // FIXME: This should always be handled by InstSimplify?
  auto *TorF = ConstantInt::get(I.getType(), I.getPredicate() == I.ICMP_NE);
  return replaceInstUsesWith(I, TorF);
}

/// Handle "(icmp eq/ne (shl AP2, A), AP1)" ->
/// (icmp eq/ne A, TrailingZeros(AP1) - TrailingZeros(AP2)).
Instruction *InstCombinerImpl::foldICmpShlConstConst(ICmpInst &I, Value *A,
                                                     const APInt &AP1,
                                                     const APInt &AP2) {
  assert(I.isEquality() && "Cannot fold icmp gt/lt");

  auto getICmp = [&I](CmpInst::Predicate Pred, Value *LHS, Value *RHS) {
    if (I.getPredicate() == I.ICMP_NE)
      Pred = CmpInst::getInversePredicate(Pred);
    return new ICmpInst(Pred, LHS, RHS);
  };

  // Don't bother doing any work for cases which InstSimplify handles.
  if (AP2.isZero())
    return nullptr;

  unsigned AP2TrailingZeros = AP2.countr_zero();

  if (!AP1 && AP2TrailingZeros != 0)
    return getICmp(
        I.ICMP_UGE, A,
        ConstantInt::get(A->getType(), AP2.getBitWidth() - AP2TrailingZeros));

  if (AP1 == AP2)
    return getICmp(I.ICMP_EQ, A, ConstantInt::getNullValue(A->getType()));

  // Get the distance between the lowest bits that are set.
  int Shift = AP1.countr_zero() - AP2TrailingZeros;

  if (Shift > 0 && AP2.shl(Shift) == AP1)
    return getICmp(I.ICMP_EQ, A, ConstantInt::get(A->getType(), Shift));

  // Shifting const2 will never be equal to const1.
  // FIXME: This should always be handled by InstSimplify?
  auto *TorF = ConstantInt::get(I.getType(), I.getPredicate() == I.ICMP_NE);
  return replaceInstUsesWith(I, TorF);
}

/// The caller has matched a pattern of the form:
///   I = icmp ugt (add (add A, B), CI2), CI1
/// If this is of the form:
///   sum = a + b
///   if (sum+128 >u 255)
/// Then replace it with llvm.sadd.with.overflow.i8.
///
static Instruction *processUGT_ADDCST_ADD(ICmpInst &I, Value *A, Value *B,
                                          ConstantInt *CI2, ConstantInt *CI1,
                                          InstCombinerImpl &IC) {
  // The transformation we're trying to do here is to transform this into an
  // llvm.sadd.with.overflow.  To do this, we have to replace the original add
  // with a narrower add, and discard the add-with-constant that is part of the
  // range check (if we can't eliminate it, this isn't profitable).

  // In order to eliminate the add-with-constant, the compare can be its only
  // use.
  Instruction *AddWithCst = cast<Instruction>(I.getOperand(0));
  if (!AddWithCst->hasOneUse())
    return nullptr;

  // If CI2 is 2^7, 2^15, 2^31, then it might be an sadd.with.overflow.
  if (!CI2->getValue().isPowerOf2())
    return nullptr;
  unsigned NewWidth = CI2->getValue().countr_zero();
  if (NewWidth != 7 && NewWidth != 15 && NewWidth != 31)
    return nullptr;

  // The width of the new add formed is 1 more than the bias.
  ++NewWidth;

  // Check to see that CI1 is an all-ones value with NewWidth bits.
  if (CI1->getBitWidth() == NewWidth ||
      CI1->getValue() != APInt::getLowBitsSet(CI1->getBitWidth(), NewWidth))
    return nullptr;

  // This is only really a signed overflow check if the inputs have been
  // sign-extended; check for that condition. For example, if CI2 is 2^31 and
  // the operands of the add are 64 bits wide, we need at least 33 sign bits.
  if (IC.ComputeMaxSignificantBits(A, 0, &I) > NewWidth ||
      IC.ComputeMaxSignificantBits(B, 0, &I) > NewWidth)
    return nullptr;

  // In order to replace the original add with a narrower
  // llvm.sadd.with.overflow, the only uses allowed are the add-with-constant
  // and truncates that discard the high bits of the add.  Verify that this is
  // the case.
  Instruction *OrigAdd = cast<Instruction>(AddWithCst->getOperand(0));
  for (User *U : OrigAdd->users()) {
    if (U == AddWithCst)
      continue;

    // Only accept truncates for now.  We would really like a nice recursive
    // predicate like SimplifyDemandedBits, but which goes downwards the use-def
    // chain to see which bits of a value are actually demanded.  If the
    // original add had another add which was then immediately truncated, we
    // could still do the transformation.
    TruncInst *TI = dyn_cast<TruncInst>(U);
    if (!TI || TI->getType()->getPrimitiveSizeInBits() > NewWidth)
      return nullptr;
  }

  // If the pattern matches, truncate the inputs to the narrower type and
  // use the sadd_with_overflow intrinsic to efficiently compute both the
  // result and the overflow bit.
  Type *NewType = IntegerType::get(OrigAdd->getContext(), NewWidth);
  Function *F = Intrinsic::getDeclaration(
      I.getModule(), Intrinsic::sadd_with_overflow, NewType);

  InstCombiner::BuilderTy &Builder = IC.Builder;

  // Put the new code above the original add, in case there are any uses of the
  // add between the add and the compare.
  Builder.SetInsertPoint(OrigAdd);

  Value *TruncA = Builder.CreateTrunc(A, NewType, A->getName() + ".trunc");
  Value *TruncB = Builder.CreateTrunc(B, NewType, B->getName() + ".trunc");
  CallInst *Call = Builder.CreateCall(F, {TruncA, TruncB}, "sadd");
  Value *Add = Builder.CreateExtractValue(Call, 0, "sadd.result");
  Value *ZExt = Builder.CreateZExt(Add, OrigAdd->getType());

  // The inner add was the result of the narrow add, zero extended to the
  // wider type.  Replace it with the result computed by the intrinsic.
  IC.replaceInstUsesWith(*OrigAdd, ZExt);
  IC.eraseInstFromFunction(*OrigAdd);

  // The original icmp gets replaced with the overflow value.
  return ExtractValueInst::Create(Call, 1, "sadd.overflow");
}

/// If we have:
///   icmp eq/ne (urem/srem %x, %y), 0
/// iff %y is a power-of-two, we can replace this with a bit test:
///   icmp eq/ne (and %x, (add %y, -1)), 0
Instruction *InstCombinerImpl::foldIRemByPowerOfTwoToBitTest(ICmpInst &I) {
  // This fold is only valid for equality predicates.
  if (!I.isEquality())
    return nullptr;
  ICmpInst::Predicate Pred;
  Value *X, *Y, *Zero;
  if (!match(&I, m_ICmp(Pred, m_OneUse(m_IRem(m_Value(X), m_Value(Y))),
                        m_CombineAnd(m_Zero(), m_Value(Zero)))))
    return nullptr;
  if (!isKnownToBeAPowerOfTwo(Y, /*OrZero*/ true, 0, &I))
    return nullptr;
  // This may increase instruction count, we don't enforce that Y is a constant.
  Value *Mask = Builder.CreateAdd(Y, Constant::getAllOnesValue(Y->getType()));
  Value *Masked = Builder.CreateAnd(X, Mask);
  return ICmpInst::Create(Instruction::ICmp, Pred, Masked, Zero);
}

/// Fold equality-comparison between zero and any (maybe truncated) right-shift
/// by one-less-than-bitwidth into a sign test on the original value.
Instruction *InstCombinerImpl::foldSignBitTest(ICmpInst &I) {
  Instruction *Val;
  ICmpInst::Predicate Pred;
  if (!I.isEquality() || !match(&I, m_ICmp(Pred, m_Instruction(Val), m_Zero())))
    return nullptr;

  Value *X;
  Type *XTy;

  Constant *C;
  if (match(Val, m_TruncOrSelf(m_Shr(m_Value(X), m_Constant(C))))) {
    XTy = X->getType();
    unsigned XBitWidth = XTy->getScalarSizeInBits();
    if (!match(C, m_SpecificInt_ICMP(ICmpInst::Predicate::ICMP_EQ,
                                     APInt(XBitWidth, XBitWidth - 1))))
      return nullptr;
  } else if (isa<BinaryOperator>(Val) &&
             (X = reassociateShiftAmtsOfTwoSameDirectionShifts(
                  cast<BinaryOperator>(Val), SQ.getWithInstruction(Val),
                  /*AnalyzeForSignBitExtraction=*/true))) {
    XTy = X->getType();
  } else
    return nullptr;

  return ICmpInst::Create(Instruction::ICmp,
                          Pred == ICmpInst::ICMP_EQ ? ICmpInst::ICMP_SGE
                                                    : ICmpInst::ICMP_SLT,
                          X, ConstantInt::getNullValue(XTy));
}

// Handle  icmp pred X, 0
Instruction *InstCombinerImpl::foldICmpWithZero(ICmpInst &Cmp) {
  CmpInst::Predicate Pred = Cmp.getPredicate();
  if (!match(Cmp.getOperand(1), m_Zero()))
    return nullptr;

  // (icmp sgt smin(PosA, B) 0) -> (icmp sgt B 0)
  if (Pred == ICmpInst::ICMP_SGT) {
    Value *A, *B;
    if (match(Cmp.getOperand(0), m_SMin(m_Value(A), m_Value(B)))) {
      if (isKnownPositive(A, SQ.getWithInstruction(&Cmp)))
        return new ICmpInst(Pred, B, Cmp.getOperand(1));
      if (isKnownPositive(B, SQ.getWithInstruction(&Cmp)))
        return new ICmpInst(Pred, A, Cmp.getOperand(1));
    }
  }

  if (Instruction *New = foldIRemByPowerOfTwoToBitTest(Cmp))
    return New;

  // Given:
  //   icmp eq/ne (urem %x, %y), 0
  // Iff %x has 0 or 1 bits set, and %y has at least 2 bits set, omit 'urem':
  //   icmp eq/ne %x, 0
  Value *X, *Y;
  if (match(Cmp.getOperand(0), m_URem(m_Value(X), m_Value(Y))) &&
      ICmpInst::isEquality(Pred)) {
    KnownBits XKnown = computeKnownBits(X, 0, &Cmp);
    KnownBits YKnown = computeKnownBits(Y, 0, &Cmp);
    if (XKnown.countMaxPopulation() == 1 && YKnown.countMinPopulation() >= 2)
      return new ICmpInst(Pred, X, Cmp.getOperand(1));
  }

  // (icmp eq/ne (mul X Y)) -> (icmp eq/ne X/Y) if we know about whether X/Y are
  // odd/non-zero/there is no overflow.
  if (match(Cmp.getOperand(0), m_Mul(m_Value(X), m_Value(Y))) &&
      ICmpInst::isEquality(Pred)) {

    KnownBits XKnown = computeKnownBits(X, 0, &Cmp);
    // if X % 2 != 0
    //    (icmp eq/ne Y)
    if (XKnown.countMaxTrailingZeros() == 0)
      return new ICmpInst(Pred, Y, Cmp.getOperand(1));

    KnownBits YKnown = computeKnownBits(Y, 0, &Cmp);
    // if Y % 2 != 0
    //    (icmp eq/ne X)
    if (YKnown.countMaxTrailingZeros() == 0)
      return new ICmpInst(Pred, X, Cmp.getOperand(1));

    auto *BO0 = cast<OverflowingBinaryOperator>(Cmp.getOperand(0));
    if (BO0->hasNoUnsignedWrap() || BO0->hasNoSignedWrap()) {
      const SimplifyQuery Q = SQ.getWithInstruction(&Cmp);
      // `isKnownNonZero` does more analysis than just `!KnownBits.One.isZero()`
      // but to avoid unnecessary work, first just if this is an obvious case.

      // if X non-zero and NoOverflow(X * Y)
      //    (icmp eq/ne Y)
      if (!XKnown.One.isZero() || isKnownNonZero(X, Q))
        return new ICmpInst(Pred, Y, Cmp.getOperand(1));

      // if Y non-zero and NoOverflow(X * Y)
      //    (icmp eq/ne X)
      if (!YKnown.One.isZero() || isKnownNonZero(Y, Q))
        return new ICmpInst(Pred, X, Cmp.getOperand(1));
    }
    // Note, we are skipping cases:
    //      if Y % 2 != 0 AND X % 2 != 0
    //          (false/true)
    //      if X non-zero and Y non-zero and NoOverflow(X * Y)
    //          (false/true)
    // Those can be simplified later as we would have already replaced the (icmp
    // eq/ne (mul X, Y)) with (icmp eq/ne X/Y) and if X/Y is known non-zero that
    // will fold to a constant elsewhere.
  }
  return nullptr;
}

/// Fold icmp Pred X, C.
/// TODO: This code structure does not make sense. The saturating add fold
/// should be moved to some other helper and extended as noted below (it is also
/// possible that code has been made unnecessary - do we canonicalize IR to
/// overflow/saturating intrinsics or not?).
Instruction *InstCombinerImpl::foldICmpWithConstant(ICmpInst &Cmp) {
  // Match the following pattern, which is a common idiom when writing
  // overflow-safe integer arithmetic functions. The source performs an addition
  // in wider type and explicitly checks for overflow using comparisons against
  // INT_MIN and INT_MAX. Simplify by using the sadd_with_overflow intrinsic.
  //
  // TODO: This could probably be generalized to handle other overflow-safe
  // operations if we worked out the formulas to compute the appropriate magic
  // constants.
  //
  // sum = a + b
  // if (sum+128 >u 255)  ...  -> llvm.sadd.with.overflow.i8
  CmpInst::Predicate Pred = Cmp.getPredicate();
  Value *Op0 = Cmp.getOperand(0), *Op1 = Cmp.getOperand(1);
  Value *A, *B;
  ConstantInt *CI, *CI2; // I = icmp ugt (add (add A, B), CI2), CI
  if (Pred == ICmpInst::ICMP_UGT && match(Op1, m_ConstantInt(CI)) &&
      match(Op0, m_Add(m_Add(m_Value(A), m_Value(B)), m_ConstantInt(CI2))))
    if (Instruction *Res = processUGT_ADDCST_ADD(Cmp, A, B, CI2, CI, *this))
      return Res;

  // icmp(phi(C1, C2, ...), C) -> phi(icmp(C1, C), icmp(C2, C), ...).
  Constant *C = dyn_cast<Constant>(Op1);
  if (!C)
    return nullptr;

  if (auto *Phi = dyn_cast<PHINode>(Op0))
    if (all_of(Phi->operands(), [](Value *V) { return isa<Constant>(V); })) {
      SmallVector<Constant *> Ops;
      for (Value *V : Phi->incoming_values()) {
        Constant *Res =
            ConstantFoldCompareInstOperands(Pred, cast<Constant>(V), C, DL);
        if (!Res)
          return nullptr;
        Ops.push_back(Res);
      }
      Builder.SetInsertPoint(Phi);
      PHINode *NewPhi = Builder.CreatePHI(Cmp.getType(), Phi->getNumOperands());
      for (auto [V, Pred] : zip(Ops, Phi->blocks()))
        NewPhi->addIncoming(V, Pred);
      return replaceInstUsesWith(Cmp, NewPhi);
    }

  if (Instruction *R = tryFoldInstWithCtpopWithNot(&Cmp))
    return R;

  return nullptr;
}

/// Canonicalize icmp instructions based on dominating conditions.
Instruction *InstCombinerImpl::foldICmpWithDominatingICmp(ICmpInst &Cmp) {
  // We already checked simple implication in InstSimplify, only handle complex
  // cases here.
  Value *X = Cmp.getOperand(0), *Y = Cmp.getOperand(1);
  const APInt *C;
  if (!match(Y, m_APInt(C)))
    return nullptr;

  CmpInst::Predicate Pred = Cmp.getPredicate();
  ConstantRange CR = ConstantRange::makeExactICmpRegion(Pred, *C);

  auto handleDomCond = [&](ICmpInst::Predicate DomPred,
                           const APInt *DomC) -> Instruction * {
    // We have 2 compares of a variable with constants. Calculate the constant
    // ranges of those compares to see if we can transform the 2nd compare:
    // DomBB:
    //   DomCond = icmp DomPred X, DomC
    //   br DomCond, CmpBB, FalseBB
    // CmpBB:
    //   Cmp = icmp Pred X, C
    ConstantRange DominatingCR =
        ConstantRange::makeExactICmpRegion(DomPred, *DomC);
    ConstantRange Intersection = DominatingCR.intersectWith(CR);
    ConstantRange Difference = DominatingCR.difference(CR);
    if (Intersection.isEmptySet())
      return replaceInstUsesWith(Cmp, Builder.getFalse());
    if (Difference.isEmptySet())
      return replaceInstUsesWith(Cmp, Builder.getTrue());

    // Canonicalizing a sign bit comparison that gets used in a branch,
    // pessimizes codegen by generating branch on zero instruction instead
    // of a test and branch. So we avoid canonicalizing in such situations
    // because test and branch instruction has better branch displacement
    // than compare and branch instruction.
    bool UnusedBit;
    bool IsSignBit = isSignBitCheck(Pred, *C, UnusedBit);
    if (Cmp.isEquality() || (IsSignBit && hasBranchUse(Cmp)))
      return nullptr;

    // Avoid an infinite loop with min/max canonicalization.
    // TODO: This will be unnecessary if we canonicalize to min/max intrinsics.
    if (Cmp.hasOneUse() &&
        match(Cmp.user_back(), m_MaxOrMin(m_Value(), m_Value())))
      return nullptr;

    if (const APInt *EqC = Intersection.getSingleElement())
      return new ICmpInst(ICmpInst::ICMP_EQ, X, Builder.getInt(*EqC));
    if (const APInt *NeC = Difference.getSingleElement())
      return new ICmpInst(ICmpInst::ICMP_NE, X, Builder.getInt(*NeC));
    return nullptr;
  };

  for (BranchInst *BI : DC.conditionsFor(X)) {
    ICmpInst::Predicate DomPred;
    const APInt *DomC;
    if (!match(BI->getCondition(),
               m_ICmp(DomPred, m_Specific(X), m_APInt(DomC))))
      continue;

    BasicBlockEdge Edge0(BI->getParent(), BI->getSuccessor(0));
    if (DT.dominates(Edge0, Cmp.getParent())) {
      if (auto *V = handleDomCond(DomPred, DomC))
        return V;
    } else {
      BasicBlockEdge Edge1(BI->getParent(), BI->getSuccessor(1));
      if (DT.dominates(Edge1, Cmp.getParent()))
        if (auto *V =
                handleDomCond(CmpInst::getInversePredicate(DomPred), DomC))
          return V;
    }
  }

  return nullptr;
}

/// Fold icmp (trunc X), C.
Instruction *InstCombinerImpl::foldICmpTruncConstant(ICmpInst &Cmp,
                                                     TruncInst *Trunc,
                                                     const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *X = Trunc->getOperand(0);
  Type *SrcTy = X->getType();
  unsigned DstBits = Trunc->getType()->getScalarSizeInBits(),
           SrcBits = SrcTy->getScalarSizeInBits();

  // Match (icmp pred (trunc nuw/nsw X), C)
  // Which we can convert to (icmp pred X, (sext/zext C))
  if (shouldChangeType(Trunc->getType(), SrcTy)) {
    if (Trunc->hasNoSignedWrap())
      return new ICmpInst(Pred, X, ConstantInt::get(SrcTy, C.sext(SrcBits)));
    if (!Cmp.isSigned() && Trunc->hasNoUnsignedWrap())
      return new ICmpInst(Pred, X, ConstantInt::get(SrcTy, C.zext(SrcBits)));
  }

  if (C.isOne() && C.getBitWidth() > 1) {
    // icmp slt trunc(signum(V)) 1 --> icmp slt V, 1
    Value *V = nullptr;
    if (Pred == ICmpInst::ICMP_SLT && match(X, m_Signum(m_Value(V))))
      return new ICmpInst(ICmpInst::ICMP_SLT, V,
                          ConstantInt::get(V->getType(), 1));
  }

  // TODO: Handle any shifted constant by subtracting trailing zeros.
  // TODO: Handle non-equality predicates.
  Value *Y;
  if (Cmp.isEquality() && match(X, m_Shl(m_One(), m_Value(Y)))) {
    // (trunc (1 << Y) to iN) == 0 --> Y u>= N
    // (trunc (1 << Y) to iN) != 0 --> Y u<  N
    if (C.isZero()) {
      auto NewPred = (Pred == Cmp.ICMP_EQ) ? Cmp.ICMP_UGE : Cmp.ICMP_ULT;
      return new ICmpInst(NewPred, Y, ConstantInt::get(SrcTy, DstBits));
    }
    // (trunc (1 << Y) to iN) == 2**C --> Y == C
    // (trunc (1 << Y) to iN) != 2**C --> Y != C
    if (C.isPowerOf2())
      return new ICmpInst(Pred, Y, ConstantInt::get(SrcTy, C.logBase2()));
  }

  if (Cmp.isEquality() && Trunc->hasOneUse()) {
    // Canonicalize to a mask and wider compare if the wide type is suitable:
    // (trunc X to i8) == C --> (X & 0xff) == (zext C)
    if (!SrcTy->isVectorTy() && shouldChangeType(DstBits, SrcBits)) {
      Constant *Mask =
          ConstantInt::get(SrcTy, APInt::getLowBitsSet(SrcBits, DstBits));
      Value *And = Builder.CreateAnd(X, Mask);
      Constant *WideC = ConstantInt::get(SrcTy, C.zext(SrcBits));
      return new ICmpInst(Pred, And, WideC);
    }

    // Simplify icmp eq (trunc x to i8), 42 -> icmp eq x, 42|highbits if all
    // of the high bits truncated out of x are known.
    KnownBits Known = computeKnownBits(X, 0, &Cmp);

    // If all the high bits are known, we can do this xform.
    if ((Known.Zero | Known.One).countl_one() >= SrcBits - DstBits) {
      // Pull in the high bits from known-ones set.
      APInt NewRHS = C.zext(SrcBits);
      NewRHS |= Known.One & APInt::getHighBitsSet(SrcBits, SrcBits - DstBits);
      return new ICmpInst(Pred, X, ConstantInt::get(SrcTy, NewRHS));
    }
  }

  // Look through truncated right-shift of the sign-bit for a sign-bit check:
  // trunc iN (ShOp >> ShAmtC) to i[N - ShAmtC] < 0  --> ShOp <  0
  // trunc iN (ShOp >> ShAmtC) to i[N - ShAmtC] > -1 --> ShOp > -1
  Value *ShOp;
  const APInt *ShAmtC;
  bool TrueIfSigned;
  if (isSignBitCheck(Pred, C, TrueIfSigned) &&
      match(X, m_Shr(m_Value(ShOp), m_APInt(ShAmtC))) &&
      DstBits == SrcBits - ShAmtC->getZExtValue()) {
    return TrueIfSigned ? new ICmpInst(ICmpInst::ICMP_SLT, ShOp,
                                       ConstantInt::getNullValue(SrcTy))
                        : new ICmpInst(ICmpInst::ICMP_SGT, ShOp,
                                       ConstantInt::getAllOnesValue(SrcTy));
  }

  return nullptr;
}

/// Fold icmp (trunc nuw/nsw X), (trunc nuw/nsw Y).
/// Fold icmp (trunc nuw/nsw X), (zext/sext Y).
Instruction *
InstCombinerImpl::foldICmpTruncWithTruncOrExt(ICmpInst &Cmp,
                                              const SimplifyQuery &Q) {
  Value *X, *Y;
  ICmpInst::Predicate Pred;
  bool YIsSExt = false;
  // Try to match icmp (trunc X), (trunc Y)
  if (match(&Cmp, m_ICmp(Pred, m_Trunc(m_Value(X)), m_Trunc(m_Value(Y))))) {
    unsigned NoWrapFlags = cast<TruncInst>(Cmp.getOperand(0))->getNoWrapKind() &
                           cast<TruncInst>(Cmp.getOperand(1))->getNoWrapKind();
    if (Cmp.isSigned()) {
      // For signed comparisons, both truncs must be nsw.
      if (!(NoWrapFlags & TruncInst::NoSignedWrap))
        return nullptr;
    } else {
      // For unsigned and equality comparisons, either both must be nuw or
      // both must be nsw, we don't care which.
      if (!NoWrapFlags)
        return nullptr;
    }

    if (X->getType() != Y->getType() &&
        (!Cmp.getOperand(0)->hasOneUse() || !Cmp.getOperand(1)->hasOneUse()))
      return nullptr;
    if (!isDesirableIntType(X->getType()->getScalarSizeInBits()) &&
        isDesirableIntType(Y->getType()->getScalarSizeInBits())) {
      std::swap(X, Y);
      Pred = Cmp.getSwappedPredicate(Pred);
    }
    YIsSExt = !(NoWrapFlags & TruncInst::NoUnsignedWrap);
  }
  // Try to match icmp (trunc nuw X), (zext Y)
  else if (!Cmp.isSigned() &&
           match(&Cmp, m_c_ICmp(Pred, m_NUWTrunc(m_Value(X)),
                                m_OneUse(m_ZExt(m_Value(Y)))))) {
    // Can fold trunc nuw + zext for unsigned and equality predicates.
  }
  // Try to match icmp (trunc nsw X), (sext Y)
  else if (match(&Cmp, m_c_ICmp(Pred, m_NSWTrunc(m_Value(X)),
                                m_OneUse(m_ZExtOrSExt(m_Value(Y)))))) {
    // Can fold trunc nsw + zext/sext for all predicates.
    YIsSExt =
        isa<SExtInst>(Cmp.getOperand(0)) || isa<SExtInst>(Cmp.getOperand(1));
  } else
    return nullptr;

  Type *TruncTy = Cmp.getOperand(0)->getType();
  unsigned TruncBits = TruncTy->getScalarSizeInBits();

  // If this transform will end up changing from desirable types -> undesirable
  // types skip it.
  if (isDesirableIntType(TruncBits) &&
      !isDesirableIntType(X->getType()->getScalarSizeInBits()))
    return nullptr;

  Value *NewY = Builder.CreateIntCast(Y, X->getType(), YIsSExt);
  return new ICmpInst(Pred, X, NewY);
}

/// Fold icmp (xor X, Y), C.
Instruction *InstCombinerImpl::foldICmpXorConstant(ICmpInst &Cmp,
                                                   BinaryOperator *Xor,
                                                   const APInt &C) {
  if (Instruction *I = foldICmpXorShiftConst(Cmp, Xor, C))
    return I;

  Value *X = Xor->getOperand(0);
  Value *Y = Xor->getOperand(1);
  const APInt *XorC;
  if (!match(Y, m_APInt(XorC)))
    return nullptr;

  // If this is a comparison that tests the signbit (X < 0) or (x > -1),
  // fold the xor.
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  bool TrueIfSigned = false;
  if (isSignBitCheck(Cmp.getPredicate(), C, TrueIfSigned)) {

    // If the sign bit of the XorCst is not set, there is no change to
    // the operation, just stop using the Xor.
    if (!XorC->isNegative())
      return replaceOperand(Cmp, 0, X);

    // Emit the opposite comparison.
    if (TrueIfSigned)
      return new ICmpInst(ICmpInst::ICMP_SGT, X,
                          ConstantInt::getAllOnesValue(X->getType()));
    else
      return new ICmpInst(ICmpInst::ICMP_SLT, X,
                          ConstantInt::getNullValue(X->getType()));
  }

  if (Xor->hasOneUse()) {
    // (icmp u/s (xor X SignMask), C) -> (icmp s/u X, (xor C SignMask))
    if (!Cmp.isEquality() && XorC->isSignMask()) {
      Pred = Cmp.getFlippedSignednessPredicate();
      return new ICmpInst(Pred, X, ConstantInt::get(X->getType(), C ^ *XorC));
    }

    // (icmp u/s (xor X ~SignMask), C) -> (icmp s/u X, (xor C ~SignMask))
    if (!Cmp.isEquality() && XorC->isMaxSignedValue()) {
      Pred = Cmp.getFlippedSignednessPredicate();
      Pred = Cmp.getSwappedPredicate(Pred);
      return new ICmpInst(Pred, X, ConstantInt::get(X->getType(), C ^ *XorC));
    }
  }

  // Mask constant magic can eliminate an 'xor' with unsigned compares.
  if (Pred == ICmpInst::ICMP_UGT) {
    // (xor X, ~C) >u C --> X <u ~C (when C+1 is a power of 2)
    if (*XorC == ~C && (C + 1).isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_ULT, X, Y);
    // (xor X, C) >u C --> X >u C (when C+1 is a power of 2)
    if (*XorC == C && (C + 1).isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_UGT, X, Y);
  }
  if (Pred == ICmpInst::ICMP_ULT) {
    // (xor X, -C) <u C --> X >u ~C (when C is a power of 2)
    if (*XorC == -C && C.isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_UGT, X,
                          ConstantInt::get(X->getType(), ~C));
    // (xor X, C) <u C --> X >u ~C (when -C is a power of 2)
    if (*XorC == C && (-C).isPowerOf2())
      return new ICmpInst(ICmpInst::ICMP_UGT, X,
                          ConstantInt::get(X->getType(), ~C));
  }
  return nullptr;
}

/// For power-of-2 C:
/// ((X s>> ShiftC) ^ X) u< C --> (X + C) u< (C << 1)
/// ((X s>> ShiftC) ^ X) u> (C - 1) --> (X + C) u> ((C << 1) - 1)
Instruction *InstCombinerImpl::foldICmpXorShiftConst(ICmpInst &Cmp,
                                                     BinaryOperator *Xor,
                                                     const APInt &C) {
  CmpInst::Predicate Pred = Cmp.getPredicate();
  APInt PowerOf2;
  if (Pred == ICmpInst::ICMP_ULT)
    PowerOf2 = C;
  else if (Pred == ICmpInst::ICMP_UGT && !C.isMaxValue())
    PowerOf2 = C + 1;
  else
    return nullptr;
  if (!PowerOf2.isPowerOf2())
    return nullptr;
  Value *X;
  const APInt *ShiftC;
  if (!match(Xor, m_OneUse(m_c_Xor(m_Value(X),
                                   m_AShr(m_Deferred(X), m_APInt(ShiftC))))))
    return nullptr;
  uint64_t Shift = ShiftC->getLimitedValue();
  Type *XType = X->getType();
  if (Shift == 0 || PowerOf2.isMinSignedValue())
    return nullptr;
  Value *Add = Builder.CreateAdd(X, ConstantInt::get(XType, PowerOf2));
  APInt Bound =
      Pred == ICmpInst::ICMP_ULT ? PowerOf2 << 1 : ((PowerOf2 << 1) - 1);
  return new ICmpInst(Pred, Add, ConstantInt::get(XType, Bound));
}

/// Fold icmp (and (sh X, Y), C2), C1.
Instruction *InstCombinerImpl::foldICmpAndShift(ICmpInst &Cmp,
                                                BinaryOperator *And,
                                                const APInt &C1,
                                                const APInt &C2) {
  BinaryOperator *Shift = dyn_cast<BinaryOperator>(And->getOperand(0));
  if (!Shift || !Shift->isShift())
    return nullptr;

  // If this is: (X >> C3) & C2 != C1 (where any shift and any compare could
  // exist), turn it into (X & (C2 << C3)) != (C1 << C3). This happens a LOT in
  // code produced by the clang front-end, for bitfield access.
  // This seemingly simple opportunity to fold away a shift turns out to be
  // rather complicated. See PR17827 for details.
  unsigned ShiftOpcode = Shift->getOpcode();
  bool IsShl = ShiftOpcode == Instruction::Shl;
  const APInt *C3;
  if (match(Shift->getOperand(1), m_APInt(C3))) {
    APInt NewAndCst, NewCmpCst;
    bool AnyCmpCstBitsShiftedOut;
    if (ShiftOpcode == Instruction::Shl) {
      // For a left shift, we can fold if the comparison is not signed. We can
      // also fold a signed comparison if the mask value and comparison value
      // are not negative. These constraints may not be obvious, but we can
      // prove that they are correct using an SMT solver.
      if (Cmp.isSigned() && (C2.isNegative() || C1.isNegative()))
        return nullptr;

      NewCmpCst = C1.lshr(*C3);
      NewAndCst = C2.lshr(*C3);
      AnyCmpCstBitsShiftedOut = NewCmpCst.shl(*C3) != C1;
    } else if (ShiftOpcode == Instruction::LShr) {
      // For a logical right shift, we can fold if the comparison is not signed.
      // We can also fold a signed comparison if the shifted mask value and the
      // shifted comparison value are not negative. These constraints may not be
      // obvious, but we can prove that they are correct using an SMT solver.
      NewCmpCst = C1.shl(*C3);
      NewAndCst = C2.shl(*C3);
      AnyCmpCstBitsShiftedOut = NewCmpCst.lshr(*C3) != C1;
      if (Cmp.isSigned() && (NewAndCst.isNegative() || NewCmpCst.isNegative()))
        return nullptr;
    } else {
      // For an arithmetic shift, check that both constants don't use (in a
      // signed sense) the top bits being shifted out.
      assert(ShiftOpcode == Instruction::AShr && "Unknown shift opcode");
      NewCmpCst = C1.shl(*C3);
      NewAndCst = C2.shl(*C3);
      AnyCmpCstBitsShiftedOut = NewCmpCst.ashr(*C3) != C1;
      if (NewAndCst.ashr(*C3) != C2)
        return nullptr;
    }

    if (AnyCmpCstBitsShiftedOut) {
      // If we shifted bits out, the fold is not going to work out. As a
      // special case, check to see if this means that the result is always
      // true or false now.
      if (Cmp.getPredicate() == ICmpInst::ICMP_EQ)
        return replaceInstUsesWith(Cmp, ConstantInt::getFalse(Cmp.getType()));
      if (Cmp.getPredicate() == ICmpInst::ICMP_NE)
        return replaceInstUsesWith(Cmp, ConstantInt::getTrue(Cmp.getType()));
    } else {
      Value *NewAnd = Builder.CreateAnd(
          Shift->getOperand(0), ConstantInt::get(And->getType(), NewAndCst));
      return new ICmpInst(Cmp.getPredicate(),
          NewAnd, ConstantInt::get(And->getType(), NewCmpCst));
    }
  }

  // Turn ((X >> Y) & C2) == 0  into  (X & (C2 << Y)) == 0.  The latter is
  // preferable because it allows the C2 << Y expression to be hoisted out of a
  // loop if Y is invariant and X is not.
  if (Shift->hasOneUse() && C1.isZero() && Cmp.isEquality() &&
      !Shift->isArithmeticShift() && !isa<Constant>(Shift->getOperand(0))) {
    // Compute C2 << Y.
    Value *NewShift =
        IsShl ? Builder.CreateLShr(And->getOperand(1), Shift->getOperand(1))
              : Builder.CreateShl(And->getOperand(1), Shift->getOperand(1));

    // Compute X & (C2 << Y).
    Value *NewAnd = Builder.CreateAnd(Shift->getOperand(0), NewShift);
    return replaceOperand(Cmp, 0, NewAnd);
  }

  return nullptr;
}

/// Fold icmp (and X, C2), C1.
Instruction *InstCombinerImpl::foldICmpAndConstConst(ICmpInst &Cmp,
                                                     BinaryOperator *And,
                                                     const APInt &C1) {
  bool isICMP_NE = Cmp.getPredicate() == ICmpInst::ICMP_NE;

  // For vectors: icmp ne (and X, 1), 0 --> trunc X to N x i1
  // TODO: We canonicalize to the longer form for scalars because we have
  // better analysis/folds for icmp, and codegen may be better with icmp.
  if (isICMP_NE && Cmp.getType()->isVectorTy() && C1.isZero() &&
      match(And->getOperand(1), m_One()))
    return new TruncInst(And->getOperand(0), Cmp.getType());

  const APInt *C2;
  Value *X;
  if (!match(And, m_And(m_Value(X), m_APInt(C2))))
    return nullptr;

  // Don't perform the following transforms if the AND has multiple uses
  if (!And->hasOneUse())
    return nullptr;

  if (Cmp.isEquality() && C1.isZero()) {
    // Restrict this fold to single-use 'and' (PR10267).
    // Replace (and X, (1 << size(X)-1) != 0) with X s< 0
    if (C2->isSignMask()) {
      Constant *Zero = Constant::getNullValue(X->getType());
      auto NewPred = isICMP_NE ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_SGE;
      return new ICmpInst(NewPred, X, Zero);
    }

    APInt NewC2 = *C2;
    KnownBits Know = computeKnownBits(And->getOperand(0), 0, And);
    // Set high zeros of C2 to allow matching negated power-of-2.
    NewC2 = *C2 | APInt::getHighBitsSet(C2->getBitWidth(),
                                        Know.countMinLeadingZeros());

    // Restrict this fold only for single-use 'and' (PR10267).
    // ((%x & C) == 0) --> %x u< (-C)  iff (-C) is power of two.
    if (NewC2.isNegatedPowerOf2()) {
      Constant *NegBOC = ConstantInt::get(And->getType(), -NewC2);
      auto NewPred = isICMP_NE ? ICmpInst::ICMP_UGE : ICmpInst::ICMP_ULT;
      return new ICmpInst(NewPred, X, NegBOC);
    }
  }

  // If the LHS is an 'and' of a truncate and we can widen the and/compare to
  // the input width without changing the value produced, eliminate the cast:
  //
  // icmp (and (trunc W), C2), C1 -> icmp (and W, C2'), C1'
  //
  // We can do this transformation if the constants do not have their sign bits
  // set or if it is an equality comparison. Extending a relational comparison
  // when we're checking the sign bit would not work.
  Value *W;
  if (match(And->getOperand(0), m_OneUse(m_Trunc(m_Value(W)))) &&
      (Cmp.isEquality() || (!C1.isNegative() && !C2->isNegative()))) {
    // TODO: Is this a good transform for vectors? Wider types may reduce
    // throughput. Should this transform be limited (even for scalars) by using
    // shouldChangeType()?
    if (!Cmp.getType()->isVectorTy()) {
      Type *WideType = W->getType();
      unsigned WideScalarBits = WideType->getScalarSizeInBits();
      Constant *ZextC1 = ConstantInt::get(WideType, C1.zext(WideScalarBits));
      Constant *ZextC2 = ConstantInt::get(WideType, C2->zext(WideScalarBits));
      Value *NewAnd = Builder.CreateAnd(W, ZextC2, And->getName());
      return new ICmpInst(Cmp.getPredicate(), NewAnd, ZextC1);
    }
  }

  if (Instruction *I = foldICmpAndShift(Cmp, And, C1, *C2))
    return I;

  // (icmp pred (and (or (lshr A, B), A), 1), 0) -->
  // (icmp pred (and A, (or (shl 1, B), 1), 0))
  //
  // iff pred isn't signed
  if (!Cmp.isSigned() && C1.isZero() && And->getOperand(0)->hasOneUse() &&
      match(And->getOperand(1), m_One())) {
    Constant *One = cast<Constant>(And->getOperand(1));
    Value *Or = And->getOperand(0);
    Value *A, *B, *LShr;
    if (match(Or, m_Or(m_Value(LShr), m_Value(A))) &&
        match(LShr, m_LShr(m_Specific(A), m_Value(B)))) {
      unsigned UsesRemoved = 0;
      if (And->hasOneUse())
        ++UsesRemoved;
      if (Or->hasOneUse())
        ++UsesRemoved;
      if (LShr->hasOneUse())
        ++UsesRemoved;

      // Compute A & ((1 << B) | 1)
      unsigned RequireUsesRemoved = match(B, m_ImmConstant()) ? 1 : 3;
      if (UsesRemoved >= RequireUsesRemoved) {
        Value *NewOr =
            Builder.CreateOr(Builder.CreateShl(One, B, LShr->getName(),
                                               /*HasNUW=*/true),
                             One, Or->getName());
        Value *NewAnd = Builder.CreateAnd(A, NewOr, And->getName());
        return replaceOperand(Cmp, 0, NewAnd);
      }
    }
  }

  // (icmp eq (and (bitcast X to int), ExponentMask), ExponentMask) -->
  // llvm.is.fpclass(X, fcInf|fcNan)
  // (icmp ne (and (bitcast X to int), ExponentMask), ExponentMask) -->
  // llvm.is.fpclass(X, ~(fcInf|fcNan))
  Value *V;
  if (!Cmp.getParent()->getParent()->hasFnAttribute(
          Attribute::NoImplicitFloat) &&
      Cmp.isEquality() &&
      match(X, m_OneUse(m_ElementWiseBitCast(m_Value(V))))) {
    Type *FPType = V->getType()->getScalarType();
    if (FPType->isIEEELikeFPTy() && C1 == *C2) {
      APInt ExponentMask =
          APFloat::getInf(FPType->getFltSemantics()).bitcastToAPInt();
      if (C1 == ExponentMask) {
        unsigned Mask = FPClassTest::fcNan | FPClassTest::fcInf;
        if (isICMP_NE)
          Mask = ~Mask & fcAllFlags;
        return replaceInstUsesWith(Cmp, Builder.createIsFPClass(V, Mask));
      }
    }
  }

  return nullptr;
}

/// Fold icmp (and X, Y), C.
Instruction *InstCombinerImpl::foldICmpAndConstant(ICmpInst &Cmp,
                                                   BinaryOperator *And,
                                                   const APInt &C) {
  if (Instruction *I = foldICmpAndConstConst(Cmp, And, C))
    return I;

  const ICmpInst::Predicate Pred = Cmp.getPredicate();
  bool TrueIfNeg;
  if (isSignBitCheck(Pred, C, TrueIfNeg)) {
    // ((X - 1) & ~X) <  0 --> X == 0
    // ((X - 1) & ~X) >= 0 --> X != 0
    Value *X;
    if (match(And->getOperand(0), m_Add(m_Value(X), m_AllOnes())) &&
        match(And->getOperand(1), m_Not(m_Specific(X)))) {
      auto NewPred = TrueIfNeg ? CmpInst::ICMP_EQ : CmpInst::ICMP_NE;
      return new ICmpInst(NewPred, X, ConstantInt::getNullValue(X->getType()));
    }
    // (X & -X) <  0 --> X == MinSignedC
    // (X & -X) > -1 --> X != MinSignedC
    if (match(And, m_c_And(m_Neg(m_Value(X)), m_Deferred(X)))) {
      Constant *MinSignedC = ConstantInt::get(
          X->getType(),
          APInt::getSignedMinValue(X->getType()->getScalarSizeInBits()));
      auto NewPred = TrueIfNeg ? CmpInst::ICMP_EQ : CmpInst::ICMP_NE;
      return new ICmpInst(NewPred, X, MinSignedC);
    }
  }

  // TODO: These all require that Y is constant too, so refactor with the above.

  // Try to optimize things like "A[i] & 42 == 0" to index computations.
  Value *X = And->getOperand(0);
  Value *Y = And->getOperand(1);
  if (auto *C2 = dyn_cast<ConstantInt>(Y))
    if (auto *LI = dyn_cast<LoadInst>(X))
      if (auto *GEP = dyn_cast<GetElementPtrInst>(LI->getOperand(0)))
        if (auto *GV = dyn_cast<GlobalVariable>(GEP->getOperand(0)))
          if (Instruction *Res =
                  foldCmpLoadFromIndexedGlobal(LI, GEP, GV, Cmp, C2))
            return Res;

  if (!Cmp.isEquality())
    return nullptr;

  // X & -C == -C -> X >  u ~C
  // X & -C != -C -> X <= u ~C
  //   iff C is a power of 2
  if (Cmp.getOperand(1) == Y && C.isNegatedPowerOf2()) {
    auto NewPred =
        Pred == CmpInst::ICMP_EQ ? CmpInst::ICMP_UGT : CmpInst::ICMP_ULE;
    return new ICmpInst(NewPred, X, SubOne(cast<Constant>(Cmp.getOperand(1))));
  }

  // If we are testing the intersection of 2 select-of-nonzero-constants with no
  // common bits set, it's the same as checking if exactly one select condition
  // is set:
  // ((A ? TC : FC) & (B ? TC : FC)) == 0 --> xor A, B
  // ((A ? TC : FC) & (B ? TC : FC)) != 0 --> not(xor A, B)
  // TODO: Generalize for non-constant values.
  // TODO: Handle signed/unsigned predicates.
  // TODO: Handle other bitwise logic connectors.
  // TODO: Extend to handle a non-zero compare constant.
  if (C.isZero() && (Pred == CmpInst::ICMP_EQ || And->hasOneUse())) {
    assert(Cmp.isEquality() && "Not expecting non-equality predicates");
    Value *A, *B;
    const APInt *TC, *FC;
    if (match(X, m_Select(m_Value(A), m_APInt(TC), m_APInt(FC))) &&
        match(Y,
              m_Select(m_Value(B), m_SpecificInt(*TC), m_SpecificInt(*FC))) &&
        !TC->isZero() && !FC->isZero() && !TC->intersects(*FC)) {
      Value *R = Builder.CreateXor(A, B);
      if (Pred == CmpInst::ICMP_NE)
        R = Builder.CreateNot(R);
      return replaceInstUsesWith(Cmp, R);
    }
  }

  // ((zext i1 X) & Y) == 0 --> !((trunc Y) & X)
  // ((zext i1 X) & Y) != 0 -->  ((trunc Y) & X)
  // ((zext i1 X) & Y) == 1 -->  ((trunc Y) & X)
  // ((zext i1 X) & Y) != 1 --> !((trunc Y) & X)
  if (match(And, m_OneUse(m_c_And(m_OneUse(m_ZExt(m_Value(X))), m_Value(Y)))) &&
      X->getType()->isIntOrIntVectorTy(1) && (C.isZero() || C.isOne())) {
    Value *TruncY = Builder.CreateTrunc(Y, X->getType());
    if (C.isZero() ^ (Pred == CmpInst::ICMP_NE)) {
      Value *And = Builder.CreateAnd(TruncY, X);
      return BinaryOperator::CreateNot(And);
    }
    return BinaryOperator::CreateAnd(TruncY, X);
  }

  // (icmp eq/ne (and (shl -1, X), Y), 0)
  //    -> (icmp eq/ne (lshr Y, X), 0)
  // We could technically handle any C == 0 or (C < 0 && isOdd(C)) but it seems
  // highly unlikely the non-zero case will ever show up in code.
  if (C.isZero() &&
      match(And, m_OneUse(m_c_And(m_OneUse(m_Shl(m_AllOnes(), m_Value(X))),
                                  m_Value(Y))))) {
    Value *LShr = Builder.CreateLShr(Y, X);
    return new ICmpInst(Pred, LShr, Constant::getNullValue(LShr->getType()));
  }

  return nullptr;
}

/// Fold icmp eq/ne (or (xor/sub (X1, X2), xor/sub (X3, X4))), 0.
static Value *foldICmpOrXorSubChain(ICmpInst &Cmp, BinaryOperator *Or,
                                    InstCombiner::BuilderTy &Builder) {
  // Are we using xors or subs to bitwise check for a pair or pairs of
  // (in)equalities? Convert to a shorter form that has more potential to be
  // folded even further.
  // ((X1 ^/- X2) || (X3 ^/- X4)) == 0 --> (X1 == X2) && (X3 == X4)
  // ((X1 ^/- X2) || (X3 ^/- X4)) != 0 --> (X1 != X2) || (X3 != X4)
  // ((X1 ^/- X2) || (X3 ^/- X4) || (X5 ^/- X6)) == 0 -->
  // (X1 == X2) && (X3 == X4) && (X5 == X6)
  // ((X1 ^/- X2) || (X3 ^/- X4) || (X5 ^/- X6)) != 0 -->
  // (X1 != X2) || (X3 != X4) || (X5 != X6)
  SmallVector<std::pair<Value *, Value *>, 2> CmpValues;
  SmallVector<Value *, 16> WorkList(1, Or);

  while (!WorkList.empty()) {
    auto MatchOrOperatorArgument = [&](Value *OrOperatorArgument) {
      Value *Lhs, *Rhs;

      if (match(OrOperatorArgument,
                m_OneUse(m_Xor(m_Value(Lhs), m_Value(Rhs))))) {
        CmpValues.emplace_back(Lhs, Rhs);
        return;
      }

      if (match(OrOperatorArgument,
                m_OneUse(m_Sub(m_Value(Lhs), m_Value(Rhs))))) {
        CmpValues.emplace_back(Lhs, Rhs);
        return;
      }

      WorkList.push_back(OrOperatorArgument);
    };

    Value *CurrentValue = WorkList.pop_back_val();
    Value *OrOperatorLhs, *OrOperatorRhs;

    if (!match(CurrentValue,
               m_Or(m_Value(OrOperatorLhs), m_Value(OrOperatorRhs)))) {
      return nullptr;
    }

    MatchOrOperatorArgument(OrOperatorRhs);
    MatchOrOperatorArgument(OrOperatorLhs);
  }

  ICmpInst::Predicate Pred = Cmp.getPredicate();
  auto BOpc = Pred == CmpInst::ICMP_EQ ? Instruction::And : Instruction::Or;
  Value *LhsCmp = Builder.CreateICmp(Pred, CmpValues.rbegin()->first,
                                     CmpValues.rbegin()->second);

  for (auto It = CmpValues.rbegin() + 1; It != CmpValues.rend(); ++It) {
    Value *RhsCmp = Builder.CreateICmp(Pred, It->first, It->second);
    LhsCmp = Builder.CreateBinOp(BOpc, LhsCmp, RhsCmp);
  }

  return LhsCmp;
}

/// Fold icmp (or X, Y), C.
Instruction *InstCombinerImpl::foldICmpOrConstant(ICmpInst &Cmp,
                                                  BinaryOperator *Or,
                                                  const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  if (C.isOne()) {
    // icmp slt signum(V) 1 --> icmp slt V, 1
    Value *V = nullptr;
    if (Pred == ICmpInst::ICMP_SLT && match(Or, m_Signum(m_Value(V))))
      return new ICmpInst(ICmpInst::ICMP_SLT, V,
                          ConstantInt::get(V->getType(), 1));
  }

  Value *OrOp0 = Or->getOperand(0), *OrOp1 = Or->getOperand(1);

  // (icmp eq/ne (or disjoint x, C0), C1)
  //    -> (icmp eq/ne x, C0^C1)
  if (Cmp.isEquality() && match(OrOp1, m_ImmConstant()) &&
      cast<PossiblyDisjointInst>(Or)->isDisjoint()) {
    Value *NewC =
        Builder.CreateXor(OrOp1, ConstantInt::get(OrOp1->getType(), C));
    return new ICmpInst(Pred, OrOp0, NewC);
  }

  const APInt *MaskC;
  if (match(OrOp1, m_APInt(MaskC)) && Cmp.isEquality()) {
    if (*MaskC == C && (C + 1).isPowerOf2()) {
      // X | C == C --> X <=u C
      // X | C != C --> X  >u C
      //   iff C+1 is a power of 2 (C is a bitmask of the low bits)
      Pred = (Pred == CmpInst::ICMP_EQ) ? CmpInst::ICMP_ULE : CmpInst::ICMP_UGT;
      return new ICmpInst(Pred, OrOp0, OrOp1);
    }

    // More general: canonicalize 'equality with set bits mask' to
    // 'equality with clear bits mask'.
    // (X | MaskC) == C --> (X & ~MaskC) == C ^ MaskC
    // (X | MaskC) != C --> (X & ~MaskC) != C ^ MaskC
    if (Or->hasOneUse()) {
      Value *And = Builder.CreateAnd(OrOp0, ~(*MaskC));
      Constant *NewC = ConstantInt::get(Or->getType(), C ^ (*MaskC));
      return new ICmpInst(Pred, And, NewC);
    }
  }

  // (X | (X-1)) s<  0 --> X s< 1
  // (X | (X-1)) s> -1 --> X s> 0
  Value *X;
  bool TrueIfSigned;
  if (isSignBitCheck(Pred, C, TrueIfSigned) &&
      match(Or, m_c_Or(m_Add(m_Value(X), m_AllOnes()), m_Deferred(X)))) {
    auto NewPred = TrueIfSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_SGT;
    Constant *NewC = ConstantInt::get(X->getType(), TrueIfSigned ? 1 : 0);
    return new ICmpInst(NewPred, X, NewC);
  }

  const APInt *OrC;
  // icmp(X | OrC, C) --> icmp(X, 0)
  if (C.isNonNegative() && match(Or, m_Or(m_Value(X), m_APInt(OrC)))) {
    switch (Pred) {
    // X | OrC s< C --> X s< 0 iff OrC s>= C s>= 0
    case ICmpInst::ICMP_SLT:
    // X | OrC s>= C --> X s>= 0 iff OrC s>= C s>= 0
    case ICmpInst::ICMP_SGE:
      if (OrC->sge(C))
        return new ICmpInst(Pred, X, ConstantInt::getNullValue(X->getType()));
      break;
    // X | OrC s<= C --> X s< 0 iff OrC s> C s>= 0
    case ICmpInst::ICMP_SLE:
    // X | OrC s> C --> X s>= 0 iff OrC s> C s>= 0
    case ICmpInst::ICMP_SGT:
      if (OrC->sgt(C))
        return new ICmpInst(ICmpInst::getFlippedStrictnessPredicate(Pred), X,
                            ConstantInt::getNullValue(X->getType()));
      break;
    default:
      break;
    }
  }

  if (!Cmp.isEquality() || !C.isZero() || !Or->hasOneUse())
    return nullptr;

  Value *P, *Q;
  if (match(Or, m_Or(m_PtrToInt(m_Value(P)), m_PtrToInt(m_Value(Q))))) {
    // Simplify icmp eq (or (ptrtoint P), (ptrtoint Q)), 0
    // -> and (icmp eq P, null), (icmp eq Q, null).
    Value *CmpP =
        Builder.CreateICmp(Pred, P, ConstantInt::getNullValue(P->getType()));
    Value *CmpQ =
        Builder.CreateICmp(Pred, Q, ConstantInt::getNullValue(Q->getType()));
    auto BOpc = Pred == CmpInst::ICMP_EQ ? Instruction::And : Instruction::Or;
    return BinaryOperator::Create(BOpc, CmpP, CmpQ);
  }

  if (Value *V = foldICmpOrXorSubChain(Cmp, Or, Builder))
    return replaceInstUsesWith(Cmp, V);

  return nullptr;
}

/// Fold icmp (mul X, Y), C.
Instruction *InstCombinerImpl::foldICmpMulConstant(ICmpInst &Cmp,
                                                   BinaryOperator *Mul,
                                                   const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Type *MulTy = Mul->getType();
  Value *X = Mul->getOperand(0);

  // If there's no overflow:
  // X * X == 0 --> X == 0
  // X * X != 0 --> X != 0
  if (Cmp.isEquality() && C.isZero() && X == Mul->getOperand(1) &&
      (Mul->hasNoUnsignedWrap() || Mul->hasNoSignedWrap()))
    return new ICmpInst(Pred, X, ConstantInt::getNullValue(MulTy));

  const APInt *MulC;
  if (!match(Mul->getOperand(1), m_APInt(MulC)))
    return nullptr;

  // If this is a test of the sign bit and the multiply is sign-preserving with
  // a constant operand, use the multiply LHS operand instead:
  // (X * +MulC) < 0 --> X < 0
  // (X * -MulC) < 0 --> X > 0
  if (isSignTest(Pred, C) && Mul->hasNoSignedWrap()) {
    if (MulC->isNegative())
      Pred = ICmpInst::getSwappedPredicate(Pred);
    return new ICmpInst(Pred, X, ConstantInt::getNullValue(MulTy));
  }

  if (MulC->isZero())
    return nullptr;

  // If the multiply does not wrap or the constant is odd, try to divide the
  // compare constant by the multiplication factor.
  if (Cmp.isEquality()) {
    // (mul nsw X, MulC) eq/ne C --> X eq/ne C /s MulC
    if (Mul->hasNoSignedWrap() && C.srem(*MulC).isZero()) {
      Constant *NewC = ConstantInt::get(MulTy, C.sdiv(*MulC));
      return new ICmpInst(Pred, X, NewC);
    }

    // C % MulC == 0 is weaker than we could use if MulC is odd because it
    // correct to transform if MulC * N == C including overflow. I.e with i8
    // (icmp eq (mul X, 5), 101) -> (icmp eq X, 225) but since 101 % 5 != 0, we
    // miss that case.
    if (C.urem(*MulC).isZero()) {
      // (mul nuw X, MulC) eq/ne C --> X eq/ne C /u MulC
      // (mul X, OddC) eq/ne N * C --> X eq/ne N
      if ((*MulC & 1).isOne() || Mul->hasNoUnsignedWrap()) {
        Constant *NewC = ConstantInt::get(MulTy, C.udiv(*MulC));
        return new ICmpInst(Pred, X, NewC);
      }
    }
  }

  // With a matching no-overflow guarantee, fold the constants:
  // (X * MulC) < C --> X < (C / MulC)
  // (X * MulC) > C --> X > (C / MulC)
  // TODO: Assert that Pred is not equal to SGE, SLE, UGE, ULE?
  Constant *NewC = nullptr;
  if (Mul->hasNoSignedWrap() && ICmpInst::isSigned(Pred)) {
    // MININT / -1 --> overflow.
    if (C.isMinSignedValue() && MulC->isAllOnes())
      return nullptr;
    if (MulC->isNegative())
      Pred = ICmpInst::getSwappedPredicate(Pred);

    if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SGE) {
      NewC = ConstantInt::get(
          MulTy, APIntOps::RoundingSDiv(C, *MulC, APInt::Rounding::UP));
    } else {
      assert((Pred == ICmpInst::ICMP_SLE || Pred == ICmpInst::ICMP_SGT) &&
             "Unexpected predicate");
      NewC = ConstantInt::get(
          MulTy, APIntOps::RoundingSDiv(C, *MulC, APInt::Rounding::DOWN));
    }
  } else if (Mul->hasNoUnsignedWrap() && ICmpInst::isUnsigned(Pred)) {
    if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_UGE) {
      NewC = ConstantInt::get(
          MulTy, APIntOps::RoundingUDiv(C, *MulC, APInt::Rounding::UP));
    } else {
      assert((Pred == ICmpInst::ICMP_ULE || Pred == ICmpInst::ICMP_UGT) &&
             "Unexpected predicate");
      NewC = ConstantInt::get(
          MulTy, APIntOps::RoundingUDiv(C, *MulC, APInt::Rounding::DOWN));
    }
  }

  return NewC ? new ICmpInst(Pred, X, NewC) : nullptr;
}

/// Fold icmp (shl 1, Y), C.
static Instruction *foldICmpShlOne(ICmpInst &Cmp, Instruction *Shl,
                                   const APInt &C) {
  Value *Y;
  if (!match(Shl, m_Shl(m_One(), m_Value(Y))))
    return nullptr;

  Type *ShiftType = Shl->getType();
  unsigned TypeBits = C.getBitWidth();
  bool CIsPowerOf2 = C.isPowerOf2();
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  if (Cmp.isUnsigned()) {
    // (1 << Y) pred C -> Y pred Log2(C)
    if (!CIsPowerOf2) {
      // (1 << Y) <  30 -> Y <= 4
      // (1 << Y) <= 30 -> Y <= 4
      // (1 << Y) >= 30 -> Y >  4
      // (1 << Y) >  30 -> Y >  4
      if (Pred == ICmpInst::ICMP_ULT)
        Pred = ICmpInst::ICMP_ULE;
      else if (Pred == ICmpInst::ICMP_UGE)
        Pred = ICmpInst::ICMP_UGT;
    }

    unsigned CLog2 = C.logBase2();
    return new ICmpInst(Pred, Y, ConstantInt::get(ShiftType, CLog2));
  } else if (Cmp.isSigned()) {
    Constant *BitWidthMinusOne = ConstantInt::get(ShiftType, TypeBits - 1);
    // (1 << Y) >  0 -> Y != 31
    // (1 << Y) >  C -> Y != 31 if C is negative.
    if (Pred == ICmpInst::ICMP_SGT && C.sle(0))
      return new ICmpInst(ICmpInst::ICMP_NE, Y, BitWidthMinusOne);

    // (1 << Y) <  0 -> Y == 31
    // (1 << Y) <  1 -> Y == 31
    // (1 << Y) <  C -> Y == 31 if C is negative and not signed min.
    // Exclude signed min by subtracting 1 and lower the upper bound to 0.
    if (Pred == ICmpInst::ICMP_SLT && (C-1).sle(0))
      return new ICmpInst(ICmpInst::ICMP_EQ, Y, BitWidthMinusOne);
  }

  return nullptr;
}

/// Fold icmp (shl X, Y), C.
Instruction *InstCombinerImpl::foldICmpShlConstant(ICmpInst &Cmp,
                                                   BinaryOperator *Shl,
                                                   const APInt &C) {
  const APInt *ShiftVal;
  if (Cmp.isEquality() && match(Shl->getOperand(0), m_APInt(ShiftVal)))
    return foldICmpShlConstConst(Cmp, Shl->getOperand(1), C, *ShiftVal);

  ICmpInst::Predicate Pred = Cmp.getPredicate();
  // (icmp pred (shl nuw&nsw X, Y), Csle0)
  //      -> (icmp pred X, Csle0)
  //
  // The idea is the nuw/nsw essentially freeze the sign bit for the shift op
  // so X's must be what is used.
  if (C.sle(0) && Shl->hasNoUnsignedWrap() && Shl->hasNoSignedWrap())
    return new ICmpInst(Pred, Shl->getOperand(0), Cmp.getOperand(1));

  // (icmp eq/ne (shl nuw|nsw X, Y), 0)
  //      -> (icmp eq/ne X, 0)
  if (ICmpInst::isEquality(Pred) && C.isZero() &&
      (Shl->hasNoUnsignedWrap() || Shl->hasNoSignedWrap()))
    return new ICmpInst(Pred, Shl->getOperand(0), Cmp.getOperand(1));

  // (icmp slt (shl nsw X, Y), 0/1)
  //      -> (icmp slt X, 0/1)
  // (icmp sgt (shl nsw X, Y), 0/-1)
  //      -> (icmp sgt X, 0/-1)
  //
  // NB: sge/sle with a constant will canonicalize to sgt/slt.
  if (Shl->hasNoSignedWrap() &&
      (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SLT))
    if (C.isZero() || (Pred == ICmpInst::ICMP_SGT ? C.isAllOnes() : C.isOne()))
      return new ICmpInst(Pred, Shl->getOperand(0), Cmp.getOperand(1));

  const APInt *ShiftAmt;
  if (!match(Shl->getOperand(1), m_APInt(ShiftAmt)))
    return foldICmpShlOne(Cmp, Shl, C);

  // Check that the shift amount is in range. If not, don't perform undefined
  // shifts. When the shift is visited, it will be simplified.
  unsigned TypeBits = C.getBitWidth();
  if (ShiftAmt->uge(TypeBits))
    return nullptr;

  Value *X = Shl->getOperand(0);
  Type *ShType = Shl->getType();

  // NSW guarantees that we are only shifting out sign bits from the high bits,
  // so we can ASHR the compare constant without needing a mask and eliminate
  // the shift.
  if (Shl->hasNoSignedWrap()) {
    if (Pred == ICmpInst::ICMP_SGT) {
      // icmp Pred (shl nsw X, ShiftAmt), C --> icmp Pred X, (C >>s ShiftAmt)
      APInt ShiftedC = C.ashr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if ((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE) &&
        C.ashr(*ShiftAmt).shl(*ShiftAmt) == C) {
      APInt ShiftedC = C.ashr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if (Pred == ICmpInst::ICMP_SLT) {
      // SLE is the same as above, but SLE is canonicalized to SLT, so convert:
      // (X << S) <=s C is equiv to X <=s (C >> S) for all C
      // (X << S) <s (C + 1) is equiv to X <s (C >> S) + 1 if C <s SMAX
      // (X << S) <s C is equiv to X <s ((C - 1) >> S) + 1 if C >s SMIN
      assert(!C.isMinSignedValue() && "Unexpected icmp slt");
      APInt ShiftedC = (C - 1).ashr(*ShiftAmt) + 1;
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
  }

  // NUW guarantees that we are only shifting out zero bits from the high bits,
  // so we can LSHR the compare constant without needing a mask and eliminate
  // the shift.
  if (Shl->hasNoUnsignedWrap()) {
    if (Pred == ICmpInst::ICMP_UGT) {
      // icmp Pred (shl nuw X, ShiftAmt), C --> icmp Pred X, (C >>u ShiftAmt)
      APInt ShiftedC = C.lshr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if ((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE) &&
        C.lshr(*ShiftAmt).shl(*ShiftAmt) == C) {
      APInt ShiftedC = C.lshr(*ShiftAmt);
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
    if (Pred == ICmpInst::ICMP_ULT) {
      // ULE is the same as above, but ULE is canonicalized to ULT, so convert:
      // (X << S) <=u C is equiv to X <=u (C >> S) for all C
      // (X << S) <u (C + 1) is equiv to X <u (C >> S) + 1 if C <u ~0u
      // (X << S) <u C is equiv to X <u ((C - 1) >> S) + 1 if C >u 0
      assert(C.ugt(0) && "ult 0 should have been eliminated");
      APInt ShiftedC = (C - 1).lshr(*ShiftAmt) + 1;
      return new ICmpInst(Pred, X, ConstantInt::get(ShType, ShiftedC));
    }
  }

  if (Cmp.isEquality() && Shl->hasOneUse()) {
    // Strength-reduce the shift into an 'and'.
    Constant *Mask = ConstantInt::get(
        ShType,
        APInt::getLowBitsSet(TypeBits, TypeBits - ShiftAmt->getZExtValue()));
    Value *And = Builder.CreateAnd(X, Mask, Shl->getName() + ".mask");
    Constant *LShrC = ConstantInt::get(ShType, C.lshr(*ShiftAmt));
    return new ICmpInst(Pred, And, LShrC);
  }

  // Otherwise, if this is a comparison of the sign bit, simplify to and/test.
  bool TrueIfSigned = false;
  if (Shl->hasOneUse() && isSignBitCheck(Pred, C, TrueIfSigned)) {
    // (X << 31) <s 0  --> (X & 1) != 0
    Constant *Mask = ConstantInt::get(
        ShType,
        APInt::getOneBitSet(TypeBits, TypeBits - ShiftAmt->getZExtValue() - 1));
    Value *And = Builder.CreateAnd(X, Mask, Shl->getName() + ".mask");
    return new ICmpInst(TrueIfSigned ? ICmpInst::ICMP_NE : ICmpInst::ICMP_EQ,
                        And, Constant::getNullValue(ShType));
  }

  // Simplify 'shl' inequality test into 'and' equality test.
  if (Cmp.isUnsigned() && Shl->hasOneUse()) {
    // (X l<< C2) u<=/u> C1 iff C1+1 is power of two -> X & (~C1 l>> C2) ==/!= 0
    if ((C + 1).isPowerOf2() &&
        (Pred == ICmpInst::ICMP_ULE || Pred == ICmpInst::ICMP_UGT)) {
      Value *And = Builder.CreateAnd(X, (~C).lshr(ShiftAmt->getZExtValue()));
      return new ICmpInst(Pred == ICmpInst::ICMP_ULE ? ICmpInst::ICMP_EQ
                                                     : ICmpInst::ICMP_NE,
                          And, Constant::getNullValue(ShType));
    }
    // (X l<< C2) u</u>= C1 iff C1 is power of two -> X & (-C1 l>> C2) ==/!= 0
    if (C.isPowerOf2() &&
        (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_UGE)) {
      Value *And =
          Builder.CreateAnd(X, (~(C - 1)).lshr(ShiftAmt->getZExtValue()));
      return new ICmpInst(Pred == ICmpInst::ICMP_ULT ? ICmpInst::ICMP_EQ
                                                     : ICmpInst::ICMP_NE,
                          And, Constant::getNullValue(ShType));
    }
  }

  // Transform (icmp pred iM (shl iM %v, N), C)
  // -> (icmp pred i(M-N) (trunc %v iM to i(M-N)), (trunc (C>>N))
  // Transform the shl to a trunc if (trunc (C>>N)) has no loss and M-N.
  // This enables us to get rid of the shift in favor of a trunc that may be
  // free on the target. It has the additional benefit of comparing to a
  // smaller constant that may be more target-friendly.
  unsigned Amt = ShiftAmt->getLimitedValue(TypeBits - 1);
  if (Shl->hasOneUse() && Amt != 0 &&
      shouldChangeType(ShType->getScalarSizeInBits(), TypeBits - Amt)) {
    ICmpInst::Predicate CmpPred = Pred;
    APInt RHSC = C;

    if (RHSC.countr_zero() < Amt && ICmpInst::isStrictPredicate(CmpPred)) {
      // Try the flipped strictness predicate.
      // e.g.:
      // icmp ult i64 (shl X, 32), 8589934593 ->
      // icmp ule i64 (shl X, 32), 8589934592 ->
      // icmp ule i32 (trunc X, i32), 2 ->
      // icmp ult i32 (trunc X, i32), 3
      if (auto FlippedStrictness =
              InstCombiner::getFlippedStrictnessPredicateAndConstant(
                  Pred, ConstantInt::get(ShType->getContext(), C))) {
        CmpPred = FlippedStrictness->first;
        RHSC = cast<ConstantInt>(FlippedStrictness->second)->getValue();
      }
    }

    if (RHSC.countr_zero() >= Amt) {
      Type *TruncTy = ShType->getWithNewBitWidth(TypeBits - Amt);
      Constant *NewC =
          ConstantInt::get(TruncTy, RHSC.ashr(*ShiftAmt).trunc(TypeBits - Amt));
      return new ICmpInst(CmpPred,
                          Builder.CreateTrunc(X, TruncTy, "", /*IsNUW=*/false,
                                              Shl->hasNoSignedWrap()),
                          NewC);
    }
  }

  return nullptr;
}

/// Fold icmp ({al}shr X, Y), C.
Instruction *InstCombinerImpl::foldICmpShrConstant(ICmpInst &Cmp,
                                                   BinaryOperator *Shr,
                                                   const APInt &C) {
  // An exact shr only shifts out zero bits, so:
  // icmp eq/ne (shr X, Y), 0 --> icmp eq/ne X, 0
  Value *X = Shr->getOperand(0);
  CmpInst::Predicate Pred = Cmp.getPredicate();
  if (Cmp.isEquality() && Shr->isExact() && C.isZero())
    return new ICmpInst(Pred, X, Cmp.getOperand(1));

  bool IsAShr = Shr->getOpcode() == Instruction::AShr;
  const APInt *ShiftValC;
  if (match(X, m_APInt(ShiftValC))) {
    if (Cmp.isEquality())
      return foldICmpShrConstConst(Cmp, Shr->getOperand(1), C, *ShiftValC);

    // (ShiftValC >> Y) >s -1 --> Y != 0 with ShiftValC < 0
    // (ShiftValC >> Y) <s  0 --> Y == 0 with ShiftValC < 0
    bool TrueIfSigned;
    if (!IsAShr && ShiftValC->isNegative() &&
        isSignBitCheck(Pred, C, TrueIfSigned))
      return new ICmpInst(TrueIfSigned ? CmpInst::ICMP_EQ : CmpInst::ICMP_NE,
                          Shr->getOperand(1),
                          ConstantInt::getNullValue(X->getType()));

    // If the shifted constant is a power-of-2, test the shift amount directly:
    // (ShiftValC >> Y) >u C --> X <u (LZ(C) - LZ(ShiftValC))
    // (ShiftValC >> Y) <u C --> X >=u (LZ(C-1) - LZ(ShiftValC))
    if (!IsAShr && ShiftValC->isPowerOf2() &&
        (Pred == CmpInst::ICMP_UGT || Pred == CmpInst::ICMP_ULT)) {
      bool IsUGT = Pred == CmpInst::ICMP_UGT;
      assert(ShiftValC->uge(C) && "Expected simplify of compare");
      assert((IsUGT || !C.isZero()) && "Expected X u< 0 to simplify");

      unsigned CmpLZ = IsUGT ? C.countl_zero() : (C - 1).countl_zero();
      unsigned ShiftLZ = ShiftValC->countl_zero();
      Constant *NewC = ConstantInt::get(Shr->getType(), CmpLZ - ShiftLZ);
      auto NewPred = IsUGT ? CmpInst::ICMP_ULT : CmpInst::ICMP_UGE;
      return new ICmpInst(NewPred, Shr->getOperand(1), NewC);
    }
  }

  const APInt *ShiftAmtC;
  if (!match(Shr->getOperand(1), m_APInt(ShiftAmtC)))
    return nullptr;

  // Check that the shift amount is in range. If not, don't perform undefined
  // shifts. When the shift is visited it will be simplified.
  unsigned TypeBits = C.getBitWidth();
  unsigned ShAmtVal = ShiftAmtC->getLimitedValue(TypeBits);
  if (ShAmtVal >= TypeBits || ShAmtVal == 0)
    return nullptr;

  bool IsExact = Shr->isExact();
  Type *ShrTy = Shr->getType();
  // TODO: If we could guarantee that InstSimplify would handle all of the
  // constant-value-based preconditions in the folds below, then we could assert
  // those conditions rather than checking them. This is difficult because of
  // undef/poison (PR34838).
  if (IsAShr && Shr->hasOneUse()) {
    if (IsExact && (Pred == CmpInst::ICMP_SLT || Pred == CmpInst::ICMP_ULT) &&
        (C - 1).isPowerOf2() && C.countLeadingZeros() > ShAmtVal) {
      // When C - 1 is a power of two and the transform can be legally
      // performed, prefer this form so the produced constant is close to a
      // power of two.
      // icmp slt/ult (ashr exact X, ShAmtC), C
      // --> icmp slt/ult X, (C - 1) << ShAmtC) + 1
      APInt ShiftedC = (C - 1).shl(ShAmtVal) + 1;
      return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
    if (IsExact || Pred == CmpInst::ICMP_SLT || Pred == CmpInst::ICMP_ULT) {
      // When ShAmtC can be shifted losslessly:
      // icmp PRED (ashr exact X, ShAmtC), C --> icmp PRED X, (C << ShAmtC)
      // icmp slt/ult (ashr X, ShAmtC), C --> icmp slt/ult X, (C << ShAmtC)
      APInt ShiftedC = C.shl(ShAmtVal);
      if (ShiftedC.ashr(ShAmtVal) == C)
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
    if (Pred == CmpInst::ICMP_SGT) {
      // icmp sgt (ashr X, ShAmtC), C --> icmp sgt X, ((C + 1) << ShAmtC) - 1
      APInt ShiftedC = (C + 1).shl(ShAmtVal) - 1;
      if (!C.isMaxSignedValue() && !(C + 1).shl(ShAmtVal).isMinSignedValue() &&
          (ShiftedC + 1).ashr(ShAmtVal) == (C + 1))
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
    if (Pred == CmpInst::ICMP_UGT) {
      // icmp ugt (ashr X, ShAmtC), C --> icmp ugt X, ((C + 1) << ShAmtC) - 1
      // 'C + 1 << ShAmtC' can overflow as a signed number, so the 2nd
      // clause accounts for that pattern.
      APInt ShiftedC = (C + 1).shl(ShAmtVal) - 1;
      if ((ShiftedC + 1).ashr(ShAmtVal) == (C + 1) ||
          (C + 1).shl(ShAmtVal).isMinSignedValue())
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }

    // If the compare constant has significant bits above the lowest sign-bit,
    // then convert an unsigned cmp to a test of the sign-bit:
    // (ashr X, ShiftC) u> C --> X s< 0
    // (ashr X, ShiftC) u< C --> X s> -1
    if (C.getBitWidth() > 2 && C.getNumSignBits() <= ShAmtVal) {
      if (Pred == CmpInst::ICMP_UGT) {
        return new ICmpInst(CmpInst::ICMP_SLT, X,
                            ConstantInt::getNullValue(ShrTy));
      }
      if (Pred == CmpInst::ICMP_ULT) {
        return new ICmpInst(CmpInst::ICMP_SGT, X,
                            ConstantInt::getAllOnesValue(ShrTy));
      }
    }
  } else if (!IsAShr) {
    if (Pred == CmpInst::ICMP_ULT || (Pred == CmpInst::ICMP_UGT && IsExact)) {
      // icmp ult (lshr X, ShAmtC), C --> icmp ult X, (C << ShAmtC)
      // icmp ugt (lshr exact X, ShAmtC), C --> icmp ugt X, (C << ShAmtC)
      APInt ShiftedC = C.shl(ShAmtVal);
      if (ShiftedC.lshr(ShAmtVal) == C)
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
    if (Pred == CmpInst::ICMP_UGT) {
      // icmp ugt (lshr X, ShAmtC), C --> icmp ugt X, ((C + 1) << ShAmtC) - 1
      APInt ShiftedC = (C + 1).shl(ShAmtVal) - 1;
      if ((ShiftedC + 1).lshr(ShAmtVal) == (C + 1))
        return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, ShiftedC));
    }
  }

  if (!Cmp.isEquality())
    return nullptr;

  // Handle equality comparisons of shift-by-constant.

  // If the comparison constant changes with the shift, the comparison cannot
  // succeed (bits of the comparison constant cannot match the shifted value).
  // This should be known by InstSimplify and already be folded to true/false.
  assert(((IsAShr && C.shl(ShAmtVal).ashr(ShAmtVal) == C) ||
          (!IsAShr && C.shl(ShAmtVal).lshr(ShAmtVal) == C)) &&
         "Expected icmp+shr simplify did not occur.");

  // If the bits shifted out are known zero, compare the unshifted value:
  //  (X & 4) >> 1 == 2  --> (X & 4) == 4.
  if (Shr->isExact())
    return new ICmpInst(Pred, X, ConstantInt::get(ShrTy, C << ShAmtVal));

  if (C.isZero()) {
    // == 0 is u< 1.
    if (Pred == CmpInst::ICMP_EQ)
      return new ICmpInst(CmpInst::ICMP_ULT, X,
                          ConstantInt::get(ShrTy, (C + 1).shl(ShAmtVal)));
    else
      return new ICmpInst(CmpInst::ICMP_UGT, X,
                          ConstantInt::get(ShrTy, (C + 1).shl(ShAmtVal) - 1));
  }

  if (Shr->hasOneUse()) {
    // Canonicalize the shift into an 'and':
    // icmp eq/ne (shr X, ShAmt), C --> icmp eq/ne (and X, HiMask), (C << ShAmt)
    APInt Val(APInt::getHighBitsSet(TypeBits, TypeBits - ShAmtVal));
    Constant *Mask = ConstantInt::get(ShrTy, Val);
    Value *And = Builder.CreateAnd(X, Mask, Shr->getName() + ".mask");
    return new ICmpInst(Pred, And, ConstantInt::get(ShrTy, C << ShAmtVal));
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldICmpSRemConstant(ICmpInst &Cmp,
                                                    BinaryOperator *SRem,
                                                    const APInt &C) {
  // Match an 'is positive' or 'is negative' comparison of remainder by a
  // constant power-of-2 value:
  // (X % pow2C) sgt/slt 0
  const ICmpInst::Predicate Pred = Cmp.getPredicate();
  if (Pred != ICmpInst::ICMP_SGT && Pred != ICmpInst::ICMP_SLT &&
      Pred != ICmpInst::ICMP_EQ && Pred != ICmpInst::ICMP_NE)
    return nullptr;

  // TODO: The one-use check is standard because we do not typically want to
  //       create longer instruction sequences, but this might be a special-case
  //       because srem is not good for analysis or codegen.
  if (!SRem->hasOneUse())
    return nullptr;

  const APInt *DivisorC;
  if (!match(SRem->getOperand(1), m_Power2(DivisorC)))
    return nullptr;

  // For cmp_sgt/cmp_slt only zero valued C is handled.
  // For cmp_eq/cmp_ne only positive valued C is handled.
  if (((Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SLT) &&
       !C.isZero()) ||
      ((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE) &&
       !C.isStrictlyPositive()))
    return nullptr;

  // Mask off the sign bit and the modulo bits (low-bits).
  Type *Ty = SRem->getType();
  APInt SignMask = APInt::getSignMask(Ty->getScalarSizeInBits());
  Constant *MaskC = ConstantInt::get(Ty, SignMask | (*DivisorC - 1));
  Value *And = Builder.CreateAnd(SRem->getOperand(0), MaskC);

  if (Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE)
    return new ICmpInst(Pred, And, ConstantInt::get(Ty, C));

  // For 'is positive?' check that the sign-bit is clear and at least 1 masked
  // bit is set. Example:
  // (i8 X % 32) s> 0 --> (X & 159) s> 0
  if (Pred == ICmpInst::ICMP_SGT)
    return new ICmpInst(ICmpInst::ICMP_SGT, And, ConstantInt::getNullValue(Ty));

  // For 'is negative?' check that the sign-bit is set and at least 1 masked
  // bit is set. Example:
  // (i16 X % 4) s< 0 --> (X & 32771) u> 32768
  return new ICmpInst(ICmpInst::ICMP_UGT, And, ConstantInt::get(Ty, SignMask));
}

/// Fold icmp (udiv X, Y), C.
Instruction *InstCombinerImpl::foldICmpUDivConstant(ICmpInst &Cmp,
                                                    BinaryOperator *UDiv,
                                                    const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *X = UDiv->getOperand(0);
  Value *Y = UDiv->getOperand(1);
  Type *Ty = UDiv->getType();

  const APInt *C2;
  if (!match(X, m_APInt(C2)))
    return nullptr;

  assert(*C2 != 0 && "udiv 0, X should have been simplified already.");

  // (icmp ugt (udiv C2, Y), C) -> (icmp ule Y, C2/(C+1))
  if (Pred == ICmpInst::ICMP_UGT) {
    assert(!C.isMaxValue() &&
           "icmp ugt X, UINT_MAX should have been simplified already.");
    return new ICmpInst(ICmpInst::ICMP_ULE, Y,
                        ConstantInt::get(Ty, C2->udiv(C + 1)));
  }

  // (icmp ult (udiv C2, Y), C) -> (icmp ugt Y, C2/C)
  if (Pred == ICmpInst::ICMP_ULT) {
    assert(C != 0 && "icmp ult X, 0 should have been simplified already.");
    return new ICmpInst(ICmpInst::ICMP_UGT, Y,
                        ConstantInt::get(Ty, C2->udiv(C)));
  }

  return nullptr;
}

/// Fold icmp ({su}div X, Y), C.
Instruction *InstCombinerImpl::foldICmpDivConstant(ICmpInst &Cmp,
                                                   BinaryOperator *Div,
                                                   const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *X = Div->getOperand(0);
  Value *Y = Div->getOperand(1);
  Type *Ty = Div->getType();
  bool DivIsSigned = Div->getOpcode() == Instruction::SDiv;

  // If unsigned division and the compare constant is bigger than
  // UMAX/2 (negative), there's only one pair of values that satisfies an
  // equality check, so eliminate the division:
  // (X u/ Y) == C --> (X == C) && (Y == 1)
  // (X u/ Y) != C --> (X != C) || (Y != 1)
  // Similarly, if signed division and the compare constant is exactly SMIN:
  // (X s/ Y) == SMIN --> (X == SMIN) && (Y == 1)
  // (X s/ Y) != SMIN --> (X != SMIN) || (Y != 1)
  if (Cmp.isEquality() && Div->hasOneUse() && C.isSignBitSet() &&
      (!DivIsSigned || C.isMinSignedValue()))   {
    Value *XBig = Builder.CreateICmp(Pred, X, ConstantInt::get(Ty, C));
    Value *YOne = Builder.CreateICmp(Pred, Y, ConstantInt::get(Ty, 1));
    auto Logic = Pred == ICmpInst::ICMP_EQ ? Instruction::And : Instruction::Or;
    return BinaryOperator::Create(Logic, XBig, YOne);
  }

  // Fold: icmp pred ([us]div X, C2), C -> range test
  // Fold this div into the comparison, producing a range check.
  // Determine, based on the divide type, what the range is being
  // checked.  If there is an overflow on the low or high side, remember
  // it, otherwise compute the range [low, hi) bounding the new value.
  // See: InsertRangeTest above for the kinds of replacements possible.
  const APInt *C2;
  if (!match(Y, m_APInt(C2)))
    return nullptr;

  // FIXME: If the operand types don't match the type of the divide
  // then don't attempt this transform. The code below doesn't have the
  // logic to deal with a signed divide and an unsigned compare (and
  // vice versa). This is because (x /s C2) <s C  produces different
  // results than (x /s C2) <u C or (x /u C2) <s C or even
  // (x /u C2) <u C.  Simply casting the operands and result won't
  // work. :(  The if statement below tests that condition and bails
  // if it finds it.
  if (!Cmp.isEquality() && DivIsSigned != Cmp.isSigned())
    return nullptr;

  // The ProdOV computation fails on divide by 0 and divide by -1. Cases with
  // INT_MIN will also fail if the divisor is 1. Although folds of all these
  // division-by-constant cases should be present, we can not assert that they
  // have happened before we reach this icmp instruction.
  if (C2->isZero() || C2->isOne() || (DivIsSigned && C2->isAllOnes()))
    return nullptr;

  // Compute Prod = C * C2. We are essentially solving an equation of
  // form X / C2 = C. We solve for X by multiplying C2 and C.
  // By solving for X, we can turn this into a range check instead of computing
  // a divide.
  APInt Prod = C * *C2;

  // Determine if the product overflows by seeing if the product is not equal to
  // the divide. Make sure we do the same kind of divide as in the LHS
  // instruction that we're folding.
  bool ProdOV = (DivIsSigned ? Prod.sdiv(*C2) : Prod.udiv(*C2)) != C;

  // If the division is known to be exact, then there is no remainder from the
  // divide, so the covered range size is unit, otherwise it is the divisor.
  APInt RangeSize = Div->isExact() ? APInt(C2->getBitWidth(), 1) : *C2;

  // Figure out the interval that is being checked.  For example, a comparison
  // like "X /u 5 == 0" is really checking that X is in the interval [0, 5).
  // Compute this interval based on the constants involved and the signedness of
  // the compare/divide.  This computes a half-open interval, keeping track of
  // whether either value in the interval overflows.  After analysis each
  // overflow variable is set to 0 if it's corresponding bound variable is valid
  // -1 if overflowed off the bottom end, or +1 if overflowed off the top end.
  int LoOverflow = 0, HiOverflow = 0;
  APInt LoBound, HiBound;

  if (!DivIsSigned) { // udiv
    // e.g. X/5 op 3  --> [15, 20)
    LoBound = Prod;
    HiOverflow = LoOverflow = ProdOV;
    if (!HiOverflow) {
      // If this is not an exact divide, then many values in the range collapse
      // to the same result value.
      HiOverflow = addWithOverflow(HiBound, LoBound, RangeSize, false);
    }
  } else if (C2->isStrictlyPositive()) { // Divisor is > 0.
    if (C.isZero()) {                    // (X / pos) op 0
      // Can't overflow.  e.g.  X/2 op 0 --> [-1, 2)
      LoBound = -(RangeSize - 1);
      HiBound = RangeSize;
    } else if (C.isStrictlyPositive()) { // (X / pos) op pos
      LoBound = Prod;                    // e.g.   X/5 op 3 --> [15, 20)
      HiOverflow = LoOverflow = ProdOV;
      if (!HiOverflow)
        HiOverflow = addWithOverflow(HiBound, Prod, RangeSize, true);
    } else { // (X / pos) op neg
      // e.g. X/5 op -3  --> [-15-4, -15+1) --> [-19, -14)
      HiBound = Prod + 1;
      LoOverflow = HiOverflow = ProdOV ? -1 : 0;
      if (!LoOverflow) {
        APInt DivNeg = -RangeSize;
        LoOverflow = addWithOverflow(LoBound, HiBound, DivNeg, true) ? -1 : 0;
      }
    }
  } else if (C2->isNegative()) { // Divisor is < 0.
    if (Div->isExact())
      RangeSize.negate();
    if (C.isZero()) { // (X / neg) op 0
      // e.g. X/-5 op 0  --> [-4, 5)
      LoBound = RangeSize + 1;
      HiBound = -RangeSize;
      if (HiBound == *C2) { // -INTMIN = INTMIN
        HiOverflow = 1;     // [INTMIN+1, overflow)
        HiBound = APInt();  // e.g. X/INTMIN = 0 --> X > INTMIN
      }
    } else if (C.isStrictlyPositive()) { // (X / neg) op pos
      // e.g. X/-5 op 3  --> [-19, -14)
      HiBound = Prod + 1;
      HiOverflow = LoOverflow = ProdOV ? -1 : 0;
      if (!LoOverflow)
        LoOverflow =
            addWithOverflow(LoBound, HiBound, RangeSize, true) ? -1 : 0;
    } else {          // (X / neg) op neg
      LoBound = Prod; // e.g. X/-5 op -3  --> [15, 20)
      LoOverflow = HiOverflow = ProdOV;
      if (!HiOverflow)
        HiOverflow = subWithOverflow(HiBound, Prod, RangeSize, true);
    }

    // Dividing by a negative swaps the condition.  LT <-> GT
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  switch (Pred) {
  default:
    llvm_unreachable("Unhandled icmp predicate!");
  case ICmpInst::ICMP_EQ:
    if (LoOverflow && HiOverflow)
      return replaceInstUsesWith(Cmp, Builder.getFalse());
    if (HiOverflow)
      return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE,
                          X, ConstantInt::get(Ty, LoBound));
    if (LoOverflow)
      return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT,
                          X, ConstantInt::get(Ty, HiBound));
    return replaceInstUsesWith(
        Cmp, insertRangeTest(X, LoBound, HiBound, DivIsSigned, true));
  case ICmpInst::ICMP_NE:
    if (LoOverflow && HiOverflow)
      return replaceInstUsesWith(Cmp, Builder.getTrue());
    if (HiOverflow)
      return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT,
                          X, ConstantInt::get(Ty, LoBound));
    if (LoOverflow)
      return new ICmpInst(DivIsSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE,
                          X, ConstantInt::get(Ty, HiBound));
    return replaceInstUsesWith(
        Cmp, insertRangeTest(X, LoBound, HiBound, DivIsSigned, false));
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_SLT:
    if (LoOverflow == +1) // Low bound is greater than input range.
      return replaceInstUsesWith(Cmp, Builder.getTrue());
    if (LoOverflow == -1) // Low bound is less than input range.
      return replaceInstUsesWith(Cmp, Builder.getFalse());
    return new ICmpInst(Pred, X, ConstantInt::get(Ty, LoBound));
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_SGT:
    if (HiOverflow == +1) // High bound greater than input range.
      return replaceInstUsesWith(Cmp, Builder.getFalse());
    if (HiOverflow == -1) // High bound less than input range.
      return replaceInstUsesWith(Cmp, Builder.getTrue());
    if (Pred == ICmpInst::ICMP_UGT)
      return new ICmpInst(ICmpInst::ICMP_UGE, X, ConstantInt::get(Ty, HiBound));
    return new ICmpInst(ICmpInst::ICMP_SGE, X, ConstantInt::get(Ty, HiBound));
  }

  return nullptr;
}

/// Fold icmp (sub X, Y), C.
Instruction *InstCombinerImpl::foldICmpSubConstant(ICmpInst &Cmp,
                                                   BinaryOperator *Sub,
                                                   const APInt &C) {
  Value *X = Sub->getOperand(0), *Y = Sub->getOperand(1);
  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Type *Ty = Sub->getType();

  // (SubC - Y) == C) --> Y == (SubC - C)
  // (SubC - Y) != C) --> Y != (SubC - C)
  Constant *SubC;
  if (Cmp.isEquality() && match(X, m_ImmConstant(SubC))) {
    return new ICmpInst(Pred, Y,
                        ConstantExpr::getSub(SubC, ConstantInt::get(Ty, C)));
  }

  // (icmp P (sub nuw|nsw C2, Y), C) -> (icmp swap(P) Y, C2-C)
  const APInt *C2;
  APInt SubResult;
  ICmpInst::Predicate SwappedPred = Cmp.getSwappedPredicate();
  bool HasNSW = Sub->hasNoSignedWrap();
  bool HasNUW = Sub->hasNoUnsignedWrap();
  if (match(X, m_APInt(C2)) &&
      ((Cmp.isUnsigned() && HasNUW) || (Cmp.isSigned() && HasNSW)) &&
      !subWithOverflow(SubResult, *C2, C, Cmp.isSigned()))
    return new ICmpInst(SwappedPred, Y, ConstantInt::get(Ty, SubResult));

  // X - Y == 0 --> X == Y.
  // X - Y != 0 --> X != Y.
  // TODO: We allow this with multiple uses as long as the other uses are not
  //       in phis. The phi use check is guarding against a codegen regression
  //       for a loop test. If the backend could undo this (and possibly
  //       subsequent transforms), we would not need this hack.
  if (Cmp.isEquality() && C.isZero() &&
      none_of((Sub->users()), [](const User *U) { return isa<PHINode>(U); }))
    return new ICmpInst(Pred, X, Y);

  // The following transforms are only worth it if the only user of the subtract
  // is the icmp.
  // TODO: This is an artificial restriction for all of the transforms below
  //       that only need a single replacement icmp. Can these use the phi test
  //       like the transform above here?
  if (!Sub->hasOneUse())
    return nullptr;

  if (Sub->hasNoSignedWrap()) {
    // (icmp sgt (sub nsw X, Y), -1) -> (icmp sge X, Y)
    if (Pred == ICmpInst::ICMP_SGT && C.isAllOnes())
      return new ICmpInst(ICmpInst::ICMP_SGE, X, Y);

    // (icmp sgt (sub nsw X, Y), 0) -> (icmp sgt X, Y)
    if (Pred == ICmpInst::ICMP_SGT && C.isZero())
      return new ICmpInst(ICmpInst::ICMP_SGT, X, Y);

    // (icmp slt (sub nsw X, Y), 0) -> (icmp slt X, Y)
    if (Pred == ICmpInst::ICMP_SLT && C.isZero())
      return new ICmpInst(ICmpInst::ICMP_SLT, X, Y);

    // (icmp slt (sub nsw X, Y), 1) -> (icmp sle X, Y)
    if (Pred == ICmpInst::ICMP_SLT && C.isOne())
      return new ICmpInst(ICmpInst::ICMP_SLE, X, Y);
  }

  if (!match(X, m_APInt(C2)))
    return nullptr;

  // C2 - Y <u C -> (Y | (C - 1)) == C2
  //   iff (C2 & (C - 1)) == C - 1 and C is a power of 2
  if (Pred == ICmpInst::ICMP_ULT && C.isPowerOf2() &&
      (*C2 & (C - 1)) == (C - 1))
    return new ICmpInst(ICmpInst::ICMP_EQ, Builder.CreateOr(Y, C - 1), X);

  // C2 - Y >u C -> (Y | C) != C2
  //   iff C2 & C == C and C + 1 is a power of 2
  if (Pred == ICmpInst::ICMP_UGT && (C + 1).isPowerOf2() && (*C2 & C) == C)
    return new ICmpInst(ICmpInst::ICMP_NE, Builder.CreateOr(Y, C), X);

  // We have handled special cases that reduce.
  // Canonicalize any remaining sub to add as:
  // (C2 - Y) > C --> (Y + ~C2) < ~C
  Value *Add = Builder.CreateAdd(Y, ConstantInt::get(Ty, ~(*C2)), "notsub",
                                 HasNUW, HasNSW);
  return new ICmpInst(SwappedPred, Add, ConstantInt::get(Ty, ~C));
}

static Value *createLogicFromTable(const std::bitset<4> &Table, Value *Op0,
                                   Value *Op1, IRBuilderBase &Builder,
                                   bool HasOneUse) {
  auto FoldConstant = [&](bool Val) {
    Constant *Res = Val ? Builder.getTrue() : Builder.getFalse();
    if (Op0->getType()->isVectorTy())
      Res = ConstantVector::getSplat(
          cast<VectorType>(Op0->getType())->getElementCount(), Res);
    return Res;
  };

  switch (Table.to_ulong()) {
  case 0: // 0 0 0 0
    return FoldConstant(false);
  case 1: // 0 0 0 1
    return HasOneUse ? Builder.CreateNot(Builder.CreateOr(Op0, Op1)) : nullptr;
  case 2: // 0 0 1 0
    return HasOneUse ? Builder.CreateAnd(Builder.CreateNot(Op0), Op1) : nullptr;
  case 3: // 0 0 1 1
    return Builder.CreateNot(Op0);
  case 4: // 0 1 0 0
    return HasOneUse ? Builder.CreateAnd(Op0, Builder.CreateNot(Op1)) : nullptr;
  case 5: // 0 1 0 1
    return Builder.CreateNot(Op1);
  case 6: // 0 1 1 0
    return Builder.CreateXor(Op0, Op1);
  case 7: // 0 1 1 1
    return HasOneUse ? Builder.CreateNot(Builder.CreateAnd(Op0, Op1)) : nullptr;
  case 8: // 1 0 0 0
    return Builder.CreateAnd(Op0, Op1);
  case 9: // 1 0 0 1
    return HasOneUse ? Builder.CreateNot(Builder.CreateXor(Op0, Op1)) : nullptr;
  case 10: // 1 0 1 0
    return Op1;
  case 11: // 1 0 1 1
    return HasOneUse ? Builder.CreateOr(Builder.CreateNot(Op0), Op1) : nullptr;
  case 12: // 1 1 0 0
    return Op0;
  case 13: // 1 1 0 1
    return HasOneUse ? Builder.CreateOr(Op0, Builder.CreateNot(Op1)) : nullptr;
  case 14: // 1 1 1 0
    return Builder.CreateOr(Op0, Op1);
  case 15: // 1 1 1 1
    return FoldConstant(true);
  default:
    llvm_unreachable("Invalid Operation");
  }
  return nullptr;
}

/// Fold icmp (add X, Y), C.
Instruction *InstCombinerImpl::foldICmpAddConstant(ICmpInst &Cmp,
                                                   BinaryOperator *Add,
                                                   const APInt &C) {
  Value *Y = Add->getOperand(1);
  Value *X = Add->getOperand(0);

  Value *Op0, *Op1;
  Instruction *Ext0, *Ext1;
  const CmpInst::Predicate Pred = Cmp.getPredicate();
  if (match(Add,
            m_Add(m_CombineAnd(m_Instruction(Ext0), m_ZExtOrSExt(m_Value(Op0))),
                  m_CombineAnd(m_Instruction(Ext1),
                               m_ZExtOrSExt(m_Value(Op1))))) &&
      Op0->getType()->isIntOrIntVectorTy(1) &&
      Op1->getType()->isIntOrIntVectorTy(1)) {
    unsigned BW = C.getBitWidth();
    std::bitset<4> Table;
    auto ComputeTable = [&](bool Op0Val, bool Op1Val) {
      int Res = 0;
      if (Op0Val)
        Res += isa<ZExtInst>(Ext0) ? 1 : -1;
      if (Op1Val)
        Res += isa<ZExtInst>(Ext1) ? 1 : -1;
      return ICmpInst::compare(APInt(BW, Res, true), C, Pred);
    };

    Table[0] = ComputeTable(false, false);
    Table[1] = ComputeTable(false, true);
    Table[2] = ComputeTable(true, false);
    Table[3] = ComputeTable(true, true);
    if (auto *Cond =
            createLogicFromTable(Table, Op0, Op1, Builder, Add->hasOneUse()))
      return replaceInstUsesWith(Cmp, Cond);
  }
  const APInt *C2;
  if (Cmp.isEquality() || !match(Y, m_APInt(C2)))
    return nullptr;

  // Fold icmp pred (add X, C2), C.
  Type *Ty = Add->getType();

  // If the add does not wrap, we can always adjust the compare by subtracting
  // the constants. Equality comparisons are handled elsewhere. SGE/SLE/UGE/ULE
  // are canonicalized to SGT/SLT/UGT/ULT.
  if ((Add->hasNoSignedWrap() &&
       (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SLT)) ||
      (Add->hasNoUnsignedWrap() &&
       (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_ULT))) {
    bool Overflow;
    APInt NewC =
        Cmp.isSigned() ? C.ssub_ov(*C2, Overflow) : C.usub_ov(*C2, Overflow);
    // If there is overflow, the result must be true or false.
    // TODO: Can we assert there is no overflow because InstSimplify always
    // handles those cases?
    if (!Overflow)
      // icmp Pred (add nsw X, C2), C --> icmp Pred X, (C - C2)
      return new ICmpInst(Pred, X, ConstantInt::get(Ty, NewC));
  }

  auto CR = ConstantRange::makeExactICmpRegion(Pred, C).subtract(*C2);
  const APInt &Upper = CR.getUpper();
  const APInt &Lower = CR.getLower();
  if (Cmp.isSigned()) {
    if (Lower.isSignMask())
      return new ICmpInst(ICmpInst::ICMP_SLT, X, ConstantInt::get(Ty, Upper));
    if (Upper.isSignMask())
      return new ICmpInst(ICmpInst::ICMP_SGE, X, ConstantInt::get(Ty, Lower));
  } else {
    if (Lower.isMinValue())
      return new ICmpInst(ICmpInst::ICMP_ULT, X, ConstantInt::get(Ty, Upper));
    if (Upper.isMinValue())
      return new ICmpInst(ICmpInst::ICMP_UGE, X, ConstantInt::get(Ty, Lower));
  }

  // This set of folds is intentionally placed after folds that use no-wrapping
  // flags because those folds are likely better for later analysis/codegen.
  const APInt SMax = APInt::getSignedMaxValue(Ty->getScalarSizeInBits());
  const APInt SMin = APInt::getSignedMinValue(Ty->getScalarSizeInBits());

  // Fold compare with offset to opposite sign compare if it eliminates offset:
  // (X + C2) >u C --> X <s -C2 (if C == C2 + SMAX)
  if (Pred == CmpInst::ICMP_UGT && C == *C2 + SMax)
    return new ICmpInst(ICmpInst::ICMP_SLT, X, ConstantInt::get(Ty, -(*C2)));

  // (X + C2) <u C --> X >s ~C2 (if C == C2 + SMIN)
  if (Pred == CmpInst::ICMP_ULT && C == *C2 + SMin)
    return new ICmpInst(ICmpInst::ICMP_SGT, X, ConstantInt::get(Ty, ~(*C2)));

  // (X + C2) >s C --> X <u (SMAX - C) (if C == C2 - 1)
  if (Pred == CmpInst::ICMP_SGT && C == *C2 - 1)
    return new ICmpInst(ICmpInst::ICMP_ULT, X, ConstantInt::get(Ty, SMax - C));

  // (X + C2) <s C --> X >u (C ^ SMAX) (if C == C2)
  if (Pred == CmpInst::ICMP_SLT && C == *C2)
    return new ICmpInst(ICmpInst::ICMP_UGT, X, ConstantInt::get(Ty, C ^ SMax));

  // (X + -1) <u C --> X <=u C (if X is never null)
  if (Pred == CmpInst::ICMP_ULT && C2->isAllOnes()) {
    const SimplifyQuery Q = SQ.getWithInstruction(&Cmp);
    if (llvm::isKnownNonZero(X, Q))
      return new ICmpInst(ICmpInst::ICMP_ULE, X, ConstantInt::get(Ty, C));
  }

  if (!Add->hasOneUse())
    return nullptr;

  // X+C <u C2 -> (X & -C2) == C
  //   iff C & (C2-1) == 0
  //       C2 is a power of 2
  if (Pred == ICmpInst::ICMP_ULT && C.isPowerOf2() && (*C2 & (C - 1)) == 0)
    return new ICmpInst(ICmpInst::ICMP_EQ, Builder.CreateAnd(X, -C),
                        ConstantExpr::getNeg(cast<Constant>(Y)));

  // X+C2 <u C -> (X & C) == 2C
  //   iff C == -(C2)
  //       C2 is a power of 2
  if (Pred == ICmpInst::ICMP_ULT && C2->isPowerOf2() && C == -*C2)
    return new ICmpInst(ICmpInst::ICMP_NE, Builder.CreateAnd(X, C),
                        ConstantInt::get(Ty, C * 2));

  // X+C >u C2 -> (X & ~C2) != C
  //   iff C & C2 == 0
  //       C2+1 is a power of 2
  if (Pred == ICmpInst::ICMP_UGT && (C + 1).isPowerOf2() && (*C2 & C) == 0)
    return new ICmpInst(ICmpInst::ICMP_NE, Builder.CreateAnd(X, ~C),
                        ConstantExpr::getNeg(cast<Constant>(Y)));

  // The range test idiom can use either ult or ugt. Arbitrarily canonicalize
  // to the ult form.
  // X+C2 >u C -> X+(C2-C-1) <u ~C
  if (Pred == ICmpInst::ICMP_UGT)
    return new ICmpInst(ICmpInst::ICMP_ULT,
                        Builder.CreateAdd(X, ConstantInt::get(Ty, *C2 - C - 1)),
                        ConstantInt::get(Ty, ~C));

  return nullptr;
}

bool InstCombinerImpl::matchThreeWayIntCompare(SelectInst *SI, Value *&LHS,
                                               Value *&RHS, ConstantInt *&Less,
                                               ConstantInt *&Equal,
                                               ConstantInt *&Greater) {
  // TODO: Generalize this to work with other comparison idioms or ensure
  // they get canonicalized into this form.

  // select i1 (a == b),
  //        i32 Equal,
  //        i32 (select i1 (a < b), i32 Less, i32 Greater)
  // where Equal, Less and Greater are placeholders for any three constants.
  ICmpInst::Predicate PredA;
  if (!match(SI->getCondition(), m_ICmp(PredA, m_Value(LHS), m_Value(RHS))) ||
      !ICmpInst::isEquality(PredA))
    return false;
  Value *EqualVal = SI->getTrueValue();
  Value *UnequalVal = SI->getFalseValue();
  // We still can get non-canonical predicate here, so canonicalize.
  if (PredA == ICmpInst::ICMP_NE)
    std::swap(EqualVal, UnequalVal);
  if (!match(EqualVal, m_ConstantInt(Equal)))
    return false;
  ICmpInst::Predicate PredB;
  Value *LHS2, *RHS2;
  if (!match(UnequalVal, m_Select(m_ICmp(PredB, m_Value(LHS2), m_Value(RHS2)),
                                  m_ConstantInt(Less), m_ConstantInt(Greater))))
    return false;
  // We can get predicate mismatch here, so canonicalize if possible:
  // First, ensure that 'LHS' match.
  if (LHS2 != LHS) {
    // x sgt y <--> y slt x
    std::swap(LHS2, RHS2);
    PredB = ICmpInst::getSwappedPredicate(PredB);
  }
  if (LHS2 != LHS)
    return false;
  // We also need to canonicalize 'RHS'.
  if (PredB == ICmpInst::ICMP_SGT && isa<Constant>(RHS2)) {
    // x sgt C-1  <-->  x sge C  <-->  not(x slt C)
    auto FlippedStrictness =
        InstCombiner::getFlippedStrictnessPredicateAndConstant(
            PredB, cast<Constant>(RHS2));
    if (!FlippedStrictness)
      return false;
    assert(FlippedStrictness->first == ICmpInst::ICMP_SGE &&
           "basic correctness failure");
    RHS2 = FlippedStrictness->second;
    // And kind-of perform the result swap.
    std::swap(Less, Greater);
    PredB = ICmpInst::ICMP_SLT;
  }
  return PredB == ICmpInst::ICMP_SLT && RHS == RHS2;
}

Instruction *InstCombinerImpl::foldICmpSelectConstant(ICmpInst &Cmp,
                                                      SelectInst *Select,
                                                      ConstantInt *C) {

  assert(C && "Cmp RHS should be a constant int!");
  // If we're testing a constant value against the result of a three way
  // comparison, the result can be expressed directly in terms of the
  // original values being compared.  Note: We could possibly be more
  // aggressive here and remove the hasOneUse test. The original select is
  // really likely to simplify or sink when we remove a test of the result.
  Value *OrigLHS, *OrigRHS;
  ConstantInt *C1LessThan, *C2Equal, *C3GreaterThan;
  if (Cmp.hasOneUse() &&
      matchThreeWayIntCompare(Select, OrigLHS, OrigRHS, C1LessThan, C2Equal,
                              C3GreaterThan)) {
    assert(C1LessThan && C2Equal && C3GreaterThan);

    bool TrueWhenLessThan = ICmpInst::compare(
        C1LessThan->getValue(), C->getValue(), Cmp.getPredicate());
    bool TrueWhenEqual = ICmpInst::compare(C2Equal->getValue(), C->getValue(),
                                           Cmp.getPredicate());
    bool TrueWhenGreaterThan = ICmpInst::compare(
        C3GreaterThan->getValue(), C->getValue(), Cmp.getPredicate());

    // This generates the new instruction that will replace the original Cmp
    // Instruction. Instead of enumerating the various combinations when
    // TrueWhenLessThan, TrueWhenEqual and TrueWhenGreaterThan are true versus
    // false, we rely on chaining of ORs and future passes of InstCombine to
    // simplify the OR further (i.e. a s< b || a == b becomes a s<= b).

    // When none of the three constants satisfy the predicate for the RHS (C),
    // the entire original Cmp can be simplified to a false.
    Value *Cond = Builder.getFalse();
    if (TrueWhenLessThan)
      Cond = Builder.CreateOr(Cond, Builder.CreateICmp(ICmpInst::ICMP_SLT,
                                                       OrigLHS, OrigRHS));
    if (TrueWhenEqual)
      Cond = Builder.CreateOr(Cond, Builder.CreateICmp(ICmpInst::ICMP_EQ,
                                                       OrigLHS, OrigRHS));
    if (TrueWhenGreaterThan)
      Cond = Builder.CreateOr(Cond, Builder.CreateICmp(ICmpInst::ICMP_SGT,
                                                       OrigLHS, OrigRHS));

    return replaceInstUsesWith(Cmp, Cond);
  }
  return nullptr;
}

Instruction *InstCombinerImpl::foldICmpBitCast(ICmpInst &Cmp) {
  auto *Bitcast = dyn_cast<BitCastInst>(Cmp.getOperand(0));
  if (!Bitcast)
    return nullptr;

  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *Op1 = Cmp.getOperand(1);
  Value *BCSrcOp = Bitcast->getOperand(0);
  Type *SrcType = Bitcast->getSrcTy();
  Type *DstType = Bitcast->getType();

  // Make sure the bitcast doesn't change between scalar and vector and
  // doesn't change the number of vector elements.
  if (SrcType->isVectorTy() == DstType->isVectorTy() &&
      SrcType->getScalarSizeInBits() == DstType->getScalarSizeInBits()) {
    // Zero-equality and sign-bit checks are preserved through sitofp + bitcast.
    Value *X;
    if (match(BCSrcOp, m_SIToFP(m_Value(X)))) {
      // icmp  eq (bitcast (sitofp X)), 0 --> icmp  eq X, 0
      // icmp  ne (bitcast (sitofp X)), 0 --> icmp  ne X, 0
      // icmp slt (bitcast (sitofp X)), 0 --> icmp slt X, 0
      // icmp sgt (bitcast (sitofp X)), 0 --> icmp sgt X, 0
      if ((Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_SLT ||
           Pred == ICmpInst::ICMP_NE || Pred == ICmpInst::ICMP_SGT) &&
          match(Op1, m_Zero()))
        return new ICmpInst(Pred, X, ConstantInt::getNullValue(X->getType()));

      // icmp slt (bitcast (sitofp X)), 1 --> icmp slt X, 1
      if (Pred == ICmpInst::ICMP_SLT && match(Op1, m_One()))
        return new ICmpInst(Pred, X, ConstantInt::get(X->getType(), 1));

      // icmp sgt (bitcast (sitofp X)), -1 --> icmp sgt X, -1
      if (Pred == ICmpInst::ICMP_SGT && match(Op1, m_AllOnes()))
        return new ICmpInst(Pred, X,
                            ConstantInt::getAllOnesValue(X->getType()));
    }

    // Zero-equality checks are preserved through unsigned floating-point casts:
    // icmp eq (bitcast (uitofp X)), 0 --> icmp eq X, 0
    // icmp ne (bitcast (uitofp X)), 0 --> icmp ne X, 0
    if (match(BCSrcOp, m_UIToFP(m_Value(X))))
      if (Cmp.isEquality() && match(Op1, m_Zero()))
        return new ICmpInst(Pred, X, ConstantInt::getNullValue(X->getType()));

    const APInt *C;
    bool TrueIfSigned;
    if (match(Op1, m_APInt(C)) && Bitcast->hasOneUse()) {
      // If this is a sign-bit test of a bitcast of a casted FP value, eliminate
      // the FP extend/truncate because that cast does not change the sign-bit.
      // This is true for all standard IEEE-754 types and the X86 80-bit type.
      // The sign-bit is always the most significant bit in those types.
      if (isSignBitCheck(Pred, *C, TrueIfSigned) &&
          (match(BCSrcOp, m_FPExt(m_Value(X))) ||
           match(BCSrcOp, m_FPTrunc(m_Value(X))))) {
        // (bitcast (fpext/fptrunc X)) to iX) < 0 --> (bitcast X to iY) < 0
        // (bitcast (fpext/fptrunc X)) to iX) > -1 --> (bitcast X to iY) > -1
        Type *XType = X->getType();

        // We can't currently handle Power style floating point operations here.
        if (!(XType->isPPC_FP128Ty() || SrcType->isPPC_FP128Ty())) {
          Type *NewType = Builder.getIntNTy(XType->getScalarSizeInBits());
          if (auto *XVTy = dyn_cast<VectorType>(XType))
            NewType = VectorType::get(NewType, XVTy->getElementCount());
          Value *NewBitcast = Builder.CreateBitCast(X, NewType);
          if (TrueIfSigned)
            return new ICmpInst(ICmpInst::ICMP_SLT, NewBitcast,
                                ConstantInt::getNullValue(NewType));
          else
            return new ICmpInst(ICmpInst::ICMP_SGT, NewBitcast,
                                ConstantInt::getAllOnesValue(NewType));
        }
      }

      // icmp eq/ne (bitcast X to int), special fp -> llvm.is.fpclass(X, class)
      Type *FPType = SrcType->getScalarType();
      if (!Cmp.getParent()->getParent()->hasFnAttribute(
              Attribute::NoImplicitFloat) &&
          Cmp.isEquality() && FPType->isIEEELikeFPTy()) {
        FPClassTest Mask = APFloat(FPType->getFltSemantics(), *C).classify();
        if (Mask & (fcInf | fcZero)) {
          if (Pred == ICmpInst::ICMP_NE)
            Mask = ~Mask;
          return replaceInstUsesWith(Cmp,
                                     Builder.createIsFPClass(BCSrcOp, Mask));
        }
      }
    }
  }

  const APInt *C;
  if (!match(Cmp.getOperand(1), m_APInt(C)) || !DstType->isIntegerTy() ||
      !SrcType->isIntOrIntVectorTy())
    return nullptr;

  // If this is checking if all elements of a vector compare are set or not,
  // invert the casted vector equality compare and test if all compare
  // elements are clear or not. Compare against zero is generally easier for
  // analysis and codegen.
  // icmp eq/ne (bitcast (not X) to iN), -1 --> icmp eq/ne (bitcast X to iN), 0
  // Example: are all elements equal? --> are zero elements not equal?
  // TODO: Try harder to reduce compare of 2 freely invertible operands?
  if (Cmp.isEquality() && C->isAllOnes() && Bitcast->hasOneUse()) {
    if (Value *NotBCSrcOp =
            getFreelyInverted(BCSrcOp, BCSrcOp->hasOneUse(), &Builder)) {
      Value *Cast = Builder.CreateBitCast(NotBCSrcOp, DstType);
      return new ICmpInst(Pred, Cast, ConstantInt::getNullValue(DstType));
    }
  }

  // If this is checking if all elements of an extended vector are clear or not,
  // compare in a narrow type to eliminate the extend:
  // icmp eq/ne (bitcast (ext X) to iN), 0 --> icmp eq/ne (bitcast X to iM), 0
  Value *X;
  if (Cmp.isEquality() && C->isZero() && Bitcast->hasOneUse() &&
      match(BCSrcOp, m_ZExtOrSExt(m_Value(X)))) {
    if (auto *VecTy = dyn_cast<FixedVectorType>(X->getType())) {
      Type *NewType = Builder.getIntNTy(VecTy->getPrimitiveSizeInBits());
      Value *NewCast = Builder.CreateBitCast(X, NewType);
      return new ICmpInst(Pred, NewCast, ConstantInt::getNullValue(NewType));
    }
  }

  // Folding: icmp <pred> iN X, C
  //  where X = bitcast <M x iK> (shufflevector <M x iK> %vec, undef, SC)) to iN
  //    and C is a splat of a K-bit pattern
  //    and SC is a constant vector = <C', C', C', ..., C'>
  // Into:
  //   %E = extractelement <M x iK> %vec, i32 C'
  //   icmp <pred> iK %E, trunc(C)
  Value *Vec;
  ArrayRef<int> Mask;
  if (match(BCSrcOp, m_Shuffle(m_Value(Vec), m_Undef(), m_Mask(Mask)))) {
    // Check whether every element of Mask is the same constant
    if (all_equal(Mask)) {
      auto *VecTy = cast<VectorType>(SrcType);
      auto *EltTy = cast<IntegerType>(VecTy->getElementType());
      if (C->isSplat(EltTy->getBitWidth())) {
        // Fold the icmp based on the value of C
        // If C is M copies of an iK sized bit pattern,
        // then:
        //   =>  %E = extractelement <N x iK> %vec, i32 Elem
        //       icmp <pred> iK %SplatVal, <pattern>
        Value *Elem = Builder.getInt32(Mask[0]);
        Value *Extract = Builder.CreateExtractElement(Vec, Elem);
        Value *NewC = ConstantInt::get(EltTy, C->trunc(EltTy->getBitWidth()));
        return new ICmpInst(Pred, Extract, NewC);
      }
    }
  }
  return nullptr;
}

/// Try to fold integer comparisons with a constant operand: icmp Pred X, C
/// where X is some kind of instruction.
Instruction *InstCombinerImpl::foldICmpInstWithConstant(ICmpInst &Cmp) {
  const APInt *C;

  if (match(Cmp.getOperand(1), m_APInt(C))) {
    if (auto *BO = dyn_cast<BinaryOperator>(Cmp.getOperand(0)))
      if (Instruction *I = foldICmpBinOpWithConstant(Cmp, BO, *C))
        return I;

    if (auto *SI = dyn_cast<SelectInst>(Cmp.getOperand(0)))
      // For now, we only support constant integers while folding the
      // ICMP(SELECT)) pattern. We can extend this to support vector of integers
      // similar to the cases handled by binary ops above.
      if (auto *ConstRHS = dyn_cast<ConstantInt>(Cmp.getOperand(1)))
        if (Instruction *I = foldICmpSelectConstant(Cmp, SI, ConstRHS))
          return I;

    if (auto *TI = dyn_cast<TruncInst>(Cmp.getOperand(0)))
      if (Instruction *I = foldICmpTruncConstant(Cmp, TI, *C))
        return I;

    if (auto *II = dyn_cast<IntrinsicInst>(Cmp.getOperand(0)))
      if (Instruction *I = foldICmpIntrinsicWithConstant(Cmp, II, *C))
        return I;

    // (extractval ([s/u]subo X, Y), 0) == 0 --> X == Y
    // (extractval ([s/u]subo X, Y), 0) != 0 --> X != Y
    // TODO: This checks one-use, but that is not strictly necessary.
    Value *Cmp0 = Cmp.getOperand(0);
    Value *X, *Y;
    if (C->isZero() && Cmp.isEquality() && Cmp0->hasOneUse() &&
        (match(Cmp0,
               m_ExtractValue<0>(m_Intrinsic<Intrinsic::ssub_with_overflow>(
                   m_Value(X), m_Value(Y)))) ||
         match(Cmp0,
               m_ExtractValue<0>(m_Intrinsic<Intrinsic::usub_with_overflow>(
                   m_Value(X), m_Value(Y))))))
      return new ICmpInst(Cmp.getPredicate(), X, Y);
  }

  if (match(Cmp.getOperand(1), m_APIntAllowPoison(C)))
    return foldICmpInstWithConstantAllowPoison(Cmp, *C);

  return nullptr;
}

/// Fold an icmp equality instruction with binary operator LHS and constant RHS:
/// icmp eq/ne BO, C.
Instruction *InstCombinerImpl::foldICmpBinOpEqualityWithConstant(
    ICmpInst &Cmp, BinaryOperator *BO, const APInt &C) {
  // TODO: Some of these folds could work with arbitrary constants, but this
  // function is limited to scalar and vector splat constants.
  if (!Cmp.isEquality())
    return nullptr;

  ICmpInst::Predicate Pred = Cmp.getPredicate();
  bool isICMP_NE = Pred == ICmpInst::ICMP_NE;
  Constant *RHS = cast<Constant>(Cmp.getOperand(1));
  Value *BOp0 = BO->getOperand(0), *BOp1 = BO->getOperand(1);

  switch (BO->getOpcode()) {
  case Instruction::SRem:
    // If we have a signed (X % (2^c)) == 0, turn it into an unsigned one.
    if (C.isZero() && BO->hasOneUse()) {
      const APInt *BOC;
      if (match(BOp1, m_APInt(BOC)) && BOC->sgt(1) && BOC->isPowerOf2()) {
        Value *NewRem = Builder.CreateURem(BOp0, BOp1, BO->getName());
        return new ICmpInst(Pred, NewRem,
                            Constant::getNullValue(BO->getType()));
      }
    }
    break;
  case Instruction::Add: {
    // (A + C2) == C --> A == (C - C2)
    // (A + C2) != C --> A != (C - C2)
    // TODO: Remove the one-use limitation? See discussion in D58633.
    if (Constant *C2 = dyn_cast<Constant>(BOp1)) {
      if (BO->hasOneUse())
        return new ICmpInst(Pred, BOp0, ConstantExpr::getSub(RHS, C2));
    } else if (C.isZero()) {
      // Replace ((add A, B) != 0) with (A != -B) if A or B is
      // efficiently invertible, or if the add has just this one use.
      if (Value *NegVal = dyn_castNegVal(BOp1))
        return new ICmpInst(Pred, BOp0, NegVal);
      if (Value *NegVal = dyn_castNegVal(BOp0))
        return new ICmpInst(Pred, NegVal, BOp1);
      if (BO->hasOneUse()) {
        // (add nuw A, B) != 0 -> (or A, B) != 0
        if (match(BO, m_NUWAdd(m_Value(), m_Value()))) {
          Value *Or = Builder.CreateOr(BOp0, BOp1);
          return new ICmpInst(Pred, Or, Constant::getNullValue(BO->getType()));
        }
        Value *Neg = Builder.CreateNeg(BOp1);
        Neg->takeName(BO);
        return new ICmpInst(Pred, BOp0, Neg);
      }
    }
    break;
  }
  case Instruction::Xor:
    if (Constant *BOC = dyn_cast<Constant>(BOp1)) {
      // For the xor case, we can xor two constants together, eliminating
      // the explicit xor.
      return new ICmpInst(Pred, BOp0, ConstantExpr::getXor(RHS, BOC));
    } else if (C.isZero()) {
      // Replace ((xor A, B) != 0) with (A != B)
      return new ICmpInst(Pred, BOp0, BOp1);
    }
    break;
  case Instruction::Or: {
    const APInt *BOC;
    if (match(BOp1, m_APInt(BOC)) && BO->hasOneUse() && RHS->isAllOnesValue()) {
      // Comparing if all bits outside of a constant mask are set?
      // Replace (X | C) == -1 with (X & ~C) == ~C.
      // This removes the -1 constant.
      Constant *NotBOC = ConstantExpr::getNot(cast<Constant>(BOp1));
      Value *And = Builder.CreateAnd(BOp0, NotBOC);
      return new ICmpInst(Pred, And, NotBOC);
    }
    break;
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
    if (BO->isExact()) {
      // div exact X, Y eq/ne 0 -> X eq/ne 0
      // div exact X, Y eq/ne 1 -> X eq/ne Y
      // div exact X, Y eq/ne C ->
      //    if Y * C never-overflow && OneUse:
      //      -> Y * C eq/ne X
      if (C.isZero())
        return new ICmpInst(Pred, BOp0, Constant::getNullValue(BO->getType()));
      else if (C.isOne())
        return new ICmpInst(Pred, BOp0, BOp1);
      else if (BO->hasOneUse()) {
        OverflowResult OR = computeOverflow(
            Instruction::Mul, BO->getOpcode() == Instruction::SDiv, BOp1,
            Cmp.getOperand(1), BO);
        if (OR == OverflowResult::NeverOverflows) {
          Value *YC =
              Builder.CreateMul(BOp1, ConstantInt::get(BO->getType(), C));
          return new ICmpInst(Pred, YC, BOp0);
        }
      }
    }
    if (BO->getOpcode() == Instruction::UDiv && C.isZero()) {
      // (icmp eq/ne (udiv A, B), 0) -> (icmp ugt/ule i32 B, A)
      auto NewPred = isICMP_NE ? ICmpInst::ICMP_ULE : ICmpInst::ICMP_UGT;
      return new ICmpInst(NewPred, BOp1, BOp0);
    }
    break;
  default:
    break;
  }
  return nullptr;
}

static Instruction *foldCtpopPow2Test(ICmpInst &I, IntrinsicInst *CtpopLhs,
                                      const APInt &CRhs,
                                      InstCombiner::BuilderTy &Builder,
                                      const SimplifyQuery &Q) {
  assert(CtpopLhs->getIntrinsicID() == Intrinsic::ctpop &&
         "Non-ctpop intrin in ctpop fold");
  if (!CtpopLhs->hasOneUse())
    return nullptr;

  // Power of 2 test:
  //    isPow2OrZero : ctpop(X) u< 2
  //    isPow2       : ctpop(X) == 1
  //    NotPow2OrZero: ctpop(X) u> 1
  //    NotPow2      : ctpop(X) != 1
  // If we know any bit of X can be folded to:
  //    IsPow2       : X & (~Bit) == 0
  //    NotPow2      : X & (~Bit) != 0
  const ICmpInst::Predicate Pred = I.getPredicate();
  if (((I.isEquality() || Pred == ICmpInst::ICMP_UGT) && CRhs == 1) ||
      (Pred == ICmpInst::ICMP_ULT && CRhs == 2)) {
    Value *Op = CtpopLhs->getArgOperand(0);
    KnownBits OpKnown = computeKnownBits(Op, Q.DL,
                                         /*Depth*/ 0, Q.AC, Q.CxtI, Q.DT);
    // No need to check for count > 1, that should be already constant folded.
    if (OpKnown.countMinPopulation() == 1) {
      Value *And = Builder.CreateAnd(
          Op, Constant::getIntegerValue(Op->getType(), ~(OpKnown.One)));
      return new ICmpInst(
          (Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_ULT)
              ? ICmpInst::ICMP_EQ
              : ICmpInst::ICMP_NE,
          And, Constant::getNullValue(Op->getType()));
    }
  }

  return nullptr;
}

/// Fold an equality icmp with LLVM intrinsic and constant operand.
Instruction *InstCombinerImpl::foldICmpEqIntrinsicWithConstant(
    ICmpInst &Cmp, IntrinsicInst *II, const APInt &C) {
  Type *Ty = II->getType();
  unsigned BitWidth = C.getBitWidth();
  const ICmpInst::Predicate Pred = Cmp.getPredicate();

  switch (II->getIntrinsicID()) {
  case Intrinsic::abs:
    // abs(A) == 0  ->  A == 0
    // abs(A) == INT_MIN  ->  A == INT_MIN
    if (C.isZero() || C.isMinSignedValue())
      return new ICmpInst(Pred, II->getArgOperand(0), ConstantInt::get(Ty, C));
    break;

  case Intrinsic::bswap:
    // bswap(A) == C  ->  A == bswap(C)
    return new ICmpInst(Pred, II->getArgOperand(0),
                        ConstantInt::get(Ty, C.byteSwap()));

  case Intrinsic::bitreverse:
    // bitreverse(A) == C  ->  A == bitreverse(C)
    return new ICmpInst(Pred, II->getArgOperand(0),
                        ConstantInt::get(Ty, C.reverseBits()));

  case Intrinsic::ctlz:
  case Intrinsic::cttz: {
    // ctz(A) == bitwidth(A)  ->  A == 0 and likewise for !=
    if (C == BitWidth)
      return new ICmpInst(Pred, II->getArgOperand(0),
                          ConstantInt::getNullValue(Ty));

    // ctz(A) == C -> A & Mask1 == Mask2, where Mask2 only has bit C set
    // and Mask1 has bits 0..C+1 set. Similar for ctl, but for high bits.
    // Limit to one use to ensure we don't increase instruction count.
    unsigned Num = C.getLimitedValue(BitWidth);
    if (Num != BitWidth && II->hasOneUse()) {
      bool IsTrailing = II->getIntrinsicID() == Intrinsic::cttz;
      APInt Mask1 = IsTrailing ? APInt::getLowBitsSet(BitWidth, Num + 1)
                               : APInt::getHighBitsSet(BitWidth, Num + 1);
      APInt Mask2 = IsTrailing
        ? APInt::getOneBitSet(BitWidth, Num)
        : APInt::getOneBitSet(BitWidth, BitWidth - Num - 1);
      return new ICmpInst(Pred, Builder.CreateAnd(II->getArgOperand(0), Mask1),
                          ConstantInt::get(Ty, Mask2));
    }
    break;
  }

  case Intrinsic::ctpop: {
    // popcount(A) == 0  ->  A == 0 and likewise for !=
    // popcount(A) == bitwidth(A)  ->  A == -1 and likewise for !=
    bool IsZero = C.isZero();
    if (IsZero || C == BitWidth)
      return new ICmpInst(Pred, II->getArgOperand(0),
                          IsZero ? Constant::getNullValue(Ty)
                                 : Constant::getAllOnesValue(Ty));

    break;
  }

  case Intrinsic::fshl:
  case Intrinsic::fshr:
    if (II->getArgOperand(0) == II->getArgOperand(1)) {
      const APInt *RotAmtC;
      // ror(X, RotAmtC) == C --> X == rol(C, RotAmtC)
      // rol(X, RotAmtC) == C --> X == ror(C, RotAmtC)
      if (match(II->getArgOperand(2), m_APInt(RotAmtC)))
        return new ICmpInst(Pred, II->getArgOperand(0),
                            II->getIntrinsicID() == Intrinsic::fshl
                                ? ConstantInt::get(Ty, C.rotr(*RotAmtC))
                                : ConstantInt::get(Ty, C.rotl(*RotAmtC)));
    }
    break;

  case Intrinsic::umax:
  case Intrinsic::uadd_sat: {
    // uadd.sat(a, b) == 0  ->  (a | b) == 0
    // umax(a, b) == 0  ->  (a | b) == 0
    if (C.isZero() && II->hasOneUse()) {
      Value *Or = Builder.CreateOr(II->getArgOperand(0), II->getArgOperand(1));
      return new ICmpInst(Pred, Or, Constant::getNullValue(Ty));
    }
    break;
  }

  case Intrinsic::ssub_sat:
    // ssub.sat(a, b) == 0 -> a == b
    if (C.isZero())
      return new ICmpInst(Pred, II->getArgOperand(0), II->getArgOperand(1));
    break;
  case Intrinsic::usub_sat: {
    // usub.sat(a, b) == 0  ->  a <= b
    if (C.isZero()) {
      ICmpInst::Predicate NewPred =
          Pred == ICmpInst::ICMP_EQ ? ICmpInst::ICMP_ULE : ICmpInst::ICMP_UGT;
      return new ICmpInst(NewPred, II->getArgOperand(0), II->getArgOperand(1));
    }
    break;
  }
  default:
    break;
  }

  return nullptr;
}

/// Fold an icmp with LLVM intrinsics
static Instruction *
foldICmpIntrinsicWithIntrinsic(ICmpInst &Cmp,
                               InstCombiner::BuilderTy &Builder) {
  assert(Cmp.isEquality());

  ICmpInst::Predicate Pred = Cmp.getPredicate();
  Value *Op0 = Cmp.getOperand(0);
  Value *Op1 = Cmp.getOperand(1);
  const auto *IIOp0 = dyn_cast<IntrinsicInst>(Op0);
  const auto *IIOp1 = dyn_cast<IntrinsicInst>(Op1);
  if (!IIOp0 || !IIOp1 || IIOp0->getIntrinsicID() != IIOp1->getIntrinsicID())
    return nullptr;

  switch (IIOp0->getIntrinsicID()) {
  case Intrinsic::bswap:
  case Intrinsic::bitreverse:
    // If both operands are byte-swapped or bit-reversed, just compare the
    // original values.
    return new ICmpInst(Pred, IIOp0->getOperand(0), IIOp1->getOperand(0));
  case Intrinsic::fshl:
  case Intrinsic::fshr: {
    // If both operands are rotated by same amount, just compare the
    // original values.
    if (IIOp0->getOperand(0) != IIOp0->getOperand(1))
      break;
    if (IIOp1->getOperand(0) != IIOp1->getOperand(1))
      break;
    if (IIOp0->getOperand(2) == IIOp1->getOperand(2))
      return new ICmpInst(Pred, IIOp0->getOperand(0), IIOp1->getOperand(0));

    // rotate(X, AmtX) == rotate(Y, AmtY)
    //  -> rotate(X, AmtX - AmtY) == Y
    // Do this if either both rotates have one use or if only one has one use
    // and AmtX/AmtY are constants.
    unsigned OneUses = IIOp0->hasOneUse() + IIOp1->hasOneUse();
    if (OneUses == 2 ||
        (OneUses == 1 && match(IIOp0->getOperand(2), m_ImmConstant()) &&
         match(IIOp1->getOperand(2), m_ImmConstant()))) {
      Value *SubAmt =
          Builder.CreateSub(IIOp0->getOperand(2), IIOp1->getOperand(2));
      Value *CombinedRotate = Builder.CreateIntrinsic(
          Op0->getType(), IIOp0->getIntrinsicID(),
          {IIOp0->getOperand(0), IIOp0->getOperand(0), SubAmt});
      return new ICmpInst(Pred, IIOp1->getOperand(0), CombinedRotate);
    }
  } break;
  default:
    break;
  }

  return nullptr;
}

/// Try to fold integer comparisons with a constant operand: icmp Pred X, C
/// where X is some kind of instruction and C is AllowPoison.
/// TODO: Move more folds which allow poison to this function.
Instruction *
InstCombinerImpl::foldICmpInstWithConstantAllowPoison(ICmpInst &Cmp,
                                                      const APInt &C) {
  const ICmpInst::Predicate Pred = Cmp.getPredicate();
  if (auto *II = dyn_cast<IntrinsicInst>(Cmp.getOperand(0))) {
    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::fshl:
    case Intrinsic::fshr:
      if (Cmp.isEquality() && II->getArgOperand(0) == II->getArgOperand(1)) {
        // (rot X, ?) == 0/-1 --> X == 0/-1
        if (C.isZero() || C.isAllOnes())
          return new ICmpInst(Pred, II->getArgOperand(0), Cmp.getOperand(1));
      }
      break;
    }
  }

  return nullptr;
}

/// Fold an icmp with BinaryOp and constant operand: icmp Pred BO, C.
Instruction *InstCombinerImpl::foldICmpBinOpWithConstant(ICmpInst &Cmp,
                                                         BinaryOperator *BO,
                                                         const APInt &C) {
  switch (BO->getOpcode()) {
  case Instruction::Xor:
    if (Instruction *I = foldICmpXorConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::And:
    if (Instruction *I = foldICmpAndConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::Or:
    if (Instruction *I = foldICmpOrConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::Mul:
    if (Instruction *I = foldICmpMulConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::Shl:
    if (Instruction *I = foldICmpShlConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::LShr:
  case Instruction::AShr:
    if (Instruction *I = foldICmpShrConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::SRem:
    if (Instruction *I = foldICmpSRemConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::UDiv:
    if (Instruction *I = foldICmpUDivConstant(Cmp, BO, C))
      return I;
    [[fallthrough]];
  case Instruction::SDiv:
    if (Instruction *I = foldICmpDivConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::Sub:
    if (Instruction *I = foldICmpSubConstant(Cmp, BO, C))
      return I;
    break;
  case Instruction::Add:
    if (Instruction *I = foldICmpAddConstant(Cmp, BO, C))
      return I;
    break;
  default:
    break;
  }

  // TODO: These folds could be refactored to be part of the above calls.
  return foldICmpBinOpEqualityWithConstant(Cmp, BO, C);
}

static Instruction *
foldICmpUSubSatOrUAddSatWithConstant(ICmpInst::Predicate Pred,
                                     SaturatingInst *II, const APInt &C,
                                     InstCombiner::BuilderTy &Builder) {
  // This transform may end up producing more than one instruction for the
  // intrinsic, so limit it to one user of the intrinsic.
  if (!II->hasOneUse())
    return nullptr;

  // Let Y        = [add/sub]_sat(X, C) pred C2
  //     SatVal   = The saturating value for the operation
  //     WillWrap = Whether or not the operation will underflow / overflow
  // => Y = (WillWrap ? SatVal : (X binop C)) pred C2
  // => Y = WillWrap ? (SatVal pred C2) : ((X binop C) pred C2)
  //
  // When (SatVal pred C2) is true, then
  //    Y = WillWrap ? true : ((X binop C) pred C2)
  // => Y = WillWrap || ((X binop C) pred C2)
  // else
  //    Y =  WillWrap ? false : ((X binop C) pred C2)
  // => Y = !WillWrap ?  ((X binop C) pred C2) : false
  // => Y = !WillWrap && ((X binop C) pred C2)
  Value *Op0 = II->getOperand(0);
  Value *Op1 = II->getOperand(1);

  const APInt *COp1;
  // This transform only works when the intrinsic has an integral constant or
  // splat vector as the second operand.
  if (!match(Op1, m_APInt(COp1)))
    return nullptr;

  APInt SatVal;
  switch (II->getIntrinsicID()) {
  default:
    llvm_unreachable(
        "This function only works with usub_sat and uadd_sat for now!");
  case Intrinsic::uadd_sat:
    SatVal = APInt::getAllOnes(C.getBitWidth());
    break;
  case Intrinsic::usub_sat:
    SatVal = APInt::getZero(C.getBitWidth());
    break;
  }

  // Check (SatVal pred C2)
  bool SatValCheck = ICmpInst::compare(SatVal, C, Pred);

  // !WillWrap.
  ConstantRange C1 = ConstantRange::makeExactNoWrapRegion(
      II->getBinaryOp(), *COp1, II->getNoWrapKind());

  // WillWrap.
  if (SatValCheck)
    C1 = C1.inverse();

  ConstantRange C2 = ConstantRange::makeExactICmpRegion(Pred, C);
  if (II->getBinaryOp() == Instruction::Add)
    C2 = C2.sub(*COp1);
  else
    C2 = C2.add(*COp1);

  Instruction::BinaryOps CombiningOp =
      SatValCheck ? Instruction::BinaryOps::Or : Instruction::BinaryOps::And;

  std::optional<ConstantRange> Combination;
  if (CombiningOp == Instruction::BinaryOps::Or)
    Combination = C1.exactUnionWith(C2);
  else /* CombiningOp == Instruction::BinaryOps::And */
    Combination = C1.exactIntersectWith(C2);

  if (!Combination)
    return nullptr;

  CmpInst::Predicate EquivPred;
  APInt EquivInt;
  APInt EquivOffset;

  Combination->getEquivalentICmp(EquivPred, EquivInt, EquivOffset);

  return new ICmpInst(
      EquivPred,
      Builder.CreateAdd(Op0, ConstantInt::get(Op1->getType(), EquivOffset)),
      ConstantInt::get(Op1->getType(), EquivInt));
}

static Instruction *
foldICmpOfCmpIntrinsicWithConstant(ICmpInst::Predicate Pred, IntrinsicInst *I,
                                   const APInt &C,
                                   InstCombiner::BuilderTy &Builder) {
  std::optional<ICmpInst::Predicate> NewPredicate = std::nullopt;
  switch (Pred) {
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_NE:
    if (C.isZero())
      NewPredicate = Pred;
    else if (C.isOne())
      NewPredicate =
          Pred == ICmpInst::ICMP_EQ ? ICmpInst::ICMP_UGT : ICmpInst::ICMP_ULE;
    else if (C.isAllOnes())
      NewPredicate =
          Pred == ICmpInst::ICMP_EQ ? ICmpInst::ICMP_ULT : ICmpInst::ICMP_UGE;
    break;

  case ICmpInst::ICMP_SGT:
    if (C.isAllOnes())
      NewPredicate = ICmpInst::ICMP_UGE;
    else if (C.isZero())
      NewPredicate = ICmpInst::ICMP_UGT;
    break;

  case ICmpInst::ICMP_SLT:
    if (C.isZero())
      NewPredicate = ICmpInst::ICMP_ULT;
    else if (C.isOne())
      NewPredicate = ICmpInst::ICMP_ULE;
    break;

  default:
    break;
  }

  if (!NewPredicate)
    return nullptr;

  if (I->getIntrinsicID() == Intrinsic::scmp)
    NewPredicate = ICmpInst::getSignedPredicate(*NewPredicate);
  Value *LHS = I->getOperand(0);
  Value *RHS = I->getOperand(1);
  return new ICmpInst(*NewPredicate, LHS, RHS);
}

/// Fold an icmp with LLVM intrinsic and constant operand: icmp Pred II, C.
Instruction *InstCombinerImpl::foldICmpIntrinsicWithConstant(ICmpInst &Cmp,
                                                             IntrinsicInst *II,
                                                             const APInt &C) {
  ICmpInst::Predicate Pred = Cmp.getPredicate();

  // Handle folds that apply for any kind of icmp.
  switch (II->getIntrinsicID()) {
  default:
    break;
  case Intrinsic::uadd_sat:
  case Intrinsic::usub_sat:
    if (auto *Folded = foldICmpUSubSatOrUAddSatWithConstant(
            Pred, cast<SaturatingInst>(II), C, Builder))
      return Folded;
    break;
  case Intrinsic::ctpop: {
    const SimplifyQuery Q = SQ.getWithInstruction(&Cmp);
    if (Instruction *R = foldCtpopPow2Test(Cmp, II, C, Builder, Q))
      return R;
  } break;
  case Intrinsic::scmp:
  case Intrinsic::ucmp:
    if (auto *Folded = foldICmpOfCmpIntrinsicWithConstant(Pred, II, C, Builder))
      return Folded;
    break;
  }

  if (Cmp.isEquality())
    return foldICmpEqIntrinsicWithConstant(Cmp, II, C);

  Type *Ty = II->getType();
  unsigned BitWidth = C.getBitWidth();
  switch (II->getIntrinsicID()) {
  case Intrinsic::ctpop: {
    // (ctpop X > BitWidth - 1) --> X == -1
    Value *X = II->getArgOperand(0);
    if (C == BitWidth - 1 && Pred == ICmpInst::ICMP_UGT)
      return CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_EQ, X,
                             ConstantInt::getAllOnesValue(Ty));
    // (ctpop X < BitWidth) --> X != -1
    if (C == BitWidth && Pred == ICmpInst::ICMP_ULT)
      return CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_NE, X,
                             ConstantInt::getAllOnesValue(Ty));
    break;
  }
  case Intrinsic::ctlz: {
    // ctlz(0bXXXXXXXX) > 3 -> 0bXXXXXXXX < 0b00010000
    if (Pred == ICmpInst::ICMP_UGT && C.ult(BitWidth)) {
      unsigned Num = C.getLimitedValue();
      APInt Limit = APInt::getOneBitSet(BitWidth, BitWidth - Num - 1);
      return CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_ULT,
                             II->getArgOperand(0), ConstantInt::get(Ty, Limit));
    }

    // ctlz(0bXXXXXXXX) < 3 -> 0bXXXXXXXX > 0b00011111
    if (Pred == ICmpInst::ICMP_ULT && C.uge(1) && C.ule(BitWidth)) {
      unsigned Num = C.getLimitedValue();
      APInt Limit = APInt::getLowBitsSet(BitWidth, BitWidth - Num);
      return CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_UGT,
                             II->getArgOperand(0), ConstantInt::get(Ty, Limit));
    }
    break;
  }
  case Intrinsic::cttz: {
    // Limit to one use to ensure we don't increase instruction count.
    if (!II->hasOneUse())
      return nullptr;

    // cttz(0bXXXXXXXX) > 3 -> 0bXXXXXXXX & 0b00001111 == 0
    if (Pred == ICmpInst::ICMP_UGT && C.ult(BitWidth)) {
      APInt Mask = APInt::getLowBitsSet(BitWidth, C.getLimitedValue() + 1);
      return CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_EQ,
                             Builder.CreateAnd(II->getArgOperand(0), Mask),
                             ConstantInt::getNullValue(Ty));
    }

    // cttz(0bXXXXXXXX) < 3 -> 0bXXXXXXXX & 0b00000111 != 0
    if (Pred == ICmpInst::ICMP_ULT && C.uge(1) && C.ule(BitWidth)) {
      APInt Mask = APInt::getLowBitsSet(BitWidth, C.getLimitedValue());
      return CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_NE,
                             Builder.CreateAnd(II->getArgOperand(0), Mask),
                             ConstantInt::getNullValue(Ty));
    }
    break;
  }
  case Intrinsic::ssub_sat:
    // ssub.sat(a, b) spred 0 -> a spred b
    if (ICmpInst::isSigned(Pred)) {
      if (C.isZero())
        return new ICmpInst(Pred, II->getArgOperand(0), II->getArgOperand(1));
      // X s<= 0 is cannonicalized to X s< 1
      if (Pred == ICmpInst::ICMP_SLT && C.isOne())
        return new ICmpInst(ICmpInst::ICMP_SLE, II->getArgOperand(0),
                            II->getArgOperand(1));
      // X s>= 0 is cannonicalized to X s> -1
      if (Pred == ICmpInst::ICMP_SGT && C.isAllOnes())
        return new ICmpInst(ICmpInst::ICMP_SGE, II->getArgOperand(0),
                            II->getArgOperand(1));
    }
    break;
  default:
    break;
  }

  return nullptr;
}

/// Handle icmp with constant (but not simple integer constant) RHS.
Instruction *InstCombinerImpl::foldICmpInstWithConstantNotInt(ICmpInst &I) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Constant *RHSC = dyn_cast<Constant>(Op1);
  Instruction *LHSI = dyn_cast<Instruction>(Op0);
  if (!RHSC || !LHSI)
    return nullptr;

  switch (LHSI->getOpcode()) {
  case Instruction::PHI:
    if (Instruction *NV = foldOpIntoPhi(I, cast<PHINode>(LHSI)))
      return NV;
    break;
  case Instruction::IntToPtr:
    // icmp pred inttoptr(X), null -> icmp pred X, 0
    if (RHSC->isNullValue() &&
        DL.getIntPtrType(RHSC->getType()) == LHSI->getOperand(0)->getType())
      return new ICmpInst(
          I.getPredicate(), LHSI->getOperand(0),
          Constant::getNullValue(LHSI->getOperand(0)->getType()));
    break;

  case Instruction::Load:
    // Try to optimize things like "A[i] > 4" to index computations.
    if (GetElementPtrInst *GEP =
            dyn_cast<GetElementPtrInst>(LHSI->getOperand(0)))
      if (GlobalVariable *GV = dyn_cast<GlobalVariable>(GEP->getOperand(0)))
        if (Instruction *Res =
                foldCmpLoadFromIndexedGlobal(cast<LoadInst>(LHSI), GEP, GV, I))
          return Res;
    break;
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldSelectICmp(ICmpInst::Predicate Pred,
                                              SelectInst *SI, Value *RHS,
                                              const ICmpInst &I) {
  // Try to fold the comparison into the select arms, which will cause the
  // select to be converted into a logical and/or.
  auto SimplifyOp = [&](Value *Op, bool SelectCondIsTrue) -> Value * {
    if (Value *Res = simplifyICmpInst(Pred, Op, RHS, SQ))
      return Res;
    if (std::optional<bool> Impl = isImpliedCondition(
            SI->getCondition(), Pred, Op, RHS, DL, SelectCondIsTrue))
      return ConstantInt::get(I.getType(), *Impl);
    return nullptr;
  };

  ConstantInt *CI = nullptr;
  Value *Op1 = SimplifyOp(SI->getOperand(1), true);
  if (Op1)
    CI = dyn_cast<ConstantInt>(Op1);

  Value *Op2 = SimplifyOp(SI->getOperand(2), false);
  if (Op2)
    CI = dyn_cast<ConstantInt>(Op2);

  // We only want to perform this transformation if it will not lead to
  // additional code. This is true if either both sides of the select
  // fold to a constant (in which case the icmp is replaced with a select
  // which will usually simplify) or this is the only user of the
  // select (in which case we are trading a select+icmp for a simpler
  // select+icmp) or all uses of the select can be replaced based on
  // dominance information ("Global cases").
  bool Transform = false;
  if (Op1 && Op2)
    Transform = true;
  else if (Op1 || Op2) {
    // Local case
    if (SI->hasOneUse())
      Transform = true;
    // Global cases
    else if (CI && !CI->isZero())
      // When Op1 is constant try replacing select with second operand.
      // Otherwise Op2 is constant and try replacing select with first
      // operand.
      Transform = replacedSelectWithOperand(SI, &I, Op1 ? 2 : 1);
  }
  if (Transform) {
    if (!Op1)
      Op1 = Builder.CreateICmp(Pred, SI->getOperand(1), RHS, I.getName());
    if (!Op2)
      Op2 = Builder.CreateICmp(Pred, SI->getOperand(2), RHS, I.getName());
    return SelectInst::Create(SI->getOperand(0), Op1, Op2);
  }

  return nullptr;
}

// Returns whether V is a Mask ((X + 1) & X == 0) or ~Mask (-Pow2OrZero)
static bool isMaskOrZero(const Value *V, bool Not, const SimplifyQuery &Q,
                         unsigned Depth = 0) {
  if (Not ? match(V, m_NegatedPower2OrZero()) : match(V, m_LowBitMaskOrZero()))
    return true;
  if (V->getType()->getScalarSizeInBits() == 1)
    return true;
  if (Depth++ >= MaxAnalysisRecursionDepth)
    return false;
  Value *X;
  const Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;
  switch (I->getOpcode()) {
  case Instruction::ZExt:
    // ZExt(Mask) is a Mask.
    return !Not && isMaskOrZero(I->getOperand(0), Not, Q, Depth);
  case Instruction::SExt:
    // SExt(Mask) is a Mask.
    // SExt(~Mask) is a ~Mask.
    return isMaskOrZero(I->getOperand(0), Not, Q, Depth);
  case Instruction::And:
  case Instruction::Or:
    // Mask0 | Mask1 is a Mask.
    // Mask0 & Mask1 is a Mask.
    // ~Mask0 | ~Mask1 is a ~Mask.
    // ~Mask0 & ~Mask1 is a ~Mask.
    return isMaskOrZero(I->getOperand(1), Not, Q, Depth) &&
           isMaskOrZero(I->getOperand(0), Not, Q, Depth);
  case Instruction::Xor:
    if (match(V, m_Not(m_Value(X))))
      return isMaskOrZero(X, !Not, Q, Depth);

    // (X ^ -X) is a ~Mask
    if (Not)
      return match(V, m_c_Xor(m_Value(X), m_Neg(m_Deferred(X))));
    // (X ^ (X - 1)) is a Mask
    else
      return match(V, m_c_Xor(m_Value(X), m_Add(m_Deferred(X), m_AllOnes())));
  case Instruction::Select:
    // c ? Mask0 : Mask1 is a Mask.
    return isMaskOrZero(I->getOperand(1), Not, Q, Depth) &&
           isMaskOrZero(I->getOperand(2), Not, Q, Depth);
  case Instruction::Shl:
    // (~Mask) << X is a ~Mask.
    return Not && isMaskOrZero(I->getOperand(0), Not, Q, Depth);
  case Instruction::LShr:
    // Mask >> X is a Mask.
    return !Not && isMaskOrZero(I->getOperand(0), Not, Q, Depth);
  case Instruction::AShr:
    // Mask s>> X is a Mask.
    // ~Mask s>> X is a ~Mask.
    return isMaskOrZero(I->getOperand(0), Not, Q, Depth);
  case Instruction::Add:
    // Pow2 - 1 is a Mask.
    if (!Not && match(I->getOperand(1), m_AllOnes()))
      return isKnownToBeAPowerOfTwo(I->getOperand(0), Q.DL, /*OrZero*/ true,
                                    Depth, Q.AC, Q.CxtI, Q.DT);
    break;
  case Instruction::Sub:
    // -Pow2 is a ~Mask.
    if (Not && match(I->getOperand(0), m_Zero()))
      return isKnownToBeAPowerOfTwo(I->getOperand(1), Q.DL, /*OrZero*/ true,
                                    Depth, Q.AC, Q.CxtI, Q.DT);
    break;
  case Instruction::Call: {
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
        // min/max(Mask0, Mask1) is a Mask.
        // min/max(~Mask0, ~Mask1) is a ~Mask.
      case Intrinsic::umax:
      case Intrinsic::smax:
      case Intrinsic::umin:
      case Intrinsic::smin:
        return isMaskOrZero(II->getArgOperand(1), Not, Q, Depth) &&
               isMaskOrZero(II->getArgOperand(0), Not, Q, Depth);

        // In the context of masks, bitreverse(Mask) == ~Mask
      case Intrinsic::bitreverse:
        return isMaskOrZero(II->getArgOperand(0), !Not, Q, Depth);
      default:
        break;
      }
    }
    break;
  }
  default:
    break;
  }
  return false;
}

/// Some comparisons can be simplified.
/// In this case, we are looking for comparisons that look like
/// a check for a lossy truncation.
/// Folds:
///   icmp SrcPred (x & Mask), x    to    icmp DstPred x, Mask
///   icmp SrcPred (x & ~Mask), ~Mask    to    icmp DstPred x, ~Mask
///   icmp eq/ne (x & ~Mask), 0     to    icmp DstPred x, Mask
///   icmp eq/ne (~x | Mask), -1     to    icmp DstPred x, Mask
/// Where Mask is some pattern that produces all-ones in low bits:
///    (-1 >> y)
///    ((-1 << y) >> y)     <- non-canonical, has extra uses
///   ~(-1 << y)
///    ((1 << y) + (-1))    <- non-canonical, has extra uses
/// The Mask can be a constant, too.
/// For some predicates, the operands are commutative.
/// For others, x can only be on a specific side.
static Value *foldICmpWithLowBitMaskedVal(ICmpInst::Predicate Pred, Value *Op0,
                                          Value *Op1, const SimplifyQuery &Q,
                                          InstCombiner &IC) {

  ICmpInst::Predicate DstPred;
  switch (Pred) {
  case ICmpInst::Predicate::ICMP_EQ:
    //  x & Mask == x
    //  x & ~Mask == 0
    //  ~x | Mask == -1
    //    ->    x u<= Mask
    //  x & ~Mask == ~Mask
    //    ->    ~Mask u<= x
    DstPred = ICmpInst::Predicate::ICMP_ULE;
    break;
  case ICmpInst::Predicate::ICMP_NE:
    //  x & Mask != x
    //  x & ~Mask != 0
    //  ~x | Mask != -1
    //    ->    x u> Mask
    //  x & ~Mask != ~Mask
    //    ->    ~Mask u> x
    DstPred = ICmpInst::Predicate::ICMP_UGT;
    break;
  case ICmpInst::Predicate::ICMP_ULT:
    //  x & Mask u< x
    //    -> x u> Mask
    //  x & ~Mask u< ~Mask
    //    -> ~Mask u> x
    DstPred = ICmpInst::Predicate::ICMP_UGT;
    break;
  case ICmpInst::Predicate::ICMP_UGE:
    //  x & Mask u>= x
    //    -> x u<= Mask
    //  x & ~Mask u>= ~Mask
    //    -> ~Mask u<= x
    DstPred = ICmpInst::Predicate::ICMP_ULE;
    break;
  case ICmpInst::Predicate::ICMP_SLT:
    //  x & Mask s< x [iff Mask s>= 0]
    //    -> x s> Mask
    //  x & ~Mask s< ~Mask [iff ~Mask != 0]
    //    -> ~Mask s> x
    DstPred = ICmpInst::Predicate::ICMP_SGT;
    break;
  case ICmpInst::Predicate::ICMP_SGE:
    //  x & Mask s>= x [iff Mask s>= 0]
    //    -> x s<= Mask
    //  x & ~Mask s>= ~Mask [iff ~Mask != 0]
    //    -> ~Mask s<= x
    DstPred = ICmpInst::Predicate::ICMP_SLE;
    break;
  default:
    // We don't support sgt,sle
    // ult/ugt are simplified to true/false respectively.
    return nullptr;
  }

  Value *X, *M;
  // Put search code in lambda for early positive returns.
  auto IsLowBitMask = [&]() {
    if (match(Op0, m_c_And(m_Specific(Op1), m_Value(M)))) {
      X = Op1;
      // Look for: x & Mask pred x
      if (isMaskOrZero(M, /*Not=*/false, Q)) {
        return !ICmpInst::isSigned(Pred) ||
               (match(M, m_NonNegative()) || isKnownNonNegative(M, Q));
      }

      // Look for: x & ~Mask pred ~Mask
      if (isMaskOrZero(X, /*Not=*/true, Q)) {
        return !ICmpInst::isSigned(Pred) || isKnownNonZero(X, Q);
      }
      return false;
    }
    if (ICmpInst::isEquality(Pred) && match(Op1, m_AllOnes()) &&
        match(Op0, m_OneUse(m_Or(m_Value(X), m_Value(M))))) {

      auto Check = [&]() {
        // Look for: ~x | Mask == -1
        if (isMaskOrZero(M, /*Not=*/false, Q)) {
          if (Value *NotX =
                  IC.getFreelyInverted(X, X->hasOneUse(), &IC.Builder)) {
            X = NotX;
            return true;
          }
        }
        return false;
      };
      if (Check())
        return true;
      std::swap(X, M);
      return Check();
    }
    if (ICmpInst::isEquality(Pred) && match(Op1, m_Zero()) &&
        match(Op0, m_OneUse(m_And(m_Value(X), m_Value(M))))) {
      auto Check = [&]() {
        // Look for: x & ~Mask == 0
        if (isMaskOrZero(M, /*Not=*/true, Q)) {
          if (Value *NotM =
                  IC.getFreelyInverted(M, M->hasOneUse(), &IC.Builder)) {
            M = NotM;
            return true;
          }
        }
        return false;
      };
      if (Check())
        return true;
      std::swap(X, M);
      return Check();
    }
    return false;
  };

  if (!IsLowBitMask())
    return nullptr;

  return IC.Builder.CreateICmp(DstPred, X, M);
}

/// Some comparisons can be simplified.
/// In this case, we are looking for comparisons that look like
/// a check for a lossy signed truncation.
/// Folds:   (MaskedBits is a constant.)
///   ((%x << MaskedBits) a>> MaskedBits) SrcPred %x
/// Into:
///   (add %x, (1 << (KeptBits-1))) DstPred (1 << KeptBits)
/// Where  KeptBits = bitwidth(%x) - MaskedBits
static Value *
foldICmpWithTruncSignExtendedVal(ICmpInst &I,
                                 InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate SrcPred;
  Value *X;
  const APInt *C0, *C1; // FIXME: non-splats, potentially with undef.
  // We are ok with 'shl' having multiple uses, but 'ashr' must be one-use.
  if (!match(&I, m_c_ICmp(SrcPred,
                          m_OneUse(m_AShr(m_Shl(m_Value(X), m_APInt(C0)),
                                          m_APInt(C1))),
                          m_Deferred(X))))
    return nullptr;

  // Potential handling of non-splats: for each element:
  //  * if both are undef, replace with constant 0.
  //    Because (1<<0) is OK and is 1, and ((1<<0)>>1) is also OK and is 0.
  //  * if both are not undef, and are different, bailout.
  //  * else, only one is undef, then pick the non-undef one.

  // The shift amount must be equal.
  if (*C0 != *C1)
    return nullptr;
  const APInt &MaskedBits = *C0;
  assert(MaskedBits != 0 && "shift by zero should be folded away already.");

  ICmpInst::Predicate DstPred;
  switch (SrcPred) {
  case ICmpInst::Predicate::ICMP_EQ:
    // ((%x << MaskedBits) a>> MaskedBits) == %x
    //   =>
    // (add %x, (1 << (KeptBits-1))) u< (1 << KeptBits)
    DstPred = ICmpInst::Predicate::ICMP_ULT;
    break;
  case ICmpInst::Predicate::ICMP_NE:
    // ((%x << MaskedBits) a>> MaskedBits) != %x
    //   =>
    // (add %x, (1 << (KeptBits-1))) u>= (1 << KeptBits)
    DstPred = ICmpInst::Predicate::ICMP_UGE;
    break;
  // FIXME: are more folds possible?
  default:
    return nullptr;
  }

  auto *XType = X->getType();
  const unsigned XBitWidth = XType->getScalarSizeInBits();
  const APInt BitWidth = APInt(XBitWidth, XBitWidth);
  assert(BitWidth.ugt(MaskedBits) && "shifts should leave some bits untouched");

  // KeptBits = bitwidth(%x) - MaskedBits
  const APInt KeptBits = BitWidth - MaskedBits;
  assert(KeptBits.ugt(0) && KeptBits.ult(BitWidth) && "unreachable");
  // ICmpCst = (1 << KeptBits)
  const APInt ICmpCst = APInt(XBitWidth, 1).shl(KeptBits);
  assert(ICmpCst.isPowerOf2());
  // AddCst = (1 << (KeptBits-1))
  const APInt AddCst = ICmpCst.lshr(1);
  assert(AddCst.ult(ICmpCst) && AddCst.isPowerOf2());

  // T0 = add %x, AddCst
  Value *T0 = Builder.CreateAdd(X, ConstantInt::get(XType, AddCst));
  // T1 = T0 DstPred ICmpCst
  Value *T1 = Builder.CreateICmp(DstPred, T0, ConstantInt::get(XType, ICmpCst));

  return T1;
}

// Given pattern:
//   icmp eq/ne (and ((x shift Q), (y oppositeshift K))), 0
// we should move shifts to the same hand of 'and', i.e. rewrite as
//   icmp eq/ne (and (x shift (Q+K)), y), 0  iff (Q+K) u< bitwidth(x)
// We are only interested in opposite logical shifts here.
// One of the shifts can be truncated.
// If we can, we want to end up creating 'lshr' shift.
static Value *
foldShiftIntoShiftInAnotherHandOfAndInICmp(ICmpInst &I, const SimplifyQuery SQ,
                                           InstCombiner::BuilderTy &Builder) {
  if (!I.isEquality() || !match(I.getOperand(1), m_Zero()) ||
      !I.getOperand(0)->hasOneUse())
    return nullptr;

  auto m_AnyLogicalShift = m_LogicalShift(m_Value(), m_Value());

  // Look for an 'and' of two logical shifts, one of which may be truncated.
  // We use m_TruncOrSelf() on the RHS to correctly handle commutative case.
  Instruction *XShift, *MaybeTruncation, *YShift;
  if (!match(
          I.getOperand(0),
          m_c_And(m_CombineAnd(m_AnyLogicalShift, m_Instruction(XShift)),
                  m_CombineAnd(m_TruncOrSelf(m_CombineAnd(
                                   m_AnyLogicalShift, m_Instruction(YShift))),
                               m_Instruction(MaybeTruncation)))))
    return nullptr;

  // We potentially looked past 'trunc', but only when matching YShift,
  // therefore YShift must have the widest type.
  Instruction *WidestShift = YShift;
  // Therefore XShift must have the shallowest type.
  // Or they both have identical types if there was no truncation.
  Instruction *NarrowestShift = XShift;

  Type *WidestTy = WidestShift->getType();
  Type *NarrowestTy = NarrowestShift->getType();
  assert(NarrowestTy == I.getOperand(0)->getType() &&
         "We did not look past any shifts while matching XShift though.");
  bool HadTrunc = WidestTy != I.getOperand(0)->getType();

  // If YShift is a 'lshr', swap the shifts around.
  if (match(YShift, m_LShr(m_Value(), m_Value())))
    std::swap(XShift, YShift);

  // The shifts must be in opposite directions.
  auto XShiftOpcode = XShift->getOpcode();
  if (XShiftOpcode == YShift->getOpcode())
    return nullptr; // Do not care about same-direction shifts here.

  Value *X, *XShAmt, *Y, *YShAmt;
  match(XShift, m_BinOp(m_Value(X), m_ZExtOrSelf(m_Value(XShAmt))));
  match(YShift, m_BinOp(m_Value(Y), m_ZExtOrSelf(m_Value(YShAmt))));

  // If one of the values being shifted is a constant, then we will end with
  // and+icmp, and [zext+]shift instrs will be constant-folded. If they are not,
  // however, we will need to ensure that we won't increase instruction count.
  if (!isa<Constant>(X) && !isa<Constant>(Y)) {
    // At least one of the hands of the 'and' should be one-use shift.
    if (!match(I.getOperand(0),
               m_c_And(m_OneUse(m_AnyLogicalShift), m_Value())))
      return nullptr;
    if (HadTrunc) {
      // Due to the 'trunc', we will need to widen X. For that either the old
      // 'trunc' or the shift amt in the non-truncated shift should be one-use.
      if (!MaybeTruncation->hasOneUse() &&
          !NarrowestShift->getOperand(1)->hasOneUse())
        return nullptr;
    }
  }

  // We have two shift amounts from two different shifts. The types of those
  // shift amounts may not match. If that's the case let's bailout now.
  if (XShAmt->getType() != YShAmt->getType())
    return nullptr;

  // As input, we have the following pattern:
  //   icmp eq/ne (and ((x shift Q), (y oppositeshift K))), 0
  // We want to rewrite that as:
  //   icmp eq/ne (and (x shift (Q+K)), y), 0  iff (Q+K) u< bitwidth(x)
  // While we know that originally (Q+K) would not overflow
  // (because  2 * (N-1) u<= iN -1), we have looked past extensions of
  // shift amounts. so it may now overflow in smaller bitwidth.
  // To ensure that does not happen, we need to ensure that the total maximal
  // shift amount is still representable in that smaller bit width.
  unsigned MaximalPossibleTotalShiftAmount =
      (WidestTy->getScalarSizeInBits() - 1) +
      (NarrowestTy->getScalarSizeInBits() - 1);
  APInt MaximalRepresentableShiftAmount =
      APInt::getAllOnes(XShAmt->getType()->getScalarSizeInBits());
  if (MaximalRepresentableShiftAmount.ult(MaximalPossibleTotalShiftAmount))
    return nullptr;

  // Can we fold (XShAmt+YShAmt) ?
  auto *NewShAmt = dyn_cast_or_null<Constant>(
      simplifyAddInst(XShAmt, YShAmt, /*isNSW=*/false,
                      /*isNUW=*/false, SQ.getWithInstruction(&I)));
  if (!NewShAmt)
    return nullptr;
  if (NewShAmt->getType() != WidestTy) {
    NewShAmt =
        ConstantFoldCastOperand(Instruction::ZExt, NewShAmt, WidestTy, SQ.DL);
    if (!NewShAmt)
      return nullptr;
  }
  unsigned WidestBitWidth = WidestTy->getScalarSizeInBits();

  // Is the new shift amount smaller than the bit width?
  // FIXME: could also rely on ConstantRange.
  if (!match(NewShAmt,
             m_SpecificInt_ICMP(ICmpInst::Predicate::ICMP_ULT,
                                APInt(WidestBitWidth, WidestBitWidth))))
    return nullptr;

  // An extra legality check is needed if we had trunc-of-lshr.
  if (HadTrunc && match(WidestShift, m_LShr(m_Value(), m_Value()))) {
    auto CanFold = [NewShAmt, WidestBitWidth, NarrowestShift, SQ,
                    WidestShift]() {
      // It isn't obvious whether it's worth it to analyze non-constants here.
      // Also, let's basically give up on non-splat cases, pessimizing vectors.
      // If *any* of these preconditions matches we can perform the fold.
      Constant *NewShAmtSplat = NewShAmt->getType()->isVectorTy()
                                    ? NewShAmt->getSplatValue()
                                    : NewShAmt;
      // If it's edge-case shift (by 0 or by WidestBitWidth-1) we can fold.
      if (NewShAmtSplat &&
          (NewShAmtSplat->isNullValue() ||
           NewShAmtSplat->getUniqueInteger() == WidestBitWidth - 1))
        return true;
      // We consider *min* leading zeros so a single outlier
      // blocks the transform as opposed to allowing it.
      if (auto *C = dyn_cast<Constant>(NarrowestShift->getOperand(0))) {
        KnownBits Known = computeKnownBits(C, SQ.DL);
        unsigned MinLeadZero = Known.countMinLeadingZeros();
        // If the value being shifted has at most lowest bit set we can fold.
        unsigned MaxActiveBits = Known.getBitWidth() - MinLeadZero;
        if (MaxActiveBits <= 1)
          return true;
        // Precondition:  NewShAmt u<= countLeadingZeros(C)
        if (NewShAmtSplat && NewShAmtSplat->getUniqueInteger().ule(MinLeadZero))
          return true;
      }
      if (auto *C = dyn_cast<Constant>(WidestShift->getOperand(0))) {
        KnownBits Known = computeKnownBits(C, SQ.DL);
        unsigned MinLeadZero = Known.countMinLeadingZeros();
        // If the value being shifted has at most lowest bit set we can fold.
        unsigned MaxActiveBits = Known.getBitWidth() - MinLeadZero;
        if (MaxActiveBits <= 1)
          return true;
        // Precondition:  ((WidestBitWidth-1)-NewShAmt) u<= countLeadingZeros(C)
        if (NewShAmtSplat) {
          APInt AdjNewShAmt =
              (WidestBitWidth - 1) - NewShAmtSplat->getUniqueInteger();
          if (AdjNewShAmt.ule(MinLeadZero))
            return true;
        }
      }
      return false; // Can't tell if it's ok.
    };
    if (!CanFold())
      return nullptr;
  }

  // All good, we can do this fold.
  X = Builder.CreateZExt(X, WidestTy);
  Y = Builder.CreateZExt(Y, WidestTy);
  // The shift is the same that was for X.
  Value *T0 = XShiftOpcode == Instruction::BinaryOps::LShr
                  ? Builder.CreateLShr(X, NewShAmt)
                  : Builder.CreateShl(X, NewShAmt);
  Value *T1 = Builder.CreateAnd(T0, Y);
  return Builder.CreateICmp(I.getPredicate(), T1,
                            Constant::getNullValue(WidestTy));
}

/// Fold
///   (-1 u/ x) u< y
///   ((x * y) ?/ x) != y
/// to
///   @llvm.?mul.with.overflow(x, y) plus extraction of overflow bit
/// Note that the comparison is commutative, while inverted (u>=, ==) predicate
/// will mean that we are looking for the opposite answer.
Value *InstCombinerImpl::foldMultiplicationOverflowCheck(ICmpInst &I) {
  ICmpInst::Predicate Pred;
  Value *X, *Y;
  Instruction *Mul;
  Instruction *Div;
  bool NeedNegation;
  // Look for: (-1 u/ x) u</u>= y
  if (!I.isEquality() &&
      match(&I, m_c_ICmp(Pred,
                         m_CombineAnd(m_OneUse(m_UDiv(m_AllOnes(), m_Value(X))),
                                      m_Instruction(Div)),
                         m_Value(Y)))) {
    Mul = nullptr;

    // Are we checking that overflow does not happen, or does happen?
    switch (Pred) {
    case ICmpInst::Predicate::ICMP_ULT:
      NeedNegation = false;
      break; // OK
    case ICmpInst::Predicate::ICMP_UGE:
      NeedNegation = true;
      break; // OK
    default:
      return nullptr; // Wrong predicate.
    }
  } else // Look for: ((x * y) / x) !=/== y
      if (I.isEquality() &&
          match(&I,
                m_c_ICmp(Pred, m_Value(Y),
                         m_CombineAnd(
                             m_OneUse(m_IDiv(m_CombineAnd(m_c_Mul(m_Deferred(Y),
                                                                  m_Value(X)),
                                                          m_Instruction(Mul)),
                                             m_Deferred(X))),
                             m_Instruction(Div))))) {
    NeedNegation = Pred == ICmpInst::Predicate::ICMP_EQ;
  } else
    return nullptr;

  BuilderTy::InsertPointGuard Guard(Builder);
  // If the pattern included (x * y), we'll want to insert new instructions
  // right before that original multiplication so that we can replace it.
  bool MulHadOtherUses = Mul && !Mul->hasOneUse();
  if (MulHadOtherUses)
    Builder.SetInsertPoint(Mul);

  Function *F = Intrinsic::getDeclaration(I.getModule(),
                                          Div->getOpcode() == Instruction::UDiv
                                              ? Intrinsic::umul_with_overflow
                                              : Intrinsic::smul_with_overflow,
                                          X->getType());
  CallInst *Call = Builder.CreateCall(F, {X, Y}, "mul");

  // If the multiplication was used elsewhere, to ensure that we don't leave
  // "duplicate" instructions, replace uses of that original multiplication
  // with the multiplication result from the with.overflow intrinsic.
  if (MulHadOtherUses)
    replaceInstUsesWith(*Mul, Builder.CreateExtractValue(Call, 0, "mul.val"));

  Value *Res = Builder.CreateExtractValue(Call, 1, "mul.ov");
  if (NeedNegation) // This technically increases instruction count.
    Res = Builder.CreateNot(Res, "mul.not.ov");

  // If we replaced the mul, erase it. Do this after all uses of Builder,
  // as the mul is used as insertion point.
  if (MulHadOtherUses)
    eraseInstFromFunction(*Mul);

  return Res;
}

static Instruction *foldICmpXNegX(ICmpInst &I,
                                  InstCombiner::BuilderTy &Builder) {
  CmpInst::Predicate Pred;
  Value *X;
  if (match(&I, m_c_ICmp(Pred, m_NSWNeg(m_Value(X)), m_Deferred(X)))) {

    if (ICmpInst::isSigned(Pred))
      Pred = ICmpInst::getSwappedPredicate(Pred);
    else if (ICmpInst::isUnsigned(Pred))
      Pred = ICmpInst::getSignedPredicate(Pred);
    // else for equality-comparisons just keep the predicate.

    return ICmpInst::Create(Instruction::ICmp, Pred, X,
                            Constant::getNullValue(X->getType()), I.getName());
  }

  // A value is not equal to its negation unless that value is 0 or
  // MinSignedValue, ie: a != -a --> (a & MaxSignedVal) != 0
  if (match(&I, m_c_ICmp(Pred, m_OneUse(m_Neg(m_Value(X))), m_Deferred(X))) &&
      ICmpInst::isEquality(Pred)) {
    Type *Ty = X->getType();
    uint32_t BitWidth = Ty->getScalarSizeInBits();
    Constant *MaxSignedVal =
        ConstantInt::get(Ty, APInt::getSignedMaxValue(BitWidth));
    Value *And = Builder.CreateAnd(X, MaxSignedVal);
    Constant *Zero = Constant::getNullValue(Ty);
    return CmpInst::Create(Instruction::ICmp, Pred, And, Zero);
  }

  return nullptr;
}

static Instruction *foldICmpAndXX(ICmpInst &I, const SimplifyQuery &Q,
                                  InstCombinerImpl &IC) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1), *A;
  // Normalize and operand as operand 0.
  CmpInst::Predicate Pred = I.getPredicate();
  if (match(Op1, m_c_And(m_Specific(Op0), m_Value()))) {
    std::swap(Op0, Op1);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  if (!match(Op0, m_c_And(m_Specific(Op1), m_Value(A))))
    return nullptr;

  // (icmp (X & Y) u< X --> (X & Y) != X
  if (Pred == ICmpInst::ICMP_ULT)
    return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);

  // (icmp (X & Y) u>= X --> (X & Y) == X
  if (Pred == ICmpInst::ICMP_UGE)
    return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);

  if (ICmpInst::isEquality(Pred) && Op0->hasOneUse()) {
    // icmp (X & Y) eq/ne Y --> (X | ~Y) eq/ne -1 if Y is freely invertible and
    // Y is non-constant. If Y is constant the `X & C == C` form is preferable
    // so don't do this fold.
    if (!match(Op1, m_ImmConstant()))
      if (auto *NotOp1 =
              IC.getFreelyInverted(Op1, !Op1->hasNUsesOrMore(3), &IC.Builder))
        return new ICmpInst(Pred, IC.Builder.CreateOr(A, NotOp1),
                            Constant::getAllOnesValue(Op1->getType()));
    // icmp (X & Y) eq/ne Y --> (~X & Y) eq/ne 0 if X  is freely invertible.
    if (auto *NotA = IC.getFreelyInverted(A, A->hasOneUse(), &IC.Builder))
      return new ICmpInst(Pred, IC.Builder.CreateAnd(Op1, NotA),
                          Constant::getNullValue(Op1->getType()));
  }

  if (!ICmpInst::isSigned(Pred))
    return nullptr;

  KnownBits KnownY = IC.computeKnownBits(A, /*Depth=*/0, &I);
  // (X & NegY) spred X --> (X & NegY) upred X
  if (KnownY.isNegative())
    return new ICmpInst(ICmpInst::getUnsignedPredicate(Pred), Op0, Op1);

  if (Pred != ICmpInst::ICMP_SLE && Pred != ICmpInst::ICMP_SGT)
    return nullptr;

  if (KnownY.isNonNegative())
    // (X & PosY) s<= X --> X s>= 0
    // (X & PosY) s> X --> X s< 0
    return new ICmpInst(ICmpInst::getSwappedPredicate(Pred), Op1,
                        Constant::getNullValue(Op1->getType()));

  if (isKnownNegative(Op1, IC.getSimplifyQuery().getWithInstruction(&I)))
    // (NegX & Y) s<= NegX --> Y s< 0
    // (NegX & Y) s> NegX --> Y s>= 0
    return new ICmpInst(ICmpInst::getFlippedStrictnessPredicate(Pred), A,
                        Constant::getNullValue(A->getType()));

  return nullptr;
}

static Instruction *foldICmpOrXX(ICmpInst &I, const SimplifyQuery &Q,
                                 InstCombinerImpl &IC) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1), *A;

  // Normalize or operand as operand 0.
  CmpInst::Predicate Pred = I.getPredicate();
  if (match(Op1, m_c_Or(m_Specific(Op0), m_Value(A)))) {
    std::swap(Op0, Op1);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  } else if (!match(Op0, m_c_Or(m_Specific(Op1), m_Value(A)))) {
    return nullptr;
  }

  // icmp (X | Y) u<= X --> (X | Y) == X
  if (Pred == ICmpInst::ICMP_ULE)
    return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);

  // icmp (X | Y) u> X --> (X | Y) != X
  if (Pred == ICmpInst::ICMP_UGT)
    return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);

  if (ICmpInst::isEquality(Pred) && Op0->hasOneUse()) {
    // icmp (X | Y) eq/ne Y --> (X & ~Y) eq/ne 0 if Y is freely invertible
    if (Value *NotOp1 =
            IC.getFreelyInverted(Op1, !Op1->hasNUsesOrMore(3), &IC.Builder))
      return new ICmpInst(Pred, IC.Builder.CreateAnd(A, NotOp1),
                          Constant::getNullValue(Op1->getType()));
    // icmp (X | Y) eq/ne Y --> (~X | Y) eq/ne -1 if X  is freely invertible.
    if (Value *NotA = IC.getFreelyInverted(A, A->hasOneUse(), &IC.Builder))
      return new ICmpInst(Pred, IC.Builder.CreateOr(Op1, NotA),
                          Constant::getAllOnesValue(Op1->getType()));
  }
  return nullptr;
}

static Instruction *foldICmpXorXX(ICmpInst &I, const SimplifyQuery &Q,
                                  InstCombinerImpl &IC) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1), *A;
  // Normalize xor operand as operand 0.
  CmpInst::Predicate Pred = I.getPredicate();
  if (match(Op1, m_c_Xor(m_Specific(Op0), m_Value()))) {
    std::swap(Op0, Op1);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }
  if (!match(Op0, m_c_Xor(m_Specific(Op1), m_Value(A))))
    return nullptr;

  // icmp (X ^ Y_NonZero) u>= X --> icmp (X ^ Y_NonZero) u> X
  // icmp (X ^ Y_NonZero) u<= X --> icmp (X ^ Y_NonZero) u< X
  // icmp (X ^ Y_NonZero) s>= X --> icmp (X ^ Y_NonZero) s> X
  // icmp (X ^ Y_NonZero) s<= X --> icmp (X ^ Y_NonZero) s< X
  CmpInst::Predicate PredOut = CmpInst::getStrictPredicate(Pred);
  if (PredOut != Pred && isKnownNonZero(A, Q))
    return new ICmpInst(PredOut, Op0, Op1);

  return nullptr;
}

/// Try to fold icmp (binop), X or icmp X, (binop).
/// TODO: A large part of this logic is duplicated in InstSimplify's
/// simplifyICmpWithBinOp(). We should be able to share that and avoid the code
/// duplication.
Instruction *InstCombinerImpl::foldICmpBinOp(ICmpInst &I,
                                             const SimplifyQuery &SQ) {
  const SimplifyQuery Q = SQ.getWithInstruction(&I);
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  // Special logic for binary operators.
  BinaryOperator *BO0 = dyn_cast<BinaryOperator>(Op0);
  BinaryOperator *BO1 = dyn_cast<BinaryOperator>(Op1);
  if (!BO0 && !BO1)
    return nullptr;

  if (Instruction *NewICmp = foldICmpXNegX(I, Builder))
    return NewICmp;

  const CmpInst::Predicate Pred = I.getPredicate();
  Value *X;

  // Convert add-with-unsigned-overflow comparisons into a 'not' with compare.
  // (Op1 + X) u</u>= Op1 --> ~Op1 u</u>= X
  if (match(Op0, m_OneUse(m_c_Add(m_Specific(Op1), m_Value(X)))) &&
      (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_UGE))
    return new ICmpInst(Pred, Builder.CreateNot(Op1), X);
  // Op0 u>/u<= (Op0 + X) --> X u>/u<= ~Op0
  if (match(Op1, m_OneUse(m_c_Add(m_Specific(Op0), m_Value(X)))) &&
      (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_ULE))
    return new ICmpInst(Pred, X, Builder.CreateNot(Op0));

  {
    // (Op1 + X) + C u</u>= Op1 --> ~C - X u</u>= Op1
    Constant *C;
    if (match(Op0, m_OneUse(m_Add(m_c_Add(m_Specific(Op1), m_Value(X)),
                                  m_ImmConstant(C)))) &&
        (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_UGE)) {
      Constant *C2 = ConstantExpr::getNot(C);
      return new ICmpInst(Pred, Builder.CreateSub(C2, X), Op1);
    }
    // Op0 u>/u<= (Op0 + X) + C --> Op0 u>/u<= ~C - X
    if (match(Op1, m_OneUse(m_Add(m_c_Add(m_Specific(Op0), m_Value(X)),
                                  m_ImmConstant(C)))) &&
        (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_ULE)) {
      Constant *C2 = ConstantExpr::getNot(C);
      return new ICmpInst(Pred, Op0, Builder.CreateSub(C2, X));
    }
  }

  {
    // Similar to above: an unsigned overflow comparison may use offset + mask:
    // ((Op1 + C) & C) u<  Op1 --> Op1 != 0
    // ((Op1 + C) & C) u>= Op1 --> Op1 == 0
    // Op0 u>  ((Op0 + C) & C) --> Op0 != 0
    // Op0 u<= ((Op0 + C) & C) --> Op0 == 0
    BinaryOperator *BO;
    const APInt *C;
    if ((Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_UGE) &&
        match(Op0, m_And(m_BinOp(BO), m_LowBitMask(C))) &&
        match(BO, m_Add(m_Specific(Op1), m_SpecificIntAllowPoison(*C)))) {
      CmpInst::Predicate NewPred =
          Pred == ICmpInst::ICMP_ULT ? ICmpInst::ICMP_NE : ICmpInst::ICMP_EQ;
      Constant *Zero = ConstantInt::getNullValue(Op1->getType());
      return new ICmpInst(NewPred, Op1, Zero);
    }

    if ((Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_ULE) &&
        match(Op1, m_And(m_BinOp(BO), m_LowBitMask(C))) &&
        match(BO, m_Add(m_Specific(Op0), m_SpecificIntAllowPoison(*C)))) {
      CmpInst::Predicate NewPred =
          Pred == ICmpInst::ICMP_UGT ? ICmpInst::ICMP_NE : ICmpInst::ICMP_EQ;
      Constant *Zero = ConstantInt::getNullValue(Op1->getType());
      return new ICmpInst(NewPred, Op0, Zero);
    }
  }

  bool NoOp0WrapProblem = false, NoOp1WrapProblem = false;
  bool Op0HasNUW = false, Op1HasNUW = false;
  bool Op0HasNSW = false, Op1HasNSW = false;
  // Analyze the case when either Op0 or Op1 is an add instruction.
  // Op0 = A + B (or A and B are null); Op1 = C + D (or C and D are null).
  auto hasNoWrapProblem = [](const BinaryOperator &BO, CmpInst::Predicate Pred,
                             bool &HasNSW, bool &HasNUW) -> bool {
    if (isa<OverflowingBinaryOperator>(BO)) {
      HasNUW = BO.hasNoUnsignedWrap();
      HasNSW = BO.hasNoSignedWrap();
      return ICmpInst::isEquality(Pred) ||
             (CmpInst::isUnsigned(Pred) && HasNUW) ||
             (CmpInst::isSigned(Pred) && HasNSW);
    } else if (BO.getOpcode() == Instruction::Or) {
      HasNUW = true;
      HasNSW = true;
      return true;
    } else {
      return false;
    }
  };
  Value *A = nullptr, *B = nullptr, *C = nullptr, *D = nullptr;

  if (BO0) {
    match(BO0, m_AddLike(m_Value(A), m_Value(B)));
    NoOp0WrapProblem = hasNoWrapProblem(*BO0, Pred, Op0HasNSW, Op0HasNUW);
  }
  if (BO1) {
    match(BO1, m_AddLike(m_Value(C), m_Value(D)));
    NoOp1WrapProblem = hasNoWrapProblem(*BO1, Pred, Op1HasNSW, Op1HasNUW);
  }

  // icmp (A+B), A -> icmp B, 0 for equalities or if there is no overflow.
  // icmp (A+B), B -> icmp A, 0 for equalities or if there is no overflow.
  if ((A == Op1 || B == Op1) && NoOp0WrapProblem)
    return new ICmpInst(Pred, A == Op1 ? B : A,
                        Constant::getNullValue(Op1->getType()));

  // icmp C, (C+D) -> icmp 0, D for equalities or if there is no overflow.
  // icmp D, (C+D) -> icmp 0, C for equalities or if there is no overflow.
  if ((C == Op0 || D == Op0) && NoOp1WrapProblem)
    return new ICmpInst(Pred, Constant::getNullValue(Op0->getType()),
                        C == Op0 ? D : C);

  // icmp (A+B), (A+D) -> icmp B, D for equalities or if there is no overflow.
  if (A && C && (A == C || A == D || B == C || B == D) && NoOp0WrapProblem &&
      NoOp1WrapProblem) {
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
    return new ICmpInst(Pred, Y, Z);
  }

  // icmp slt (A + -1), Op1 -> icmp sle A, Op1
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SLT &&
      match(B, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SLE, A, Op1);

  // icmp sge (A + -1), Op1 -> icmp sgt A, Op1
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SGE &&
      match(B, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SGT, A, Op1);

  // icmp sle (A + 1), Op1 -> icmp slt A, Op1
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SLE && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_SLT, A, Op1);

  // icmp sgt (A + 1), Op1 -> icmp sge A, Op1
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_SGT && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_SGE, A, Op1);

  // icmp sgt Op0, (C + -1) -> icmp sge Op0, C
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SGT &&
      match(D, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SGE, Op0, C);

  // icmp sle Op0, (C + -1) -> icmp slt Op0, C
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SLE &&
      match(D, m_AllOnes()))
    return new ICmpInst(CmpInst::ICMP_SLT, Op0, C);

  // icmp sge Op0, (C + 1) -> icmp sgt Op0, C
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SGE && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_SGT, Op0, C);

  // icmp slt Op0, (C + 1) -> icmp sle Op0, C
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_SLT && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_SLE, Op0, C);

  // TODO: The subtraction-related identities shown below also hold, but
  // canonicalization from (X -nuw 1) to (X + -1) means that the combinations
  // wouldn't happen even if they were implemented.
  //
  // icmp ult (A - 1), Op1 -> icmp ule A, Op1
  // icmp uge (A - 1), Op1 -> icmp ugt A, Op1
  // icmp ugt Op0, (C - 1) -> icmp uge Op0, C
  // icmp ule Op0, (C - 1) -> icmp ult Op0, C

  // icmp ule (A + 1), Op0 -> icmp ult A, Op1
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_ULE && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_ULT, A, Op1);

  // icmp ugt (A + 1), Op0 -> icmp uge A, Op1
  if (A && NoOp0WrapProblem && Pred == CmpInst::ICMP_UGT && match(B, m_One()))
    return new ICmpInst(CmpInst::ICMP_UGE, A, Op1);

  // icmp uge Op0, (C + 1) -> icmp ugt Op0, C
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_UGE && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_UGT, Op0, C);

  // icmp ult Op0, (C + 1) -> icmp ule Op0, C
  if (C && NoOp1WrapProblem && Pred == CmpInst::ICMP_ULT && match(D, m_One()))
    return new ICmpInst(CmpInst::ICMP_ULE, Op0, C);

  // if C1 has greater magnitude than C2:
  //  icmp (A + C1), (C + C2) -> icmp (A + C3), C
  //  s.t. C3 = C1 - C2
  //
  // if C2 has greater magnitude than C1:
  //  icmp (A + C1), (C + C2) -> icmp A, (C + C3)
  //  s.t. C3 = C2 - C1
  if (A && C && NoOp0WrapProblem && NoOp1WrapProblem &&
      (BO0->hasOneUse() || BO1->hasOneUse()) && !I.isUnsigned()) {
    const APInt *AP1, *AP2;
    // TODO: Support non-uniform vectors.
    // TODO: Allow poison passthrough if B or D's element is poison.
    if (match(B, m_APIntAllowPoison(AP1)) &&
        match(D, m_APIntAllowPoison(AP2)) &&
        AP1->isNegative() == AP2->isNegative()) {
      APInt AP1Abs = AP1->abs();
      APInt AP2Abs = AP2->abs();
      if (AP1Abs.uge(AP2Abs)) {
        APInt Diff = *AP1 - *AP2;
        Constant *C3 = Constant::getIntegerValue(BO0->getType(), Diff);
        Value *NewAdd = Builder.CreateAdd(
            A, C3, "", Op0HasNUW && Diff.ule(*AP1), Op0HasNSW);
        return new ICmpInst(Pred, NewAdd, C);
      } else {
        APInt Diff = *AP2 - *AP1;
        Constant *C3 = Constant::getIntegerValue(BO0->getType(), Diff);
        Value *NewAdd = Builder.CreateAdd(
            C, C3, "", Op1HasNUW && Diff.ule(*AP2), Op1HasNSW);
        return new ICmpInst(Pred, A, NewAdd);
      }
    }
    Constant *Cst1, *Cst2;
    if (match(B, m_ImmConstant(Cst1)) && match(D, m_ImmConstant(Cst2)) &&
        ICmpInst::isEquality(Pred)) {
      Constant *Diff = ConstantExpr::getSub(Cst2, Cst1);
      Value *NewAdd = Builder.CreateAdd(C, Diff);
      return new ICmpInst(Pred, A, NewAdd);
    }
  }

  // Analyze the case when either Op0 or Op1 is a sub instruction.
  // Op0 = A - B (or A and B are null); Op1 = C - D (or C and D are null).
  A = nullptr;
  B = nullptr;
  C = nullptr;
  D = nullptr;
  if (BO0 && BO0->getOpcode() == Instruction::Sub) {
    A = BO0->getOperand(0);
    B = BO0->getOperand(1);
  }
  if (BO1 && BO1->getOpcode() == Instruction::Sub) {
    C = BO1->getOperand(0);
    D = BO1->getOperand(1);
  }

  // icmp (A-B), A -> icmp 0, B for equalities or if there is no overflow.
  if (A == Op1 && NoOp0WrapProblem)
    return new ICmpInst(Pred, Constant::getNullValue(Op1->getType()), B);
  // icmp C, (C-D) -> icmp D, 0 for equalities or if there is no overflow.
  if (C == Op0 && NoOp1WrapProblem)
    return new ICmpInst(Pred, D, Constant::getNullValue(Op0->getType()));

  // Convert sub-with-unsigned-overflow comparisons into a comparison of args.
  // (A - B) u>/u<= A --> B u>/u<= A
  if (A == Op1 && (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_ULE))
    return new ICmpInst(Pred, B, A);
  // C u</u>= (C - D) --> C u</u>= D
  if (C == Op0 && (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_UGE))
    return new ICmpInst(Pred, C, D);
  // (A - B) u>=/u< A --> B u>/u<= A  iff B != 0
  if (A == Op1 && (Pred == ICmpInst::ICMP_UGE || Pred == ICmpInst::ICMP_ULT) &&
      isKnownNonZero(B, Q))
    return new ICmpInst(CmpInst::getFlippedStrictnessPredicate(Pred), B, A);
  // C u<=/u> (C - D) --> C u</u>= D  iff B != 0
  if (C == Op0 && (Pred == ICmpInst::ICMP_ULE || Pred == ICmpInst::ICMP_UGT) &&
      isKnownNonZero(D, Q))
    return new ICmpInst(CmpInst::getFlippedStrictnessPredicate(Pred), C, D);

  // icmp (A-B), (C-B) -> icmp A, C for equalities or if there is no overflow.
  if (B && D && B == D && NoOp0WrapProblem && NoOp1WrapProblem)
    return new ICmpInst(Pred, A, C);

  // icmp (A-B), (A-D) -> icmp D, B for equalities or if there is no overflow.
  if (A && C && A == C && NoOp0WrapProblem && NoOp1WrapProblem)
    return new ICmpInst(Pred, D, B);

  // icmp (0-X) < cst --> x > -cst
  if (NoOp0WrapProblem && ICmpInst::isSigned(Pred)) {
    Value *X;
    if (match(BO0, m_Neg(m_Value(X))))
      if (Constant *RHSC = dyn_cast<Constant>(Op1))
        if (RHSC->isNotMinSignedValue())
          return new ICmpInst(I.getSwappedPredicate(), X,
                              ConstantExpr::getNeg(RHSC));
  }

  if (Instruction * R = foldICmpXorXX(I, Q, *this))
    return R;
  if (Instruction *R = foldICmpOrXX(I, Q, *this))
    return R;

  {
    // Try to remove shared multiplier from comparison:
    // X * Z u{lt/le/gt/ge}/eq/ne Y * Z
    Value *X, *Y, *Z;
    if (Pred == ICmpInst::getUnsignedPredicate(Pred) &&
        ((match(Op0, m_Mul(m_Value(X), m_Value(Z))) &&
          match(Op1, m_c_Mul(m_Specific(Z), m_Value(Y)))) ||
         (match(Op0, m_Mul(m_Value(Z), m_Value(X))) &&
          match(Op1, m_c_Mul(m_Specific(Z), m_Value(Y)))))) {
      bool NonZero;
      if (ICmpInst::isEquality(Pred)) {
        KnownBits ZKnown = computeKnownBits(Z, 0, &I);
        // if Z % 2 != 0
        //    X * Z eq/ne Y * Z -> X eq/ne Y
        if (ZKnown.countMaxTrailingZeros() == 0)
          return new ICmpInst(Pred, X, Y);
        NonZero = !ZKnown.One.isZero() || isKnownNonZero(Z, Q);
        // if Z != 0 and nsw(X * Z) and nsw(Y * Z)
        //    X * Z eq/ne Y * Z -> X eq/ne Y
        if (NonZero && BO0 && BO1 && Op0HasNSW && Op1HasNSW)
          return new ICmpInst(Pred, X, Y);
      } else
        NonZero = isKnownNonZero(Z, Q);

      // If Z != 0 and nuw(X * Z) and nuw(Y * Z)
      //    X * Z u{lt/le/gt/ge}/eq/ne Y * Z -> X u{lt/le/gt/ge}/eq/ne Y
      if (NonZero && BO0 && BO1 && Op0HasNUW && Op1HasNUW)
        return new ICmpInst(Pred, X, Y);
    }
  }

  BinaryOperator *SRem = nullptr;
  // icmp (srem X, Y), Y
  if (BO0 && BO0->getOpcode() == Instruction::SRem && Op1 == BO0->getOperand(1))
    SRem = BO0;
  // icmp Y, (srem X, Y)
  else if (BO1 && BO1->getOpcode() == Instruction::SRem &&
           Op0 == BO1->getOperand(1))
    SRem = BO1;
  if (SRem) {
    // We don't check hasOneUse to avoid increasing register pressure because
    // the value we use is the same value this instruction was already using.
    switch (SRem == BO0 ? ICmpInst::getSwappedPredicate(Pred) : Pred) {
    default:
      break;
    case ICmpInst::ICMP_EQ:
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    case ICmpInst::ICMP_NE:
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
      return new ICmpInst(ICmpInst::ICMP_SGT, SRem->getOperand(1),
                          Constant::getAllOnesValue(SRem->getType()));
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
      return new ICmpInst(ICmpInst::ICMP_SLT, SRem->getOperand(1),
                          Constant::getNullValue(SRem->getType()));
    }
  }

  if (BO0 && BO1 && BO0->getOpcode() == BO1->getOpcode() &&
      (BO0->hasOneUse() || BO1->hasOneUse()) &&
      BO0->getOperand(1) == BO1->getOperand(1)) {
    switch (BO0->getOpcode()) {
    default:
      break;
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Xor: {
      if (I.isEquality()) // a+x icmp eq/ne b+x --> a icmp b
        return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

      const APInt *C;
      if (match(BO0->getOperand(1), m_APInt(C))) {
        // icmp u/s (a ^ signmask), (b ^ signmask) --> icmp s/u a, b
        if (C->isSignMask()) {
          ICmpInst::Predicate NewPred = I.getFlippedSignednessPredicate();
          return new ICmpInst(NewPred, BO0->getOperand(0), BO1->getOperand(0));
        }

        // icmp u/s (a ^ maxsignval), (b ^ maxsignval) --> icmp s/u' a, b
        if (BO0->getOpcode() == Instruction::Xor && C->isMaxSignedValue()) {
          ICmpInst::Predicate NewPred = I.getFlippedSignednessPredicate();
          NewPred = I.getSwappedPredicate(NewPred);
          return new ICmpInst(NewPred, BO0->getOperand(0), BO1->getOperand(0));
        }
      }
      break;
    }
    case Instruction::Mul: {
      if (!I.isEquality())
        break;

      const APInt *C;
      if (match(BO0->getOperand(1), m_APInt(C)) && !C->isZero() &&
          !C->isOne()) {
        // icmp eq/ne (X * C), (Y * C) --> icmp (X & Mask), (Y & Mask)
        // Mask = -1 >> count-trailing-zeros(C).
        if (unsigned TZs = C->countr_zero()) {
          Constant *Mask = ConstantInt::get(
              BO0->getType(),
              APInt::getLowBitsSet(C->getBitWidth(), C->getBitWidth() - TZs));
          Value *And1 = Builder.CreateAnd(BO0->getOperand(0), Mask);
          Value *And2 = Builder.CreateAnd(BO1->getOperand(0), Mask);
          return new ICmpInst(Pred, And1, And2);
        }
      }
      break;
    }
    case Instruction::UDiv:
    case Instruction::LShr:
      if (I.isSigned() || !BO0->isExact() || !BO1->isExact())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

    case Instruction::SDiv:
      if (!(I.isEquality() || match(BO0->getOperand(1), m_NonNegative())) ||
          !BO0->isExact() || !BO1->isExact())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

    case Instruction::AShr:
      if (!BO0->isExact() || !BO1->isExact())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));

    case Instruction::Shl: {
      bool NUW = Op0HasNUW && Op1HasNUW;
      bool NSW = Op0HasNSW && Op1HasNSW;
      if (!NUW && !NSW)
        break;
      if (!NSW && I.isSigned())
        break;
      return new ICmpInst(Pred, BO0->getOperand(0), BO1->getOperand(0));
    }
    }
  }

  if (BO0) {
    // Transform  A & (L - 1) `ult` L --> L != 0
    auto LSubOne = m_Add(m_Specific(Op1), m_AllOnes());
    auto BitwiseAnd = m_c_And(m_Value(), LSubOne);

    if (match(BO0, BitwiseAnd) && Pred == ICmpInst::ICMP_ULT) {
      auto *Zero = Constant::getNullValue(BO0->getType());
      return new ICmpInst(ICmpInst::ICMP_NE, Op1, Zero);
    }
  }

  // For unsigned predicates / eq / ne:
  // icmp pred (x << 1), x --> icmp getSignedPredicate(pred) x, 0
  // icmp pred x, (x << 1) --> icmp getSignedPredicate(pred) 0, x
  if (!ICmpInst::isSigned(Pred)) {
    if (match(Op0, m_Shl(m_Specific(Op1), m_One())))
      return new ICmpInst(ICmpInst::getSignedPredicate(Pred), Op1,
                          Constant::getNullValue(Op1->getType()));
    else if (match(Op1, m_Shl(m_Specific(Op0), m_One())))
      return new ICmpInst(ICmpInst::getSignedPredicate(Pred),
                          Constant::getNullValue(Op0->getType()), Op0);
  }

  if (Value *V = foldMultiplicationOverflowCheck(I))
    return replaceInstUsesWith(I, V);

  if (Instruction *R = foldICmpAndXX(I, Q, *this))
    return R;

  if (Value *V = foldICmpWithTruncSignExtendedVal(I, Builder))
    return replaceInstUsesWith(I, V);

  if (Value *V = foldShiftIntoShiftInAnotherHandOfAndInICmp(I, SQ, Builder))
    return replaceInstUsesWith(I, V);

  return nullptr;
}

/// Fold icmp Pred min|max(X, Y), Z.
Instruction *InstCombinerImpl::foldICmpWithMinMax(Instruction &I,
                                                  MinMaxIntrinsic *MinMax,
                                                  Value *Z,
                                                  ICmpInst::Predicate Pred) {
  Value *X = MinMax->getLHS();
  Value *Y = MinMax->getRHS();
  if (ICmpInst::isSigned(Pred) && !MinMax->isSigned())
    return nullptr;
  if (ICmpInst::isUnsigned(Pred) && MinMax->isSigned()) {
    // Revert the transform signed pred -> unsigned pred
    // TODO: We can flip the signedness of predicate if both operands of icmp
    // are negative.
    if (isKnownNonNegative(Z, SQ.getWithInstruction(&I)) &&
        isKnownNonNegative(MinMax, SQ.getWithInstruction(&I))) {
      Pred = ICmpInst::getFlippedSignednessPredicate(Pred);
    } else
      return nullptr;
  }
  SimplifyQuery Q = SQ.getWithInstruction(&I);
  auto IsCondKnownTrue = [](Value *Val) -> std::optional<bool> {
    if (!Val)
      return std::nullopt;
    if (match(Val, m_One()))
      return true;
    if (match(Val, m_Zero()))
      return false;
    return std::nullopt;
  };
  auto CmpXZ = IsCondKnownTrue(simplifyICmpInst(Pred, X, Z, Q));
  auto CmpYZ = IsCondKnownTrue(simplifyICmpInst(Pred, Y, Z, Q));
  if (!CmpXZ.has_value() && !CmpYZ.has_value())
    return nullptr;
  if (!CmpXZ.has_value()) {
    std::swap(X, Y);
    std::swap(CmpXZ, CmpYZ);
  }

  auto FoldIntoCmpYZ = [&]() -> Instruction * {
    if (CmpYZ.has_value())
      return replaceInstUsesWith(I, ConstantInt::getBool(I.getType(), *CmpYZ));
    return ICmpInst::Create(Instruction::ICmp, Pred, Y, Z);
  };

  switch (Pred) {
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_NE: {
    // If X == Z:
    //     Expr       Result
    // min(X, Y) == Z X <= Y
    // max(X, Y) == Z X >= Y
    // min(X, Y) != Z X > Y
    // max(X, Y) != Z X < Y
    if ((Pred == ICmpInst::ICMP_EQ) == *CmpXZ) {
      ICmpInst::Predicate NewPred =
          ICmpInst::getNonStrictPredicate(MinMax->getPredicate());
      if (Pred == ICmpInst::ICMP_NE)
        NewPred = ICmpInst::getInversePredicate(NewPred);
      return ICmpInst::Create(Instruction::ICmp, NewPred, X, Y);
    }
    // Otherwise (X != Z):
    ICmpInst::Predicate NewPred = MinMax->getPredicate();
    auto MinMaxCmpXZ = IsCondKnownTrue(simplifyICmpInst(NewPred, X, Z, Q));
    if (!MinMaxCmpXZ.has_value()) {
      std::swap(X, Y);
      std::swap(CmpXZ, CmpYZ);
      // Re-check pre-condition X != Z
      if (!CmpXZ.has_value() || (Pred == ICmpInst::ICMP_EQ) == *CmpXZ)
        break;
      MinMaxCmpXZ = IsCondKnownTrue(simplifyICmpInst(NewPred, X, Z, Q));
    }
    if (!MinMaxCmpXZ.has_value())
      break;
    if (*MinMaxCmpXZ) {
      //    Expr         Fact    Result
      // min(X, Y) == Z  X < Z   false
      // max(X, Y) == Z  X > Z   false
      // min(X, Y) != Z  X < Z    true
      // max(X, Y) != Z  X > Z    true
      return replaceInstUsesWith(
          I, ConstantInt::getBool(I.getType(), Pred == ICmpInst::ICMP_NE));
    } else {
      //    Expr         Fact    Result
      // min(X, Y) == Z  X > Z   Y == Z
      // max(X, Y) == Z  X < Z   Y == Z
      // min(X, Y) != Z  X > Z   Y != Z
      // max(X, Y) != Z  X < Z   Y != Z
      return FoldIntoCmpYZ();
    }
    break;
  }
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_SLE:
  case ICmpInst::ICMP_ULE:
  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_SGE:
  case ICmpInst::ICMP_UGE: {
    bool IsSame = MinMax->getPredicate() == ICmpInst::getStrictPredicate(Pred);
    if (*CmpXZ) {
      if (IsSame) {
        //      Expr        Fact    Result
        // min(X, Y) < Z    X < Z   true
        // min(X, Y) <= Z   X <= Z  true
        // max(X, Y) > Z    X > Z   true
        // max(X, Y) >= Z   X >= Z  true
        return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
      } else {
        //      Expr        Fact    Result
        // max(X, Y) < Z    X < Z   Y < Z
        // max(X, Y) <= Z   X <= Z  Y <= Z
        // min(X, Y) > Z    X > Z   Y > Z
        // min(X, Y) >= Z   X >= Z  Y >= Z
        return FoldIntoCmpYZ();
      }
    } else {
      if (IsSame) {
        //      Expr        Fact    Result
        // min(X, Y) < Z    X >= Z  Y < Z
        // min(X, Y) <= Z   X > Z   Y <= Z
        // max(X, Y) > Z    X <= Z  Y > Z
        // max(X, Y) >= Z   X < Z   Y >= Z
        return FoldIntoCmpYZ();
      } else {
        //      Expr        Fact    Result
        // max(X, Y) < Z    X >= Z  false
        // max(X, Y) <= Z   X > Z   false
        // min(X, Y) > Z    X <= Z  false
        // min(X, Y) >= Z   X < Z   false
        return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
      }
    }
    break;
  }
  default:
    break;
  }

  return nullptr;
}

// Canonicalize checking for a power-of-2-or-zero value:
static Instruction *foldICmpPow2Test(ICmpInst &I,
                                     InstCombiner::BuilderTy &Builder) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  const CmpInst::Predicate Pred = I.getPredicate();
  Value *A = nullptr;
  bool CheckIs;
  if (I.isEquality()) {
    // (A & (A-1)) == 0 --> ctpop(A) < 2 (two commuted variants)
    // ((A-1) & A) != 0 --> ctpop(A) > 1 (two commuted variants)
    if (!match(Op0, m_OneUse(m_c_And(m_Add(m_Value(A), m_AllOnes()),
                                     m_Deferred(A)))) ||
        !match(Op1, m_ZeroInt()))
      A = nullptr;

    // (A & -A) == A --> ctpop(A) < 2 (four commuted variants)
    // (-A & A) != A --> ctpop(A) > 1 (four commuted variants)
    if (match(Op0, m_OneUse(m_c_And(m_Neg(m_Specific(Op1)), m_Specific(Op1)))))
      A = Op1;
    else if (match(Op1,
                   m_OneUse(m_c_And(m_Neg(m_Specific(Op0)), m_Specific(Op0)))))
      A = Op0;

    CheckIs = Pred == ICmpInst::ICMP_EQ;
  } else if (ICmpInst::isUnsigned(Pred)) {
    // (A ^ (A-1)) u>= A --> ctpop(A) < 2 (two commuted variants)
    // ((A-1) ^ A) u< A --> ctpop(A) > 1 (two commuted variants)

    if ((Pred == ICmpInst::ICMP_UGE || Pred == ICmpInst::ICMP_ULT) &&
        match(Op0, m_OneUse(m_c_Xor(m_Add(m_Specific(Op1), m_AllOnes()),
                                    m_Specific(Op1))))) {
      A = Op1;
      CheckIs = Pred == ICmpInst::ICMP_UGE;
    } else if ((Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_ULE) &&
               match(Op1, m_OneUse(m_c_Xor(m_Add(m_Specific(Op0), m_AllOnes()),
                                           m_Specific(Op0))))) {
      A = Op0;
      CheckIs = Pred == ICmpInst::ICMP_ULE;
    }
  }

  if (A) {
    Type *Ty = A->getType();
    CallInst *CtPop = Builder.CreateUnaryIntrinsic(Intrinsic::ctpop, A);
    return CheckIs ? new ICmpInst(ICmpInst::ICMP_ULT, CtPop,
                                  ConstantInt::get(Ty, 2))
                   : new ICmpInst(ICmpInst::ICMP_UGT, CtPop,
                                  ConstantInt::get(Ty, 1));
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldICmpEquality(ICmpInst &I) {
  if (!I.isEquality())
    return nullptr;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  const CmpInst::Predicate Pred = I.getPredicate();
  Value *A, *B, *C, *D;
  if (match(Op0, m_Xor(m_Value(A), m_Value(B)))) {
    if (A == Op1 || B == Op1) { // (A^B) == A  ->  B == 0
      Value *OtherVal = A == Op1 ? B : A;
      return new ICmpInst(Pred, OtherVal, Constant::getNullValue(A->getType()));
    }

    if (match(Op1, m_Xor(m_Value(C), m_Value(D)))) {
      // A^c1 == C^c2 --> A == C^(c1^c2)
      ConstantInt *C1, *C2;
      if (match(B, m_ConstantInt(C1)) && match(D, m_ConstantInt(C2)) &&
          Op1->hasOneUse()) {
        Constant *NC = Builder.getInt(C1->getValue() ^ C2->getValue());
        Value *Xor = Builder.CreateXor(C, NC);
        return new ICmpInst(Pred, A, Xor);
      }

      // A^B == A^D -> B == D
      if (A == C)
        return new ICmpInst(Pred, B, D);
      if (A == D)
        return new ICmpInst(Pred, B, C);
      if (B == C)
        return new ICmpInst(Pred, A, D);
      if (B == D)
        return new ICmpInst(Pred, A, C);
    }
  }

  if (match(Op1, m_Xor(m_Value(A), m_Value(B))) && (A == Op0 || B == Op0)) {
    // A == (A^B)  ->  B == 0
    Value *OtherVal = A == Op0 ? B : A;
    return new ICmpInst(Pred, OtherVal, Constant::getNullValue(A->getType()));
  }

  // (X&Z) == (Y&Z) -> (X^Y) & Z == 0
  if (match(Op0, m_And(m_Value(A), m_Value(B))) &&
      match(Op1, m_And(m_Value(C), m_Value(D)))) {
    Value *X = nullptr, *Y = nullptr, *Z = nullptr;

    if (A == C) {
      X = B;
      Y = D;
      Z = A;
    } else if (A == D) {
      X = B;
      Y = C;
      Z = A;
    } else if (B == C) {
      X = A;
      Y = D;
      Z = B;
    } else if (B == D) {
      X = A;
      Y = C;
      Z = B;
    }

    if (X) {
      // If X^Y is a negative power of two, then `icmp eq/ne (Z & NegP2), 0`
      // will fold to `icmp ult/uge Z, -NegP2` incurringb no additional
      // instructions.
      const APInt *C0, *C1;
      bool XorIsNegP2 = match(X, m_APInt(C0)) && match(Y, m_APInt(C1)) &&
                        (*C0 ^ *C1).isNegatedPowerOf2();

      // If either Op0/Op1 are both one use or X^Y will constant fold and one of
      // Op0/Op1 are one use, proceed. In those cases we are instruction neutral
      // but `icmp eq/ne A, 0` is easier to analyze than `icmp eq/ne A, B`.
      int UseCnt =
          int(Op0->hasOneUse()) + int(Op1->hasOneUse()) +
          (int(match(X, m_ImmConstant()) && match(Y, m_ImmConstant())));
      if (XorIsNegP2 || UseCnt >= 2) {
        // Build (X^Y) & Z
        Op1 = Builder.CreateXor(X, Y);
        Op1 = Builder.CreateAnd(Op1, Z);
        return new ICmpInst(Pred, Op1, Constant::getNullValue(Op1->getType()));
      }
    }
  }

  {
    // Similar to above, but specialized for constant because invert is needed:
    // (X | C) == (Y | C) --> (X ^ Y) & ~C == 0
    Value *X, *Y;
    Constant *C;
    if (match(Op0, m_OneUse(m_Or(m_Value(X), m_Constant(C)))) &&
        match(Op1, m_OneUse(m_Or(m_Value(Y), m_Specific(C))))) {
      Value *Xor = Builder.CreateXor(X, Y);
      Value *And = Builder.CreateAnd(Xor, ConstantExpr::getNot(C));
      return new ICmpInst(Pred, And, Constant::getNullValue(And->getType()));
    }
  }

  if (match(Op1, m_ZExt(m_Value(A))) &&
      (Op0->hasOneUse() || Op1->hasOneUse())) {
    // (B & (Pow2C-1)) == zext A --> A == trunc B
    // (B & (Pow2C-1)) != zext A --> A != trunc B
    const APInt *MaskC;
    if (match(Op0, m_And(m_Value(B), m_LowBitMask(MaskC))) &&
        MaskC->countr_one() == A->getType()->getScalarSizeInBits())
      return new ICmpInst(Pred, A, Builder.CreateTrunc(B, A->getType()));
  }

  // (A >> C) == (B >> C) --> (A^B) u< (1 << C)
  // For lshr and ashr pairs.
  const APInt *AP1, *AP2;
  if ((match(Op0, m_OneUse(m_LShr(m_Value(A), m_APIntAllowPoison(AP1)))) &&
       match(Op1, m_OneUse(m_LShr(m_Value(B), m_APIntAllowPoison(AP2))))) ||
      (match(Op0, m_OneUse(m_AShr(m_Value(A), m_APIntAllowPoison(AP1)))) &&
       match(Op1, m_OneUse(m_AShr(m_Value(B), m_APIntAllowPoison(AP2)))))) {
    if (AP1 != AP2)
      return nullptr;
    unsigned TypeBits = AP1->getBitWidth();
    unsigned ShAmt = AP1->getLimitedValue(TypeBits);
    if (ShAmt < TypeBits && ShAmt != 0) {
      ICmpInst::Predicate NewPred =
          Pred == ICmpInst::ICMP_NE ? ICmpInst::ICMP_UGE : ICmpInst::ICMP_ULT;
      Value *Xor = Builder.CreateXor(A, B, I.getName() + ".unshifted");
      APInt CmpVal = APInt::getOneBitSet(TypeBits, ShAmt);
      return new ICmpInst(NewPred, Xor, ConstantInt::get(A->getType(), CmpVal));
    }
  }

  // (A << C) == (B << C) --> ((A^B) & (~0U >> C)) == 0
  ConstantInt *Cst1;
  if (match(Op0, m_OneUse(m_Shl(m_Value(A), m_ConstantInt(Cst1)))) &&
      match(Op1, m_OneUse(m_Shl(m_Value(B), m_Specific(Cst1))))) {
    unsigned TypeBits = Cst1->getBitWidth();
    unsigned ShAmt = (unsigned)Cst1->getLimitedValue(TypeBits);
    if (ShAmt < TypeBits && ShAmt != 0) {
      Value *Xor = Builder.CreateXor(A, B, I.getName() + ".unshifted");
      APInt AndVal = APInt::getLowBitsSet(TypeBits, TypeBits - ShAmt);
      Value *And = Builder.CreateAnd(Xor, Builder.getInt(AndVal),
                                      I.getName() + ".mask");
      return new ICmpInst(Pred, And, Constant::getNullValue(Cst1->getType()));
    }
  }

  // Transform "icmp eq (trunc (lshr(X, cst1)), cst" to
  // "icmp (and X, mask), cst"
  uint64_t ShAmt = 0;
  if (Op0->hasOneUse() &&
      match(Op0, m_Trunc(m_OneUse(m_LShr(m_Value(A), m_ConstantInt(ShAmt))))) &&
      match(Op1, m_ConstantInt(Cst1)) &&
      // Only do this when A has multiple uses.  This is most important to do
      // when it exposes other optimizations.
      !A->hasOneUse()) {
    unsigned ASize = cast<IntegerType>(A->getType())->getPrimitiveSizeInBits();

    if (ShAmt < ASize) {
      APInt MaskV =
          APInt::getLowBitsSet(ASize, Op0->getType()->getPrimitiveSizeInBits());
      MaskV <<= ShAmt;

      APInt CmpV = Cst1->getValue().zext(ASize);
      CmpV <<= ShAmt;

      Value *Mask = Builder.CreateAnd(A, Builder.getInt(MaskV));
      return new ICmpInst(Pred, Mask, Builder.getInt(CmpV));
    }
  }

  if (Instruction *ICmp = foldICmpIntrinsicWithIntrinsic(I, Builder))
    return ICmp;

  // Match icmp eq (trunc (lshr A, BW), (ashr (trunc A), BW-1)), which checks the
  // top BW/2 + 1 bits are all the same. Create "A >=s INT_MIN && A <=s INT_MAX",
  // which we generate as "icmp ult (add A, 2^(BW-1)), 2^BW" to skip a few steps
  // of instcombine.
  unsigned BitWidth = Op0->getType()->getScalarSizeInBits();
  if (match(Op0, m_AShr(m_Trunc(m_Value(A)), m_SpecificInt(BitWidth - 1))) &&
      match(Op1, m_Trunc(m_LShr(m_Specific(A), m_SpecificInt(BitWidth)))) &&
      A->getType()->getScalarSizeInBits() == BitWidth * 2 &&
      (I.getOperand(0)->hasOneUse() || I.getOperand(1)->hasOneUse())) {
    APInt C = APInt::getOneBitSet(BitWidth * 2, BitWidth - 1);
    Value *Add = Builder.CreateAdd(A, ConstantInt::get(A->getType(), C));
    return new ICmpInst(Pred == ICmpInst::ICMP_EQ ? ICmpInst::ICMP_ULT
                                                  : ICmpInst::ICMP_UGE,
                        Add, ConstantInt::get(A->getType(), C.shl(1)));
  }

  // Canonicalize:
  // Assume B_Pow2 != 0
  // 1. A & B_Pow2 != B_Pow2 -> A & B_Pow2 == 0
  // 2. A & B_Pow2 == B_Pow2 -> A & B_Pow2 != 0
  if (match(Op0, m_c_And(m_Specific(Op1), m_Value())) &&
      isKnownToBeAPowerOfTwo(Op1, /* OrZero */ false, 0, &I))
    return new ICmpInst(CmpInst::getInversePredicate(Pred), Op0,
                        ConstantInt::getNullValue(Op0->getType()));

  if (match(Op1, m_c_And(m_Specific(Op0), m_Value())) &&
      isKnownToBeAPowerOfTwo(Op0, /* OrZero */ false, 0, &I))
    return new ICmpInst(CmpInst::getInversePredicate(Pred), Op1,
                        ConstantInt::getNullValue(Op1->getType()));

  // Canonicalize:
  // icmp eq/ne X, OneUse(rotate-right(X))
  //    -> icmp eq/ne X, rotate-left(X)
  // We generally try to convert rotate-right -> rotate-left, this just
  // canonicalizes another case.
  CmpInst::Predicate PredUnused = Pred;
  if (match(&I, m_c_ICmp(PredUnused, m_Value(A),
                         m_OneUse(m_Intrinsic<Intrinsic::fshr>(
                             m_Deferred(A), m_Deferred(A), m_Value(B))))))
    return new ICmpInst(
        Pred, A,
        Builder.CreateIntrinsic(Op0->getType(), Intrinsic::fshl, {A, A, B}));

  // Canonicalize:
  // icmp eq/ne OneUse(A ^ Cst), B --> icmp eq/ne (A ^ B), Cst
  Constant *Cst;
  if (match(&I, m_c_ICmp(PredUnused,
                         m_OneUse(m_Xor(m_Value(A), m_ImmConstant(Cst))),
                         m_CombineAnd(m_Value(B), m_Unless(m_ImmConstant())))))
    return new ICmpInst(Pred, Builder.CreateXor(A, B), Cst);

  {
    // (icmp eq/ne (and (add/sub/xor X, P2), P2), P2)
    auto m_Matcher =
        m_CombineOr(m_CombineOr(m_c_Add(m_Value(B), m_Deferred(A)),
                                m_c_Xor(m_Value(B), m_Deferred(A))),
                    m_Sub(m_Value(B), m_Deferred(A)));
    std::optional<bool> IsZero = std::nullopt;
    if (match(&I, m_c_ICmp(PredUnused, m_OneUse(m_c_And(m_Value(A), m_Matcher)),
                           m_Deferred(A))))
      IsZero = false;
    // (icmp eq/ne (and (add/sub/xor X, P2), P2), 0)
    else if (match(&I,
                   m_ICmp(PredUnused, m_OneUse(m_c_And(m_Value(A), m_Matcher)),
                          m_Zero())))
      IsZero = true;

    if (IsZero && isKnownToBeAPowerOfTwo(A, /* OrZero */ true, /*Depth*/ 0, &I))
      // (icmp eq/ne (and (add/sub/xor X, P2), P2), P2)
      //    -> (icmp eq/ne (and X, P2), 0)
      // (icmp eq/ne (and (add/sub/xor X, P2), P2), 0)
      //    -> (icmp eq/ne (and X, P2), P2)
      return new ICmpInst(Pred, Builder.CreateAnd(B, A),
                          *IsZero ? A
                                  : ConstantInt::getNullValue(A->getType()));
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldICmpWithTrunc(ICmpInst &ICmp) {
  ICmpInst::Predicate Pred = ICmp.getPredicate();
  Value *Op0 = ICmp.getOperand(0), *Op1 = ICmp.getOperand(1);

  // Try to canonicalize trunc + compare-to-constant into a mask + cmp.
  // The trunc masks high bits while the compare may effectively mask low bits.
  Value *X;
  const APInt *C;
  if (!match(Op0, m_OneUse(m_Trunc(m_Value(X)))) || !match(Op1, m_APInt(C)))
    return nullptr;

  // This matches patterns corresponding to tests of the signbit as well as:
  // (trunc X) u< C --> (X & -C) == 0 (are all masked-high-bits clear?)
  // (trunc X) u> C --> (X & ~C) != 0 (are any masked-high-bits set?)
  APInt Mask;
  if (decomposeBitTestICmp(Op0, Op1, Pred, X, Mask, true /* WithTrunc */)) {
    Value *And = Builder.CreateAnd(X, Mask);
    Constant *Zero = ConstantInt::getNullValue(X->getType());
    return new ICmpInst(Pred, And, Zero);
  }

  unsigned SrcBits = X->getType()->getScalarSizeInBits();
  if (Pred == ICmpInst::ICMP_ULT && C->isNegatedPowerOf2()) {
    // If C is a negative power-of-2 (high-bit mask):
    // (trunc X) u< C --> (X & C) != C (are any masked-high-bits clear?)
    Constant *MaskC = ConstantInt::get(X->getType(), C->zext(SrcBits));
    Value *And = Builder.CreateAnd(X, MaskC);
    return new ICmpInst(ICmpInst::ICMP_NE, And, MaskC);
  }

  if (Pred == ICmpInst::ICMP_UGT && (~*C).isPowerOf2()) {
    // If C is not-of-power-of-2 (one clear bit):
    // (trunc X) u> C --> (X & (C+1)) == C+1 (are all masked-high-bits set?)
    Constant *MaskC = ConstantInt::get(X->getType(), (*C + 1).zext(SrcBits));
    Value *And = Builder.CreateAnd(X, MaskC);
    return new ICmpInst(ICmpInst::ICMP_EQ, And, MaskC);
  }

  if (auto *II = dyn_cast<IntrinsicInst>(X)) {
    if (II->getIntrinsicID() == Intrinsic::cttz ||
        II->getIntrinsicID() == Intrinsic::ctlz) {
      unsigned MaxRet = SrcBits;
      // If the "is_zero_poison" argument is set, then we know at least
      // one bit is set in the input, so the result is always at least one
      // less than the full bitwidth of that input.
      if (match(II->getArgOperand(1), m_One()))
        MaxRet--;

      // Make sure the destination is wide enough to hold the largest output of
      // the intrinsic.
      if (llvm::Log2_32(MaxRet) + 1 <= Op0->getType()->getScalarSizeInBits())
        if (Instruction *I =
                foldICmpIntrinsicWithConstant(ICmp, II, C->zext(SrcBits)))
          return I;
    }
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldICmpWithZextOrSext(ICmpInst &ICmp) {
  assert(isa<CastInst>(ICmp.getOperand(0)) && "Expected cast for operand 0");
  auto *CastOp0 = cast<CastInst>(ICmp.getOperand(0));
  Value *X;
  if (!match(CastOp0, m_ZExtOrSExt(m_Value(X))))
    return nullptr;

  bool IsSignedExt = CastOp0->getOpcode() == Instruction::SExt;
  bool IsSignedCmp = ICmp.isSigned();

  // icmp Pred (ext X), (ext Y)
  Value *Y;
  if (match(ICmp.getOperand(1), m_ZExtOrSExt(m_Value(Y)))) {
    bool IsZext0 = isa<ZExtInst>(ICmp.getOperand(0));
    bool IsZext1 = isa<ZExtInst>(ICmp.getOperand(1));

    if (IsZext0 != IsZext1) {
        // If X and Y and both i1
        // (icmp eq/ne (zext X) (sext Y))
        //      eq -> (icmp eq (or X, Y), 0)
        //      ne -> (icmp ne (or X, Y), 0)
      if (ICmp.isEquality() && X->getType()->isIntOrIntVectorTy(1) &&
          Y->getType()->isIntOrIntVectorTy(1))
        return new ICmpInst(ICmp.getPredicate(), Builder.CreateOr(X, Y),
                            Constant::getNullValue(X->getType()));

      // If we have mismatched casts and zext has the nneg flag, we can
      //  treat the "zext nneg" as "sext". Otherwise, we cannot fold and quit.

      auto *NonNegInst0 = dyn_cast<PossiblyNonNegInst>(ICmp.getOperand(0));
      auto *NonNegInst1 = dyn_cast<PossiblyNonNegInst>(ICmp.getOperand(1));

      bool IsNonNeg0 = NonNegInst0 && NonNegInst0->hasNonNeg();
      bool IsNonNeg1 = NonNegInst1 && NonNegInst1->hasNonNeg();

      if ((IsZext0 && IsNonNeg0) || (IsZext1 && IsNonNeg1))
        IsSignedExt = true;
      else
        return nullptr;
    }

    // Not an extension from the same type?
    Type *XTy = X->getType(), *YTy = Y->getType();
    if (XTy != YTy) {
      // One of the casts must have one use because we are creating a new cast.
      if (!ICmp.getOperand(0)->hasOneUse() && !ICmp.getOperand(1)->hasOneUse())
        return nullptr;
      // Extend the narrower operand to the type of the wider operand.
      CastInst::CastOps CastOpcode =
          IsSignedExt ? Instruction::SExt : Instruction::ZExt;
      if (XTy->getScalarSizeInBits() < YTy->getScalarSizeInBits())
        X = Builder.CreateCast(CastOpcode, X, YTy);
      else if (YTy->getScalarSizeInBits() < XTy->getScalarSizeInBits())
        Y = Builder.CreateCast(CastOpcode, Y, XTy);
      else
        return nullptr;
    }

    // (zext X) == (zext Y) --> X == Y
    // (sext X) == (sext Y) --> X == Y
    if (ICmp.isEquality())
      return new ICmpInst(ICmp.getPredicate(), X, Y);

    // A signed comparison of sign extended values simplifies into a
    // signed comparison.
    if (IsSignedCmp && IsSignedExt)
      return new ICmpInst(ICmp.getPredicate(), X, Y);

    // The other three cases all fold into an unsigned comparison.
    return new ICmpInst(ICmp.getUnsignedPredicate(), X, Y);
  }

  // Below here, we are only folding a compare with constant.
  auto *C = dyn_cast<Constant>(ICmp.getOperand(1));
  if (!C)
    return nullptr;

  // If a lossless truncate is possible...
  Type *SrcTy = CastOp0->getSrcTy();
  Constant *Res = getLosslessTrunc(C, SrcTy, CastOp0->getOpcode());
  if (Res) {
    if (ICmp.isEquality())
      return new ICmpInst(ICmp.getPredicate(), X, Res);

    // A signed comparison of sign extended values simplifies into a
    // signed comparison.
    if (IsSignedExt && IsSignedCmp)
      return new ICmpInst(ICmp.getPredicate(), X, Res);

    // The other three cases all fold into an unsigned comparison.
    return new ICmpInst(ICmp.getUnsignedPredicate(), X, Res);
  }

  // The re-extended constant changed, partly changed (in the case of a vector),
  // or could not be determined to be equal (in the case of a constant
  // expression), so the constant cannot be represented in the shorter type.
  // All the cases that fold to true or false will have already been handled
  // by simplifyICmpInst, so only deal with the tricky case.
  if (IsSignedCmp || !IsSignedExt || !isa<ConstantInt>(C))
    return nullptr;

  // Is source op positive?
  // icmp ult (sext X), C --> icmp sgt X, -1
  if (ICmp.getPredicate() == ICmpInst::ICMP_ULT)
    return new ICmpInst(CmpInst::ICMP_SGT, X, Constant::getAllOnesValue(SrcTy));

  // Is source op negative?
  // icmp ugt (sext X), C --> icmp slt X, 0
  assert(ICmp.getPredicate() == ICmpInst::ICMP_UGT && "ICmp should be folded!");
  return new ICmpInst(CmpInst::ICMP_SLT, X, Constant::getNullValue(SrcTy));
}

/// Handle icmp (cast x), (cast or constant).
Instruction *InstCombinerImpl::foldICmpWithCastOp(ICmpInst &ICmp) {
  // If any operand of ICmp is a inttoptr roundtrip cast then remove it as
  // icmp compares only pointer's value.
  // icmp (inttoptr (ptrtoint p1)), p2 --> icmp p1, p2.
  Value *SimplifiedOp0 = simplifyIntToPtrRoundTripCast(ICmp.getOperand(0));
  Value *SimplifiedOp1 = simplifyIntToPtrRoundTripCast(ICmp.getOperand(1));
  if (SimplifiedOp0 || SimplifiedOp1)
    return new ICmpInst(ICmp.getPredicate(),
                        SimplifiedOp0 ? SimplifiedOp0 : ICmp.getOperand(0),
                        SimplifiedOp1 ? SimplifiedOp1 : ICmp.getOperand(1));

  auto *CastOp0 = dyn_cast<CastInst>(ICmp.getOperand(0));
  if (!CastOp0)
    return nullptr;
  if (!isa<Constant>(ICmp.getOperand(1)) && !isa<CastInst>(ICmp.getOperand(1)))
    return nullptr;

  Value *Op0Src = CastOp0->getOperand(0);
  Type *SrcTy = CastOp0->getSrcTy();
  Type *DestTy = CastOp0->getDestTy();

  // Turn icmp (ptrtoint x), (ptrtoint/c) into a compare of the input if the
  // integer type is the same size as the pointer type.
  auto CompatibleSizes = [&](Type *SrcTy, Type *DestTy) {
    if (isa<VectorType>(SrcTy)) {
      SrcTy = cast<VectorType>(SrcTy)->getElementType();
      DestTy = cast<VectorType>(DestTy)->getElementType();
    }
    return DL.getPointerTypeSizeInBits(SrcTy) == DestTy->getIntegerBitWidth();
  };
  if (CastOp0->getOpcode() == Instruction::PtrToInt &&
      CompatibleSizes(SrcTy, DestTy)) {
    Value *NewOp1 = nullptr;
    if (auto *PtrToIntOp1 = dyn_cast<PtrToIntOperator>(ICmp.getOperand(1))) {
      Value *PtrSrc = PtrToIntOp1->getOperand(0);
      if (PtrSrc->getType() == Op0Src->getType())
        NewOp1 = PtrToIntOp1->getOperand(0);
    } else if (auto *RHSC = dyn_cast<Constant>(ICmp.getOperand(1))) {
      NewOp1 = ConstantExpr::getIntToPtr(RHSC, SrcTy);
    }

    if (NewOp1)
      return new ICmpInst(ICmp.getPredicate(), Op0Src, NewOp1);
  }

  if (Instruction *R = foldICmpWithTrunc(ICmp))
    return R;

  return foldICmpWithZextOrSext(ICmp);
}

static bool isNeutralValue(Instruction::BinaryOps BinaryOp, Value *RHS, bool IsSigned) {
  switch (BinaryOp) {
    default:
      llvm_unreachable("Unsupported binary op");
    case Instruction::Add:
    case Instruction::Sub:
      return match(RHS, m_Zero());
    case Instruction::Mul:
      return !(RHS->getType()->isIntOrIntVectorTy(1) && IsSigned) &&
             match(RHS, m_One());
  }
}

OverflowResult
InstCombinerImpl::computeOverflow(Instruction::BinaryOps BinaryOp,
                                  bool IsSigned, Value *LHS, Value *RHS,
                                  Instruction *CxtI) const {
  switch (BinaryOp) {
    default:
      llvm_unreachable("Unsupported binary op");
    case Instruction::Add:
      if (IsSigned)
        return computeOverflowForSignedAdd(LHS, RHS, CxtI);
      else
        return computeOverflowForUnsignedAdd(LHS, RHS, CxtI);
    case Instruction::Sub:
      if (IsSigned)
        return computeOverflowForSignedSub(LHS, RHS, CxtI);
      else
        return computeOverflowForUnsignedSub(LHS, RHS, CxtI);
    case Instruction::Mul:
      if (IsSigned)
        return computeOverflowForSignedMul(LHS, RHS, CxtI);
      else
        return computeOverflowForUnsignedMul(LHS, RHS, CxtI);
  }
}

bool InstCombinerImpl::OptimizeOverflowCheck(Instruction::BinaryOps BinaryOp,
                                             bool IsSigned, Value *LHS,
                                             Value *RHS, Instruction &OrigI,
                                             Value *&Result,
                                             Constant *&Overflow) {
  if (OrigI.isCommutative() && isa<Constant>(LHS) && !isa<Constant>(RHS))
    std::swap(LHS, RHS);

  // If the overflow check was an add followed by a compare, the insertion point
  // may be pointing to the compare.  We want to insert the new instructions
  // before the add in case there are uses of the add between the add and the
  // compare.
  Builder.SetInsertPoint(&OrigI);

  Type *OverflowTy = Type::getInt1Ty(LHS->getContext());
  if (auto *LHSTy = dyn_cast<VectorType>(LHS->getType()))
    OverflowTy = VectorType::get(OverflowTy, LHSTy->getElementCount());

  if (isNeutralValue(BinaryOp, RHS, IsSigned)) {
    Result = LHS;
    Overflow = ConstantInt::getFalse(OverflowTy);
    return true;
  }

  switch (computeOverflow(BinaryOp, IsSigned, LHS, RHS, &OrigI)) {
    case OverflowResult::MayOverflow:
      return false;
    case OverflowResult::AlwaysOverflowsLow:
    case OverflowResult::AlwaysOverflowsHigh:
      Result = Builder.CreateBinOp(BinaryOp, LHS, RHS);
      Result->takeName(&OrigI);
      Overflow = ConstantInt::getTrue(OverflowTy);
      return true;
    case OverflowResult::NeverOverflows:
      Result = Builder.CreateBinOp(BinaryOp, LHS, RHS);
      Result->takeName(&OrigI);
      Overflow = ConstantInt::getFalse(OverflowTy);
      if (auto *Inst = dyn_cast<Instruction>(Result)) {
        if (IsSigned)
          Inst->setHasNoSignedWrap();
        else
          Inst->setHasNoUnsignedWrap();
      }
      return true;
  }

  llvm_unreachable("Unexpected overflow result");
}

/// Recognize and process idiom involving test for multiplication
/// overflow.
///
/// The caller has matched a pattern of the form:
///   I = cmp u (mul(zext A, zext B), V
/// The function checks if this is a test for overflow and if so replaces
/// multiplication with call to 'mul.with.overflow' intrinsic.
///
/// \param I Compare instruction.
/// \param MulVal Result of 'mult' instruction.  It is one of the arguments of
///               the compare instruction.  Must be of integer type.
/// \param OtherVal The other argument of compare instruction.
/// \returns Instruction which must replace the compare instruction, NULL if no
///          replacement required.
static Instruction *processUMulZExtIdiom(ICmpInst &I, Value *MulVal,
                                         const APInt *OtherVal,
                                         InstCombinerImpl &IC) {
  // Don't bother doing this transformation for pointers, don't do it for
  // vectors.
  if (!isa<IntegerType>(MulVal->getType()))
    return nullptr;

  auto *MulInstr = dyn_cast<Instruction>(MulVal);
  if (!MulInstr)
    return nullptr;
  assert(MulInstr->getOpcode() == Instruction::Mul);

  auto *LHS = cast<ZExtInst>(MulInstr->getOperand(0)),
       *RHS = cast<ZExtInst>(MulInstr->getOperand(1));
  assert(LHS->getOpcode() == Instruction::ZExt);
  assert(RHS->getOpcode() == Instruction::ZExt);
  Value *A = LHS->getOperand(0), *B = RHS->getOperand(0);

  // Calculate type and width of the result produced by mul.with.overflow.
  Type *TyA = A->getType(), *TyB = B->getType();
  unsigned WidthA = TyA->getPrimitiveSizeInBits(),
           WidthB = TyB->getPrimitiveSizeInBits();
  unsigned MulWidth;
  Type *MulType;
  if (WidthB > WidthA) {
    MulWidth = WidthB;
    MulType = TyB;
  } else {
    MulWidth = WidthA;
    MulType = TyA;
  }

  // In order to replace the original mul with a narrower mul.with.overflow,
  // all uses must ignore upper bits of the product.  The number of used low
  // bits must be not greater than the width of mul.with.overflow.
  if (MulVal->hasNUsesOrMore(2))
    for (User *U : MulVal->users()) {
      if (U == &I)
        continue;
      if (TruncInst *TI = dyn_cast<TruncInst>(U)) {
        // Check if truncation ignores bits above MulWidth.
        unsigned TruncWidth = TI->getType()->getPrimitiveSizeInBits();
        if (TruncWidth > MulWidth)
          return nullptr;
      } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(U)) {
        // Check if AND ignores bits above MulWidth.
        if (BO->getOpcode() != Instruction::And)
          return nullptr;
        if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->getOperand(1))) {
          const APInt &CVal = CI->getValue();
          if (CVal.getBitWidth() - CVal.countl_zero() > MulWidth)
            return nullptr;
        } else {
          // In this case we could have the operand of the binary operation
          // being defined in another block, and performing the replacement
          // could break the dominance relation.
          return nullptr;
        }
      } else {
        // Other uses prohibit this transformation.
        return nullptr;
      }
    }

  // Recognize patterns
  switch (I.getPredicate()) {
  case ICmpInst::ICMP_UGT: {
    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp ugt mulval, max
    APInt MaxVal = APInt::getMaxValue(MulWidth);
    MaxVal = MaxVal.zext(OtherVal->getBitWidth());
    if (MaxVal.eq(*OtherVal))
      break; // Recognized
    return nullptr;
  }

  case ICmpInst::ICMP_ULT: {
    // Recognize pattern:
    //   mulval = mul(zext A, zext B)
    //   cmp ule mulval, max + 1
    APInt MaxVal = APInt::getOneBitSet(OtherVal->getBitWidth(), MulWidth);
    if (MaxVal.eq(*OtherVal))
      break; // Recognized
    return nullptr;
  }

  default:
    return nullptr;
  }

  InstCombiner::BuilderTy &Builder = IC.Builder;
  Builder.SetInsertPoint(MulInstr);

  // Replace: mul(zext A, zext B) --> mul.with.overflow(A, B)
  Value *MulA = A, *MulB = B;
  if (WidthA < MulWidth)
    MulA = Builder.CreateZExt(A, MulType);
  if (WidthB < MulWidth)
    MulB = Builder.CreateZExt(B, MulType);
  Function *F = Intrinsic::getDeclaration(
      I.getModule(), Intrinsic::umul_with_overflow, MulType);
  CallInst *Call = Builder.CreateCall(F, {MulA, MulB}, "umul");
  IC.addToWorklist(MulInstr);

  // If there are uses of mul result other than the comparison, we know that
  // they are truncation or binary AND. Change them to use result of
  // mul.with.overflow and adjust properly mask/size.
  if (MulVal->hasNUsesOrMore(2)) {
    Value *Mul = Builder.CreateExtractValue(Call, 0, "umul.value");
    for (User *U : make_early_inc_range(MulVal->users())) {
      if (U == &I)
        continue;
      if (TruncInst *TI = dyn_cast<TruncInst>(U)) {
        if (TI->getType()->getPrimitiveSizeInBits() == MulWidth)
          IC.replaceInstUsesWith(*TI, Mul);
        else
          TI->setOperand(0, Mul);
      } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(U)) {
        assert(BO->getOpcode() == Instruction::And);
        // Replace (mul & mask) --> zext (mul.with.overflow & short_mask)
        ConstantInt *CI = cast<ConstantInt>(BO->getOperand(1));
        APInt ShortMask = CI->getValue().trunc(MulWidth);
        Value *ShortAnd = Builder.CreateAnd(Mul, ShortMask);
        Value *Zext = Builder.CreateZExt(ShortAnd, BO->getType());
        IC.replaceInstUsesWith(*BO, Zext);
      } else {
        llvm_unreachable("Unexpected Binary operation");
      }
      IC.addToWorklist(cast<Instruction>(U));
    }
  }

  // The original icmp gets replaced with the overflow value, maybe inverted
  // depending on predicate.
  if (I.getPredicate() == ICmpInst::ICMP_ULT) {
    Value *Res = Builder.CreateExtractValue(Call, 1);
    return BinaryOperator::CreateNot(Res);
  }

  return ExtractValueInst::Create(Call, 1);
}

/// When performing a comparison against a constant, it is possible that not all
/// the bits in the LHS are demanded. This helper method computes the mask that
/// IS demanded.
static APInt getDemandedBitsLHSMask(ICmpInst &I, unsigned BitWidth) {
  const APInt *RHS;
  if (!match(I.getOperand(1), m_APInt(RHS)))
    return APInt::getAllOnes(BitWidth);

  // If this is a normal comparison, it demands all bits. If it is a sign bit
  // comparison, it only demands the sign bit.
  bool UnusedBit;
  if (isSignBitCheck(I.getPredicate(), *RHS, UnusedBit))
    return APInt::getSignMask(BitWidth);

  switch (I.getPredicate()) {
  // For a UGT comparison, we don't care about any bits that
  // correspond to the trailing ones of the comparand.  The value of these
  // bits doesn't impact the outcome of the comparison, because any value
  // greater than the RHS must differ in a bit higher than these due to carry.
  case ICmpInst::ICMP_UGT:
    return APInt::getBitsSetFrom(BitWidth, RHS->countr_one());

  // Similarly, for a ULT comparison, we don't care about the trailing zeros.
  // Any value less than the RHS must differ in a higher bit because of carries.
  case ICmpInst::ICMP_ULT:
    return APInt::getBitsSetFrom(BitWidth, RHS->countr_zero());

  default:
    return APInt::getAllOnes(BitWidth);
  }
}

/// Check that one use is in the same block as the definition and all
/// other uses are in blocks dominated by a given block.
///
/// \param DI Definition
/// \param UI Use
/// \param DB Block that must dominate all uses of \p DI outside
///           the parent block
/// \return true when \p UI is the only use of \p DI in the parent block
/// and all other uses of \p DI are in blocks dominated by \p DB.
///
bool InstCombinerImpl::dominatesAllUses(const Instruction *DI,
                                        const Instruction *UI,
                                        const BasicBlock *DB) const {
  assert(DI && UI && "Instruction not defined\n");
  // Ignore incomplete definitions.
  if (!DI->getParent())
    return false;
  // DI and UI must be in the same block.
  if (DI->getParent() != UI->getParent())
    return false;
  // Protect from self-referencing blocks.
  if (DI->getParent() == DB)
    return false;
  for (const User *U : DI->users()) {
    auto *Usr = cast<Instruction>(U);
    if (Usr != UI && !DT.dominates(DB, Usr->getParent()))
      return false;
  }
  return true;
}

/// Return true when the instruction sequence within a block is select-cmp-br.
static bool isChainSelectCmpBranch(const SelectInst *SI) {
  const BasicBlock *BB = SI->getParent();
  if (!BB)
    return false;
  auto *BI = dyn_cast_or_null<BranchInst>(BB->getTerminator());
  if (!BI || BI->getNumSuccessors() != 2)
    return false;
  auto *IC = dyn_cast<ICmpInst>(BI->getCondition());
  if (!IC || (IC->getOperand(0) != SI && IC->getOperand(1) != SI))
    return false;
  return true;
}

/// True when a select result is replaced by one of its operands
/// in select-icmp sequence. This will eventually result in the elimination
/// of the select.
///
/// \param SI    Select instruction
/// \param Icmp  Compare instruction
/// \param SIOpd Operand that replaces the select
///
/// Notes:
/// - The replacement is global and requires dominator information
/// - The caller is responsible for the actual replacement
///
/// Example:
///
/// entry:
///  %4 = select i1 %3, %C* %0, %C* null
///  %5 = icmp eq %C* %4, null
///  br i1 %5, label %9, label %7
///  ...
///  ; <label>:7                                       ; preds = %entry
///  %8 = getelementptr inbounds %C* %4, i64 0, i32 0
///  ...
///
/// can be transformed to
///
///  %5 = icmp eq %C* %0, null
///  %6 = select i1 %3, i1 %5, i1 true
///  br i1 %6, label %9, label %7
///  ...
///  ; <label>:7                                       ; preds = %entry
///  %8 = getelementptr inbounds %C* %0, i64 0, i32 0  // replace by %0!
///
/// Similar when the first operand of the select is a constant or/and
/// the compare is for not equal rather than equal.
///
/// NOTE: The function is only called when the select and compare constants
/// are equal, the optimization can work only for EQ predicates. This is not a
/// major restriction since a NE compare should be 'normalized' to an equal
/// compare, which usually happens in the combiner and test case
/// select-cmp-br.ll checks for it.
bool InstCombinerImpl::replacedSelectWithOperand(SelectInst *SI,
                                                 const ICmpInst *Icmp,
                                                 const unsigned SIOpd) {
  assert((SIOpd == 1 || SIOpd == 2) && "Invalid select operand!");
  if (isChainSelectCmpBranch(SI) && Icmp->getPredicate() == ICmpInst::ICMP_EQ) {
    BasicBlock *Succ = SI->getParent()->getTerminator()->getSuccessor(1);
    // The check for the single predecessor is not the best that can be
    // done. But it protects efficiently against cases like when SI's
    // home block has two successors, Succ and Succ1, and Succ1 predecessor
    // of Succ. Then SI can't be replaced by SIOpd because the use that gets
    // replaced can be reached on either path. So the uniqueness check
    // guarantees that the path all uses of SI (outside SI's parent) are on
    // is disjoint from all other paths out of SI. But that information
    // is more expensive to compute, and the trade-off here is in favor
    // of compile-time. It should also be noticed that we check for a single
    // predecessor and not only uniqueness. This to handle the situation when
    // Succ and Succ1 points to the same basic block.
    if (Succ->getSinglePredecessor() && dominatesAllUses(SI, Icmp, Succ)) {
      NumSel++;
      SI->replaceUsesOutsideBlock(SI->getOperand(SIOpd), SI->getParent());
      return true;
    }
  }
  return false;
}

/// Try to fold the comparison based on range information we can get by checking
/// whether bits are known to be zero or one in the inputs.
Instruction *InstCombinerImpl::foldICmpUsingKnownBits(ICmpInst &I) {
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = Op0->getType();
  ICmpInst::Predicate Pred = I.getPredicate();

  // Get scalar or pointer size.
  unsigned BitWidth = Ty->isIntOrIntVectorTy()
                          ? Ty->getScalarSizeInBits()
                          : DL.getPointerTypeSizeInBits(Ty->getScalarType());

  if (!BitWidth)
    return nullptr;

  KnownBits Op0Known(BitWidth);
  KnownBits Op1Known(BitWidth);

  {
    // Don't use dominating conditions when folding icmp using known bits. This
    // may convert signed into unsigned predicates in ways that other passes
    // (especially IndVarSimplify) may not be able to reliably undo.
    SimplifyQuery Q = SQ.getWithoutDomCondCache().getWithInstruction(&I);
    if (SimplifyDemandedBits(&I, 0, getDemandedBitsLHSMask(I, BitWidth),
                             Op0Known, /*Depth=*/0, Q))
      return &I;

    if (SimplifyDemandedBits(&I, 1, APInt::getAllOnes(BitWidth), Op1Known,
                             /*Depth=*/0, Q))
      return &I;
  }

  // Given the known and unknown bits, compute a range that the LHS could be
  // in.  Compute the Min, Max and RHS values based on the known bits. For the
  // EQ and NE we use unsigned values.
  APInt Op0Min(BitWidth, 0), Op0Max(BitWidth, 0);
  APInt Op1Min(BitWidth, 0), Op1Max(BitWidth, 0);
  if (I.isSigned()) {
    Op0Min = Op0Known.getSignedMinValue();
    Op0Max = Op0Known.getSignedMaxValue();
    Op1Min = Op1Known.getSignedMinValue();
    Op1Max = Op1Known.getSignedMaxValue();
  } else {
    Op0Min = Op0Known.getMinValue();
    Op0Max = Op0Known.getMaxValue();
    Op1Min = Op1Known.getMinValue();
    Op1Max = Op1Known.getMaxValue();
  }

  // If Min and Max are known to be the same, then SimplifyDemandedBits figured
  // out that the LHS or RHS is a constant. Constant fold this now, so that
  // code below can assume that Min != Max.
  if (!isa<Constant>(Op0) && Op0Min == Op0Max)
    return new ICmpInst(Pred, ConstantExpr::getIntegerValue(Ty, Op0Min), Op1);
  if (!isa<Constant>(Op1) && Op1Min == Op1Max)
    return new ICmpInst(Pred, Op0, ConstantExpr::getIntegerValue(Ty, Op1Min));

  // Don't break up a clamp pattern -- (min(max X, Y), Z) -- by replacing a
  // min/max canonical compare with some other compare. That could lead to
  // conflict with select canonicalization and infinite looping.
  // FIXME: This constraint may go away if min/max intrinsics are canonical.
  auto isMinMaxCmp = [&](Instruction &Cmp) {
    if (!Cmp.hasOneUse())
      return false;
    Value *A, *B;
    SelectPatternFlavor SPF = matchSelectPattern(Cmp.user_back(), A, B).Flavor;
    if (!SelectPatternResult::isMinOrMax(SPF))
      return false;
    return match(Op0, m_MaxOrMin(m_Value(), m_Value())) ||
           match(Op1, m_MaxOrMin(m_Value(), m_Value()));
  };
  if (!isMinMaxCmp(I)) {
    switch (Pred) {
    default:
      break;
    case ICmpInst::ICMP_ULT: {
      if (Op1Min == Op0Max) // A <u B -> A != B if max(A) == min(B)
        return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);
      const APInt *CmpC;
      if (match(Op1, m_APInt(CmpC))) {
        // A <u C -> A == C-1 if min(A)+1 == C
        if (*CmpC == Op0Min + 1)
          return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                              ConstantInt::get(Op1->getType(), *CmpC - 1));
        // X <u C --> X == 0, if the number of zero bits in the bottom of X
        // exceeds the log2 of C.
        if (Op0Known.countMinTrailingZeros() >= CmpC->ceilLogBase2())
          return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                              Constant::getNullValue(Op1->getType()));
      }
      break;
    }
    case ICmpInst::ICMP_UGT: {
      if (Op1Max == Op0Min) // A >u B -> A != B if min(A) == max(B)
        return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);
      const APInt *CmpC;
      if (match(Op1, m_APInt(CmpC))) {
        // A >u C -> A == C+1 if max(a)-1 == C
        if (*CmpC == Op0Max - 1)
          return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                              ConstantInt::get(Op1->getType(), *CmpC + 1));
        // X >u C --> X != 0, if the number of zero bits in the bottom of X
        // exceeds the log2 of C.
        if (Op0Known.countMinTrailingZeros() >= CmpC->getActiveBits())
          return new ICmpInst(ICmpInst::ICMP_NE, Op0,
                              Constant::getNullValue(Op1->getType()));
      }
      break;
    }
    case ICmpInst::ICMP_SLT: {
      if (Op1Min == Op0Max) // A <s B -> A != B if max(A) == min(B)
        return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);
      const APInt *CmpC;
      if (match(Op1, m_APInt(CmpC))) {
        if (*CmpC == Op0Min + 1) // A <s C -> A == C-1 if min(A)+1 == C
          return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                              ConstantInt::get(Op1->getType(), *CmpC - 1));
      }
      break;
    }
    case ICmpInst::ICMP_SGT: {
      if (Op1Max == Op0Min) // A >s B -> A != B if min(A) == max(B)
        return new ICmpInst(ICmpInst::ICMP_NE, Op0, Op1);
      const APInt *CmpC;
      if (match(Op1, m_APInt(CmpC))) {
        if (*CmpC == Op0Max - 1) // A >s C -> A == C+1 if max(A)-1 == C
          return new ICmpInst(ICmpInst::ICMP_EQ, Op0,
                              ConstantInt::get(Op1->getType(), *CmpC + 1));
      }
      break;
    }
    }
  }

  // Based on the range information we know about the LHS, see if we can
  // simplify this comparison.  For example, (x&4) < 8 is always true.
  switch (Pred) {
  default:
    llvm_unreachable("Unknown icmp opcode!");
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_NE: {
    if (Op0Max.ult(Op1Min) || Op0Min.ugt(Op1Max))
      return replaceInstUsesWith(
          I, ConstantInt::getBool(I.getType(), Pred == CmpInst::ICMP_NE));

    // If all bits are known zero except for one, then we know at most one bit
    // is set. If the comparison is against zero, then this is a check to see if
    // *that* bit is set.
    APInt Op0KnownZeroInverted = ~Op0Known.Zero;
    if (Op1Known.isZero()) {
      // If the LHS is an AND with the same constant, look through it.
      Value *LHS = nullptr;
      const APInt *LHSC;
      if (!match(Op0, m_And(m_Value(LHS), m_APInt(LHSC))) ||
          *LHSC != Op0KnownZeroInverted)
        LHS = Op0;

      Value *X;
      const APInt *C1;
      if (match(LHS, m_Shl(m_Power2(C1), m_Value(X)))) {
        Type *XTy = X->getType();
        unsigned Log2C1 = C1->countr_zero();
        APInt C2 = Op0KnownZeroInverted;
        APInt C2Pow2 = (C2 & ~(*C1 - 1)) + *C1;
        if (C2Pow2.isPowerOf2()) {
          // iff (C1 is pow2) & ((C2 & ~(C1-1)) + C1) is pow2):
          // ((C1 << X) & C2) == 0 -> X >= (Log2(C2+C1) - Log2(C1))
          // ((C1 << X) & C2) != 0 -> X  < (Log2(C2+C1) - Log2(C1))
          unsigned Log2C2 = C2Pow2.countr_zero();
          auto *CmpC = ConstantInt::get(XTy, Log2C2 - Log2C1);
          auto NewPred =
              Pred == CmpInst::ICMP_EQ ? CmpInst::ICMP_UGE : CmpInst::ICMP_ULT;
          return new ICmpInst(NewPred, X, CmpC);
        }
      }
    }

    // Op0 eq C_Pow2 -> Op0 ne 0 if Op0 is known to be C_Pow2 or zero.
    if (Op1Known.isConstant() && Op1Known.getConstant().isPowerOf2() &&
        (Op0Known & Op1Known) == Op0Known)
      return new ICmpInst(CmpInst::getInversePredicate(Pred), Op0,
                          ConstantInt::getNullValue(Op1->getType()));
    break;
  }
  case ICmpInst::ICMP_ULT: {
    if (Op0Max.ult(Op1Min)) // A <u B -> true if max(A) < min(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.uge(Op1Max)) // A <u B -> false if min(A) >= max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    break;
  }
  case ICmpInst::ICMP_UGT: {
    if (Op0Min.ugt(Op1Max)) // A >u B -> true if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.ule(Op1Min)) // A >u B -> false if max(A) <= max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    break;
  }
  case ICmpInst::ICMP_SLT: {
    if (Op0Max.slt(Op1Min)) // A <s B -> true if max(A) < min(C)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.sge(Op1Max)) // A <s B -> false if min(A) >= max(C)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    break;
  }
  case ICmpInst::ICMP_SGT: {
    if (Op0Min.sgt(Op1Max)) // A >s B -> true if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.sle(Op1Min)) // A >s B -> false if max(A) <= min(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    break;
  }
  case ICmpInst::ICMP_SGE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_SGE with ConstantInt not folded!");
    if (Op0Min.sge(Op1Max)) // A >=s B -> true if min(A) >= max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.slt(Op1Min)) // A >=s B -> false if max(A) < min(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Min == Op0Max) // A >=s B -> A == B if max(A) == min(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  case ICmpInst::ICMP_SLE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_SLE with ConstantInt not folded!");
    if (Op0Max.sle(Op1Min)) // A <=s B -> true if max(A) <= min(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.sgt(Op1Max)) // A <=s B -> false if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Max == Op0Min) // A <=s B -> A == B if min(A) == max(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  case ICmpInst::ICMP_UGE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_UGE with ConstantInt not folded!");
    if (Op0Min.uge(Op1Max)) // A >=u B -> true if min(A) >= max(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Max.ult(Op1Min)) // A >=u B -> false if max(A) < min(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Min == Op0Max) // A >=u B -> A == B if max(A) == min(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  case ICmpInst::ICMP_ULE:
    assert(!isa<ConstantInt>(Op1) && "ICMP_ULE with ConstantInt not folded!");
    if (Op0Max.ule(Op1Min)) // A <=u B -> true if max(A) <= min(B)
      return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
    if (Op0Min.ugt(Op1Max)) // A <=u B -> false if min(A) > max(B)
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    if (Op1Max == Op0Min) // A <=u B -> A == B if min(A) == max(B)
      return new ICmpInst(ICmpInst::ICMP_EQ, Op0, Op1);
    break;
  }

  // Turn a signed comparison into an unsigned one if both operands are known to
  // have the same sign.
  if (I.isSigned() &&
      ((Op0Known.Zero.isNegative() && Op1Known.Zero.isNegative()) ||
       (Op0Known.One.isNegative() && Op1Known.One.isNegative())))
    return new ICmpInst(I.getUnsignedPredicate(), Op0, Op1);

  return nullptr;
}

/// If one operand of an icmp is effectively a bool (value range of {0,1}),
/// then try to reduce patterns based on that limit.
Instruction *InstCombinerImpl::foldICmpUsingBoolRange(ICmpInst &I) {
  Value *X, *Y;
  ICmpInst::Predicate Pred;

  // X must be 0 and bool must be true for "ULT":
  // X <u (zext i1 Y) --> (X == 0) & Y
  if (match(&I, m_c_ICmp(Pred, m_Value(X), m_OneUse(m_ZExt(m_Value(Y))))) &&
      Y->getType()->isIntOrIntVectorTy(1) && Pred == ICmpInst::ICMP_ULT)
    return BinaryOperator::CreateAnd(Builder.CreateIsNull(X), Y);

  // X must be 0 or bool must be true for "ULE":
  // X <=u (sext i1 Y) --> (X == 0) | Y
  if (match(&I, m_c_ICmp(Pred, m_Value(X), m_OneUse(m_SExt(m_Value(Y))))) &&
      Y->getType()->isIntOrIntVectorTy(1) && Pred == ICmpInst::ICMP_ULE)
    return BinaryOperator::CreateOr(Builder.CreateIsNull(X), Y);

  // icmp eq/ne X, (zext/sext (icmp eq/ne X, C))
  ICmpInst::Predicate Pred1, Pred2;
  const APInt *C;
  Instruction *ExtI;
  if (match(&I, m_c_ICmp(Pred1, m_Value(X),
                         m_CombineAnd(m_Instruction(ExtI),
                                      m_ZExtOrSExt(m_ICmp(Pred2, m_Deferred(X),
                                                          m_APInt(C)))))) &&
      ICmpInst::isEquality(Pred1) && ICmpInst::isEquality(Pred2)) {
    bool IsSExt = ExtI->getOpcode() == Instruction::SExt;
    bool HasOneUse = ExtI->hasOneUse() && ExtI->getOperand(0)->hasOneUse();
    auto CreateRangeCheck = [&] {
      Value *CmpV1 =
          Builder.CreateICmp(Pred1, X, Constant::getNullValue(X->getType()));
      Value *CmpV2 = Builder.CreateICmp(
          Pred1, X, ConstantInt::getSigned(X->getType(), IsSExt ? -1 : 1));
      return BinaryOperator::Create(
          Pred1 == ICmpInst::ICMP_EQ ? Instruction::Or : Instruction::And,
          CmpV1, CmpV2);
    };
    if (C->isZero()) {
      if (Pred2 == ICmpInst::ICMP_EQ) {
        // icmp eq X, (zext/sext (icmp eq X, 0)) --> false
        // icmp ne X, (zext/sext (icmp eq X, 0)) --> true
        return replaceInstUsesWith(
            I, ConstantInt::getBool(I.getType(), Pred1 == ICmpInst::ICMP_NE));
      } else if (!IsSExt || HasOneUse) {
        // icmp eq X, (zext (icmp ne X, 0)) --> X == 0 || X == 1
        // icmp ne X, (zext (icmp ne X, 0)) --> X != 0 && X != 1
        // icmp eq X, (sext (icmp ne X, 0)) --> X == 0 || X == -1
        // icmp ne X, (sext (icmp ne X, 0)) --> X != 0 && X == -1
        return CreateRangeCheck();
      }
    } else if (IsSExt ? C->isAllOnes() : C->isOne()) {
      if (Pred2 == ICmpInst::ICMP_NE) {
        // icmp eq X, (zext (icmp ne X, 1)) --> false
        // icmp ne X, (zext (icmp ne X, 1)) --> true
        // icmp eq X, (sext (icmp ne X, -1)) --> false
        // icmp ne X, (sext (icmp ne X, -1)) --> true
        return replaceInstUsesWith(
            I, ConstantInt::getBool(I.getType(), Pred1 == ICmpInst::ICMP_NE));
      } else if (!IsSExt || HasOneUse) {
        // icmp eq X, (zext (icmp eq X, 1)) --> X == 0 || X == 1
        // icmp ne X, (zext (icmp eq X, 1)) --> X != 0 && X != 1
        // icmp eq X, (sext (icmp eq X, -1)) --> X == 0 || X == -1
        // icmp ne X, (sext (icmp eq X, -1)) --> X != 0 && X == -1
        return CreateRangeCheck();
      }
    } else {
      // when C != 0 && C != 1:
      //   icmp eq X, (zext (icmp eq X, C)) --> icmp eq X, 0
      //   icmp eq X, (zext (icmp ne X, C)) --> icmp eq X, 1
      //   icmp ne X, (zext (icmp eq X, C)) --> icmp ne X, 0
      //   icmp ne X, (zext (icmp ne X, C)) --> icmp ne X, 1
      // when C != 0 && C != -1:
      //   icmp eq X, (sext (icmp eq X, C)) --> icmp eq X, 0
      //   icmp eq X, (sext (icmp ne X, C)) --> icmp eq X, -1
      //   icmp ne X, (sext (icmp eq X, C)) --> icmp ne X, 0
      //   icmp ne X, (sext (icmp ne X, C)) --> icmp ne X, -1
      return ICmpInst::Create(
          Instruction::ICmp, Pred1, X,
          ConstantInt::getSigned(X->getType(), Pred2 == ICmpInst::ICMP_NE
                                                   ? (IsSExt ? -1 : 1)
                                                   : 0));
    }
  }

  return nullptr;
}

std::optional<std::pair<CmpInst::Predicate, Constant *>>
InstCombiner::getFlippedStrictnessPredicateAndConstant(CmpInst::Predicate Pred,
                                                       Constant *C) {
  assert(ICmpInst::isRelational(Pred) && ICmpInst::isIntPredicate(Pred) &&
         "Only for relational integer predicates.");

  Type *Type = C->getType();
  bool IsSigned = ICmpInst::isSigned(Pred);

  CmpInst::Predicate UnsignedPred = ICmpInst::getUnsignedPredicate(Pred);
  bool WillIncrement =
      UnsignedPred == ICmpInst::ICMP_ULE || UnsignedPred == ICmpInst::ICMP_UGT;

  // Check if the constant operand can be safely incremented/decremented
  // without overflowing/underflowing.
  auto ConstantIsOk = [WillIncrement, IsSigned](ConstantInt *C) {
    return WillIncrement ? !C->isMaxValue(IsSigned) : !C->isMinValue(IsSigned);
  };

  Constant *SafeReplacementConstant = nullptr;
  if (auto *CI = dyn_cast<ConstantInt>(C)) {
    // Bail out if the constant can't be safely incremented/decremented.
    if (!ConstantIsOk(CI))
      return std::nullopt;
  } else if (auto *FVTy = dyn_cast<FixedVectorType>(Type)) {
    unsigned NumElts = FVTy->getNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *Elt = C->getAggregateElement(i);
      if (!Elt)
        return std::nullopt;

      if (isa<UndefValue>(Elt))
        continue;

      // Bail out if we can't determine if this constant is min/max or if we
      // know that this constant is min/max.
      auto *CI = dyn_cast<ConstantInt>(Elt);
      if (!CI || !ConstantIsOk(CI))
        return std::nullopt;

      if (!SafeReplacementConstant)
        SafeReplacementConstant = CI;
    }
  } else if (isa<VectorType>(C->getType())) {
    // Handle scalable splat
    Value *SplatC = C->getSplatValue();
    auto *CI = dyn_cast_or_null<ConstantInt>(SplatC);
    // Bail out if the constant can't be safely incremented/decremented.
    if (!CI || !ConstantIsOk(CI))
      return std::nullopt;
  } else {
    // ConstantExpr?
    return std::nullopt;
  }

  // It may not be safe to change a compare predicate in the presence of
  // undefined elements, so replace those elements with the first safe constant
  // that we found.
  // TODO: in case of poison, it is safe; let's replace undefs only.
  if (C->containsUndefOrPoisonElement()) {
    assert(SafeReplacementConstant && "Replacement constant not set");
    C = Constant::replaceUndefsWith(C, SafeReplacementConstant);
  }

  CmpInst::Predicate NewPred = CmpInst::getFlippedStrictnessPredicate(Pred);

  // Increment or decrement the constant.
  Constant *OneOrNegOne = ConstantInt::get(Type, WillIncrement ? 1 : -1, true);
  Constant *NewC = ConstantExpr::getAdd(C, OneOrNegOne);

  return std::make_pair(NewPred, NewC);
}

/// If we have an icmp le or icmp ge instruction with a constant operand, turn
/// it into the appropriate icmp lt or icmp gt instruction. This transform
/// allows them to be folded in visitICmpInst.
static ICmpInst *canonicalizeCmpWithConstant(ICmpInst &I) {
  ICmpInst::Predicate Pred = I.getPredicate();
  if (ICmpInst::isEquality(Pred) || !ICmpInst::isIntPredicate(Pred) ||
      InstCombiner::isCanonicalPredicate(Pred))
    return nullptr;

  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  auto *Op1C = dyn_cast<Constant>(Op1);
  if (!Op1C)
    return nullptr;

  auto FlippedStrictness =
      InstCombiner::getFlippedStrictnessPredicateAndConstant(Pred, Op1C);
  if (!FlippedStrictness)
    return nullptr;

  return new ICmpInst(FlippedStrictness->first, Op0, FlippedStrictness->second);
}

/// If we have a comparison with a non-canonical predicate, if we can update
/// all the users, invert the predicate and adjust all the users.
CmpInst *InstCombinerImpl::canonicalizeICmpPredicate(CmpInst &I) {
  // Is the predicate already canonical?
  CmpInst::Predicate Pred = I.getPredicate();
  if (InstCombiner::isCanonicalPredicate(Pred))
    return nullptr;

  // Can all users be adjusted to predicate inversion?
  if (!InstCombiner::canFreelyInvertAllUsersOf(&I, /*IgnoredUser=*/nullptr))
    return nullptr;

  // Ok, we can canonicalize comparison!
  // Let's first invert the comparison's predicate.
  I.setPredicate(CmpInst::getInversePredicate(Pred));
  I.setName(I.getName() + ".not");

  // And, adapt users.
  freelyInvertAllUsersOf(&I);

  return &I;
}

/// Integer compare with boolean values can always be turned into bitwise ops.
static Instruction *canonicalizeICmpBool(ICmpInst &I,
                                         InstCombiner::BuilderTy &Builder) {
  Value *A = I.getOperand(0), *B = I.getOperand(1);
  assert(A->getType()->isIntOrIntVectorTy(1) && "Bools only");

  // A boolean compared to true/false can be simplified to Op0/true/false in
  // 14 out of the 20 (10 predicates * 2 constants) possible combinations.
  // Cases not handled by InstSimplify are always 'not' of Op0.
  if (match(B, m_Zero())) {
    switch (I.getPredicate()) {
      case CmpInst::ICMP_EQ:  // A ==   0 -> !A
      case CmpInst::ICMP_ULE: // A <=u  0 -> !A
      case CmpInst::ICMP_SGE: // A >=s  0 -> !A
        return BinaryOperator::CreateNot(A);
      default:
        llvm_unreachable("ICmp i1 X, C not simplified as expected.");
    }
  } else if (match(B, m_One())) {
    switch (I.getPredicate()) {
      case CmpInst::ICMP_NE:  // A !=  1 -> !A
      case CmpInst::ICMP_ULT: // A <u  1 -> !A
      case CmpInst::ICMP_SGT: // A >s -1 -> !A
        return BinaryOperator::CreateNot(A);
      default:
        llvm_unreachable("ICmp i1 X, C not simplified as expected.");
    }
  }

  switch (I.getPredicate()) {
  default:
    llvm_unreachable("Invalid icmp instruction!");
  case ICmpInst::ICMP_EQ:
    // icmp eq i1 A, B -> ~(A ^ B)
    return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  case ICmpInst::ICMP_NE:
    // icmp ne i1 A, B -> A ^ B
    return BinaryOperator::CreateXor(A, B);

  case ICmpInst::ICMP_UGT:
    // icmp ugt -> icmp ult
    std::swap(A, B);
    [[fallthrough]];
  case ICmpInst::ICMP_ULT:
    // icmp ult i1 A, B -> ~A & B
    return BinaryOperator::CreateAnd(Builder.CreateNot(A), B);

  case ICmpInst::ICMP_SGT:
    // icmp sgt -> icmp slt
    std::swap(A, B);
    [[fallthrough]];
  case ICmpInst::ICMP_SLT:
    // icmp slt i1 A, B -> A & ~B
    return BinaryOperator::CreateAnd(Builder.CreateNot(B), A);

  case ICmpInst::ICMP_UGE:
    // icmp uge -> icmp ule
    std::swap(A, B);
    [[fallthrough]];
  case ICmpInst::ICMP_ULE:
    // icmp ule i1 A, B -> ~A | B
    return BinaryOperator::CreateOr(Builder.CreateNot(A), B);

  case ICmpInst::ICMP_SGE:
    // icmp sge -> icmp sle
    std::swap(A, B);
    [[fallthrough]];
  case ICmpInst::ICMP_SLE:
    // icmp sle i1 A, B -> A | ~B
    return BinaryOperator::CreateOr(Builder.CreateNot(B), A);
  }
}

// Transform pattern like:
//   (1 << Y) u<= X  or  ~(-1 << Y) u<  X  or  ((1 << Y)+(-1)) u<  X
//   (1 << Y) u>  X  or  ~(-1 << Y) u>= X  or  ((1 << Y)+(-1)) u>= X
// Into:
//   (X l>> Y) != 0
//   (X l>> Y) == 0
static Instruction *foldICmpWithHighBitMask(ICmpInst &Cmp,
                                            InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate Pred, NewPred;
  Value *X, *Y;
  if (match(&Cmp,
            m_c_ICmp(Pred, m_OneUse(m_Shl(m_One(), m_Value(Y))), m_Value(X)))) {
    switch (Pred) {
    case ICmpInst::ICMP_ULE:
      NewPred = ICmpInst::ICMP_NE;
      break;
    case ICmpInst::ICMP_UGT:
      NewPred = ICmpInst::ICMP_EQ;
      break;
    default:
      return nullptr;
    }
  } else if (match(&Cmp, m_c_ICmp(Pred,
                                  m_OneUse(m_CombineOr(
                                      m_Not(m_Shl(m_AllOnes(), m_Value(Y))),
                                      m_Add(m_Shl(m_One(), m_Value(Y)),
                                            m_AllOnes()))),
                                  m_Value(X)))) {
    // The variant with 'add' is not canonical, (the variant with 'not' is)
    // we only get it because it has extra uses, and can't be canonicalized,

    switch (Pred) {
    case ICmpInst::ICMP_ULT:
      NewPred = ICmpInst::ICMP_NE;
      break;
    case ICmpInst::ICMP_UGE:
      NewPred = ICmpInst::ICMP_EQ;
      break;
    default:
      return nullptr;
    }
  } else
    return nullptr;

  Value *NewX = Builder.CreateLShr(X, Y, X->getName() + ".highbits");
  Constant *Zero = Constant::getNullValue(NewX->getType());
  return CmpInst::Create(Instruction::ICmp, NewPred, NewX, Zero);
}

static Instruction *foldVectorCmp(CmpInst &Cmp,
                                  InstCombiner::BuilderTy &Builder) {
  const CmpInst::Predicate Pred = Cmp.getPredicate();
  Value *LHS = Cmp.getOperand(0), *RHS = Cmp.getOperand(1);
  Value *V1, *V2;

  auto createCmpReverse = [&](CmpInst::Predicate Pred, Value *X, Value *Y) {
    Value *V = Builder.CreateCmp(Pred, X, Y, Cmp.getName());
    if (auto *I = dyn_cast<Instruction>(V))
      I->copyIRFlags(&Cmp);
    Module *M = Cmp.getModule();
    Function *F =
        Intrinsic::getDeclaration(M, Intrinsic::vector_reverse, V->getType());
    return CallInst::Create(F, V);
  };

  if (match(LHS, m_VecReverse(m_Value(V1)))) {
    // cmp Pred, rev(V1), rev(V2) --> rev(cmp Pred, V1, V2)
    if (match(RHS, m_VecReverse(m_Value(V2))) &&
        (LHS->hasOneUse() || RHS->hasOneUse()))
      return createCmpReverse(Pred, V1, V2);

    // cmp Pred, rev(V1), RHSSplat --> rev(cmp Pred, V1, RHSSplat)
    if (LHS->hasOneUse() && isSplatValue(RHS))
      return createCmpReverse(Pred, V1, RHS);
  }
  // cmp Pred, LHSSplat, rev(V2) --> rev(cmp Pred, LHSSplat, V2)
  else if (isSplatValue(LHS) && match(RHS, m_OneUse(m_VecReverse(m_Value(V2)))))
    return createCmpReverse(Pred, LHS, V2);

  ArrayRef<int> M;
  if (!match(LHS, m_Shuffle(m_Value(V1), m_Undef(), m_Mask(M))))
    return nullptr;

  // If both arguments of the cmp are shuffles that use the same mask and
  // shuffle within a single vector, move the shuffle after the cmp:
  // cmp (shuffle V1, M), (shuffle V2, M) --> shuffle (cmp V1, V2), M
  Type *V1Ty = V1->getType();
  if (match(RHS, m_Shuffle(m_Value(V2), m_Undef(), m_SpecificMask(M))) &&
      V1Ty == V2->getType() && (LHS->hasOneUse() || RHS->hasOneUse())) {
    Value *NewCmp = Builder.CreateCmp(Pred, V1, V2);
    return new ShuffleVectorInst(NewCmp, M);
  }

  // Try to canonicalize compare with splatted operand and splat constant.
  // TODO: We could generalize this for more than splats. See/use the code in
  //       InstCombiner::foldVectorBinop().
  Constant *C;
  if (!LHS->hasOneUse() || !match(RHS, m_Constant(C)))
    return nullptr;

  // Length-changing splats are ok, so adjust the constants as needed:
  // cmp (shuffle V1, M), C --> shuffle (cmp V1, C'), M
  Constant *ScalarC = C->getSplatValue(/* AllowPoison */ true);
  int MaskSplatIndex;
  if (ScalarC && match(M, m_SplatOrPoisonMask(MaskSplatIndex))) {
    // We allow poison in matching, but this transform removes it for safety.
    // Demanded elements analysis should be able to recover some/all of that.
    C = ConstantVector::getSplat(cast<VectorType>(V1Ty)->getElementCount(),
                                 ScalarC);
    SmallVector<int, 8> NewM(M.size(), MaskSplatIndex);
    Value *NewCmp = Builder.CreateCmp(Pred, V1, C);
    return new ShuffleVectorInst(NewCmp, NewM);
  }

  return nullptr;
}

// extract(uadd.with.overflow(A, B), 0) ult A
//  -> extract(uadd.with.overflow(A, B), 1)
static Instruction *foldICmpOfUAddOv(ICmpInst &I) {
  CmpInst::Predicate Pred = I.getPredicate();
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  Value *UAddOv;
  Value *A, *B;
  auto UAddOvResultPat = m_ExtractValue<0>(
      m_Intrinsic<Intrinsic::uadd_with_overflow>(m_Value(A), m_Value(B)));
  if (match(Op0, UAddOvResultPat) &&
      ((Pred == ICmpInst::ICMP_ULT && (Op1 == A || Op1 == B)) ||
       (Pred == ICmpInst::ICMP_EQ && match(Op1, m_ZeroInt()) &&
        (match(A, m_One()) || match(B, m_One()))) ||
       (Pred == ICmpInst::ICMP_NE && match(Op1, m_AllOnes()) &&
        (match(A, m_AllOnes()) || match(B, m_AllOnes())))))
    // extract(uadd.with.overflow(A, B), 0) < A
    // extract(uadd.with.overflow(A, 1), 0) == 0
    // extract(uadd.with.overflow(A, -1), 0) != -1
    UAddOv = cast<ExtractValueInst>(Op0)->getAggregateOperand();
  else if (match(Op1, UAddOvResultPat) &&
           Pred == ICmpInst::ICMP_UGT && (Op0 == A || Op0 == B))
    // A > extract(uadd.with.overflow(A, B), 0)
    UAddOv = cast<ExtractValueInst>(Op1)->getAggregateOperand();
  else
    return nullptr;

  return ExtractValueInst::Create(UAddOv, 1);
}

static Instruction *foldICmpInvariantGroup(ICmpInst &I) {
  if (!I.getOperand(0)->getType()->isPointerTy() ||
      NullPointerIsDefined(
          I.getParent()->getParent(),
          I.getOperand(0)->getType()->getPointerAddressSpace())) {
    return nullptr;
  }
  Instruction *Op;
  if (match(I.getOperand(0), m_Instruction(Op)) &&
      match(I.getOperand(1), m_Zero()) &&
      Op->isLaunderOrStripInvariantGroup()) {
    return ICmpInst::Create(Instruction::ICmp, I.getPredicate(),
                            Op->getOperand(0), I.getOperand(1));
  }
  return nullptr;
}

/// This function folds patterns produced by lowering of reduce idioms, such as
/// llvm.vector.reduce.and which are lowered into instruction chains. This code
/// attempts to generate fewer number of scalar comparisons instead of vector
/// comparisons when possible.
static Instruction *foldReductionIdiom(ICmpInst &I,
                                       InstCombiner::BuilderTy &Builder,
                                       const DataLayout &DL) {
  if (I.getType()->isVectorTy())
    return nullptr;
  ICmpInst::Predicate OuterPred, InnerPred;
  Value *LHS, *RHS;

  // Match lowering of @llvm.vector.reduce.and. Turn
  ///   %vec_ne = icmp ne <8 x i8> %lhs, %rhs
  ///   %scalar_ne = bitcast <8 x i1> %vec_ne to i8
  ///   %res = icmp <pred> i8 %scalar_ne, 0
  ///
  /// into
  ///
  ///   %lhs.scalar = bitcast <8 x i8> %lhs to i64
  ///   %rhs.scalar = bitcast <8 x i8> %rhs to i64
  ///   %res = icmp <pred> i64 %lhs.scalar, %rhs.scalar
  ///
  /// for <pred> in {ne, eq}.
  if (!match(&I, m_ICmp(OuterPred,
                        m_OneUse(m_BitCast(m_OneUse(
                            m_ICmp(InnerPred, m_Value(LHS), m_Value(RHS))))),
                        m_Zero())))
    return nullptr;
  auto *LHSTy = dyn_cast<FixedVectorType>(LHS->getType());
  if (!LHSTy || !LHSTy->getElementType()->isIntegerTy())
    return nullptr;
  unsigned NumBits =
      LHSTy->getNumElements() * LHSTy->getElementType()->getIntegerBitWidth();
  // TODO: Relax this to "not wider than max legal integer type"?
  if (!DL.isLegalInteger(NumBits))
    return nullptr;

  if (ICmpInst::isEquality(OuterPred) && InnerPred == ICmpInst::ICMP_NE) {
    auto *ScalarTy = Builder.getIntNTy(NumBits);
    LHS = Builder.CreateBitCast(LHS, ScalarTy, LHS->getName() + ".scalar");
    RHS = Builder.CreateBitCast(RHS, ScalarTy, RHS->getName() + ".scalar");
    return ICmpInst::Create(Instruction::ICmp, OuterPred, LHS, RHS,
                            I.getName());
  }

  return nullptr;
}

// This helper will be called with icmp operands in both orders.
Instruction *InstCombinerImpl::foldICmpCommutative(ICmpInst::Predicate Pred,
                                                   Value *Op0, Value *Op1,
                                                   ICmpInst &CxtI) {
  // Try to optimize 'icmp GEP, P' or 'icmp P, GEP'.
  if (auto *GEP = dyn_cast<GEPOperator>(Op0))
    if (Instruction *NI = foldGEPICmp(GEP, Op1, Pred, CxtI))
      return NI;

  if (auto *SI = dyn_cast<SelectInst>(Op0))
    if (Instruction *NI = foldSelectICmp(Pred, SI, Op1, CxtI))
      return NI;

  if (auto *MinMax = dyn_cast<MinMaxIntrinsic>(Op0))
    if (Instruction *Res = foldICmpWithMinMax(CxtI, MinMax, Op1, Pred))
      return Res;

  {
    Value *X;
    const APInt *C;
    // icmp X+Cst, X
    if (match(Op0, m_Add(m_Value(X), m_APInt(C))) && Op1 == X)
      return foldICmpAddOpConst(X, *C, Pred);
  }

  // abs(X) >=  X --> true
  // abs(X) u<= X --> true
  // abs(X) <   X --> false
  // abs(X) u>  X --> false
  // abs(X) u>= X --> IsIntMinPosion ? `X > -1`: `X u<= INTMIN`
  // abs(X) <=  X --> IsIntMinPosion ? `X > -1`: `X u<= INTMIN`
  // abs(X) ==  X --> IsIntMinPosion ? `X > -1`: `X u<= INTMIN`
  // abs(X) u<  X --> IsIntMinPosion ? `X < 0` : `X >   INTMIN`
  // abs(X) >   X --> IsIntMinPosion ? `X < 0` : `X >   INTMIN`
  // abs(X) !=  X --> IsIntMinPosion ? `X < 0` : `X >   INTMIN`
  {
    Value *X;
    Constant *C;
    if (match(Op0, m_Intrinsic<Intrinsic::abs>(m_Value(X), m_Constant(C))) &&
        match(Op1, m_Specific(X))) {
      Value *NullValue = Constant::getNullValue(X->getType());
      Value *AllOnesValue = Constant::getAllOnesValue(X->getType());
      const APInt SMin =
          APInt::getSignedMinValue(X->getType()->getScalarSizeInBits());
      bool IsIntMinPosion = C->isAllOnesValue();
      switch (Pred) {
      case CmpInst::ICMP_ULE:
      case CmpInst::ICMP_SGE:
        return replaceInstUsesWith(CxtI, ConstantInt::getTrue(CxtI.getType()));
      case CmpInst::ICMP_UGT:
      case CmpInst::ICMP_SLT:
        return replaceInstUsesWith(CxtI, ConstantInt::getFalse(CxtI.getType()));
      case CmpInst::ICMP_UGE:
      case CmpInst::ICMP_SLE:
      case CmpInst::ICMP_EQ: {
        return replaceInstUsesWith(
            CxtI, IsIntMinPosion
                      ? Builder.CreateICmpSGT(X, AllOnesValue)
                      : Builder.CreateICmpULT(
                            X, ConstantInt::get(X->getType(), SMin + 1)));
      }
      case CmpInst::ICMP_ULT:
      case CmpInst::ICMP_SGT:
      case CmpInst::ICMP_NE: {
        return replaceInstUsesWith(
            CxtI, IsIntMinPosion
                      ? Builder.CreateICmpSLT(X, NullValue)
                      : Builder.CreateICmpUGT(
                            X, ConstantInt::get(X->getType(), SMin)));
      }
      default:
        llvm_unreachable("Invalid predicate!");
      }
    }
  }

  const SimplifyQuery Q = SQ.getWithInstruction(&CxtI);
  if (Value *V = foldICmpWithLowBitMaskedVal(Pred, Op0, Op1, Q, *this))
    return replaceInstUsesWith(CxtI, V);

  // Folding (X / Y) pred X => X swap(pred) 0 for constant Y other than 0 or 1
  auto CheckUGT1 = [](const APInt &Divisor) { return Divisor.ugt(1); };
  {
    if (match(Op0, m_UDiv(m_Specific(Op1), m_CheckedInt(CheckUGT1)))) {
      return new ICmpInst(ICmpInst::getSwappedPredicate(Pred), Op1,
                          Constant::getNullValue(Op1->getType()));
    }

    if (!ICmpInst::isUnsigned(Pred) &&
        match(Op0, m_SDiv(m_Specific(Op1), m_CheckedInt(CheckUGT1)))) {
      return new ICmpInst(ICmpInst::getSwappedPredicate(Pred), Op1,
                          Constant::getNullValue(Op1->getType()));
    }
  }

  // Another case of this fold is (X >> Y) pred X => X swap(pred) 0 if Y != 0
  auto CheckNE0 = [](const APInt &Shift) { return !Shift.isZero(); };
  {
    if (match(Op0, m_LShr(m_Specific(Op1), m_CheckedInt(CheckNE0)))) {
      return new ICmpInst(ICmpInst::getSwappedPredicate(Pred), Op1,
                          Constant::getNullValue(Op1->getType()));
    }

    if ((Pred == CmpInst::ICMP_SLT || Pred == CmpInst::ICMP_SGE) &&
        match(Op0, m_AShr(m_Specific(Op1), m_CheckedInt(CheckNE0)))) {
      return new ICmpInst(ICmpInst::getSwappedPredicate(Pred), Op1,
                          Constant::getNullValue(Op1->getType()));
    }
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitICmpInst(ICmpInst &I) {
  bool Changed = false;
  const SimplifyQuery Q = SQ.getWithInstruction(&I);
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  unsigned Op0Cplxity = getComplexity(Op0);
  unsigned Op1Cplxity = getComplexity(Op1);

  /// Orders the operands of the compare so that they are listed from most
  /// complex to least complex.  This puts constants before unary operators,
  /// before binary operators.
  if (Op0Cplxity < Op1Cplxity) {
    I.swapOperands();
    std::swap(Op0, Op1);
    Changed = true;
  }

  if (Value *V = simplifyICmpInst(I.getPredicate(), Op0, Op1, Q))
    return replaceInstUsesWith(I, V);

  // Comparing -val or val with non-zero is the same as just comparing val
  // ie, abs(val) != 0 -> val != 0
  if (I.getPredicate() == ICmpInst::ICMP_NE && match(Op1, m_Zero())) {
    Value *Cond, *SelectTrue, *SelectFalse;
    if (match(Op0, m_Select(m_Value(Cond), m_Value(SelectTrue),
                            m_Value(SelectFalse)))) {
      if (Value *V = dyn_castNegVal(SelectTrue)) {
        if (V == SelectFalse)
          return CmpInst::Create(Instruction::ICmp, I.getPredicate(), V, Op1);
      }
      else if (Value *V = dyn_castNegVal(SelectFalse)) {
        if (V == SelectTrue)
          return CmpInst::Create(Instruction::ICmp, I.getPredicate(), V, Op1);
      }
    }
  }

  if (Op0->getType()->isIntOrIntVectorTy(1))
    if (Instruction *Res = canonicalizeICmpBool(I, Builder))
      return Res;

  if (Instruction *Res = canonicalizeCmpWithConstant(I))
    return Res;

  if (Instruction *Res = canonicalizeICmpPredicate(I))
    return Res;

  if (Instruction *Res = foldICmpWithConstant(I))
    return Res;

  if (Instruction *Res = foldICmpWithDominatingICmp(I))
    return Res;

  if (Instruction *Res = foldICmpUsingBoolRange(I))
    return Res;

  if (Instruction *Res = foldICmpUsingKnownBits(I))
    return Res;

  if (Instruction *Res = foldICmpTruncWithTruncOrExt(I, Q))
    return Res;

  // Test if the ICmpInst instruction is used exclusively by a select as
  // part of a minimum or maximum operation. If so, refrain from doing
  // any other folding. This helps out other analyses which understand
  // non-obfuscated minimum and maximum idioms, such as ScalarEvolution
  // and CodeGen. And in this case, at least one of the comparison
  // operands has at least one user besides the compare (the select),
  // which would often largely negate the benefit of folding anyway.
  //
  // Do the same for the other patterns recognized by matchSelectPattern.
  if (I.hasOneUse())
    if (SelectInst *SI = dyn_cast<SelectInst>(I.user_back())) {
      Value *A, *B;
      SelectPatternResult SPR = matchSelectPattern(SI, A, B);
      if (SPR.Flavor != SPF_UNKNOWN)
        return nullptr;
    }

  // Do this after checking for min/max to prevent infinite looping.
  if (Instruction *Res = foldICmpWithZero(I))
    return Res;

  // FIXME: We only do this after checking for min/max to prevent infinite
  // looping caused by a reverse canonicalization of these patterns for min/max.
  // FIXME: The organization of folds is a mess. These would naturally go into
  // canonicalizeCmpWithConstant(), but we can't move all of the above folds
  // down here after the min/max restriction.
  ICmpInst::Predicate Pred = I.getPredicate();
  const APInt *C;
  if (match(Op1, m_APInt(C))) {
    // For i32: x >u 2147483647 -> x <s 0  -> true if sign bit set
    if (Pred == ICmpInst::ICMP_UGT && C->isMaxSignedValue()) {
      Constant *Zero = Constant::getNullValue(Op0->getType());
      return new ICmpInst(ICmpInst::ICMP_SLT, Op0, Zero);
    }

    // For i32: x <u 2147483648 -> x >s -1  -> true if sign bit clear
    if (Pred == ICmpInst::ICMP_ULT && C->isMinSignedValue()) {
      Constant *AllOnes = Constant::getAllOnesValue(Op0->getType());
      return new ICmpInst(ICmpInst::ICMP_SGT, Op0, AllOnes);
    }
  }

  // The folds in here may rely on wrapping flags and special constants, so
  // they can break up min/max idioms in some cases but not seemingly similar
  // patterns.
  // FIXME: It may be possible to enhance select folding to make this
  //        unnecessary. It may also be moot if we canonicalize to min/max
  //        intrinsics.
  if (Instruction *Res = foldICmpBinOp(I, Q))
    return Res;

  if (Instruction *Res = foldICmpInstWithConstant(I))
    return Res;

  // Try to match comparison as a sign bit test. Intentionally do this after
  // foldICmpInstWithConstant() to potentially let other folds to happen first.
  if (Instruction *New = foldSignBitTest(I))
    return New;

  if (Instruction *Res = foldICmpInstWithConstantNotInt(I))
    return Res;

  if (Instruction *Res = foldICmpCommutative(I.getPredicate(), Op0, Op1, I))
    return Res;
  if (Instruction *Res =
          foldICmpCommutative(I.getSwappedPredicate(), Op1, Op0, I))
    return Res;

  if (I.isCommutative()) {
    if (auto Pair = matchSymmetricPair(I.getOperand(0), I.getOperand(1))) {
      replaceOperand(I, 0, Pair->first);
      replaceOperand(I, 1, Pair->second);
      return &I;
    }
  }

  // In case of a comparison with two select instructions having the same
  // condition, check whether one of the resulting branches can be simplified.
  // If so, just compare the other branch and select the appropriate result.
  // For example:
  //   %tmp1 = select i1 %cmp, i32 %y, i32 %x
  //   %tmp2 = select i1 %cmp, i32 %z, i32 %x
  //   %cmp2 = icmp slt i32 %tmp2, %tmp1
  // The icmp will result false for the false value of selects and the result
  // will depend upon the comparison of true values of selects if %cmp is
  // true. Thus, transform this into:
  //   %cmp = icmp slt i32 %y, %z
  //   %sel = select i1 %cond, i1 %cmp, i1 false
  // This handles similar cases to transform.
  {
    Value *Cond, *A, *B, *C, *D;
    if (match(Op0, m_Select(m_Value(Cond), m_Value(A), m_Value(B))) &&
        match(Op1, m_Select(m_Specific(Cond), m_Value(C), m_Value(D))) &&
        (Op0->hasOneUse() || Op1->hasOneUse())) {
      // Check whether comparison of TrueValues can be simplified
      if (Value *Res = simplifyICmpInst(Pred, A, C, SQ)) {
        Value *NewICMP = Builder.CreateICmp(Pred, B, D);
        return SelectInst::Create(Cond, Res, NewICMP);
      }
      // Check whether comparison of FalseValues can be simplified
      if (Value *Res = simplifyICmpInst(Pred, B, D, SQ)) {
        Value *NewICMP = Builder.CreateICmp(Pred, A, C);
        return SelectInst::Create(Cond, NewICMP, Res);
      }
    }
  }

  // Try to optimize equality comparisons against alloca-based pointers.
  if (Op0->getType()->isPointerTy() && I.isEquality()) {
    assert(Op1->getType()->isPointerTy() && "Comparing pointer with non-pointer?");
    if (auto *Alloca = dyn_cast<AllocaInst>(getUnderlyingObject(Op0)))
      if (foldAllocaCmp(Alloca))
        return nullptr;
    if (auto *Alloca = dyn_cast<AllocaInst>(getUnderlyingObject(Op1)))
      if (foldAllocaCmp(Alloca))
        return nullptr;
  }

  if (Instruction *Res = foldICmpBitCast(I))
    return Res;

  // TODO: Hoist this above the min/max bailout.
  if (Instruction *R = foldICmpWithCastOp(I))
    return R;

  {
    Value *X, *Y;
    // Transform (X & ~Y) == 0 --> (X & Y) != 0
    // and       (X & ~Y) != 0 --> (X & Y) == 0
    // if A is a power of 2.
    if (match(Op0, m_And(m_Value(X), m_Not(m_Value(Y)))) &&
        match(Op1, m_Zero()) && isKnownToBeAPowerOfTwo(X, false, 0, &I) &&
        I.isEquality())
      return new ICmpInst(I.getInversePredicate(), Builder.CreateAnd(X, Y),
                          Op1);

    // Op0 pred Op1 -> ~Op1 pred ~Op0, if this allows us to drop an instruction.
    if (Op0->getType()->isIntOrIntVectorTy()) {
      bool ConsumesOp0, ConsumesOp1;
      if (isFreeToInvert(Op0, Op0->hasOneUse(), ConsumesOp0) &&
          isFreeToInvert(Op1, Op1->hasOneUse(), ConsumesOp1) &&
          (ConsumesOp0 || ConsumesOp1)) {
        Value *InvOp0 = getFreelyInverted(Op0, Op0->hasOneUse(), &Builder);
        Value *InvOp1 = getFreelyInverted(Op1, Op1->hasOneUse(), &Builder);
        assert(InvOp0 && InvOp1 &&
               "Mismatch between isFreeToInvert and getFreelyInverted");
        return new ICmpInst(I.getSwappedPredicate(), InvOp0, InvOp1);
      }
    }

    Instruction *AddI = nullptr;
    if (match(&I, m_UAddWithOverflow(m_Value(X), m_Value(Y),
                                     m_Instruction(AddI))) &&
        isa<IntegerType>(X->getType())) {
      Value *Result;
      Constant *Overflow;
      // m_UAddWithOverflow can match patterns that do not include  an explicit
      // "add" instruction, so check the opcode of the matched op.
      if (AddI->getOpcode() == Instruction::Add &&
          OptimizeOverflowCheck(Instruction::Add, /*Signed*/ false, X, Y, *AddI,
                                Result, Overflow)) {
        replaceInstUsesWith(*AddI, Result);
        eraseInstFromFunction(*AddI);
        return replaceInstUsesWith(I, Overflow);
      }
    }

    // (zext X) * (zext Y)  --> llvm.umul.with.overflow.
    if (match(Op0, m_NUWMul(m_ZExt(m_Value(X)), m_ZExt(m_Value(Y)))) &&
        match(Op1, m_APInt(C))) {
      if (Instruction *R = processUMulZExtIdiom(I, Op0, C, *this))
        return R;
    }

    // Signbit test folds
    // Fold (X u>> BitWidth - 1 Pred ZExt(i1))  -->  X s< 0 Pred i1
    // Fold (X s>> BitWidth - 1 Pred SExt(i1))  -->  X s< 0 Pred i1
    Instruction *ExtI;
    if ((I.isUnsigned() || I.isEquality()) &&
        match(Op1,
              m_CombineAnd(m_Instruction(ExtI), m_ZExtOrSExt(m_Value(Y)))) &&
        Y->getType()->getScalarSizeInBits() == 1 &&
        (Op0->hasOneUse() || Op1->hasOneUse())) {
      unsigned OpWidth = Op0->getType()->getScalarSizeInBits();
      Instruction *ShiftI;
      if (match(Op0, m_CombineAnd(m_Instruction(ShiftI),
                                  m_Shr(m_Value(X), m_SpecificIntAllowPoison(
                                                        OpWidth - 1))))) {
        unsigned ExtOpc = ExtI->getOpcode();
        unsigned ShiftOpc = ShiftI->getOpcode();
        if ((ExtOpc == Instruction::ZExt && ShiftOpc == Instruction::LShr) ||
            (ExtOpc == Instruction::SExt && ShiftOpc == Instruction::AShr)) {
          Value *SLTZero =
              Builder.CreateICmpSLT(X, Constant::getNullValue(X->getType()));
          Value *Cmp = Builder.CreateICmp(Pred, SLTZero, Y, I.getName());
          return replaceInstUsesWith(I, Cmp);
        }
      }
    }
  }

  if (Instruction *Res = foldICmpEquality(I))
    return Res;

  if (Instruction *Res = foldICmpPow2Test(I, Builder))
    return Res;

  if (Instruction *Res = foldICmpOfUAddOv(I))
    return Res;

  // The 'cmpxchg' instruction returns an aggregate containing the old value and
  // an i1 which indicates whether or not we successfully did the swap.
  //
  // Replace comparisons between the old value and the expected value with the
  // indicator that 'cmpxchg' returns.
  //
  // N.B.  This transform is only valid when the 'cmpxchg' is not permitted to
  // spuriously fail.  In those cases, the old value may equal the expected
  // value but it is possible for the swap to not occur.
  if (I.getPredicate() == ICmpInst::ICMP_EQ)
    if (auto *EVI = dyn_cast<ExtractValueInst>(Op0))
      if (auto *ACXI = dyn_cast<AtomicCmpXchgInst>(EVI->getAggregateOperand()))
        if (EVI->getIndices()[0] == 0 && ACXI->getCompareOperand() == Op1 &&
            !ACXI->isWeak())
          return ExtractValueInst::Create(ACXI, 1);

  if (Instruction *Res = foldICmpWithHighBitMask(I, Builder))
    return Res;

  if (I.getType()->isVectorTy())
    if (Instruction *Res = foldVectorCmp(I, Builder))
      return Res;

  if (Instruction *Res = foldICmpInvariantGroup(I))
    return Res;

  if (Instruction *Res = foldReductionIdiom(I, Builder, DL))
    return Res;

  return Changed ? &I : nullptr;
}

/// Fold fcmp ([us]itofp x, cst) if possible.
Instruction *InstCombinerImpl::foldFCmpIntToFPConst(FCmpInst &I,
                                                    Instruction *LHSI,
                                                    Constant *RHSC) {
  const APFloat *RHS;
  if (!match(RHSC, m_APFloat(RHS)))
    return nullptr;

  // Get the width of the mantissa.  We don't want to hack on conversions that
  // might lose information from the integer, e.g. "i64 -> float"
  int MantissaWidth = LHSI->getType()->getFPMantissaWidth();
  if (MantissaWidth == -1) return nullptr;  // Unknown.

  Type *IntTy = LHSI->getOperand(0)->getType();
  unsigned IntWidth = IntTy->getScalarSizeInBits();
  bool LHSUnsigned = isa<UIToFPInst>(LHSI);

  if (I.isEquality()) {
    FCmpInst::Predicate P = I.getPredicate();
    bool IsExact = false;
    APSInt RHSCvt(IntWidth, LHSUnsigned);
    RHS->convertToInteger(RHSCvt, APFloat::rmNearestTiesToEven, &IsExact);

    // If the floating point constant isn't an integer value, we know if we will
    // ever compare equal / not equal to it.
    if (!IsExact) {
      // TODO: Can never be -0.0 and other non-representable values
      APFloat RHSRoundInt(*RHS);
      RHSRoundInt.roundToIntegral(APFloat::rmNearestTiesToEven);
      if (*RHS != RHSRoundInt) {
        if (P == FCmpInst::FCMP_OEQ || P == FCmpInst::FCMP_UEQ)
          return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));

        assert(P == FCmpInst::FCMP_ONE || P == FCmpInst::FCMP_UNE);
        return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
      }
    }

    // TODO: If the constant is exactly representable, is it always OK to do
    // equality compares as integer?
  }

  // Check to see that the input is converted from an integer type that is small
  // enough that preserves all bits.  TODO: check here for "known" sign bits.
  // This would allow us to handle (fptosi (x >>s 62) to float) if x is i64 f.e.

  // Following test does NOT adjust IntWidth downwards for signed inputs,
  // because the most negative value still requires all the mantissa bits
  // to distinguish it from one less than that value.
  if ((int)IntWidth > MantissaWidth) {
    // Conversion would lose accuracy. Check if loss can impact comparison.
    int Exp = ilogb(*RHS);
    if (Exp == APFloat::IEK_Inf) {
      int MaxExponent = ilogb(APFloat::getLargest(RHS->getSemantics()));
      if (MaxExponent < (int)IntWidth - !LHSUnsigned)
        // Conversion could create infinity.
        return nullptr;
    } else {
      // Note that if RHS is zero or NaN, then Exp is negative
      // and first condition is trivially false.
      if (MantissaWidth <= Exp && Exp <= (int)IntWidth - !LHSUnsigned)
        // Conversion could affect comparison.
        return nullptr;
    }
  }

  // Otherwise, we can potentially simplify the comparison.  We know that it
  // will always come through as an integer value and we know the constant is
  // not a NAN (it would have been previously simplified).
  assert(!RHS->isNaN() && "NaN comparison not already folded!");

  ICmpInst::Predicate Pred;
  switch (I.getPredicate()) {
  default: llvm_unreachable("Unexpected predicate!");
  case FCmpInst::FCMP_UEQ:
  case FCmpInst::FCMP_OEQ:
    Pred = ICmpInst::ICMP_EQ;
    break;
  case FCmpInst::FCMP_UGT:
  case FCmpInst::FCMP_OGT:
    Pred = LHSUnsigned ? ICmpInst::ICMP_UGT : ICmpInst::ICMP_SGT;
    break;
  case FCmpInst::FCMP_UGE:
  case FCmpInst::FCMP_OGE:
    Pred = LHSUnsigned ? ICmpInst::ICMP_UGE : ICmpInst::ICMP_SGE;
    break;
  case FCmpInst::FCMP_ULT:
  case FCmpInst::FCMP_OLT:
    Pred = LHSUnsigned ? ICmpInst::ICMP_ULT : ICmpInst::ICMP_SLT;
    break;
  case FCmpInst::FCMP_ULE:
  case FCmpInst::FCMP_OLE:
    Pred = LHSUnsigned ? ICmpInst::ICMP_ULE : ICmpInst::ICMP_SLE;
    break;
  case FCmpInst::FCMP_UNE:
  case FCmpInst::FCMP_ONE:
    Pred = ICmpInst::ICMP_NE;
    break;
  case FCmpInst::FCMP_ORD:
    return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
  case FCmpInst::FCMP_UNO:
    return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
  }

  // Now we know that the APFloat is a normal number, zero or inf.

  // See if the FP constant is too large for the integer.  For example,
  // comparing an i8 to 300.0.
  if (!LHSUnsigned) {
    // If the RHS value is > SignedMax, fold the comparison.  This handles +INF
    // and large values.
    APFloat SMax(RHS->getSemantics());
    SMax.convertFromAPInt(APInt::getSignedMaxValue(IntWidth), true,
                          APFloat::rmNearestTiesToEven);
    if (SMax < *RHS) { // smax < 13123.0
      if (Pred == ICmpInst::ICMP_NE  || Pred == ICmpInst::ICMP_SLT ||
          Pred == ICmpInst::ICMP_SLE)
        return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    }
  } else {
    // If the RHS value is > UnsignedMax, fold the comparison. This handles
    // +INF and large values.
    APFloat UMax(RHS->getSemantics());
    UMax.convertFromAPInt(APInt::getMaxValue(IntWidth), false,
                          APFloat::rmNearestTiesToEven);
    if (UMax < *RHS) { // umax < 13123.0
      if (Pred == ICmpInst::ICMP_NE  || Pred == ICmpInst::ICMP_ULT ||
          Pred == ICmpInst::ICMP_ULE)
        return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    }
  }

  if (!LHSUnsigned) {
    // See if the RHS value is < SignedMin.
    APFloat SMin(RHS->getSemantics());
    SMin.convertFromAPInt(APInt::getSignedMinValue(IntWidth), true,
                          APFloat::rmNearestTiesToEven);
    if (SMin > *RHS) { // smin > 12312.0
      if (Pred == ICmpInst::ICMP_NE || Pred == ICmpInst::ICMP_SGT ||
          Pred == ICmpInst::ICMP_SGE)
        return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    }
  } else {
    // See if the RHS value is < UnsignedMin.
    APFloat UMin(RHS->getSemantics());
    UMin.convertFromAPInt(APInt::getMinValue(IntWidth), false,
                          APFloat::rmNearestTiesToEven);
    if (UMin > *RHS) { // umin > 12312.0
      if (Pred == ICmpInst::ICMP_NE || Pred == ICmpInst::ICMP_UGT ||
          Pred == ICmpInst::ICMP_UGE)
        return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
      return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
    }
  }

  // Okay, now we know that the FP constant fits in the range [SMIN, SMAX] or
  // [0, UMAX], but it may still be fractional. Check whether this is the case
  // using the IsExact flag.
  // Don't do this for zero, because -0.0 is not fractional.
  APSInt RHSInt(IntWidth, LHSUnsigned);
  bool IsExact;
  RHS->convertToInteger(RHSInt, APFloat::rmTowardZero, &IsExact);
  if (!RHS->isZero()) {
    if (!IsExact) {
      // If we had a comparison against a fractional value, we have to adjust
      // the compare predicate and sometimes the value.  RHSC is rounded towards
      // zero at this point.
      switch (Pred) {
      default: llvm_unreachable("Unexpected integer comparison!");
      case ICmpInst::ICMP_NE:  // (float)int != 4.4   --> true
        return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
      case ICmpInst::ICMP_EQ:  // (float)int == 4.4   --> false
        return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
      case ICmpInst::ICMP_ULE:
        // (float)int <= 4.4   --> int <= 4
        // (float)int <= -4.4  --> false
        if (RHS->isNegative())
          return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
        break;
      case ICmpInst::ICMP_SLE:
        // (float)int <= 4.4   --> int <= 4
        // (float)int <= -4.4  --> int < -4
        if (RHS->isNegative())
          Pred = ICmpInst::ICMP_SLT;
        break;
      case ICmpInst::ICMP_ULT:
        // (float)int < -4.4   --> false
        // (float)int < 4.4    --> int <= 4
        if (RHS->isNegative())
          return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
        Pred = ICmpInst::ICMP_ULE;
        break;
      case ICmpInst::ICMP_SLT:
        // (float)int < -4.4   --> int < -4
        // (float)int < 4.4    --> int <= 4
        if (!RHS->isNegative())
          Pred = ICmpInst::ICMP_SLE;
        break;
      case ICmpInst::ICMP_UGT:
        // (float)int > 4.4    --> int > 4
        // (float)int > -4.4   --> true
        if (RHS->isNegative())
          return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
        break;
      case ICmpInst::ICMP_SGT:
        // (float)int > 4.4    --> int > 4
        // (float)int > -4.4   --> int >= -4
        if (RHS->isNegative())
          Pred = ICmpInst::ICMP_SGE;
        break;
      case ICmpInst::ICMP_UGE:
        // (float)int >= -4.4   --> true
        // (float)int >= 4.4    --> int > 4
        if (RHS->isNegative())
          return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
        Pred = ICmpInst::ICMP_UGT;
        break;
      case ICmpInst::ICMP_SGE:
        // (float)int >= -4.4   --> int >= -4
        // (float)int >= 4.4    --> int > 4
        if (!RHS->isNegative())
          Pred = ICmpInst::ICMP_SGT;
        break;
      }
    }
  }

  // Lower this FP comparison into an appropriate integer version of the
  // comparison.
  return new ICmpInst(Pred, LHSI->getOperand(0),
                      ConstantInt::get(LHSI->getOperand(0)->getType(), RHSInt));
}

/// Fold (C / X) < 0.0 --> X < 0.0 if possible. Swap predicate if necessary.
static Instruction *foldFCmpReciprocalAndZero(FCmpInst &I, Instruction *LHSI,
                                              Constant *RHSC) {
  // When C is not 0.0 and infinities are not allowed:
  // (C / X) < 0.0 is a sign-bit test of X
  // (C / X) < 0.0 --> X < 0.0 (if C is positive)
  // (C / X) < 0.0 --> X > 0.0 (if C is negative, swap the predicate)
  //
  // Proof:
  // Multiply (C / X) < 0.0 by X * X / C.
  // - X is non zero, if it is the flag 'ninf' is violated.
  // - C defines the sign of X * X * C. Thus it also defines whether to swap
  //   the predicate. C is also non zero by definition.
  //
  // Thus X * X / C is non zero and the transformation is valid. [qed]

  FCmpInst::Predicate Pred = I.getPredicate();

  // Check that predicates are valid.
  if ((Pred != FCmpInst::FCMP_OGT) && (Pred != FCmpInst::FCMP_OLT) &&
      (Pred != FCmpInst::FCMP_OGE) && (Pred != FCmpInst::FCMP_OLE))
    return nullptr;

  // Check that RHS operand is zero.
  if (!match(RHSC, m_AnyZeroFP()))
    return nullptr;

  // Check fastmath flags ('ninf').
  if (!LHSI->hasNoInfs() || !I.hasNoInfs())
    return nullptr;

  // Check the properties of the dividend. It must not be zero to avoid a
  // division by zero (see Proof).
  const APFloat *C;
  if (!match(LHSI->getOperand(0), m_APFloat(C)))
    return nullptr;

  if (C->isZero())
    return nullptr;

  // Get swapped predicate if necessary.
  if (C->isNegative())
    Pred = I.getSwappedPredicate();

  return new FCmpInst(Pred, LHSI->getOperand(1), RHSC, "", &I);
}

/// Optimize fabs(X) compared with zero.
static Instruction *foldFabsWithFcmpZero(FCmpInst &I, InstCombinerImpl &IC) {
  Value *X;
  if (!match(I.getOperand(0), m_FAbs(m_Value(X))))
    return nullptr;

  const APFloat *C;
  if (!match(I.getOperand(1), m_APFloat(C)))
    return nullptr;

  if (!C->isPosZero()) {
    if (!C->isSmallestNormalized())
      return nullptr;

    const Function *F = I.getFunction();
    DenormalMode Mode = F->getDenormalMode(C->getSemantics());
    if (Mode.Input == DenormalMode::PreserveSign ||
        Mode.Input == DenormalMode::PositiveZero) {

      auto replaceFCmp = [](FCmpInst *I, FCmpInst::Predicate P, Value *X) {
        Constant *Zero = ConstantFP::getZero(X->getType());
        return new FCmpInst(P, X, Zero, "", I);
      };

      switch (I.getPredicate()) {
      case FCmpInst::FCMP_OLT:
        // fcmp olt fabs(x), smallest_normalized_number -> fcmp oeq x, 0.0
        return replaceFCmp(&I, FCmpInst::FCMP_OEQ, X);
      case FCmpInst::FCMP_UGE:
        // fcmp uge fabs(x), smallest_normalized_number -> fcmp une x, 0.0
        return replaceFCmp(&I, FCmpInst::FCMP_UNE, X);
      case FCmpInst::FCMP_OGE:
        // fcmp oge fabs(x), smallest_normalized_number -> fcmp one x, 0.0
        return replaceFCmp(&I, FCmpInst::FCMP_ONE, X);
      case FCmpInst::FCMP_ULT:
        // fcmp ult fabs(x), smallest_normalized_number -> fcmp ueq x, 0.0
        return replaceFCmp(&I, FCmpInst::FCMP_UEQ, X);
      default:
        break;
      }
    }

    return nullptr;
  }

  auto replacePredAndOp0 = [&IC](FCmpInst *I, FCmpInst::Predicate P, Value *X) {
    I->setPredicate(P);
    return IC.replaceOperand(*I, 0, X);
  };

  switch (I.getPredicate()) {
  case FCmpInst::FCMP_UGE:
  case FCmpInst::FCMP_OLT:
    // fabs(X) >= 0.0 --> true
    // fabs(X) <  0.0 --> false
    llvm_unreachable("fcmp should have simplified");

  case FCmpInst::FCMP_OGT:
    // fabs(X) > 0.0 --> X != 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_ONE, X);

  case FCmpInst::FCMP_UGT:
    // fabs(X) u> 0.0 --> X u!= 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_UNE, X);

  case FCmpInst::FCMP_OLE:
    // fabs(X) <= 0.0 --> X == 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_OEQ, X);

  case FCmpInst::FCMP_ULE:
    // fabs(X) u<= 0.0 --> X u== 0.0
    return replacePredAndOp0(&I, FCmpInst::FCMP_UEQ, X);

  case FCmpInst::FCMP_OGE:
    // fabs(X) >= 0.0 --> !isnan(X)
    assert(!I.hasNoNaNs() && "fcmp should have simplified");
    return replacePredAndOp0(&I, FCmpInst::FCMP_ORD, X);

  case FCmpInst::FCMP_ULT:
    // fabs(X) u< 0.0 --> isnan(X)
    assert(!I.hasNoNaNs() && "fcmp should have simplified");
    return replacePredAndOp0(&I, FCmpInst::FCMP_UNO, X);

  case FCmpInst::FCMP_OEQ:
  case FCmpInst::FCMP_UEQ:
  case FCmpInst::FCMP_ONE:
  case FCmpInst::FCMP_UNE:
  case FCmpInst::FCMP_ORD:
  case FCmpInst::FCMP_UNO:
    // Look through the fabs() because it doesn't change anything but the sign.
    // fabs(X) == 0.0 --> X == 0.0,
    // fabs(X) != 0.0 --> X != 0.0
    // isnan(fabs(X)) --> isnan(X)
    // !isnan(fabs(X) --> !isnan(X)
    return replacePredAndOp0(&I, I.getPredicate(), X);

  default:
    return nullptr;
  }
}

static Instruction *foldFCmpFNegCommonOp(FCmpInst &I) {
  CmpInst::Predicate Pred = I.getPredicate();
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  // Canonicalize fneg as Op1.
  if (match(Op0, m_FNeg(m_Value())) && !match(Op1, m_FNeg(m_Value()))) {
    std::swap(Op0, Op1);
    Pred = I.getSwappedPredicate();
  }

  if (!match(Op1, m_FNeg(m_Specific(Op0))))
    return nullptr;

  // Replace the negated operand with 0.0:
  // fcmp Pred Op0, -Op0 --> fcmp Pred Op0, 0.0
  Constant *Zero = ConstantFP::getZero(Op0->getType());
  return new FCmpInst(Pred, Op0, Zero, "", &I);
}

static Instruction *foldFCmpFSubIntoFCmp(FCmpInst &I, Instruction *LHSI,
                                         Constant *RHSC, InstCombinerImpl &CI) {
  const CmpInst::Predicate Pred = I.getPredicate();
  Value *X = LHSI->getOperand(0);
  Value *Y = LHSI->getOperand(1);
  switch (Pred) {
  default:
    break;
  case FCmpInst::FCMP_UGT:
  case FCmpInst::FCMP_ULT:
  case FCmpInst::FCMP_UNE:
  case FCmpInst::FCMP_OEQ:
  case FCmpInst::FCMP_OGE:
  case FCmpInst::FCMP_OLE:
    // The optimization is not valid if X and Y are infinities of the same
    // sign, i.e. the inf - inf = nan case. If the fsub has the ninf or nnan
    // flag then we can assume we do not have that case. Otherwise we might be
    // able to prove that either X or Y is not infinity.
    if (!LHSI->hasNoNaNs() && !LHSI->hasNoInfs() &&
        !isKnownNeverInfinity(Y, /*Depth=*/0,
                              CI.getSimplifyQuery().getWithInstruction(&I)) &&
        !isKnownNeverInfinity(X, /*Depth=*/0,
                              CI.getSimplifyQuery().getWithInstruction(&I)))
      break;

    [[fallthrough]];
  case FCmpInst::FCMP_OGT:
  case FCmpInst::FCMP_OLT:
  case FCmpInst::FCMP_ONE:
  case FCmpInst::FCMP_UEQ:
  case FCmpInst::FCMP_UGE:
  case FCmpInst::FCMP_ULE:
    // fcmp pred (x - y), 0 --> fcmp pred x, y
    if (match(RHSC, m_AnyZeroFP()) &&
        I.getFunction()->getDenormalMode(
            LHSI->getType()->getScalarType()->getFltSemantics()) ==
            DenormalMode::getIEEE()) {
      CI.replaceOperand(I, 0, X);
      CI.replaceOperand(I, 1, Y);
      return &I;
    }
    break;
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitFCmpInst(FCmpInst &I) {
  bool Changed = false;

  /// Orders the operands of the compare so that they are listed from most
  /// complex to least complex.  This puts constants before unary operators,
  /// before binary operators.
  if (getComplexity(I.getOperand(0)) < getComplexity(I.getOperand(1))) {
    I.swapOperands();
    Changed = true;
  }

  const CmpInst::Predicate Pred = I.getPredicate();
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  if (Value *V = simplifyFCmpInst(Pred, Op0, Op1, I.getFastMathFlags(),
                                  SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  // Simplify 'fcmp pred X, X'
  Type *OpType = Op0->getType();
  assert(OpType == Op1->getType() && "fcmp with different-typed operands?");
  if (Op0 == Op1) {
    switch (Pred) {
      default: break;
    case FCmpInst::FCMP_UNO:    // True if unordered: isnan(X) | isnan(Y)
    case FCmpInst::FCMP_ULT:    // True if unordered or less than
    case FCmpInst::FCMP_UGT:    // True if unordered or greater than
    case FCmpInst::FCMP_UNE:    // True if unordered or not equal
      // Canonicalize these to be 'fcmp uno %X, 0.0'.
      I.setPredicate(FCmpInst::FCMP_UNO);
      I.setOperand(1, Constant::getNullValue(OpType));
      return &I;

    case FCmpInst::FCMP_ORD:    // True if ordered (no nans)
    case FCmpInst::FCMP_OEQ:    // True if ordered and equal
    case FCmpInst::FCMP_OGE:    // True if ordered and greater than or equal
    case FCmpInst::FCMP_OLE:    // True if ordered and less than or equal
      // Canonicalize these to be 'fcmp ord %X, 0.0'.
      I.setPredicate(FCmpInst::FCMP_ORD);
      I.setOperand(1, Constant::getNullValue(OpType));
      return &I;
    }
  }

  if (I.isCommutative()) {
    if (auto Pair = matchSymmetricPair(I.getOperand(0), I.getOperand(1))) {
      replaceOperand(I, 0, Pair->first);
      replaceOperand(I, 1, Pair->second);
      return &I;
    }
  }

  // If we're just checking for a NaN (ORD/UNO) and have a non-NaN operand,
  // then canonicalize the operand to 0.0.
  if (Pred == CmpInst::FCMP_ORD || Pred == CmpInst::FCMP_UNO) {
    if (!match(Op0, m_PosZeroFP()) &&
        isKnownNeverNaN(Op0, 0, getSimplifyQuery().getWithInstruction(&I)))
      return replaceOperand(I, 0, ConstantFP::getZero(OpType));

    if (!match(Op1, m_PosZeroFP()) &&
        isKnownNeverNaN(Op1, 0, getSimplifyQuery().getWithInstruction(&I)))
      return replaceOperand(I, 1, ConstantFP::getZero(OpType));
  }

  // fcmp pred (fneg X), (fneg Y) -> fcmp swap(pred) X, Y
  Value *X, *Y;
  if (match(Op0, m_FNeg(m_Value(X))) && match(Op1, m_FNeg(m_Value(Y))))
    return new FCmpInst(I.getSwappedPredicate(), X, Y, "", &I);

  if (Instruction *R = foldFCmpFNegCommonOp(I))
    return R;

  // Test if the FCmpInst instruction is used exclusively by a select as
  // part of a minimum or maximum operation. If so, refrain from doing
  // any other folding. This helps out other analyses which understand
  // non-obfuscated minimum and maximum idioms, such as ScalarEvolution
  // and CodeGen. And in this case, at least one of the comparison
  // operands has at least one user besides the compare (the select),
  // which would often largely negate the benefit of folding anyway.
  if (I.hasOneUse())
    if (SelectInst *SI = dyn_cast<SelectInst>(I.user_back())) {
      Value *A, *B;
      SelectPatternResult SPR = matchSelectPattern(SI, A, B);
      if (SPR.Flavor != SPF_UNKNOWN)
        return nullptr;
    }

  // The sign of 0.0 is ignored by fcmp, so canonicalize to +0.0:
  // fcmp Pred X, -0.0 --> fcmp Pred X, 0.0
  if (match(Op1, m_AnyZeroFP()) && !match(Op1, m_PosZeroFP()))
    return replaceOperand(I, 1, ConstantFP::getZero(OpType));

  // Canonicalize:
  // fcmp olt X, +inf -> fcmp one X, +inf
  // fcmp ole X, +inf -> fcmp ord X, 0
  // fcmp ogt X, +inf -> false
  // fcmp oge X, +inf -> fcmp oeq X, +inf
  // fcmp ult X, +inf -> fcmp une X, +inf
  // fcmp ule X, +inf -> true
  // fcmp ugt X, +inf -> fcmp uno X, 0
  // fcmp uge X, +inf -> fcmp ueq X, +inf
  // fcmp olt X, -inf -> false
  // fcmp ole X, -inf -> fcmp oeq X, -inf
  // fcmp ogt X, -inf -> fcmp one X, -inf
  // fcmp oge X, -inf -> fcmp ord X, 0
  // fcmp ult X, -inf -> fcmp uno X, 0
  // fcmp ule X, -inf -> fcmp ueq X, -inf
  // fcmp ugt X, -inf -> fcmp une X, -inf
  // fcmp uge X, -inf -> true
  const APFloat *C;
  if (match(Op1, m_APFloat(C)) && C->isInfinity()) {
    switch (C->isNegative() ? FCmpInst::getSwappedPredicate(Pred) : Pred) {
    default:
      break;
    case FCmpInst::FCMP_ORD:
    case FCmpInst::FCMP_UNO:
    case FCmpInst::FCMP_TRUE:
    case FCmpInst::FCMP_FALSE:
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_ULE:
      llvm_unreachable("Should be simplified by InstSimplify");
    case FCmpInst::FCMP_OLT:
      return new FCmpInst(FCmpInst::FCMP_ONE, Op0, Op1, "", &I);
    case FCmpInst::FCMP_OLE:
      return new FCmpInst(FCmpInst::FCMP_ORD, Op0, ConstantFP::getZero(OpType),
                          "", &I);
    case FCmpInst::FCMP_OGE:
      return new FCmpInst(FCmpInst::FCMP_OEQ, Op0, Op1, "", &I);
    case FCmpInst::FCMP_ULT:
      return new FCmpInst(FCmpInst::FCMP_UNE, Op0, Op1, "", &I);
    case FCmpInst::FCMP_UGT:
      return new FCmpInst(FCmpInst::FCMP_UNO, Op0, ConstantFP::getZero(OpType),
                          "", &I);
    case FCmpInst::FCMP_UGE:
      return new FCmpInst(FCmpInst::FCMP_UEQ, Op0, Op1, "", &I);
    }
  }

  // Ignore signbit of bitcasted int when comparing equality to FP 0.0:
  // fcmp oeq/une (bitcast X), 0.0 --> (and X, SignMaskC) ==/!= 0
  if (match(Op1, m_PosZeroFP()) &&
      match(Op0, m_OneUse(m_ElementWiseBitCast(m_Value(X))))) {
    ICmpInst::Predicate IntPred = ICmpInst::BAD_ICMP_PREDICATE;
    if (Pred == FCmpInst::FCMP_OEQ)
      IntPred = ICmpInst::ICMP_EQ;
    else if (Pred == FCmpInst::FCMP_UNE)
      IntPred = ICmpInst::ICMP_NE;

    if (IntPred != ICmpInst::BAD_ICMP_PREDICATE) {
      Type *IntTy = X->getType();
      const APInt &SignMask = ~APInt::getSignMask(IntTy->getScalarSizeInBits());
      Value *MaskX = Builder.CreateAnd(X, ConstantInt::get(IntTy, SignMask));
      return new ICmpInst(IntPred, MaskX, ConstantInt::getNullValue(IntTy));
    }
  }

  // Handle fcmp with instruction LHS and constant RHS.
  Instruction *LHSI;
  Constant *RHSC;
  if (match(Op0, m_Instruction(LHSI)) && match(Op1, m_Constant(RHSC))) {
    switch (LHSI->getOpcode()) {
    case Instruction::Select:
      // fcmp eq (cond ? x : -x), 0 --> fcmp eq x, 0
      if (FCmpInst::isEquality(Pred) && match(RHSC, m_AnyZeroFP()) &&
          (match(LHSI,
                 m_Select(m_Value(), m_Value(X), m_FNeg(m_Deferred(X)))) ||
           match(LHSI, m_Select(m_Value(), m_FNeg(m_Value(X)), m_Deferred(X)))))
        return replaceOperand(I, 0, X);
      if (Instruction *NV = FoldOpIntoSelect(I, cast<SelectInst>(LHSI)))
        return NV;
      break;
    case Instruction::FSub:
      if (LHSI->hasOneUse())
        if (Instruction *NV = foldFCmpFSubIntoFCmp(I, LHSI, RHSC, *this))
          return NV;
      break;
    case Instruction::PHI:
      if (Instruction *NV = foldOpIntoPhi(I, cast<PHINode>(LHSI)))
        return NV;
      break;
    case Instruction::SIToFP:
    case Instruction::UIToFP:
      if (Instruction *NV = foldFCmpIntToFPConst(I, LHSI, RHSC))
        return NV;
      break;
    case Instruction::FDiv:
      if (Instruction *NV = foldFCmpReciprocalAndZero(I, LHSI, RHSC))
        return NV;
      break;
    case Instruction::Load:
      if (auto *GEP = dyn_cast<GetElementPtrInst>(LHSI->getOperand(0)))
        if (auto *GV = dyn_cast<GlobalVariable>(GEP->getOperand(0)))
          if (Instruction *Res = foldCmpLoadFromIndexedGlobal(
                  cast<LoadInst>(LHSI), GEP, GV, I))
            return Res;
      break;
  }
  }

  if (Instruction *R = foldFabsWithFcmpZero(I, *this))
    return R;

  if (match(Op0, m_FNeg(m_Value(X)))) {
    // fcmp pred (fneg X), C --> fcmp swap(pred) X, -C
    Constant *C;
    if (match(Op1, m_Constant(C)))
      if (Constant *NegC = ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL))
        return new FCmpInst(I.getSwappedPredicate(), X, NegC, "", &I);
  }

  // fcmp (fadd X, 0.0), Y --> fcmp X, Y
  if (match(Op0, m_FAdd(m_Value(X), m_AnyZeroFP())))
    return new FCmpInst(Pred, X, Op1, "", &I);

  // fcmp X, (fadd Y, 0.0) --> fcmp X, Y
  if (match(Op1, m_FAdd(m_Value(Y), m_AnyZeroFP())))
    return new FCmpInst(Pred, Op0, Y, "", &I);

  if (match(Op0, m_FPExt(m_Value(X)))) {
    // fcmp (fpext X), (fpext Y) -> fcmp X, Y
    if (match(Op1, m_FPExt(m_Value(Y))) && X->getType() == Y->getType())
      return new FCmpInst(Pred, X, Y, "", &I);

    const APFloat *C;
    if (match(Op1, m_APFloat(C))) {
      const fltSemantics &FPSem =
          X->getType()->getScalarType()->getFltSemantics();
      bool Lossy;
      APFloat TruncC = *C;
      TruncC.convert(FPSem, APFloat::rmNearestTiesToEven, &Lossy);

      if (Lossy) {
        // X can't possibly equal the higher-precision constant, so reduce any
        // equality comparison.
        // TODO: Other predicates can be handled via getFCmpCode().
        switch (Pred) {
        case FCmpInst::FCMP_OEQ:
          // X is ordered and equal to an impossible constant --> false
          return replaceInstUsesWith(I, ConstantInt::getFalse(I.getType()));
        case FCmpInst::FCMP_ONE:
          // X is ordered and not equal to an impossible constant --> ordered
          return new FCmpInst(FCmpInst::FCMP_ORD, X,
                              ConstantFP::getZero(X->getType()));
        case FCmpInst::FCMP_UEQ:
          // X is unordered or equal to an impossible constant --> unordered
          return new FCmpInst(FCmpInst::FCMP_UNO, X,
                              ConstantFP::getZero(X->getType()));
        case FCmpInst::FCMP_UNE:
          // X is unordered or not equal to an impossible constant --> true
          return replaceInstUsesWith(I, ConstantInt::getTrue(I.getType()));
        default:
          break;
        }
      }

      // fcmp (fpext X), C -> fcmp X, (fptrunc C) if fptrunc is lossless
      // Avoid lossy conversions and denormals.
      // Zero is a special case that's OK to convert.
      APFloat Fabs = TruncC;
      Fabs.clearSign();
      if (!Lossy &&
          (Fabs.isZero() || !(Fabs < APFloat::getSmallestNormalized(FPSem)))) {
        Constant *NewC = ConstantFP::get(X->getType(), TruncC);
        return new FCmpInst(Pred, X, NewC, "", &I);
      }
    }
  }

  // Convert a sign-bit test of an FP value into a cast and integer compare.
  // TODO: Simplify if the copysign constant is 0.0 or NaN.
  // TODO: Handle non-zero compare constants.
  // TODO: Handle other predicates.
  if (match(Op0, m_OneUse(m_Intrinsic<Intrinsic::copysign>(m_APFloat(C),
                                                           m_Value(X)))) &&
      match(Op1, m_AnyZeroFP()) && !C->isZero() && !C->isNaN()) {
    Type *IntType = Builder.getIntNTy(X->getType()->getScalarSizeInBits());
    if (auto *VecTy = dyn_cast<VectorType>(OpType))
      IntType = VectorType::get(IntType, VecTy->getElementCount());

    // copysign(non-zero constant, X) < 0.0 --> (bitcast X) < 0
    if (Pred == FCmpInst::FCMP_OLT) {
      Value *IntX = Builder.CreateBitCast(X, IntType);
      return new ICmpInst(ICmpInst::ICMP_SLT, IntX,
                          ConstantInt::getNullValue(IntType));
    }
  }

  {
    Value *CanonLHS = nullptr, *CanonRHS = nullptr;
    match(Op0, m_Intrinsic<Intrinsic::canonicalize>(m_Value(CanonLHS)));
    match(Op1, m_Intrinsic<Intrinsic::canonicalize>(m_Value(CanonRHS)));

    // (canonicalize(x) == x) => (x == x)
    if (CanonLHS == Op1)
      return new FCmpInst(Pred, Op1, Op1, "", &I);

    // (x == canonicalize(x)) => (x == x)
    if (CanonRHS == Op0)
      return new FCmpInst(Pred, Op0, Op0, "", &I);

    // (canonicalize(x) == canonicalize(y)) => (x == y)
    if (CanonLHS && CanonRHS)
      return new FCmpInst(Pred, CanonLHS, CanonRHS, "", &I);
  }

  if (I.getType()->isVectorTy())
    if (Instruction *Res = foldVectorCmp(I, Builder))
      return Res;

  return Changed ? &I : nullptr;
}
