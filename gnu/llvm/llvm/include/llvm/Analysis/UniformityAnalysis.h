//===- UniformityAnalysis.h ---------------------*- C++ -*-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief LLVM IR instance of the generic uniformity analysis
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_UNIFORMITYANALYSIS_H
#define LLVM_ANALYSIS_UNIFORMITYANALYSIS_H

#include "llvm/ADT/GenericUniformityInfo.h"
#include "llvm/Analysis/CycleAnalysis.h"

namespace llvm {

extern template class GenericUniformityInfo<SSAContext>;
using UniformityInfo = GenericUniformityInfo<SSAContext>;

/// Analysis pass which computes \ref UniformityInfo.
class UniformityInfoAnalysis
    : public AnalysisInfoMixin<UniformityInfoAnalysis> {
  friend AnalysisInfoMixin<UniformityInfoAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = UniformityInfo;

  /// Run the analysis pass over a function and produce a dominator tree.
  UniformityInfo run(Function &F, FunctionAnalysisManager &);

  // TODO: verify analysis
};

/// Printer pass for the \c UniformityInfo.
class UniformityInfoPrinterPass
    : public PassInfoMixin<UniformityInfoPrinterPass> {
  raw_ostream &OS;

public:
  explicit UniformityInfoPrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

/// Legacy analysis pass which computes a \ref CycleInfo.
class UniformityInfoWrapperPass : public FunctionPass {
  Function *m_function = nullptr;
  UniformityInfo m_uniformityInfo;

public:
  static char ID;

  UniformityInfoWrapperPass();

  UniformityInfo &getUniformityInfo() { return m_uniformityInfo; }
  const UniformityInfo &getUniformityInfo() const { return m_uniformityInfo; }

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

  // TODO: verify analysis
};

} // namespace llvm

#endif // LLVM_ANALYSIS_UNIFORMITYANALYSIS_H
