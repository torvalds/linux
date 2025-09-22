//===- ReduceSpecialGlobals.cpp - Specialized Delta Pass ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce special globals, like @llvm.used, in the provided Module.
//
// For more details about special globals, see
// https://llvm.org/docs/LangRef.html#intrinsic-global-variables
//
//===----------------------------------------------------------------------===//

#include "ReduceSpecialGlobals.h"
#include "Delta.h"
#include "Utils.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"

using namespace llvm;

static StringRef SpecialGlobalNames[] = {"llvm.used", "llvm.compiler.used"};

/// Removes all special globals aren't inside any of the
/// desired Chunks.
static void extractSpecialGlobalsFromModule(Oracle &O,
                                            ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();

  for (StringRef Name : SpecialGlobalNames) {
    if (auto *Used = Program.getNamedGlobal(Name)) {
      Used->replaceAllUsesWith(getDefaultValue(Used->getType()));
      Used->eraseFromParent();
    }
  }
}

void llvm::reduceSpecialGlobalsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractSpecialGlobalsFromModule,
               "Reducing Special Globals");
}
