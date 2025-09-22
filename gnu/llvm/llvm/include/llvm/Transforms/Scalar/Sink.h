//===-- Sink.h - Code Sinking -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass moves instructions into successor blocks, when possible, so that
// they aren't executed on paths where their results aren't needed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SINK_H
#define LLVM_TRANSFORMS_SCALAR_SINK_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Move instructions into successor blocks when possible.
class SinkingPass : public PassInfoMixin<SinkingPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_SINK_H
