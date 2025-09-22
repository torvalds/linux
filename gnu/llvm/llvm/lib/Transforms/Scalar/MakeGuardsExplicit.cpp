//===- MakeGuardsExplicit.cpp - Turn guard intrinsics into guard branches -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the @llvm.experimental.guard intrinsic to the new form of
// guard represented as widenable explicit branch to the deopt block. The
// difference between this pass and LowerGuardIntrinsic is that after this pass
// the guard represented as intrinsic:
//
//   call void(i1, ...) @llvm.experimental.guard(i1 %old_cond) [ "deopt"() ]
//
// transforms to a guard represented as widenable explicit branch:
//
//   %widenable_cond = call i1 @llvm.experimental.widenable.condition()
//   br i1 (%old_cond & %widenable_cond), label %guarded, label %deopt
//
// Here:
//   - The semantics of @llvm.experimental.widenable.condition allows to replace
//     %widenable_cond with the construction (%widenable_cond & %any_other_cond)
//     without loss of correctness;
//   - %guarded is the lower part of old guard intrinsic's parent block split by
//     the intrinsic call;
//   - %deopt is a block containing a sole call to @llvm.experimental.deoptimize
//     intrinsic.
//
// Therefore, this branch preserves the property of widenability.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/MakeGuardsExplicit.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/GuardUtils.h"

using namespace llvm;

static void turnToExplicitForm(CallInst *Guard, Function *DeoptIntrinsic) {
  // Replace the guard with an explicit branch (just like in GuardWidening).
  BasicBlock *OriginalBB = Guard->getParent();
  (void)OriginalBB;
  makeGuardControlFlowExplicit(DeoptIntrinsic, Guard, true);
  assert(isWidenableBranch(OriginalBB->getTerminator()) && "should hold");

  Guard->eraseFromParent();
}

static bool explicifyGuards(Function &F) {
  // Check if we can cheaply rule out the possibility of not having any work to
  // do.
  auto *GuardDecl = F.getParent()->getFunction(
      Intrinsic::getName(Intrinsic::experimental_guard));
  if (!GuardDecl || GuardDecl->use_empty())
    return false;

  SmallVector<CallInst *, 8> GuardIntrinsics;
  for (auto &I : instructions(F))
    if (isGuard(&I))
      GuardIntrinsics.push_back(cast<CallInst>(&I));

  if (GuardIntrinsics.empty())
    return false;

  auto *DeoptIntrinsic = Intrinsic::getDeclaration(
      F.getParent(), Intrinsic::experimental_deoptimize, {F.getReturnType()});
  DeoptIntrinsic->setCallingConv(GuardDecl->getCallingConv());

  for (auto *Guard : GuardIntrinsics)
    turnToExplicitForm(Guard, DeoptIntrinsic);

  return true;
}

PreservedAnalyses MakeGuardsExplicitPass::run(Function &F,
                                           FunctionAnalysisManager &) {
  if (explicifyGuards(F))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
