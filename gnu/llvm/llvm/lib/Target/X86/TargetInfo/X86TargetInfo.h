//===-- X86TargetInfo.h - X86 Target Implementation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_TARGETINFO_X86TARGETINFO_H
#define LLVM_LIB_TARGET_X86_TARGETINFO_X86TARGETINFO_H

namespace llvm {

class Target;

Target &getTheX86_32Target();
Target &getTheX86_64Target();

}

#endif // LLVM_LIB_TARGET_X86_TARGETINFO_X86TARGETINFO_H
