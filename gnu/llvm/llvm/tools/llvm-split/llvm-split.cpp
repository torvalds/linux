//===-- llvm-split: command line tool for testing module splitting --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program can be used to test the llvm::SplitModule and
// TargetMachine::splitModule functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/SplitModule.h"

using namespace llvm;

static cl::OptionCategory SplitCategory("Split Options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"),
                                          cl::cat(SplitCategory));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"),
                                           cl::cat(SplitCategory));

static cl::opt<unsigned> NumOutputs("j", cl::Prefix, cl::init(2),
                                    cl::desc("Number of output files"),
                                    cl::cat(SplitCategory));

static cl::opt<bool>
    PreserveLocals("preserve-locals", cl::Prefix, cl::init(false),
                   cl::desc("Split without externalizing locals"),
                   cl::cat(SplitCategory));

static cl::opt<bool>
    RoundRobin("round-robin", cl::Prefix, cl::init(false),
               cl::desc("Use round-robin distribution of functions to "
                        "modules instead of the default name-hash-based one"),
               cl::cat(SplitCategory));

static cl::opt<std::string>
    MTriple("mtriple",
            cl::desc("Target triple. When present, a TargetMachine is created "
                     "and TargetMachine::splitModule is used instead of the "
                     "common SplitModule logic."),
            cl::value_desc("triple"), cl::cat(SplitCategory));

static cl::opt<std::string>
    MCPU("mcpu", cl::desc("Target CPU, ignored if -mtriple is not used"),
         cl::value_desc("cpu"), cl::cat(SplitCategory));

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  LLVMContext Context;
  SMDiagnostic Err;
  cl::HideUnrelatedOptions({&SplitCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "LLVM module splitter\n");

  std::unique_ptr<TargetMachine> TM;
  if (!MTriple.empty()) {
    InitializeAllTargets();
    InitializeAllTargetMCs();

    std::string Error;
    const Target *T = TargetRegistry::lookupTarget(MTriple, Error);
    if (!T) {
      errs() << "unknown target '" << MTriple << "': " << Error << "\n";
      return 1;
    }

    TargetOptions Options;
    TM = std::unique_ptr<TargetMachine>(T->createTargetMachine(
        MTriple, MCPU, /*FS*/ "", Options, std::nullopt, std::nullopt));
  }

  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  unsigned I = 0;
  const auto HandleModulePart = [&](std::unique_ptr<Module> MPart) {
    std::error_code EC;
    std::unique_ptr<ToolOutputFile> Out(
        new ToolOutputFile(OutputFilename + utostr(I++), EC, sys::fs::OF_None));
    if (EC) {
      errs() << EC.message() << '\n';
      exit(1);
    }

    if (verifyModule(*MPart, &errs())) {
      errs() << "Broken module!\n";
      exit(1);
    }

    WriteBitcodeToFile(*MPart, Out->os());

    // Declare success.
    Out->keep();
  };

  if (TM) {
    if (PreserveLocals) {
      errs() << "warning: -preserve-locals has no effect when using "
                "TargetMachine::splitModule\n";
    }
    if (RoundRobin)
      errs() << "warning: -round-robin has no effect when using "
                "TargetMachine::splitModule\n";

    if (TM->splitModule(*M, NumOutputs, HandleModulePart))
      return 0;

    errs() << "warning: "
              "TargetMachine::splitModule failed, falling back to default "
              "splitModule implementation\n";
  }

  SplitModule(*M, NumOutputs, HandleModulePart, PreserveLocals, RoundRobin);
  return 0;
}
