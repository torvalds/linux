//===- NoFolder.h - Constant folding helper ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the NoFolder class, a helper for IRBuilder.  It provides
// IRBuilder with a set of methods for creating unfolded constants.  This is
// useful for learners trying to understand how LLVM IR works, and who don't
// want details to be hidden by the constant folder.  For general constant
// creation and folding, use ConstantExpr and the routines in
// llvm/Analysis/ConstantFolding.h.
//
// Note: since it is not actually possible to create unfolded constants, this
// class returns instructions rather than constants.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_NOFOLDER_H
#define LLVM_IR_NOFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/IRBuilderFolder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

/// NoFolder - Create "constants" (actually, instructions) with no folding.
class NoFolder final : public IRBuilderFolder {
  virtual void anchor();

public:
  explicit NoFolder() = default;

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                   Value *RHS) const override {
    return nullptr;
  }

  Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                        bool IsExact) const override {
    return nullptr;
  }

  Value *FoldNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                         bool HasNUW, bool HasNSW) const override {
    return nullptr;
  }

  Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                      FastMathFlags FMF) const override {
    return nullptr;
  }

  Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                     FastMathFlags FMF) const override {
    return nullptr;
  }

  Value *FoldCmp(CmpInst::Predicate P, Value *LHS, Value *RHS) const override {
    return nullptr;
  }

  Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                 GEPNoWrapFlags NW) const override {
    return nullptr;
  }

  Value *FoldSelect(Value *C, Value *True, Value *False) const override {
    return nullptr;
  }

  Value *FoldExtractValue(Value *Agg,
                          ArrayRef<unsigned> IdxList) const override {
    return nullptr;
  }

  Value *FoldInsertValue(Value *Agg, Value *Val,
                         ArrayRef<unsigned> IdxList) const override {
    return nullptr;
  }

  Value *FoldExtractElement(Value *Vec, Value *Idx) const override {
    return nullptr;
  }

  Value *FoldInsertElement(Value *Vec, Value *NewElt,
                           Value *Idx) const override {
    return nullptr;
  }

  Value *FoldShuffleVector(Value *V1, Value *V2,
                           ArrayRef<int> Mask) const override {
    return nullptr;
  }

  Value *FoldCast(Instruction::CastOps Op, Value *V,
                  Type *DestTy) const override {
    return nullptr;
  }

  Value *FoldBinaryIntrinsic(Intrinsic::ID ID, Value *LHS, Value *RHS, Type *Ty,
                             Instruction *FMFSource) const override {
    return nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  Instruction *CreatePointerCast(Constant *C, Type *DestTy) const override {
    return CastInst::CreatePointerCast(C, DestTy);
  }

  Instruction *CreatePointerBitCastOrAddrSpaceCast(
      Constant *C, Type *DestTy) const override {
    return CastInst::CreatePointerBitCastOrAddrSpaceCast(C, DestTy);
  }
};

} // end namespace llvm

#endif // LLVM_IR_NOFOLDER_H
