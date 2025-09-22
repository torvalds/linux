//===- Transforms/Instrumentation/CGProfile.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Call Graph Profile pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_CGPROFILE_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_CGPROFILE_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
class CGProfilePass : public PassInfoMixin<CGProfilePass> {
public:
  CGProfilePass(bool InLTO) : InLTO(InLTO) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  bool InLTO = false;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_CGPROFILE_H
