//===- ReduceFunctions.cpp - Specialized Delta Pass -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce function bodies in the provided Module.
//
//===----------------------------------------------------------------------===//

#include "ReduceFunctionBodies.h"
#include "Delta.h"
#include "Utils.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

/// Removes all the bodies of defined functions that aren't inside any of the
/// desired Chunks.
static void extractFunctionBodiesFromModule(Oracle &O,
                                            ReducerWorkItem &WorkItem) {
  // Delete out-of-chunk function bodies
  for (auto &F : WorkItem.getModule()) {
    if (!F.isDeclaration() && !hasAliasUse(F) && !O.shouldKeep()) {
      F.deleteBody();
      F.setComdat(nullptr);
    }
  }
}

void llvm::reduceFunctionBodiesDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractFunctionBodiesFromModule,
               "Reducing Function Bodies");
}

static void reduceFunctionData(Oracle &O, ReducerWorkItem &WorkItem) {
  for (Function &F : WorkItem.getModule()) {
    if (F.hasPersonalityFn()) {
      if (none_of(F,
                  [](const BasicBlock &BB) {
                    return BB.isEHPad() || isa<ResumeInst>(BB.getTerminator());
                  }) &&
          !O.shouldKeep()) {
        F.setPersonalityFn(nullptr);
      }
    }

    if (F.hasPrefixData() && !O.shouldKeep())
      F.setPrefixData(nullptr);

    if (F.hasPrologueData() && !O.shouldKeep())
      F.setPrologueData(nullptr);
  }
}

void llvm::reduceFunctionDataDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceFunctionData, "Reducing Function Data");
}
