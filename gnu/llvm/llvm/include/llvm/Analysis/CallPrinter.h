//===-- CallPrinter.h - Call graph printer external interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines external functions that can be called to explicitly
// instantiate the call graph printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CALLPRINTER_H
#define LLVM_ANALYSIS_CALLPRINTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class ModulePass;

/// Pass for printing the call graph to a dot file
class CallGraphDOTPrinterPass : public PassInfoMixin<CallGraphDOTPrinterPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

/// Pass for viewing the call graph
class CallGraphViewerPass : public PassInfoMixin<CallGraphViewerPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

ModulePass *createCallGraphViewerPass();
ModulePass *createCallGraphDOTPrinterPass();

} // end namespace llvm

#endif
