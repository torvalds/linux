//===-------------------- NVPTXAliasAnalysis.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the NVPTX address space based alias analysis pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXALIASANALYSIS_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"

namespace llvm {

class MemoryLocation;

class NVPTXAAResult : public AAResultBase {
public:
  NVPTXAAResult() {}
  NVPTXAAResult(NVPTXAAResult &&Arg) : AAResultBase(std::move(Arg)) {}

  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &Inv) {
    return false;
  }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI = nullptr);

  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class NVPTXAA : public AnalysisInfoMixin<NVPTXAA> {
  friend AnalysisInfoMixin<NVPTXAA>;

  static AnalysisKey Key;

public:
  using Result = NVPTXAAResult;

  NVPTXAAResult run(Function &F, AnalysisManager<Function> &AM) {
    return NVPTXAAResult();
  }
};

/// Legacy wrapper pass to provide the NVPTXAAResult object.
class NVPTXAAWrapperPass : public ImmutablePass {
  std::unique_ptr<NVPTXAAResult> Result;

public:
  static char ID;

  NVPTXAAWrapperPass();

  NVPTXAAResult &getResult() { return *Result; }
  const NVPTXAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override {
    Result.reset(new NVPTXAAResult());
    return false;
  }

  bool doFinalization(Module &M) override {
    Result.reset();
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

// Wrapper around ExternalAAWrapperPass so that the default
// constructor gets the callback.
class NVPTXExternalAAWrapper : public ExternalAAWrapperPass {
public:
  static char ID;

  NVPTXExternalAAWrapper()
      : ExternalAAWrapperPass([](Pass &P, Function &, AAResults &AAR) {
          if (auto *WrapperPass =
                  P.getAnalysisIfAvailable<NVPTXAAWrapperPass>())
            AAR.addAAResult(WrapperPass->getResult());
        }) {}
};

ImmutablePass *createNVPTXAAWrapperPass();
void initializeNVPTXAAWrapperPassPass(PassRegistry &);
ImmutablePass *createNVPTXExternalAAWrapperPass();
void initializeNVPTXExternalAAWrapperPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_NVPTX_NVPTXALIASANALYSIS_H
