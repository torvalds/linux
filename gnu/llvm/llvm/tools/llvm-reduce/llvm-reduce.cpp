//===- llvm-reduce.cpp - The LLVM Delta Reduction utility -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program tries to reduce an IR test case for a given interesting-ness
// test. It runs multiple delta debugging passes in order to minimize the input
// file. It's worth noting that this is a part of the bugpoint redesign
// proposal, and thus a *temporary* tool that will eventually be integrated
// into the bugpoint tool itself.
//
//===----------------------------------------------------------------------===//

#include "DeltaManager.h"
#include "ReducerWorkItem.h"
#include "TestRunner.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace llvm;

cl::OptionCategory LLVMReduceOptions("llvm-reduce options");

static cl::opt<bool> Help("h", cl::desc("Alias for -help"), cl::Hidden,
                          cl::cat(LLVMReduceOptions));
static cl::opt<bool> Version("v", cl::desc("Alias for -version"), cl::Hidden,
                             cl::cat(LLVMReduceOptions));

static cl::opt<bool> PreserveDebugEnvironment(
    "preserve-debug-environment",
    cl::desc("Don't disable features used for crash "
             "debugging (crash reports, llvm-symbolizer and core dumps)"),
    cl::cat(LLVMReduceOptions));

static cl::opt<bool>
    PrintDeltaPasses("print-delta-passes",
                     cl::desc("Print list of delta passes, passable to "
                              "--delta-passes as a comma separated list"),
                     cl::cat(LLVMReduceOptions));

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input llvm ll/bc file>"),
                                          cl::cat(LLVMReduceOptions));

static cl::opt<std::string>
    TestFilename("test",
                 cl::desc("Name of the interesting-ness test to be run"),
                 cl::cat(LLVMReduceOptions));

static cl::list<std::string>
    TestArguments("test-arg",
                  cl::desc("Arguments passed onto the interesting-ness test"),
                  cl::cat(LLVMReduceOptions));

static cl::opt<std::string> OutputFilename(
    "output",
    cl::desc("Specify the output file. default: reduced.ll|.bc|.mir"));
static cl::alias OutputFileAlias("o", cl::desc("Alias for -output"),
                                 cl::aliasopt(OutputFilename),
                                 cl::cat(LLVMReduceOptions));

static cl::opt<bool>
    ReplaceInput("in-place",
                 cl::desc("WARNING: This option will replace your input file "
                          "with the reduced version!"),
                 cl::cat(LLVMReduceOptions));

enum class InputLanguages { None, IR, MIR };

static cl::opt<InputLanguages>
    InputLanguage("x", cl::ValueOptional,
                  cl::desc("Input language ('ir' or 'mir')"),
                  cl::init(InputLanguages::None),
                  cl::values(clEnumValN(InputLanguages::IR, "ir", ""),
                             clEnumValN(InputLanguages::MIR, "mir", "")),
                  cl::cat(LLVMReduceOptions));

static cl::opt<bool> ForceOutputBitcode(
    "output-bitcode",
    cl::desc("Emit final result as bitcode instead of text IR"), cl::Hidden,
    cl::cat(LLVMReduceOptions));

static cl::opt<int>
    MaxPassIterations("max-pass-iterations",
                      cl::desc("Maximum number of times to run the full set "
                               "of delta passes (default=5)"),
                      cl::init(5), cl::cat(LLVMReduceOptions));

extern cl::opt<cl::boolOrDefault> PreserveInputDbgFormat;

static codegen::RegisterCodeGenFlags CGF;

/// Turn off crash debugging features
///
/// Crash is expected, so disable crash reports and symbolization to reduce
/// output clutter and avoid potentially slow symbolization.
static void disableEnvironmentDebugFeatures() {
  sys::Process::PreventCoreFiles();

  // TODO: Copied from not. Should have a wrapper around setenv.
#ifdef _WIN32
  SetEnvironmentVariableA("LLVM_DISABLE_CRASH_REPORT", "1");
  SetEnvironmentVariableA("LLVM_DISABLE_SYMBOLIZATION", "1");
#else
  setenv("LLVM_DISABLE_CRASH_REPORT", "1", /*overwrite=*/1);
  setenv("LLVM_DISABLE_SYMBOLIZATION", "1", /*overwrite=*/1);
#endif
}

static std::pair<StringRef, bool> determineOutputType(bool IsMIR,
                                                      bool InputIsBitcode) {
  bool OutputBitcode = ForceOutputBitcode || InputIsBitcode;

  if (ReplaceInput) { // In-place
    OutputFilename = InputFilename.c_str();
  } else if (OutputFilename.empty()) {
    // Default to producing bitcode if the input was bitcode, if not explicitly
    // requested.

    OutputFilename =
        IsMIR ? "reduced.mir" : (OutputBitcode ? "reduced.bc" : "reduced.ll");
  }

  return {OutputFilename, OutputBitcode};
}

int main(int Argc, char **Argv) {
  InitLLVM X(Argc, Argv);
  const StringRef ToolName(Argv[0]);
  PreserveInputDbgFormat = cl::boolOrDefault::BOU_TRUE;

  cl::HideUnrelatedOptions({&LLVMReduceOptions, &getColorCategory()});
  cl::ParseCommandLineOptions(Argc, Argv, "LLVM automatic testcase reducer.\n");

  if (Argc == 1) {
    cl::PrintHelpMessage();
    return 0;
  }

  if (PrintDeltaPasses) {
    printDeltaPasses(outs());
    return 0;
  }

  bool ReduceModeMIR = false;
  if (InputLanguage != InputLanguages::None) {
    if (InputLanguage == InputLanguages::MIR)
      ReduceModeMIR = true;
  } else if (StringRef(InputFilename).ends_with(".mir")) {
    ReduceModeMIR = true;
  }

  if (InputFilename.empty()) {
    WithColor::error(errs(), ToolName)
        << "reduction testcase positional argument must be specified\n";
    return 1;
  }

  if (TestFilename.empty()) {
    WithColor::error(errs(), ToolName) << "--test option must be specified\n";
    return 1;
  }

  if (!PreserveDebugEnvironment)
    disableEnvironmentDebugFeatures();

  LLVMContext Context;
  std::unique_ptr<TargetMachine> TM;

  auto [OriginalProgram, InputIsBitcode] =
      parseReducerWorkItem(ToolName, InputFilename, Context, TM, ReduceModeMIR);
  if (!OriginalProgram) {
    return 1;
  }

  StringRef OutputFilename;
  bool OutputBitcode;
  std::tie(OutputFilename, OutputBitcode) =
      determineOutputType(ReduceModeMIR, InputIsBitcode);

  // Initialize test environment
  TestRunner Tester(TestFilename, TestArguments, std::move(OriginalProgram),
                    std::move(TM), ToolName, OutputFilename, InputIsBitcode,
                    OutputBitcode);

  // This parses and writes out the testcase into a temporary file copy for the
  // test, rather than evaluating the source IR directly. This is for the
  // convenience of lit tests; the stripped out comments may have broken the
  // interestingness checks.
  if (!Tester.getProgram().isReduced(Tester)) {
    errs() << "\nInput isn't interesting! Verify interesting-ness test\n";
    return 1;
  }

  // Try to reduce code
  runDeltaPasses(Tester, MaxPassIterations);

  // Print reduced file to STDOUT
  if (OutputFilename == "-")
    Tester.getProgram().print(outs(), nullptr);
  else
    Tester.writeOutput("Done reducing! Reduced testcase: ");

  return 0;
}
