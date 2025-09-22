//===---- BDCE.cpp - Bit-tracking dead code elimination ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Bit-Tracking Dead Code Elimination pass. Some
// instructions (shifts, some ands, ors, etc.) kill some of their input bits.
// We track these dead bits and remove instructions that compute only these
// dead bits.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_BDCE_H
#define LLVM_TRANSFORMS_SCALAR_BDCE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

// The Bit-Tracking Dead Code Elimination pass.
struct BDCEPass : PassInfoMixin<BDCEPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_BDCE_H
