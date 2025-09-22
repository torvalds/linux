//===-- StripDeadPrototypes.h - Remove unused function declarations -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass loops over all of the functions in the input module, looking for
// dead declarations and removes them. Dead declarations are declarations of
// functions for which no implementation is available (i.e., declarations for
// unused library functions).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_STRIPDEADPROTOTYPES_H
#define LLVM_TRANSFORMS_IPO_STRIPDEADPROTOTYPES_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass to remove unused function declarations.
struct StripDeadPrototypesPass : PassInfoMixin<StripDeadPrototypesPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

}

#endif // LLVM_TRANSFORMS_IPO_STRIPDEADPROTOTYPES_H
