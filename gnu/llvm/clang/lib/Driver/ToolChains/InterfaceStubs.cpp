//===---  InterfaceStubs.cpp - Base InterfaceStubs Implementations C++  ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InterfaceStubs.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "llvm/Support/Path.h"

namespace clang {
namespace driver {
namespace tools {
namespace ifstool {
void Merger::ConstructJob(Compilation &C, const JobAction &JA,
                          const InputInfo &Output, const InputInfoList &Inputs,
                          const llvm::opt::ArgList &Args,
                          const char *LinkingOutput) const {
  std::string Merger = getToolChain().GetProgramPath(getShortName());
  // TODO: Use IFS library directly in the future.
  llvm::opt::ArgStringList CmdArgs;
  CmdArgs.push_back("--input-format=IFS");
  const bool WriteBin = !Args.getLastArg(options::OPT_emit_merged_ifs);
  CmdArgs.push_back(WriteBin ? "--output-format=ELF" : "--output-format=IFS");
  CmdArgs.push_back("-o");

  // Normally we want to write to a side-car file ending in ".ifso" so for
  // example if `clang -emit-interface-stubs -shared -o libhello.so` were
  // invoked then we would like to get libhello.so and libhello.ifso. If the
  // stdout stream is given as the output file (ie `-o -`), that is the one
  // exception where we will just append to the same filestream as the normal
  // output.
  SmallString<128> OutputFilename(Output.getFilename());
  if (OutputFilename != "-") {
    if (Args.hasArg(options::OPT_shared))
      llvm::sys::path::replace_extension(OutputFilename,
                                         (WriteBin ? "ifso" : "ifs"));
    else
      OutputFilename += (WriteBin ? ".ifso" : ".ifs");
  }

  CmdArgs.push_back(Args.MakeArgString(OutputFilename.c_str()));

  // Here we append the input files. If the input files are object files, then
  // we look for .ifs files present in the same location as the object files.
  for (const auto &Input : Inputs) {
    if (!Input.isFilename())
      continue;
    SmallString<128> InputFilename(Input.getFilename());
    if (Input.getType() == types::TY_Object)
      llvm::sys::path::replace_extension(InputFilename, ".ifs");
    CmdArgs.push_back(Args.MakeArgString(InputFilename.c_str()));
  }

  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Args.MakeArgString(Merger), CmdArgs,
                                         Inputs, Output));
}
} // namespace ifstool
} // namespace tools
} // namespace driver
} // namespace clang
