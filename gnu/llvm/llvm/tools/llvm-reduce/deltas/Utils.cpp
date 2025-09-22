//===- Utils.cpp - llvm-reduce utility functions --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some utility functions supporting llvm-reduce.
//
//===----------------------------------------------------------------------===//

#include "Utils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"

using namespace llvm;

extern cl::OptionCategory LLVMReduceOptions;

cl::opt<bool> llvm::Verbose("verbose",
                            cl::desc("Print extra debugging information"),
                            cl::init(false), cl::cat(LLVMReduceOptions));

Value *llvm::getDefaultValue(Type *T) {
  return T->isVoidTy() ? PoisonValue::get(T) : Constant::getNullValue(T);
}

bool llvm::hasAliasUse(Function &F) {
  return any_of(F.users(), [](User *U) {
      return isa<GlobalAlias>(U) || isa<GlobalIFunc>(U);
    });
}

bool llvm::hasAliasOrBlockAddressUse(Function &F) {
  return any_of(F.users(), [](User *U) {
    return isa<GlobalAlias, GlobalIFunc, BlockAddress>(U);
  });
}
