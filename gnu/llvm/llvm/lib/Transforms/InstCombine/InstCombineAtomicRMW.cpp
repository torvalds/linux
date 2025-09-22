//===- InstCombineAtomicRMW.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visit functions for atomic rmw instructions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace {
/// Return true if and only if the given instruction does not modify the memory
/// location referenced.  Note that an idemptent atomicrmw may still have
/// ordering effects on nearby instructions, or be volatile.
/// TODO: Common w/ the version in AtomicExpandPass, and change the term used.
/// Idemptotent is confusing in this context.
bool isIdempotentRMW(AtomicRMWInst& RMWI) {
  if (auto CF = dyn_cast<ConstantFP>(RMWI.getValOperand()))
    switch(RMWI.getOperation()) {
    case AtomicRMWInst::FAdd: // -0.0
      return CF->isZero() && CF->isNegative();
    case AtomicRMWInst::FSub: // +0.0
      return CF->isZero() && !CF->isNegative();
    default:
      return false;
    };

  auto C = dyn_cast<ConstantInt>(RMWI.getValOperand());
  if(!C)
    return false;

  switch(RMWI.getOperation()) {
    case AtomicRMWInst::Add:
    case AtomicRMWInst::Sub:
    case AtomicRMWInst::Or:
    case AtomicRMWInst::Xor:
      return C->isZero();
    case AtomicRMWInst::And:
      return C->isMinusOne();
    case AtomicRMWInst::Min:
      return C->isMaxValue(true);
    case AtomicRMWInst::Max:
      return C->isMinValue(true);
    case AtomicRMWInst::UMin:
      return C->isMaxValue(false);
    case AtomicRMWInst::UMax:
      return C->isMinValue(false);
    default:
      return false;
  }
}

/// Return true if the given instruction always produces a value in memory
/// equivalent to its value operand.
bool isSaturating(AtomicRMWInst& RMWI) {
  if (auto CF = dyn_cast<ConstantFP>(RMWI.getValOperand()))
    switch (RMWI.getOperation()) {
    case AtomicRMWInst::FMax:
      // maxnum(x, +inf) -> +inf
      return !CF->isNegative() && CF->isInfinity();
    case AtomicRMWInst::FMin:
      // minnum(x, -inf) -> +inf
      return CF->isNegative() && CF->isInfinity();
    case AtomicRMWInst::FAdd:
    case AtomicRMWInst::FSub:
      return CF->isNaN();
    default:
      return false;
    };

  auto C = dyn_cast<ConstantInt>(RMWI.getValOperand());
  if(!C)
    return false;

  switch(RMWI.getOperation()) {
  default:
    return false;
  case AtomicRMWInst::Xchg:
    return true;
  case AtomicRMWInst::Or:
    return C->isAllOnesValue();
  case AtomicRMWInst::And:
    return C->isZero();
  case AtomicRMWInst::Min:
    return C->isMinValue(true);
  case AtomicRMWInst::Max:
    return C->isMaxValue(true);
  case AtomicRMWInst::UMin:
    return C->isMinValue(false);
  case AtomicRMWInst::UMax:
    return C->isMaxValue(false);
  };
}
} // namespace

Instruction *InstCombinerImpl::visitAtomicRMWInst(AtomicRMWInst &RMWI) {

  // Volatile RMWs perform a load and a store, we cannot replace this by just a
  // load or just a store. We chose not to canonicalize out of general paranoia
  // about user expectations around volatile.
  if (RMWI.isVolatile())
    return nullptr;

  // Any atomicrmw op which produces a known result in memory can be
  // replaced w/an atomicrmw xchg.
  if (isSaturating(RMWI) &&
      RMWI.getOperation() != AtomicRMWInst::Xchg) {
    RMWI.setOperation(AtomicRMWInst::Xchg);
    return &RMWI;
  }

  assert(RMWI.getOrdering() != AtomicOrdering::NotAtomic &&
         RMWI.getOrdering() != AtomicOrdering::Unordered &&
         "AtomicRMWs don't make sense with Unordered or NotAtomic");

  if (!isIdempotentRMW(RMWI))
    return nullptr;

  // We chose to canonicalize all idempotent operations to an single
  // operation code and constant.  This makes it easier for the rest of the
  // optimizer to match easily.  The choices of or w/0 and fadd w/-0.0 are
  // arbitrary.
  if (RMWI.getType()->isIntegerTy() &&
      RMWI.getOperation() != AtomicRMWInst::Or) {
    RMWI.setOperation(AtomicRMWInst::Or);
    return replaceOperand(RMWI, 1, ConstantInt::get(RMWI.getType(), 0));
  } else if (RMWI.getType()->isFloatingPointTy() &&
             RMWI.getOperation() != AtomicRMWInst::FAdd) {
    RMWI.setOperation(AtomicRMWInst::FAdd);
    return replaceOperand(RMWI, 1, ConstantFP::getNegativeZero(RMWI.getType()));
  }

  return nullptr;
}
