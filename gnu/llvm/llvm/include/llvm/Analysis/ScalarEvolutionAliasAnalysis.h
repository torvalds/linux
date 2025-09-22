//===- ScalarEvolutionAliasAnalysis.h - SCEV-based AA -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for a SCEV-based alias analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONALIASANALYSIS_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Pass.h"

namespace llvm {

class Function;
class ScalarEvolution;
class SCEV;

/// A simple alias analysis implementation that uses ScalarEvolution to answer
/// queries.
class SCEVAAResult : public AAResultBase {
  ScalarEvolution &SE;

public:
  explicit SCEVAAResult(ScalarEvolution &SE) : SE(SE) {}
  SCEVAAResult(SCEVAAResult &&Arg) : AAResultBase(std::move(Arg)), SE(Arg.SE) {}

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI);

  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

private:
  Value *GetBaseValue(const SCEV *S);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class SCEVAA : public AnalysisInfoMixin<SCEVAA> {
  friend AnalysisInfoMixin<SCEVAA>;
  static AnalysisKey Key;

public:
  typedef SCEVAAResult Result;

  SCEVAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the SCEVAAResult object.
class SCEVAAWrapperPass : public FunctionPass {
  std::unique_ptr<SCEVAAResult> Result;

public:
  static char ID;

  SCEVAAWrapperPass();

  SCEVAAResult &getResult() { return *Result; }
  const SCEVAAResult &getResult() const { return *Result; }

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Creates an instance of \c SCEVAAWrapperPass.
FunctionPass *createSCEVAAWrapperPass();

}

#endif
