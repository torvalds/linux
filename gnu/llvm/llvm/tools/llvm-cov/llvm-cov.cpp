//===- llvm-cov.cpp - LLVM coverage tool ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-cov is a command line tools to analyze and report coverage information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

/// The main entry point for the 'show' subcommand.
int showMain(int argc, const char *argv[]);

/// The main entry point for the 'report' subcommand.
int reportMain(int argc, const char *argv[]);

/// The main entry point for the 'export' subcommand.
int exportMain(int argc, const char *argv[]);

/// The main entry point for the 'convert-for-testing' subcommand.
int convertForTestingMain(int argc, const char *argv[]);

/// The main entry point for the gcov compatible coverage tool.
int gcovMain(int argc, const char *argv[]);

/// Top level help.
static int helpMain(int argc, const char *argv[]) {
  errs() << "Usage: llvm-cov {export|gcov|report|show} [OPTION]...\n\n"
         << "Shows code coverage information.\n\n"
         << "Subcommands:\n"
         << "  export: Export instrprof file to structured format.\n"
         << "  gcov:   Work with the gcov format.\n"
         << "  report: Summarize instrprof style coverage information.\n"
         << "  show:   Annotate source files using instrprof style coverage.\n";

  return 0;
}

/// Top level version information.
static int versionMain(int argc, const char *argv[]) {
  cl::PrintVersionMessage();
  return 0;
}

int main(int argc, const char **argv) {
  InitLLVM X(argc, argv);

  // If argv[0] is or ends with 'gcov', always be gcov compatible
  if (sys::path::stem(argv[0]).ends_with_insensitive("gcov"))
    return gcovMain(argc, argv);

  // Check if we are invoking a specific tool command.
  if (argc > 1) {
    typedef int (*MainFunction)(int, const char *[]);
    MainFunction Func = StringSwitch<MainFunction>(argv[1])
                            .Case("convert-for-testing", convertForTestingMain)
                            .Case("export", exportMain)
                            .Case("gcov", gcovMain)
                            .Case("report", reportMain)
                            .Case("show", showMain)
                            .Cases("-h", "-help", "--help", helpMain)
                            .Cases("-version", "--version", versionMain)
                            .Default(nullptr);

    if (Func) {
      std::string Invocation = std::string(argv[0]) + " " + argv[1];
      argv[1] = Invocation.c_str();
      return Func(argc - 1, argv + 1);
    }
  }

  if (argc > 1) {
    if (sys::Process::StandardErrHasColors())
      errs().changeColor(raw_ostream::RED);
    errs() << "Unrecognized command: " << argv[1] << ".\n\n";
    if (sys::Process::StandardErrHasColors())
      errs().resetColor();
  }
  helpMain(argc, argv);
  return 1;
}
