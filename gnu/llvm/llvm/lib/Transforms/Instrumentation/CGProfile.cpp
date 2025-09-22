//===-- CGProfile.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/CGProfile.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Transforms/Instrumentation.h"
#include <optional>

using namespace llvm;

static bool
addModuleFlags(Module &M,
               MapVector<std::pair<Function *, Function *>, uint64_t> &Counts) {
  if (Counts.empty())
    return false;

  LLVMContext &Context = M.getContext();
  MDBuilder MDB(Context);
  std::vector<Metadata *> Nodes;

  for (auto E : Counts) {
    Metadata *Vals[] = {ValueAsMetadata::get(E.first.first),
                        ValueAsMetadata::get(E.first.second),
                        MDB.createConstant(ConstantInt::get(
                            Type::getInt64Ty(Context), E.second))};
    Nodes.push_back(MDNode::get(Context, Vals));
  }

  M.addModuleFlag(Module::Append, "CG Profile",
                  MDTuple::getDistinct(Context, Nodes));
  return true;
}

static bool runCGProfilePass(Module &M, FunctionAnalysisManager &FAM,
                             bool InLTO) {
  MapVector<std::pair<Function *, Function *>, uint64_t> Counts;
  InstrProfSymtab Symtab;
  auto UpdateCounts = [&](TargetTransformInfo &TTI, Function *F,
                          Function *CalledF, uint64_t NewCount) {
    if (NewCount == 0)
      return;
    if (!CalledF || !TTI.isLoweredToCall(CalledF) ||
        CalledF->hasDLLImportStorageClass())
      return;
    uint64_t &Count = Counts[std::make_pair(F, CalledF)];
    Count = SaturatingAdd(Count, NewCount);
  };
  // Ignore error here.  Indirect calls are ignored if this fails.
  (void)(bool)Symtab.create(M, InLTO);
  for (auto &F : M) {
    // Avoid extra cost of running passes for BFI when the function doesn't have
    // entry count.
    if (F.isDeclaration() || !F.getEntryCount())
      continue;
    auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
    if (BFI.getEntryFreq() == BlockFrequency(0))
      continue;
    TargetTransformInfo &TTI = FAM.getResult<TargetIRAnalysis>(F);
    for (auto &BB : F) {
      std::optional<uint64_t> BBCount = BFI.getBlockProfileCount(&BB);
      if (!BBCount)
        continue;
      for (auto &I : BB) {
        CallBase *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;
        if (CB->isIndirectCall()) {
          uint64_t TotalC;
          auto ValueData =
              getValueProfDataFromInst(*CB, IPVK_IndirectCallTarget, 8, TotalC);
          for (const auto &VD : ValueData)
            UpdateCounts(TTI, &F, Symtab.getFunction(VD.Value), VD.Count);
          continue;
        }
        UpdateCounts(TTI, &F, CB->getCalledFunction(), *BBCount);
      }
    }
  }

  return addModuleFlags(M, Counts);
}

PreservedAnalyses CGProfilePass::run(Module &M, ModuleAnalysisManager &MAM) {
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  runCGProfilePass(M, FAM, InLTO);

  return PreservedAnalyses::all();
}
