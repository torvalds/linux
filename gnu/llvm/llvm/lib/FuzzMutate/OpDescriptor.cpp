//===-- OpDescriptor.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace fuzzerop;

static cl::opt<bool> UseUndef("use-undef",
                              cl::desc("Use undef when generating programs."),
                              cl::init(false));

void fuzzerop::makeConstantsWithType(Type *T, std::vector<Constant *> &Cs) {
  if (auto *IntTy = dyn_cast<IntegerType>(T)) {
    uint64_t W = IntTy->getBitWidth();
    Cs.push_back(ConstantInt::get(IntTy, 0));
    Cs.push_back(ConstantInt::get(IntTy, 1));
    Cs.push_back(ConstantInt::get(IntTy, 42));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getMaxValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getMinValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getSignedMaxValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getSignedMinValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getOneBitSet(W, W / 2)));
  } else if (T->isFloatingPointTy()) {
    auto &Ctx = T->getContext();
    auto &Sem = T->getFltSemantics();
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getZero(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat(Sem, 1)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat(Sem, 42)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getLargest(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getSmallest(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getInf(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getNaN(Sem)));
  } else if (VectorType *VecTy = dyn_cast<VectorType>(T)) {
    std::vector<Constant *> EleCs;
    Type *EltTy = VecTy->getElementType();
    makeConstantsWithType(EltTy, EleCs);
    ElementCount EC = VecTy->getElementCount();
    for (Constant *Elt : EleCs) {
      Cs.push_back(ConstantVector::getSplat(EC, Elt));
    }
  } else {
    if (UseUndef)
      Cs.push_back(UndefValue::get(T));
    Cs.push_back(PoisonValue::get(T));
  }
}

std::vector<Constant *> fuzzerop::makeConstantsWithType(Type *T) {
  std::vector<Constant *> Result;
  makeConstantsWithType(T, Result);
  return Result;
}
