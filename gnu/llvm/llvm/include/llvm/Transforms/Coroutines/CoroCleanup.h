//===-- CoroCleanup.h - Lower all coroutine related intrinsics --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file delcares a pass that lowers all remaining coroutine intrinsics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROCLEANUP_H
#define LLVM_TRANSFORMS_COROUTINES_COROCLEANUP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

struct CoroCleanupPass : PassInfoMixin<CoroCleanupPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROCLEANUP_H
