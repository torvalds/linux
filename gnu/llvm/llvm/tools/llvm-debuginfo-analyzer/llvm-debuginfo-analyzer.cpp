//===-- llvm-debuginfo-analyzer.cpp - LLVM Debug info analysis utility ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that displays the logical view for the debug
// information.
//
//===----------------------------------------------------------------------===//

#include "Options.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/LVReaderHandler.h"
#include "llvm/Support/COM.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace logicalview;
using namespace cmdline;

/// Create formatted StringError object.
static StringRef ToolName = "llvm-debuginfo-analyzer";
template <typename... Ts>
static void error(std::error_code EC, char const *Fmt, const Ts &...Vals) {
  if (!EC)
    return;
  std::string Buffer;
  raw_string_ostream Stream(Buffer);
  Stream << format(Fmt, Vals...);
  WithColor::error(errs(), ToolName) << Stream.str() << "\n";
  exit(1);
}

static void error(Error EC) {
  if (!EC)
    return;
  handleAllErrors(std::move(EC), [&](const ErrorInfoBase &EI) {
    errs() << "\n";
    WithColor::error(errs(), ToolName) << EI.message() << ".\n";
    exit(1);
  });
}

/// If the input path is a .dSYM bundle (as created by the dsymutil tool),
/// replace it with individual entries for each of the object files inside the
/// bundle otherwise return the input path.
static std::vector<std::string> expandBundle(const std::string &InputPath) {
  std::vector<std::string> BundlePaths;
  SmallString<256> BundlePath(InputPath);
  // Normalize input path. This is necessary to accept `bundle.dSYM/`.
  sys::path::remove_dots(BundlePath);
  // Manually open up the bundle to avoid introducing additional dependencies.
  if (sys::fs::is_directory(BundlePath) &&
      sys::path::extension(BundlePath) == ".dSYM") {
    std::error_code EC;
    sys::path::append(BundlePath, "Contents", "Resources", "DWARF");
    for (sys::fs::directory_iterator Dir(BundlePath, EC), DirEnd;
         Dir != DirEnd && !EC; Dir.increment(EC)) {
      const std::string &Path = Dir->path();
      sys::fs::file_status Status;
      EC = sys::fs::status(Path, Status);
      error(EC, "%s", Path.c_str());
      switch (Status.type()) {
      case sys::fs::file_type::regular_file:
      case sys::fs::file_type::symlink_file:
      case sys::fs::file_type::type_unknown:
        BundlePaths.push_back(Path);
        break;
      default: /*ignore*/;
      }
    }
  }
  if (BundlePaths.empty())
    BundlePaths.push_back(InputPath);
  return BundlePaths;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Initialize targets and assembly printers/parsers.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  InitializeAllDisassemblers();

  llvm::sys::InitializeCOMRAII COM(llvm::sys::COMThreadingMode::MultiThreaded);

  cl::extrahelp HelpResponse(
      "\nPass @FILE as argument to read options from FILE.\n");

  cl::HideUnrelatedOptions(
      {&AttributeCategory, &CompareCategory, &InternalCategory, &OutputCategory,
       &PrintCategory, &ReportCategory, &SelectCategory, &WarningCategory});
  cl::ParseCommandLineOptions(argc, argv,
                              "Printing a logical representation of low-level "
                              "debug information.\n");
  cl::PrintOptionValues();

  std::error_code EC;
  ToolOutputFile OutputFile(OutputFilename, EC, sys::fs::OF_None);
  error(EC, "Unable to open output file %s", OutputFilename.c_str());
  // Don't remove output file if we exit with an error.
  OutputFile.keep();

  // Defaults to a.out if no filenames specified.
  if (InputFilenames.empty())
    InputFilenames.push_back("a.out");

  // Expand any .dSYM bundles to the individual object files contained therein.
  std::vector<std::string> Objects;
  for (const std::string &Filename : InputFilenames) {
    std::vector<std::string> Objs = expandBundle(Filename);
    Objects.insert(Objects.end(), Objs.begin(), Objs.end());
  }

  propagateOptions();
  ScopedPrinter W(OutputFile.os());
  LVReaderHandler ReaderHandler(Objects, W, ReaderOptions);

  // Print the command line.
  if (options().getInternalCmdline()) {
    raw_ostream &Stream = W.getOStream();
    Stream << "\nCommand line:\n";
    for (int Index = 0; Index < argc; ++Index)
      Stream << "  " << argv[Index] << "\n";
    Stream << "\n";
  }

  // Create readers and perform requested tasks on them.
  if (Error Err = ReaderHandler.process())
    error(std::move(Err));

  return EXIT_SUCCESS;
}
