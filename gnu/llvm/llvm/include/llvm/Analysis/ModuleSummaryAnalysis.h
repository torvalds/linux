//===- ModuleSummaryAnalysis.h - Module summary index builder ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface to build a ModuleSummaryIndex for a module.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H
#define LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H

#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <functional>
#include <optional>

namespace llvm {

class BlockFrequencyInfo;
class Function;
class Module;
class ProfileSummaryInfo;
class StackSafetyInfo;

/// Direct function to compute a \c ModuleSummaryIndex from a given module.
///
/// If operating within a pass manager which has defined ways to compute the \c
/// BlockFrequencyInfo for a given function, that can be provided via
/// a std::function callback. Otherwise, this routine will manually construct
/// that information.
ModuleSummaryIndex buildModuleSummaryIndex(
    const Module &M,
    std::function<BlockFrequencyInfo *(const Function &F)> GetBFICallback,
    ProfileSummaryInfo *PSI,
    std::function<const StackSafetyInfo *(const Function &F)> GetSSICallback =
        [](const Function &F) -> const StackSafetyInfo * { return nullptr; });

/// Analysis pass to provide the ModuleSummaryIndex object.
class ModuleSummaryIndexAnalysis
    : public AnalysisInfoMixin<ModuleSummaryIndexAnalysis> {
  friend AnalysisInfoMixin<ModuleSummaryIndexAnalysis>;

  static AnalysisKey Key;

public:
  using Result = ModuleSummaryIndex;

  Result run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the ModuleSummaryIndex object.
class ModuleSummaryIndexWrapperPass : public ModulePass {
  std::optional<ModuleSummaryIndex> Index;

public:
  static char ID;

  ModuleSummaryIndexWrapperPass();

  /// Get the index built by pass
  ModuleSummaryIndex &getIndex() { return *Index; }
  const ModuleSummaryIndex &getIndex() const { return *Index; }

  bool runOnModule(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createModuleSummaryIndexWrapperPass - This pass builds a ModuleSummaryIndex
// object for the module, to be written to bitcode or LLVM assembly.
//
ModulePass *createModuleSummaryIndexWrapperPass();

/// Legacy wrapper pass to provide the ModuleSummaryIndex object.
class ImmutableModuleSummaryIndexWrapperPass : public ImmutablePass {
  const ModuleSummaryIndex *Index;

public:
  static char ID;

  ImmutableModuleSummaryIndexWrapperPass(
      const ModuleSummaryIndex *Index = nullptr);
  const ModuleSummaryIndex *getIndex() const { return Index; }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// ImmutableModuleSummaryIndexWrapperPass - This pass wrap provided
// ModuleSummaryIndex object for the module, to be used by other passes.
//
ImmutablePass *
createImmutableModuleSummaryIndexWrapperPass(const ModuleSummaryIndex *Index);

/// Returns true if the instruction could have memprof metadata, used to ensure
/// consistency between summary analysis and the ThinLTO backend processing.
bool mayHaveMemprofSummary(const CallBase *CB);

} // end namespace llvm

#endif // LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H
