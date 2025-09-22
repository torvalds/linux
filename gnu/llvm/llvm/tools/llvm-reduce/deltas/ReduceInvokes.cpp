//===- ReduceInvokes.cpp - Specialized Delta Pass -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Try to replace invokes with calls.
//
//===----------------------------------------------------------------------===//

#include "ReduceInvokes.h"
#include "Delta.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

static void reduceInvokesInFunction(Oracle &O, Function &F) {
  for (BasicBlock &BB : F) {
    InvokeInst *Invoke = dyn_cast<InvokeInst>(BB.getTerminator());
    if (Invoke && !O.shouldKeep())
      changeToCall(Invoke);
  }

  // TODO: We most likely are leaving behind dead landingpad blocks. Should we
  // delete unreachable blocks now, or leave that for the unreachable block
  // reduction.
}

static void reduceInvokesInModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (Function &F : WorkItem.getModule()) {
    if (F.hasPersonalityFn())
      reduceInvokesInFunction(O, F);
  }
}

void llvm::reduceInvokesDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceInvokesInModule, "Reducing Invokes");
}
