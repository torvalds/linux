//===- MetaRenamer.h - Rename everything with metasyntatic names ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass renames everything with metasyntatic names. The intent is to use
// this pass after bugpoint reduction to conceal the nature of the original
// program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_METARENAMER_H
#define LLVM_TRANSFORMS_UTILS_METARENAMER_H

#include "llvm/IR/PassManager.h"

namespace llvm {
struct MetaRenamerPass : PassInfoMixin<MetaRenamerPass> {
  PreservedAnalyses run(Module &, ModuleAnalysisManager &);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_METARENAMER_H
