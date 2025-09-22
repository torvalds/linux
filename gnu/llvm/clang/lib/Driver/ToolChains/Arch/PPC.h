//===--- PPC.h - PPC-specific Tool Helpers ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_PPC_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_PPC_H

#include "clang/Driver/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include <string>
#include <vector>

namespace clang {
namespace driver {
namespace tools {
namespace ppc {

bool hasPPCAbiArg(const llvm::opt::ArgList &Args, const char *Value);

enum class FloatABI {
  Invalid,
  Soft,
  Hard,
};

enum class ReadGOTPtrMode {
  Bss,
  SecurePlt,
};

FloatABI getPPCFloatABI(const Driver &D, const llvm::opt::ArgList &Args);

std::string getPPCTargetCPU(const Driver &D, const llvm::opt::ArgList &Args,
                            const llvm::Triple &T);
std::string getPPCTuneCPU(const llvm::opt::ArgList &Args,
                          const llvm::Triple &T);
const char *getPPCAsmModeForCPU(StringRef Name);
ReadGOTPtrMode getPPCReadGOTPtrMode(const Driver &D, const llvm::Triple &Triple,
                                    const llvm::opt::ArgList &Args);

void getPPCTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                          const llvm::opt::ArgList &Args,
                          std::vector<llvm::StringRef> &Features);

} // end namespace ppc
} // end namespace target
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_PPC_H
