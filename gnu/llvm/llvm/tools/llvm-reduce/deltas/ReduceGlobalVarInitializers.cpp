//===- ReduceGlobalVars.cpp - Specialized Delta Pass ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce initializers of Global Variables in the provided Module.
//
//===----------------------------------------------------------------------===//

#include "ReduceGlobalVarInitializers.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"

using namespace llvm;

/// Removes all the Initialized GVs that aren't inside the desired Chunks.
static void extractGVsFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  // Drop initializers of out-of-chunk GVs
  for (auto &GV : WorkItem.getModule().globals())
    if (GV.hasInitializer() && !O.shouldKeep()) {
      GV.setInitializer(nullptr);
      GV.setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);
      GV.setComdat(nullptr);
    }
}

void llvm::reduceGlobalsInitializersDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractGVsFromModule, "Reducing GV Initializers");
}
