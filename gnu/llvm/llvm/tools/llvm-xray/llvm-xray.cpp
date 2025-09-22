//===- llvm-xray.cpp: XRay Tool Main Program ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the main entry point for the suite of XRay tools. All
// additional functionality are implemented as subcommands.
//
//===----------------------------------------------------------------------===//
//
// Basic usage:
//
//   llvm-xray [options] <subcommand> [subcommand-specific options]
//
#include "xray-registry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::xray;

int main(int argc, char *argv[]) {
  cl::ParseCommandLineOptions(argc, argv,
                              "XRay Tools\n\n"
                              "  This program consolidates multiple XRay trace "
                              "processing tools for convenient access.\n");
  for (auto *SC : cl::getRegisteredSubcommands()) {
    if (*SC) {
      // If no subcommand was provided, we need to explicitly check if this is
      // the top-level subcommand.
      if (SC == &cl::SubCommand::getTopLevel()) {
        cl::PrintHelpMessage(false, true);
        return 0;
      }
      if (auto C = dispatch(SC)) {
        ExitOnError("llvm-xray: ")(C());
        return 0;
      }
    }
  }

  // If all else fails, we still print the usage message.
  cl::PrintHelpMessage(false, true);
  return 0;
}
