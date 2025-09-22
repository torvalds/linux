//===- HLSLResource.cpp - HLSL Resource helper objects --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains helper objects for working with HLSL Resources.
///
//===----------------------------------------------------------------------===//

#include "llvm/Frontend/HLSL/HLSLResource.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace llvm::hlsl;

GlobalVariable *FrontendResource::getGlobalVariable() {
  return cast<GlobalVariable>(
      cast<ConstantAsMetadata>(Entry->getOperand(0))->getValue());
}

ResourceKind FrontendResource::getResourceKind() {
  return static_cast<ResourceKind>(
      cast<ConstantInt>(
          cast<ConstantAsMetadata>(Entry->getOperand(1))->getValue())
          ->getLimitedValue());
}
ElementType FrontendResource::getElementType() {
  return static_cast<ElementType>(
      cast<ConstantInt>(
          cast<ConstantAsMetadata>(Entry->getOperand(2))->getValue())
          ->getLimitedValue());
}
bool FrontendResource::getIsROV() {
  return cast<ConstantInt>(
             cast<ConstantAsMetadata>(Entry->getOperand(3))->getValue())
      ->getLimitedValue();
}
uint32_t FrontendResource::getResourceIndex() {
  return cast<ConstantInt>(
             cast<ConstantAsMetadata>(Entry->getOperand(4))->getValue())
      ->getLimitedValue();
}
uint32_t FrontendResource::getSpace() {
  return cast<ConstantInt>(
             cast<ConstantAsMetadata>(Entry->getOperand(5))->getValue())
      ->getLimitedValue();
}

FrontendResource::FrontendResource(MDNode *E) : Entry(E) {
  assert(Entry->getNumOperands() == 6 && "Unexpected metadata shape");
}

FrontendResource::FrontendResource(GlobalVariable *GV, ResourceKind RK,
                                   ElementType ElTy, bool IsROV,
                                   uint32_t ResIndex, uint32_t Space) {
  auto &Ctx = GV->getContext();
  IRBuilder<> B(Ctx);
  Entry = MDNode::get(
      Ctx, {ValueAsMetadata::get(GV),
            ConstantAsMetadata::get(B.getInt32(static_cast<int>(RK))),
            ConstantAsMetadata::get(B.getInt32(static_cast<int>(ElTy))),
            ConstantAsMetadata::get(B.getInt1(IsROV)),
            ConstantAsMetadata::get(B.getInt32(ResIndex)),
            ConstantAsMetadata::get(B.getInt32(Space))});
}
