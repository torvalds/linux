//===- IRBuilderFolder.h - Const folder interface for IRBuilder -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines for constant folding interface used by IRBuilder.
// It is implemented by ConstantFolder (default), TargetFolder and NoFoler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IRBUILDERFOLDER_H
#define LLVM_IR_IRBUILDERFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

/// IRBuilderFolder - Interface for constant folding in IRBuilder.
class IRBuilderFolder {
public:
  virtual ~IRBuilderFolder();

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  virtual Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                           Value *RHS) const = 0;

  virtual Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS,
                                Value *RHS, bool IsExact) const = 0;

  virtual Value *FoldNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS,
                                 Value *RHS, bool HasNUW,
                                 bool HasNSW) const = 0;

  virtual Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS,
                              Value *RHS, FastMathFlags FMF) const = 0;

  virtual Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                             FastMathFlags FMF) const = 0;

  virtual Value *FoldCmp(CmpInst::Predicate P, Value *LHS,
                         Value *RHS) const = 0;

  virtual Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                         GEPNoWrapFlags NW) const = 0;

  virtual Value *FoldSelect(Value *C, Value *True, Value *False) const = 0;

  virtual Value *FoldExtractValue(Value *Agg,
                                  ArrayRef<unsigned> IdxList) const = 0;

  virtual Value *FoldInsertValue(Value *Agg, Value *Val,
                                 ArrayRef<unsigned> IdxList) const = 0;

  virtual Value *FoldExtractElement(Value *Vec, Value *Idx) const = 0;

  virtual Value *FoldInsertElement(Value *Vec, Value *NewElt,
                                   Value *Idx) const = 0;

  virtual Value *FoldShuffleVector(Value *V1, Value *V2,
                                   ArrayRef<int> Mask) const = 0;

  virtual Value *FoldCast(Instruction::CastOps Op, Value *V,
                          Type *DestTy) const = 0;

  virtual Value *
  FoldBinaryIntrinsic(Intrinsic::ID ID, Value *LHS, Value *RHS, Type *Ty,
                      Instruction *FMFSource = nullptr) const = 0;

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  virtual Value *CreatePointerCast(Constant *C, Type *DestTy) const = 0;
  virtual Value *CreatePointerBitCastOrAddrSpaceCast(Constant *C,
                                                     Type *DestTy) const = 0;
};

} // end namespace llvm

#endif // LLVM_IR_IRBUILDERFOLDER_H
