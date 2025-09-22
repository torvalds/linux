//===- ConvergenceVerifier.cpp - Verify convergence control -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ConvergenceVerifier.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GenericConvergenceVerifierImpl.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/SSAContext.h"

using namespace llvm;

template <>
auto GenericConvergenceVerifier<SSAContext>::getConvOp(const Instruction &I)
    -> ConvOpKind {
  const auto *CB = dyn_cast<CallBase>(&I);
  if (!CB)
    return CONV_NONE;
  switch (CB->getIntrinsicID()) {
  default:
    return CONV_NONE;
  case Intrinsic::experimental_convergence_anchor:
    return CONV_ANCHOR;
  case Intrinsic::experimental_convergence_entry:
    return CONV_ENTRY;
  case Intrinsic::experimental_convergence_loop:
    return CONV_LOOP;
  }
}

template <>
void GenericConvergenceVerifier<SSAContext>::checkConvergenceTokenProduced(
    const Instruction &I) {
  return;
}

template <>
const Instruction *
GenericConvergenceVerifier<SSAContext>::findAndCheckConvergenceTokenUsed(
    const Instruction &I) {
  auto *CB = dyn_cast<CallBase>(&I);
  if (!CB)
    return nullptr;

  unsigned Count =
      CB->countOperandBundlesOfType(LLVMContext::OB_convergencectrl);
  CheckOrNull(Count <= 1,
              "The 'convergencectrl' bundle can occur at most once on a call",
              {Context.print(CB)});
  if (!Count)
    return nullptr;

  auto Bundle = CB->getOperandBundle(LLVMContext::OB_convergencectrl);
  CheckOrNull(Bundle->Inputs.size() == 1 &&
                  Bundle->Inputs[0]->getType()->isTokenTy(),
              "The 'convergencectrl' bundle requires exactly one token use.",
              {Context.print(CB)});
  auto *Token = Bundle->Inputs[0].get();
  auto *Def = dyn_cast<Instruction>(Token);

  CheckOrNull(Def && getConvOp(*Def) != CONV_NONE,
              "Convergence control tokens can only be produced by calls to the "
              "convergence control intrinsics.",
              {Context.print(Token), Context.print(&I)});

  if (Def)
    Tokens[&I] = Def;

  return Def;
}

template <>
bool GenericConvergenceVerifier<SSAContext>::isInsideConvergentFunction(
    const Instruction &I) {
  auto *F = I.getFunction();
  return F->isConvergent();
}

template <>
bool GenericConvergenceVerifier<SSAContext>::isConvergent(
    const Instruction &I) {
  if (auto *CB = dyn_cast<CallBase>(&I)) {
    return CB->isConvergent();
  }
  return false;
}

template class llvm::GenericConvergenceVerifier<SSAContext>;
