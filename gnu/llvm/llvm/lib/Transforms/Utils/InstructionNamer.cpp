//===- InstructionNamer.cpp - Give anonymous instructions names -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a little utility pass that gives instructions names, this is mostly
// useful when diffing the effect of an optimization because deleting an
// unnamed instruction can change all other instruction numbering, making the
// diff very noisy.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/InstructionNamer.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"

using namespace llvm;

namespace {
void nameInstructions(Function &F) {
  for (auto &Arg : F.args()) {
    if (!Arg.hasName())
      Arg.setName("arg");
  }

  for (BasicBlock &BB : F) {
    if (!BB.hasName())
      BB.setName("bb");

    for (Instruction &I : BB) {
      if (!I.hasName() && !I.getType()->isVoidTy())
        I.setName("i");
    }
  }
}

} // namespace

PreservedAnalyses InstructionNamerPass::run(Function &F,
                                            FunctionAnalysisManager &FAM) {
  nameInstructions(F);
  return PreservedAnalyses::all();
}
