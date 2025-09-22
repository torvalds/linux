//===-- LanaiTargetInfo.cpp - Lanai Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/LanaiTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheLanaiTarget() {
  static Target TheLanaiTarget;
  return TheLanaiTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLanaiTargetInfo() {
  RegisterTarget<Triple::lanai> X(getTheLanaiTarget(), "lanai", "Lanai",
                                  "Lanai");
}
