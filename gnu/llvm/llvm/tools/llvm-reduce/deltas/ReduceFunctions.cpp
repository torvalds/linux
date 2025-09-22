//===- ReduceFunctions.cpp - Specialized Delta Pass -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce functions (and any instruction that calls it) in the provided
// Module.
//
//===----------------------------------------------------------------------===//

#include "ReduceFunctions.h"
#include "Delta.h"
#include "Utils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <iterator>

using namespace llvm;

/// Removes all the Defined Functions
/// that aren't inside any of the desired Chunks.
static void extractFunctionsFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();

  // Record all out-of-chunk functions.
  SmallPtrSet<Constant *, 8> FuncsToRemove;
  for (Function &F : Program.functions()) {
    // Intrinsics don't have function bodies that are useful to
    // reduce. Additionally, intrinsics may have additional operand
    // constraints. But, do drop intrinsics that are not referenced.
    if ((!F.isIntrinsic() || F.use_empty()) && !hasAliasOrBlockAddressUse(F) &&
        !O.shouldKeep())
      FuncsToRemove.insert(&F);
  }

  removeFromUsedLists(Program, [&FuncsToRemove](Constant *C) {
    return FuncsToRemove.count(C);
  });

  // Then, drop body of each of them. We want to batch this and do nothing else
  // here so that minimal number of remaining exteranal uses will remain.
  for (Constant *F : FuncsToRemove)
    F->dropAllReferences();

  // And finally, we can actually delete them.
  for (Constant *F : FuncsToRemove) {
    // Replace all *still* remaining uses with the default value.
    F->replaceAllUsesWith(getDefaultValue(F->getType()));
    // And finally, fully drop it.
    cast<Function>(F)->eraseFromParent();
  }
}

void llvm::reduceFunctionsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractFunctionsFromModule, "Reducing Functions");
}
