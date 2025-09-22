//===-- APINotesTest.cpp - API Notes Testing Tool ------------------ C++ --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/APINotes/APINotesYAMLCompiler.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

static llvm::cl::list<std::string> APINotes(llvm::cl::Positional,
                                            llvm::cl::desc("[<apinotes> ...]"),
                                            llvm::cl::Required);

static llvm::cl::opt<std::string>
    OutputFileName("o", llvm::cl::desc("output filename"),
                   llvm::cl::value_desc("filename"), llvm::cl::init("-"));

int main(int argc, const char **argv) {
  const bool DisableCrashReporting = true;
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0], DisableCrashReporting);
  llvm::cl::ParseCommandLineOptions(argc, argv);

  auto Error = [](const llvm::Twine &Msg) {
    llvm::WithColor::error(llvm::errs(), "apinotes-test") << Msg << '\n';
  };

  std::error_code EC;
  auto Out = std::make_unique<llvm::ToolOutputFile>(OutputFileName, EC,
                                                    llvm::sys::fs::OF_None);
  if (EC) {
    Error("failed to open '" + OutputFileName + "': " + EC.message());
    return EXIT_FAILURE;
  }

  for (const std::string &Notes : APINotes) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> NotesOrError =
        llvm::MemoryBuffer::getFileOrSTDIN(Notes);
    if (std::error_code EC = NotesOrError.getError()) {
      llvm::errs() << EC.message() << '\n';
      return EXIT_FAILURE;
    }

    clang::api_notes::parseAndDumpAPINotes((*NotesOrError)->getBuffer(),
                                           Out->os());
  }

  return EXIT_SUCCESS;
}
