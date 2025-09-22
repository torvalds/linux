//===- StripSymbols.h - Strip symbols and debug info from a module --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The StripSymbols transformation implements code stripping. Specifically, it
// can delete:
//
//   * names for virtual registers
//   * symbols for internal globals and functions
//   * debug information
//
// Note that this transformation makes code much less readable, so it should
// only be used in situations where the 'strip' utility would be used, such as
// reducing code size or making it harder to reverse engineer code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_STRIPSYMBOLS_H
#define LLVM_TRANSFORMS_IPO_STRIPSYMBOLS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct StripSymbolsPass : PassInfoMixin<StripSymbolsPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

struct StripNonDebugSymbolsPass : PassInfoMixin<StripNonDebugSymbolsPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

struct StripDebugDeclarePass : PassInfoMixin<StripDebugDeclarePass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

struct StripDeadDebugInfoPass : PassInfoMixin<StripDeadDebugInfoPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_STRIPSYMBOLS_H
