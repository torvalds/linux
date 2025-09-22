//===--- RISCV.h - RISC-V-specific Tool Helpers -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_RISCV_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_RISCV_H

#include "clang/Driver/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include <string>
#include <vector>

namespace clang {
namespace driver {
namespace tools {
namespace riscv {
void getRISCVTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                            const llvm::opt::ArgList &Args,
                            std::vector<llvm::StringRef> &Features);
StringRef getRISCVABI(const llvm::opt::ArgList &Args,
                      const llvm::Triple &Triple);
std::string getRISCVArch(const llvm::opt::ArgList &Args,
                         const llvm::Triple &Triple);
std::string getRISCVTargetCPU(const llvm::opt::ArgList &Args,
                              const llvm::Triple &Triple);
} // end namespace riscv
} // namespace tools
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_RISCV_H
