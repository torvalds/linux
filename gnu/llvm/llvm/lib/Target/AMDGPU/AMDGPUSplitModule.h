//===- AMDGPUSplitModule.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_AMDGPUSPLITMODULE_H
#define LLVM_TARGET_AMDGPUSPLITMODULE_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/PassManager.h"
#include <memory>

namespace llvm {

/// Splits the module M into N linkable partitions. The function ModuleCallback
/// is called N times passing each individual partition as the MPart argument.
class AMDGPUSplitModulePass : public PassInfoMixin<AMDGPUSplitModulePass> {
public:
  using ModuleCreationCallback =
      function_ref<void(std::unique_ptr<Module> MPart)>;

  AMDGPUSplitModulePass(unsigned N, ModuleCreationCallback ModuleCallback)
      : N(N), ModuleCallback(ModuleCallback) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  unsigned N;
  ModuleCreationCallback ModuleCallback;
};

} // end namespace llvm

#endif // LLVM_TARGET_AMDGPUSPLITMODULE_H
