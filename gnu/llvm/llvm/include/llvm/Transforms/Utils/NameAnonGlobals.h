//===-- NameAnonGlobals.h - Anonymous Global Naming Pass --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements naming anonymous globals to make sure they can be
// referred to by ThinLTO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_NAMEANONGLOBALS_H
#define LLVM_TRANSFORMS_UTILS_NAMEANONGLOBALS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Simple pass that provides a name to every anonymous globals.
class NameAnonGlobalPass : public PassInfoMixin<NameAnonGlobalPass> {
public:
  NameAnonGlobalPass() = default;

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_NAMEANONGLOBALS_H
