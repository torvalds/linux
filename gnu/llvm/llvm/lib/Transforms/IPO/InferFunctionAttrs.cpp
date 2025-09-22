//===- InferFunctionAttrs.cpp - Infer implicit function attributes --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/Transforms/Utils/Local.h"
using namespace llvm;

#define DEBUG_TYPE "inferattrs"

static bool inferAllPrototypeAttributes(
    Module &M, function_ref<TargetLibraryInfo &(Function &)> GetTLI) {
  bool Changed = false;

  for (Function &F : M.functions())
    // We only infer things using the prototype and the name; we don't need
    // definitions.  This ensures libfuncs are annotated and also allows our
    // CGSCC inference to avoid needing to duplicate the inference from other
    // attribute logic on all calls to declarations (as declarations aren't
    // explicitly visited by CGSCC passes in the new pass manager.)
    if (F.isDeclaration() && !F.hasOptNone()) {
      if (!F.hasFnAttribute(Attribute::NoBuiltin))
        Changed |= inferNonMandatoryLibFuncAttrs(F, GetTLI(F));
      Changed |= inferAttributesFromOthers(F);
    }

  return Changed;
}

PreservedAnalyses InferFunctionAttrsPass::run(Module &M,
                                              ModuleAnalysisManager &AM) {
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto GetTLI = [&FAM](Function &F) -> TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };

  if (!inferAllPrototypeAttributes(M, GetTLI))
    // If we didn't infer anything, preserve all analyses.
    return PreservedAnalyses::all();

  // Otherwise, we may have changed fundamental function attributes, so clear
  // out all the passes.
  return PreservedAnalyses::none();
}
