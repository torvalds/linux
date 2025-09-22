//===- LowerIFunc.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements replacing calls to ifuncs by introducing indirect calls.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LowerIFunc.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

/// Replace all call users of ifuncs in the module.
PreservedAnalyses LowerIFuncPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (M.ifunc_empty())
    return PreservedAnalyses::all();

  lowerGlobalIFuncUsersAsGlobalCtor(M, {});
  return PreservedAnalyses::none();
}
