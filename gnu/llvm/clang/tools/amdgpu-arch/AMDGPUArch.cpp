//===- AMDGPUArch.cpp - list AMDGPU installed ----------*- C++ -*---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a tool for detecting name of AMDGPU installed in system.
// This tool is used by AMDGPU OpenMP and HIP driver.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Version.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> Help("h", cl::desc("Alias for -help"), cl::Hidden);

// Mark all our options with this category.
static cl::OptionCategory AMDGPUArchCategory("amdgpu-arch options");

static void PrintVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("amdgpu-arch") << '\n';
}

int printGPUsByHSA();
int printGPUsByHIP();

int main(int argc, char *argv[]) {
  cl::HideUnrelatedOptions(AMDGPUArchCategory);

  cl::SetVersionPrinter(PrintVersion);
  cl::ParseCommandLineOptions(
      argc, argv,
      "A tool to detect the presence of AMDGPU devices on the system. \n\n"
      "The tool will output each detected GPU architecture separated by a\n"
      "newline character. If multiple GPUs of the same architecture are found\n"
      "a string will be printed for each\n");

  if (Help) {
    cl::PrintHelpMessage();
    return 0;
  }

#ifndef _WIN32
  if (!printGPUsByHSA())
    return 0;
#endif

  return printGPUsByHIP();
}
