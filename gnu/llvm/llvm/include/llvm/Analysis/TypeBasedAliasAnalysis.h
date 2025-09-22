//===- TypeBasedAliasAnalysis.h - Type-Based Alias Analysis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is the interface for a metadata-based TBAA. See the source file for
/// details on the algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H
#define LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <memory>

namespace llvm {

class CallBase;
class Function;
class MDNode;
class MemoryLocation;

/// A simple AA result that uses TBAA metadata to answer queries.
class TypeBasedAAResult : public AAResultBase {
public:
  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;
  }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI);
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals);

  MemoryEffects getMemoryEffects(const CallBase *Call, AAQueryInfo &AAQI);
  MemoryEffects getMemoryEffects(const Function *F);
  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI);

private:
  bool Aliases(const MDNode *A, const MDNode *B) const;
};

/// Analysis pass providing a never-invalidated alias analysis result.
class TypeBasedAA : public AnalysisInfoMixin<TypeBasedAA> {
  friend AnalysisInfoMixin<TypeBasedAA>;

  static AnalysisKey Key;

public:
  using Result = TypeBasedAAResult;

  TypeBasedAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the TypeBasedAAResult object.
class TypeBasedAAWrapperPass : public ImmutablePass {
  std::unique_ptr<TypeBasedAAResult> Result;

public:
  static char ID;

  TypeBasedAAWrapperPass();

  TypeBasedAAResult &getResult() { return *Result; }
  const TypeBasedAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createTypeBasedAAWrapperPass - This pass implements metadata-based
// type-based alias analysis.
//
ImmutablePass *createTypeBasedAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H
