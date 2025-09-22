//===--- PatternInit.cpp - Pattern Initialization -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PatternInit.h"
#include "CodeGenModule.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Type.h"

llvm::Constant *clang::CodeGen::initializationPatternFor(CodeGenModule &CGM,
                                                         llvm::Type *Ty) {
  // The following value is a guaranteed unmappable pointer value and has a
  // repeated byte-pattern which makes it easier to synthesize. We use it for
  // pointers as well as integers so that aggregates are likely to be
  // initialized with this repeated value.
  // For 32-bit platforms it's a bit trickier because, across systems, only the
  // zero page can reasonably be expected to be unmapped. We use max 0xFFFFFFFF
  // assuming that memory access will overlap into zero page.
  const uint64_t IntValue =
      CGM.getContext().getTargetInfo().getMaxPointerWidth() < 64
          ? 0xFFFFFFFFFFFFFFFFull
          : 0xAAAAAAAAAAAAAAAAull;
  // Floating-point values are initialized as NaNs because they propagate. Using
  // a repeated byte pattern means that it will be easier to initialize
  // all-floating-point aggregates and arrays with memset. Further, aggregates
  // which mix integral and a few floats might also initialize with memset
  // followed by a handful of stores for the floats. Using fairly unique NaNs
  // also means they'll be easier to distinguish in a crash.
  constexpr bool NegativeNaN = true;
  constexpr uint64_t NaNPayload = 0xFFFFFFFFFFFFFFFFull;
  if (Ty->isIntOrIntVectorTy()) {
    unsigned BitWidth =
        cast<llvm::IntegerType>(Ty->getScalarType())->getBitWidth();
    if (BitWidth <= 64)
      return llvm::ConstantInt::get(Ty, IntValue);
    return llvm::ConstantInt::get(
        Ty, llvm::APInt::getSplat(BitWidth, llvm::APInt(64, IntValue)));
  }
  if (Ty->isPtrOrPtrVectorTy()) {
    auto *PtrTy = cast<llvm::PointerType>(Ty->getScalarType());
    unsigned PtrWidth =
        CGM.getDataLayout().getPointerSizeInBits(PtrTy->getAddressSpace());
    if (PtrWidth > 64)
      llvm_unreachable("pattern initialization of unsupported pointer width");
    llvm::Type *IntTy = llvm::IntegerType::get(CGM.getLLVMContext(), PtrWidth);
    auto *Int = llvm::ConstantInt::get(IntTy, IntValue);
    return llvm::ConstantExpr::getIntToPtr(Int, PtrTy);
  }
  if (Ty->isFPOrFPVectorTy()) {
    unsigned BitWidth = llvm::APFloat::semanticsSizeInBits(
        Ty->getScalarType()->getFltSemantics());
    llvm::APInt Payload(64, NaNPayload);
    if (BitWidth >= 64)
      Payload = llvm::APInt::getSplat(BitWidth, Payload);
    return llvm::ConstantFP::getQNaN(Ty, NegativeNaN, &Payload);
  }
  if (Ty->isArrayTy()) {
    // Note: this doesn't touch tail padding (at the end of an object, before
    // the next array object). It is instead handled by replaceUndef.
    auto *ArrTy = cast<llvm::ArrayType>(Ty);
    llvm::SmallVector<llvm::Constant *, 8> Element(
        ArrTy->getNumElements(),
        initializationPatternFor(CGM, ArrTy->getElementType()));
    return llvm::ConstantArray::get(ArrTy, Element);
  }

  // Note: this doesn't touch struct padding. It will initialize as much union
  // padding as is required for the largest type in the union. Padding is
  // instead handled by replaceUndef. Stores to structs with volatile members
  // don't have a volatile qualifier when initialized according to C++. This is
  // fine because stack-based volatiles don't really have volatile semantics
  // anyways, and the initialization shouldn't be observable.
  auto *StructTy = cast<llvm::StructType>(Ty);
  llvm::SmallVector<llvm::Constant *, 8> Struct(StructTy->getNumElements());
  for (unsigned El = 0; El != Struct.size(); ++El)
    Struct[El] = initializationPatternFor(CGM, StructTy->getElementType(El));
  return llvm::ConstantStruct::get(StructTy, Struct);
}
