//===--- TCE.h - TCE Tool and ToolChain Implementations ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_TCE_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_TCE_H

#include "clang/Driver/Driver.h"
#include "clang/Driver/ToolChain.h"
#include <set>

namespace clang {
namespace driver {
namespace toolchains {

/// TCEToolChain - A tool chain using the llvm bitcode tools to perform
/// all subcommands. See http://tce.cs.tut.fi for our peculiar target.
class LLVM_LIBRARY_VISIBILITY TCEToolChain : public ToolChain {
public:
  TCEToolChain(const Driver &D, const llvm::Triple &Triple,
               const llvm::opt::ArgList &Args);
  ~TCEToolChain() override;

  bool IsMathErrnoDefault() const override;
  bool isPICDefault() const override;
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override;
  bool isPICDefaultForced() const override;
};

/// Toolchain for little endian TCE cores.
class LLVM_LIBRARY_VISIBILITY TCELEToolChain : public TCEToolChain {
public:
  TCELEToolChain(const Driver &D, const llvm::Triple &Triple,
                 const llvm::opt::ArgList &Args);
  ~TCELEToolChain() override;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_TCE_H
