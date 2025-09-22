//===- AMDGPUAliasAnalysis --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the AMGPU address space based alias analysis pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUALIASANALYSIS_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Module.h"

namespace llvm {

class DataLayout;
class MemoryLocation;

/// A simple AA result that uses TBAA metadata to answer queries.
class AMDGPUAAResult : public AAResultBase {
  const DataLayout &DL;

public:
  explicit AMDGPUAAResult(const DataLayout &DL) : DL(DL) {}
  AMDGPUAAResult(AMDGPUAAResult &&Arg)
      : AAResultBase(std::move(Arg)), DL(Arg.DL) {}

  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &Inv) {
    return false;
  }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI);
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class AMDGPUAA : public AnalysisInfoMixin<AMDGPUAA> {
  friend AnalysisInfoMixin<AMDGPUAA>;

  static AnalysisKey Key;

public:
  using Result = AMDGPUAAResult;

  AMDGPUAAResult run(Function &F, AnalysisManager<Function> &AM) {
    return AMDGPUAAResult(F.getDataLayout());
  }
};

/// Legacy wrapper pass to provide the AMDGPUAAResult object.
class AMDGPUAAWrapperPass : public ImmutablePass {
  std::unique_ptr<AMDGPUAAResult> Result;

public:
  static char ID;

  AMDGPUAAWrapperPass();

  AMDGPUAAResult &getResult() { return *Result; }
  const AMDGPUAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override {
    Result.reset(new AMDGPUAAResult(M.getDataLayout()));
    return false;
  }

  bool doFinalization(Module &M) override {
    Result.reset();
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

// Wrapper around ExternalAAWrapperPass so that the default constructor gets the
// callback.
class AMDGPUExternalAAWrapper : public ExternalAAWrapperPass {
public:
  static char ID;

  AMDGPUExternalAAWrapper() : ExternalAAWrapperPass(
    [](Pass &P, Function &, AAResults &AAR) {
      if (auto *WrapperPass = P.getAnalysisIfAvailable<AMDGPUAAWrapperPass>())
        AAR.addAAResult(WrapperPass->getResult());
    }) {}
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUALIASANALYSIS_H
