//===--- HIPUtility.h - Common HIP Tool Chain Utilities ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HIPUTILITY_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HIPUTILITY_H

#include "clang/Driver/Tool.h"

namespace clang {
namespace driver {
namespace tools {
namespace HIP {

// Construct command for creating HIP fatbin.
void constructHIPFatbinCommand(Compilation &C, const JobAction &JA,
                               StringRef OutputFileName,
                               const InputInfoList &Inputs,
                               const llvm::opt::ArgList &TCArgs, const Tool &T);

// Construct command for creating Object from HIP fatbin.
void constructGenerateObjFileFromHIPFatBinary(
    Compilation &C, const InputInfo &Output, const InputInfoList &Inputs,
    const llvm::opt::ArgList &Args, const JobAction &JA, const Tool &T);

} // namespace HIP
} // namespace tools
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HIPUTILITY_H
