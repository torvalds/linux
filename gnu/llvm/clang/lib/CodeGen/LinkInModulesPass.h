//===-- LinkInModulesPass.h - Module Linking pass ----------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides a pass to link in Modules from a provided
/// BackendConsumer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_LINKINMODULESPASS_H
#define LLVM_BITCODE_LINKINMODULESPASS_H

#include "BackendConsumer.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
class ModulePass;
class Pass;

/// Create and return a pass that links in Moduels from a provided
/// BackendConsumer to a given primary Module. Note that this pass is designed
/// for use with the legacy pass manager.
class LinkInModulesPass : public PassInfoMixin<LinkInModulesPass> {
  clang::BackendConsumer *BC;

public:
  LinkInModulesPass(clang::BackendConsumer *BC);

  PreservedAnalyses run(Module &M, AnalysisManager<Module> &);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif
