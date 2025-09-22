//===--- llvm/CodeGen/WasmEHPrepare.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WASMEHPREPARE_H
#define LLVM_CODEGEN_WASMEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class WasmEHPreparePass : public PassInfoMixin<WasmEHPreparePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_WASMEHPREPARE_H
