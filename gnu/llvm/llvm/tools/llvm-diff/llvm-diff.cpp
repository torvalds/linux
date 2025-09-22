//===-- llvm-diff.cpp - Module comparator command-line driver ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the command-line driver for the difference engine.
//
//===----------------------------------------------------------------------===//

#include "lib/DiffLog.h"
#include "lib/DifferenceEngine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"
#include <string>
#include <utility>


using namespace llvm;

/// Reads a module from a file.  On error, messages are written to stderr
/// and null is returned.
static std::unique_ptr<Module> readModule(LLVMContext &Context,
                                          StringRef Name) {
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(Name, Diag, Context);
  if (!M)
    Diag.print("llvm-diff", errs());
  return M;
}

static void diffGlobal(DifferenceEngine &Engine, Module &L, Module &R,
                       StringRef Name) {
  // Drop leading sigils from the global name.
  Name.consume_front("@");

  Function *LFn = L.getFunction(Name);
  Function *RFn = R.getFunction(Name);
  if (LFn && RFn)
    Engine.diff(LFn, RFn);
  else if (!LFn && !RFn)
    errs() << "No function named @" << Name << " in either module\n";
  else if (!LFn)
    errs() << "No function named @" << Name << " in left module\n";
  else
    errs() << "No function named @" << Name << " in right module\n";
}

cl::OptionCategory DiffCategory("Diff Options");

static cl::opt<std::string> LeftFilename(cl::Positional,
                                         cl::desc("<first file>"), cl::Required,
                                         cl::cat(DiffCategory));
static cl::opt<std::string> RightFilename(cl::Positional,
                                          cl::desc("<second file>"),
                                          cl::Required, cl::cat(DiffCategory));
static cl::list<std::string> GlobalsToCompare(cl::Positional,
                                              cl::desc("<globals to compare>"),
                                              cl::cat(DiffCategory));

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions({&DiffCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv);

  LLVMContext Context;

  // Load both modules.  Die if that fails.
  std::unique_ptr<Module> LModule = readModule(Context, LeftFilename);
  std::unique_ptr<Module> RModule = readModule(Context, RightFilename);
  if (!LModule || !RModule) return 1;

  DiffConsumer Consumer;
  DifferenceEngine Engine(Consumer);

  // If any global names were given, just diff those.
  if (!GlobalsToCompare.empty()) {
    for (unsigned I = 0, E = GlobalsToCompare.size(); I != E; ++I)
      diffGlobal(Engine, *LModule, *RModule, GlobalsToCompare[I]);

  // Otherwise, diff everything in the module.
  } else {
    Engine.diff(LModule.get(), RModule.get());
  }

  return Consumer.hadDifferences();
}
