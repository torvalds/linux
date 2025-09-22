//===-- CSKYTargetInfo.cpp - CSKY Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_TARGETINFO_CSKYTARGETINFO_H
#define LLVM_LIB_TARGET_CSKY_TARGETINFO_CSKYTARGETINFO_H

namespace llvm {

class Target;

Target &getTheCSKYTarget();

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_TARGETINFO_CSKYTARGETINFO_H
