//===-- CanonicalizeAliases.h - Alias Canonicalization Pass -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file canonicalizes aliases.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CANONICALIZEALIASES_H
#define LLVM_TRANSFORMS_UTILS_CANONICALIZEALIASES_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Simple pass that canonicalizes aliases.
class CanonicalizeAliasesPass : public PassInfoMixin<CanonicalizeAliasesPass> {
public:
  CanonicalizeAliasesPass() = default;

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CANONICALIZEALIASES_H
