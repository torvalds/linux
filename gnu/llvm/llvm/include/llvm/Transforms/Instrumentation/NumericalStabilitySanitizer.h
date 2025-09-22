//===- NumericalStabilitySanitizer.h - NSan Pass ---------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file defines the numerical stability sanitizer (nsan) pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_NUMERICALSTABIITYSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_NUMERICALSTABIITYSANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

/// A function pass for nsan instrumentation.
///
/// Instruments functions to duplicate floating point computations in a
/// higher-precision type.
/// This pass inserts calls to runtime library functions. If the
/// functions aren't declared yet, the pass inserts the declarations.
struct NumericalStabilitySanitizerPass
    : public PassInfoMixin<NumericalStabilitySanitizerPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_NUMERICALSTABIITYSANITIZER_H
