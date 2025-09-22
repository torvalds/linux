//===- JumpTableToSwitch.h - ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_JUMP_TABLE_TO_SWITCH_H
#define LLVM_TRANSFORMS_SCALAR_JUMP_TABLE_TO_SWITCH_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct JumpTableToSwitchPass : PassInfoMixin<JumpTableToSwitchPass> {
  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_JUMP_TABLE_TO_SWITCH_H
