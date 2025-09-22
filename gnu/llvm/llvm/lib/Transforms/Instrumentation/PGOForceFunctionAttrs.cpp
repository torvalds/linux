//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/PGOForceFunctionAttrs.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

static bool shouldRunOnFunction(Function &F, ProfileSummaryInfo &PSI,
                                FunctionAnalysisManager &FAM) {
  if (F.isDeclaration())
    return false;
  // Respect existing attributes.
  if (F.hasOptNone() || F.hasOptSize() || F.hasMinSize())
    return false;
  if (F.hasFnAttribute(Attribute::Cold))
    return true;
  if (!PSI.hasProfileSummary())
    return false;
  BlockFrequencyInfo &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
  return PSI.isFunctionColdInCallGraph(&F, BFI);
}

PreservedAnalyses PGOForceFunctionAttrsPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  if (ColdType == PGOOptions::ColdFuncOpt::Default)
    return PreservedAnalyses::all();
  ProfileSummaryInfo &PSI = AM.getResult<ProfileSummaryAnalysis>(M);
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  bool MadeChange = false;
  for (Function &F : M) {
    if (!shouldRunOnFunction(F, PSI, FAM))
      continue;
    switch (ColdType) {
    case PGOOptions::ColdFuncOpt::Default:
      llvm_unreachable("bailed out for default above");
      break;
    case PGOOptions::ColdFuncOpt::OptSize:
      F.addFnAttr(Attribute::OptimizeForSize);
      break;
    case PGOOptions::ColdFuncOpt::MinSize:
      F.addFnAttr(Attribute::MinSize);
      break;
    case PGOOptions::ColdFuncOpt::OptNone:
      // alwaysinline is incompatible with optnone.
      if (F.hasFnAttribute(Attribute::AlwaysInline))
        continue;
      F.addFnAttr(Attribute::OptimizeNone);
      F.addFnAttr(Attribute::NoInline);
      break;
    }
    MadeChange = true;
  }
  return MadeChange ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
