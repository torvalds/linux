//===- SSAContext.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a specialization of the GenericSSAContext<X>
/// template class for LLVM IR.
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/SSAContext.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

template <>
void SSAContext::appendBlockDefs(SmallVectorImpl<Value *> &defs,
                                 BasicBlock &block) {
  for (auto &instr : block) {
    if (instr.isTerminator())
      break;
    defs.push_back(&instr);
  }
}

template <>
void SSAContext::appendBlockDefs(SmallVectorImpl<const Value *> &defs,
                                 const BasicBlock &block) {
  for (auto &instr : block) {
    if (instr.isTerminator())
      break;
    defs.push_back(&instr);
  }
}

template <>
void SSAContext::appendBlockTerms(SmallVectorImpl<Instruction *> &terms,
                                  BasicBlock &block) {
  terms.push_back(block.getTerminator());
}

template <>
void SSAContext::appendBlockTerms(SmallVectorImpl<const Instruction *> &terms,
                                  const BasicBlock &block) {
  terms.push_back(block.getTerminator());
}

template <>
const BasicBlock *SSAContext::getDefBlock(const Value *value) const {
  if (const auto *instruction = dyn_cast<Instruction>(value))
    return instruction->getParent();
  return nullptr;
}

template <>
bool SSAContext::isConstantOrUndefValuePhi(const Instruction &Instr) {
  if (auto *Phi = dyn_cast<PHINode>(&Instr))
    return Phi->hasConstantOrUndefValue();
  return false;
}

template <> Intrinsic::ID SSAContext::getIntrinsicID(const Instruction &I) {
  if (auto *CB = dyn_cast<CallBase>(&I))
    return CB->getIntrinsicID();
  return Intrinsic::not_intrinsic;
}

template <> Printable SSAContext::print(const Value *V) const {
  return Printable([V](raw_ostream &Out) { V->print(Out); });
}

template <> Printable SSAContext::print(const Instruction *Inst) const {
  return print(cast<Value>(Inst));
}

template <> Printable SSAContext::print(const BasicBlock *BB) const {
  if (!BB)
    return Printable([](raw_ostream &Out) { Out << "<nullptr>"; });
  if (BB->hasName())
    return Printable([BB](raw_ostream &Out) { Out << BB->getName(); });

  return Printable([BB](raw_ostream &Out) {
    ModuleSlotTracker MST{BB->getParent()->getParent(), false};
    MST.incorporateFunction(*BB->getParent());
    Out << MST.getLocalSlot(BB);
  });
}

template <> Printable SSAContext::printAsOperand(const BasicBlock *BB) const {
  return Printable([BB](raw_ostream &Out) { BB->printAsOperand(Out); });
}
