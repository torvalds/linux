//===- InstCombineVectorOps.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements instcombine for ExtractElement, InsertElement and
// ShuffleVector.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

#define DEBUG_TYPE "instcombine"

using namespace llvm;
using namespace PatternMatch;

STATISTIC(NumAggregateReconstructionsSimplified,
          "Number of aggregate reconstructions turned into reuse of the "
          "original aggregate");

/// Return true if the value is cheaper to scalarize than it is to leave as a
/// vector operation. If the extract index \p EI is a constant integer then
/// some operations may be cheap to scalarize.
///
/// FIXME: It's possible to create more instructions than previously existed.
static bool cheapToScalarize(Value *V, Value *EI) {
  ConstantInt *CEI = dyn_cast<ConstantInt>(EI);

  // If we can pick a scalar constant value out of a vector, that is free.
  if (auto *C = dyn_cast<Constant>(V))
    return CEI || C->getSplatValue();

  if (CEI && match(V, m_Intrinsic<Intrinsic::experimental_stepvector>())) {
    ElementCount EC = cast<VectorType>(V->getType())->getElementCount();
    // Index needs to be lower than the minimum size of the vector, because
    // for scalable vector, the vector size is known at run time.
    return CEI->getValue().ult(EC.getKnownMinValue());
  }

  // An insertelement to the same constant index as our extract will simplify
  // to the scalar inserted element. An insertelement to a different constant
  // index is irrelevant to our extract.
  if (match(V, m_InsertElt(m_Value(), m_Value(), m_ConstantInt())))
    return CEI;

  if (match(V, m_OneUse(m_Load(m_Value()))))
    return true;

  if (match(V, m_OneUse(m_UnOp())))
    return true;

  Value *V0, *V1;
  if (match(V, m_OneUse(m_BinOp(m_Value(V0), m_Value(V1)))))
    if (cheapToScalarize(V0, EI) || cheapToScalarize(V1, EI))
      return true;

  CmpInst::Predicate UnusedPred;
  if (match(V, m_OneUse(m_Cmp(UnusedPred, m_Value(V0), m_Value(V1)))))
    if (cheapToScalarize(V0, EI) || cheapToScalarize(V1, EI))
      return true;

  return false;
}

// If we have a PHI node with a vector type that is only used to feed
// itself and be an operand of extractelement at a constant location,
// try to replace the PHI of the vector type with a PHI of a scalar type.
Instruction *InstCombinerImpl::scalarizePHI(ExtractElementInst &EI,
                                            PHINode *PN) {
  SmallVector<Instruction *, 2> Extracts;
  // The users we want the PHI to have are:
  // 1) The EI ExtractElement (we already know this)
  // 2) Possibly more ExtractElements with the same index.
  // 3) Another operand, which will feed back into the PHI.
  Instruction *PHIUser = nullptr;
  for (auto *U : PN->users()) {
    if (ExtractElementInst *EU = dyn_cast<ExtractElementInst>(U)) {
      if (EI.getIndexOperand() == EU->getIndexOperand())
        Extracts.push_back(EU);
      else
        return nullptr;
    } else if (!PHIUser) {
      PHIUser = cast<Instruction>(U);
    } else {
      return nullptr;
    }
  }

  if (!PHIUser)
    return nullptr;

  // Verify that this PHI user has one use, which is the PHI itself,
  // and that it is a binary operation which is cheap to scalarize.
  // otherwise return nullptr.
  if (!PHIUser->hasOneUse() || !(PHIUser->user_back() == PN) ||
      !(isa<BinaryOperator>(PHIUser)) ||
      !cheapToScalarize(PHIUser, EI.getIndexOperand()))
    return nullptr;

  // Create a scalar PHI node that will replace the vector PHI node
  // just before the current PHI node.
  PHINode *scalarPHI = cast<PHINode>(InsertNewInstWith(
      PHINode::Create(EI.getType(), PN->getNumIncomingValues(), ""), PN->getIterator()));
  // Scalarize each PHI operand.
  for (unsigned i = 0; i < PN->getNumIncomingValues(); i++) {
    Value *PHIInVal = PN->getIncomingValue(i);
    BasicBlock *inBB = PN->getIncomingBlock(i);
    Value *Elt = EI.getIndexOperand();
    // If the operand is the PHI induction variable:
    if (PHIInVal == PHIUser) {
      // Scalarize the binary operation. Its first operand is the
      // scalar PHI, and the second operand is extracted from the other
      // vector operand.
      BinaryOperator *B0 = cast<BinaryOperator>(PHIUser);
      unsigned opId = (B0->getOperand(0) == PN) ? 1 : 0;
      Value *Op = InsertNewInstWith(
          ExtractElementInst::Create(B0->getOperand(opId), Elt,
                                     B0->getOperand(opId)->getName() + ".Elt"),
          B0->getIterator());
      Value *newPHIUser = InsertNewInstWith(
          BinaryOperator::CreateWithCopiedFlags(B0->getOpcode(),
                                                scalarPHI, Op, B0), B0->getIterator());
      scalarPHI->addIncoming(newPHIUser, inBB);
    } else {
      // Scalarize PHI input:
      Instruction *newEI = ExtractElementInst::Create(PHIInVal, Elt, "");
      // Insert the new instruction into the predecessor basic block.
      Instruction *pos = dyn_cast<Instruction>(PHIInVal);
      BasicBlock::iterator InsertPos;
      if (pos && !isa<PHINode>(pos)) {
        InsertPos = ++pos->getIterator();
      } else {
        InsertPos = inBB->getFirstInsertionPt();
      }

      InsertNewInstWith(newEI, InsertPos);

      scalarPHI->addIncoming(newEI, inBB);
    }
  }

  for (auto *E : Extracts) {
    replaceInstUsesWith(*E, scalarPHI);
    // Add old extract to worklist for DCE.
    addToWorklist(E);
  }

  return &EI;
}

Instruction *InstCombinerImpl::foldBitcastExtElt(ExtractElementInst &Ext) {
  Value *X;
  uint64_t ExtIndexC;
  if (!match(Ext.getVectorOperand(), m_BitCast(m_Value(X))) ||
      !match(Ext.getIndexOperand(), m_ConstantInt(ExtIndexC)))
    return nullptr;

  ElementCount NumElts =
      cast<VectorType>(Ext.getVectorOperandType())->getElementCount();
  Type *DestTy = Ext.getType();
  unsigned DestWidth = DestTy->getPrimitiveSizeInBits();
  bool IsBigEndian = DL.isBigEndian();

  // If we are casting an integer to vector and extracting a portion, that is
  // a shift-right and truncate.
  if (X->getType()->isIntegerTy()) {
    assert(isa<FixedVectorType>(Ext.getVectorOperand()->getType()) &&
           "Expected fixed vector type for bitcast from scalar integer");

    // Big endian requires adjusting the extract index since MSB is at index 0.
    // LittleEndian: extelt (bitcast i32 X to v4i8), 0 -> trunc i32 X to i8
    // BigEndian: extelt (bitcast i32 X to v4i8), 0 -> trunc i32 (X >> 24) to i8
    if (IsBigEndian)
      ExtIndexC = NumElts.getKnownMinValue() - 1 - ExtIndexC;
    unsigned ShiftAmountC = ExtIndexC * DestWidth;
    if (!ShiftAmountC ||
        (isDesirableIntType(X->getType()->getPrimitiveSizeInBits()) &&
        Ext.getVectorOperand()->hasOneUse())) {
      if (ShiftAmountC)
        X = Builder.CreateLShr(X, ShiftAmountC, "extelt.offset");
      if (DestTy->isFloatingPointTy()) {
        Type *DstIntTy = IntegerType::getIntNTy(X->getContext(), DestWidth);
        Value *Trunc = Builder.CreateTrunc(X, DstIntTy);
        return new BitCastInst(Trunc, DestTy);
      }
      return new TruncInst(X, DestTy);
    }
  }

  if (!X->getType()->isVectorTy())
    return nullptr;

  // If this extractelement is using a bitcast from a vector of the same number
  // of elements, see if we can find the source element from the source vector:
  // extelt (bitcast VecX), IndexC --> bitcast X[IndexC]
  auto *SrcTy = cast<VectorType>(X->getType());
  ElementCount NumSrcElts = SrcTy->getElementCount();
  if (NumSrcElts == NumElts)
    if (Value *Elt = findScalarElement(X, ExtIndexC))
      return new BitCastInst(Elt, DestTy);

  assert(NumSrcElts.isScalable() == NumElts.isScalable() &&
         "Src and Dst must be the same sort of vector type");

  // If the source elements are wider than the destination, try to shift and
  // truncate a subset of scalar bits of an insert op.
  if (NumSrcElts.getKnownMinValue() < NumElts.getKnownMinValue()) {
    Value *Scalar;
    Value *Vec;
    uint64_t InsIndexC;
    if (!match(X, m_InsertElt(m_Value(Vec), m_Value(Scalar),
                              m_ConstantInt(InsIndexC))))
      return nullptr;

    // The extract must be from the subset of vector elements that we inserted
    // into. Example: if we inserted element 1 of a <2 x i64> and we are
    // extracting an i16 (narrowing ratio = 4), then this extract must be from 1
    // of elements 4-7 of the bitcasted vector.
    unsigned NarrowingRatio =
        NumElts.getKnownMinValue() / NumSrcElts.getKnownMinValue();

    if (ExtIndexC / NarrowingRatio != InsIndexC) {
      // Remove insertelement, if we don't use the inserted element.
      // extractelement (bitcast (insertelement (Vec, b)), a) ->
      // extractelement (bitcast (Vec), a)
      // FIXME: this should be removed to SimplifyDemandedVectorElts,
      // once scale vectors are supported.
      if (X->hasOneUse() && Ext.getVectorOperand()->hasOneUse()) {
        Value *NewBC = Builder.CreateBitCast(Vec, Ext.getVectorOperandType());
        return ExtractElementInst::Create(NewBC, Ext.getIndexOperand());
      }
      return nullptr;
    }

    // We are extracting part of the original scalar. How that scalar is
    // inserted into the vector depends on the endian-ness. Example:
    //              Vector Byte Elt Index:    0  1  2  3  4  5  6  7
    //                                       +--+--+--+--+--+--+--+--+
    // inselt <2 x i32> V, <i32> S, 1:       |V0|V1|V2|V3|S0|S1|S2|S3|
    // extelt <4 x i16> V', 3:               |                 |S2|S3|
    //                                       +--+--+--+--+--+--+--+--+
    // If this is little-endian, S2|S3 are the MSB of the 32-bit 'S' value.
    // If this is big-endian, S2|S3 are the LSB of the 32-bit 'S' value.
    // In this example, we must right-shift little-endian. Big-endian is just a
    // truncate.
    unsigned Chunk = ExtIndexC % NarrowingRatio;
    if (IsBigEndian)
      Chunk = NarrowingRatio - 1 - Chunk;

    // Bail out if this is an FP vector to FP vector sequence. That would take
    // more instructions than we started with unless there is no shift, and it
    // may not be handled as well in the backend.
    bool NeedSrcBitcast = SrcTy->getScalarType()->isFloatingPointTy();
    bool NeedDestBitcast = DestTy->isFloatingPointTy();
    if (NeedSrcBitcast && NeedDestBitcast)
      return nullptr;

    unsigned SrcWidth = SrcTy->getScalarSizeInBits();
    unsigned ShAmt = Chunk * DestWidth;

    // TODO: This limitation is more strict than necessary. We could sum the
    // number of new instructions and subtract the number eliminated to know if
    // we can proceed.
    if (!X->hasOneUse() || !Ext.getVectorOperand()->hasOneUse())
      if (NeedSrcBitcast || NeedDestBitcast)
        return nullptr;

    if (NeedSrcBitcast) {
      Type *SrcIntTy = IntegerType::getIntNTy(Scalar->getContext(), SrcWidth);
      Scalar = Builder.CreateBitCast(Scalar, SrcIntTy);
    }

    if (ShAmt) {
      // Bail out if we could end with more instructions than we started with.
      if (!Ext.getVectorOperand()->hasOneUse())
        return nullptr;
      Scalar = Builder.CreateLShr(Scalar, ShAmt);
    }

    if (NeedDestBitcast) {
      Type *DestIntTy = IntegerType::getIntNTy(Scalar->getContext(), DestWidth);
      return new BitCastInst(Builder.CreateTrunc(Scalar, DestIntTy), DestTy);
    }
    return new TruncInst(Scalar, DestTy);
  }

  return nullptr;
}

/// Find elements of V demanded by UserInstr.
static APInt findDemandedEltsBySingleUser(Value *V, Instruction *UserInstr) {
  unsigned VWidth = cast<FixedVectorType>(V->getType())->getNumElements();

  // Conservatively assume that all elements are needed.
  APInt UsedElts(APInt::getAllOnes(VWidth));

  switch (UserInstr->getOpcode()) {
  case Instruction::ExtractElement: {
    ExtractElementInst *EEI = cast<ExtractElementInst>(UserInstr);
    assert(EEI->getVectorOperand() == V);
    ConstantInt *EEIIndexC = dyn_cast<ConstantInt>(EEI->getIndexOperand());
    if (EEIIndexC && EEIIndexC->getValue().ult(VWidth)) {
      UsedElts = APInt::getOneBitSet(VWidth, EEIIndexC->getZExtValue());
    }
    break;
  }
  case Instruction::ShuffleVector: {
    ShuffleVectorInst *Shuffle = cast<ShuffleVectorInst>(UserInstr);
    unsigned MaskNumElts =
        cast<FixedVectorType>(UserInstr->getType())->getNumElements();

    UsedElts = APInt(VWidth, 0);
    for (unsigned i = 0; i < MaskNumElts; i++) {
      unsigned MaskVal = Shuffle->getMaskValue(i);
      if (MaskVal == -1u || MaskVal >= 2 * VWidth)
        continue;
      if (Shuffle->getOperand(0) == V && (MaskVal < VWidth))
        UsedElts.setBit(MaskVal);
      if (Shuffle->getOperand(1) == V &&
          ((MaskVal >= VWidth) && (MaskVal < 2 * VWidth)))
        UsedElts.setBit(MaskVal - VWidth);
    }
    break;
  }
  default:
    break;
  }
  return UsedElts;
}

/// Find union of elements of V demanded by all its users.
/// If it is known by querying findDemandedEltsBySingleUser that
/// no user demands an element of V, then the corresponding bit
/// remains unset in the returned value.
static APInt findDemandedEltsByAllUsers(Value *V) {
  unsigned VWidth = cast<FixedVectorType>(V->getType())->getNumElements();

  APInt UnionUsedElts(VWidth, 0);
  for (const Use &U : V->uses()) {
    if (Instruction *I = dyn_cast<Instruction>(U.getUser())) {
      UnionUsedElts |= findDemandedEltsBySingleUser(V, I);
    } else {
      UnionUsedElts = APInt::getAllOnes(VWidth);
      break;
    }

    if (UnionUsedElts.isAllOnes())
      break;
  }

  return UnionUsedElts;
}

/// Given a constant index for a extractelement or insertelement instruction,
/// return it with the canonical type if it isn't already canonical.  We
/// arbitrarily pick 64 bit as our canonical type.  The actual bitwidth doesn't
/// matter, we just want a consistent type to simplify CSE.
static ConstantInt *getPreferredVectorIndex(ConstantInt *IndexC) {
  const unsigned IndexBW = IndexC->getBitWidth();
  if (IndexBW == 64 || IndexC->getValue().getActiveBits() > 64)
    return nullptr;
  return ConstantInt::get(IndexC->getContext(),
                          IndexC->getValue().zextOrTrunc(64));
}

Instruction *InstCombinerImpl::visitExtractElementInst(ExtractElementInst &EI) {
  Value *SrcVec = EI.getVectorOperand();
  Value *Index = EI.getIndexOperand();
  if (Value *V = simplifyExtractElementInst(SrcVec, Index,
                                            SQ.getWithInstruction(&EI)))
    return replaceInstUsesWith(EI, V);

  // extractelt (select %x, %vec1, %vec2), %const ->
  // select %x, %vec1[%const], %vec2[%const]
  // TODO: Support constant folding of multiple select operands:
  // extractelt (select %x, %vec1, %vec2), (select %x, %c1, %c2)
  // If the extractelement will for instance try to do out of bounds accesses
  // because of the values of %c1 and/or %c2, the sequence could be optimized
  // early. This is currently not possible because constant folding will reach
  // an unreachable assertion if it doesn't find a constant operand.
  if (SelectInst *SI = dyn_cast<SelectInst>(EI.getVectorOperand()))
    if (SI->getCondition()->getType()->isIntegerTy() &&
        isa<Constant>(EI.getIndexOperand()))
      if (Instruction *R = FoldOpIntoSelect(EI, SI))
        return R;

  // If extracting a specified index from the vector, see if we can recursively
  // find a previously computed scalar that was inserted into the vector.
  auto *IndexC = dyn_cast<ConstantInt>(Index);
  bool HasKnownValidIndex = false;
  if (IndexC) {
    // Canonicalize type of constant indices to i64 to simplify CSE
    if (auto *NewIdx = getPreferredVectorIndex(IndexC))
      return replaceOperand(EI, 1, NewIdx);

    ElementCount EC = EI.getVectorOperandType()->getElementCount();
    unsigned NumElts = EC.getKnownMinValue();
    HasKnownValidIndex = IndexC->getValue().ult(NumElts);

    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(SrcVec)) {
      Intrinsic::ID IID = II->getIntrinsicID();
      // Index needs to be lower than the minimum size of the vector, because
      // for scalable vector, the vector size is known at run time.
      if (IID == Intrinsic::experimental_stepvector &&
          IndexC->getValue().ult(NumElts)) {
        Type *Ty = EI.getType();
        unsigned BitWidth = Ty->getIntegerBitWidth();
        Value *Idx;
        // Return index when its value does not exceed the allowed limit
        // for the element type of the vector, otherwise return undefined.
        if (IndexC->getValue().getActiveBits() <= BitWidth)
          Idx = ConstantInt::get(Ty, IndexC->getValue().zextOrTrunc(BitWidth));
        else
          Idx = PoisonValue::get(Ty);
        return replaceInstUsesWith(EI, Idx);
      }
    }

    // InstSimplify should handle cases where the index is invalid.
    // For fixed-length vector, it's invalid to extract out-of-range element.
    if (!EC.isScalable() && IndexC->getValue().uge(NumElts))
      return nullptr;

    if (Instruction *I = foldBitcastExtElt(EI))
      return I;

    // If there's a vector PHI feeding a scalar use through this extractelement
    // instruction, try to scalarize the PHI.
    if (auto *Phi = dyn_cast<PHINode>(SrcVec))
      if (Instruction *ScalarPHI = scalarizePHI(EI, Phi))
        return ScalarPHI;
  }

  // TODO come up with a n-ary matcher that subsumes both unary and
  // binary matchers.
  UnaryOperator *UO;
  if (match(SrcVec, m_UnOp(UO)) && cheapToScalarize(SrcVec, Index)) {
    // extelt (unop X), Index --> unop (extelt X, Index)
    Value *X = UO->getOperand(0);
    Value *E = Builder.CreateExtractElement(X, Index);
    return UnaryOperator::CreateWithCopiedFlags(UO->getOpcode(), E, UO);
  }

  // If the binop is not speculatable, we cannot hoist the extractelement if
  // it may make the operand poison.
  BinaryOperator *BO;
  if (match(SrcVec, m_BinOp(BO)) && cheapToScalarize(SrcVec, Index) &&
      (HasKnownValidIndex || isSafeToSpeculativelyExecute(BO))) {
    // extelt (binop X, Y), Index --> binop (extelt X, Index), (extelt Y, Index)
    Value *X = BO->getOperand(0), *Y = BO->getOperand(1);
    Value *E0 = Builder.CreateExtractElement(X, Index);
    Value *E1 = Builder.CreateExtractElement(Y, Index);
    return BinaryOperator::CreateWithCopiedFlags(BO->getOpcode(), E0, E1, BO);
  }

  Value *X, *Y;
  CmpInst::Predicate Pred;
  if (match(SrcVec, m_Cmp(Pred, m_Value(X), m_Value(Y))) &&
      cheapToScalarize(SrcVec, Index)) {
    // extelt (cmp X, Y), Index --> cmp (extelt X, Index), (extelt Y, Index)
    Value *E0 = Builder.CreateExtractElement(X, Index);
    Value *E1 = Builder.CreateExtractElement(Y, Index);
    CmpInst *SrcCmpInst = cast<CmpInst>(SrcVec);
    return CmpInst::CreateWithCopiedFlags(SrcCmpInst->getOpcode(), Pred, E0, E1,
                                          SrcCmpInst);
  }

  if (auto *I = dyn_cast<Instruction>(SrcVec)) {
    if (auto *IE = dyn_cast<InsertElementInst>(I)) {
      // instsimplify already handled the case where the indices are constants
      // and equal by value, if both are constants, they must not be the same
      // value, extract from the pre-inserted value instead.
      if (isa<Constant>(IE->getOperand(2)) && IndexC)
        return replaceOperand(EI, 0, IE->getOperand(0));
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      auto *VecType = cast<VectorType>(GEP->getType());
      ElementCount EC = VecType->getElementCount();
      uint64_t IdxVal = IndexC ? IndexC->getZExtValue() : 0;
      if (IndexC && IdxVal < EC.getKnownMinValue() && GEP->hasOneUse()) {
        // Find out why we have a vector result - these are a few examples:
        //  1. We have a scalar pointer and a vector of indices, or
        //  2. We have a vector of pointers and a scalar index, or
        //  3. We have a vector of pointers and a vector of indices, etc.
        // Here we only consider combining when there is exactly one vector
        // operand, since the optimization is less obviously a win due to
        // needing more than one extractelements.

        unsigned VectorOps =
            llvm::count_if(GEP->operands(), [](const Value *V) {
              return isa<VectorType>(V->getType());
            });
        if (VectorOps == 1) {
          Value *NewPtr = GEP->getPointerOperand();
          if (isa<VectorType>(NewPtr->getType()))
            NewPtr = Builder.CreateExtractElement(NewPtr, IndexC);

          SmallVector<Value *> NewOps;
          for (unsigned I = 1; I != GEP->getNumOperands(); ++I) {
            Value *Op = GEP->getOperand(I);
            if (isa<VectorType>(Op->getType()))
              NewOps.push_back(Builder.CreateExtractElement(Op, IndexC));
            else
              NewOps.push_back(Op);
          }

          GetElementPtrInst *NewGEP = GetElementPtrInst::Create(
              GEP->getSourceElementType(), NewPtr, NewOps);
          NewGEP->setIsInBounds(GEP->isInBounds());
          return NewGEP;
        }
      }
    } else if (auto *SVI = dyn_cast<ShuffleVectorInst>(I)) {
      // If this is extracting an element from a shufflevector, figure out where
      // it came from and extract from the appropriate input element instead.
      // Restrict the following transformation to fixed-length vector.
      if (isa<FixedVectorType>(SVI->getType()) && isa<ConstantInt>(Index)) {
        int SrcIdx =
            SVI->getMaskValue(cast<ConstantInt>(Index)->getZExtValue());
        Value *Src;
        unsigned LHSWidth = cast<FixedVectorType>(SVI->getOperand(0)->getType())
                                ->getNumElements();

        if (SrcIdx < 0)
          return replaceInstUsesWith(EI, PoisonValue::get(EI.getType()));
        if (SrcIdx < (int)LHSWidth)
          Src = SVI->getOperand(0);
        else {
          SrcIdx -= LHSWidth;
          Src = SVI->getOperand(1);
        }
        Type *Int64Ty = Type::getInt64Ty(EI.getContext());
        return ExtractElementInst::Create(
            Src, ConstantInt::get(Int64Ty, SrcIdx, false));
      }
    } else if (auto *CI = dyn_cast<CastInst>(I)) {
      // Canonicalize extractelement(cast) -> cast(extractelement).
      // Bitcasts can change the number of vector elements, and they cost
      // nothing.
      if (CI->hasOneUse() && (CI->getOpcode() != Instruction::BitCast)) {
        Value *EE = Builder.CreateExtractElement(CI->getOperand(0), Index);
        return CastInst::Create(CI->getOpcode(), EE, EI.getType());
      }
    }
  }

  // Run demanded elements after other transforms as this can drop flags on
  // binops.  If there's two paths to the same final result, we prefer the
  // one which doesn't force us to drop flags.
  if (IndexC) {
    ElementCount EC = EI.getVectorOperandType()->getElementCount();
    unsigned NumElts = EC.getKnownMinValue();
    // This instruction only demands the single element from the input vector.
    // Skip for scalable type, the number of elements is unknown at
    // compile-time.
    if (!EC.isScalable() && NumElts != 1) {
      // If the input vector has a single use, simplify it based on this use
      // property.
      if (SrcVec->hasOneUse()) {
        APInt PoisonElts(NumElts, 0);
        APInt DemandedElts(NumElts, 0);
        DemandedElts.setBit(IndexC->getZExtValue());
        if (Value *V =
                SimplifyDemandedVectorElts(SrcVec, DemandedElts, PoisonElts))
          return replaceOperand(EI, 0, V);
      } else {
        // If the input vector has multiple uses, simplify it based on a union
        // of all elements used.
        APInt DemandedElts = findDemandedEltsByAllUsers(SrcVec);
        if (!DemandedElts.isAllOnes()) {
          APInt PoisonElts(NumElts, 0);
          if (Value *V = SimplifyDemandedVectorElts(
                  SrcVec, DemandedElts, PoisonElts, 0 /* Depth */,
                  true /* AllowMultipleUsers */)) {
            if (V != SrcVec) {
              Worklist.addValue(SrcVec);
              SrcVec->replaceAllUsesWith(V);
              return &EI;
            }
          }
        }
      }
    }
  }
  return nullptr;
}

/// If V is a shuffle of values that ONLY returns elements from either LHS or
/// RHS, return the shuffle mask and true. Otherwise, return false.
static bool collectSingleShuffleElements(Value *V, Value *LHS, Value *RHS,
                                         SmallVectorImpl<int> &Mask) {
  assert(LHS->getType() == RHS->getType() &&
         "Invalid CollectSingleShuffleElements");
  unsigned NumElts = cast<FixedVectorType>(V->getType())->getNumElements();

  if (match(V, m_Poison())) {
    Mask.assign(NumElts, -1);
    return true;
  }

  if (V == LHS) {
    for (unsigned i = 0; i != NumElts; ++i)
      Mask.push_back(i);
    return true;
  }

  if (V == RHS) {
    for (unsigned i = 0; i != NumElts; ++i)
      Mask.push_back(i + NumElts);
    return true;
  }

  if (InsertElementInst *IEI = dyn_cast<InsertElementInst>(V)) {
    // If this is an insert of an extract from some other vector, include it.
    Value *VecOp    = IEI->getOperand(0);
    Value *ScalarOp = IEI->getOperand(1);
    Value *IdxOp    = IEI->getOperand(2);

    if (!isa<ConstantInt>(IdxOp))
      return false;
    unsigned InsertedIdx = cast<ConstantInt>(IdxOp)->getZExtValue();

    if (isa<PoisonValue>(ScalarOp)) {  // inserting poison into vector.
      // We can handle this if the vector we are inserting into is
      // transitively ok.
      if (collectSingleShuffleElements(VecOp, LHS, RHS, Mask)) {
        // If so, update the mask to reflect the inserted poison.
        Mask[InsertedIdx] = -1;
        return true;
      }
    } else if (ExtractElementInst *EI = dyn_cast<ExtractElementInst>(ScalarOp)){
      if (isa<ConstantInt>(EI->getOperand(1))) {
        unsigned ExtractedIdx =
        cast<ConstantInt>(EI->getOperand(1))->getZExtValue();
        unsigned NumLHSElts =
            cast<FixedVectorType>(LHS->getType())->getNumElements();

        // This must be extracting from either LHS or RHS.
        if (EI->getOperand(0) == LHS || EI->getOperand(0) == RHS) {
          // We can handle this if the vector we are inserting into is
          // transitively ok.
          if (collectSingleShuffleElements(VecOp, LHS, RHS, Mask)) {
            // If so, update the mask to reflect the inserted value.
            if (EI->getOperand(0) == LHS) {
              Mask[InsertedIdx % NumElts] = ExtractedIdx;
            } else {
              assert(EI->getOperand(0) == RHS);
              Mask[InsertedIdx % NumElts] = ExtractedIdx + NumLHSElts;
            }
            return true;
          }
        }
      }
    }
  }

  return false;
}

/// If we have insertion into a vector that is wider than the vector that we
/// are extracting from, try to widen the source vector to allow a single
/// shufflevector to replace one or more insert/extract pairs.
static bool replaceExtractElements(InsertElementInst *InsElt,
                                   ExtractElementInst *ExtElt,
                                   InstCombinerImpl &IC) {
  auto *InsVecType = cast<FixedVectorType>(InsElt->getType());
  auto *ExtVecType = cast<FixedVectorType>(ExtElt->getVectorOperandType());
  unsigned NumInsElts = InsVecType->getNumElements();
  unsigned NumExtElts = ExtVecType->getNumElements();

  // The inserted-to vector must be wider than the extracted-from vector.
  if (InsVecType->getElementType() != ExtVecType->getElementType() ||
      NumExtElts >= NumInsElts)
    return false;

  // Create a shuffle mask to widen the extended-from vector using poison
  // values. The mask selects all of the values of the original vector followed
  // by as many poison values as needed to create a vector of the same length
  // as the inserted-to vector.
  SmallVector<int, 16> ExtendMask;
  for (unsigned i = 0; i < NumExtElts; ++i)
    ExtendMask.push_back(i);
  for (unsigned i = NumExtElts; i < NumInsElts; ++i)
    ExtendMask.push_back(-1);

  Value *ExtVecOp = ExtElt->getVectorOperand();
  auto *ExtVecOpInst = dyn_cast<Instruction>(ExtVecOp);
  BasicBlock *InsertionBlock = (ExtVecOpInst && !isa<PHINode>(ExtVecOpInst))
                                   ? ExtVecOpInst->getParent()
                                   : ExtElt->getParent();

  // TODO: This restriction matches the basic block check below when creating
  // new extractelement instructions. If that limitation is removed, this one
  // could also be removed. But for now, we just bail out to ensure that we
  // will replace the extractelement instruction that is feeding our
  // insertelement instruction. This allows the insertelement to then be
  // replaced by a shufflevector. If the insertelement is not replaced, we can
  // induce infinite looping because there's an optimization for extractelement
  // that will delete our widening shuffle. This would trigger another attempt
  // here to create that shuffle, and we spin forever.
  if (InsertionBlock != InsElt->getParent())
    return false;

  // TODO: This restriction matches the check in visitInsertElementInst() and
  // prevents an infinite loop caused by not turning the extract/insert pair
  // into a shuffle. We really should not need either check, but we're lacking
  // folds for shufflevectors because we're afraid to generate shuffle masks
  // that the backend can't handle.
  if (InsElt->hasOneUse() && isa<InsertElementInst>(InsElt->user_back()))
    return false;

  auto *WideVec = new ShuffleVectorInst(ExtVecOp, ExtendMask);

  // Insert the new shuffle after the vector operand of the extract is defined
  // (as long as it's not a PHI) or at the start of the basic block of the
  // extract, so any subsequent extracts in the same basic block can use it.
  // TODO: Insert before the earliest ExtractElementInst that is replaced.
  if (ExtVecOpInst && !isa<PHINode>(ExtVecOpInst))
    WideVec->insertAfter(ExtVecOpInst);
  else
    IC.InsertNewInstWith(WideVec, ExtElt->getParent()->getFirstInsertionPt());

  // Replace extracts from the original narrow vector with extracts from the new
  // wide vector.
  for (User *U : ExtVecOp->users()) {
    ExtractElementInst *OldExt = dyn_cast<ExtractElementInst>(U);
    if (!OldExt || OldExt->getParent() != WideVec->getParent())
      continue;
    auto *NewExt = ExtractElementInst::Create(WideVec, OldExt->getOperand(1));
    IC.InsertNewInstWith(NewExt, OldExt->getIterator());
    IC.replaceInstUsesWith(*OldExt, NewExt);
    // Add the old extracts to the worklist for DCE. We can't remove the
    // extracts directly, because they may still be used by the calling code.
    IC.addToWorklist(OldExt);
  }

  return true;
}

/// We are building a shuffle to create V, which is a sequence of insertelement,
/// extractelement pairs. If PermittedRHS is set, then we must either use it or
/// not rely on the second vector source. Return a std::pair containing the
/// left and right vectors of the proposed shuffle (or 0), and set the Mask
/// parameter as required.
///
/// Note: we intentionally don't try to fold earlier shuffles since they have
/// often been chosen carefully to be efficiently implementable on the target.
using ShuffleOps = std::pair<Value *, Value *>;

static ShuffleOps collectShuffleElements(Value *V, SmallVectorImpl<int> &Mask,
                                         Value *PermittedRHS,
                                         InstCombinerImpl &IC, bool &Rerun) {
  assert(V->getType()->isVectorTy() && "Invalid shuffle!");
  unsigned NumElts = cast<FixedVectorType>(V->getType())->getNumElements();

  if (match(V, m_Poison())) {
    Mask.assign(NumElts, -1);
    return std::make_pair(
        PermittedRHS ? PoisonValue::get(PermittedRHS->getType()) : V, nullptr);
  }

  if (isa<ConstantAggregateZero>(V)) {
    Mask.assign(NumElts, 0);
    return std::make_pair(V, nullptr);
  }

  if (InsertElementInst *IEI = dyn_cast<InsertElementInst>(V)) {
    // If this is an insert of an extract from some other vector, include it.
    Value *VecOp    = IEI->getOperand(0);
    Value *ScalarOp = IEI->getOperand(1);
    Value *IdxOp    = IEI->getOperand(2);

    if (ExtractElementInst *EI = dyn_cast<ExtractElementInst>(ScalarOp)) {
      if (isa<ConstantInt>(EI->getOperand(1)) && isa<ConstantInt>(IdxOp)) {
        unsigned ExtractedIdx =
          cast<ConstantInt>(EI->getOperand(1))->getZExtValue();
        unsigned InsertedIdx = cast<ConstantInt>(IdxOp)->getZExtValue();

        // Either the extracted from or inserted into vector must be RHSVec,
        // otherwise we'd end up with a shuffle of three inputs.
        if (EI->getOperand(0) == PermittedRHS || PermittedRHS == nullptr) {
          Value *RHS = EI->getOperand(0);
          ShuffleOps LR = collectShuffleElements(VecOp, Mask, RHS, IC, Rerun);
          assert(LR.second == nullptr || LR.second == RHS);

          if (LR.first->getType() != RHS->getType()) {
            // Although we are giving up for now, see if we can create extracts
            // that match the inserts for another round of combining.
            if (replaceExtractElements(IEI, EI, IC))
              Rerun = true;

            // We tried our best, but we can't find anything compatible with RHS
            // further up the chain. Return a trivial shuffle.
            for (unsigned i = 0; i < NumElts; ++i)
              Mask[i] = i;
            return std::make_pair(V, nullptr);
          }

          unsigned NumLHSElts =
              cast<FixedVectorType>(RHS->getType())->getNumElements();
          Mask[InsertedIdx % NumElts] = NumLHSElts + ExtractedIdx;
          return std::make_pair(LR.first, RHS);
        }

        if (VecOp == PermittedRHS) {
          // We've gone as far as we can: anything on the other side of the
          // extractelement will already have been converted into a shuffle.
          unsigned NumLHSElts =
              cast<FixedVectorType>(EI->getOperand(0)->getType())
                  ->getNumElements();
          for (unsigned i = 0; i != NumElts; ++i)
            Mask.push_back(i == InsertedIdx ? ExtractedIdx : NumLHSElts + i);
          return std::make_pair(EI->getOperand(0), PermittedRHS);
        }

        // If this insertelement is a chain that comes from exactly these two
        // vectors, return the vector and the effective shuffle.
        if (EI->getOperand(0)->getType() == PermittedRHS->getType() &&
            collectSingleShuffleElements(IEI, EI->getOperand(0), PermittedRHS,
                                         Mask))
          return std::make_pair(EI->getOperand(0), PermittedRHS);
      }
    }
  }

  // Otherwise, we can't do anything fancy. Return an identity vector.
  for (unsigned i = 0; i != NumElts; ++i)
    Mask.push_back(i);
  return std::make_pair(V, nullptr);
}

/// Look for chain of insertvalue's that fully define an aggregate, and trace
/// back the values inserted, see if they are all were extractvalue'd from
/// the same source aggregate from the exact same element indexes.
/// If they were, just reuse the source aggregate.
/// This potentially deals with PHI indirections.
Instruction *InstCombinerImpl::foldAggregateConstructionIntoAggregateReuse(
    InsertValueInst &OrigIVI) {
  Type *AggTy = OrigIVI.getType();
  unsigned NumAggElts;
  switch (AggTy->getTypeID()) {
  case Type::StructTyID:
    NumAggElts = AggTy->getStructNumElements();
    break;
  case Type::ArrayTyID:
    NumAggElts = AggTy->getArrayNumElements();
    break;
  default:
    llvm_unreachable("Unhandled aggregate type?");
  }

  // Arbitrary aggregate size cut-off. Motivation for limit of 2 is to be able
  // to handle clang C++ exception struct (which is hardcoded as {i8*, i32}),
  // FIXME: any interesting patterns to be caught with larger limit?
  assert(NumAggElts > 0 && "Aggregate should have elements.");
  if (NumAggElts > 2)
    return nullptr;

  static constexpr auto NotFound = std::nullopt;
  static constexpr auto FoundMismatch = nullptr;

  // Try to find a value of each element of an aggregate.
  // FIXME: deal with more complex, not one-dimensional, aggregate types
  SmallVector<std::optional<Instruction *>, 2> AggElts(NumAggElts, NotFound);

  // Do we know values for each element of the aggregate?
  auto KnowAllElts = [&AggElts]() {
    return !llvm::is_contained(AggElts, NotFound);
  };

  int Depth = 0;

  // Arbitrary `insertvalue` visitation depth limit. Let's be okay with
  // every element being overwritten twice, which should never happen.
  static const int DepthLimit = 2 * NumAggElts;

  // Recurse up the chain of `insertvalue` aggregate operands until either we've
  // reconstructed full initializer or can't visit any more `insertvalue`'s.
  for (InsertValueInst *CurrIVI = &OrigIVI;
       Depth < DepthLimit && CurrIVI && !KnowAllElts();
       CurrIVI = dyn_cast<InsertValueInst>(CurrIVI->getAggregateOperand()),
                       ++Depth) {
    auto *InsertedValue =
        dyn_cast<Instruction>(CurrIVI->getInsertedValueOperand());
    if (!InsertedValue)
      return nullptr; // Inserted value must be produced by an instruction.

    ArrayRef<unsigned int> Indices = CurrIVI->getIndices();

    // Don't bother with more than single-level aggregates.
    if (Indices.size() != 1)
      return nullptr; // FIXME: deal with more complex aggregates?

    // Now, we may have already previously recorded the value for this element
    // of an aggregate. If we did, that means the CurrIVI will later be
    // overwritten with the already-recorded value. But if not, let's record it!
    std::optional<Instruction *> &Elt = AggElts[Indices.front()];
    Elt = Elt.value_or(InsertedValue);

    // FIXME: should we handle chain-terminating undef base operand?
  }

  // Was that sufficient to deduce the full initializer for the aggregate?
  if (!KnowAllElts())
    return nullptr; // Give up then.

  // We now want to find the source[s] of the aggregate elements we've found.
  // And with "source" we mean the original aggregate[s] from which
  // the inserted elements were extracted. This may require PHI translation.

  enum class AggregateDescription {
    /// When analyzing the value that was inserted into an aggregate, we did
    /// not manage to find defining `extractvalue` instruction to analyze.
    NotFound,
    /// When analyzing the value that was inserted into an aggregate, we did
    /// manage to find defining `extractvalue` instruction[s], and everything
    /// matched perfectly - aggregate type, element insertion/extraction index.
    Found,
    /// When analyzing the value that was inserted into an aggregate, we did
    /// manage to find defining `extractvalue` instruction, but there was
    /// a mismatch: either the source type from which the extraction was didn't
    /// match the aggregate type into which the insertion was,
    /// or the extraction/insertion channels mismatched,
    /// or different elements had different source aggregates.
    FoundMismatch
  };
  auto Describe = [](std::optional<Value *> SourceAggregate) {
    if (SourceAggregate == NotFound)
      return AggregateDescription::NotFound;
    if (*SourceAggregate == FoundMismatch)
      return AggregateDescription::FoundMismatch;
    return AggregateDescription::Found;
  };

  // Given the value \p Elt that was being inserted into element \p EltIdx of an
  // aggregate AggTy, see if \p Elt was originally defined by an
  // appropriate extractvalue (same element index, same aggregate type).
  // If found, return the source aggregate from which the extraction was.
  // If \p PredBB is provided, does PHI translation of an \p Elt first.
  auto FindSourceAggregate =
      [&](Instruction *Elt, unsigned EltIdx, std::optional<BasicBlock *> UseBB,
          std::optional<BasicBlock *> PredBB) -> std::optional<Value *> {
    // For now(?), only deal with, at most, a single level of PHI indirection.
    if (UseBB && PredBB)
      Elt = dyn_cast<Instruction>(Elt->DoPHITranslation(*UseBB, *PredBB));
    // FIXME: deal with multiple levels of PHI indirection?

    // Did we find an extraction?
    auto *EVI = dyn_cast_or_null<ExtractValueInst>(Elt);
    if (!EVI)
      return NotFound;

    Value *SourceAggregate = EVI->getAggregateOperand();

    // Is the extraction from the same type into which the insertion was?
    if (SourceAggregate->getType() != AggTy)
      return FoundMismatch;
    // And the element index doesn't change between extraction and insertion?
    if (EVI->getNumIndices() != 1 || EltIdx != EVI->getIndices().front())
      return FoundMismatch;

    return SourceAggregate; // AggregateDescription::Found
  };

  // Given elements AggElts that were constructing an aggregate OrigIVI,
  // see if we can find appropriate source aggregate for each of the elements,
  // and see it's the same aggregate for each element. If so, return it.
  auto FindCommonSourceAggregate =
      [&](std::optional<BasicBlock *> UseBB,
          std::optional<BasicBlock *> PredBB) -> std::optional<Value *> {
    std::optional<Value *> SourceAggregate;

    for (auto I : enumerate(AggElts)) {
      assert(Describe(SourceAggregate) != AggregateDescription::FoundMismatch &&
             "We don't store nullptr in SourceAggregate!");
      assert((Describe(SourceAggregate) == AggregateDescription::Found) ==
                 (I.index() != 0) &&
             "SourceAggregate should be valid after the first element,");

      // For this element, is there a plausible source aggregate?
      // FIXME: we could special-case undef element, IFF we know that in the
      //        source aggregate said element isn't poison.
      std::optional<Value *> SourceAggregateForElement =
          FindSourceAggregate(*I.value(), I.index(), UseBB, PredBB);

      // Okay, what have we found? Does that correlate with previous findings?

      // Regardless of whether or not we have previously found source
      // aggregate for previous elements (if any), if we didn't find one for
      // this element, passthrough whatever we have just found.
      if (Describe(SourceAggregateForElement) != AggregateDescription::Found)
        return SourceAggregateForElement;

      // Okay, we have found source aggregate for this element.
      // Let's see what we already know from previous elements, if any.
      switch (Describe(SourceAggregate)) {
      case AggregateDescription::NotFound:
        // This is apparently the first element that we have examined.
        SourceAggregate = SourceAggregateForElement; // Record the aggregate!
        continue; // Great, now look at next element.
      case AggregateDescription::Found:
        // We have previously already successfully examined other elements.
        // Is this the same source aggregate we've found for other elements?
        if (*SourceAggregateForElement != *SourceAggregate)
          return FoundMismatch;
        continue; // Still the same aggregate, look at next element.
      case AggregateDescription::FoundMismatch:
        llvm_unreachable("Can't happen. We would have early-exited then.");
      };
    }

    assert(Describe(SourceAggregate) == AggregateDescription::Found &&
           "Must be a valid Value");
    return *SourceAggregate;
  };

  std::optional<Value *> SourceAggregate;

  // Can we find the source aggregate without looking at predecessors?
  SourceAggregate = FindCommonSourceAggregate(/*UseBB=*/std::nullopt,
                                              /*PredBB=*/std::nullopt);
  if (Describe(SourceAggregate) != AggregateDescription::NotFound) {
    if (Describe(SourceAggregate) == AggregateDescription::FoundMismatch)
      return nullptr; // Conflicting source aggregates!
    ++NumAggregateReconstructionsSimplified;
    return replaceInstUsesWith(OrigIVI, *SourceAggregate);
  }

  // Okay, apparently we need to look at predecessors.

  // We should be smart about picking the "use" basic block, which will be the
  // merge point for aggregate, where we'll insert the final PHI that will be
  // used instead of OrigIVI. Basic block of OrigIVI is *not* the right choice.
  // We should look in which blocks each of the AggElts is being defined,
  // they all should be defined in the same basic block.
  BasicBlock *UseBB = nullptr;

  for (const std::optional<Instruction *> &I : AggElts) {
    BasicBlock *BB = (*I)->getParent();
    // If it's the first instruction we've encountered, record the basic block.
    if (!UseBB) {
      UseBB = BB;
      continue;
    }
    // Otherwise, this must be the same basic block we've seen previously.
    if (UseBB != BB)
      return nullptr;
  }

  // If *all* of the elements are basic-block-independent, meaning they are
  // either function arguments, or constant expressions, then if we didn't
  // handle them without predecessor-aware handling, we won't handle them now.
  if (!UseBB)
    return nullptr;

  // If we didn't manage to find source aggregate without looking at
  // predecessors, and there are no predecessors to look at, then we're done.
  if (pred_empty(UseBB))
    return nullptr;

  // Arbitrary predecessor count limit.
  static const int PredCountLimit = 64;

  // Cache the (non-uniqified!) list of predecessors in a vector,
  // checking the limit at the same time for efficiency.
  SmallVector<BasicBlock *, 4> Preds; // May have duplicates!
  for (BasicBlock *Pred : predecessors(UseBB)) {
    // Don't bother if there are too many predecessors.
    if (Preds.size() >= PredCountLimit) // FIXME: only count duplicates once?
      return nullptr;
    Preds.emplace_back(Pred);
  }

  // For each predecessor, what is the source aggregate,
  // from which all the elements were originally extracted from?
  // Note that we want for the map to have stable iteration order!
  SmallDenseMap<BasicBlock *, Value *, 4> SourceAggregates;
  for (BasicBlock *Pred : Preds) {
    std::pair<decltype(SourceAggregates)::iterator, bool> IV =
        SourceAggregates.insert({Pred, nullptr});
    // Did we already evaluate this predecessor?
    if (!IV.second)
      continue;

    // Let's hope that when coming from predecessor Pred, all elements of the
    // aggregate produced by OrigIVI must have been originally extracted from
    // the same aggregate. Is that so? Can we find said original aggregate?
    SourceAggregate = FindCommonSourceAggregate(UseBB, Pred);
    if (Describe(SourceAggregate) != AggregateDescription::Found)
      return nullptr; // Give up.
    IV.first->second = *SourceAggregate;
  }

  // All good! Now we just need to thread the source aggregates here.
  // Note that we have to insert the new PHI here, ourselves, because we can't
  // rely on InstCombinerImpl::run() inserting it into the right basic block.
  // Note that the same block can be a predecessor more than once,
  // and we need to preserve that invariant for the PHI node.
  BuilderTy::InsertPointGuard Guard(Builder);
  Builder.SetInsertPoint(UseBB, UseBB->getFirstNonPHIIt());
  auto *PHI =
      Builder.CreatePHI(AggTy, Preds.size(), OrigIVI.getName() + ".merged");
  for (BasicBlock *Pred : Preds)
    PHI->addIncoming(SourceAggregates[Pred], Pred);

  ++NumAggregateReconstructionsSimplified;
  return replaceInstUsesWith(OrigIVI, PHI);
}

/// Try to find redundant insertvalue instructions, like the following ones:
///  %0 = insertvalue { i8, i32 } undef, i8 %x, 0
///  %1 = insertvalue { i8, i32 } %0,    i8 %y, 0
/// Here the second instruction inserts values at the same indices, as the
/// first one, making the first one redundant.
/// It should be transformed to:
///  %0 = insertvalue { i8, i32 } undef, i8 %y, 0
Instruction *InstCombinerImpl::visitInsertValueInst(InsertValueInst &I) {
  if (Value *V = simplifyInsertValueInst(
          I.getAggregateOperand(), I.getInsertedValueOperand(), I.getIndices(),
          SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  bool IsRedundant = false;
  ArrayRef<unsigned int> FirstIndices = I.getIndices();

  // If there is a chain of insertvalue instructions (each of them except the
  // last one has only one use and it's another insertvalue insn from this
  // chain), check if any of the 'children' uses the same indices as the first
  // instruction. In this case, the first one is redundant.
  Value *V = &I;
  unsigned Depth = 0;
  while (V->hasOneUse() && Depth < 10) {
    User *U = V->user_back();
    auto UserInsInst = dyn_cast<InsertValueInst>(U);
    if (!UserInsInst || U->getOperand(0) != V)
      break;
    if (UserInsInst->getIndices() == FirstIndices) {
      IsRedundant = true;
      break;
    }
    V = UserInsInst;
    Depth++;
  }

  if (IsRedundant)
    return replaceInstUsesWith(I, I.getOperand(0));

  if (Instruction *NewI = foldAggregateConstructionIntoAggregateReuse(I))
    return NewI;

  return nullptr;
}

static bool isShuffleEquivalentToSelect(ShuffleVectorInst &Shuf) {
  // Can not analyze scalable type, the number of elements is not a compile-time
  // constant.
  if (isa<ScalableVectorType>(Shuf.getOperand(0)->getType()))
    return false;

  int MaskSize = Shuf.getShuffleMask().size();
  int VecSize =
      cast<FixedVectorType>(Shuf.getOperand(0)->getType())->getNumElements();

  // A vector select does not change the size of the operands.
  if (MaskSize != VecSize)
    return false;

  // Each mask element must be undefined or choose a vector element from one of
  // the source operands without crossing vector lanes.
  for (int i = 0; i != MaskSize; ++i) {
    int Elt = Shuf.getMaskValue(i);
    if (Elt != -1 && Elt != i && Elt != i + VecSize)
      return false;
  }

  return true;
}

/// Turn a chain of inserts that splats a value into an insert + shuffle:
/// insertelt(insertelt(insertelt(insertelt X, %k, 0), %k, 1), %k, 2) ... ->
/// shufflevector(insertelt(X, %k, 0), poison, zero)
static Instruction *foldInsSequenceIntoSplat(InsertElementInst &InsElt) {
  // We are interested in the last insert in a chain. So if this insert has a
  // single user and that user is an insert, bail.
  if (InsElt.hasOneUse() && isa<InsertElementInst>(InsElt.user_back()))
    return nullptr;

  VectorType *VecTy = InsElt.getType();
  // Can not handle scalable type, the number of elements is not a compile-time
  // constant.
  if (isa<ScalableVectorType>(VecTy))
    return nullptr;
  unsigned NumElements = cast<FixedVectorType>(VecTy)->getNumElements();

  // Do not try to do this for a one-element vector, since that's a nop,
  // and will cause an inf-loop.
  if (NumElements == 1)
    return nullptr;

  Value *SplatVal = InsElt.getOperand(1);
  InsertElementInst *CurrIE = &InsElt;
  SmallBitVector ElementPresent(NumElements, false);
  InsertElementInst *FirstIE = nullptr;

  // Walk the chain backwards, keeping track of which indices we inserted into,
  // until we hit something that isn't an insert of the splatted value.
  while (CurrIE) {
    auto *Idx = dyn_cast<ConstantInt>(CurrIE->getOperand(2));
    if (!Idx || CurrIE->getOperand(1) != SplatVal)
      return nullptr;

    auto *NextIE = dyn_cast<InsertElementInst>(CurrIE->getOperand(0));
    // Check none of the intermediate steps have any additional uses, except
    // for the root insertelement instruction, which can be re-used, if it
    // inserts at position 0.
    if (CurrIE != &InsElt &&
        (!CurrIE->hasOneUse() && (NextIE != nullptr || !Idx->isZero())))
      return nullptr;

    ElementPresent[Idx->getZExtValue()] = true;
    FirstIE = CurrIE;
    CurrIE = NextIE;
  }

  // If this is just a single insertelement (not a sequence), we are done.
  if (FirstIE == &InsElt)
    return nullptr;

  // If we are not inserting into a poison vector, make sure we've seen an
  // insert into every element.
  // TODO: If the base vector is not undef, it might be better to create a splat
  //       and then a select-shuffle (blend) with the base vector.
  if (!match(FirstIE->getOperand(0), m_Poison()))
    if (!ElementPresent.all())
      return nullptr;

  // Create the insert + shuffle.
  Type *Int64Ty = Type::getInt64Ty(InsElt.getContext());
  PoisonValue *PoisonVec = PoisonValue::get(VecTy);
  Constant *Zero = ConstantInt::get(Int64Ty, 0);
  if (!cast<ConstantInt>(FirstIE->getOperand(2))->isZero())
    FirstIE = InsertElementInst::Create(PoisonVec, SplatVal, Zero, "",
                                        InsElt.getIterator());

  // Splat from element 0, but replace absent elements with poison in the mask.
  SmallVector<int, 16> Mask(NumElements, 0);
  for (unsigned i = 0; i != NumElements; ++i)
    if (!ElementPresent[i])
      Mask[i] = -1;

  return new ShuffleVectorInst(FirstIE, Mask);
}

/// Try to fold an insert element into an existing splat shuffle by changing
/// the shuffle's mask to include the index of this insert element.
static Instruction *foldInsEltIntoSplat(InsertElementInst &InsElt) {
  // Check if the vector operand of this insert is a canonical splat shuffle.
  auto *Shuf = dyn_cast<ShuffleVectorInst>(InsElt.getOperand(0));
  if (!Shuf || !Shuf->isZeroEltSplat())
    return nullptr;

  // Bail out early if shuffle is scalable type. The number of elements in
  // shuffle mask is unknown at compile-time.
  if (isa<ScalableVectorType>(Shuf->getType()))
    return nullptr;

  // Check for a constant insertion index.
  uint64_t IdxC;
  if (!match(InsElt.getOperand(2), m_ConstantInt(IdxC)))
    return nullptr;

  // Check if the splat shuffle's input is the same as this insert's scalar op.
  Value *X = InsElt.getOperand(1);
  Value *Op0 = Shuf->getOperand(0);
  if (!match(Op0, m_InsertElt(m_Undef(), m_Specific(X), m_ZeroInt())))
    return nullptr;

  // Replace the shuffle mask element at the index of this insert with a zero.
  // For example:
  // inselt (shuf (inselt undef, X, 0), _, <0,undef,0,undef>), X, 1
  //   --> shuf (inselt undef, X, 0), poison, <0,0,0,undef>
  unsigned NumMaskElts =
      cast<FixedVectorType>(Shuf->getType())->getNumElements();
  SmallVector<int, 16> NewMask(NumMaskElts);
  for (unsigned i = 0; i != NumMaskElts; ++i)
    NewMask[i] = i == IdxC ? 0 : Shuf->getMaskValue(i);

  return new ShuffleVectorInst(Op0, NewMask);
}

/// Try to fold an extract+insert element into an existing identity shuffle by
/// changing the shuffle's mask to include the index of this insert element.
static Instruction *foldInsEltIntoIdentityShuffle(InsertElementInst &InsElt) {
  // Check if the vector operand of this insert is an identity shuffle.
  auto *Shuf = dyn_cast<ShuffleVectorInst>(InsElt.getOperand(0));
  if (!Shuf || !match(Shuf->getOperand(1), m_Poison()) ||
      !(Shuf->isIdentityWithExtract() || Shuf->isIdentityWithPadding()))
    return nullptr;

  // Bail out early if shuffle is scalable type. The number of elements in
  // shuffle mask is unknown at compile-time.
  if (isa<ScalableVectorType>(Shuf->getType()))
    return nullptr;

  // Check for a constant insertion index.
  uint64_t IdxC;
  if (!match(InsElt.getOperand(2), m_ConstantInt(IdxC)))
    return nullptr;

  // Check if this insert's scalar op is extracted from the identity shuffle's
  // input vector.
  Value *Scalar = InsElt.getOperand(1);
  Value *X = Shuf->getOperand(0);
  if (!match(Scalar, m_ExtractElt(m_Specific(X), m_SpecificInt(IdxC))))
    return nullptr;

  // Replace the shuffle mask element at the index of this extract+insert with
  // that same index value.
  // For example:
  // inselt (shuf X, IdMask), (extelt X, IdxC), IdxC --> shuf X, IdMask'
  unsigned NumMaskElts =
      cast<FixedVectorType>(Shuf->getType())->getNumElements();
  SmallVector<int, 16> NewMask(NumMaskElts);
  ArrayRef<int> OldMask = Shuf->getShuffleMask();
  for (unsigned i = 0; i != NumMaskElts; ++i) {
    if (i != IdxC) {
      // All mask elements besides the inserted element remain the same.
      NewMask[i] = OldMask[i];
    } else if (OldMask[i] == (int)IdxC) {
      // If the mask element was already set, there's nothing to do
      // (demanded elements analysis may unset it later).
      return nullptr;
    } else {
      assert(OldMask[i] == PoisonMaskElem &&
             "Unexpected shuffle mask element for identity shuffle");
      NewMask[i] = IdxC;
    }
  }

  return new ShuffleVectorInst(X, Shuf->getOperand(1), NewMask);
}

/// If we have an insertelement instruction feeding into another insertelement
/// and the 2nd is inserting a constant into the vector, canonicalize that
/// constant insertion before the insertion of a variable:
///
/// insertelement (insertelement X, Y, IdxC1), ScalarC, IdxC2 -->
/// insertelement (insertelement X, ScalarC, IdxC2), Y, IdxC1
///
/// This has the potential of eliminating the 2nd insertelement instruction
/// via constant folding of the scalar constant into a vector constant.
static Instruction *hoistInsEltConst(InsertElementInst &InsElt2,
                                     InstCombiner::BuilderTy &Builder) {
  auto *InsElt1 = dyn_cast<InsertElementInst>(InsElt2.getOperand(0));
  if (!InsElt1 || !InsElt1->hasOneUse())
    return nullptr;

  Value *X, *Y;
  Constant *ScalarC;
  ConstantInt *IdxC1, *IdxC2;
  if (match(InsElt1->getOperand(0), m_Value(X)) &&
      match(InsElt1->getOperand(1), m_Value(Y)) && !isa<Constant>(Y) &&
      match(InsElt1->getOperand(2), m_ConstantInt(IdxC1)) &&
      match(InsElt2.getOperand(1), m_Constant(ScalarC)) &&
      match(InsElt2.getOperand(2), m_ConstantInt(IdxC2)) && IdxC1 != IdxC2) {
    Value *NewInsElt1 = Builder.CreateInsertElement(X, ScalarC, IdxC2);
    return InsertElementInst::Create(NewInsElt1, Y, IdxC1);
  }

  return nullptr;
}

/// insertelt (shufflevector X, CVec, Mask|insertelt X, C1, CIndex1), C, CIndex
/// --> shufflevector X, CVec', Mask'
static Instruction *foldConstantInsEltIntoShuffle(InsertElementInst &InsElt) {
  auto *Inst = dyn_cast<Instruction>(InsElt.getOperand(0));
  // Bail out if the parent has more than one use. In that case, we'd be
  // replacing the insertelt with a shuffle, and that's not a clear win.
  if (!Inst || !Inst->hasOneUse())
    return nullptr;
  if (auto *Shuf = dyn_cast<ShuffleVectorInst>(InsElt.getOperand(0))) {
    // The shuffle must have a constant vector operand. The insertelt must have
    // a constant scalar being inserted at a constant position in the vector.
    Constant *ShufConstVec, *InsEltScalar;
    uint64_t InsEltIndex;
    if (!match(Shuf->getOperand(1), m_Constant(ShufConstVec)) ||
        !match(InsElt.getOperand(1), m_Constant(InsEltScalar)) ||
        !match(InsElt.getOperand(2), m_ConstantInt(InsEltIndex)))
      return nullptr;

    // Adding an element to an arbitrary shuffle could be expensive, but a
    // shuffle that selects elements from vectors without crossing lanes is
    // assumed cheap.
    // If we're just adding a constant into that shuffle, it will still be
    // cheap.
    if (!isShuffleEquivalentToSelect(*Shuf))
      return nullptr;

    // From the above 'select' check, we know that the mask has the same number
    // of elements as the vector input operands. We also know that each constant
    // input element is used in its lane and can not be used more than once by
    // the shuffle. Therefore, replace the constant in the shuffle's constant
    // vector with the insertelt constant. Replace the constant in the shuffle's
    // mask vector with the insertelt index plus the length of the vector
    // (because the constant vector operand of a shuffle is always the 2nd
    // operand).
    ArrayRef<int> Mask = Shuf->getShuffleMask();
    unsigned NumElts = Mask.size();
    SmallVector<Constant *, 16> NewShufElts(NumElts);
    SmallVector<int, 16> NewMaskElts(NumElts);
    for (unsigned I = 0; I != NumElts; ++I) {
      if (I == InsEltIndex) {
        NewShufElts[I] = InsEltScalar;
        NewMaskElts[I] = InsEltIndex + NumElts;
      } else {
        // Copy over the existing values.
        NewShufElts[I] = ShufConstVec->getAggregateElement(I);
        NewMaskElts[I] = Mask[I];
      }

      // Bail if we failed to find an element.
      if (!NewShufElts[I])
        return nullptr;
    }

    // Create new operands for a shuffle that includes the constant of the
    // original insertelt. The old shuffle will be dead now.
    return new ShuffleVectorInst(Shuf->getOperand(0),
                                 ConstantVector::get(NewShufElts), NewMaskElts);
  } else if (auto *IEI = dyn_cast<InsertElementInst>(Inst)) {
    // Transform sequences of insertelements ops with constant data/indexes into
    // a single shuffle op.
    // Can not handle scalable type, the number of elements needed to create
    // shuffle mask is not a compile-time constant.
    if (isa<ScalableVectorType>(InsElt.getType()))
      return nullptr;
    unsigned NumElts =
        cast<FixedVectorType>(InsElt.getType())->getNumElements();

    uint64_t InsertIdx[2];
    Constant *Val[2];
    if (!match(InsElt.getOperand(2), m_ConstantInt(InsertIdx[0])) ||
        !match(InsElt.getOperand(1), m_Constant(Val[0])) ||
        !match(IEI->getOperand(2), m_ConstantInt(InsertIdx[1])) ||
        !match(IEI->getOperand(1), m_Constant(Val[1])))
      return nullptr;
    SmallVector<Constant *, 16> Values(NumElts);
    SmallVector<int, 16> Mask(NumElts);
    auto ValI = std::begin(Val);
    // Generate new constant vector and mask.
    // We have 2 values/masks from the insertelements instructions. Insert them
    // into new value/mask vectors.
    for (uint64_t I : InsertIdx) {
      if (!Values[I]) {
        Values[I] = *ValI;
        Mask[I] = NumElts + I;
      }
      ++ValI;
    }
    // Remaining values are filled with 'poison' values.
    for (unsigned I = 0; I < NumElts; ++I) {
      if (!Values[I]) {
        Values[I] = PoisonValue::get(InsElt.getType()->getElementType());
        Mask[I] = I;
      }
    }
    // Create new operands for a shuffle that includes the constant of the
    // original insertelt.
    return new ShuffleVectorInst(IEI->getOperand(0),
                                 ConstantVector::get(Values), Mask);
  }
  return nullptr;
}

/// If both the base vector and the inserted element are extended from the same
/// type, do the insert element in the narrow source type followed by extend.
/// TODO: This can be extended to include other cast opcodes, but particularly
///       if we create a wider insertelement, make sure codegen is not harmed.
static Instruction *narrowInsElt(InsertElementInst &InsElt,
                                 InstCombiner::BuilderTy &Builder) {
  // We are creating a vector extend. If the original vector extend has another
  // use, that would mean we end up with 2 vector extends, so avoid that.
  // TODO: We could ease the use-clause to "if at least one op has one use"
  //       (assuming that the source types match - see next TODO comment).
  Value *Vec = InsElt.getOperand(0);
  if (!Vec->hasOneUse())
    return nullptr;

  Value *Scalar = InsElt.getOperand(1);
  Value *X, *Y;
  CastInst::CastOps CastOpcode;
  if (match(Vec, m_FPExt(m_Value(X))) && match(Scalar, m_FPExt(m_Value(Y))))
    CastOpcode = Instruction::FPExt;
  else if (match(Vec, m_SExt(m_Value(X))) && match(Scalar, m_SExt(m_Value(Y))))
    CastOpcode = Instruction::SExt;
  else if (match(Vec, m_ZExt(m_Value(X))) && match(Scalar, m_ZExt(m_Value(Y))))
    CastOpcode = Instruction::ZExt;
  else
    return nullptr;

  // TODO: We can allow mismatched types by creating an intermediate cast.
  if (X->getType()->getScalarType() != Y->getType())
    return nullptr;

  // inselt (ext X), (ext Y), Index --> ext (inselt X, Y, Index)
  Value *NewInsElt = Builder.CreateInsertElement(X, Y, InsElt.getOperand(2));
  return CastInst::Create(CastOpcode, NewInsElt, InsElt.getType());
}

/// If we are inserting 2 halves of a value into adjacent elements of a vector,
/// try to convert to a single insert with appropriate bitcasts.
static Instruction *foldTruncInsEltPair(InsertElementInst &InsElt,
                                        bool IsBigEndian,
                                        InstCombiner::BuilderTy &Builder) {
  Value *VecOp    = InsElt.getOperand(0);
  Value *ScalarOp = InsElt.getOperand(1);
  Value *IndexOp  = InsElt.getOperand(2);

  // Pattern depends on endian because we expect lower index is inserted first.
  // Big endian:
  // inselt (inselt BaseVec, (trunc (lshr X, BW/2), Index0), (trunc X), Index1
  // Little endian:
  // inselt (inselt BaseVec, (trunc X), Index0), (trunc (lshr X, BW/2)), Index1
  // Note: It is not safe to do this transform with an arbitrary base vector
  //       because the bitcast of that vector to fewer/larger elements could
  //       allow poison to spill into an element that was not poison before.
  // TODO: Detect smaller fractions of the scalar.
  // TODO: One-use checks are conservative.
  auto *VTy = dyn_cast<FixedVectorType>(InsElt.getType());
  Value *Scalar0, *BaseVec;
  uint64_t Index0, Index1;
  if (!VTy || (VTy->getNumElements() & 1) ||
      !match(IndexOp, m_ConstantInt(Index1)) ||
      !match(VecOp, m_InsertElt(m_Value(BaseVec), m_Value(Scalar0),
                                m_ConstantInt(Index0))) ||
      !match(BaseVec, m_Undef()))
    return nullptr;

  // The first insert must be to the index one less than this one, and
  // the first insert must be to an even index.
  if (Index0 + 1 != Index1 || Index0 & 1)
    return nullptr;

  // For big endian, the high half of the value should be inserted first.
  // For little endian, the low half of the value should be inserted first.
  Value *X;
  uint64_t ShAmt;
  if (IsBigEndian) {
    if (!match(ScalarOp, m_Trunc(m_Value(X))) ||
        !match(Scalar0, m_Trunc(m_LShr(m_Specific(X), m_ConstantInt(ShAmt)))))
      return nullptr;
  } else {
    if (!match(Scalar0, m_Trunc(m_Value(X))) ||
        !match(ScalarOp, m_Trunc(m_LShr(m_Specific(X), m_ConstantInt(ShAmt)))))
      return nullptr;
  }

  Type *SrcTy = X->getType();
  unsigned ScalarWidth = SrcTy->getScalarSizeInBits();
  unsigned VecEltWidth = VTy->getScalarSizeInBits();
  if (ScalarWidth != VecEltWidth * 2 || ShAmt != VecEltWidth)
    return nullptr;

  // Bitcast the base vector to a vector type with the source element type.
  Type *CastTy = FixedVectorType::get(SrcTy, VTy->getNumElements() / 2);
  Value *CastBaseVec = Builder.CreateBitCast(BaseVec, CastTy);

  // Scale the insert index for a vector with half as many elements.
  // bitcast (inselt (bitcast BaseVec), X, NewIndex)
  uint64_t NewIndex = IsBigEndian ? Index1 / 2 : Index0 / 2;
  Value *NewInsert = Builder.CreateInsertElement(CastBaseVec, X, NewIndex);
  return new BitCastInst(NewInsert, VTy);
}

Instruction *InstCombinerImpl::visitInsertElementInst(InsertElementInst &IE) {
  Value *VecOp    = IE.getOperand(0);
  Value *ScalarOp = IE.getOperand(1);
  Value *IdxOp    = IE.getOperand(2);

  if (auto *V = simplifyInsertElementInst(
          VecOp, ScalarOp, IdxOp, SQ.getWithInstruction(&IE)))
    return replaceInstUsesWith(IE, V);

  // Canonicalize type of constant indices to i64 to simplify CSE
  if (auto *IndexC = dyn_cast<ConstantInt>(IdxOp)) {
    if (auto *NewIdx = getPreferredVectorIndex(IndexC))
      return replaceOperand(IE, 2, NewIdx);

    Value *BaseVec, *OtherScalar;
    uint64_t OtherIndexVal;
    if (match(VecOp, m_OneUse(m_InsertElt(m_Value(BaseVec),
                                          m_Value(OtherScalar),
                                          m_ConstantInt(OtherIndexVal)))) &&
        !isa<Constant>(OtherScalar) && OtherIndexVal > IndexC->getZExtValue()) {
      Value *NewIns = Builder.CreateInsertElement(BaseVec, ScalarOp, IdxOp);
      return InsertElementInst::Create(NewIns, OtherScalar,
                                       Builder.getInt64(OtherIndexVal));
    }
  }

  // If the scalar is bitcast and inserted into undef, do the insert in the
  // source type followed by bitcast.
  // TODO: Generalize for insert into any constant, not just undef?
  Value *ScalarSrc;
  if (match(VecOp, m_Undef()) &&
      match(ScalarOp, m_OneUse(m_BitCast(m_Value(ScalarSrc)))) &&
      (ScalarSrc->getType()->isIntegerTy() ||
       ScalarSrc->getType()->isFloatingPointTy())) {
    // inselt undef, (bitcast ScalarSrc), IdxOp -->
    //   bitcast (inselt undef, ScalarSrc, IdxOp)
    Type *ScalarTy = ScalarSrc->getType();
    Type *VecTy = VectorType::get(ScalarTy, IE.getType()->getElementCount());
    Constant *NewUndef = isa<PoisonValue>(VecOp) ? PoisonValue::get(VecTy)
                                                 : UndefValue::get(VecTy);
    Value *NewInsElt = Builder.CreateInsertElement(NewUndef, ScalarSrc, IdxOp);
    return new BitCastInst(NewInsElt, IE.getType());
  }

  // If the vector and scalar are both bitcast from the same element type, do
  // the insert in that source type followed by bitcast.
  Value *VecSrc;
  if (match(VecOp, m_BitCast(m_Value(VecSrc))) &&
      match(ScalarOp, m_BitCast(m_Value(ScalarSrc))) &&
      (VecOp->hasOneUse() || ScalarOp->hasOneUse()) &&
      VecSrc->getType()->isVectorTy() && !ScalarSrc->getType()->isVectorTy() &&
      cast<VectorType>(VecSrc->getType())->getElementType() ==
          ScalarSrc->getType()) {
    // inselt (bitcast VecSrc), (bitcast ScalarSrc), IdxOp -->
    //   bitcast (inselt VecSrc, ScalarSrc, IdxOp)
    Value *NewInsElt = Builder.CreateInsertElement(VecSrc, ScalarSrc, IdxOp);
    return new BitCastInst(NewInsElt, IE.getType());
  }

  // If the inserted element was extracted from some other fixed-length vector
  // and both indexes are valid constants, try to turn this into a shuffle.
  // Can not handle scalable vector type, the number of elements needed to
  // create shuffle mask is not a compile-time constant.
  uint64_t InsertedIdx, ExtractedIdx;
  Value *ExtVecOp;
  if (isa<FixedVectorType>(IE.getType()) &&
      match(IdxOp, m_ConstantInt(InsertedIdx)) &&
      match(ScalarOp,
            m_ExtractElt(m_Value(ExtVecOp), m_ConstantInt(ExtractedIdx))) &&
      isa<FixedVectorType>(ExtVecOp->getType()) &&
      ExtractedIdx <
          cast<FixedVectorType>(ExtVecOp->getType())->getNumElements()) {
    // TODO: Looking at the user(s) to determine if this insert is a
    // fold-to-shuffle opportunity does not match the usual instcombine
    // constraints. We should decide if the transform is worthy based only
    // on this instruction and its operands, but that may not work currently.
    //
    // Here, we are trying to avoid creating shuffles before reaching
    // the end of a chain of extract-insert pairs. This is complicated because
    // we do not generally form arbitrary shuffle masks in instcombine
    // (because those may codegen poorly), but collectShuffleElements() does
    // exactly that.
    //
    // The rules for determining what is an acceptable target-independent
    // shuffle mask are fuzzy because they evolve based on the backend's
    // capabilities and real-world impact.
    auto isShuffleRootCandidate = [](InsertElementInst &Insert) {
      if (!Insert.hasOneUse())
        return true;
      auto *InsertUser = dyn_cast<InsertElementInst>(Insert.user_back());
      if (!InsertUser)
        return true;
      return false;
    };

    // Try to form a shuffle from a chain of extract-insert ops.
    if (isShuffleRootCandidate(IE)) {
      bool Rerun = true;
      while (Rerun) {
        Rerun = false;

        SmallVector<int, 16> Mask;
        ShuffleOps LR =
            collectShuffleElements(&IE, Mask, nullptr, *this, Rerun);

        // The proposed shuffle may be trivial, in which case we shouldn't
        // perform the combine.
        if (LR.first != &IE && LR.second != &IE) {
          // We now have a shuffle of LHS, RHS, Mask.
          if (LR.second == nullptr)
            LR.second = PoisonValue::get(LR.first->getType());
          return new ShuffleVectorInst(LR.first, LR.second, Mask);
        }
      }
    }
  }

  if (auto VecTy = dyn_cast<FixedVectorType>(VecOp->getType())) {
    unsigned VWidth = VecTy->getNumElements();
    APInt PoisonElts(VWidth, 0);
    APInt AllOnesEltMask(APInt::getAllOnes(VWidth));
    if (Value *V = SimplifyDemandedVectorElts(&IE, AllOnesEltMask,
                                              PoisonElts)) {
      if (V != &IE)
        return replaceInstUsesWith(IE, V);
      return &IE;
    }
  }

  if (Instruction *Shuf = foldConstantInsEltIntoShuffle(IE))
    return Shuf;

  if (Instruction *NewInsElt = hoistInsEltConst(IE, Builder))
    return NewInsElt;

  if (Instruction *Broadcast = foldInsSequenceIntoSplat(IE))
    return Broadcast;

  if (Instruction *Splat = foldInsEltIntoSplat(IE))
    return Splat;

  if (Instruction *IdentityShuf = foldInsEltIntoIdentityShuffle(IE))
    return IdentityShuf;

  if (Instruction *Ext = narrowInsElt(IE, Builder))
    return Ext;

  if (Instruction *Ext = foldTruncInsEltPair(IE, DL.isBigEndian(), Builder))
    return Ext;

  return nullptr;
}

/// Return true if we can evaluate the specified expression tree if the vector
/// elements were shuffled in a different order.
static bool canEvaluateShuffled(Value *V, ArrayRef<int> Mask,
                                unsigned Depth = 5) {
  // We can always reorder the elements of a constant.
  if (isa<Constant>(V))
    return true;

  // We won't reorder vector arguments. No IPO here.
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return false;

  // Two users may expect different orders of the elements. Don't try it.
  if (!I->hasOneUse())
    return false;

  if (Depth == 0) return false;

  switch (I->getOpcode()) {
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
      // Propagating an undefined shuffle mask element to integer div/rem is not
      // allowed because those opcodes can create immediate undefined behavior
      // from an undefined element in an operand.
      if (llvm::is_contained(Mask, -1))
        return false;
      [[fallthrough]];
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::GetElementPtr: {
      // Bail out if we would create longer vector ops. We could allow creating
      // longer vector ops, but that may result in more expensive codegen.
      Type *ITy = I->getType();
      if (ITy->isVectorTy() &&
          Mask.size() > cast<FixedVectorType>(ITy)->getNumElements())
        return false;
      for (Value *Operand : I->operands()) {
        if (!canEvaluateShuffled(Operand, Mask, Depth - 1))
          return false;
      }
      return true;
    }
    case Instruction::InsertElement: {
      ConstantInt *CI = dyn_cast<ConstantInt>(I->getOperand(2));
      if (!CI) return false;
      int ElementNumber = CI->getLimitedValue();

      // Verify that 'CI' does not occur twice in Mask. A single 'insertelement'
      // can't put an element into multiple indices.
      bool SeenOnce = false;
      for (int I : Mask) {
        if (I == ElementNumber) {
          if (SeenOnce)
            return false;
          SeenOnce = true;
        }
      }
      return canEvaluateShuffled(I->getOperand(0), Mask, Depth - 1);
    }
  }
  return false;
}

/// Rebuild a new instruction just like 'I' but with the new operands given.
/// In the event of type mismatch, the type of the operands is correct.
static Value *buildNew(Instruction *I, ArrayRef<Value*> NewOps,
                       IRBuilderBase &Builder) {
  Builder.SetInsertPoint(I);
  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
      BinaryOperator *BO = cast<BinaryOperator>(I);
      assert(NewOps.size() == 2 && "binary operator with #ops != 2");
      Value *New = Builder.CreateBinOp(cast<BinaryOperator>(I)->getOpcode(),
                                       NewOps[0], NewOps[1]);
      if (auto *NewI = dyn_cast<Instruction>(New)) {
        if (isa<OverflowingBinaryOperator>(BO)) {
          NewI->setHasNoUnsignedWrap(BO->hasNoUnsignedWrap());
          NewI->setHasNoSignedWrap(BO->hasNoSignedWrap());
        }
        if (isa<PossiblyExactOperator>(BO)) {
          NewI->setIsExact(BO->isExact());
        }
        if (isa<FPMathOperator>(BO))
          NewI->copyFastMathFlags(I);
      }
      return New;
    }
    case Instruction::ICmp:
      assert(NewOps.size() == 2 && "icmp with #ops != 2");
      return Builder.CreateICmp(cast<ICmpInst>(I)->getPredicate(), NewOps[0],
                                NewOps[1]);
    case Instruction::FCmp:
      assert(NewOps.size() == 2 && "fcmp with #ops != 2");
      return Builder.CreateFCmp(cast<FCmpInst>(I)->getPredicate(), NewOps[0],
                                NewOps[1]);
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt: {
      // It's possible that the mask has a different number of elements from
      // the original cast. We recompute the destination type to match the mask.
      Type *DestTy = VectorType::get(
          I->getType()->getScalarType(),
          cast<VectorType>(NewOps[0]->getType())->getElementCount());
      assert(NewOps.size() == 1 && "cast with #ops != 1");
      return Builder.CreateCast(cast<CastInst>(I)->getOpcode(), NewOps[0],
                                DestTy);
    }
    case Instruction::GetElementPtr: {
      Value *Ptr = NewOps[0];
      ArrayRef<Value*> Idx = NewOps.slice(1);
      return Builder.CreateGEP(cast<GEPOperator>(I)->getSourceElementType(),
                               Ptr, Idx, "",
                               cast<GEPOperator>(I)->isInBounds());
    }
  }
  llvm_unreachable("failed to rebuild vector instructions");
}

static Value *evaluateInDifferentElementOrder(Value *V, ArrayRef<int> Mask,
                                              IRBuilderBase &Builder) {
  // Mask.size() does not need to be equal to the number of vector elements.

  assert(V->getType()->isVectorTy() && "can't reorder non-vector elements");
  Type *EltTy = V->getType()->getScalarType();

  if (isa<PoisonValue>(V))
    return PoisonValue::get(FixedVectorType::get(EltTy, Mask.size()));

  if (match(V, m_Undef()))
    return UndefValue::get(FixedVectorType::get(EltTy, Mask.size()));

  if (isa<ConstantAggregateZero>(V))
    return ConstantAggregateZero::get(FixedVectorType::get(EltTy, Mask.size()));

  if (Constant *C = dyn_cast<Constant>(V))
    return ConstantExpr::getShuffleVector(C, PoisonValue::get(C->getType()),
                                          Mask);

  Instruction *I = cast<Instruction>(V);
  switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::Select:
    case Instruction::GetElementPtr: {
      SmallVector<Value*, 8> NewOps;
      bool NeedsRebuild =
          (Mask.size() !=
           cast<FixedVectorType>(I->getType())->getNumElements());
      for (int i = 0, e = I->getNumOperands(); i != e; ++i) {
        Value *V;
        // Recursively call evaluateInDifferentElementOrder on vector arguments
        // as well. E.g. GetElementPtr may have scalar operands even if the
        // return value is a vector, so we need to examine the operand type.
        if (I->getOperand(i)->getType()->isVectorTy())
          V = evaluateInDifferentElementOrder(I->getOperand(i), Mask, Builder);
        else
          V = I->getOperand(i);
        NewOps.push_back(V);
        NeedsRebuild |= (V != I->getOperand(i));
      }
      if (NeedsRebuild)
        return buildNew(I, NewOps, Builder);
      return I;
    }
    case Instruction::InsertElement: {
      int Element = cast<ConstantInt>(I->getOperand(2))->getLimitedValue();

      // The insertelement was inserting at Element. Figure out which element
      // that becomes after shuffling. The answer is guaranteed to be unique
      // by CanEvaluateShuffled.
      bool Found = false;
      int Index = 0;
      for (int e = Mask.size(); Index != e; ++Index) {
        if (Mask[Index] == Element) {
          Found = true;
          break;
        }
      }

      // If element is not in Mask, no need to handle the operand 1 (element to
      // be inserted). Just evaluate values in operand 0 according to Mask.
      if (!Found)
        return evaluateInDifferentElementOrder(I->getOperand(0), Mask, Builder);

      Value *V = evaluateInDifferentElementOrder(I->getOperand(0), Mask,
                                                 Builder);
      Builder.SetInsertPoint(I);
      return Builder.CreateInsertElement(V, I->getOperand(1), Index);
    }
  }
  llvm_unreachable("failed to reorder elements of vector instruction!");
}

// Returns true if the shuffle is extracting a contiguous range of values from
// LHS, for example:
//                 +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   Input:        |AA|BB|CC|DD|EE|FF|GG|HH|II|JJ|KK|LL|MM|NN|OO|PP|
//   Shuffles to:  |EE|FF|GG|HH|
//                 +--+--+--+--+
static bool isShuffleExtractingFromLHS(ShuffleVectorInst &SVI,
                                       ArrayRef<int> Mask) {
  unsigned LHSElems =
      cast<FixedVectorType>(SVI.getOperand(0)->getType())->getNumElements();
  unsigned MaskElems = Mask.size();
  unsigned BegIdx = Mask.front();
  unsigned EndIdx = Mask.back();
  if (BegIdx > EndIdx || EndIdx >= LHSElems || EndIdx - BegIdx != MaskElems - 1)
    return false;
  for (unsigned I = 0; I != MaskElems; ++I)
    if (static_cast<unsigned>(Mask[I]) != BegIdx + I)
      return false;
  return true;
}

/// These are the ingredients in an alternate form binary operator as described
/// below.
struct BinopElts {
  BinaryOperator::BinaryOps Opcode;
  Value *Op0;
  Value *Op1;
  BinopElts(BinaryOperator::BinaryOps Opc = (BinaryOperator::BinaryOps)0,
            Value *V0 = nullptr, Value *V1 = nullptr) :
      Opcode(Opc), Op0(V0), Op1(V1) {}
  operator bool() const { return Opcode != 0; }
};

/// Binops may be transformed into binops with different opcodes and operands.
/// Reverse the usual canonicalization to enable folds with the non-canonical
/// form of the binop. If a transform is possible, return the elements of the
/// new binop. If not, return invalid elements.
static BinopElts getAlternateBinop(BinaryOperator *BO, const DataLayout &DL) {
  Value *BO0 = BO->getOperand(0), *BO1 = BO->getOperand(1);
  Type *Ty = BO->getType();
  switch (BO->getOpcode()) {
  case Instruction::Shl: {
    // shl X, C --> mul X, (1 << C)
    Constant *C;
    if (match(BO1, m_ImmConstant(C))) {
      Constant *ShlOne = ConstantFoldBinaryOpOperands(
          Instruction::Shl, ConstantInt::get(Ty, 1), C, DL);
      assert(ShlOne && "Constant folding of immediate constants failed");
      return {Instruction::Mul, BO0, ShlOne};
    }
    break;
  }
  case Instruction::Or: {
    // or disjoin X, C --> add X, C
    if (cast<PossiblyDisjointInst>(BO)->isDisjoint())
      return {Instruction::Add, BO0, BO1};
    break;
  }
  case Instruction::Sub:
    // sub 0, X --> mul X, -1
    if (match(BO0, m_ZeroInt()))
      return {Instruction::Mul, BO1, ConstantInt::getAllOnesValue(Ty)};
    break;
  default:
    break;
  }
  return {};
}

/// A select shuffle of a select shuffle with a shared operand can be reduced
/// to a single select shuffle. This is an obvious improvement in IR, and the
/// backend is expected to lower select shuffles efficiently.
static Instruction *foldSelectShuffleOfSelectShuffle(ShuffleVectorInst &Shuf) {
  assert(Shuf.isSelect() && "Must have select-equivalent shuffle");

  Value *Op0 = Shuf.getOperand(0), *Op1 = Shuf.getOperand(1);
  SmallVector<int, 16> Mask;
  Shuf.getShuffleMask(Mask);
  unsigned NumElts = Mask.size();

  // Canonicalize a select shuffle with common operand as Op1.
  auto *ShufOp = dyn_cast<ShuffleVectorInst>(Op0);
  if (ShufOp && ShufOp->isSelect() &&
      (ShufOp->getOperand(0) == Op1 || ShufOp->getOperand(1) == Op1)) {
    std::swap(Op0, Op1);
    ShuffleVectorInst::commuteShuffleMask(Mask, NumElts);
  }

  ShufOp = dyn_cast<ShuffleVectorInst>(Op1);
  if (!ShufOp || !ShufOp->isSelect() ||
      (ShufOp->getOperand(0) != Op0 && ShufOp->getOperand(1) != Op0))
    return nullptr;

  Value *X = ShufOp->getOperand(0), *Y = ShufOp->getOperand(1);
  SmallVector<int, 16> Mask1;
  ShufOp->getShuffleMask(Mask1);
  assert(Mask1.size() == NumElts && "Vector size changed with select shuffle");

  // Canonicalize common operand (Op0) as X (first operand of first shuffle).
  if (Y == Op0) {
    std::swap(X, Y);
    ShuffleVectorInst::commuteShuffleMask(Mask1, NumElts);
  }

  // If the mask chooses from X (operand 0), it stays the same.
  // If the mask chooses from the earlier shuffle, the other mask value is
  // transferred to the combined select shuffle:
  // shuf X, (shuf X, Y, M1), M --> shuf X, Y, M'
  SmallVector<int, 16> NewMask(NumElts);
  for (unsigned i = 0; i != NumElts; ++i)
    NewMask[i] = Mask[i] < (signed)NumElts ? Mask[i] : Mask1[i];

  // A select mask with undef elements might look like an identity mask.
  assert((ShuffleVectorInst::isSelectMask(NewMask, NumElts) ||
          ShuffleVectorInst::isIdentityMask(NewMask, NumElts)) &&
         "Unexpected shuffle mask");
  return new ShuffleVectorInst(X, Y, NewMask);
}

static Instruction *foldSelectShuffleWith1Binop(ShuffleVectorInst &Shuf,
                                                const SimplifyQuery &SQ) {
  assert(Shuf.isSelect() && "Must have select-equivalent shuffle");

  // Are we shuffling together some value and that same value after it has been
  // modified by a binop with a constant?
  Value *Op0 = Shuf.getOperand(0), *Op1 = Shuf.getOperand(1);
  Constant *C;
  bool Op0IsBinop;
  if (match(Op0, m_BinOp(m_Specific(Op1), m_Constant(C))))
    Op0IsBinop = true;
  else if (match(Op1, m_BinOp(m_Specific(Op0), m_Constant(C))))
    Op0IsBinop = false;
  else
    return nullptr;

  // The identity constant for a binop leaves a variable operand unchanged. For
  // a vector, this is a splat of something like 0, -1, or 1.
  // If there's no identity constant for this binop, we're done.
  auto *BO = cast<BinaryOperator>(Op0IsBinop ? Op0 : Op1);
  BinaryOperator::BinaryOps BOpcode = BO->getOpcode();
  Constant *IdC = ConstantExpr::getBinOpIdentity(BOpcode, Shuf.getType(), true);
  if (!IdC)
    return nullptr;

  Value *X = Op0IsBinop ? Op1 : Op0;

  // Prevent folding in the case the non-binop operand might have NaN values.
  // If X can have NaN elements then we have that the floating point math
  // operation in the transformed code may not preserve the exact NaN
  // bit-pattern -- e.g. `fadd sNaN, 0.0 -> qNaN`.
  // This makes the transformation incorrect since the original program would
  // have preserved the exact NaN bit-pattern.
  // Avoid the folding if X can have NaN elements.
  if (Shuf.getType()->getElementType()->isFloatingPointTy() &&
      !isKnownNeverNaN(X, 0, SQ))
    return nullptr;

  // Shuffle identity constants into the lanes that return the original value.
  // Example: shuf (mul X, {-1,-2,-3,-4}), X, {0,5,6,3} --> mul X, {-1,1,1,-4}
  // Example: shuf X, (add X, {-1,-2,-3,-4}), {0,1,6,7} --> add X, {0,0,-3,-4}
  // The existing binop constant vector remains in the same operand position.
  ArrayRef<int> Mask = Shuf.getShuffleMask();
  Constant *NewC = Op0IsBinop ? ConstantExpr::getShuffleVector(C, IdC, Mask) :
                                ConstantExpr::getShuffleVector(IdC, C, Mask);

  bool MightCreatePoisonOrUB =
      is_contained(Mask, PoisonMaskElem) &&
      (Instruction::isIntDivRem(BOpcode) || Instruction::isShift(BOpcode));
  if (MightCreatePoisonOrUB)
    NewC = InstCombiner::getSafeVectorConstantForBinop(BOpcode, NewC, true);

  // shuf (bop X, C), X, M --> bop X, C'
  // shuf X, (bop X, C), M --> bop X, C'
  Instruction *NewBO = BinaryOperator::Create(BOpcode, X, NewC);
  NewBO->copyIRFlags(BO);

  // An undef shuffle mask element may propagate as an undef constant element in
  // the new binop. That would produce poison where the original code might not.
  // If we already made a safe constant, then there's no danger.
  if (is_contained(Mask, PoisonMaskElem) && !MightCreatePoisonOrUB)
    NewBO->dropPoisonGeneratingFlags();
  return NewBO;
}

/// If we have an insert of a scalar to a non-zero element of an undefined
/// vector and then shuffle that value, that's the same as inserting to the zero
/// element and shuffling. Splatting from the zero element is recognized as the
/// canonical form of splat.
static Instruction *canonicalizeInsertSplat(ShuffleVectorInst &Shuf,
                                            InstCombiner::BuilderTy &Builder) {
  Value *Op0 = Shuf.getOperand(0), *Op1 = Shuf.getOperand(1);
  ArrayRef<int> Mask = Shuf.getShuffleMask();
  Value *X;
  uint64_t IndexC;

  // Match a shuffle that is a splat to a non-zero element.
  if (!match(Op0, m_OneUse(m_InsertElt(m_Poison(), m_Value(X),
                                       m_ConstantInt(IndexC)))) ||
      !match(Op1, m_Poison()) || match(Mask, m_ZeroMask()) || IndexC == 0)
    return nullptr;

  // Insert into element 0 of a poison vector.
  PoisonValue *PoisonVec = PoisonValue::get(Shuf.getType());
  Value *NewIns = Builder.CreateInsertElement(PoisonVec, X, (uint64_t)0);

  // Splat from element 0. Any mask element that is poison remains poison.
  // For example:
  // shuf (inselt poison, X, 2), _, <2,2,undef>
  //   --> shuf (inselt poison, X, 0), poison, <0,0,undef>
  unsigned NumMaskElts =
      cast<FixedVectorType>(Shuf.getType())->getNumElements();
  SmallVector<int, 16> NewMask(NumMaskElts, 0);
  for (unsigned i = 0; i != NumMaskElts; ++i)
    if (Mask[i] == PoisonMaskElem)
      NewMask[i] = Mask[i];

  return new ShuffleVectorInst(NewIns, NewMask);
}

/// Try to fold shuffles that are the equivalent of a vector select.
Instruction *InstCombinerImpl::foldSelectShuffle(ShuffleVectorInst &Shuf) {
  if (!Shuf.isSelect())
    return nullptr;

  // Canonicalize to choose from operand 0 first unless operand 1 is undefined.
  // Commuting undef to operand 0 conflicts with another canonicalization.
  unsigned NumElts = cast<FixedVectorType>(Shuf.getType())->getNumElements();
  if (!match(Shuf.getOperand(1), m_Undef()) &&
      Shuf.getMaskValue(0) >= (int)NumElts) {
    // TODO: Can we assert that both operands of a shuffle-select are not undef
    // (otherwise, it would have been folded by instsimplify?
    Shuf.commute();
    return &Shuf;
  }

  if (Instruction *I = foldSelectShuffleOfSelectShuffle(Shuf))
    return I;

  if (Instruction *I = foldSelectShuffleWith1Binop(
          Shuf, getSimplifyQuery().getWithInstruction(&Shuf)))
    return I;

  BinaryOperator *B0, *B1;
  if (!match(Shuf.getOperand(0), m_BinOp(B0)) ||
      !match(Shuf.getOperand(1), m_BinOp(B1)))
    return nullptr;

  // If one operand is "0 - X", allow that to be viewed as "X * -1"
  // (ConstantsAreOp1) by getAlternateBinop below. If the neg is not paired
  // with a multiply, we will exit because C0/C1 will not be set.
  Value *X, *Y;
  Constant *C0 = nullptr, *C1 = nullptr;
  bool ConstantsAreOp1;
  if (match(B0, m_BinOp(m_Constant(C0), m_Value(X))) &&
      match(B1, m_BinOp(m_Constant(C1), m_Value(Y))))
    ConstantsAreOp1 = false;
  else if (match(B0, m_CombineOr(m_BinOp(m_Value(X), m_Constant(C0)),
                                 m_Neg(m_Value(X)))) &&
           match(B1, m_CombineOr(m_BinOp(m_Value(Y), m_Constant(C1)),
                                 m_Neg(m_Value(Y)))))
    ConstantsAreOp1 = true;
  else
    return nullptr;

  // We need matching binops to fold the lanes together.
  BinaryOperator::BinaryOps Opc0 = B0->getOpcode();
  BinaryOperator::BinaryOps Opc1 = B1->getOpcode();
  bool DropNSW = false;
  if (ConstantsAreOp1 && Opc0 != Opc1) {
    // TODO: We drop "nsw" if shift is converted into multiply because it may
    // not be correct when the shift amount is BitWidth - 1. We could examine
    // each vector element to determine if it is safe to keep that flag.
    if (Opc0 == Instruction::Shl || Opc1 == Instruction::Shl)
      DropNSW = true;
    if (BinopElts AltB0 = getAlternateBinop(B0, DL)) {
      assert(isa<Constant>(AltB0.Op1) && "Expecting constant with alt binop");
      Opc0 = AltB0.Opcode;
      C0 = cast<Constant>(AltB0.Op1);
    } else if (BinopElts AltB1 = getAlternateBinop(B1, DL)) {
      assert(isa<Constant>(AltB1.Op1) && "Expecting constant with alt binop");
      Opc1 = AltB1.Opcode;
      C1 = cast<Constant>(AltB1.Op1);
    }
  }

  if (Opc0 != Opc1 || !C0 || !C1)
    return nullptr;

  // The opcodes must be the same. Use a new name to make that clear.
  BinaryOperator::BinaryOps BOpc = Opc0;

  // Select the constant elements needed for the single binop.
  ArrayRef<int> Mask = Shuf.getShuffleMask();
  Constant *NewC = ConstantExpr::getShuffleVector(C0, C1, Mask);

  // We are moving a binop after a shuffle. When a shuffle has an undefined
  // mask element, the result is undefined, but it is not poison or undefined
  // behavior. That is not necessarily true for div/rem/shift.
  bool MightCreatePoisonOrUB =
      is_contained(Mask, PoisonMaskElem) &&
      (Instruction::isIntDivRem(BOpc) || Instruction::isShift(BOpc));
  if (MightCreatePoisonOrUB)
    NewC = InstCombiner::getSafeVectorConstantForBinop(BOpc, NewC,
                                                       ConstantsAreOp1);

  Value *V;
  if (X == Y) {
    // Remove a binop and the shuffle by rearranging the constant:
    // shuffle (op V, C0), (op V, C1), M --> op V, C'
    // shuffle (op C0, V), (op C1, V), M --> op C', V
    V = X;
  } else {
    // If there are 2 different variable operands, we must create a new shuffle
    // (select) first, so check uses to ensure that we don't end up with more
    // instructions than we started with.
    if (!B0->hasOneUse() && !B1->hasOneUse())
      return nullptr;

    // If we use the original shuffle mask and op1 is *variable*, we would be
    // putting an undef into operand 1 of div/rem/shift. This is either UB or
    // poison. We do not have to guard against UB when *constants* are op1
    // because safe constants guarantee that we do not overflow sdiv/srem (and
    // there's no danger for other opcodes).
    // TODO: To allow this case, create a new shuffle mask with no undefs.
    if (MightCreatePoisonOrUB && !ConstantsAreOp1)
      return nullptr;

    // Note: In general, we do not create new shuffles in InstCombine because we
    // do not know if a target can lower an arbitrary shuffle optimally. In this
    // case, the shuffle uses the existing mask, so there is no additional risk.

    // Select the variable vectors first, then perform the binop:
    // shuffle (op X, C0), (op Y, C1), M --> op (shuffle X, Y, M), C'
    // shuffle (op C0, X), (op C1, Y), M --> op C', (shuffle X, Y, M)
    V = Builder.CreateShuffleVector(X, Y, Mask);
  }

  Value *NewBO = ConstantsAreOp1 ? Builder.CreateBinOp(BOpc, V, NewC) :
                                   Builder.CreateBinOp(BOpc, NewC, V);

  // Flags are intersected from the 2 source binops. But there are 2 exceptions:
  // 1. If we changed an opcode, poison conditions might have changed.
  // 2. If the shuffle had undef mask elements, the new binop might have undefs
  //    where the original code did not. But if we already made a safe constant,
  //    then there's no danger.
  if (auto *NewI = dyn_cast<Instruction>(NewBO)) {
    NewI->copyIRFlags(B0);
    NewI->andIRFlags(B1);
    if (DropNSW)
      NewI->setHasNoSignedWrap(false);
    if (is_contained(Mask, PoisonMaskElem) && !MightCreatePoisonOrUB)
      NewI->dropPoisonGeneratingFlags();
  }
  return replaceInstUsesWith(Shuf, NewBO);
}

/// Convert a narrowing shuffle of a bitcasted vector into a vector truncate.
/// Example (little endian):
/// shuf (bitcast <4 x i16> X to <8 x i8>), <0, 2, 4, 6> --> trunc X to <4 x i8>
static Instruction *foldTruncShuffle(ShuffleVectorInst &Shuf,
                                     bool IsBigEndian) {
  // This must be a bitcasted shuffle of 1 vector integer operand.
  Type *DestType = Shuf.getType();
  Value *X;
  if (!match(Shuf.getOperand(0), m_BitCast(m_Value(X))) ||
      !match(Shuf.getOperand(1), m_Poison()) || !DestType->isIntOrIntVectorTy())
    return nullptr;

  // The source type must have the same number of elements as the shuffle,
  // and the source element type must be larger than the shuffle element type.
  Type *SrcType = X->getType();
  if (!SrcType->isVectorTy() || !SrcType->isIntOrIntVectorTy() ||
      cast<FixedVectorType>(SrcType)->getNumElements() !=
          cast<FixedVectorType>(DestType)->getNumElements() ||
      SrcType->getScalarSizeInBits() % DestType->getScalarSizeInBits() != 0)
    return nullptr;

  assert(Shuf.changesLength() && !Shuf.increasesLength() &&
         "Expected a shuffle that decreases length");

  // Last, check that the mask chooses the correct low bits for each narrow
  // element in the result.
  uint64_t TruncRatio =
      SrcType->getScalarSizeInBits() / DestType->getScalarSizeInBits();
  ArrayRef<int> Mask = Shuf.getShuffleMask();
  for (unsigned i = 0, e = Mask.size(); i != e; ++i) {
    if (Mask[i] == PoisonMaskElem)
      continue;
    uint64_t LSBIndex = IsBigEndian ? (i + 1) * TruncRatio - 1 : i * TruncRatio;
    assert(LSBIndex <= INT32_MAX && "Overflowed 32-bits");
    if (Mask[i] != (int)LSBIndex)
      return nullptr;
  }

  return new TruncInst(X, DestType);
}

/// Match a shuffle-select-shuffle pattern where the shuffles are widening and
/// narrowing (concatenating with poison and extracting back to the original
/// length). This allows replacing the wide select with a narrow select.
static Instruction *narrowVectorSelect(ShuffleVectorInst &Shuf,
                                       InstCombiner::BuilderTy &Builder) {
  // This must be a narrowing identity shuffle. It extracts the 1st N elements
  // of the 1st vector operand of a shuffle.
  if (!match(Shuf.getOperand(1), m_Poison()) || !Shuf.isIdentityWithExtract())
    return nullptr;

  // The vector being shuffled must be a vector select that we can eliminate.
  // TODO: The one-use requirement could be eased if X and/or Y are constants.
  Value *Cond, *X, *Y;
  if (!match(Shuf.getOperand(0),
             m_OneUse(m_Select(m_Value(Cond), m_Value(X), m_Value(Y)))))
    return nullptr;

  // We need a narrow condition value. It must be extended with poison elements
  // and have the same number of elements as this shuffle.
  unsigned NarrowNumElts =
      cast<FixedVectorType>(Shuf.getType())->getNumElements();
  Value *NarrowCond;
  if (!match(Cond, m_OneUse(m_Shuffle(m_Value(NarrowCond), m_Poison()))) ||
      cast<FixedVectorType>(NarrowCond->getType())->getNumElements() !=
          NarrowNumElts ||
      !cast<ShuffleVectorInst>(Cond)->isIdentityWithPadding())
    return nullptr;

  // shuf (sel (shuf NarrowCond, poison, WideMask), X, Y), poison, NarrowMask)
  // -->
  // sel NarrowCond, (shuf X, poison, NarrowMask), (shuf Y, poison, NarrowMask)
  Value *NarrowX = Builder.CreateShuffleVector(X, Shuf.getShuffleMask());
  Value *NarrowY = Builder.CreateShuffleVector(Y, Shuf.getShuffleMask());
  return SelectInst::Create(NarrowCond, NarrowX, NarrowY);
}

/// Canonicalize FP negate/abs after shuffle.
static Instruction *foldShuffleOfUnaryOps(ShuffleVectorInst &Shuf,
                                          InstCombiner::BuilderTy &Builder) {
  auto *S0 = dyn_cast<Instruction>(Shuf.getOperand(0));
  Value *X;
  if (!S0 || !match(S0, m_CombineOr(m_FNeg(m_Value(X)), m_FAbs(m_Value(X)))))
    return nullptr;

  bool IsFNeg = S0->getOpcode() == Instruction::FNeg;

  // Match 1-input (unary) shuffle.
  // shuffle (fneg/fabs X), Mask --> fneg/fabs (shuffle X, Mask)
  if (S0->hasOneUse() && match(Shuf.getOperand(1), m_Poison())) {
    Value *NewShuf = Builder.CreateShuffleVector(X, Shuf.getShuffleMask());
    if (IsFNeg)
      return UnaryOperator::CreateFNegFMF(NewShuf, S0);

    Function *FAbs = Intrinsic::getDeclaration(Shuf.getModule(),
                                               Intrinsic::fabs, Shuf.getType());
    CallInst *NewF = CallInst::Create(FAbs, {NewShuf});
    NewF->setFastMathFlags(S0->getFastMathFlags());
    return NewF;
  }

  // Match 2-input (binary) shuffle.
  auto *S1 = dyn_cast<Instruction>(Shuf.getOperand(1));
  Value *Y;
  if (!S1 || !match(S1, m_CombineOr(m_FNeg(m_Value(Y)), m_FAbs(m_Value(Y)))) ||
      S0->getOpcode() != S1->getOpcode() ||
      (!S0->hasOneUse() && !S1->hasOneUse()))
    return nullptr;

  // shuf (fneg/fabs X), (fneg/fabs Y), Mask --> fneg/fabs (shuf X, Y, Mask)
  Value *NewShuf = Builder.CreateShuffleVector(X, Y, Shuf.getShuffleMask());
  Instruction *NewF;
  if (IsFNeg) {
    NewF = UnaryOperator::CreateFNeg(NewShuf);
  } else {
    Function *FAbs = Intrinsic::getDeclaration(Shuf.getModule(),
                                               Intrinsic::fabs, Shuf.getType());
    NewF = CallInst::Create(FAbs, {NewShuf});
  }
  NewF->copyIRFlags(S0);
  NewF->andIRFlags(S1);
  return NewF;
}

/// Canonicalize casts after shuffle.
static Instruction *foldCastShuffle(ShuffleVectorInst &Shuf,
                                    InstCombiner::BuilderTy &Builder) {
  // Do we have 2 matching cast operands?
  auto *Cast0 = dyn_cast<CastInst>(Shuf.getOperand(0));
  auto *Cast1 = dyn_cast<CastInst>(Shuf.getOperand(1));
  if (!Cast0 || !Cast1 || Cast0->getOpcode() != Cast1->getOpcode() ||
      Cast0->getSrcTy() != Cast1->getSrcTy())
    return nullptr;

  // TODO: Allow other opcodes? That would require easing the type restrictions
  //       below here.
  CastInst::CastOps CastOpcode = Cast0->getOpcode();
  switch (CastOpcode) {
  case Instruction::FPToSI:
  case Instruction::FPToUI:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
    break;
  default:
    return nullptr;
  }

  VectorType *ShufTy = Shuf.getType();
  VectorType *ShufOpTy = cast<VectorType>(Shuf.getOperand(0)->getType());
  VectorType *CastSrcTy = cast<VectorType>(Cast0->getSrcTy());

  // TODO: Allow length-increasing shuffles?
  if (ShufTy->getElementCount().getKnownMinValue() >
      ShufOpTy->getElementCount().getKnownMinValue())
    return nullptr;

  // TODO: Allow element-size-decreasing casts (ex: fptosi float to i8)?
  assert(isa<FixedVectorType>(CastSrcTy) && isa<FixedVectorType>(ShufOpTy) &&
         "Expected fixed vector operands for casts and binary shuffle");
  if (CastSrcTy->getPrimitiveSizeInBits() > ShufOpTy->getPrimitiveSizeInBits())
    return nullptr;

  // At least one of the operands must have only one use (the shuffle).
  if (!Cast0->hasOneUse() && !Cast1->hasOneUse())
    return nullptr;

  // shuffle (cast X), (cast Y), Mask --> cast (shuffle X, Y, Mask)
  Value *X = Cast0->getOperand(0);
  Value *Y = Cast1->getOperand(0);
  Value *NewShuf = Builder.CreateShuffleVector(X, Y, Shuf.getShuffleMask());
  return CastInst::Create(CastOpcode, NewShuf, ShufTy);
}

/// Try to fold an extract subvector operation.
static Instruction *foldIdentityExtractShuffle(ShuffleVectorInst &Shuf) {
  Value *Op0 = Shuf.getOperand(0), *Op1 = Shuf.getOperand(1);
  if (!Shuf.isIdentityWithExtract() || !match(Op1, m_Poison()))
    return nullptr;

  // Check if we are extracting all bits of an inserted scalar:
  // extract-subvec (bitcast (inselt ?, X, 0) --> bitcast X to subvec type
  Value *X;
  if (match(Op0, m_BitCast(m_InsertElt(m_Value(), m_Value(X), m_Zero()))) &&
      X->getType()->getPrimitiveSizeInBits() ==
          Shuf.getType()->getPrimitiveSizeInBits())
    return new BitCastInst(X, Shuf.getType());

  // Try to combine 2 shuffles into 1 shuffle by concatenating a shuffle mask.
  Value *Y;
  ArrayRef<int> Mask;
  if (!match(Op0, m_Shuffle(m_Value(X), m_Value(Y), m_Mask(Mask))))
    return nullptr;

  // Be conservative with shuffle transforms. If we can't kill the 1st shuffle,
  // then combining may result in worse codegen.
  if (!Op0->hasOneUse())
    return nullptr;

  // We are extracting a subvector from a shuffle. Remove excess elements from
  // the 1st shuffle mask to eliminate the extract.
  //
  // This transform is conservatively limited to identity extracts because we do
  // not allow arbitrary shuffle mask creation as a target-independent transform
  // (because we can't guarantee that will lower efficiently).
  //
  // If the extracting shuffle has an poison mask element, it transfers to the
  // new shuffle mask. Otherwise, copy the original mask element. Example:
  //   shuf (shuf X, Y, <C0, C1, C2, poison, C4>), poison, <0, poison, 2, 3> -->
  //   shuf X, Y, <C0, poison, C2, poison>
  unsigned NumElts = cast<FixedVectorType>(Shuf.getType())->getNumElements();
  SmallVector<int, 16> NewMask(NumElts);
  assert(NumElts < Mask.size() &&
         "Identity with extract must have less elements than its inputs");

  for (unsigned i = 0; i != NumElts; ++i) {
    int ExtractMaskElt = Shuf.getMaskValue(i);
    int MaskElt = Mask[i];
    NewMask[i] = ExtractMaskElt == PoisonMaskElem ? ExtractMaskElt : MaskElt;
  }
  return new ShuffleVectorInst(X, Y, NewMask);
}

/// Try to replace a shuffle with an insertelement or try to replace a shuffle
/// operand with the operand of an insertelement.
static Instruction *foldShuffleWithInsert(ShuffleVectorInst &Shuf,
                                          InstCombinerImpl &IC) {
  Value *V0 = Shuf.getOperand(0), *V1 = Shuf.getOperand(1);
  SmallVector<int, 16> Mask;
  Shuf.getShuffleMask(Mask);

  int NumElts = Mask.size();
  int InpNumElts = cast<FixedVectorType>(V0->getType())->getNumElements();

  // This is a specialization of a fold in SimplifyDemandedVectorElts. We may
  // not be able to handle it there if the insertelement has >1 use.
  // If the shuffle has an insertelement operand but does not choose the
  // inserted scalar element from that value, then we can replace that shuffle
  // operand with the source vector of the insertelement.
  Value *X;
  uint64_t IdxC;
  if (match(V0, m_InsertElt(m_Value(X), m_Value(), m_ConstantInt(IdxC)))) {
    // shuf (inselt X, ?, IdxC), ?, Mask --> shuf X, ?, Mask
    if (!is_contained(Mask, (int)IdxC))
      return IC.replaceOperand(Shuf, 0, X);
  }
  if (match(V1, m_InsertElt(m_Value(X), m_Value(), m_ConstantInt(IdxC)))) {
    // Offset the index constant by the vector width because we are checking for
    // accesses to the 2nd vector input of the shuffle.
    IdxC += InpNumElts;
    // shuf ?, (inselt X, ?, IdxC), Mask --> shuf ?, X, Mask
    if (!is_contained(Mask, (int)IdxC))
      return IC.replaceOperand(Shuf, 1, X);
  }
  // For the rest of the transform, the shuffle must not change vector sizes.
  // TODO: This restriction could be removed if the insert has only one use
  //       (because the transform would require a new length-changing shuffle).
  if (NumElts != InpNumElts)
    return nullptr;

  // shuffle (insert ?, Scalar, IndexC), V1, Mask --> insert V1, Scalar, IndexC'
  auto isShufflingScalarIntoOp1 = [&](Value *&Scalar, ConstantInt *&IndexC) {
    // We need an insertelement with a constant index.
    if (!match(V0, m_InsertElt(m_Value(), m_Value(Scalar),
                               m_ConstantInt(IndexC))))
      return false;

    // Test the shuffle mask to see if it splices the inserted scalar into the
    // operand 1 vector of the shuffle.
    int NewInsIndex = -1;
    for (int i = 0; i != NumElts; ++i) {
      // Ignore undef mask elements.
      if (Mask[i] == -1)
        continue;

      // The shuffle takes elements of operand 1 without lane changes.
      if (Mask[i] == NumElts + i)
        continue;

      // The shuffle must choose the inserted scalar exactly once.
      if (NewInsIndex != -1 || Mask[i] != IndexC->getSExtValue())
        return false;

      // The shuffle is placing the inserted scalar into element i.
      NewInsIndex = i;
    }

    assert(NewInsIndex != -1 && "Did not fold shuffle with unused operand?");

    // Index is updated to the potentially translated insertion lane.
    IndexC = ConstantInt::get(IndexC->getIntegerType(), NewInsIndex);
    return true;
  };

  // If the shuffle is unnecessary, insert the scalar operand directly into
  // operand 1 of the shuffle. Example:
  // shuffle (insert ?, S, 1), V1, <1, 5, 6, 7> --> insert V1, S, 0
  Value *Scalar;
  ConstantInt *IndexC;
  if (isShufflingScalarIntoOp1(Scalar, IndexC))
    return InsertElementInst::Create(V1, Scalar, IndexC);

  // Try again after commuting shuffle. Example:
  // shuffle V0, (insert ?, S, 0), <0, 1, 2, 4> -->
  // shuffle (insert ?, S, 0), V0, <4, 5, 6, 0> --> insert V0, S, 3
  std::swap(V0, V1);
  ShuffleVectorInst::commuteShuffleMask(Mask, NumElts);
  if (isShufflingScalarIntoOp1(Scalar, IndexC))
    return InsertElementInst::Create(V1, Scalar, IndexC);

  return nullptr;
}

static Instruction *foldIdentityPaddedShuffles(ShuffleVectorInst &Shuf) {
  // Match the operands as identity with padding (also known as concatenation
  // with undef) shuffles of the same source type. The backend is expected to
  // recreate these concatenations from a shuffle of narrow operands.
  auto *Shuffle0 = dyn_cast<ShuffleVectorInst>(Shuf.getOperand(0));
  auto *Shuffle1 = dyn_cast<ShuffleVectorInst>(Shuf.getOperand(1));
  if (!Shuffle0 || !Shuffle0->isIdentityWithPadding() ||
      !Shuffle1 || !Shuffle1->isIdentityWithPadding())
    return nullptr;

  // We limit this transform to power-of-2 types because we expect that the
  // backend can convert the simplified IR patterns to identical nodes as the
  // original IR.
  // TODO: If we can verify the same behavior for arbitrary types, the
  //       power-of-2 checks can be removed.
  Value *X = Shuffle0->getOperand(0);
  Value *Y = Shuffle1->getOperand(0);
  if (X->getType() != Y->getType() ||
      !isPowerOf2_32(cast<FixedVectorType>(Shuf.getType())->getNumElements()) ||
      !isPowerOf2_32(
          cast<FixedVectorType>(Shuffle0->getType())->getNumElements()) ||
      !isPowerOf2_32(cast<FixedVectorType>(X->getType())->getNumElements()) ||
      match(X, m_Undef()) || match(Y, m_Undef()))
    return nullptr;
  assert(match(Shuffle0->getOperand(1), m_Undef()) &&
         match(Shuffle1->getOperand(1), m_Undef()) &&
         "Unexpected operand for identity shuffle");

  // This is a shuffle of 2 widening shuffles. We can shuffle the narrow source
  // operands directly by adjusting the shuffle mask to account for the narrower
  // types:
  // shuf (widen X), (widen Y), Mask --> shuf X, Y, Mask'
  int NarrowElts = cast<FixedVectorType>(X->getType())->getNumElements();
  int WideElts = cast<FixedVectorType>(Shuffle0->getType())->getNumElements();
  assert(WideElts > NarrowElts && "Unexpected types for identity with padding");

  ArrayRef<int> Mask = Shuf.getShuffleMask();
  SmallVector<int, 16> NewMask(Mask.size(), -1);
  for (int i = 0, e = Mask.size(); i != e; ++i) {
    if (Mask[i] == -1)
      continue;

    // If this shuffle is choosing an undef element from 1 of the sources, that
    // element is undef.
    if (Mask[i] < WideElts) {
      if (Shuffle0->getMaskValue(Mask[i]) == -1)
        continue;
    } else {
      if (Shuffle1->getMaskValue(Mask[i] - WideElts) == -1)
        continue;
    }

    // If this shuffle is choosing from the 1st narrow op, the mask element is
    // the same. If this shuffle is choosing from the 2nd narrow op, the mask
    // element is offset down to adjust for the narrow vector widths.
    if (Mask[i] < WideElts) {
      assert(Mask[i] < NarrowElts && "Unexpected shuffle mask");
      NewMask[i] = Mask[i];
    } else {
      assert(Mask[i] < (WideElts + NarrowElts) && "Unexpected shuffle mask");
      NewMask[i] = Mask[i] - (WideElts - NarrowElts);
    }
  }
  return new ShuffleVectorInst(X, Y, NewMask);
}

// Splatting the first element of the result of a BinOp, where any of the
// BinOp's operands are the result of a first element splat can be simplified to
// splatting the first element of the result of the BinOp
Instruction *InstCombinerImpl::simplifyBinOpSplats(ShuffleVectorInst &SVI) {
  if (!match(SVI.getOperand(1), m_Poison()) ||
      !match(SVI.getShuffleMask(), m_ZeroMask()) ||
      !SVI.getOperand(0)->hasOneUse())
    return nullptr;

  Value *Op0 = SVI.getOperand(0);
  Value *X, *Y;
  if (!match(Op0, m_BinOp(m_Shuffle(m_Value(X), m_Poison(), m_ZeroMask()),
                          m_Value(Y))) &&
      !match(Op0, m_BinOp(m_Value(X),
                          m_Shuffle(m_Value(Y), m_Poison(), m_ZeroMask()))))
    return nullptr;
  if (X->getType() != Y->getType())
    return nullptr;

  auto *BinOp = cast<BinaryOperator>(Op0);
  if (!isSafeToSpeculativelyExecute(BinOp))
    return nullptr;

  Value *NewBO = Builder.CreateBinOp(BinOp->getOpcode(), X, Y);
  if (auto NewBOI = dyn_cast<Instruction>(NewBO))
    NewBOI->copyIRFlags(BinOp);

  return new ShuffleVectorInst(NewBO, SVI.getShuffleMask());
}

Instruction *InstCombinerImpl::visitShuffleVectorInst(ShuffleVectorInst &SVI) {
  Value *LHS = SVI.getOperand(0);
  Value *RHS = SVI.getOperand(1);
  SimplifyQuery ShufQuery = SQ.getWithInstruction(&SVI);
  if (auto *V = simplifyShuffleVectorInst(LHS, RHS, SVI.getShuffleMask(),
                                          SVI.getType(), ShufQuery))
    return replaceInstUsesWith(SVI, V);

  if (Instruction *I = simplifyBinOpSplats(SVI))
    return I;

  // Canonicalize splat shuffle to use poison RHS. Handle this explicitly in
  // order to support scalable vectors.
  if (match(SVI.getShuffleMask(), m_ZeroMask()) && !isa<PoisonValue>(RHS))
    return replaceOperand(SVI, 1, PoisonValue::get(RHS->getType()));

  if (isa<ScalableVectorType>(LHS->getType()))
    return nullptr;

  unsigned VWidth = cast<FixedVectorType>(SVI.getType())->getNumElements();
  unsigned LHSWidth = cast<FixedVectorType>(LHS->getType())->getNumElements();

  // shuffle (bitcast X), (bitcast Y), Mask --> bitcast (shuffle X, Y, Mask)
  //
  // if X and Y are of the same (vector) type, and the element size is not
  // changed by the bitcasts, we can distribute the bitcasts through the
  // shuffle, hopefully reducing the number of instructions. We make sure that
  // at least one bitcast only has one use, so we don't *increase* the number of
  // instructions here.
  Value *X, *Y;
  if (match(LHS, m_BitCast(m_Value(X))) && match(RHS, m_BitCast(m_Value(Y))) &&
      X->getType()->isVectorTy() && X->getType() == Y->getType() &&
      X->getType()->getScalarSizeInBits() ==
          SVI.getType()->getScalarSizeInBits() &&
      (LHS->hasOneUse() || RHS->hasOneUse())) {
    Value *V = Builder.CreateShuffleVector(X, Y, SVI.getShuffleMask(),
                                           SVI.getName() + ".uncasted");
    return new BitCastInst(V, SVI.getType());
  }

  ArrayRef<int> Mask = SVI.getShuffleMask();

  // Peek through a bitcasted shuffle operand by scaling the mask. If the
  // simulated shuffle can simplify, then this shuffle is unnecessary:
  // shuf (bitcast X), undef, Mask --> bitcast X'
  // TODO: This could be extended to allow length-changing shuffles.
  //       The transform might also be obsoleted if we allowed canonicalization
  //       of bitcasted shuffles.
  if (match(LHS, m_BitCast(m_Value(X))) && match(RHS, m_Undef()) &&
      X->getType()->isVectorTy() && VWidth == LHSWidth) {
    // Try to create a scaled mask constant.
    auto *XType = cast<FixedVectorType>(X->getType());
    unsigned XNumElts = XType->getNumElements();
    SmallVector<int, 16> ScaledMask;
    if (scaleShuffleMaskElts(XNumElts, Mask, ScaledMask)) {
      // If the shuffled source vector simplifies, cast that value to this
      // shuffle's type.
      if (auto *V = simplifyShuffleVectorInst(X, UndefValue::get(XType),
                                              ScaledMask, XType, ShufQuery))
        return BitCastInst::Create(Instruction::BitCast, V, SVI.getType());
    }
  }

  // shuffle x, x, mask --> shuffle x, undef, mask'
  if (LHS == RHS) {
    assert(!match(RHS, m_Undef()) &&
           "Shuffle with 2 undef ops not simplified?");
    return new ShuffleVectorInst(LHS, createUnaryMask(Mask, LHSWidth));
  }

  // shuffle undef, x, mask --> shuffle x, undef, mask'
  if (match(LHS, m_Undef())) {
    SVI.commute();
    return &SVI;
  }

  if (Instruction *I = canonicalizeInsertSplat(SVI, Builder))
    return I;

  if (Instruction *I = foldSelectShuffle(SVI))
    return I;

  if (Instruction *I = foldTruncShuffle(SVI, DL.isBigEndian()))
    return I;

  if (Instruction *I = narrowVectorSelect(SVI, Builder))
    return I;

  if (Instruction *I = foldShuffleOfUnaryOps(SVI, Builder))
    return I;

  if (Instruction *I = foldCastShuffle(SVI, Builder))
    return I;

  APInt PoisonElts(VWidth, 0);
  APInt AllOnesEltMask(APInt::getAllOnes(VWidth));
  if (Value *V = SimplifyDemandedVectorElts(&SVI, AllOnesEltMask, PoisonElts)) {
    if (V != &SVI)
      return replaceInstUsesWith(SVI, V);
    return &SVI;
  }

  if (Instruction *I = foldIdentityExtractShuffle(SVI))
    return I;

  // These transforms have the potential to lose undef knowledge, so they are
  // intentionally placed after SimplifyDemandedVectorElts().
  if (Instruction *I = foldShuffleWithInsert(SVI, *this))
    return I;
  if (Instruction *I = foldIdentityPaddedShuffles(SVI))
    return I;

  if (match(RHS, m_Poison()) && canEvaluateShuffled(LHS, Mask)) {
    Value *V = evaluateInDifferentElementOrder(LHS, Mask, Builder);
    return replaceInstUsesWith(SVI, V);
  }

  // SROA generates shuffle+bitcast when the extracted sub-vector is bitcast to
  // a non-vector type. We can instead bitcast the original vector followed by
  // an extract of the desired element:
  //
  //   %sroa = shufflevector <16 x i8> %in, <16 x i8> undef,
  //                         <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  //   %1 = bitcast <4 x i8> %sroa to i32
  // Becomes:
  //   %bc = bitcast <16 x i8> %in to <4 x i32>
  //   %ext = extractelement <4 x i32> %bc, i32 0
  //
  // If the shuffle is extracting a contiguous range of values from the input
  // vector then each use which is a bitcast of the extracted size can be
  // replaced. This will work if the vector types are compatible, and the begin
  // index is aligned to a value in the casted vector type. If the begin index
  // isn't aligned then we can shuffle the original vector (keeping the same
  // vector type) before extracting.
  //
  // This code will bail out if the target type is fundamentally incompatible
  // with vectors of the source type.
  //
  // Example of <16 x i8>, target type i32:
  // Index range [4,8):         v-----------v Will work.
  //                +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //     <16 x i8>: |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
  //     <4 x i32>: |           |           |           |           |
  //                +-----------+-----------+-----------+-----------+
  // Index range [6,10):              ^-----------^ Needs an extra shuffle.
  // Target type i40:           ^--------------^ Won't work, bail.
  bool MadeChange = false;
  if (isShuffleExtractingFromLHS(SVI, Mask)) {
    Value *V = LHS;
    unsigned MaskElems = Mask.size();
    auto *SrcTy = cast<FixedVectorType>(V->getType());
    unsigned VecBitWidth = SrcTy->getPrimitiveSizeInBits().getFixedValue();
    unsigned SrcElemBitWidth = DL.getTypeSizeInBits(SrcTy->getElementType());
    assert(SrcElemBitWidth && "vector elements must have a bitwidth");
    unsigned SrcNumElems = SrcTy->getNumElements();
    SmallVector<BitCastInst *, 8> BCs;
    DenseMap<Type *, Value *> NewBCs;
    for (User *U : SVI.users())
      if (BitCastInst *BC = dyn_cast<BitCastInst>(U))
        if (!BC->use_empty())
          // Only visit bitcasts that weren't previously handled.
          BCs.push_back(BC);
    for (BitCastInst *BC : BCs) {
      unsigned BegIdx = Mask.front();
      Type *TgtTy = BC->getDestTy();
      unsigned TgtElemBitWidth = DL.getTypeSizeInBits(TgtTy);
      if (!TgtElemBitWidth)
        continue;
      unsigned TgtNumElems = VecBitWidth / TgtElemBitWidth;
      bool VecBitWidthsEqual = VecBitWidth == TgtNumElems * TgtElemBitWidth;
      bool BegIsAligned = 0 == ((SrcElemBitWidth * BegIdx) % TgtElemBitWidth);
      if (!VecBitWidthsEqual)
        continue;
      if (!VectorType::isValidElementType(TgtTy))
        continue;
      auto *CastSrcTy = FixedVectorType::get(TgtTy, TgtNumElems);
      if (!BegIsAligned) {
        // Shuffle the input so [0,NumElements) contains the output, and
        // [NumElems,SrcNumElems) is undef.
        SmallVector<int, 16> ShuffleMask(SrcNumElems, -1);
        for (unsigned I = 0, E = MaskElems, Idx = BegIdx; I != E; ++Idx, ++I)
          ShuffleMask[I] = Idx;
        V = Builder.CreateShuffleVector(V, ShuffleMask,
                                        SVI.getName() + ".extract");
        BegIdx = 0;
      }
      unsigned SrcElemsPerTgtElem = TgtElemBitWidth / SrcElemBitWidth;
      assert(SrcElemsPerTgtElem);
      BegIdx /= SrcElemsPerTgtElem;
      bool BCAlreadyExists = NewBCs.contains(CastSrcTy);
      auto *NewBC =
          BCAlreadyExists
              ? NewBCs[CastSrcTy]
              : Builder.CreateBitCast(V, CastSrcTy, SVI.getName() + ".bc");
      if (!BCAlreadyExists)
        NewBCs[CastSrcTy] = NewBC;
      auto *Ext = Builder.CreateExtractElement(NewBC, BegIdx,
                                               SVI.getName() + ".extract");
      // The shufflevector isn't being replaced: the bitcast that used it
      // is. InstCombine will visit the newly-created instructions.
      replaceInstUsesWith(*BC, Ext);
      MadeChange = true;
    }
  }

  // If the LHS is a shufflevector itself, see if we can combine it with this
  // one without producing an unusual shuffle.
  // Cases that might be simplified:
  // 1.
  // x1=shuffle(v1,v2,mask1)
  //  x=shuffle(x1,undef,mask)
  //        ==>
  //  x=shuffle(v1,undef,newMask)
  // newMask[i] = (mask[i] < x1.size()) ? mask1[mask[i]] : -1
  // 2.
  // x1=shuffle(v1,undef,mask1)
  //  x=shuffle(x1,x2,mask)
  // where v1.size() == mask1.size()
  //        ==>
  //  x=shuffle(v1,x2,newMask)
  // newMask[i] = (mask[i] < x1.size()) ? mask1[mask[i]] : mask[i]
  // 3.
  // x2=shuffle(v2,undef,mask2)
  //  x=shuffle(x1,x2,mask)
  // where v2.size() == mask2.size()
  //        ==>
  //  x=shuffle(x1,v2,newMask)
  // newMask[i] = (mask[i] < x1.size())
  //              ? mask[i] : mask2[mask[i]-x1.size()]+x1.size()
  // 4.
  // x1=shuffle(v1,undef,mask1)
  // x2=shuffle(v2,undef,mask2)
  //  x=shuffle(x1,x2,mask)
  // where v1.size() == v2.size()
  //        ==>
  //  x=shuffle(v1,v2,newMask)
  // newMask[i] = (mask[i] < x1.size())
  //              ? mask1[mask[i]] : mask2[mask[i]-x1.size()]+v1.size()
  //
  // Here we are really conservative:
  // we are absolutely afraid of producing a shuffle mask not in the input
  // program, because the code gen may not be smart enough to turn a merged
  // shuffle into two specific shuffles: it may produce worse code.  As such,
  // we only merge two shuffles if the result is either a splat or one of the
  // input shuffle masks.  In this case, merging the shuffles just removes
  // one instruction, which we know is safe.  This is good for things like
  // turning: (splat(splat)) -> splat, or
  // merge(V[0..n], V[n+1..2n]) -> V[0..2n]
  ShuffleVectorInst* LHSShuffle = dyn_cast<ShuffleVectorInst>(LHS);
  ShuffleVectorInst* RHSShuffle = dyn_cast<ShuffleVectorInst>(RHS);
  if (LHSShuffle)
    if (!match(LHSShuffle->getOperand(1), m_Poison()) &&
        !match(RHS, m_Poison()))
      LHSShuffle = nullptr;
  if (RHSShuffle)
    if (!match(RHSShuffle->getOperand(1), m_Poison()))
      RHSShuffle = nullptr;
  if (!LHSShuffle && !RHSShuffle)
    return MadeChange ? &SVI : nullptr;

  Value* LHSOp0 = nullptr;
  Value* LHSOp1 = nullptr;
  Value* RHSOp0 = nullptr;
  unsigned LHSOp0Width = 0;
  unsigned RHSOp0Width = 0;
  if (LHSShuffle) {
    LHSOp0 = LHSShuffle->getOperand(0);
    LHSOp1 = LHSShuffle->getOperand(1);
    LHSOp0Width = cast<FixedVectorType>(LHSOp0->getType())->getNumElements();
  }
  if (RHSShuffle) {
    RHSOp0 = RHSShuffle->getOperand(0);
    RHSOp0Width = cast<FixedVectorType>(RHSOp0->getType())->getNumElements();
  }
  Value* newLHS = LHS;
  Value* newRHS = RHS;
  if (LHSShuffle) {
    // case 1
    if (match(RHS, m_Poison())) {
      newLHS = LHSOp0;
      newRHS = LHSOp1;
    }
    // case 2 or 4
    else if (LHSOp0Width == LHSWidth) {
      newLHS = LHSOp0;
    }
  }
  // case 3 or 4
  if (RHSShuffle && RHSOp0Width == LHSWidth) {
    newRHS = RHSOp0;
  }
  // case 4
  if (LHSOp0 == RHSOp0) {
    newLHS = LHSOp0;
    newRHS = nullptr;
  }

  if (newLHS == LHS && newRHS == RHS)
    return MadeChange ? &SVI : nullptr;

  ArrayRef<int> LHSMask;
  ArrayRef<int> RHSMask;
  if (newLHS != LHS)
    LHSMask = LHSShuffle->getShuffleMask();
  if (RHSShuffle && newRHS != RHS)
    RHSMask = RHSShuffle->getShuffleMask();

  unsigned newLHSWidth = (newLHS != LHS) ? LHSOp0Width : LHSWidth;
  SmallVector<int, 16> newMask;
  bool isSplat = true;
  int SplatElt = -1;
  // Create a new mask for the new ShuffleVectorInst so that the new
  // ShuffleVectorInst is equivalent to the original one.
  for (unsigned i = 0; i < VWidth; ++i) {
    int eltMask;
    if (Mask[i] < 0) {
      // This element is a poison value.
      eltMask = -1;
    } else if (Mask[i] < (int)LHSWidth) {
      // This element is from left hand side vector operand.
      //
      // If LHS is going to be replaced (case 1, 2, or 4), calculate the
      // new mask value for the element.
      if (newLHS != LHS) {
        eltMask = LHSMask[Mask[i]];
        // If the value selected is an poison value, explicitly specify it
        // with a -1 mask value.
        if (eltMask >= (int)LHSOp0Width && isa<PoisonValue>(LHSOp1))
          eltMask = -1;
      } else
        eltMask = Mask[i];
    } else {
      // This element is from right hand side vector operand
      //
      // If the value selected is a poison value, explicitly specify it
      // with a -1 mask value. (case 1)
      if (match(RHS, m_Poison()))
        eltMask = -1;
      // If RHS is going to be replaced (case 3 or 4), calculate the
      // new mask value for the element.
      else if (newRHS != RHS) {
        eltMask = RHSMask[Mask[i]-LHSWidth];
        // If the value selected is an poison value, explicitly specify it
        // with a -1 mask value.
        if (eltMask >= (int)RHSOp0Width) {
          assert(match(RHSShuffle->getOperand(1), m_Poison()) &&
                 "should have been check above");
          eltMask = -1;
        }
      } else
        eltMask = Mask[i]-LHSWidth;

      // If LHS's width is changed, shift the mask value accordingly.
      // If newRHS == nullptr, i.e. LHSOp0 == RHSOp0, we want to remap any
      // references from RHSOp0 to LHSOp0, so we don't need to shift the mask.
      // If newRHS == newLHS, we want to remap any references from newRHS to
      // newLHS so that we can properly identify splats that may occur due to
      // obfuscation across the two vectors.
      if (eltMask >= 0 && newRHS != nullptr && newLHS != newRHS)
        eltMask += newLHSWidth;
    }

    // Check if this could still be a splat.
    if (eltMask >= 0) {
      if (SplatElt >= 0 && SplatElt != eltMask)
        isSplat = false;
      SplatElt = eltMask;
    }

    newMask.push_back(eltMask);
  }

  // If the result mask is equal to one of the original shuffle masks,
  // or is a splat, do the replacement.
  if (isSplat || newMask == LHSMask || newMask == RHSMask || newMask == Mask) {
    if (!newRHS)
      newRHS = PoisonValue::get(newLHS->getType());
    return new ShuffleVectorInst(newLHS, newRHS, newMask);
  }

  return MadeChange ? &SVI : nullptr;
}
