//===-- PGOCtxProfLowering.h - Contextual PGO Instr. Lowering ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the PGOCtxProfLoweringPass class.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOCTXPROFLOWERING_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOCTXPROFLOWERING_H

#include "llvm/IR/PassManager.h"
namespace llvm {
class Type;

class PGOCtxProfLoweringPass : public PassInfoMixin<PGOCtxProfLoweringPass> {
public:
  explicit PGOCtxProfLoweringPass() = default;
  static bool isContextualIRPGOEnabled();

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};
} // namespace llvm
#endif
