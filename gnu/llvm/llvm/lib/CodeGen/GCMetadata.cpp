//===-- GCMetadata.cpp - Garbage collector metadata -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the GCFunctionInfo class and GCModuleInfo pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <memory>
#include <string>

using namespace llvm;

bool GCStrategyMap::invalidate(Module &M, const PreservedAnalyses &PA,
                               ModuleAnalysisManager::Invalidator &) {
  for (const auto &F : M) {
    if (F.isDeclaration() || !F.hasGC())
      continue;
    if (!StrategyMap.contains(F.getGC()))
      return true;
  }
  return false;
}

AnalysisKey CollectorMetadataAnalysis::Key;

CollectorMetadataAnalysis::Result
CollectorMetadataAnalysis::run(Module &M, ModuleAnalysisManager &MAM) {
  Result R;
  auto &Map = R.StrategyMap;
  for (auto &F : M) {
    if (F.isDeclaration() || !F.hasGC())
      continue;
    if (auto GCName = F.getGC(); !Map.contains(GCName))
      Map[GCName] = getGCStrategy(GCName);
  }
  return R;
}

AnalysisKey GCFunctionAnalysis::Key;

GCFunctionAnalysis::Result
GCFunctionAnalysis::run(Function &F, FunctionAnalysisManager &FAM) {
  assert(!F.isDeclaration() && "Can only get GCFunctionInfo for a definition!");
  assert(F.hasGC() && "Function doesn't have GC!");

  auto &MAMProxy = FAM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  assert(
      MAMProxy.cachedResultExists<CollectorMetadataAnalysis>(*F.getParent()) &&
      "This pass need module analysis `collector-metadata`!");
  auto &Map =
      MAMProxy.getCachedResult<CollectorMetadataAnalysis>(*F.getParent())
          ->StrategyMap;
  GCFunctionInfo Info(F, *Map[F.getGC()]);
  return Info;
}

INITIALIZE_PASS(GCModuleInfo, "collector-metadata",
                "Create Garbage Collector Module Metadata", false, false)

// -----------------------------------------------------------------------------

GCFunctionInfo::GCFunctionInfo(const Function &F, GCStrategy &S)
    : F(F), S(S), FrameSize(~0LL) {}

GCFunctionInfo::~GCFunctionInfo() = default;

bool GCFunctionInfo::invalidate(Function &F, const PreservedAnalyses &PA,
                                FunctionAnalysisManager::Invalidator &) {
  auto PAC = PA.getChecker<GCFunctionAnalysis>();
  return !PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Function>>();
}

// -----------------------------------------------------------------------------

char GCModuleInfo::ID = 0;

GCModuleInfo::GCModuleInfo() : ImmutablePass(ID) {
  initializeGCModuleInfoPass(*PassRegistry::getPassRegistry());
}

GCFunctionInfo &GCModuleInfo::getFunctionInfo(const Function &F) {
  assert(!F.isDeclaration() && "Can only get GCFunctionInfo for a definition!");
  assert(F.hasGC());

  finfo_map_type::iterator I = FInfoMap.find(&F);
  if (I != FInfoMap.end())
    return *I->second;

  GCStrategy *S = getGCStrategy(F.getGC());
  Functions.push_back(std::make_unique<GCFunctionInfo>(F, *S));
  GCFunctionInfo *GFI = Functions.back().get();
  FInfoMap[&F] = GFI;
  return *GFI;
}

void GCModuleInfo::clear() {
  Functions.clear();
  FInfoMap.clear();
  GCStrategyList.clear();
}

// -----------------------------------------------------------------------------

GCStrategy *GCModuleInfo::getGCStrategy(const StringRef Name) {
  // TODO: Arguably, just doing a linear search would be faster for small N
  auto NMI = GCStrategyMap.find(Name);
  if (NMI != GCStrategyMap.end())
    return NMI->getValue();

  std::unique_ptr<GCStrategy> S = llvm::getGCStrategy(Name);
  S->Name = std::string(Name);
  GCStrategyMap[Name] = S.get();
  GCStrategyList.push_back(std::move(S));
  return GCStrategyList.back().get();
}
