//===- GlobalSplit.h - global variable splitter -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass uses inrange annotations on GEP indices to split globals where
// beneficial. Clang currently attaches these annotations to references to
// virtual table globals under the Itanium ABI for the benefit of the
// whole-program virtual call optimization and control flow integrity passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_GLOBALSPLIT_H
#define LLVM_TRANSFORMS_IPO_GLOBALSPLIT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass to perform split of global variables.
class GlobalSplitPass : public PassInfoMixin<GlobalSplitPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_GLOBALSPLIT_H
