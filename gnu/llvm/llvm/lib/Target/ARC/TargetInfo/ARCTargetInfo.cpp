//===- ARCTargetInfo.cpp - ARC Target Implementation ----------- *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/ARCTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheARCTarget() {
  static Target TheARCTarget;
  return TheARCTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeARCTargetInfo() {
  RegisterTarget<Triple::arc> X(getTheARCTarget(), "arc", "ARC", "ARC");
}
