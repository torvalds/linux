//===- CompilationDatabase.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file contains implementations of the CompilationDatabase base class
//  and the FixedCompilationDatabase.
//
//  FIXME: Various functions that take a string &ErrorMessage should be upgraded
//  to Expected.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Job.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/CompilationDatabasePluginRegistry.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace clang;
using namespace tooling;

LLVM_INSTANTIATE_REGISTRY(CompilationDatabasePluginRegistry)

CompilationDatabase::~CompilationDatabase() = default;

std::unique_ptr<CompilationDatabase>
CompilationDatabase::loadFromDirectory(StringRef BuildDirectory,
                                       std::string &ErrorMessage) {
  llvm::raw_string_ostream ErrorStream(ErrorMessage);
  for (const CompilationDatabasePluginRegistry::entry &Database :
       CompilationDatabasePluginRegistry::entries()) {
    std::string DatabaseErrorMessage;
    std::unique_ptr<CompilationDatabasePlugin> Plugin(Database.instantiate());
    if (std::unique_ptr<CompilationDatabase> DB =
            Plugin->loadFromDirectory(BuildDirectory, DatabaseErrorMessage))
      return DB;
    ErrorStream << Database.getName() << ": " << DatabaseErrorMessage << "\n";
  }
  return nullptr;
}

static std::unique_ptr<CompilationDatabase>
findCompilationDatabaseFromDirectory(StringRef Directory,
                                     std::string &ErrorMessage) {
  std::stringstream ErrorStream;
  bool HasErrorMessage = false;
  while (!Directory.empty()) {
    std::string LoadErrorMessage;

    if (std::unique_ptr<CompilationDatabase> DB =
            CompilationDatabase::loadFromDirectory(Directory, LoadErrorMessage))
      return DB;

    if (!HasErrorMessage) {
      ErrorStream << "No compilation database found in " << Directory.str()
                  << " or any parent directory\n" << LoadErrorMessage;
      HasErrorMessage = true;
    }

    Directory = llvm::sys::path::parent_path(Directory);
  }
  ErrorMessage = ErrorStream.str();
  return nullptr;
}

std::unique_ptr<CompilationDatabase>
CompilationDatabase::autoDetectFromSource(StringRef SourceFile,
                                          std::string &ErrorMessage) {
  SmallString<1024> AbsolutePath(getAbsolutePath(SourceFile));
  StringRef Directory = llvm::sys::path::parent_path(AbsolutePath);

  std::unique_ptr<CompilationDatabase> DB =
      findCompilationDatabaseFromDirectory(Directory, ErrorMessage);

  if (!DB)
    ErrorMessage = ("Could not auto-detect compilation database for file \"" +
                   SourceFile + "\"\n" + ErrorMessage).str();
  return DB;
}

std::unique_ptr<CompilationDatabase>
CompilationDatabase::autoDetectFromDirectory(StringRef SourceDir,
                                             std::string &ErrorMessage) {
  SmallString<1024> AbsolutePath(getAbsolutePath(SourceDir));

  std::unique_ptr<CompilationDatabase> DB =
      findCompilationDatabaseFromDirectory(AbsolutePath, ErrorMessage);

  if (!DB)
    ErrorMessage = ("Could not auto-detect compilation database from directory \"" +
                   SourceDir + "\"\n" + ErrorMessage).str();
  return DB;
}

std::vector<CompileCommand> CompilationDatabase::getAllCompileCommands() const {
  std::vector<CompileCommand> Result;
  for (const auto &File : getAllFiles()) {
    auto C = getCompileCommands(File);
    std::move(C.begin(), C.end(), std::back_inserter(Result));
  }
  return Result;
}

CompilationDatabasePlugin::~CompilationDatabasePlugin() = default;

namespace {

// Helper for recursively searching through a chain of actions and collecting
// all inputs, direct and indirect, of compile jobs.
struct CompileJobAnalyzer {
  SmallVector<std::string, 2> Inputs;

  void run(const driver::Action *A) {
    runImpl(A, false);
  }

private:
  void runImpl(const driver::Action *A, bool Collect) {
    bool CollectChildren = Collect;
    switch (A->getKind()) {
    case driver::Action::CompileJobClass:
    case driver::Action::PrecompileJobClass:
      CollectChildren = true;
      break;

    case driver::Action::InputClass:
      if (Collect) {
        const auto *IA = cast<driver::InputAction>(A);
        Inputs.push_back(std::string(IA->getInputArg().getSpelling()));
      }
      break;

    default:
      // Don't care about others
      break;
    }

    for (const driver::Action *AI : A->inputs())
      runImpl(AI, CollectChildren);
  }
};

// Special DiagnosticConsumer that looks for warn_drv_input_file_unused
// diagnostics from the driver and collects the option strings for those unused
// options.
class UnusedInputDiagConsumer : public DiagnosticConsumer {
public:
  UnusedInputDiagConsumer(DiagnosticConsumer &Other) : Other(Other) {}

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    if (Info.getID() == diag::warn_drv_input_file_unused) {
      // Arg 1 for this diagnostic is the option that didn't get used.
      UnusedInputs.push_back(Info.getArgStdStr(0));
    } else if (DiagLevel >= DiagnosticsEngine::Error) {
      // If driver failed to create compilation object, show the diagnostics
      // to user.
      Other.HandleDiagnostic(DiagLevel, Info);
    }
  }

  DiagnosticConsumer &Other;
  SmallVector<std::string, 2> UnusedInputs;
};

// Filter of tools unused flags such as -no-integrated-as and -Wa,*.
// They are not used for syntax checking, and could confuse targets
// which don't support these options.
struct FilterUnusedFlags {
  bool operator() (StringRef S) {
    return (S == "-no-integrated-as") || S.starts_with("-Wa,");
  }
};

std::string GetClangToolCommand() {
  static int Dummy;
  std::string ClangExecutable =
      llvm::sys::fs::getMainExecutable("clang", (void *)&Dummy);
  SmallString<128> ClangToolPath;
  ClangToolPath = llvm::sys::path::parent_path(ClangExecutable);
  llvm::sys::path::append(ClangToolPath, "clang-tool");
  return std::string(ClangToolPath);
}

} // namespace

/// Strips any positional args and possible argv[0] from a command-line
/// provided by the user to construct a FixedCompilationDatabase.
///
/// FixedCompilationDatabase requires a command line to be in this format as it
/// constructs the command line for each file by appending the name of the file
/// to be compiled. FixedCompilationDatabase also adds its own argv[0] to the
/// start of the command line although its value is not important as it's just
/// ignored by the Driver invoked by the ClangTool using the
/// FixedCompilationDatabase.
///
/// FIXME: This functionality should probably be made available by
/// clang::driver::Driver although what the interface should look like is not
/// clear.
///
/// \param[in] Args Args as provided by the user.
/// \return Resulting stripped command line.
///          \li true if successful.
///          \li false if \c Args cannot be used for compilation jobs (e.g.
///          contains an option like -E or -version).
static bool stripPositionalArgs(std::vector<const char *> Args,
                                std::vector<std::string> &Result,
                                std::string &ErrorMsg) {
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  llvm::raw_string_ostream Output(ErrorMsg);
  TextDiagnosticPrinter DiagnosticPrinter(Output, &*DiagOpts);
  UnusedInputDiagConsumer DiagClient(DiagnosticPrinter);
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs()),
      &*DiagOpts, &DiagClient, false);

  // The clang executable path isn't required since the jobs the driver builds
  // will not be executed.
  std::unique_ptr<driver::Driver> NewDriver(new driver::Driver(
      /* ClangExecutable= */ "", llvm::sys::getDefaultTargetTriple(),
      Diagnostics));
  NewDriver->setCheckInputsExist(false);

  // This becomes the new argv[0]. The value is used to detect libc++ include
  // dirs on Mac, it isn't used for other platforms.
  std::string Argv0 = GetClangToolCommand();
  Args.insert(Args.begin(), Argv0.c_str());

  // By adding -c, we force the driver to treat compilation as the last phase.
  // It will then issue warnings via Diagnostics about un-used options that
  // would have been used for linking. If the user provided a compiler name as
  // the original argv[0], this will be treated as a linker input thanks to
  // insertng a new argv[0] above. All un-used options get collected by
  // UnusedInputdiagConsumer and get stripped out later.
  Args.push_back("-c");

  // Put a dummy C++ file on to ensure there's at least one compile job for the
  // driver to construct. If the user specified some other argument that
  // prevents compilation, e.g. -E or something like -version, we may still end
  // up with no jobs but then this is the user's fault.
  Args.push_back("placeholder.cpp");

  llvm::erase_if(Args, FilterUnusedFlags());

  const std::unique_ptr<driver::Compilation> Compilation(
      NewDriver->BuildCompilation(Args));
  if (!Compilation)
    return false;

  const driver::JobList &Jobs = Compilation->getJobs();

  CompileJobAnalyzer CompileAnalyzer;

  for (const auto &Cmd : Jobs) {
    // Collect only for Assemble, Backend, and Compile jobs. If we do all jobs
    // we get duplicates since Link jobs point to Assemble jobs as inputs.
    // -flto* flags make the BackendJobClass, which still needs analyzer.
    if (Cmd.getSource().getKind() == driver::Action::AssembleJobClass ||
        Cmd.getSource().getKind() == driver::Action::BackendJobClass ||
        Cmd.getSource().getKind() == driver::Action::CompileJobClass ||
        Cmd.getSource().getKind() == driver::Action::PrecompileJobClass) {
      CompileAnalyzer.run(&Cmd.getSource());
    }
  }

  if (CompileAnalyzer.Inputs.empty()) {
    ErrorMsg = "warning: no compile jobs found\n";
    return false;
  }

  // Remove all compilation input files from the command line and inputs deemed
  // unused for compilation. This is necessary so that getCompileCommands() can
  // construct a command line for each file.
  std::vector<const char *>::iterator End =
      llvm::remove_if(Args, [&](StringRef S) {
        return llvm::is_contained(CompileAnalyzer.Inputs, S) ||
               llvm::is_contained(DiagClient.UnusedInputs, S);
      });
  // Remove the -c add above as well. It will be at the end right now.
  assert(strcmp(*(End - 1), "-c") == 0);
  --End;

  Result = std::vector<std::string>(Args.begin() + 1, End);
  return true;
}

std::unique_ptr<FixedCompilationDatabase>
FixedCompilationDatabase::loadFromCommandLine(int &Argc,
                                              const char *const *Argv,
                                              std::string &ErrorMsg,
                                              const Twine &Directory) {
  ErrorMsg.clear();
  if (Argc == 0)
    return nullptr;
  const char *const *DoubleDash = std::find(Argv, Argv + Argc, StringRef("--"));
  if (DoubleDash == Argv + Argc)
    return nullptr;
  std::vector<const char *> CommandLine(DoubleDash + 1, Argv + Argc);
  Argc = DoubleDash - Argv;

  std::vector<std::string> StrippedArgs;
  if (!stripPositionalArgs(CommandLine, StrippedArgs, ErrorMsg))
    return nullptr;
  return std::make_unique<FixedCompilationDatabase>(Directory, StrippedArgs);
}

std::unique_ptr<FixedCompilationDatabase>
FixedCompilationDatabase::loadFromFile(StringRef Path, std::string &ErrorMsg) {
  ErrorMsg.clear();
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      llvm::MemoryBuffer::getFile(Path);
  if (std::error_code Result = File.getError()) {
    ErrorMsg = "Error while opening fixed database: " + Result.message();
    return nullptr;
  }
  return loadFromBuffer(llvm::sys::path::parent_path(Path),
                        (*File)->getBuffer(), ErrorMsg);
}

std::unique_ptr<FixedCompilationDatabase>
FixedCompilationDatabase::loadFromBuffer(StringRef Directory, StringRef Data,
                                         std::string &ErrorMsg) {
  ErrorMsg.clear();
  std::vector<std::string> Args;
  StringRef Line;
  while (!Data.empty()) {
    std::tie(Line, Data) = Data.split('\n');
    // Stray whitespace is almost certainly unintended.
    Line = Line.trim();
    if (!Line.empty())
      Args.push_back(Line.str());
  }
  return std::make_unique<FixedCompilationDatabase>(Directory, std::move(Args));
}

FixedCompilationDatabase::FixedCompilationDatabase(
    const Twine &Directory, ArrayRef<std::string> CommandLine) {
  std::vector<std::string> ToolCommandLine(1, GetClangToolCommand());
  ToolCommandLine.insert(ToolCommandLine.end(),
                         CommandLine.begin(), CommandLine.end());
  CompileCommands.emplace_back(Directory, StringRef(),
                               std::move(ToolCommandLine),
                               StringRef());
}

std::vector<CompileCommand>
FixedCompilationDatabase::getCompileCommands(StringRef FilePath) const {
  std::vector<CompileCommand> Result(CompileCommands);
  Result[0].CommandLine.push_back(std::string(FilePath));
  Result[0].Filename = std::string(FilePath);
  return Result;
}

namespace {

class FixedCompilationDatabasePlugin : public CompilationDatabasePlugin {
  std::unique_ptr<CompilationDatabase>
  loadFromDirectory(StringRef Directory, std::string &ErrorMessage) override {
    SmallString<1024> DatabasePath(Directory);
    llvm::sys::path::append(DatabasePath, "compile_flags.txt");
    return FixedCompilationDatabase::loadFromFile(DatabasePath, ErrorMessage);
  }
};

} // namespace

static CompilationDatabasePluginRegistry::Add<FixedCompilationDatabasePlugin>
X("fixed-compilation-database", "Reads plain-text flags file");

namespace clang {
namespace tooling {

// This anchor is used to force the linker to link in the generated object file
// and thus register the JSONCompilationDatabasePlugin.
extern volatile int JSONAnchorSource;
static int LLVM_ATTRIBUTE_UNUSED JSONAnchorDest = JSONAnchorSource;

} // namespace tooling
} // namespace clang
