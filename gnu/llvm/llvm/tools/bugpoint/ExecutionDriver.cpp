//===- ExecutionDriver.cpp - Allow execution of LLVM program --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code used to execute the program utilizing one of the
// various ways of running LLVM bitcode.
//
//===----------------------------------------------------------------------===//

#include "BugDriver.h"
#include "ToolRunner.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace llvm;

namespace {
// OutputType - Allow the user to specify the way code should be run, to test
// for miscompilation.
//
enum OutputType {
  AutoPick,
  RunLLI,
  RunJIT,
  RunLLC,
  RunLLCIA,
  CompileCustom,
  Custom
};

cl::opt<double> AbsTolerance("abs-tolerance",
                             cl::desc("Absolute error tolerated"),
                             cl::init(0.0));
cl::opt<double> RelTolerance("rel-tolerance",
                             cl::desc("Relative error tolerated"),
                             cl::init(0.0));

cl::opt<OutputType> InterpreterSel(
    cl::desc("Specify the \"test\" i.e. suspect back-end:"),
    cl::values(clEnumValN(AutoPick, "auto", "Use best guess"),
               clEnumValN(RunLLI, "run-int", "Execute with the interpreter"),
               clEnumValN(RunJIT, "run-jit", "Execute with JIT"),
               clEnumValN(RunLLC, "run-llc", "Compile with LLC"),
               clEnumValN(RunLLCIA, "run-llc-ia",
                          "Compile with LLC with integrated assembler"),
               clEnumValN(CompileCustom, "compile-custom",
                          "Use -compile-command to define a command to "
                          "compile the bitcode. Useful to avoid linking."),
               clEnumValN(Custom, "run-custom",
                          "Use -exec-command to define a command to execute "
                          "the bitcode. Useful for cross-compilation.")),
    cl::init(AutoPick));

cl::opt<OutputType> SafeInterpreterSel(
    cl::desc("Specify \"safe\" i.e. known-good backend:"),
    cl::values(clEnumValN(AutoPick, "safe-auto", "Use best guess"),
               clEnumValN(RunLLC, "safe-run-llc", "Compile with LLC"),
               clEnumValN(Custom, "safe-run-custom",
                          "Use -exec-command to define a command to execute "
                          "the bitcode. Useful for cross-compilation.")),
    cl::init(AutoPick));

cl::opt<std::string> SafeInterpreterPath(
    "safe-path", cl::desc("Specify the path to the \"safe\" backend program"),
    cl::init(""));

cl::opt<bool> AppendProgramExitCode(
    "append-exit-code",
    cl::desc("Append the exit code to the output so it gets diff'd too"),
    cl::init(false));

cl::opt<std::string>
    InputFile("input", cl::init("/dev/null"),
              cl::desc("Filename to pipe in as stdin (default: /dev/null)"));

cl::list<std::string>
    AdditionalSOs("additional-so", cl::desc("Additional shared objects to load "
                                            "into executing programs"));

cl::list<std::string> AdditionalLinkerArgs(
    "Xlinker", cl::desc("Additional arguments to pass to the linker"));

cl::opt<std::string> CustomCompileCommand(
    "compile-command", cl::init("llc"),
    cl::desc("Command to compile the bitcode (use with -compile-custom) "
             "(default: llc)"));

cl::opt<std::string> CustomExecCommand(
    "exec-command", cl::init("simulate"),
    cl::desc("Command to execute the bitcode (use with -run-custom) "
             "(default: simulate)"));
}

namespace llvm {
// Anything specified after the --args option are taken as arguments to the
// program being debugged.
cl::list<std::string> InputArgv("args", cl::Positional,
                                cl::desc("<program arguments>..."),
                                cl::PositionalEatsArgs);

cl::opt<std::string>
    OutputPrefix("output-prefix", cl::init("bugpoint"),
                 cl::desc("Prefix to use for outputs (default: 'bugpoint')"));
}

namespace {
cl::list<std::string> ToolArgv("tool-args", cl::Positional,
                               cl::desc("<tool arguments>..."),
                               cl::PositionalEatsArgs);

cl::list<std::string> SafeToolArgv("safe-tool-args", cl::Positional,
                                   cl::desc("<safe-tool arguments>..."),
                                   cl::PositionalEatsArgs);

cl::opt<std::string> CCBinary("gcc", cl::init(""),
                              cl::desc("The gcc binary to use."));

cl::list<std::string> CCToolArgv("gcc-tool-args", cl::Positional,
                                 cl::desc("<gcc-tool arguments>..."),
                                 cl::PositionalEatsArgs);
}

//===----------------------------------------------------------------------===//
// BugDriver method implementation
//

/// initializeExecutionEnvironment - This method is used to set up the
/// environment for executing LLVM programs.
///
Error BugDriver::initializeExecutionEnvironment() {
  outs() << "Initializing execution environment: ";

  // Create an instance of the AbstractInterpreter interface as specified on
  // the command line
  SafeInterpreter = nullptr;
  std::string Message;

  if (CCBinary.empty()) {
    if (ErrorOr<std::string> ClangPath =
            FindProgramByName("clang", getToolName(), &AbsTolerance))
      CCBinary = *ClangPath;
    else
      CCBinary = "gcc";
  }

  switch (InterpreterSel) {
  case AutoPick:
    if (!Interpreter) {
      InterpreterSel = RunJIT;
      Interpreter =
          AbstractInterpreter::createJIT(getToolName(), Message, &ToolArgv);
    }
    if (!Interpreter) {
      InterpreterSel = RunLLC;
      Interpreter = AbstractInterpreter::createLLC(
          getToolName(), Message, CCBinary, &ToolArgv, &CCToolArgv);
    }
    if (!Interpreter) {
      InterpreterSel = RunLLI;
      Interpreter =
          AbstractInterpreter::createLLI(getToolName(), Message, &ToolArgv);
    }
    if (!Interpreter) {
      InterpreterSel = AutoPick;
      Message = "Sorry, I can't automatically select an interpreter!\n";
    }
    break;
  case RunLLI:
    Interpreter =
        AbstractInterpreter::createLLI(getToolName(), Message, &ToolArgv);
    break;
  case RunLLC:
  case RunLLCIA:
    Interpreter = AbstractInterpreter::createLLC(
        getToolName(), Message, CCBinary, &ToolArgv, &CCToolArgv,
        InterpreterSel == RunLLCIA);
    break;
  case RunJIT:
    Interpreter =
        AbstractInterpreter::createJIT(getToolName(), Message, &ToolArgv);
    break;
  case CompileCustom:
    Interpreter = AbstractInterpreter::createCustomCompiler(
        getToolName(), Message, CustomCompileCommand);
    break;
  case Custom:
    Interpreter = AbstractInterpreter::createCustomExecutor(
        getToolName(), Message, CustomExecCommand);
    break;
  }
  if (!Interpreter)
    errs() << Message;
  else // Display informational messages on stdout instead of stderr
    outs() << Message;

  std::string Path = SafeInterpreterPath;
  if (Path.empty())
    Path = getToolName();
  std::vector<std::string> SafeToolArgs = SafeToolArgv;
  switch (SafeInterpreterSel) {
  case AutoPick:
    // In "llc-safe" mode, default to using LLC as the "safe" backend.
    if (InterpreterSel == RunLLC) {
      SafeInterpreterSel = RunLLC;
      SafeToolArgs.push_back("--relocation-model=pic");
      SafeInterpreter = AbstractInterpreter::createLLC(
          Path.c_str(), Message, CCBinary, &SafeToolArgs, &CCToolArgv);
    } else if (InterpreterSel != CompileCustom) {
      SafeInterpreterSel = AutoPick;
      Message = "Sorry, I can't automatically select a safe interpreter!\n";
    }
    break;
  case RunLLC:
  case RunLLCIA:
    SafeToolArgs.push_back("--relocation-model=pic");
    SafeInterpreter = AbstractInterpreter::createLLC(
        Path.c_str(), Message, CCBinary, &SafeToolArgs, &CCToolArgv,
        SafeInterpreterSel == RunLLCIA);
    break;
  case Custom:
    SafeInterpreter = AbstractInterpreter::createCustomExecutor(
        getToolName(), Message, CustomExecCommand);
    break;
  default:
    Message = "Sorry, this back-end is not supported by bugpoint as the "
              "\"safe\" backend right now!\n";
    break;
  }
  if (!SafeInterpreter && InterpreterSel != CompileCustom) {
    outs() << Message << "\nExiting.\n";
    exit(1);
  }

  cc = CC::create(getToolName(), Message, CCBinary, &CCToolArgv);
  if (!cc) {
    outs() << Message << "\nExiting.\n";
    exit(1);
  }

  // If there was an error creating the selected interpreter, quit with error.
  if (Interpreter == nullptr)
    return make_error<StringError>("Failed to init execution environment",
                                   inconvertibleErrorCode());
  return Error::success();
}

/// Try to compile the specified module, returning false and setting Error if an
/// error occurs.  This is used for code generation crash testing.
Error BugDriver::compileProgram(Module &M) const {
  // Emit the program to a bitcode file...
  auto Temp =
      sys::fs::TempFile::create(OutputPrefix + "-test-program-%%%%%%%.bc");
  if (!Temp) {
    errs() << ToolName
           << ": Error making unique filename: " << toString(Temp.takeError())
           << "\n";
    exit(1);
  }
  DiscardTemp Discard{*Temp};
  if (writeProgramToFile(Temp->FD, M)) {
    errs() << ToolName << ": Error emitting bitcode to file '" << Temp->TmpName
           << "'!\n";
    exit(1);
  }

  // Actually compile the program!
  return Interpreter->compileProgram(Temp->TmpName, Timeout, MemoryLimit);
}

/// This method runs "Program", capturing the output of the program to a file,
/// returning the filename of the file.  A recommended filename may be
/// optionally specified.
Expected<std::string> BugDriver::executeProgram(const Module &Program,
                                                std::string OutputFile,
                                                std::string BitcodeFile,
                                                const std::string &SharedObj,
                                                AbstractInterpreter *AI) const {
  if (!AI)
    AI = Interpreter;
  assert(AI && "Interpreter should have been created already!");
  bool CreatedBitcode = false;
  if (BitcodeFile.empty()) {
    // Emit the program to a bitcode file...
    SmallString<128> UniqueFilename;
    int UniqueFD;
    std::error_code EC = sys::fs::createUniqueFile(
        OutputPrefix + "-test-program-%%%%%%%.bc", UniqueFD, UniqueFilename);
    if (EC) {
      errs() << ToolName << ": Error making unique filename: " << EC.message()
             << "!\n";
      exit(1);
    }
    BitcodeFile = std::string(UniqueFilename);

    if (writeProgramToFile(BitcodeFile, UniqueFD, Program)) {
      errs() << ToolName << ": Error emitting bitcode to file '" << BitcodeFile
             << "'!\n";
      exit(1);
    }
    CreatedBitcode = true;
  }

  // Remove the temporary bitcode file when we are done.
  std::string BitcodePath(BitcodeFile);
  FileRemover BitcodeFileRemover(BitcodePath, CreatedBitcode && !SaveTemps);

  if (OutputFile.empty())
    OutputFile = OutputPrefix + "-execution-output-%%%%%%%";

  // Check to see if this is a valid output filename...
  SmallString<128> UniqueFile;
  std::error_code EC = sys::fs::createUniqueFile(OutputFile, UniqueFile);
  if (EC) {
    errs() << ToolName << ": Error making unique filename: " << EC.message()
           << "\n";
    exit(1);
  }
  OutputFile = std::string(UniqueFile);

  // Figure out which shared objects to run, if any.
  std::vector<std::string> SharedObjs(AdditionalSOs);
  if (!SharedObj.empty())
    SharedObjs.push_back(SharedObj);

  Expected<int> RetVal = AI->ExecuteProgram(BitcodeFile, InputArgv, InputFile,
                                            OutputFile, AdditionalLinkerArgs,
                                            SharedObjs, Timeout, MemoryLimit);
  if (Error E = RetVal.takeError())
    return std::move(E);

  if (*RetVal == -1) {
    errs() << "<timeout>";
    static bool FirstTimeout = true;
    if (FirstTimeout) {
      outs()
          << "\n"
             "*** Program execution timed out!  This mechanism is designed to "
             "handle\n"
             "    programs stuck in infinite loops gracefully.  The -timeout "
             "option\n"
             "    can be used to change the timeout threshold or disable it "
             "completely\n"
             "    (with -timeout=0).  This message is only displayed once.\n";
      FirstTimeout = false;
    }
  }

  if (AppendProgramExitCode) {
    std::ofstream outFile(OutputFile.c_str(), std::ios_base::app);
    outFile << "exit " << *RetVal << '\n';
    outFile.close();
  }

  // Return the filename we captured the output to.
  return OutputFile;
}

/// Used to create reference output with the "safe" backend, if reference output
/// is not provided.
Expected<std::string>
BugDriver::executeProgramSafely(const Module &Program,
                                const std::string &OutputFile) const {
  return executeProgram(Program, OutputFile, "", "", SafeInterpreter);
}

Expected<std::string>
BugDriver::compileSharedObject(const std::string &BitcodeFile) {
  assert(Interpreter && "Interpreter should have been created already!");
  std::string OutputFile;

  // Using the known-good backend.
  Expected<CC::FileType> FT =
      SafeInterpreter->OutputCode(BitcodeFile, OutputFile);
  if (Error E = FT.takeError())
    return std::move(E);

  std::string SharedObjectFile;
  if (Error E = cc->MakeSharedObject(OutputFile, *FT, SharedObjectFile,
                                     AdditionalLinkerArgs))
    return std::move(E);

  // Remove the intermediate C file
  sys::fs::remove(OutputFile);

  return SharedObjectFile;
}

/// Calls compileProgram and then records the output into ReferenceOutputFile.
/// Returns true if reference file created, false otherwise. Note:
/// initializeExecutionEnvironment should be called BEFORE this function.
Error BugDriver::createReferenceFile(Module &M, const std::string &Filename) {
  if (Error E = compileProgram(*Program))
    return E;

  Expected<std::string> Result = executeProgramSafely(*Program, Filename);
  if (Error E = Result.takeError()) {
    if (Interpreter != SafeInterpreter) {
      E = joinErrors(
              std::move(E),
              make_error<StringError>(
                  "*** There is a bug running the \"safe\" backend.  Either"
                  " debug it (for example with the -run-jit bugpoint option,"
                  " if JIT is being used as the \"safe\" backend), or fix the"
                  " error some other way.\n",
                  inconvertibleErrorCode()));
    }
    return E;
  }
  ReferenceOutputFile = *Result;
  outs() << "\nReference output is: " << ReferenceOutputFile << "\n\n";
  return Error::success();
}

/// This method executes the specified module and diffs the output against the
/// file specified by ReferenceOutputFile.  If the output is different, 1 is
/// returned.  If there is a problem with the code generator (e.g., llc
/// crashes), this will set ErrMsg.
Expected<bool> BugDriver::diffProgram(const Module &Program,
                                      const std::string &BitcodeFile,
                                      const std::string &SharedObject,
                                      bool RemoveBitcode) const {
  // Execute the program, generating an output file...
  Expected<std::string> Output =
      executeProgram(Program, "", BitcodeFile, SharedObject, nullptr);
  if (Error E = Output.takeError())
    return std::move(E);

  std::string Error;
  bool FilesDifferent = false;
  if (int Diff = DiffFilesWithTolerance(ReferenceOutputFile, *Output,
                                        AbsTolerance, RelTolerance, &Error)) {
    if (Diff == 2) {
      errs() << "While diffing output: " << Error << '\n';
      exit(1);
    }
    FilesDifferent = true;
  } else {
    // Remove the generated output if there are no differences.
    sys::fs::remove(*Output);
  }

  // Remove the bitcode file if we are supposed to.
  if (RemoveBitcode)
    sys::fs::remove(BitcodeFile);
  return FilesDifferent;
}

bool BugDriver::isExecutingJIT() { return InterpreterSel == RunJIT; }
