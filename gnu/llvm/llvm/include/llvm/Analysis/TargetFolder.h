//====- TargetFolder.h - Constant folding helper ---------------*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TargetFolder class, a helper for IRBuilder.
// It provides IRBuilder with a set of methods for creating constants with
// target dependent folding, in addition to the same target-independent
// folding that the ConstantFolder class provides.  For general constant
// creation and folding, use ConstantExpr and the routines in
// llvm/Analysis/ConstantFolding.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TARGETFOLDER_H
#define LLVM_ANALYSIS_TARGETFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilderFolder.h"
#include "llvm/IR/Operator.h"

namespace llvm {

class Constant;
class DataLayout;
class Type;

/// TargetFolder - Create constants with target dependent folding.
class TargetFolder final : public IRBuilderFolder {
  const DataLayout &DL;

  /// Fold - Fold the constant using target specific information.
  Constant *Fold(Constant *C) const {
    return ConstantFoldConstant(C, DL);
  }

  virtual void anchor();

public:
  explicit TargetFolder(const DataLayout &DL) : DL(DL) {}

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                   Value *RHS) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc))
        return Fold(ConstantExpr::get(Opc, LC, RC));
      return ConstantFoldBinaryOpOperands(Opc, LC, RC, DL);
    }
    return nullptr;
  }

  Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                        bool IsExact) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc))
        return Fold(ConstantExpr::get(
            Opc, LC, RC, IsExact ? PossiblyExactOperator::IsExact : 0));
      return ConstantFoldBinaryOpOperands(Opc, LC, RC, DL);
    }
    return nullptr;
  }

  Value *FoldNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                         bool HasNUW, bool HasNSW) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc)) {
        unsigned Flags = 0;
        if (HasNUW)
          Flags |= OverflowingBinaryOperator::NoUnsignedWrap;
        if (HasNSW)
          Flags |= OverflowingBinaryOperator::NoSignedWrap;
        return Fold(ConstantExpr::get(Opc, LC, RC, Flags));
      }
      return ConstantFoldBinaryOpOperands(Opc, LC, RC, DL);
    }
    return nullptr;
  }

  Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                      FastMathFlags FMF) const override {
    return FoldBinOp(Opc, LHS, RHS);
  }

  Value *FoldCmp(CmpInst::Predicate P, Value *LHS, Value *RHS) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC)
      return ConstantFoldCompareInstOperands(P, LC, RC, DL);
    return nullptr;
  }

  Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                      FastMathFlags FMF) const override {
    if (Constant *C = dyn_cast<Constant>(V))
      return ConstantFoldUnaryOpOperand(Opc, C, DL);
    return nullptr;
  }

  Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                 GEPNoWrapFlags NW) const override {
    if (!ConstantExpr::isSupportedGetElementPtr(Ty))
      return nullptr;

    if (auto *PC = dyn_cast<Constant>(Ptr)) {
      // Every index must be constant.
      if (any_of(IdxList, [](Value *V) { return !isa<Constant>(V); }))
        return nullptr;
      return Fold(ConstantExpr::getGetElementPtr(Ty, PC, IdxList, NW));
    }
    return nullptr;
  }

  Value *FoldSelect(Value *C, Value *True, Value *False) const override {
    auto *CC = dyn_cast<Constant>(C);
    auto *TC = dyn_cast<Constant>(True);
    auto *FC = dyn_cast<Constant>(False);
    if (CC && TC && FC)
      return ConstantFoldSelectInstruction(CC, TC, FC);

    return nullptr;
  }

  Value *FoldExtractValue(Value *Agg,
                          ArrayRef<unsigned> IdxList) const override {
    if (auto *CAgg = dyn_cast<Constant>(Agg))
      return ConstantFoldExtractValueInstruction(CAgg, IdxList);
    return nullptr;
  };

  Value *FoldInsertValue(Value *Agg, Value *Val,
                         ArrayRef<unsigned> IdxList) const override {
    auto *CAgg = dyn_cast<Constant>(Agg);
    auto *CVal = dyn_cast<Constant>(Val);
    if (CAgg && CVal)
      return ConstantFoldInsertValueInstruction(CAgg, CVal, IdxList);
    return nullptr;
  }

  Value *FoldExtractElement(Value *Vec, Value *Idx) const override {
    auto *CVec = dyn_cast<Constant>(Vec);
    auto *CIdx = dyn_cast<Constant>(Idx);
    if (CVec && CIdx)
      return Fold(ConstantExpr::getExtractElement(CVec, CIdx));
    return nullptr;
  }

  Value *FoldInsertElement(Value *Vec, Value *NewElt,
                           Value *Idx) const override {
    auto *CVec = dyn_cast<Constant>(Vec);
    auto *CNewElt = dyn_cast<Constant>(NewElt);
    auto *CIdx = dyn_cast<Constant>(Idx);
    if (CVec && CNewElt && CIdx)
      return Fold(ConstantExpr::getInsertElement(CVec, CNewElt, CIdx));
    return nullptr;
  }

  Value *FoldShuffleVector(Value *V1, Value *V2,
                           ArrayRef<int> Mask) const override {
    auto *C1 = dyn_cast<Constant>(V1);
    auto *C2 = dyn_cast<Constant>(V2);
    if (C1 && C2)
      return Fold(ConstantExpr::getShuffleVector(C1, C2, Mask));
    return nullptr;
  }

  Value *FoldCast(Instruction::CastOps Op, Value *V,
                  Type *DestTy) const override {
    if (auto *C = dyn_cast<Constant>(V))
      return ConstantFoldCastOperand(Op, C, DestTy, DL);
    return nullptr;
  }

  Value *FoldBinaryIntrinsic(Intrinsic::ID ID, Value *LHS, Value *RHS, Type *Ty,
                             Instruction *FMFSource) const override {
    auto *C1 = dyn_cast<Constant>(LHS);
    auto *C2 = dyn_cast<Constant>(RHS);
    if (C1 && C2)
      return ConstantFoldBinaryIntrinsic(ID, C1, C2, Ty, FMFSource);
    return nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  Constant *CreatePointerCast(Constant *C, Type *DestTy) const override {
    if (C->getType() == DestTy)
      return C; // avoid calling Fold
    return Fold(ConstantExpr::getPointerCast(C, DestTy));
  }

  Constant *CreatePointerBitCastOrAddrSpaceCast(Constant *C,
                                                Type *DestTy) const override {
    if (C->getType() == DestTy)
      return C; // avoid calling Fold
    return Fold(ConstantExpr::getPointerBitCastOrAddrSpaceCast(C, DestTy));
  }
};

}

#endif
