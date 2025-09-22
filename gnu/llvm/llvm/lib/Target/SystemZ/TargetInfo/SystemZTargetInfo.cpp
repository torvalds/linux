//===-- SystemZTargetInfo.cpp - SystemZ target implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/SystemZTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheSystemZTarget() {
  static Target TheSystemZTarget;
  return TheSystemZTarget;
}

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSystemZTargetInfo() {
  RegisterTarget<Triple::systemz, /*HasJIT=*/true> X(
      getTheSystemZTarget(), "systemz", "SystemZ", "SystemZ");
}
