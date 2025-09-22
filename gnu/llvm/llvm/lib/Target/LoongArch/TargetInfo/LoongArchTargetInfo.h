//===-- LoongArchTargetInfo.h - LoongArch Target Implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_TARGETINFO_LOONGARCHTARGETINFO_H
#define LLVM_LIB_TARGET_LOONGARCH_TARGETINFO_LOONGARCHTARGETINFO_H

namespace llvm {

class Target;

Target &getTheLoongArch32Target();
Target &getTheLoongArch64Target();

} // end namespace llvm

#endif // LLVM_LIB_TARGET_LOONGARCH_TARGETINFO_LOONGARCHTARGETINFO_H
