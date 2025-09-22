//===- LibCallsShrinkWrap.h - Shrink Wrap Library Calls -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H
#define LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class LibCallsShrinkWrapPass : public PassInfoMixin<LibCallsShrinkWrapPass> {
public:
  static StringRef name() { return "LibCallsShrinkWrapPass"; }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H
