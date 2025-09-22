//===- LowerGlobalDtors.h - Lower @llvm.global_dtors ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers @llvm.global_dtors by creating wrapper functions that are
// registered in @llvm.global_ctors and which contain a call to `__cxa_atexit`
// to register their destructor functions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_LOWERGLOBALDTORS_H
#define LLVM_TRANSFORMS_UTILS_LOWERGLOBALDTORS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class LowerGlobalDtorsPass : public PassInfoMixin<LowerGlobalDtorsPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOWERGLOBALDTORS_H
