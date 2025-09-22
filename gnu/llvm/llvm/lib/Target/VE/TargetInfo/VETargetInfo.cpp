//===-- VETargetInfo.cpp - VE Target Implementation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/VETargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheVETarget() {
  static Target TheVETarget;
  return TheVETarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVETargetInfo() {
  RegisterTarget<Triple::ve, /*HasJIT=*/false> X(getTheVETarget(), "ve",
                                                 "VE", "VE");
}
