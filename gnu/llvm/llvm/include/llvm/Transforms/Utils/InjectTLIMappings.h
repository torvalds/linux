//===- InjectTLIMAppings.h - TLI to VFABI attribute injection  ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Populates the VFABI attribute with the scalar-to-vector mappings
// from the TargetLibraryInfo.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_INJECTTLIMAPPINGS_H
#define LLVM_TRANSFORMS_UTILS_INJECTTLIMAPPINGS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;
class InjectTLIMappings : public PassInfoMixin<InjectTLIMappings> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // End namespace llvm
#endif // LLVM_TRANSFORMS_UTILS_INJECTTLIMAPPINGS_H
