//===-- InferFunctionAttrs.h - Infer implicit function attributes ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Interfaces for passes which infer implicit function attributes from the
/// name and signature of function declarations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INFERFUNCTIONATTRS_H
#define LLVM_TRANSFORMS_IPO_INFERFUNCTIONATTRS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;

/// A pass which infers function attributes from the names and signatures of
/// function declarations in a module.
struct InferFunctionAttrsPass : PassInfoMixin<InferFunctionAttrsPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

}

#endif // LLVM_TRANSFORMS_IPO_INFERFUNCTIONATTRS_H
