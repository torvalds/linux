//===-- ValueLatticeUtils.cpp - Utils for solving lattices ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements common functions useful for performing data-flow
// analyses that propagate values across function boundaries.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ValueLatticeUtils.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
using namespace llvm;

bool llvm::canTrackArgumentsInterprocedurally(Function *F) {
  return F->hasLocalLinkage() && !F->hasAddressTaken();
}

bool llvm::canTrackReturnsInterprocedurally(Function *F) {
  return F->hasExactDefinition() && !F->hasFnAttribute(Attribute::Naked);
}

bool llvm::canTrackGlobalVariableInterprocedurally(GlobalVariable *GV) {
  if (GV->isConstant() || !GV->hasLocalLinkage() ||
      !GV->hasDefinitiveInitializer())
    return false;
  return all_of(GV->users(), [&](User *U) {
    // Currently all users of a global variable have to be non-volatile loads
    // or stores of the global type, and the global cannot be stored itself.
    if (auto *Store = dyn_cast<StoreInst>(U))
      return Store->getValueOperand() != GV && !Store->isVolatile() &&
             Store->getValueOperand()->getType() == GV->getValueType();
    if (auto *Load = dyn_cast<LoadInst>(U))
      return !Load->isVolatile() && Load->getType() == GV->getValueType();

    return false;
  });
}
