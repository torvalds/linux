//===--- TCE.cpp - TCE ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TCE.h"
#include "CommonArgs.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// TCEToolChain - A tool chain using the llvm bitcode tools to perform
/// all subcommands. See http://tce.cs.tut.fi for our peculiar target.
/// Currently does not support anything else but compilation.

TCEToolChain::TCEToolChain(const Driver &D, const llvm::Triple &Triple,
                           const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  // Path mangling to find libexec
  std::string Path(getDriver().Dir);

  Path += "/../libexec";
  getProgramPaths().push_back(Path);
}

TCEToolChain::~TCEToolChain() {}

bool TCEToolChain::IsMathErrnoDefault() const { return true; }

bool TCEToolChain::isPICDefault() const { return false; }

bool TCEToolChain::isPIEDefault(const llvm::opt::ArgList &Args) const {
  return false;
}

bool TCEToolChain::isPICDefaultForced() const { return false; }

TCELEToolChain::TCELEToolChain(const Driver &D, const llvm::Triple& Triple,
                               const ArgList &Args)
  : TCEToolChain(D, Triple, Args) {
}

TCELEToolChain::~TCELEToolChain() {}
