//===- OptimizerDriver.cpp - Allow BugPoint to run passes safely ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an interface that allows bugpoint to run various passes
// without the threat of a buggy pass corrupting bugpoint (of course, bugpoint
// may have its own bugs, but that's another story...).  It achieves this by
// forking a copy of itself and having the child process do the optimizations.
// If this client dies, we can always fork a new one.  :)
//
//===----------------------------------------------------------------------===//

#include "BugDriver.h"
#include "ToolRunner.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ToolOutputFile.h"

#define DONT_GET_PLUGIN_LOADER_OPTION
#include "llvm/Support/PluginLoader.h"


using namespace llvm;

#define DEBUG_TYPE "bugpoint"

namespace llvm {
extern cl::opt<std::string> OutputPrefix;
}

static cl::opt<bool> PreserveBitcodeUseListOrder(
    "preserve-bc-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM bitcode."),
    cl::init(true), cl::Hidden);

static cl::opt<std::string>
    OptCmd("opt-command", cl::init(""),
           cl::desc("Path to opt. (default: search path "
                    "for 'opt'.)"));

/// This writes the current "Program" to the named bitcode file.  If an error
/// occurs, true is returned.
static bool writeProgramToFileAux(ToolOutputFile &Out, const Module &M) {
  WriteBitcodeToFile(M, Out.os(), PreserveBitcodeUseListOrder);
  Out.os().close();
  if (!Out.os().has_error()) {
    Out.keep();
    return false;
  }
  return true;
}

bool BugDriver::writeProgramToFile(const std::string &Filename, int FD,
                                   const Module &M) const {
  ToolOutputFile Out(Filename, FD);
  return writeProgramToFileAux(Out, M);
}

bool BugDriver::writeProgramToFile(int FD, const Module &M) const {
  raw_fd_ostream OS(FD, /*shouldClose*/ false);
  WriteBitcodeToFile(M, OS, PreserveBitcodeUseListOrder);
  OS.flush();
  if (!OS.has_error())
    return false;
  OS.clear_error();
  return true;
}

bool BugDriver::writeProgramToFile(const std::string &Filename,
                                   const Module &M) const {
  std::error_code EC;
  ToolOutputFile Out(Filename, EC, sys::fs::OF_None);
  if (!EC)
    return writeProgramToFileAux(Out, M);
  return true;
}

/// This function is used to output the current Program to a file named
/// "bugpoint-ID.bc".
void BugDriver::EmitProgressBitcode(const Module &M, const std::string &ID,
                                    bool NoFlyer) const {
  // Output the input to the current pass to a bitcode file, emit a message
  // telling the user how to reproduce it: opt -foo blah.bc
  //
  std::string Filename = OutputPrefix + "-" + ID + ".bc";
  if (writeProgramToFile(Filename, M)) {
    errs() << "Error opening file '" << Filename << "' for writing!\n";
    return;
  }

  outs() << "Emitted bitcode to '" << Filename << "'\n";
  if (NoFlyer || PassesToRun.empty())
    return;
  outs() << "\n*** You can reproduce the problem with: ";
  if (UseValgrind)
    outs() << "valgrind ";
  outs() << "opt " << Filename;
  for (unsigned i = 0, e = PluginLoader::getNumPlugins(); i != e; ++i) {
    outs() << " -load " << PluginLoader::getPlugin(i);
  }
  outs() << " " << getPassesString(PassesToRun) << "\n";
}

cl::opt<bool> SilencePasses(
    "silence-passes",
    cl::desc("Suppress output of running passes (both stdout and stderr)"));

static cl::list<std::string> OptArgs("opt-args", cl::Positional,
                                     cl::desc("<opt arguments>..."),
                                     cl::PositionalEatsArgs);

/// runPasses - Run the specified passes on Program, outputting a bitcode file
/// and writing the filename into OutputFile if successful.  If the
/// optimizations fail for some reason (optimizer crashes), return true,
/// otherwise return false.  If DeleteOutput is set to true, the bitcode is
/// deleted on success, and the filename string is undefined.  This prints to
/// outs() a single line message indicating whether compilation was successful
/// or failed.
///
bool BugDriver::runPasses(Module &Program,
                          const std::vector<std::string> &Passes,
                          std::string &OutputFilename, bool DeleteOutput,
                          bool Quiet, ArrayRef<std::string> ExtraArgs) const {
  // setup the output file name
  outs().flush();
  SmallString<128> UniqueFilename;
  std::error_code EC = sys::fs::createUniqueFile(
      OutputPrefix + "-output-%%%%%%%.bc", UniqueFilename);
  if (EC) {
    errs() << getToolName()
           << ": Error making unique filename: " << EC.message() << "\n";
    return true;
  }
  OutputFilename = std::string(UniqueFilename);

  // set up the input file name
  Expected<sys::fs::TempFile> Temp =
      sys::fs::TempFile::create(OutputPrefix + "-input-%%%%%%%.bc");
  if (!Temp) {
    errs() << getToolName()
           << ": Error making unique filename: " << toString(Temp.takeError())
           << "\n";
    return true;
  }
  DiscardTemp Discard{*Temp};
  raw_fd_ostream OS(Temp->FD, /*shouldClose*/ false);

  WriteBitcodeToFile(Program, OS, PreserveBitcodeUseListOrder);
  OS.flush();
  if (OS.has_error()) {
    errs() << "Error writing bitcode file: " << Temp->TmpName << "\n";
    OS.clear_error();
    return true;
  }

  std::string tool = OptCmd;
  if (OptCmd.empty()) {
    if (ErrorOr<std::string> Path =
            FindProgramByName("opt", getToolName(), &OutputPrefix))
      tool = *Path;
    else
      errs() << Path.getError().message() << "\n";
  }
  if (tool.empty()) {
    errs() << "Cannot find `opt' in PATH!\n";
    return true;
  }
  if (!sys::fs::exists(tool)) {
    errs() << "Specified `opt' binary does not exist: " << tool << "\n";
    return true;
  }

  std::string Prog;
  if (UseValgrind) {
    if (ErrorOr<std::string> Path = sys::findProgramByName("valgrind"))
      Prog = *Path;
    else
      errs() << Path.getError().message() << "\n";
  } else
    Prog = tool;
  if (Prog.empty()) {
    errs() << "Cannot find `valgrind' in PATH!\n";
    return true;
  }

  // setup the child process' arguments
  SmallVector<StringRef, 8> Args;
  if (UseValgrind) {
    Args.push_back("valgrind");
    Args.push_back("--error-exitcode=1");
    Args.push_back("-q");
    Args.push_back(tool);
  } else
    Args.push_back(tool);

  for (unsigned i = 0, e = OptArgs.size(); i != e; ++i)
    Args.push_back(OptArgs[i]);
  // Pin to legacy PM since bugpoint has lots of infra and hacks revolving
  // around the legacy PM.
  Args.push_back("-bugpoint-enable-legacy-pm");
  Args.push_back("-disable-symbolication");
  Args.push_back("-o");
  Args.push_back(OutputFilename);
  std::vector<std::string> pass_args;
  for (unsigned i = 0, e = PluginLoader::getNumPlugins(); i != e; ++i) {
    pass_args.push_back(std::string("-load"));
    pass_args.push_back(PluginLoader::getPlugin(i));
  }
  for (std::vector<std::string>::const_iterator I = Passes.begin(),
                                                E = Passes.end();
       I != E; ++I)
    pass_args.push_back(std::string("-") + (*I));
  for (std::vector<std::string>::const_iterator I = pass_args.begin(),
                                                E = pass_args.end();
       I != E; ++I)
    Args.push_back(*I);
  Args.push_back(Temp->TmpName);
  Args.append(ExtraArgs.begin(), ExtraArgs.end());

  LLVM_DEBUG(errs() << "\nAbout to run:\t";
             for (unsigned i = 0, e = Args.size() - 1; i != e; ++i) errs()
             << " " << Args[i];
             errs() << "\n";);

  std::optional<StringRef> Redirects[3] = {std::nullopt, std::nullopt,
                                           std::nullopt};
  // Redirect stdout and stderr to nowhere if SilencePasses is given.
  if (SilencePasses) {
    Redirects[1] = "";
    Redirects[2] = "";
  }

  std::string ErrMsg;
  int result = sys::ExecuteAndWait(Prog, Args, std::nullopt, Redirects, Timeout,
                                   MemoryLimit, &ErrMsg);

  // If we are supposed to delete the bitcode file or if the passes crashed,
  // remove it now.  This may fail if the file was never created, but that's ok.
  if (DeleteOutput || result != 0)
    sys::fs::remove(OutputFilename);

  if (!Quiet) {
    if (result == 0)
      outs() << "Success!\n";
    else if (result > 0)
      outs() << "Exited with error code '" << result << "'\n";
    else if (result < 0) {
      if (result == -1)
        outs() << "Execute failed: " << ErrMsg << "\n";
      else
        outs() << "Crashed: " << ErrMsg << "\n";
    }
    if (result & 0x01000000)
      outs() << "Dumped core\n";
  }

  // Was the child successful?
  return result != 0;
}

std::unique_ptr<Module>
BugDriver::runPassesOn(Module *M, const std::vector<std::string> &Passes,
                       ArrayRef<std::string> ExtraArgs) {
  std::string BitcodeResult;
  if (runPasses(*M, Passes, BitcodeResult, false /*delete*/, true /*quiet*/,
                ExtraArgs)) {
    return nullptr;
  }

  std::unique_ptr<Module> Ret = parseInputFile(BitcodeResult, Context);
  if (!Ret) {
    errs() << getToolName() << ": Error reading bitcode file '" << BitcodeResult
           << "'!\n";
    exit(1);
  }
  sys::fs::remove(BitcodeResult);
  return Ret;
}
