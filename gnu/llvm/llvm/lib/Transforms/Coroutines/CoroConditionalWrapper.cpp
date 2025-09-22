//===- CoroConditionalWrapper.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Coroutines/CoroConditionalWrapper.h"
#include "CoroInternal.h"
#include "llvm/IR/Module.h"

using namespace llvm;

CoroConditionalWrapper::CoroConditionalWrapper(ModulePassManager &&PM)
    : PM(std::move(PM)) {}

PreservedAnalyses CoroConditionalWrapper::run(Module &M,
                                              ModuleAnalysisManager &AM) {
  if (!coro::declaresAnyIntrinsic(M))
    return PreservedAnalyses::all();

  return PM.run(M, AM);
}

void CoroConditionalWrapper::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  OS << "coro-cond";
  OS << '(';
  PM.printPipeline(OS, MapClassName2PassName);
  OS << ')';
}
