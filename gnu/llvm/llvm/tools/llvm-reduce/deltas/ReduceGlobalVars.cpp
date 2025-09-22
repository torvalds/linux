//===- ReduceGlobalVars.cpp - Specialized Delta Pass ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce Global Variables in the provided Module.
//
//===----------------------------------------------------------------------===//

#include "ReduceGlobalVars.h"
#include "Utils.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

static bool shouldAlwaysKeep(const GlobalVariable &GV) {
  return GV.getName() == "llvm.used" || GV.getName() == "llvm.compiler.used";
}

/// Removes all the GVs that aren't inside the desired Chunks.
static void extractGVsFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();

  // Get GVs inside desired chunks
  std::vector<Constant *> InitGVsToKeep;
  for (auto &GV : Program.globals()) {
    if (shouldAlwaysKeep(GV) || O.shouldKeep())
      InitGVsToKeep.push_back(&GV);
  }

  // We create a vector first, then convert it to a set, so that we don't have
  // to pay the cost of rebalancing the set frequently if the order we insert
  // the elements doesn't match the order they should appear inside the set.
  DenseSet<Constant *> GVsToKeep(InitGVsToKeep.begin(), InitGVsToKeep.end());

  // Delete out-of-chunk GVs and their uses
  DenseSet<Constant *> ToRemove;
  for (auto &GV : Program.globals()) {
    if (!GVsToKeep.count(&GV))
      ToRemove.insert(&GV);
  }

  removeFromUsedLists(Program,
                      [&ToRemove](Constant *C) { return ToRemove.count(C); });

  for (auto *GV : ToRemove) {
    GV->replaceAllUsesWith(getDefaultValue(GV->getType()));
    cast<GlobalVariable>(GV)->eraseFromParent();
  }
}

void llvm::reduceGlobalsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractGVsFromModule, "Reducing GlobalVariables");
}
