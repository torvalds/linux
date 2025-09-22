//===- ReduceInstructions.cpp - Specialized Delta Pass ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting Instructions from defined functions.
//
//===----------------------------------------------------------------------===//

#include "ReduceInstructions.h"
#include "Utils.h"
#include "llvm/IR/Constants.h"
#include <set>

using namespace llvm;

/// Filter out cases where deleting the instruction will likely cause the
/// user/def of the instruction to fail the verifier.
//
// TODO: Technically the verifier only enforces preallocated token usage and
// there is a none token.
static bool shouldAlwaysKeep(const Instruction &I) {
  return I.isEHPad() || I.getType()->isTokenTy() || I.isSwiftError();
}

/// Removes out-of-chunk arguments from functions, and modifies their calls
/// accordingly. It also removes allocations of out-of-chunk arguments.
static void extractInstrFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();
  std::vector<Instruction *> InitInstToKeep;

  for (auto &F : Program)
    for (auto &BB : F) {
      // Removing the terminator would make the block invalid. Only iterate over
      // instructions before the terminator.
      InitInstToKeep.push_back(BB.getTerminator());
      for (auto &Inst : make_range(BB.begin(), std::prev(BB.end()))) {
        if (shouldAlwaysKeep(Inst) || O.shouldKeep())
          InitInstToKeep.push_back(&Inst);
      }
    }

  // We create a vector first, then convert it to a set, so that we don't have
  // to pay the cost of rebalancing the set frequently if the order we insert
  // the elements doesn't match the order they should appear inside the set.
  std::set<Instruction *> InstToKeep(InitInstToKeep.begin(),
                                     InitInstToKeep.end());

  std::vector<Instruction *> InstToDelete;
  for (auto &F : Program)
    for (auto &BB : F)
      for (auto &Inst : BB)
        if (!InstToKeep.count(&Inst)) {
          Inst.replaceAllUsesWith(getDefaultValue(Inst.getType()));
          InstToDelete.push_back(&Inst);
        }

  for (auto &I : InstToDelete)
    I->eraseFromParent();
}

void llvm::reduceInstructionsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractInstrFromModule, "Reducing Instructions");
}
