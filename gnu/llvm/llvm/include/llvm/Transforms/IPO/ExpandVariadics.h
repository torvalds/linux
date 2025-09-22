//===- ExpandVariadics.h - expand variadic functions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
#define LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;
class ModulePass;
class OptimizationLevel;

enum class ExpandVariadicsMode {
  Unspecified, // Use the implementation defaults
  Disable,     // Disable the pass entirely
  Optimize,    // Optimise without changing ABI
  Lowering,    // Change variadic calling convention
};

class ExpandVariadicsPass : public PassInfoMixin<ExpandVariadicsPass> {
  const ExpandVariadicsMode Mode;

public:
  // Operates under passed mode unless overridden on commandline
  ExpandVariadicsPass(ExpandVariadicsMode Mode);

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

ModulePass *createExpandVariadicsPass(ExpandVariadicsMode);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
