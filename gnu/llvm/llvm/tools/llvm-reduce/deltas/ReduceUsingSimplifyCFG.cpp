//===- ReduceUsingSimplifyCFG.h - Specialized Delta Pass ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to call SimplifyCFG on individual basic blocks.
//
//===----------------------------------------------------------------------===//

#include "ReduceUsingSimplifyCFG.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

static void reduceUsingSimplifyCFG(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();
  SmallVector<BasicBlock *, 16> ToSimplify;
  for (auto &F : Program)
    for (auto &BB : F)
      if (!O.shouldKeep())
        ToSimplify.push_back(&BB);
  TargetTransformInfo TTI(Program.getDataLayout());
  for (auto *BB : ToSimplify)
    simplifyCFG(BB, TTI);
}

void llvm::reduceUsingSimplifyCFGDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceUsingSimplifyCFG, "Reducing using SimplifyCFG");
}
static void reduceConditionals(Oracle &O, ReducerWorkItem &WorkItem,
                               bool Direction) {
  Module &M = WorkItem.getModule();
  SmallVector<BasicBlock *, 16> ToSimplify;

  for (auto &F : M) {
    for (auto &BB : F) {
      auto *BR = dyn_cast<BranchInst>(BB.getTerminator());
      if (!BR || !BR->isConditional() || O.shouldKeep())
        continue;

      if (Direction)
        BR->setCondition(ConstantInt::getTrue(BR->getContext()));
      else
        BR->setCondition(ConstantInt::getFalse(BR->getContext()));

      ToSimplify.push_back(&BB);
    }
  }

  TargetTransformInfo TTI(M.getDataLayout());
  for (auto *BB : ToSimplify)
    simplifyCFG(BB, TTI);
}

void llvm::reduceConditionalsTrueDeltaPass(TestRunner &Test) {
  runDeltaPass(
      Test,
      [](Oracle &O, ReducerWorkItem &WorkItem) {
        reduceConditionals(O, WorkItem, true);
      },
      "Reducing conditional branches to true");
}

void llvm::reduceConditionalsFalseDeltaPass(TestRunner &Test) {
  runDeltaPass(
      Test,
      [](Oracle &O, ReducerWorkItem &WorkItem) {
        reduceConditionals(O, WorkItem, false);
      },
      "Reducing conditional branches to false");
}
