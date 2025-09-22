//===--- PartiallyInlineLibCalls.h - Partially inline libcalls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass tries to partially inline the fast path of well-known library
// functions, such as using square-root instructions for cases where sqrt()
// does not need to set errno.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H
#define LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;
class PartiallyInlineLibCallsPass
    : public PassInfoMixin<PartiallyInlineLibCallsPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H
