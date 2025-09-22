//===-- llvm/CodeGen/LowLevelTypeUtils.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file implements the more header-heavy bits of the LLT class to
/// avoid polluting users' namespaces.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
using namespace llvm;

LLT llvm::getLLTForType(Type &Ty, const DataLayout &DL) {
  if (auto VTy = dyn_cast<VectorType>(&Ty)) {
    auto EC = VTy->getElementCount();
    LLT ScalarTy = getLLTForType(*VTy->getElementType(), DL);
    if (EC.isScalar())
      return ScalarTy;
    return LLT::vector(EC, ScalarTy);
  }

  if (auto PTy = dyn_cast<PointerType>(&Ty)) {
    unsigned AddrSpace = PTy->getAddressSpace();
    return LLT::pointer(AddrSpace, DL.getPointerSizeInBits(AddrSpace));
  }

  if (Ty.isSized() && !Ty.isScalableTargetExtTy()) {
    // Aggregates are no different from real scalars as far as GlobalISel is
    // concerned.
    auto SizeInBits = DL.getTypeSizeInBits(&Ty);
    assert(SizeInBits != 0 && "invalid zero-sized type");
    return LLT::scalar(SizeInBits);
  }

  if (Ty.isTokenTy())
    return LLT::token();

  return LLT();
}

MVT llvm::getMVTForLLT(LLT Ty) {
  if (!Ty.isVector())
    return MVT::getIntegerVT(Ty.getSizeInBits());

  return MVT::getVectorVT(
      MVT::getIntegerVT(Ty.getElementType().getSizeInBits()),
      Ty.getElementCount());
}

EVT llvm::getApproximateEVTForLLT(LLT Ty, const DataLayout &DL,
                                  LLVMContext &Ctx) {
  if (Ty.isVector()) {
    EVT EltVT = getApproximateEVTForLLT(Ty.getElementType(), DL, Ctx);
    return EVT::getVectorVT(Ctx, EltVT, Ty.getElementCount());
  }

  return EVT::getIntegerVT(Ctx, Ty.getSizeInBits());
}

LLT llvm::getLLTForMVT(MVT Ty) {
  if (!Ty.isVector())
    return LLT::scalar(Ty.getSizeInBits());

  return LLT::scalarOrVector(Ty.getVectorElementCount(),
                             Ty.getVectorElementType().getSizeInBits());
}

const llvm::fltSemantics &llvm::getFltSemanticForLLT(LLT Ty) {
  assert(Ty.isScalar() && "Expected a scalar type.");
  switch (Ty.getSizeInBits()) {
  case 16:
    return APFloat::IEEEhalf();
  case 32:
    return APFloat::IEEEsingle();
  case 64:
    return APFloat::IEEEdouble();
  case 128:
    return APFloat::IEEEquad();
  }
  llvm_unreachable("Invalid FP type size.");
}
