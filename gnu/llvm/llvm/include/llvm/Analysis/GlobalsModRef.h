//===- GlobalsModRef.h - Simple Mod/Ref AA for Globals ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for a simple mod/ref and alias analysis over globals.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GLOBALSMODREF_H
#define LLVM_ANALYSIS_GLOBALSMODREF_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include <list>

namespace llvm {
class CallGraph;
class Function;

/// An alias analysis result set for globals.
///
/// This focuses on handling aliasing properties of globals and interprocedural
/// function call mod/ref information.
class GlobalsAAResult : public AAResultBase {
  class FunctionInfo;

  const DataLayout &DL;
  std::function<const TargetLibraryInfo &(Function &F)> GetTLI;

  /// The globals that do not have their addresses taken.
  SmallPtrSet<const GlobalValue *, 8> NonAddressTakenGlobals;

  /// Are there functions with local linkage that may modify globals.
  bool UnknownFunctionsWithLocalLinkage = false;

  /// IndirectGlobals - The memory pointed to by this global is known to be
  /// 'owned' by the global.
  SmallPtrSet<const GlobalValue *, 8> IndirectGlobals;

  /// AllocsForIndirectGlobals - If an instruction allocates memory for an
  /// indirect global, this map indicates which one.
  DenseMap<const Value *, const GlobalValue *> AllocsForIndirectGlobals;

  /// For each function, keep track of what globals are modified or read.
  DenseMap<const Function *, FunctionInfo> FunctionInfos;

  /// A map of functions to SCC. The SCCs are described by a simple integer
  /// ID that is only useful for comparing for equality (are two functions
  /// in the same SCC or not?)
  DenseMap<const Function *, unsigned> FunctionToSCCMap;

  /// Handle to clear this analysis on deletion of values.
  struct DeletionCallbackHandle final : CallbackVH {
    GlobalsAAResult *GAR;
    std::list<DeletionCallbackHandle>::iterator I;

    DeletionCallbackHandle(GlobalsAAResult &GAR, Value *V)
        : CallbackVH(V), GAR(&GAR) {}

    void deleted() override;
  };

  /// List of callbacks for globals being tracked by this analysis. Note that
  /// these objects are quite large, but we only anticipate having one per
  /// global tracked by this analysis. There are numerous optimizations we
  /// could perform to the memory utilization here if this becomes a problem.
  std::list<DeletionCallbackHandle> Handles;

  explicit GlobalsAAResult(
      const DataLayout &DL,
      std::function<const TargetLibraryInfo &(Function &F)> GetTLI);

  friend struct RecomputeGlobalsAAPass;

public:
  GlobalsAAResult(GlobalsAAResult &&Arg);
  ~GlobalsAAResult();

  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &);

  static GlobalsAAResult
  analyzeModule(Module &M,
                std::function<const TargetLibraryInfo &(Function &F)> GetTLI,
                CallGraph &CG);

  //------------------------------------------------
  // Implement the AliasAnalysis API
  //
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI);

  using AAResultBase::getModRefInfo;
  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);

  using AAResultBase::getMemoryEffects;
  /// getMemoryEffects - Return the behavior of the specified function if
  /// called from the specified call site.  The call site may be null in which
  /// case the most generic behavior of this function should be returned.
  MemoryEffects getMemoryEffects(const Function *F);

private:
  FunctionInfo *getFunctionInfo(const Function *F);

  void AnalyzeGlobals(Module &M);
  void AnalyzeCallGraph(CallGraph &CG, Module &M);
  bool AnalyzeUsesOfPointer(Value *V,
                            SmallPtrSetImpl<Function *> *Readers = nullptr,
                            SmallPtrSetImpl<Function *> *Writers = nullptr,
                            GlobalValue *OkayStoreDest = nullptr);
  bool AnalyzeIndirectGlobalMemory(GlobalVariable *GV);
  void CollectSCCMembership(CallGraph &CG);

  bool isNonEscapingGlobalNoAlias(const GlobalValue *GV, const Value *V);
  ModRefInfo getModRefInfoForArgument(const CallBase *Call,
                                      const GlobalValue *GV, AAQueryInfo &AAQI);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class GlobalsAA : public AnalysisInfoMixin<GlobalsAA> {
  friend AnalysisInfoMixin<GlobalsAA>;
  static AnalysisKey Key;

public:
  typedef GlobalsAAResult Result;

  GlobalsAAResult run(Module &M, ModuleAnalysisManager &AM);
};

struct RecomputeGlobalsAAPass : PassInfoMixin<RecomputeGlobalsAAPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the GlobalsAAResult object.
class GlobalsAAWrapperPass : public ModulePass {
  std::unique_ptr<GlobalsAAResult> Result;

public:
  static char ID;

  GlobalsAAWrapperPass();

  GlobalsAAResult &getResult() { return *Result; }
  const GlobalsAAResult &getResult() const { return *Result; }

  bool runOnModule(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createGlobalsAAWrapperPass - This pass provides alias and mod/ref info for
// global values that do not have their addresses taken.
//
ModulePass *createGlobalsAAWrapperPass();
}

#endif
