//===- RewriteStatepointsForGC.h - ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides interface to "Rewrite Statepoints for GC" pass.
//
// This passe rewrites call/invoke instructions so as to make potential
// relocations performed by the garbage collector explicit in the IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_REWRITESTATEPOINTSFORGC_H
#define LLVM_TRANSFORMS_SCALAR_REWRITESTATEPOINTSFORGC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class DominatorTree;
class Function;
class Module;
class TargetTransformInfo;
class TargetLibraryInfo;

struct RewriteStatepointsForGC : public PassInfoMixin<RewriteStatepointsForGC> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  bool runOnFunction(Function &F, DominatorTree &, TargetTransformInfo &,
                     const TargetLibraryInfo &);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_REWRITESTATEPOINTSFORGC_H
