//===- Local.cpp - Functions to perform local transformations -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform various local transformations to the
// program.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Utils/Local.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;

Value *llvm::emitGEPOffset(IRBuilderBase *Builder, const DataLayout &DL,
                           User *GEP, bool NoAssumptions) {
  GEPOperator *GEPOp = cast<GEPOperator>(GEP);
  Type *IntIdxTy = DL.getIndexType(GEP->getType());
  Value *Result = nullptr;

  // nusw implies nsw for the offset arithmetic.
  bool NSW = GEPOp->hasNoUnsignedSignedWrap() && !NoAssumptions;
  bool NUW = GEPOp->hasNoUnsignedWrap() && !NoAssumptions;
  auto AddOffset = [&](Value *Offset) {
    if (Result)
      Result = Builder->CreateAdd(Result, Offset, GEP->getName() + ".offs",
                                  NUW, NSW);
    else
      Result = Offset;
  };

  gep_type_iterator GTI = gep_type_begin(GEP);
  for (User::op_iterator i = GEP->op_begin() + 1, e = GEP->op_end(); i != e;
       ++i, ++GTI) {
    Value *Op = *i;
    if (Constant *OpC = dyn_cast<Constant>(Op)) {
      if (OpC->isZeroValue())
        continue;

      // Handle a struct index, which adds its field offset to the pointer.
      if (StructType *STy = GTI.getStructTypeOrNull()) {
        uint64_t OpValue = OpC->getUniqueInteger().getZExtValue();
        uint64_t Size = DL.getStructLayout(STy)->getElementOffset(OpValue);
        if (!Size)
          continue;

        AddOffset(ConstantInt::get(IntIdxTy, Size));
        continue;
      }
    }

    // Splat the index if needed.
    if (IntIdxTy->isVectorTy() && !Op->getType()->isVectorTy())
      Op = Builder->CreateVectorSplat(
          cast<VectorType>(IntIdxTy)->getElementCount(), Op);

    // Convert to correct type.
    if (Op->getType() != IntIdxTy)
      Op = Builder->CreateIntCast(Op, IntIdxTy, true, Op->getName() + ".c");
    TypeSize TSize = GTI.getSequentialElementStride(DL);
    if (TSize != TypeSize::getFixed(1)) {
      Value *Scale = Builder->CreateTypeSize(IntIdxTy->getScalarType(), TSize);
      if (IntIdxTy->isVectorTy())
        Scale = Builder->CreateVectorSplat(
            cast<VectorType>(IntIdxTy)->getElementCount(), Scale);
      // We'll let instcombine(mul) convert this to a shl if possible.
      Op = Builder->CreateMul(Op, Scale, GEP->getName() + ".idx", NUW, NSW);
    }
    AddOffset(Op);
  }
  return Result ? Result : Constant::getNullValue(IntIdxTy);
}
