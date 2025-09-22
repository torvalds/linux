//===---- CoroConditionalWrapper.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROCONDITIONALWRAPPER_H
#define LLVM_TRANSFORMS_COROUTINES_COROCONDITIONALWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

// Only runs passes in the contained pass manager if the module contains any
// coroutine intrinsic declarations.
struct CoroConditionalWrapper : PassInfoMixin<CoroConditionalWrapper> {
  CoroConditionalWrapper(ModulePassManager &&);
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  ModulePassManager PM;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROCONDITIONALWRAPPER_H
