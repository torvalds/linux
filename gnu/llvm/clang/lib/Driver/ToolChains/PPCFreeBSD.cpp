//===-- PPCFreeBSD.cpp - PowerPC ToolChain Implementations ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCFreeBSD.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "llvm/Support/Path.h"

using namespace clang::driver::toolchains;
using namespace llvm::opt;

void PPCFreeBSDToolChain::AddClangSystemIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  if (!DriverArgs.hasArg(clang::driver::options::OPT_nostdinc) &&
      !DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    const Driver &D = getDriver();
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include", "ppc_wrappers");
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  FreeBSD::AddClangSystemIncludeArgs(DriverArgs, CC1Args);
}
