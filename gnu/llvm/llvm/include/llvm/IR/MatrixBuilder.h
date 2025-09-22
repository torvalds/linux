//===- llvm/MatrixBuilder.h - Builder to lower matrix ops -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MatrixBuilder class, which is used as a convenient way
// to lower matrix operations to LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MATRIXBUILDER_H
#define LLVM_IR_MATRIXBUILDER_H

#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"

namespace llvm {

class Function;
class Twine;
class Module;

class MatrixBuilder {
  IRBuilderBase &B;
  Module *getModule() { return B.GetInsertBlock()->getParent()->getParent(); }

  std::pair<Value *, Value *> splatScalarOperandIfNeeded(Value *LHS,
                                                         Value *RHS) {
    assert((LHS->getType()->isVectorTy() || RHS->getType()->isVectorTy()) &&
           "One of the operands must be a matrix (embedded in a vector)");
    if (LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(LHS->getType()) &&
             "LHS Assumed to be fixed width");
      RHS = B.CreateVectorSplat(
          cast<VectorType>(LHS->getType())->getElementCount(), RHS,
          "scalar.splat");
    } else if (!LHS->getType()->isVectorTy() && RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(RHS->getType()) &&
             "RHS Assumed to be fixed width");
      LHS = B.CreateVectorSplat(
          cast<VectorType>(RHS->getType())->getElementCount(), LHS,
          "scalar.splat");
    }
    return {LHS, RHS};
  }

public:
  MatrixBuilder(IRBuilderBase &Builder) : B(Builder) {}

  /// Create a column major, strided matrix load.
  /// \p EltTy   - Matrix element type
  /// \p DataPtr - Start address of the matrix read
  /// \p Rows    - Number of rows in matrix (must be a constant)
  /// \p Columns - Number of columns in matrix (must be a constant)
  /// \p Stride  - Space between columns
  CallInst *CreateColumnMajorLoad(Type *EltTy, Value *DataPtr, Align Alignment,
                                  Value *Stride, bool IsVolatile, unsigned Rows,
                                  unsigned Columns, const Twine &Name = "") {
    auto *RetType = FixedVectorType::get(EltTy, Rows * Columns);

    Value *Ops[] = {DataPtr, Stride, B.getInt1(IsVolatile), B.getInt32(Rows),
                    B.getInt32(Columns)};
    Type *OverloadedTypes[] = {RetType, Stride->getType()};

    Function *TheFn = Intrinsic::getDeclaration(
        getModule(), Intrinsic::matrix_column_major_load, OverloadedTypes);

    CallInst *Call = B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
    Attribute AlignAttr =
        Attribute::getWithAlignment(Call->getContext(), Alignment);
    Call->addParamAttr(0, AlignAttr);
    return Call;
  }

  /// Create a column major, strided matrix store.
  /// \p Matrix  - Matrix to store
  /// \p Ptr     - Pointer to write back to
  /// \p Stride  - Space between columns
  CallInst *CreateColumnMajorStore(Value *Matrix, Value *Ptr, Align Alignment,
                                   Value *Stride, bool IsVolatile,
                                   unsigned Rows, unsigned Columns,
                                   const Twine &Name = "") {
    Value *Ops[] = {Matrix,           Ptr,
                    Stride,           B.getInt1(IsVolatile),
                    B.getInt32(Rows), B.getInt32(Columns)};
    Type *OverloadedTypes[] = {Matrix->getType(), Stride->getType()};

    Function *TheFn = Intrinsic::getDeclaration(
        getModule(), Intrinsic::matrix_column_major_store, OverloadedTypes);

    CallInst *Call = B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
    Attribute AlignAttr =
        Attribute::getWithAlignment(Call->getContext(), Alignment);
    Call->addParamAttr(1, AlignAttr);
    return Call;
  }

  /// Create a llvm.matrix.transpose call, transposing \p Matrix with \p Rows
  /// rows and \p Columns columns.
  CallInst *CreateMatrixTranspose(Value *Matrix, unsigned Rows,
                                  unsigned Columns, const Twine &Name = "") {
    auto *OpType = cast<VectorType>(Matrix->getType());
    auto *ReturnType =
        FixedVectorType::get(OpType->getElementType(), Rows * Columns);

    Type *OverloadedTypes[] = {ReturnType};
    Value *Ops[] = {Matrix, B.getInt32(Rows), B.getInt32(Columns)};
    Function *TheFn = Intrinsic::getDeclaration(
        getModule(), Intrinsic::matrix_transpose, OverloadedTypes);

    return B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
  }

  /// Create a llvm.matrix.multiply call, multiplying matrixes \p LHS and \p
  /// RHS.
  CallInst *CreateMatrixMultiply(Value *LHS, Value *RHS, unsigned LHSRows,
                                 unsigned LHSColumns, unsigned RHSColumns,
                                 const Twine &Name = "") {
    auto *LHSType = cast<VectorType>(LHS->getType());
    auto *RHSType = cast<VectorType>(RHS->getType());

    auto *ReturnType =
        FixedVectorType::get(LHSType->getElementType(), LHSRows * RHSColumns);

    Value *Ops[] = {LHS, RHS, B.getInt32(LHSRows), B.getInt32(LHSColumns),
                    B.getInt32(RHSColumns)};
    Type *OverloadedTypes[] = {ReturnType, LHSType, RHSType};

    Function *TheFn = Intrinsic::getDeclaration(
        getModule(), Intrinsic::matrix_multiply, OverloadedTypes);
    return B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
  }

  /// Insert a single element \p NewVal into \p Matrix at indices (\p RowIdx, \p
  /// ColumnIdx).
  Value *CreateMatrixInsert(Value *Matrix, Value *NewVal, Value *RowIdx,
                            Value *ColumnIdx, unsigned NumRows) {
    return B.CreateInsertElement(
        Matrix, NewVal,
        B.CreateAdd(B.CreateMul(ColumnIdx, ConstantInt::get(
                                               ColumnIdx->getType(), NumRows)),
                    RowIdx));
  }

  /// Add matrixes \p LHS and \p RHS. Support both integer and floating point
  /// matrixes.
  Value *CreateAdd(Value *LHS, Value *RHS) {
    assert(LHS->getType()->isVectorTy() || RHS->getType()->isVectorTy());
    if (LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(LHS->getType()) &&
             "LHS Assumed to be fixed width");
      RHS = B.CreateVectorSplat(
          cast<VectorType>(LHS->getType())->getElementCount(), RHS,
          "scalar.splat");
    } else if (!LHS->getType()->isVectorTy() && RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(RHS->getType()) &&
             "RHS Assumed to be fixed width");
      LHS = B.CreateVectorSplat(
          cast<VectorType>(RHS->getType())->getElementCount(), LHS,
          "scalar.splat");
    }

    return cast<VectorType>(LHS->getType())
                   ->getElementType()
                   ->isFloatingPointTy()
               ? B.CreateFAdd(LHS, RHS)
               : B.CreateAdd(LHS, RHS);
  }

  /// Subtract matrixes \p LHS and \p RHS. Support both integer and floating
  /// point matrixes.
  Value *CreateSub(Value *LHS, Value *RHS) {
    assert(LHS->getType()->isVectorTy() || RHS->getType()->isVectorTy());
    if (LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(LHS->getType()) &&
             "LHS Assumed to be fixed width");
      RHS = B.CreateVectorSplat(
          cast<VectorType>(LHS->getType())->getElementCount(), RHS,
          "scalar.splat");
    } else if (!LHS->getType()->isVectorTy() && RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(RHS->getType()) &&
             "RHS Assumed to be fixed width");
      LHS = B.CreateVectorSplat(
          cast<VectorType>(RHS->getType())->getElementCount(), LHS,
          "scalar.splat");
    }

    return cast<VectorType>(LHS->getType())
                   ->getElementType()
                   ->isFloatingPointTy()
               ? B.CreateFSub(LHS, RHS)
               : B.CreateSub(LHS, RHS);
  }

  /// Multiply matrix \p LHS with scalar \p RHS or scalar \p LHS with matrix \p
  /// RHS.
  Value *CreateScalarMultiply(Value *LHS, Value *RHS) {
    std::tie(LHS, RHS) = splatScalarOperandIfNeeded(LHS, RHS);
    if (LHS->getType()->getScalarType()->isFloatingPointTy())
      return B.CreateFMul(LHS, RHS);
    return B.CreateMul(LHS, RHS);
  }

  /// Divide matrix \p LHS by scalar \p RHS. If the operands are integers, \p
  /// IsUnsigned indicates whether UDiv or SDiv should be used.
  Value *CreateScalarDiv(Value *LHS, Value *RHS, bool IsUnsigned) {
    assert(LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy());
    assert(!isa<ScalableVectorType>(LHS->getType()) &&
           "LHS Assumed to be fixed width");
    RHS =
        B.CreateVectorSplat(cast<VectorType>(LHS->getType())->getElementCount(),
                            RHS, "scalar.splat");
    return cast<VectorType>(LHS->getType())
                   ->getElementType()
                   ->isFloatingPointTy()
               ? B.CreateFDiv(LHS, RHS)
               : (IsUnsigned ? B.CreateUDiv(LHS, RHS) : B.CreateSDiv(LHS, RHS));
  }

  /// Create an assumption that \p Idx is less than \p NumElements.
  void CreateIndexAssumption(Value *Idx, unsigned NumElements,
                             Twine const &Name = "") {
    Value *NumElts =
        B.getIntN(Idx->getType()->getScalarSizeInBits(), NumElements);
    auto *Cmp = B.CreateICmpULT(Idx, NumElts);
    if (isa<ConstantInt>(Cmp))
      assert(cast<ConstantInt>(Cmp)->isOne() && "Index must be valid!");
    else
      B.CreateAssumption(Cmp);
  }

  /// Compute the index to access the element at (\p RowIdx, \p ColumnIdx) from
  /// a matrix with \p NumRows embedded in a vector.
  Value *CreateIndex(Value *RowIdx, Value *ColumnIdx, unsigned NumRows,
                     Twine const &Name = "") {
    unsigned MaxWidth = std::max(RowIdx->getType()->getScalarSizeInBits(),
                                 ColumnIdx->getType()->getScalarSizeInBits());
    Type *IntTy = IntegerType::get(RowIdx->getType()->getContext(), MaxWidth);
    RowIdx = B.CreateZExt(RowIdx, IntTy);
    ColumnIdx = B.CreateZExt(ColumnIdx, IntTy);
    Value *NumRowsV = B.getIntN(MaxWidth, NumRows);
    return B.CreateAdd(B.CreateMul(ColumnIdx, NumRowsV), RowIdx);
  }
};

} // end namespace llvm

#endif // LLVM_IR_MATRIXBUILDER_H
