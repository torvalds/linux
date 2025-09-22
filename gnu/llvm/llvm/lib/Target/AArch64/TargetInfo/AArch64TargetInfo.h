//===-- AArch64TargetInfo.h - AArch64 Target Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_TARGETINFO_AARCH64TARGETINFO_H
#define LLVM_LIB_TARGET_AARCH64_TARGETINFO_AARCH64TARGETINFO_H

namespace llvm {

class Target;

Target &getTheAArch64leTarget();
Target &getTheAArch64beTarget();
Target &getTheAArch64_32Target();
Target &getTheARM64Target();
Target &getTheARM64_32Target();

} // namespace llvm

#endif // LLVM_LIB_TARGET_AARCH64_TARGETINFO_AARCH64TARGETINFO_H
