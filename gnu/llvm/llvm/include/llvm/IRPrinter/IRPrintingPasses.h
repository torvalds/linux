//===- IRPrintingPasses.h - Passes to print out IR constructs ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines passes to print out IR in various granularities. The
/// PrintModulePass pass simply prints out the entire module when it is
/// executed. The PrintFunctionPass class is designed to be pipelined with
/// other FunctionPass's, and prints out the functions of the module as they
/// are processed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IRPRINTER_IRPRINTINGPASSES_H
#define LLVM_IRPRINTER_IRPRINTINGPASSES_H

#include "llvm/IR/PassManager.h"
#include <string>

namespace llvm {
class raw_ostream;
class Function;
class Module;
class Pass;

/// Pass (for the new pass manager) for printing a Module as
/// LLVM's text IR assembly.
class PrintModulePass : public PassInfoMixin<PrintModulePass> {
  raw_ostream &OS;
  std::string Banner;
  bool ShouldPreserveUseListOrder;
  bool EmitSummaryIndex;

public:
  PrintModulePass();
  PrintModulePass(raw_ostream &OS, const std::string &Banner = "",
                  bool ShouldPreserveUseListOrder = false,
                  bool EmitSummaryIndex = false);

  PreservedAnalyses run(Module &M, AnalysisManager<Module> &);
  static bool isRequired() { return true; }
};

/// Pass (for the new pass manager) for printing a Function as
/// LLVM's text IR assembly.
class PrintFunctionPass : public PassInfoMixin<PrintFunctionPass> {
  raw_ostream &OS;
  std::string Banner;

public:
  PrintFunctionPass();
  PrintFunctionPass(raw_ostream &OS, const std::string &Banner = "");

  PreservedAnalyses run(Function &F, AnalysisManager<Function> &);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif
