//===- llvm/CodeGen/MachineFunctionAnalysis.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MachineFunctionAnalysis class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFUNCTIONANALYSIS
#define LLVM_CODEGEN_MACHINEFUNCTIONANALYSIS

#include "llvm/IR/PassManager.h"

namespace llvm {

class MachineFunction;
class LLVMTargetMachine;

/// This analysis create MachineFunction for given Function.
/// To release the MachineFunction, users should invalidate it explicitly.
class MachineFunctionAnalysis
    : public AnalysisInfoMixin<MachineFunctionAnalysis> {
  friend AnalysisInfoMixin<MachineFunctionAnalysis>;

  static AnalysisKey Key;

  const LLVMTargetMachine *TM;

public:
  class Result {
    std::unique_ptr<MachineFunction> MF;

  public:
    Result(std::unique_ptr<MachineFunction> MF) : MF(std::move(MF)) {}
    MachineFunction &getMF() { return *MF; };
    bool invalidate(Function &, const PreservedAnalyses &PA,
                    FunctionAnalysisManager::Invalidator &);
  };

  MachineFunctionAnalysis(const LLVMTargetMachine *TM) : TM(TM){};
  Result run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MachineFunctionAnalysis
