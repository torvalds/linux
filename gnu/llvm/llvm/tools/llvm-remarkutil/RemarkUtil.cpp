//===--------- llvm-remarkutil/RemarkUtil.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// Utility for remark files.
//===----------------------------------------------------------------------===//

#include "RemarkUtilRegistry.h"
#include "llvm/Support/InitLLVM.h"

using namespace llvm;
using namespace llvm::remarkutil;
ExitOnError ExitOnErr;

static Error handleSubOptions() {
  for (auto *SC : cl::getRegisteredSubcommands()) {
    if (*SC) {
      // If no subcommand was provided, we need to explicitly check if this is
      // the top-level subcommand.
      if (SC == &cl::SubCommand::getTopLevel())
        break;
      if (auto C = dispatch(SC)) {
        return C();
      }
    }
  }

  return make_error<StringError>(
      "Please specify a subcommand. (See -help for options)",
      inconvertibleErrorCode());
}

int main(int argc, char *argv[]) {
  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv, "Remark file utilities\n");
  ExitOnErr.setBanner(std::string(argv[0]) + ": error: ");
  ExitOnErr(handleSubOptions());
}
